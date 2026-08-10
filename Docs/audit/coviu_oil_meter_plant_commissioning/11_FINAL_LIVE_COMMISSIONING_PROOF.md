# 11 — Final Live Commissioning Proof (ack-watermark fix verified on hardware)

**Date:** 2026-07-25 (evening session, 22:05–22:32 local). **Device:**
esp32-F4E5B2858428 — confirmed **ESP32-S3 (QFN56, rev v0.2), USB-Serial/JTAG
mode**, COM6 @115200, fw 1.0.0 commit `e5a593b`, boot_id 54.
**Network:** Amar's A34 hotspot (2.4 GHz, Ch 11; laptop co-connected, 99%
signal). **Server:** production `https://data.funtastik.co.in`, deployed SHA
`0a9aec7c` containing ack-watermark fix `7b011048`.
**Constraints honored:** no firmware/ERP/API-key/URL/Wi-Fi/calibration/
totalizer/NVS modification. All device interaction read-only + resets.

## VERDICT: ✅ A — COVIO PHYSICAL COMMISSIONING COMPLETE

The doc-10 blocker (server `ack_seq` stuck at 2) is fixed and proven on the
physical device: the ack watermark jumped past the 58736–58785 window, the
~5,300-record backlog fully drained to 0, and fresh live readings are now
transmitted and acknowledged within one 5-second sync cycle, strictly
monotonically, with zero auth/TLS/queue errors.

## Recorded values (required record)

