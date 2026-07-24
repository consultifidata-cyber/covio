# 09 — Watchdog/Self-Healing and Time Accuracy (Parts 10-11)

## Part 10 — Watchdog and Self-Healing

Confirmed directly from the actual installed `sdkconfig`
(`framework-arduinoespressif32/tools/sdk/esp32s3/sdkconfig`), not
assumed:

```
CONFIG_ESP_INT_WDT=y                    (interrupt watchdog, 300ms)
CONFIG_ESP_TASK_WDT=y                   (task watchdog, 5s, panics on trigger)
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP32S3_BROWNOUT_DET=y           (brownout detector, level 7 = most sensitive)
```
**No explicit `esp_task_wdt_reset()`/watchdog-feeding call exists
anywhere in this firmware's own source** (confirmed by grep across every
`.h`/`.ino` file) — the firmware relies entirely on the framework's
default idle-task auto-feed behavior, which requires the main `loop()`
to yield periodically (via `delay()`/blocking I/O that cooperatively
yields). **A genuine infinite, non-yielding stall in `loop()` would
trigger the 5-second task watchdog and reset the device** — this is a
real safety net, not a gap, but it was **not independently tested this
session** (deliberately stalling the loop was judged out of the safe
execution boundary for a data-bearing device, and no isolated disposable
unit was available).

### Self-recovery per failure class (code-inspected; hardware-executed
only where this session or this chain's own history actually observed it)

| Failure class | Self-recovers without human intervention? | Evidence |
|---|---|---|
| Network task stall / blocked HTTP call | Likely — `HTTPClient`'s own `setTimeout()` bounds every call this codebase makes (6-15s depending on the call site) | Code-confirmed timeouts exist; not independently stall-tested |
| Repeated server errors | Yes — retried every `PUSH_PERIOD_MS`, no backoff escalation, no permanent giveup state | Code-confirmed |
| Wi-Fi disconnect/reconnect | Yes — proven real, repeatedly, this entire session (every server-down/up cycle involved implicit WiFi-still-up-but-app-level-retry; direct WiFi drop/reconnect specifically was not separately forced this session) | Partially direct, partially code-confirmed |
| Forced software restart | Yes — proven real, this session (`reboot` console command, doc 37-equivalent test) | **Direct, hardware-proven** |
| Filesystem mount failure | Firmware halts into a serviceable console loop (`while(true){provision.service();delay(50);}`), NOT a silent hang — but also **does not self-heal**; requires human intervention (console command or physical reflash) | Code-confirmed; not hardware-triggered this session (would require actually corrupting the filesystem) |
| Queue scan failure | No distinct "scan failure" state exists — corrupt rows are silently skipped (doc 05), not treated as a fatal condition | Code-confirmed |
| Sensor inactivity | No firmware-level detection/alarm for "sensor should be producing pulses but isn't" — `pulse_frequency_hz` simply reads `0.000` with no alarm raised (confirmed live, this entire session, since the sensor has never produced pulses at all) | **Real, this session — a genuine gap**, not a design feature: an operator must notice `pulse_frequency_hz:0` themselves; nothing pages them |
| Memory exhaustion | No explicit low-memory handling code found (confirmed by grep for `getFreeHeap`/heap-threshold logic — only read for reporting via `/api/v1/metrics`, never acted on) — matches this project's own prior audit finding (`03_ENTERPRISE_CHECKLIST_15_SECTIONS.md`, itself a lead, independently re-confirmed by this session's own code read) | Code-confirmed absence |

### Post-restart state verification (real, this session)

Every real reboot this session and this chain (8+) showed: unsynced
queue intact, totalizer intact (`401` unchanged throughout), floor
intact (persisted correctly, doc 37), config intact (`server_url`/Wi-Fi
unchanged across every reboot), sequence continuity intact (server DB
fully contiguous every time), sync resumes automatically, restart reason
visible via `/api/v1/metrics`.

## Part 11 — Time Accuracy

### Time sources that exist

- **Server-provided time** (`server_time_ms`, in every push response) —
  the ONLY wall-clock signal this device ever receives. Fixed this
  session's remediation chain (the 32-bit overflow bug, docs 35-36).
