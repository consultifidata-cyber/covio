# 16 — Independent Data Reconciliation (Part 20)

Final, fresh snapshot this session — not carried forward from an earlier
document.

```
Device highest generated sequence (last_seq):        58,210
Device highest acknowledged sequence (acked_seq):     58,209
Device-reported pending backlog:                           1
Server DB: unique committed records for this device: 58,209
Server DB: min sequence:                                    1
Server DB: max sequence:                              58,209
Server DB: quarantined rows:                                0
Server DB: duplicate rows (COUNT(*) vs COUNT(DISTINCT seq)): 0 (implied
  by PRIMARY KEY(device_id, seq) -- a duplicate is structurally
  impossible in this schema, not merely absent by chance)
Totalizer start (this session's first check, doc 27-era): 401
Totalizer end (this final check):                          401
  (unchanged -- no real flow occurred this entire session, per doc 14)
Reboot count this session/chain: 10 (boot_id 19 -> 29)
OTA transitions this chain: 2 genuinely successful, 2 correctly
  interrupted/aborted, multiple correctly rejected (signature/hash/
  hardware/downgrade)
```

## The required identity

```
captured records = server unique committed records
                  + currently durable unsynced records
                  + explicitly identified corrupt/lost records

58,210            = 58,209  +  1  +  0

BALANCES EXACTLY. Zero unexplained difference.
```

This is the live, final state as of this audit. It reflects the
CUMULATIVE history of this entire multi-session engagement (every reboot,
every OTA test, every dedup/gap test using a separate synthetic
device_id that was confirmed cleaned up and never touched this count) —
not a single isolated window, but the full running total, and it still
balances exactly.

## What this does and does not prove

**Proves**: for the ENTIRE observable history of this specific physical
unit across this whole audit engagement, no record was silently lost,
duplicated, or left unaccounted for. This is real, checked, exact-match
evidence — not an estimate.

**Does not prove**: behavior under conditions never exercised this
session — a true physical power-cut (doc 04), a filled-to-capacity
queue (doc 03/06), a genuinely unhealthy OTA candidate (doc 08), or
multi-week continuous unattended operation (doc 15). The reconciliation
identity balancing perfectly under everything that WAS tested is
meaningful, real evidence — but it is not a claim that it would still
balance under those untested conditions.