| Item | Value |
|---|---|
| Power-on timestamp (captured boot) | **2026-07-25 22:24:49.003** local (esptool hard reset; boot_id 54) |
| Assigned IP | **10.120.222.188** (DHCP on Amar's A34; local API + mDNS `covio-858428.local` reachable) |
| Previous acked_seq (doc-10 stuck state) | **58735** persisted / server ack **2** |
| Persisted acked_seq at this boot | **66015** (`[Q] acked_seq=66015` at t+0.5 s) |
| First repaired acked_seq observed | **64035** (22:11:30, pre-power-cycle drain); first post-power-cycle ack **66022** |
| Backlog before / after | **632** (doc-10 final, 19:26) → **0** (22:14:47, and still 0 at 22:31:33) |
| Newest transmitted seq | **66137** |
| Newest acknowledged seq | **66137** (`acked_seq == last_seq`, backlog 0) |
| Totalizer | 401 raw pulses — unchanged, as required |

## Evidence timeline

**Phase 1 — backlog drain observed (pre-power-cycle).** Device had been
powered at ~22:05:25 (USB plug-in; derived from `uptime_ms` 562582 at
22:14:47). First serial attachment 22:11:24 showed the fix already working —
the exact drain the fix was deployed for:

```
[  6.078] [SYNC] acked_seq=64035 (sent 50)
[ 10.110] [SYNC] acked_seq=64085 (sent 50)
   ... +50 every ~5 s, strictly increasing, HTTP 200 each cycle ...
[112.297] [SYNC] acked_seq=65035 (sent 50)
```

By 22:14:47 `/api/v1/status`: `backlog=0, acked_seq=65997, last_seq=65997,
last_push_http_code=200`. **This is the ack jump + queue drain proof:** server
ack went 2 → >65k; the previously re-sent-forever window 58736–58785 was
acknowledged and pruned; drain rate 50 records / 5 s cycle.

**Phase 2 — power-cycle and captured cold boot.** 22:24:47 esptool
(read-only `chip-id`, RAM-only stub, no flash access) issued a hard reset;
boot captured from t+0.5 s:

```
[ 0.500] === Covio Oil Flow Meter 1.0.0 ===
[ 0.500] build_commit=e5a593b...  build_dirty=0
[ 0.500] device_id=esp32-F4E5B2858428  boot_id=54
[ 0.500] [TOT] recovered total=401 writes=132034
[ 0.500] [Q] acked_seq=66015            <- persisted watermark, FAR beyond 58735
[ 1.015] [NET] connecting to Amar's A34
[ 1.015] [LOCALAPI] HTTP server started on :80
[ 1.015] [LOCALAPI] mDNS started: covio-858428.local
[12.281] hostByName(): DNS Failed for data.funtastik.co.in   <- transient, once
[12.281] [SYNC] push HTTP -1 — keeping queue
[17.375] [SYNC] acked_seq=66022 (sent 7)   <- boot-accumulated records acked
[20.953] [SYNC] acked_seq=66023 (sent 1)   <- fresh live readings, acked each cycle
   ... 71 acks over 6.5 min, 66022 -> 66133, strictly monotonic ...
[389.422] [SYNC] acked_seq=66133 (sent 1)
```

**Phase 3 — steady state.** Final `/api/v1/status` (22:31:33, uptime 400 s):
`backlog=0, acked_seq=66137, last_seq=66137, capacity 68.2%,
failed_write_count=0, last_push_http_code=200, rssi −47 dBm, health ok`.

## Checklist results (all pass)

| Step | Result | Evidence |
|---|---|---|
| Wi-Fi connects | ✅ | `[NET] connecting` t+1.0 s; LOCALAPI + mDNS up; RSSI −47…−49 dBm |
| IP assigned | ✅ | 10.120.222.188 answers local API; uptime matches this boot |
| TLS succeeds | ✅ | HTTP 200s over https:// with pinned Let's Encrypt CA (no setInsecure) |
| GET /api/iot/flow/config | ✅ | no 404/401/TLS error this boot; silent-on-success by design (sync.h only logs version change); the push 200s prove path+key+TLS |
| POST /api/iot/flow/push → 200 | ✅ | 71 consecutive `acked_seq=… (sent N)` cycles; `last_push_http_code=200` |
| acked_seq advances from 2 past 58785 | ✅ | server acks observed at 64035→66137 (all ≫ 58785) vs doc-10's stuck 2 |
| Persisted acked_seq beyond 58735 | ✅ | boot NVS: `[Q] acked_seq=66015` |
| Backlog decreases | ✅ | 632 → 0 (drained at 50 records/cycle; stayed 0) |
| Repeated seq 58736–58785 stops | ✅ | window advanced +50 every cycle in phase 1; those seqs acked + pruned |
| Fresh seq > 58785 transmitted | ✅ | 118 records seq 66016–66133 sent during capture; +66134–66137 by final status |
| Fresh seq acknowledged | ✅ | every push acked same cycle; `acked_seq == last_seq` |
| Ack monotonic | ✅ | 71 ack values strictly increasing (verified programmatically) |
| No 401/404/TLS/queue/reboot-loop | ✅ | zero occurrences in 6.5-min capture; exactly one boot banner; only anomaly: one transient DNS failure at t+12.281 s (`push HTTP -1 — keeping queue`), self-recovered by t+17.375 s — same known transient as doc 10. Boot-time littlefs `failA/failB.bin does not exist` lines are the fail-journal probe finding no failed writes (expected), and `logical_id NOT_FOUND` is an optional NVS key. |

## Incident during test (disclosed): reset method put chip in ROM download mode

This board resets via the **USB-Serial-JTAG peripheral**, not a classic
UART-bridge EN circuit. An RTS-only EN pulse does nothing, and a DTR/RTS
sequence passing through the (1,1) state (attempted 22:16:41) is the
S3's *enter-bootloader* pattern — the chip sat in ROM download mode
(USB enumerated, app dead, off Wi-Fi) from 22:16:41 to 22:24:49. Recovery:
`esptool --before no-reset --after hard-reset chip-id` (read-only; stub runs
from RAM; no flash/NVS touched), which doubled as the required power-cycle.
No data was lost — the queue journal is on flash and `[TOT]`/`[Q]` recovered
intact — and the post-recovery capture above is the commissioning evidence.
Documented for future sessions: never toggle DTR/RTS on this board; use
esptool hard reset or the console `reboot` command.

## Rules compliance

No firmware rebuild/flash, no ERP change, no API key/URL/Wi-Fi/calibration
change, no totalizer/NVS modification, no factory reset. API key never
printed. esptool usage was read-only (`chip-id`). Capture logs retained in
session scratchpad (`final_commissioning.log`, `final_commissioning4.log`).
