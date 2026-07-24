# 01 — Live Plant Commissioning Report

**Commissioning attempt timestamp:** unix `1784874660` (2026-07-24).

## Verdict: LIVE PLANT COMMISSIONING — FAIL

Not a failure of the device or firmware — a failure to **reach** the
device. Commissioning could not begin: the ESP32 is not connected to
this laptop by USB, is not present on this laptop's current network, and
no production configuration values were supplied. Every subsequent phase
depends on Phase 1 (identify the device), which could not complete.
Nothing was guessed, fabricated, or assumed.

---

## Phase 1 — Identify the Device: COULD NOT COMPLETE

The mandate's premise was "Connect the ESP32 to the laptop via USB" and
"the ESP32 is now physically installed in the plant ... powered ON."
Neither could be confirmed from where this session runs. Direct evidence:

| Check | Result |
|---|---|
| USB serial ports on this laptop | **NONE** — `[System.IO.Ports.SerialPort]::GetPortNames()` returned empty; zero `COM\d` devices enumerated |
| USB ESP32 (VID_303A) enumerated | **NONE** |
| Device at its last-known IP (`192.168.1.4`) | **Unreachable** — curl timed out (RC 28); and `192.168.1.4` now resolves via ARP to a DIFFERENT device (MAC `6c-0b-84-03-b0-df`), not our ESP32 |
| Device MAC (`28-84-85-b2-e5-f4`) anywhere in ARP | **Not found**, before or after a full-subnet ping sweep |
| Device mDNS name (`covio-858428.local`) | **Does not resolve** |
| Full 192.168.1.0/24 ping sweep | 12 live hosts found; **none is the Coviu device** |

**This laptop is on a different network than every prior phase.** It is
now `192.168.1.41` with gateway MAC `14-c3-5e-17-7b-4c`. Every prior
phase of this engagement ran with this laptop at `192.168.1.3` (it also
WAS the bench server) with gateway MAC `3c-f7-5d-b0-eb-c0`. This is a
genuinely different LAN/router — consistent with the laptop having been
moved, but the ESP32 is not visible on it, nor cabled to it.

**Not one Phase 1 field could be read** (build commit, firmware version,
device ID, plant ID, server URL, queue depth, totalizer, uptime, reset
reason, health state) — reading any of them requires either a USB serial
connection or network reachability to the device, and neither exists.
Reporting them from memory of prior phases would be fabrication, not a
live reading, so they are deliberately left unread.

---

## Phases 2–7 — NOT ATTEMPTED (blocked by Phase 1)

- **Phase 2 (plant network):** cannot connect a device that isn't
  cabled and isn't visible.
- **Phase 3 (live provisioning):** blocked twice over — (a) the device
  is unreachable, and (b) **no production configuration values were
  supplied**. The mandate says "Obtain from the owner: production API
  endpoint, API token, device ID, plant ID, Wi-Fi credentials, sync
  interval, sampling interval, timezone, K-factor" — none of these were
  provided in this session. Per the standing rule (and every prior
  phase's own human-input-boundary stop), these must not be guessed.
- **Phase 4 (server registration):** requires a provisioned, reachable
  device.
- **Phase 5 (first live data):** requires the sensor and device
  reachable and provisioned.
- **Phase 6 (network test):** same.
- **Phase 7 (sensor validation):** requires physical presence + reachable
  device + a calibrated reference; unavailable, as in every prior phase.

---

## Phase 8 — Final Go/No-Go

| Question | Answer |
|---|---|
| 1. Correctly provisioned? | Unknown — device unreachable, no values supplied |
| 2. Communicating with the live server? | Unknown — device unreachable |
| 3. Correct plant receiving data? | Unknown — no data flow observed; no plant-ID concept exists in the firmware regardless (see prior audits) |
| 4. Queue sync working? | Not observable this session |
| 5. No-flow detection correct? | Not observable |
| 6. Flow detection correct? | Not observable |
| 7. Totalizer behaviour as expected? | Not observable |
| 8. Duplicate records? | None observed (no data flowed) |
| 9. Missing records? | None observed (no data flowed) |
| 10. Ready for supervised plant operation? | **Cannot be certified this session** |

---

## What is required to actually run this commissioning

1. **Physically connect the ESP32 to this laptop via USB** (so it
   enumerates as a COM port — expected `VID_303A&PID_1001`), **OR** put
   this laptop on the same LAN as the powered device and confirm the
   device joins that network (visible via ARP by MAC `28-84-85-b2-e5-f4`
   or mDNS `covio-858428.local`).
2. **Supply the real production configuration values** (endpoint, token,
   device ID, plant-asset mapping, plant Wi-Fi credentials, approved
   K-factor, and confirmation of sync/sampling/timezone expectations).
   See `Docs/audit/coviu_oil_meter_plant_pilot_activation/04_PLANT_CONFIGURATION_RECORD_REDACTED.md`
   for the complete required list — unchanged, still outstanding.
3. **Have a physically present operator** for the sensor/flow steps
   (Phases 5–7), with a calibrated reference for accuracy.

Once (1) and (2) are in place, this exact commissioning sequence can be
re-run and will proceed past Phase 1.

---

## Standing-rules compliance

- No firmware modified.
- No factory reset, no NVS/LittleFS/queue erase.
- No configuration values guessed or written.
- No device readings fabricated.
- No data loss possible (no device was touched).

## "Leave the device running on the live plant network for observation"

Cannot be actioned from this session — there is no connection to the
device to leave it in any particular state. If the device is genuinely
powered and installed at the plant, it is running on whatever network it
was last provisioned for — which, per every prior phase, is still the
**bench address** (`http://192.168.1.3:8000`), an unresolved blocker
carried forward from
`Docs/audit/coviu_oil_meter_plant_pilot_activation/`. If the device is
currently powered at the plant with that stale config, it is buffering
locally and failing to sync (exactly the safe, no-data-loss offline
behaviour proven in prior phases) until its endpoint is reprovisioned.

No temporary servers or background processes were started this session;
nothing to shut down.
