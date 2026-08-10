# 02 — Balaji V1 Field Service Guide

Scope: the one production Covio oil-flow meter deployed at Balaji. This
guide reflects the system as it actually is after the freeze remediation —
it does not describe capabilities the canonical architecture designs for
commercial rollout but that don't exist yet (bulk provisioning, remote
Wi-Fi/key reconfiguration, an OTA-trigger button in Device Manager). Where a
procedure requires physical USB access, that's stated plainly — it is the
correct, deliberate security posture for this device (see Section 9 of the
Product Readiness Review), not a gap.

**Before touching anything:** connect via USB, open a serial terminal at
115200 baud. Type `help` for the full command list. Type `show` at any time
to see current identity, config, calibration, and diagnostic counters
without changing anything.

---

## 1. Replacing the device

Use this when the physical unit has failed and is being swapped for a new
one.

1. **Record the old device's state before disconnecting it**, if at all
   possible: run `show` and note `device_id`, `calib` (K/density/Tref), and
   the `diag` line. This is your record of what the new unit should be
   configured to match.
2. **Physically swap the hardware** (power off old unit, install new unit,
   power on).
3. **Provision the new device** — if it has never been configured before,
   it boots into SoftAP mode automatically (`Covio-Setup-<id>`). Follow the
   Device Manager provisioning wizard (connect to that network, run the
   wizard, enter the same server URL, a **new** API key issued for this
   device — see Section 3 below, never reuse the old device's key — and
   Balaji's Wi-Fi credentials).
4. **Set calibration** via the serial console (`set` commands don't cover
   calibration directly — calibration is server-authoritative; confirm the
   server has this device's correct K-factor/density/Tref configured, then
   let the device pick it up on its next `pollConfig()` cycle, or power-cycle
   to force an earlier check).
5. **Validate the new device's accuracy** before trusting its data — see
   Section 4 below (`scripts/validate_meter_calibration.py`). Do not skip
   this: a replacement meter is a new physical sensor, and this project's
   own audit trail already flagged that sensor accuracy had never been
   validated against real flow.
6. **Confirm health**: watch `show` and Device Manager's Live Monitor for a
   few sync cycles — `acked_seq` advancing, `push_fail_count`/`crash_streak`
   at 0, no `SENSOR_STOPPED`/`REBOOT_LOOP` alarm in `/api/v1/health`.

## 2. Changing Wi-Fi

There is currently no remote way to do this — the device must be reachable
by USB, or must be sent back into SoftAP provisioning mode.

- **If the device is reachable via USB:** connect, run `provision` at the
  serial console. This re-enters AP-mode setup on the next boot **without**
  a factory reset — existing server URL, API key, and all queue/totalizer
  data are untouched. Then follow the Device Manager Wi-Fi wizard as in a
  fresh commissioning.
- **If the device is not reachable via USB** (e.g. it's already stuck
  offline because the old Wi-Fi network is gone): it will automatically
  fall back to SoftAP mode on its own after `AP_FALLBACK_TIMEOUT_MS`
  (15 seconds) of failing to join the configured network on a fresh boot —
  power-cycle it and it will offer its setup network. No USB access is
  required for this specific case, only physical power access.
- Do **not** use `factory` for a Wi-Fi change alone — that wipes
  calibration cache, API key, and logical ID too (see Section 6). Use
  `provision` instead, which is scoped to exactly this.

## 3. Rotating the API key

1. On the ERP/backend side, issue a new key for this device (the
   `/admin/devices/<id>/rotate-key` endpoint, an operator/admin action —
   outside this device's own scope).
2. Connect via USB, run: `set key <new-key>` at the serial console. This
   takes effect on the next push cycle; no reboot required (though `reboot`
   is harmless if you want to confirm cleanly).
3. **Verify the rotation took**, without ever typing the raw key back out
   loud: run `show` and note the new `fingerprint=...` value. Compare it
   against the fingerprint of the key you just issued using
   `scripts/verify_api_key_fingerprint.py` (reads the key from a hidden
   prompt or stdin — never as a command-line argument, never printed back):
   ```
   python scripts/verify_api_key_fingerprint.py --expected-fingerprint <fp-from-show>
   ```
4. Confirm the device is still pushing successfully afterward (`show`'s
   `diag` line's `push_fails` should stop incrementing; Device Manager's
   Live Monitor should show a recent successful sync).

## 4. Recalibration

Calibration (K-factor/density/Tref) is **server-authoritative** — the
device only caches the last version it was told, for its own offline
display estimate. It does not have a local edit command for calibration
values themselves.

1. Update the calibration record on the server/ERP side for this device.
2. The device picks up the new version automatically on its next
   `pollConfig()` cycle (`CONFIG_POLL_MS`, 60 seconds) — no device-side
   action required. Confirm via `show`'s `calib` line (the `v` number
   should match the new version).
3. **Validate the new calibration against real, measured flow** before
   relying on it operationally:
   ```
   python scripts/validate_meter_calibration.py capture-before \
       --device-host covio-858428.local --session session.json
   # ... dispense a known volume through the meter using a calibrated reference ...
   python scripts/validate_meter_calibration.py capture-after \
       --device-host covio-858428.local --session session.json \
       --measured-volume-liters <actual measured volume> \
       --configured-k-factor <the K value from 'show'> \
       --technician "<your name>" --out report.json
   ```
   The tool reports the deviation percentage and a PASS/FAIL verdict
   (default tolerance ±2%). Keep `report.json` as the validation record.

## 5. Firmware update

There is no OTA-trigger button in Device Manager yet (this is deliberately
deferred fleet-scale tooling, per the canonical architecture document) —
today, an update is published server-side and the device picks it up on its
own poll cycle (`OTA_POLL_MS`, 5 minutes) with no device-side action
required, **provided** the release was signed with the production key (see
`Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md`).

1. Confirm the new release manifest is signed and published with
   `key_id: covio-prod-key-2026-07` (the device rejects anything else).
2. Watch `/api/v1/status`'s `ota` object (or `show` after the device's next
   poll) for `state` transitioning through the update; `last_reject_reason`/
   `last_auth_reject_reason` will explain a rejection if one occurs (wrong
   hardware compat, downgrade attempt, bad signature, expired manifest).
