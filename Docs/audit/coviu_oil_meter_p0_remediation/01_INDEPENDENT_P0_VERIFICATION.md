# 01 — Independent P0 Verification

Each of the prior audit's five P0 findings was re-derived independently from
current source in this session (not inherited from the prior report's
summary) before any fix was written. All five were confirmed accurate.

## RISK-01 — Acknowledgement-gap / queue-pruning deadlock

**File/line:** `server/server.py`, `push()`, the ack-computation block
(originally lines 614-627, before this session's fix).

**Confirmed behavior:** `ack_seq` was computed as "the highest CONTIGUOUS
`seq` held in the `records` table" via a plain scan over
`SELECT DISTINCT seq FROM records WHERE device_id=?`. A record that fails
ADR-001's schema/record-type acceptance rule is written to
`quarantined_records` (a separate table), never to `records`. Because the
scan only ever looked at `records`, a single permanently-quarantined `seq`
made the contiguous scan halt at that point **forever** — every later,
successfully-accepted `seq` was durably stored server-side but never
included in `ack_seq`, so `queue.h::ackThrough()` (device side) never
learned it could prune anything past that point.

**Failure sequence:** device sends seq 1 (valid) and seq 2 (schema version
99, unsupported) → server stores seq 1 in `records`, quarantines seq 2 →
`ack_seq` returned = 1, forever, regardless of how much MORE valid data
(seq 3, 4, 5, ...) is sent afterward, because the scan never advances past
the position where a resolved-but-non-records seq sits.

**Verdict:** prior report correct. Severity P0 justified — this is
unbounded local storage growth on any device that ever produces one
permanently-rejected record, with no operator-visible symptom distinguishing
it from an ordinary connectivity outage.

## RISK-02 — Hardcoded live WiFi credential

**File/line:** `config.h:37-38` (pre-fix).

**Confirmed independently** by direct file read in this session (not just
citing the prior report): `DEFAULT_WIFI_SSID`/`DEFAULT_WIFI_PASS` held a
real-looking SSID/password, and the physically-connected device's serial
log (captured in the prior session, referenced but not re-captured in this
one — this session did not touch COM6, see Phase 0 note below) showed it
actively connected to that same network name.

**Verdict:** prior report correct. Severity P0 justified — source-controlled
plaintext credential for a real network.

## RISK-03 — Unauthenticated admin API

**File/line:** `server/server.py`, every `@app.route("/admin/...")`
definition (pre-fix): `/admin/kfactor`, `/admin/devices/provision`,
`/admin/devices/<id>/revoke-key`, `/admin/devices/<id>/rotate-key`,
`/admin/events`, `/admin/devices/<id>/events`, `/admin/devices`, and `/`.

**Confirmed independently:** none of these routes called any auth check;
`require_api_key()` (the real, working device-facing auth gate) is used only
by `push()`/`config()`/`ota_manifest()`. The code's own comment at the
`provision_device()` definition explicitly acknowledged this as "an already-
accepted, already-documented bench/pilot-scope limitation (ADR-016)."

**Verdict:** prior report correct. Severity P0 justified — unauthenticated
device revocation, key issuance, and billing-relevant K-factor control.

## RISK-04 — Silent data loss at flash-full / write failure

**File/line:** `queue.h::append()` (pre-fix), four failure branches: segment-
open failure (~line 138), truncate-before-append failure (~line 159),
segment-append-open failure (~line 184), short/failed write (~line 199).

**Confirmed independently:** every one of these branches called
`Serial.println(...)`/`Serial.printf(...)` and `return` — no durable state
was written, no counter incremented, no alarm raised. Meanwhile
`totalizer.h`'s `Totalizer` is a **separate, independent** counter (backed by
the PCNT hardware peripheral) that keeps incrementing regardless of whether
`queue.h` succeeds at durably recording that value. `diagnostics.h`'s alarm
set (`computeHealth_()`) had no corresponding alarm type for a queue write
failure — only `QUEUE_HIGH`/`QUEUE_CRITICAL` (backlog-size thresholds, which
say nothing about a failure that reduces backlog by silently dropping data).

**Verdict:** prior report correct. Severity P0 justified — this is exactly
the mandate's stated invariant violation: a measurement can be silently
treated as unsafe-but-invisible.

## RISK-05 — OTA update/rollback never proven on real hardware

**File/line:** `ota.h` (whole file); `esp_ota_mark_app_valid_cancel_rollback()`
call at line ~85.

**Confirmed independently:** the rollback mechanism is real, standard
ESP-IDF usage (`esp_ota_get_state_partition`/`esp_ota_mark_app_valid_cancel_rollback`),
correctly gated on a genuine health signal (`confirmHealthyBoot()` is only
called after WiFi connects AND at least one successful server ack/config
poll — see `covio_firmware.ino`'s `healthySignalled` wiring). This session
additionally confirmed, by reading the actual compiled SDK headers this
toolchain uses
(`~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/dio_qspi/include/sdkconfig.h`),
that `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` is genuinely compiled into
this exact build's bootloader — the underlying safety net the code depends
on is present, not silently absent. This is stronger evidence than the prior
session had, but it is still **not** a substitute for an actual OTA + forced
rollback exercised on the connected hardware, which this session's authority
boundary explicitly prohibits without separate authorization (see
`07_OTA_PHYSICAL_TEST_PLAN.md`).

**Verdict:** prior report correct. Severity P0 justified — remains genuinely
unproven end-to-end; static confidence increased, hardware proof still
outstanding.

## Phase 0 baseline (recorded before any change)

- Repository path: `C:\Users\Dell\Documents\ConsultiFi_Data\covio-main`
- Git: **no `.git` existed at session start** (`git status` returned "not a
  git repository"), despite `.github/workflows/*.yml` and `.gitignore`
  already existing in the tree — this repo had never been placed under
  version control. `git init` was run this session; see the baseline commit
  message on `main` for the exact justification and ordering (credential
  redacted from `config.h` BEFORE the first-ever commit, so the real
  WiFi credential never entered git history at all — no history rewrite is
  needed because it was never committed).
- Branch: `fix/coviu-oil-meter-p0-enterprise-readiness`, created from the
  `main` baseline commit.
- Baseline commit: `c83fcd7` (initial snapshot). Fix commits on top:
  `9c76edb` (P0-1 + P0-3), `ae4e037` (P0-4).
- Toolchain: Python 3.11.9; PlatformIO Core 6.1.15; espressif32@6.5.0
  (arduino-esp32 core 2.0.14), all confirmed genuinely installed and used
  (not assumed) via real `pio run` compiles this session.
- Server/test DB: SQLite, throwaway temp-file databases per test (never
  `server/covio.db`), matching the pre-existing test convention.
- Existing audit folder: `docs/audit/coviu_oil_meter_enterprise_readiness/`
  (the prior session's 15-file report) — preserved unchanged; this
  session's output lives in the separate
  `docs/audit/coviu_oil_meter_p0_remediation/` folder per the mandate.
- **Hardware boundary respected:** COM6 (the physically-connected ESP32-S3)
  was not opened, no `esptool` command was run, no serial monitor was
  started, and no `-t upload` build target was invoked at any point in this
  session. Every `pio run` invocation was a plain build with no upload
  target.
