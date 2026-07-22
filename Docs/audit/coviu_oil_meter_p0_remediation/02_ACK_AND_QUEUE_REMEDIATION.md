# 02 — RISK-01 Remediation: Acknowledgement-Gap / Queue-Pruning Deadlock

## Design chosen (and why it's the smallest correct fix)

The mandate's illustrative contract (`accepted`/`duplicates`/
`permanently_rejected`/`retryable` arrays) was considered and rejected as
larger than necessary. This architecture's actual failure modes, once
traced, reduce to exactly two terminal dispositions per `seq` once `push()`
has parsed a record:

- **accepted** → row lands in `records`.
- **permanently rejected** → row lands in `quarantined_records`, with a
  `reason` string, keyed `(device_id, seq, schema_version)`.

There is no third, genuinely *retryable* per-record outcome in this
receiver: once a record is parsed, its schema_version/record_type/required-
fields are either valid or not, deterministically — retrying the identical
bytes produces the identical outcome. "Retryable" only exists at the
**whole-batch** level (network failure, non-200 HTTP, auth failure), which
was already handled correctly before this fix (the device just keeps the
whole queue and retries later — see `sync.h::pushOnce()`'s existing
`if (code != 200) { ...; return false; }` path, untouched by this fix).

Given that, the fix is: **`ack_seq`'s contiguous-scan must treat the union
of `records` and `quarantined_records` seqs as "resolved"**, not `records`
alone. The wire contract stays exactly `{"ack_seq": <int>, ...}` — a single
integer the device already understands. **No firmware change was required.**
`queue.h`/`sync.h` are byte-for-byte unchanged; `ackThrough()`'s existing
"prune everything `<= ack_seq`" logic was always correct — only the
server's computation of what `ack_seq` should be was wrong.

An additive-only `quarantined: [{seq, reason}, ...]` field was added to the
push response (covering the requirement that "the server must explicitly
communicate the disposition of every submitted sequence") and a
`RECORDS_QUARANTINED` event is now written to the existing `device_events`
audit trail on every push that quarantines anything — satisfying
"permanent rejection details must be auditable in Coviu Device Manager/
server logs" using infrastructure that already existed (DM-Phase 4's event
log), not a new subsystem.

## Exact diff

`server/server.py`, `push()`:
- Added `newly_quarantined` list, appended to at both existing quarantine
  `INSERT` call sites (schema/record-type rejection, and missing-field
  rejection) whenever the record carried a `seq`.
- Added a `record_event(..., "RECORDS_QUARANTINED", ...)` call once per push
  (not once per row) if anything was quarantined.
- Replaced the ack-computation query:
  ```sql
  -- before:
  SELECT DISTINCT seq FROM records WHERE device_id=? ORDER BY seq
  -- after:
  SELECT seq FROM records WHERE device_id=?
  UNION
  SELECT seq FROM quarantined_records WHERE device_id=? AND seq IS NOT NULL
  ORDER BY seq
  ```
  The contiguous-scan loop itself is unchanged — only its input set changed.
- Response now includes `"quarantined": [...]` when non-empty (additive
  field; `sync.h::extractLong_()` looks up `"ack_seq"` by name and ignores
  unknown keys, so this is wire-compatible with every already-fielded
  device using the pre-fix firmware).

## Why every one of the 12 required design properties holds

1. **Can't silently disappear** — already true pre-fix (stored in
   `quarantined_records` with full raw JSON); now additionally surfaced in
   the response and the audit log.
2. **Can't permanently block later readings** — fixed (this is the core
   change).
3. **Server explicitly communicates disposition** — the `quarantined` field
   plus the `device_events` row.
4. **Accepted / duplicate / retryable / permanent distinguished** — accepted
   = in `records`; duplicate = `INSERT OR IGNORE` no-op (same `ack_seq`
   result on replay); permanent = in `quarantined_records`; retryable =
   whole-batch transport failure (unchanged, pre-existing, correct).
