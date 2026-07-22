# 07 — OTA Physical Test Plan (Authorization Required Before Execution)

This document is a **plan**, prepared per the mandate's own required
structure. **No step in this document has been executed.** Executing any
step below requires separate, explicit human authorization — this session
stops at that boundary, as instructed.

## 1. Exact device identity

Physically connected unit on COM6, per the prior audit's live query:
`hardware_id = esp32-F4E5B2858428`, `model = covio-oilflow-v1`,
`fw_version = 1.0.0` (as of the last time it was queried in the prior
session; not re-queried in this session, since doing so would require
opening COM6).

## 2. Current firmware version
`1.0.0` (`FW_VERSION` in `config.h`, unchanged by this session's fixes —
none of the P0-1/P0-3/P0-4 changes touch `FW_VERSION`).

## 3. Candidate firmware version
Not yet built/designated. Recommendation: bump `FW_VERSION` to `1.0.1` in a
throwaway test build that includes this session's P0 fixes, built via
`pio run -e esp32dev` (already proven to compile in this session), for use
as the "candidate" image in the success-path test below.

## 4. Backup procedure (before any physical step)
1. Read and save the currently-running firmware's reported state via the
   device's own local API (`GET /api/v1/info`, `/api/v1/status`) — read-only,
   no state change.
2. Retain the currently-built `env:esp32dev` binary
   (`.pio/build/esp32dev/firmware.bin`) as the known-good rollback image.
3. Confirm (via the same read-only API call) the device's current queue
   backlog and `acked_seq` before any update attempt, so post-test
   reconciliation has a known starting point.

## 5. Recovery image
`.pio/build/esp32dev/firmware.bin`, already compiled and present in this
session's build output (bootloader.bin/partitions.bin alongside it), built
from the exact source tree this session's P0 fixes are committed against
(commit `ae4e037` and later on `fix/coviu-oil-meter-p0-enterprise-readiness`).

## 6. USB recovery command (documented, NOT executed)
```
pio run -e esp32dev -t upload --upload-port COM6
```
This is the ONLY command in this entire remediation pass that would ever
touch COM6, and it is explicitly not run without separate authorization.

## 7. Queue/configuration preservation checks (to run AFTER any real test)
- `GET /api/v1/status` → confirm `queue.backlog`/`queue.acked_seq` match
  pre-test values (accounting for any new samples generated during the
  test window).
- Confirm NVS-held config (`server_url`, `wifi_ssid`, API key status) is
  unchanged.

## 8. Success test (candidate OTA)
1. Publish a manifest (`server/firmware/manifest.json`) pointing at the
   `1.0.1` candidate binary, on the LOCAL bench server only (never a
   production endpoint).
2. Confirm the device's `server_url` genuinely points at this local bench
   server, not a production/staging host, BEFORE proceeding (per this
   mandate's own "determine whether the connected device points to
   development, staging, or production" requirement — the prior session's
   live query already confirmed `http://192.168.1.3:8000`, a private LAN
   address; must be RE-confirmed at authorization time, not assumed stale).
3. Observe (via serial monitor, once authorized) the update download,
   reboot, and `confirmHealthyBoot()` firing.
4. Confirm via `GET /api/v1/info` that `fw_version` now reports `1.0.1`.

## 9. Forced-failure test (rollback proof)
1. Publish a manifest pointing at a deliberately corrupted/truncated binary
   (e.g. the real `1.0.1` .bin with its last 10KB removed).
2. Observe the device attempt the update, detect the bad image (via
   `HTTPUpdate`'s internal validation or a failed boot of the corrupt
   image), and confirm the bootloader's compiled-in rollback mechanism
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`, confirmed present this
   session — see `06_OTA_STATIC_CERTIFICATION.md`) actually reboots back
   into the previous good (`1.0.0`) image automatically.
3. Confirm via `GET /api/v1/info` that `fw_version` reports `1.0.0` again
   (i.e. the device recovered on its own, with zero manual intervention).

## 10. Rollback expected result
Device returns to `fw_version=1.0.0`, `ota.state` reflects a failed/rolled-
back attempt, queue backlog and `acked_seq` are unchanged from immediately
before the forced-failure test began (module 7's checks pass).

## 11. Stop conditions
- Abort immediately if the device fails to recover automatically within 5
  minutes of the forced-failure test starting (this would indicate the
  compiled-in rollback mechanism did NOT work as the static evidence
  suggests it should) — use the USB recovery command (item 6) at that
  point, with explicit authorization.
- Abort if the device's `server_url` is ever found to point anywhere other
  than the local bench server before step 8 begins.
- Do not proceed to the forced-failure test (step 9) until the success test
  (step 8) has fully passed and been reconciled.

## 12. Human approval line

> I, ______________________, authorize execution of the physical OTA test
> plan above (steps 8-9 specifically, which trigger real OTA and reboot
> the physically-connected device), on the device identified in section 1,
> on ____________ (date). I confirm the device's configured endpoint has
> been independently reconfirmed as a non-production bench address
> immediately before this authorization.
>
> Signature: ______________________
