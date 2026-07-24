# 15 — Long-Duration Stability (Part 18)

## What actually exists (real, this session + this chain, not extrapolated)

```
boot_id observed range this audit trail: 19 -> 29 (10 reboots across
  this whole multi-session engagement, each one deliberately caused --
  USB flashes, OTA installs, one console "reboot" -- not spontaneous)
Unexpected/unexplained resets: ZERO observed across this entire trail
Longest single continuous uptime observed: on the order of tens of
  minutes to a few hours between deliberate actions -- NOT a multi-day
  soak; this session's own pacing (waiting on 5-minute OTA poll cycles,
  etc.) means no single boot has run unattended for anywhere close to
  30 days
Total records generated this device, this entire trail: 57,506+ (server
  DB, live-confirmed this session)
Total records synced: matches acked_seq at every checkpoint (fully
  reconciled, doc 16)
Duplicates: ZERO, at every checkpoint this entire trail
Missing sequences: ZERO, at every checkpoint
Quarantined rows: ZERO, at every checkpoint
Heap trend: not tracked across time this session (only point-in-time
  reads, e.g. free_heap_bytes ~265-266KB consistently across many
  checks -- no observed downward drift across this session's checks,
  but checks were not frequent/regular enough to call this a trend
  analysis)
Filesystem trend: capacity_pct_used observed to RISE over this session
  (from ~43% early to ~59% by this final phase) -- see doc 03's capacity
  finding; this IS a real, observed upward trend worth flagging, not
  just a snapshot
Wi-Fi reconnect count: not explicitly counted, but multiple implicit
  reconnects occurred across every server-down/up cycle this session
Server retry count: not explicitly counted; qualitatively frequent
  (PUSH_PERIOD_MS=5000ms retry cadence throughout every outage window)
Alarm history: OTA_FAILED observed and cleared correctly once
  (doc 08); no other alarm type observed to fire this session
```

## Verdict

**`30-DAY STABILITY — NOT CERTIFIED`.** No claim of 30-day continuous
operation is made — none has occurred. The **filesystem usage trend
rising from ~43% to ~59% over this session's cumulative activity** is a
genuine, real observation this soak plan should specifically watch —
it may indicate the queue/segment cleanup isn't reclaiming space as
completely as the design intends (doc 03), which would directly threaten
long-duration operation regardless of how well any single reboot
recovers.

## 30-day soak test plan (to be executed, not performed this session)

```
Duration: 30 continuous days, real device, real (or realistically
  simulated, clearly labeled) sensor pulses at the firmware's actual
  1Hz TELEMETRY_PERIOD_MS rate.

Daily:
  - Record boot_id, uptime, reset_reason (raw + mapped)
  - Record free_heap_bytes, heap_low_water_mark_bytes (watch for any
    downward drift -- a leak would show here)
  - Record capacity_pct_used (watch specifically for the rising-trend
    behavior this session already observed at a much smaller scale --
    does it plateau, or continue climbing toward 100%?)
  - Record queue.backlog, acked_seq, last_seq
  - Reconcile server DB: contiguous range, 0 duplicates, 0 quarantined
  - Note any alarm firing

Scheduled disruptions (not left to chance):
  - At least 3 intermittent internet/Wi-Fi outages of varying duration
    (minutes to hours) at unpredictable times
  - At least 2 controlled server outages (bench-server-equivalent)
  - At least 2 deliberate reboots (console command, matching this
    session's own proven-safe method)
  - If real oil flow is available by this point: at least 1 full
    real production cycle with physical reconciliation (doc 14's tests)

Final check (day 30):
  - Full database continuity check (contiguous, 0 duplicates, 0
    quarantined) across the ENTIRE 30-day window, not just the final
    snapshot
  - Confirm accepted_security_floor unchanged (unless a genuine,
    approved OTA occurred during the window, in which case confirm it
    advanced correctly and only then)
  - Confirm build_commit unchanged (unless an approved OTA occurred)
  - Confirm capacity_pct_used trend: did it stabilize, or is it still
    climbing? This is the single most important open question this
    session's own shorter-duration observation raised.
```

## Does the lack of a 30-day soak block deployment?

- **Controlled pilot**: does not block — a supervised, short-duration
  pilot with active daily monitoring (per the plant-readiness series'
  own first-shift/following-morning checklists) is exactly the kind of
  bounded, observed operation that substitutes for a full soak in the
  short term.
- **Normal production**: **blocks** — routine, less-supervised operation
  for weeks/months at a time should not begin without at least one
  completed 30-day soak demonstrating the filesystem-usage trend
  genuinely stabilizes (not just "no crash occurred").
- **Remote unattended operation**: **blocks**, for the same reason, plus
  the already-established rollback-limitation blocker (doc 08).
