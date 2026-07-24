# 33 — Hardened USB Baseline Result (Parts 5-7 Executed)

Executes Parts 5-7 of the Final Production Hardware Certification
mandate: pre-flash baseline, USB flash of commit
`8a9c9a9033f866b421722867f6540c317a35ac89`, post-flash identity
certification. **Stopped exactly after Part 7 as instructed — Parts 8-14
(OTA, rollback, fault injection, downgrade) were not attempted.**

## Verdict: BUILD IDENTITY FLASH CERTIFIED WITH OBSERVATIONS

One cosmetic observation (not a functional failure) below; every required
check otherwise passed with direct, cross-validated evidence.

## 1. Pre-flash firmware identity

```
Timestamp: 1784811813 (immediately before flash)
COM6 USB identity: VID_303A&PID_1001, Status OK (re-confirmed unchanged)
GET /api/v1/info -> hardware_id esp32-F4E5B2858428, fw_version 1.0.0,
                     boot_id 19, schema_version_current 1
                     (no build_commit/build_dirty/security_version fields
                      -- pre-hardening firmware, as expected)
GET /api/v1/status -> uptime_ms 7912265, health ok, queue backlog 1,
                     acked_seq 41529, totalizer_raw_pulses 401
                     (up from a flat 280 earlier this session -- some
                      real pulse activity occurred at an unknown point;
                      not attributed to any action by this session)
Server DB: records count=41519 min=1 max=41519 (contiguous), quarantined=0
git rev-parse HEAD: 8a9c9a9033f866b421722867f6540c317a35ac89 (confirmed,
                     matches the authorized commit)
git status --short: only the 6 prior sessions' untracked audit docs;
                     zero tracked modifications
```

## 2. Flash command used

