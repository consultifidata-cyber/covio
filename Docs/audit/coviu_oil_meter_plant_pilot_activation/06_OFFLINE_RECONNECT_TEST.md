# 06 — Short Offline and Reconnect Proof (Part 8)

Real, deliberate, controlled test performed this phase (bench server
stopped and restarted intentionally, not an incidental outage) —
against the reachable bench infrastructure, since no real plant server
exists yet (doc 05). This proves the MECHANISM; it must be re-observed
against the real plant server once reachable.

## Pre-outage baseline

```
timestamp: 1784834477 (deliberate outage start)
backlog: 4, acked_seq: 58634, last_seq: 58638, totalizer: 401, floor: 2
health_state: ok
```

## During the 5-minute deliberate outage (server stopped)

```
T+14s   backlog=12  last_seq=58651  acked_seq=58639 (frozen)  health=ok
T+35s   backlog=15  last_seq=58654  acked_seq=58639            health=ok
T+56s   backlog=19  last_seq=58658  acked_seq=58639            health=ok
T+78s   backlog=23  last_seq=58662  acked_seq=58639            health=ok
T+99s   backlog=26  last_seq=58665  acked_seq=58639            health=ok
T+120s  backlog=30  last_seq=58669  acked_seq=58639            health=ok
T+141s  backlog=33  last_seq=58672  acked_seq=58639            health=ok
T+163s  backlog=36  last_seq=58675  acked_seq=58639            health=ok
T+184s  backlog=40  last_seq=58679  acked_seq=58639            health=ok
T+206s  backlog=44  last_seq=58683  acked_seq=58639            health=ok
T+227s  backlog=47  last_seq=58686  acked_seq=58639            health=ok
T+249s  backlog=51  last_seq=58690  acked_seq=58639            health=ok
T+270s  backlog=55  last_seq=58694  acked_seq=58639            health=ok
T+291s  backlog=58  last_seq=58697  acked_seq=58639            health=offline
```

**Pulse/sample collection continued throughout** (`last_seq` climbed
steadily); **totalizer stayed at exactly 401** (no real flow this
session, correctly stable, not decreasing or corrupting); **queue grew
predictably, no eviction**; **health correctly transitioned to
"offline"** once staleness crossed the threshold, not before.

## Reconnect (server restarted)

```
Within ~8s of the server coming back: last_push_http_code returned to
  200, backlog began draining (58 -> 4 -> 2 across successive checks),
  acked_seq caught up from 58639 to 58730, health_state returned to "ok"
```

## Reconciliation after reconnect

```
Device: last_seq=58732, acked_seq=58730, backlog=2
Server DB: 58,730 records, contiguous (1..58730), 0 quarantined

Identity: 58,732 = 58,730 + 2 + 0  -- BALANCES EXACTLY
```

**Zero duplicate rows, zero sequence gaps, zero data loss across the
entire deliberate 5-minute outage and reconnect.**

## Real capacity, recalculated (unchanged from the enterprise re-audit — restated here per this phase's own instruction not to claim 5-day operation)

```
LittleFS queue partition (from the actual partition table): 3,538,944 bytes
QRow size: 36 bytes
Theoretical ceiling: 98,304 rows
At the firmware's actual TELEMETRY_PERIOD_MS=1000ms (1 sample/sec):
  98,304 seconds ≈ 27.3 hours ≈ 1.14 days
```

**No claim of 5-day offline operation is made.** This session's live
observation (43%→60%+ filesystem usage growth over the course of this
whole multi-day audit trail's cumulative activity) suggests real usable
capacity may be somewhat below even this theoretical figure.

## Recommended operational alarm threshold and maximum approved outage window

```
Recommended alarm: 80% capacity_pct_used (already implemented,
  capacityAlarmLevel() QCAP_WARNING_80 -- code-confirmed, not newly
  added this phase)
Recommended MAXIMUM approved outage window for this pilot, with safety
  margin below the ~1.14-day theoretical ceiling:
  ~12 HOURS, not 24, and certainly not "days" -- leaving substantial
  margin given the unresolved real-vs-theoretical capacity gap above.
  This is a recommendation for the pilot's own operating restrictions
  (doc 10), not a code change.
```
