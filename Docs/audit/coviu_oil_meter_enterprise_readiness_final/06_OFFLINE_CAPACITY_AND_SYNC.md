# 06 — Offline Sync Engine (Part 5)

Capacity math is in `03_DATA_PERSISTENCE_AND_FLASH.md` — this document
covers the sync ENGINE's behavior specifically.

## Real, this-session evidence at each representative duration

| Duration | Evidence type | What was observed |
|---|---|---|
| ~5 minutes | **Real, this session** | Multiple OTA-poll-cycle windows (server down between phases 36-38 and again this phase) showed backlog growing steadily (e.g. 89→105→269 records across successive checks), zero eviction, `health_state` correctly flipping to `"offline"` once `last_sync_ms_ago` grew stale |
| ~5 hours | **Not directly observed this session** (no single continuous 5-hour offline window occurred) — but the mechanism generating the ~5-minute evidence (append/pending/ack-cursor code) has no time-based special-casing at all; nothing in `queue.h`/`sync.h` behaves differently at 5 minutes vs. 5 hours offline | Projected from code inspection, not elapsed-time-proven |
| ~5 days | **Not proven, and per doc 03's real capacity finding, likely not achievable at this firmware's actual 1-second sampling rate** — the theoretical ceiling is ~27.3 hours (~1.14 days), and live filesystem usage already showed higher-than-expected consumption at a small backlog. **5 days is marked FAILED against the current configuration**, not merely "unverified." | Real capacity math (doc 03), not a completed 5-day soak |

## Verified sync-engine behaviors (real, this session + this chain's own prior real hardware execution)

- **Pulse/totalizer collection continues offline**: yes — PCNT counting
  is hardware, independent of network state (design property, and
  `totalizer_raw_pulses` never reset across any of this session's
  network interruptions).
- **Rows remain durable**: yes — confirmed via the live backlog-growth
  observation above (no data loss across a ~9-minute server-down window
  this session, zero quarantined/duplicate rows found afterward).
- **Queue grows predictably**: yes, observed directly (89→105→269).
- **No eviction occurs**: confirmed — no code path evicts, and no
  unexplained backlog drop was observed.
- **Health reflects backlog**: `health_state` correctly went
  `"offline"` once sync went stale — real, this session.
- **High-water / full-storage behavior**: code-verified (doc 05 §8-11),
  not executed at real capacity this session.
- **Reconnect behavior**: proven real, repeatedly, this entire
  audit trail — every server restart this session was followed by the
  device automatically resuming pushes within seconds (`last_push_http_code`
  returning to 200, `last_sync_ms_ago` resetting to a small value).
- **Batch draining / retry backoff**: `PUSH_PERIOD_MS=5000ms` fixed
  retry cadence (not exponential backoff) — confirmed by code read; no
  backoff escalation exists for repeated push failures (a real,
  disclosed absence, not a defect per se — a fixed 5-second retry is a
  reasonable, simple choice for this system's scale, but is not
  "backoff" in the adaptive sense the mandate's wording might imply).
- **Duplicate protection while offline-then-reconnecting**: proven live
  this session (05_DELIVERY_QUEUE_AND_ACK.md's dedup test).
- **Server ordering**: proven — the contiguous-ack computation
  (`server.py::push()`) is order-independent of arrival (it always
  recomputes the full contiguous range from stored+quarantined seqs),
  confirmed by the gap-then-fill test this session.
- **No starvation, no permanent sync lock**: no code path found that
  would starve or permanently lock the sync loop; every observed
  disconnect/reconnect this entire audit trail (dozens of cycles) has
  resolved cleanly.

## Verdict

**5-minute and 5-hour-class offline resilience: proven sound by design
and partially reproduced live this session. The specific "several days"
success criterion: FAILED at the firmware's actual configured 1-second
sampling rate** — real partition-size math (doc 03) puts the theoretical
ceiling at ~1.14 days, with live evidence suggesting real usable
capacity may be lower still. Achieving genuine multi-day offline
buffering would require either a slower sampling interval (a
configuration change, not attempted here) or a partition/architecture
change (out of this audit's execution boundary) — reported as an open
gap requiring a business decision, not silently assumed solved.