```
pio run -e esp32dev -t upload --upload-port COM6 -v
```
(PlatformIO's normal upload process, not a bespoke esptool invocation --
per the mandate's own "use normal PlatformIO flashing process".)

## 3. Flash output (verbatim key lines, full log captured separately)

```
Serial port COM6
Chip is ESP32-S3 (revision v0.2)
Flash will be erased from 0x00000000 to 0x00003fff...   (bootloader)
Flash will be erased from 0x00008000 to 0x00008fff...   (partition table)
Flash will be erased from 0x0000e000 to 0x0000ffff...   (otadata)
Flash will be erased from 0x00010000 to 0x00109fff...   (application)
Wrote 14032 bytes at 0x00000000 ... Hash of data verified.
Wrote 3072 bytes at 0x00008000 ... Hash of data verified.
Wrote 8192 bytes at 0x0000e000 ... Hash of data verified.
Wrote 1021056 bytes at 0x00010000 ... Hash of data verified.
Leaving...
Hard resetting via RTS pin...
========================= [SUCCESS] Took 71.90 seconds =========================
```
**No `erase_flash` / full-chip erase was used** — only the four sector
ranges esptool always erases immediately before writing them (standard,
expected behavior for any upload, not a bulk/full erase). **NVS and the
LittleFS/queue ("spiffs") partition were never in the erase or write
list at all** — per `default_16MB.csv`, those live at higher offsets this
flash never touched. The application byte count (1,021,056) and its
prior-recorded SHA-256 (`5eceaef4a7875f3b7fbcf67ddc91ba78981c71418c669743d8ac67a630b700b0`,
doc 32) match exactly — the artifact written is the one built from
commit `8a9c9a9033f866b421722867f6540c317a35ac89`.

## 4. Verification output

Every one of the four writes reported **"Hash of data verified."**
independently from esptool itself (not inferred from a bare "SUCCESS"
line). The reset at the end (`Hard resetting via RTS pin...`) is
esptool's own standard post-flash completion step — the documented
process requiring it, not a manual reset issued separately.

## 5. Boot log (passive serial capture, ~15s bounded read, zero bytes transmitted)

```
=== Covio Oil Flow Meter 1.0.0 ===
build_commit=8a9c9a9033f866b421722867f6540c317a35ac89  build_dirty=0  build_time_utc=2026-07-23T13:04:01Z
device_id=esp32-F4E5B2858428  boot_id=20
(serial console ready — type 'help')
[OK] internal flash storage ready
[SYNC] acked_seq=41653 (sent 5)
[SYNC] acked_seq=41658 (sent 5)
[SYNC] acked_seq=41663 (sent 5)
```
`boot_id` incremented by exactly **one** (19 → 20) — a single clean
reboot, no boot loop. Queue/LittleFS mounted successfully
(`internal flash storage ready`) and sync resumed immediately, both
proving the data partitions survived the flash intact.

## 6. `GET /api/v1/info` response (independent HTTP check, not read from serial)

```json
{"hardware_id":"esp32-F4E5B2858428","logical_device_id":null,"asset_id":null,
 "fw_version":"1.0.0","model":"covio-oilflow-v1","boot_id":20,
 "schema_version_current":1,
 "build_commit":"8a9c9a9033f866b421722867f6540c317a35ac89",
 "build_dirty":false,
 "build_time_utc":"2026-07-23T13:04:01Z",
 "security_version":1,"accepted_security_floor":0}
```
`GET /api/v1/status` (abridged): `health_state:"ok"`, `queue:{backlog:4,
acked_seq:41678, last_seq:41682, capacity_pct_used:43.3,
failed_write_count:0, last_write_failure:null}`,
`ota:{state:"none", running_version:"1.0.0", security_version:1,
accepted_security_floor:0, last_reject_reason:null,
last_auth_reject_reason:null}`, `totalizer_raw_pulses:401` (unchanged
from the pre-flash reading — not reset), `last_push_http_code:200`.

**This is the exact fix for the blocker that stopped doc 31**: the new
diagnostic fields (`security_version`, `accepted_security_floor`,
`last_reject_reason`, `last_auth_reject_reason`) are now genuinely live
on the device, not just present in the source tree.

## 7. Runtime health summary

```
GET /api/v1/health  -> {"health_state":"ok","alarms":[]}
GET /api/v1/metrics -> free_heap_bytes 265864, cpu_freq_mhz 240,
                        flash_size_bytes 16777216, sd_write_latency_ms 66,
                        queue_read_latency_ms 25, network_rtt_ms 274,
                        reset_reason "unknown" (see observation below),
                        temperature_c null (no temp module, by design)
```
Web server up (all four endpoints answered HTTP 200), Wi-Fi reconnected
automatically, queue/storage initialized, sync resumed and already
pushed new records successfully (`last_push_http_code:200`). No crash,
no repeated resets (`boot_id` +1 exactly), no boot loop, no critical
alarms.

## 8. Expected vs actual build identity

| Field | Expected | Actual (serial) | Actual (HTTP) | Match |
|---|---|---|---|---|
| build_commit | `8a9c9a9033f866b421722867f6540c317a35ac89` | same | same | ✅ |
| build_dirty | `false` | `0` | `false` | ✅ |
| build_time_utc | (whatever the build produced) | `2026-07-23T13:04:01Z` | same | ✅ (serial/HTTP agree) |
| device_id | `esp32-F4E5B2858428` | same | same | ✅ unchanged |
| security_version | `1` | (not printed on boot line) | `1` | ✅ |
| accepted_security_floor | `0` (never previously raised — old firmware had no floor-raising logic) | n/a | `0` | ✅ consistent with NVS being preserved, not erased |

## 9. Anomalies encountered

- **`reset_reason: "unknown"`** in `/api/v1/metrics` — `diagnostics.h`'s
  `resetReasonStr_()` switch does not have an explicit case matching
  whatever `esp_reset_reason()` returned for esptool's RTS-pin hard
  reset, so it fell through to the default string. This is a **cosmetic
  observability gap, not evidence of a crash**: the switch's explicit
  `ESP_RST_PANIC` case would have fired and reported `"panic"` if that
  had occurred, and every other signal (clean `boot_id` +1, `health:"ok"`,
  zero alarms, immediate successful sync) independently confirms a normal
  boot. Flagged honestly rather than omitted.
- No other anomaly found.

## Regression check (read-only only, as instructed)

`/api/v1/info`, `/api/v1/status`, `/api/v1/health`, `/api/v1/metrics` all
answered correctly. Server-side reconciliation: records fully contiguous
`1..41708`, 0 quarantined, before and after the flash — no duplicate,
no missing sequence, no data loss across the reboot. No OTA, rollback,
downgrade, credential rotation, fault injection, queue mutation, or NVS
erase was performed.

---

# Required Final Response

## 1. Verdict
**BUILD IDENTITY FLASH CERTIFIED WITH OBSERVATIONS**

## 2-9. See sections 1-9 above (this document *is* the certification report).

## 10. Final verdict (repeated per instructions)
**BUILD IDENTITY FLASH CERTIFIED WITH OBSERVATIONS** — one cosmetic
`reset_reason` string gap; every substantive identity, integrity, and
health check passed with direct, independently cross-validated evidence.

**STOPPING HERE, as instructed.** Parts 8-14 (OTA, rollback, anti-
downgrade, invalid-signature, hash-mismatch, interrupted-download,
recovery boundary) were not attempted and require a new explicit
authorization.
