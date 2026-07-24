# 08 — First-Shift Monitoring Sheet (blank template)

Mandatory observation windows. Template only — to be completed onsite.

## First 30 minutes (continuous monitoring)

Record at 5-minute intervals:

| Time | Pulses/totalizer | Network (WiFi RSSI, server reachable?) | Queue backlog | Last sync | Health state | Resets? | Alarms? |
|---|---|---|---|---|---|---|---|
| T+0 | | | | | | | |
| T+5 | | | | | | | |
| T+10 | | | | | | | |
| T+15 | | | | | | | |
| T+20 | | | | | | | |
| T+25 | | | | | | | |
| T+30 | | | | | | | |

## First production cycle reconciliation

```
Beginning totalizer:        ______________
Ending totalizer:           ______________
Physical oil quantity (reference measurement): ______________
Server records for this cycle: ______________
Duplicates found (should be 0): ______________
Missing data / sequence gaps (should be 0): ______________
```

## End of first shift

```
Uptime:                      ______________
Boot ID:                     ______________
Reset reason (final):        ______________
Queue state (backlog, capacity_pct_used): ______________
Server continuity (contiguous? quarantined count?): ______________
Total quantity for the shift: ______________
Physical reconciliation result: ______________
Operator observations (free text):
______________________________________________________
______________________________________________________
Unresolved alarms (list, or "none"): ______________
```

## Following morning (before resuming normal operation)

```
[ ] Device remained online, OR queued safely through any disconnect
[ ] No silent reset loop occurred overnight (check boot_id history / reset reasons)
[ ] No missing records (server sequence range contiguous)
[ ] No duplicate records
[ ] accepted_security_floor unchanged from the prior evening's value
[ ] build_commit unchanged (no unexpected/unauthorized OTA occurred)
[ ] Storage remains healthy (capacity_pct_used, failed_write_count == 0)
```

**Do not mark normal production ready based only on a successful boot.**
All boxes above must be independently checked against real device/server
data before declaring the overnight window clean.
