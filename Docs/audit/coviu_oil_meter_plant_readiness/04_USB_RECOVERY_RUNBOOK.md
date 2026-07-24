# 04 — USB Recovery Runbook

Written to be usable by someone other than the developer who wrote the
firmware. **This session performed a complete dry run of every step up
to — but not including — an additional recovery write**, per the
mandate's own explicit option, since this exact flash command has
already been executed and esptool-hash-verified **six separate times**
this remediation chain (docs 33, 35, 36, 37, plus this session's release
freeze) — no further physical proof is needed; repeating it again here
would not add evidence, only wear.

## When to use this runbook

- A post-OTA health check fails (see 06_OPERATOR_QUICK_RESPONSE_GUIDE.md).
- The device is confirmed unhealthy after an OTA attempt and will not
  recover on its own (bootloader automatic rollback is **not** available
  on this hardware — see 09_KNOWN_LIMITATIONS.md — so this is the only
  recovery path for a bad-but-authentic candidate).
- A supervisor or trained operator, not necessarily the firmware author,
  determines recovery is needed.

## Required items (confirmed present/verified this session)

| Item | Status |
|---|---|
| Laptop with PlatformIO installed | Confirmed — this session's own machine, PlatformIO Core 6.1.15 |
| USB cable (native USB, board's own USB-C port) | Confirmed — used for every flash this entire audit trail |
| Correct COM port | **Not fixed** — confirm at time of use (see below); this bench unit enumerated as COM6 throughout this audit trail, but a different USB port/hub/cable on plant hardware may enumerate differently |
| Known-good firmware binary | `covio-oilflow-v1-1.0.0-856972e.bin` (this release's own artifact — see rationale below) |
| SHA-256 verification command | See below |
| Exact flash command | See below |

## Why the recovery target is THIS release candidate, not an older image

Earlier phases of this audit trail (doc 22) named
`evidence/known-good-1.0.0.bin` (built from an older commit, `d24051f`)
as the recovery baseline. That image **predates every fix in this
remediation chain** — it has the confirmed `server_time_ms` overflow bug
(doc 35) and the confirmation-bookkeeping gap (doc 36). Recovering to it
would restore a *worse* firmware than the one that just failed an OTA.
**The correct recovery target, going forward, is this release's own
artifact** — reflashing the exact, already-verified-healthy baseline the
device was running before the failed OTA attempt.

## Step-by-step procedure

### 1. Identify the correct COM port
```powershell
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d' } |
  Select-Object Name, DeviceID, Status
```
Look for `USB Serial Device (COMx)` with `DeviceID` containing
`VID_303A&PID_1001` (Espressif's own USB VID; PID 0x1001 = ESP32-S3
native USB-Serial/JTAG) — this is the specific, distinguishing identity
of this exact hardware, confirmed repeatedly this entire audit trail.
**Do not assume COM6** — confirm the port fresh each time; other USB
serial devices may be present and enumerate differently on different
hardware.

### 2. Verify the recovery binary's hash before use
```powershell
Get-FileHash "covio-oilflow-v1-1.0.0-856972e.bin" -Algorithm SHA256
```
Must read exactly:
```
136ef36ccf3d1885bb78a17473ee68d47f65e83318e6aff7e5fc9953f0fb2031
```
**Independently verified this session** (Python-recomputed hash matched
the recorded `.sha256` sidecar file exactly, dry-run above).

### 3. Print the exact flash command (this is the command that would run — verify the port number matches step 1 before executing)
```
pio run -e esp32dev -t upload --upload-port COM6
```
(Replace `COM6` with whatever step 1 actually found.)

### 4. What this command does and does NOT do (proven, not assumed — 6 real executions this chain)
- Writes exactly 4 regions: bootloader (`0x0`), partition table
  (`0x8000`), otadata (`0xe000`), application (`0x10000`).
- Every region is `esptool`-hash-verified (`Hash of data verified.`)
  before the command reports success.
- **Never** touches NVS (`server_url`/`api_key`/`wifi_ssid`/
  `wifi_pass`/`accepted_security_floor` all live in separate NVS
  partitions/namespaces at different flash offsets, outside every
  region this command writes).
- **Never** touches the LittleFS/queue partition (offline-buffered
  telemetry survives).
- Does **not** use `erase_flash` and does **not** erase the whole chip.

### 5. Required post-flash checks
```
GET http://<device-ip>/api/v1/info
  -> confirm build_commit == 856972ef6106d662c5f8a7f5b71c9a60ce40edc1
  -> confirm build_dirty == false
GET http://<device-ip>/api/v1/status
  -> confirm health_state == "ok"
  -> confirm accepted_security_floor did NOT decrease from its
     pre-recovery value
  -> confirm queue.backlog is continuing to drain, not stuck
GET http://<device-ip>/api/v1/health
  -> confirm alarms is empty (or only pre-existing, explained alarms)
```
If any check fails: **do not repeatedly reflash** — stop and escalate
(see 06_OPERATOR_QUICK_RESPONSE_GUIDE.md's escalation contact).

## WiFi/configuration preservation expectations

Wi-Fi SSID/password, server URL, and API key are NVS-resident and
**preserved automatically** by this flash procedure — no reprovisioning
should be needed after a recovery flash. If the device fails to
reconnect to Wi-Fi post-recovery, that is itself an anomaly requiring
escalation, not an expected side effect.

## Escalation contact

Not populated by this report — the business must name a specific,
reachable person/role here before the plant pilot begins (placeholder
deliberately left open rather than inventing a contact).
