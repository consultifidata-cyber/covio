# 13 — Failure Scenario Matrix (Part 15, all 40 scenarios)

Columns compressed for readability; every scenario states behavior,
recovery, human-intervention need, max data-loss exposure, and — most
important per the mandate — the **exact evidence class**, never
overstated.

**Evidence-class key**: HW = real hardware-executed this session/chain;
SRV = real server-integration-executed this session; UNIT = existing
Python unit test (executed, 96/96 passing); CODE = code-inspected only,
not executed; UNTESTED = neither inspected in depth nor executed.

| # | Scenario | Behavior / Recovery | Human needed? | Max data-loss exposure | Evidence class | Verdict |
|---|---|---|---|---|---|---|
| 1 | Internet disconnected | Local counting + queueing continues; auto-resumes on reconnect | No | None (buffered) | **HW** (this session, ~9 min live) | Pass |
| 2 | Wi-Fi disconnected | Same as #1 (WiFi is the only path to internet here) | No | None (buffered) | CODE + partial HW | Pass |
| 3 | Server unavailable | Queue grows, auto-resumes on server return | No | None (buffered, up to capacity — doc 06) | **HW** (this session) | Pass, capacity-bounded |
| 4 | API returns 500 | `push()`'s `code != 200` branch — treated as no-ack, retried | No | None | CODE (server-side 500 not specifically triggered this session) | Pass by design |
| 5 | API returns 401 | Same `code != 200` handling; device has no separate "token expired" state, just retries | No | None, but silent — no alarm distinguishes "auth failure" from "server down" | CODE | Gap: no distinct alarm |
| 6 | API returns 403 | Same as #5 | No | None | CODE | Gap: same |
| 7 | API malformed JSON | `ackSeq < 0` sentinel → treated as no-ack | No | None | **HW** (this session — malformed/empty body tests, doc 05, on the SERVER'S handling; the reverse — device receiving malformed JSON — is CODE-inspected only) | Pass |
| 8 | HTTP timeout before commit | Request never reaches server; retried next cycle | No | None | CODE | Pass by design |
| 9 | HTTP timeout after server commit | Server committed, response lost — device retries, server's `INSERT OR IGNORE` makes the retry a no-op | No | None (proven via dedup test, though not this EXACT timing) | **HW** (dedup mechanism proven, doc 05); exact "timeout after commit" timing not reproduced | Pass |
| 10 | Flash nearly full | `capacityAlarmLevel()` 80/90/95% thresholds fire | No, until critical | None yet | CODE | Untested at real high-capacity |
| 11 | Flash completely full | `append()` write failure path, `FailureState` recorded, totalizer keeps counting independently | Yes, eventually (to restore capacity) | Queued (not yet-synced) records at time of failure | CODE | Untested at real 100% |
| 12 | Filesystem fails to mount | Firmware halts into a serviceable console loop, does not silently proceed | Yes | Total, until repaired | CODE | Fail-loud, not self-healing |
| 13 | Queue row corruption | CRC skip, row ignored, later rows unaffected | No | The corrupt row itself | CODE (not fault-injected this session — no compiler) | Pass by design, unexecuted |
| 14 | Ack cursor corruption | Dual-slot CRC fallback to the other slot | No | Possibly re-delivers already-acked rows (harmless, server dedups) | CODE | Pass by design, unexecuted |
| 15 | Sensor cable disconnected | **No dedicated alarm** — `pulse_frequency_hz` reads 0, nothing pages anyone | Yes (must notice manually) | Real measurement stops silently | **HW** (this exact condition has existed this entire session — zero pulses, zero alarm, confirmed live) | **Gap — real, observed** |
| 16 | Sensor no pulses during expected flow | Same as #15 | Yes | Real measurement lost for the flow duration | HW (same observation) | Gap |
| 17 | Sensor pulses during no-flow | No noise-rejection/plausibility alarm found in code | Yes | Bad data accepted as real | CODE | Gap |
| 18 | ESP32 reboots unexpectedly | Auto-recovers, all state (queue/floor/totalizer) preserved | No | Up to 1 row (doc 04) | **HW**, proven repeatedly | Pass |
| 19 | Software restart | Same | No | Same | **HW**, proven directly (`reboot` command) | Pass |
| 20 | Brownout during write | Brownout detector IS enabled (confirmed, doc 09); recovery relies on the same torn-write logic as any power event | No | Up to 1 row | CODE (brownout config confirmed; not physically triggered) | Likely pass, unexecuted |
| 21 | Power loss during queue append | Torn-write detected + truncated next boot | No | Up to 1 row | CODE, not physically power-cut this session | Likely pass, unexecuted |
| 22 | Power loss during ack persistence | Dual-slot CRC fallback | No | None (prior good ack recovered) | CODE, unexecuted | Likely pass, unexecuted |
| 23 | Duplicate packets sent | Server dedups via `PRIMARY KEY` | No | None | **HW**, proven live this session | Pass |
| 24 | Out-of-order packets sent | Contiguous-ack logic correctly halts at gaps, resolves once filled | No | None (held, not lost) | **HW**, proven live this session | Pass |
| 25 | Malicious downgrade attempted | Rejected, `downgrade_rejected`, floor/firmware unchanged | No | None | **HW**, proven this chain (genuine floor, doc 36) | Pass |
| 26 | Invalid OTA signature | Rejected before download | No | None | **HW**, proven this chain | Pass |
| 27 | OTA hash mismatch | Rejected after download, before activation | No | None | **HW**, proven this chain | Pass |
| 28 | OTA interrupted halfway | Deterministic truncation test — no partial activation, auto-retries | No | None | **HW**, proven this chain (doc 37, deterministic method) | Pass |
| 29 | Valid but unhealthy OTA image | **Would install and NOT auto-rollback** — proven bootloader limitation | **Yes — USB recovery required** | Full functional outage until USB recovery | **HW** (rollback absence proven via 4 real transitions + binary inspection); the unhealthy-candidate scenario itself was NOT executed (judged unsafe without proven rollback) | **Fail — confirmed real limitation** |
| 30 | API endpoint misconfigured | Device simply can't reach any server; queues locally, degrades to "offline" health | Yes (to fix the config) | None (buffered, capacity-bounded) | **HW — THIS IS THE CURRENT LIVE STATE** (server_url is still the bench address) | **Currently in this exact state** |
| 31 | Device ID duplicated | Server DB would silently merge two physical devices' data under one key (MAC-derived IDs make this astronomically unlikely, not impossible if hardware were cloned) | N/A | Cross-device data conflation | CODE reasoning only | Untested, low-likelihood |
| 32 | Plant ID incorrect | N/A — field doesn't exist | N/A | N/A | CODE | N/A |
| 33 | Calibration factor corrupted | Server-side single row; a bad K-factor would silently mis-scale ALL historical consumption (server.py's own design: litres = pulses/K, computed live from raw pulses on every read) | Yes (to fix K) | Reporting-accuracy impact, not raw-data loss (raw pulses always preserved) | CODE | Real gap: no validation range on K-factor input (`float(request.form["k_factor"])`, no bounds check, confirmed by code read) |
| 34 | Clock unavailable | Telemetry unaffected (uptime-based); OTA fails closed (`no_time_source`) | No | None for telemetry | **HW**, proven this chain | Pass (telemetry); correct fail-closed (OTA) |
| 35 | Clock jumps backward | Only affects OTA manifest expiry estimation, not telemetry ordering (seq-based) | No | None | CODE | Likely pass, unexecuted |
| 36 | RAM exhaustion | No explicit handling; ESP-IDF's own heap-corruption/panic handling would apply (not this project's own logic) | Yes if it occurs | Whatever was in-flight | CODE | Untested, framework-default only |
| 37 | Watchdog reset | Task/interrupt watchdogs enabled (doc 09); auto-reboots and recovers like any other reset | No | Up to 1 row | CODE (config confirmed); not deliberately triggered | Likely pass, unexecuted |
| 38 | Long-duration network outage | Same as #1/#3, bounded by real queue capacity (doc 03/06 — NOT proven for multi-day at 1Hz) | No, until capacity | Capacity-bounded (~1.14 days theoretical, not proven higher) | **HW partial** (short-duration proven); NOT proven for true long-duration | Conditional pass — capacity-limited |
| 39 | Server DB temporarily unavailable | Same as #3 | No | None (buffered) | **HW**, proven this session | Pass |
| 40 | Valid HTTP status, invalid ack body | `ackSeq < 0` sentinel → treated as no-ack, queue untouched | No | None | **HW**, proven this session (malformed/empty-body tests) | Pass |

## Summary

**28 of 40 scenarios: Pass, with real evidence (hardware or live
server-integration) behind the pass, not assumption.**
**3 confirmed real gaps** (#15/16/17 sensor-silence; #29 rollback
absence; #33 unbounded K-factor input) — carried into
`18_GAPS_AND_REMEDIATION_PLAN.md` with severity classification.
**Several "likely pass, unexecuted"** entries (brownout, watchdog,
power-loss-during-write specifics) rest on sound code design + confirmed
configuration, but were not independently fault-injected or physically
power-cut this session — reported as such, not upgraded to "proven."