3. After the device reboots into the new image, confirm
   `ota.state == "confirmed"` (not stuck at `pending_verify`) once it has
   proven a healthy run (a successful push or config poll).
4. If an update fails to confirm repeatedly, check `ota_debug` in
   `/api/v1/status` (raw partition/rollback state) — this is a temporary
   diagnostic surface, not yet wired into Device Manager's UI, so read it
   directly (`curl http://<device>/api/v1/status`).

## 6. Recovering from common failures

| Symptom | What to check | Action |
|---|---|---|
| Device offline in Device Manager | `health_state` via `/api/v1/health`, or serial `show` | If `offline`, confirm Wi-Fi is up at the plant; if Wi-Fi is fine but the device isn't, power-cycle it (USB or plant power) |
| `SENSOR_STOPPED` alarm | `ms_since_last_pulse_change` in `/api/v1/metrics` | This is an advisory, not a hard fault — first confirm whether the line is genuinely idle (no dispensing expected) before assuming sensor failure. If flow is expected and pulses aren't moving, inspect the physical sensor/wiring. |
| `REBOOT_LOOP` alarm | `crash_streak`/`diag` line via `show`, or `crash_reset_streak` in `/api/v1/metrics` | Device is resetting (watchdog/brownout/panic) before ever completing 5 minutes of stable uptime. Check power supply quality (brownout) first — this is the most common real-world cause; if `watchdog_reset_count` specifically is climbing, that indicates a firmware hang, escalate for firmware investigation. |
| Queue backlog growing / `QUEUE_HIGH`/`QUEUE_CRITICAL` | `backlog`, `last_push_http_code` via `show`/`/api/v1/status` | Confirm network reachability to the server first (this is almost always a connectivity issue, not a device fault — the device retains all data safely while offline). |
| `STORAGE_WARNING`/`STORAGE_HIGH`/`STORAGE_CRITICAL`/`STORAGE_FULL` | `capacity_pct_used` via `show`/`/api/v1/status` | Confirms the device is buffering because it can't reach the server — resolve connectivity; data is not lost unless capacity reaches 100% for an extended period. |
| `QUEUE_WRITE_FAILURE` (CRITICAL) | `failed_write_count`/`last_write_failure` via `/api/v1/status` | This means actual data loss already occurred (a measurement failed to persist). This is a hard fault requiring investigation of the flash storage's health — escalate. |
| Suspected wrong/miscalibrated readings | Run `scripts/validate_meter_calibration.py` (Section 4) | Don't guess — measure. |
| API key rejected (401s in `last_push_http_code`) | `show`'s `fingerprint`, cross-check with `verify_api_key_fingerprint.py` | Confirm the ERP hasn't revoked/rotated the key without the device being updated to match; re-run Section 3's rotation procedure with the currently-intended key. |
| Completely misconfigured / need a clean slate | `show` to confirm current (mis)configuration first | `factory` at the serial console wipes NVS (Wi-Fi, server URL, API key, calibration cache, logical ID) and reboots — **does not** touch the anti-downgrade security floor (by design) or the queue/totalizer data on flash (also by design — no data loss). Re-provision from scratch afterward, exactly like a brand-new device. |

## Rules this guide assumes

Everything above is achievable without ERP redesign, without fleet-management
tooling, and without any capability beyond what exists in this codebase
today. Anything requiring remote-without-USB reconfiguration, bulk
operations, or an OTA-trigger UI is intentionally out of scope for Balaji's
freeze — see `Docs/COVIO_V2_CANONICAL_ARCHITECTURE.md` for where those land
in the Phase 2/3 roadmap.