5. **Watermark advances only when every seq below it has a terminal
   disposition** — true by construction: the union query only contains rows
   with a terminal disposition; a genuine gap (never received at all) is
   in neither table and correctly halts the scan (see
   `test_genuine_gap_in_receipt_correctly_halts_ack_not_treated_as_resolved`).
6. **Retryable failures never terminal** — a whole-batch transport failure
   never reaches `push()`'s DB writes at all (network/auth failure happens
   before or independent of them), so nothing is written, nothing resolves.
7. **Malformed acks don't advance local state** — unchanged pre-existing
   firmware behavior (`sync.h`'s "200 but no ack_seq" guard, Invariant 6).
8. **Reboot during ack processing is safe** — unchanged; `queue.h`'s
   dual-slot CRC `AckRec` persistence is untouched by this fix.
9. **Lost response after commit handled idempotently** — `INSERT OR IGNORE`
   plus stateless recomputation of `ack_seq` from durable data means a
   retried identical batch produces the identical response
   (`test_replayed_batch_is_idempotent_and_ack_unchanged`).
10. **Out-of-order/replayed batches safe** — `INSERT OR IGNORE` is
    order-independent; the ack computation re-derives from stored data
    regardless of arrival order
    (`test_out_of_order_batch_ack_still_only_advances_through_contiguous_prefix`).
11. **Auditable** — `device_events` row, visible via `/admin/events` (now
    authenticated, see doc 04).
12. **Local queue retains evidence to explain a skip** — this is
    deliberately satisfied **server-side**, not device-side: the server's
    `quarantined_records` table already stores the full raw JSON of every
    rejected row, permanently and centrally. Duplicating that evidence
    on-device would consume exactly the flash budget RISK-04 is about
    protecting, for a flash-constrained device whose local copy is already
    disposable the moment the server has durably resolved it. This is a
    deliberate architectural choice, not an oversight — recorded here
    explicitly rather than silently deviating from the mandate's literal
    per-property wording.

## Test evidence

`test/native/test_p0_1_ack_gap_remediation.py` — 14 tests, all passing,
covering scenarios 1-4, 6-13, 17-18 of the mandate's 18-item list directly
against the real Flask app + a throwaway SQLite DB. Scenario 5 ("retryable
failure in the middle") is addressed as a documented non-applicability
(see file docstring: no such per-record state exists in this architecture).
Scenarios 8-9 and 14-16 are called out explicitly in the test file as
NOT COVERED HERE with the specific reason (concurrency covered separately;
9 and 14-16 are device-side C++ logic with no host test harness in this
repo — see `09_AUTOMATED_TEST_EVIDENCE.md` for the full breakdown).

The one pre-existing test that encoded the bug as expected behavior
(`test_adr001_schema.py::test_unsupported_version_quarantined_alongside_accepted`,
which asserted `ack_seq == 1` for a batch where seq 2 was quarantined) was
corrected to assert the fixed value (`ack_seq == 2`), with a comment
explaining exactly why the old assertion was itself proof of the bug.

## Database requirements (mandate's explicit checklist)

- Real DB uniqueness constraint: `PRIMARY KEY (device_id, seq)` on
  `records` — confirmed present, unchanged by this fix (`server.py:161-164`).
- Transaction-safe duplicate handling: `INSERT OR IGNORE`, confirmed via
  `test_concurrent_duplicate_pushes_produce_exactly_one_stored_row` (8
  concurrent threads posting the identical record; exactly one row stored).
- No `.exists()`-then-insert race: confirmed — this codebase never does a
  SELECT-then-INSERT pattern; it relies entirely on the DB-level PK +
  `INSERT OR IGNORE`, which is race-free by construction (SQLite serializes
  writers).
- Stable response for replayed batches: confirmed
  (`test_replayed_batch_is_idempotent_and_ack_unchanged`).
- Explicit quarantine/audit storage: confirmed, pre-existing
  (`quarantined_records` table), now also mirrored into `device_events`.