- **NTP**: **not used at all** — no NTP client code exists anywhere in
  this firmware (confirmed by grep for `configTime`/`sntp`/`NTP` —
  none found).
- **Hardware RTC**: **none** — confirmed by this project's own
  pre-existing, disclosed comment (`sync.h`: "this device has no RTC/NTP
  (RISK-11, unchanged, pre-existing gap)"), independently re-confirmed
  by this session's own grep (no RTC driver/library referenced).
- **Monotonic uptime**: `millis()` — used for `ts` (telemetry
  timestamp field, uptime SECONDS, not wall clock — confirmed,
  `SCHEMA_REGISTRY.md`'s own documented contract).
- **Persisted time**: none — nothing about wall-clock time is ever
  written to NVS/flash; only `serverUnixS_` + `serverTimeCapturedAtMs_`
  (RAM-only, reset every boot) estimate current wall time for OTA
  manifest expiry checks.
- **Sequence-based ordering**: yes, this is the REAL ordering mechanism
  for telemetry — `seq` is globally monotonic and is what the server's
  ack/contiguity logic actually relies on, NOT wall-clock time.

### Answers

1. **NTP used?** No.
2. **Server time used?** Yes, but only for OTA manifest expiry
   estimation (RISK-16-era design) — not for telemetry timestamps.
3. **Hardware RTC?** No.
4. **Cold boot without internet**: `haveServerTime()` stays false; the
   device operates normally for telemetry (uptime-second timestamps
   don't need wall time) but OTA manifests are correctly rejected
   `no_time_source` until a real server contact succeeds (fail-closed,
   proven this whole chain).
5. **Can records be created without trusted wall time?** Yes — every
   telemetry record's `ts` field is device-uptime seconds, never wall
   time, so record creation is entirely independent of whether wall
   time is known.
6. **How are they timestamped?** Device uptime seconds
   (`millis()`-derived); the SERVER stamps its own `recv_ms` wall-clock
   value on receipt (`server.py`'s `push()`, confirmed by direct code
   read) — the authoritative wall-clock timestamp for a record lives
   server-side, not device-side.
7. **Is timestamp corrected later?** No retroactive correction
   mechanism exists — the device's own `ts` field is fixed at creation.
8. **Can time move backwards?** Device uptime (`millis()`) cannot move
   backwards short of a reboot (which resets it to 0, a discontinuity,
   not a "backwards" jump within one boot). Server wall-clock
   (`recv_ms`) reflects the server's own system clock and could move
   backwards if the server's clock were ever adjusted backwards — not
   something this device-side firmware can detect or protect against.
9. **Timezone stored, or applied server-side only?** Neither — no
   timezone concept exists anywhere in this system (confirmed, §Part 8
   too).
10. **DST/timezone-change behavior?** Not applicable — there is nothing
    to change, since no timezone-aware timestamp exists anywhere.
11. **Maximum timestamp error during outage?** Device-side telemetry
    `ts` (uptime seconds) has effectively zero error (it's a direct
    hardware counter read, not subject to network outage at all).
    Server-side `recv_ms` (the actual wall-clock stamp) is assigned at
    the moment the SERVER receives a delayed/backlogged batch — meaning
    a record generated during a long offline period will be recorded
    server-side with a `recv_ms` reflecting when it was FINALLY
    delivered, not when it actually occurred. **This is a real,
    disclosed limitation**: this system does not preserve "when did this
    actually happen in wall-clock terms" for offline-buffered data
    beyond the relative `ts` (uptime-seconds) ordering — reconstructing
    true wall-clock time for a backlogged record would require the
    consuming application to do `server_receive_time - (uptime_at_receipt
    - ts_of_record)` arithmetic itself, which is not currently
    implemented anywhere in this codebase.

### Tests performed vs. not performed

This session did **not** simulate "no internet at boot", "malformed
server time", "backward server time", or "future server time" against
the real device (would require either a custom fake server or
disconnecting the real device's network, judged lower priority than the
dedup/gap/endpoint tests actually performed given finite session time) —
**code-inspected only** for these specific scenarios. The
`server_time_ms` overflow class of bug (backward-going/malformed VALUE
handling) IS real-hardware-proven via this whole chain's own
remediation (doc 35-36) — the parser now correctly fails closed on
malformed/overflowing values, proven live.
