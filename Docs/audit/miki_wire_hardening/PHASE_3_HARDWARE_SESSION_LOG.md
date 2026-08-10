# Phase 3 — Hardware Validation Session Log

Append one entry per session. Never mark a hardware row PASS from this log
alone — evidence goes into `PHASE_2_VALIDATION_MATRIX.md`'s Actual/Evidence
columns; this log records who/when/what-hardware.

---

## Session 1 — 2026-08-10 22:43 (bench host, unattended agent session)

**Hardware detection result: NO DEVICE CONNECTED.**

- `[System.IO.Ports.SerialPort]::GetPortNames()` → empty
- `Get-PnpDevice -Class Ports` → only `ECP Printer Port (LPT1)`
- USB-class scan for Serial/UART/CP210x/CH34x/FTDI/Espressif → none
- `pio device list` → no devices

**Consequence (mandate §22):** zero firmware changes made; zero hardware
tests executed; every hardware row in the validation matrix remains
**BLOCKED**. Candidate remains frozen at branch
`miki-wire-enterprise-hardening` @ `170b5c4` (firmware source `41e31af`),
clean tree. Status unchanged:
`HARDWARE VALIDATION: BLOCKED · SITE DEPLOYMENT: NOT READY`.

---

## Required physical hardware (blocking everything below A-row procedures)

1. **Waveshare ESP32-S3-POE-ETH-8DI-8DO** bench unit (NOT the MW-001
   production unit unless a site visit is planned) + USB-C cable to this PC.
2. **LJ12A3-4-Z/BX** NPN NO inductive proximity sensor + ferrous target,
   wired per `deploy/miki-wire/wiring/WIRING.md` (output→DI1, 0V→DI COM,
   +V→7-36V field terminal). A field-side DC supply (e.g. 24 V) for the
   sensor/DI side.
3. **Multimeter** (mandatory for A2/A3) and ideally a scope for edge quality.
4. **Controllable pulse source** for C3/B1 known-pulse tests: scripted
   target passes, a function generator into the DI (respecting DI voltage
   range), or a relay driven by a second MCU.
5. **Switchable supply** (bench PSU or switched socket) for D1 power-cut
   tests — controlled, not improvised.
6. **Controllable WiFi AP** (phone hotspot acceptable) + reachable test
   server (`server/server.py` bench stub or the Miki backend with a test
   device row) for F1/network and ack verification.
7. Time: the endurance row (C4) needs the device powered ≥27 h.

## Device identification record (fill BEFORE any flash — mandate §3/§4)

```
Date/time:
Operator:
COM port:
Boot banner (verbatim first lines):
device_id (from `show`):
build_commit / build_dirty (from boot banner):
FW version:
server_url (from `show` — identifies which backend it was provisioned for):
Classification:  [ ] bench/blank  [ ] test firmware  [ ] MIKI PRODUCTION (STOP — do not flash)
                 [ ] unknown (STOP — investigate before flashing)
Target firmware: esp32dev-mikiwire @ 41e31af  (sha256 c55c0031…11d8)
Rollback at hand: covio-mw001-deployed-1.0.0-73d82ff.bin (sha256 082954a4…5805)
```

**STOP rules:** `device_id: esp32-F84AD1A172E0` = the MW-001 production
unit — do not flash outside the site-deployment procedure. Any device whose
`server_url` points at a production backend is treated as production until
proven otherwise. `esp32-F4E5B2858428` = the Balaji unit — never flash it
from this effort at all.

## Execution order once a bench unit is identified (unchanged)

`A2 (electrical) → E2 sensor sweep (matrix A4/A5) → C3/B1 (known pulses) →
F1 (network outage) → H1 (AP mode) → D1 (power cuts) → E2 (watchdog
hang, WDT_TEST_BUILD image) → C3/C4 (rollover + ≥27 h endurance) → §16
characterization at the machine → §17 thresholds → long-run`.
