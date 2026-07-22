# 08 — Database Idempotency and Reconciliation

## Idempotency mechanism (verified, unchanged by this session's fixes)

- `records` table: `PRIMARY KEY (device_id, seq)` — a real database-level
  constraint (`server.py` schema, `init_db()`), not an application-level
  `.exists()`-then-insert check. Every insert uses `INSERT OR IGNORE`, so a
  replayed/duplicate row is a guaranteed no-op at the database engine level,
  including under concurrent writers (SQLite serializes writers; confirmed
  behaviorally by `test_p0_1_ack_gap_remediation.py::test_concurrent_duplicate_pushes_produce_exactly_one_stored_row`,
  8 concurrent threads posting the identical record, exactly one row
  stored).
- `quarantined_records` table: `PRIMARY KEY (device_id, seq, schema_version)`
  — same `INSERT OR IGNORE` pattern, confirmed idempotent under replay
  (`test_replayed_batch_with_quarantined_record_does_not_double_quarantine`).
- **No `.exists()`-then-insert race exists anywhere in this codebase** —
  confirmed by reading every `INSERT` statement in `server.py`; all rely on
  the PK constraint + `INSERT OR IGNORE`, never a separate existence check
  followed by a conditional insert.

## Reconciliation identity (per the mandate's required format)

For the synthetic test batches exercised by `test_p0_1_ack_gap_remediation.py`
(the only reconciliation-shaped data this session generated — no synthetic
readings were ever sent to any production or bench server outside these
throwaway-SQLite-backed unit tests):

```
Unique device sequences generated  =  unique server rows stored
                                     + terminally quarantined rows
                                     + pending/never-submitted rows
```

Every test in that file constructs its own closed set of sequences and
asserts the exact resulting `records`/`quarantined_records` contents and
the exact `ack_seq` — there is no unexplained sequence, duplicate row, or
silent loss in any of the 14 scenarios covered. Concretely, for the
capstone scenario
(`test_quarantine_resolves_immediately_in_the_same_push_that_produced_it`):
seqs 1-6 generated → seq 2 quarantined (1 row), seqs {1,3,4,5,6} accepted (5
rows) → `ack_seq` progresses 2 → 5 → 6 across three pushes, with zero
unaccounted sequences at any point.

## What this session did NOT reconcile (honestly stated)

No end-to-end reconciliation against the **physically connected device**
was performed — that would require either (a) opening COM6 to read its real
`seq`/`acked_seq` state, or (b) sending it real network traffic, both
outside this session's authority. The reconciliation above is a **complete,
closed-system proof against synthetic test data**, not a live-device
reconciliation. A genuine device-to-server reconciliation (matching the
physically connected unit's actual local `seq`/`acked_seq` against what
`server/covio.db` holds for it) remains open, and is exactly the kind of
check the OTA physical test plan's step 4 (module 7 checks) would also
exercise incidentally, once authorized.

## Database transaction/concurrency verification

`push()`'s writes (records inserts, quarantine inserts, device-twin update,
quarantine audit event) are committed together in a single `c.commit()`
call at the end of the function — confirmed by reading the function: there
is exactly one `c.commit()` for the whole request, so a crash mid-request
leaves either all of a push's effects durable or none of them (SQLite's own
transaction semantics), not a partially-applied state. This was true before
this session's changes and remains true after — the P0-1 fix added more
statements inside the same transaction boundary, not a new one.
