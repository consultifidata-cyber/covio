# 11 — Physical Test Authorization Request

## Exact physical commands proposed (none executed)

1. `pio run -e esp32dev -t upload --upload-port COM6` (or the direct
   `esptool.py write_flash` form in doc 09) — flashes a candidate or
   recovery image.
2. Serial monitor (`pio device monitor --port COM6` or equivalent) —
   read-only observation of boot logs during the tests.
3. HTTP requests to the device's own local API (`GET /api/v1/info`,
   `/api/v1/status`) over the LAN — read-only.
4. Editing `server/firmware/manifest.json` on the LOCAL bench server (not
   a physical-device command, but drives what the device will attempt) —
   listed for completeness since it's part of the same workflow.

## Exact serial port

`COM6` (`USB\VID_303A&PID_1001&MI_00`, Espressif USB-Serial/JTAG),
previously confirmed connected. Must be re-confirmed present at
authorization time — not assumed unchanged from a prior session.

## Exact firmware images and hashes

- `evidence/known-good-1.0.0.bin`, sha256 `7cc6f373eb389069f88ebaccdd7db29467e8f8fc8adce3041abed09bf8cd0fa6`
- `evidence/candidate-1.0.1-candidate.bin`, sha256 `5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89`
- An unhealthy candidate and a downgrade-test manifest, both constructed
  fresh at test time per doc 07/08 (not pre-built, see doc 07's rationale)

## Exact OTA endpoint

`http://192.168.1.3:8000` (private LAN bench server) — per the prior
session's live query. **Must be re-confirmed, not assumed current**, as
the very first step of doc 08's test plan.

## Will any reboot occur?

Yes — every successful OTA test (1, 2) and the recovery runbook (if
triggered) reboots the device. Test 3 (downgrade rejection) and Test 4
(interrupted download) are designed NOT to reboot the device.

## Will any flash write occur?

Yes, for Tests 1 and 2 (the `app0`/`app1` OTA partitions) and for any USB
recovery reflash. Tests 3 and 4 are designed to reject/fail BEFORE any
flash write. `spiffs` (queue) and `nvs` (config + security floor) are not
targeted by any planned write in this test plan.

## Could any production data be affected?

Only if the endpoint re-confirmation step (doc 08, step 1) is skipped or
fails — this is why it is the explicit first step of every physical test
and a named stop condition. Assuming re-confirmation passes: no, the
device only ever talks to the private-LAN bench server named above, which
has no connection to any production system.

## Recovery commands

See `09_USB_RECOVERY_RUNBOOK.md` in full.

## Maximum potential failure impact

Worst case: the device fails to recover automatically after the forced-
rollback test (Test 2) and requires the USB recovery reflash (doc 09) —
a bounded, documented, low-risk recovery path using an already-verified-
compiling known-good binary. No scenario in this plan risks permanent
device loss (no eFuse operations, no full-chip erase) or any production
system.

## Requested authorization wording

> **PHYSICAL HARDWARE AUTHORIZATION REQUIRED:** The code, automated tests,
> firmware artifacts, and recovery plan are ready. No serial, flashing,
> reset, HTTP device access, or OTA action has been performed. Explicit
> authorization is required before executing the documented isolated
> hardware tests.

Per the mandate, authorization must be an explicit instruction such as:
> "Authorized: execute the isolated OTA success, rollback,
> downgrade-rejection, and interrupted-download tests on COM6 using the
> documented artifacts and recovery plan."

This document does not itself constitute that authorization.
