# 06 — USB Commissioning Result (device finally reachable via serial)

**Timestamp:** 2026-07-24. **Interface:** USB serial, COM6.

## Outcome: device is ALIVE, HEALTHY, and READ — commissioning is blocked
at exactly one step: the real production values were not provided and do
not exist in the repository. **No configuration was changed** (there were
no correct values to write; guessing them was neither authorized nor
safe). This is a genuine external blocker (credentials/endpoint), not a
device fault.

## Tasks 1–2 — Device verified alive; full state read via serial `show`

The ESP32 enumerated on **COM6** as `VID_303A&PID_1001`, and the USB
composite descriptor itself carries MAC `28:84:85:B2:E5:F4` — the exact
device searched for in the prior network-discovery phases. Serial console
responded normally. Live readings:

| Field | Value |
|---|---|
| Device ID | `esp32-F4E5B2858428` |
| Firmware version | `1.0.0` |
| Build SHA (commit) | `443dc448423b9fec2a48fb4bf7ac02740e6c2341` (the credential-redaction build), `build_dirty=0` |
| Build time | `2026-07-23T19:17:22Z` |
| Boot ID | `33` (incremented by the incidental resets from opening the serial port — normal, non-destructive) |
| **Wi-Fi SSID** | **`Airtel_amar_3999`** (the OLD bench network) |
| IP address | none — not associated (its saved SSID is not in range at this location) |
| **Server URL** | **`http://192.168.1.3:8000`** (the OLD bench endpoint) |
| **API token** | **`default`** (the shared bootstrap key; fingerprint `57c8139db729`) — raw value correctly NOT printed (the redaction fix works, live-proven again here) |
| Plant ID | field does not exist in this firmware's data model |
| Queue depth | not shown by `show`; queue/totalizer persist on flash (unaffected) |
| Time status | no RTC/NTP; wall-clock only via `server_time_ms` on sync (none currently, offline) |
| Connectivity status | offline (saved Wi-Fi not reachable here; server unreachable regardless) |
| Calibration | `K=1000.0, density=0.840, Tref=15.0` (server-side default seed) |

## Task 3 — Every incorrect value blocking live operation

Three configuration values are wrong for production, all confirmed live:

1. **`server_url = http://192.168.1.3:8000`** — the bench address. Must
   become the real production ERP/API endpoint (and should be `https://`).
2. **`api_key = default`** (shared bootstrap `dev-key-change-me`) — must
   become the real, unique per-device production token.
3. **`wifi_ssid = Airtel_amar_3999`** — the old bench network, not in
   range at the plant. Must become the plant Wi-Fi (SSID + password).

## Task 4 — Locate the production values: NOT AVAILABLE

I searched the entire repository for real production values (any
`https://` ERP/API endpoint, `.env`/secrets/production-config files, ERP/
plant/token references). **None exist.** Every endpoint found is an
explicit example/placeholder — `https://yourdomain.com`,
`https://lcs.example.com`, `https://factory.example.com` — or a
third-party library URL. This is the correct, deliberate security posture
(real secrets never committed), and it means the production values must
come from the owner/ERP operator. **They were not provided in this
task.**

## Tasks 5–6 — Program + verify: STOPPED (nothing correct to write)

The five values below are required to commission, and **not one is
available to this session**:

```
[ ] Production API URL   (e.g. https://<real-erp-host>/...)
[ ] Production API token (the real per-device key, from
                          POST /admin/devices/provision on the REAL server)
[ ] Plant Wi-Fi SSID     (in range at the device's physical location)
[ ] Plant Wi-Fi password
[ ] Plant ID / asset mapping (note: no plant_id field exists in firmware;
                          this must be recorded server-side against the
                          device_id, or a data-model change is needed)
```

**I did not write any value.** Programming a guessed endpoint/token/Wi-Fi
would point a real production device at a nonexistent server with a
non-authenticating key on an unjoinable network — strictly worse than the
current state, and impossible to verify (tasks 6–10 would all fail on
fabricated inputs). Per every prior phase's rule and basic correctness,
guessed credentials are not an option.

## Tasks 7–10 — Reboot / verify / trace data: NOT REACHED

All depend on tasks 5–6 (correct config written), which are blocked.

## Exact ready-to-run commissioning sequence (the moment real values arrive)

Over this same serial console (COM6), with the owner's real values:
```
set wifi <PLANT_SSID> <PLANT_PASSWORD>
set url  https://<REAL_PRODUCTION_ENDPOINT>
set key  <REAL_PER_DEVICE_TOKEN>
show                 # read back: verify server_url, wifi_ssid, and the
                     #   api_key fingerprint CHANGED from 57c8139db729
reboot
```
Then verify (serial boot log + the device's own `/api/v1/status` once it
joins the plant network): Wi-Fi connects, IP obtained, `last_push_http_code:200`
against the real endpoint, `last_auth_reject_reason:null`, queue draining.
K-factor, if it must change from the seeded `1000.0`, is set server-side
via `POST /admin/kfactor` on the real server (device re-polls within 60s)
— with owner-approved calibration evidence, never guessed.

## Final report

- **Final configuration:** unchanged from the readings above — `server_url`
  still `http://192.168.1.3:8000`, `api_key` still `default`
  (fp `57c8139db729`), `wifi_ssid` still `Airtel_amar_3999`. **Nothing was
  written** (no correct values available).
- **Device IP:** none (not associated — saved Wi-Fi out of range).
- **Connected SSID:** none currently; configured SSID is `Airtel_amar_3999`.
- **Server URL:** `http://192.168.1.3:8000` (bench — must change).
- **Plant ID:** not represented in firmware.
- **Firmware version:** `1.0.0`, commit `443dc44...`, clean.
- **API connectivity status:** offline — cannot reach the (bench) server;
  no production endpoint configured.
- **Last successful heartbeat:** none this session (device offline).
- **Last successful sensor upload:** none — and separately, the sensor
  has produced zero pulses this entire engagement (`totalizer` static at
  the last-known value); real flow was never observed.
- **Evidence data is visible on the live ERP:** none — no production ERP
  endpoint is configured or was supplied.
- **Remaining blocker (precise, with corrective action):**
  **The five production commissioning values are missing** (production API
  URL, per-device token, plant Wi-Fi SSID + password, plant/asset
  mapping). **Corrective action:** the owner supplies these five values;
  I then run the exact `set wifi` / `set url` / `set key` sequence above
  over COM6, read back to prove each write, reboot, and verify live
  authentication + heartbeat + first upload against the real ERP. The
  device is otherwise ready — alive, healthy, correct firmware, serial
  console working, config writable, all state (queue/floor/totalizer)
  intact.

## Rules compliance

- No NVS erased, no factory reset, no reflash, no config changed.
- No production values guessed or fabricated.
- Every value reported was read live from the device; nothing invented.
- The only side effect was incidental resets from opening the serial
  port (boot_id increments) — non-destructive, no data loss (queue/
  totalizer/floor confirmed intact via the persistent state the firmware
  recovers on each boot).

## Verdict

**LIVE PLANT COMMISSIONING — BLOCKED ON OWNER-SUPPLIED PRODUCTION VALUES.**
The device is fully prepared and reachable; commissioning completes as
soon as the five real values are provided. It cannot be completed with
guessed data.
