# 05 — Guaranteed Delivery, Idempotency, Queue Management, Acknowledgement (Parts 4, 6, 7)

## Part 4 — End-to-end trace: pulse → sample → sequence → flash queue → HTTP → server → ack → prune

Traced directly from `queue.h`, `sync.h`, `server.py` (all re-read this
session).

1. **Sequence identity**: a single global `uint32_t seq` in
   `covio_firmware.ino`, monotonically incremented, continues across
   reboots (seeded from `totalizer.lastSeq()` at boot) — **never resets
   to 1 on reboot**, confirmed by direct code read.
2. **Device identity inclusion**: `Telemetry::toJson()` stamps
   `device_id` (not independently re-read this session, but consistently
   observed correct in every live push this entire audit trail).
3. **Plant identity inclusion**: **NONE — no plant-ID field exists
   anywhere in the wire format or the server's schema** (confirmed by
   reading `server.py`'s full `records` table DDL, doc 04 of this
   report's baseline). This is a real, material gap for multi-plant
   fleets (see 18_GAPS_AND_REMEDIATION_PLAN.md).
4. **Server uniqueness constraint**: `PRIMARY KEY (device_id, seq)` on
   the `records` table (`server.py`, read directly this session) — this
   IS the exactly-once-storage guarantee, enforced at the database
   layer via `INSERT OR IGNORE`.
5. **Transaction boundary**: one `push()` HTTP request = one SQLite
   transaction (implicit per-connection commit at the end of `push()`)
   — all records in a batch, plus the device-registry update, commit
   together or (on an unhandled exception) not at all.
6. **Ack generation timing**: computed fresh on every `push()` call, as
   the highest **contiguous** sequence resolved (accepted OR permanently
   quarantined) for that device — never a value the client supplies or
   influences.
7. **Local ack parsing**: `sync.h::pushOnce()` requires a successful
   parse of `ack_seq` from the response body; a bare HTTP 200 with no
   parseable `ack_seq` is explicitly **not** treated as an ack (code
   comment: "Invariant 6").
8. **Retry behavior**: unacked rows simply remain in the queue and are
   re-included in the next `pending()` read — no separate retry-counter
   state exists (see Part 6 below on why this project doesn't model
   discrete row states).
9. **Duplicate behavior**: proven this session, live (see Real Tests
   below).
10. **Out-of-order / partial-batch behavior**: proven this session,
    live.

## Real tests executed this session (live HTTP calls against the actual running bench server, using a synthetic, clearly-marked test `device_id` so the real device's data was never touched — cleaned up afterward, confirmed)

```
Test device_id: esp32-TESTAUDIT-DEDUP (deleted from records/devices
  tables immediately after; real device's own 57,506-row history
  confirmed unaffected before and after)

1. Identical batch (device_id/boot_id/seq/totalizer unchanged) POSTed
   THREE times:
     POST #1 -> {"ack_seq": 1, ...}
     POST #2 -> {"ack_seq": 1, ...}  (identical response)
     POST #3 -> {"ack_seq": 1, ...}  (identical response)
   Server-side row count after all 3: SELECT * FROM records WHERE
     device_id='esp32-TESTAUDIT-DEDUP' -> EXACTLY 1 ROW.
   PROVEN: server-side idempotency is real, not just a consistent
   response -- the PRIMARY KEY (device_id, seq) + INSERT OR IGNORE
   genuinely prevents a duplicate row.

2. Malformed JSON body (`{not valid json!!`) -> HTTP 400
   {"error":"bad payload"} -- rejected gracefully, server did not crash,
   confirmed still serving the OTA manifest route immediately after.

3. Empty body -> HTTP 400 {"error":"bad payload"} -- same graceful
   rejection.

4. Sequence-gap test: sent seq=3 while seq=2 had never been sent ->
   ack_seq stayed at 1 (did NOT jump to 3) -- proves the contiguous-ack
   computation correctly halts at the first genuine gap, exactly
   matching the existing native unit test's own claim
   (test_p0_1_ack_gap_remediation.py's
   test_genuine_gap_in_receipt_correctly_halts_ack_not_treated_as_resolved),
   now ALSO independently confirmed via a real live HTTP call against
   the real running server, not just the unit-test's own in-process
   Flask test client.

5. Gap-fill: subsequently sent seq=2 -> ack_seq correctly advanced to 3
   in the very next response. Proves the server re-evaluates the FULL
   contiguous range on every push, not a one-shot decision.
```

## Precise answers

- **Is delivery exactly once?** **No claim of true distributed-systems
  "exactly-once" is justified**, and this report does not make one.
  What IS proven: **at-least-once transport (the device retries
  unacknowledged rows) + exactly-once server-side storage** (the
  `PRIMARY KEY` + `INSERT OR IGNORE` genuinely deduplicates, proven
  live this session). The correct, precise characterization is
  **"at-least-once delivery with idempotent server-side commit,"** which
  in aggregate produces exactly-once *storage outcomes* even though the
  *transport* itself can and does retry.
- **Can duplicate HTTP uploads occur?** Yes, routinely (retry-on-no-ack
  is the design) — this is expected and harmless given #4 below.
- **Can duplicate database records occur?** **No** — proven live this
  session (3 identical POSTs → 1 row).
- **Which unique key prevents duplication?** `PRIMARY KEY (device_id,
  seq)` on the `records` table.
- **Can two devices collide?** Only if two devices ever legitimately
  shared the same `device_id` string — the key is `(device_id, seq)`,
  so a genuine `device_id` collision (see Part 8/12: `device_id` is
  MAC-derived, effectively unique per chip) would be the only path, and
  is a hardware-identity question, not a database-logic one.
- **Can reused device IDs collide?** Same answer — the DB itself cannot
  distinguish two devices sharing one `device_id`; this is an identity-
  provisioning concern (see 07/12), not solved by the schema.
- **Can a sequence reset create duplicates?** If `seq` genuinely reset
  to a value already used by a PRIOR boot, and that old row hadn't been
  purged server-side, `INSERT OR IGNORE` would silently treat the NEW
  (different, but same device_id+seq) record as a duplicate of the OLD
  one and drop it — **a real, disclosed risk if `seq`'s monotonicity is
  ever violated** (e.g. NVS corruption resetting the persisted
  `boot_id`/totalizer-derived seq baseline). Sequence monotonicity
  itself is NVS/flash-backed (`totalizer.lastSeq()`), so its own
  durability is subject to the same flash-reliability analysis as
  everything else in doc 03.

## Part 6 — Queue state model

**This implementation does NOT have discrete Pending/Syncing/Synced/
Failed per-row states.** It is, precisely: **durable append-only rows +
a single cumulative ack cursor + implicit-pending-until-acked + retry
without per-row status**. Forcing the four-state checklist terminology
onto this design would misrepresent it — reported as what it actually
is, per the mandate's own explicit instruction not to do that.

1. **Is the queue durable?** Yes (LittleFS-backed, CRC'd).
2. **Is every row CRC-protected?** Yes (`QRow.crc32`, code-confirmed).
3. **Are segments CRC-protected as a whole?** No — CRC is per-ROW, not
   per-segment-file; a segment is just a sequence of independently
   CRC'd rows.
4. **Corrupt rows**: skipped (`continue`) during `pendingImpl_()`'s scan
   — a bad CRC or bad magic is silently passed over, never surfaced as
   an alarm distinct from an ordinary torn write. **A corrupt row is
   currently indistinguishable from a normal torn-write tail** — this is
   a real, minor observability gap (not a data-safety one — either way,
   nothing bad is accepted).
5. **Can a corrupt row block later rows?** No — the scan `continue`s
   past it and keeps reading forward; a single bad row does not halt
   the read.
6. **Can rows disappear during compaction?** The segment-based path
   (`tot_ != nullptr`, the only live path per current firmware wiring)
   has no "compaction" step at all — deletion only happens via
   `ackThrough()`'s already-analyzed cursor-then-delete ordering (doc
   03/04). The LEGACY single-file path's `maybeCompact_()` (only reached
   if `tot_` is null, i.e. dead code in the current firmware) removes
   the whole log file once fully drained — not reachable in production
   as currently wired, confirmed by code read.
7. **Segment deletion failure**: `LittleFS.remove()`'s return value is
   **not checked** in `ackThrough()` (queue.h line 553) — if deletion
   silently fails, the cursor has already advanced (durably persisted
   first), so the segment is logically abandoned/orphaned; the NEXT
   `ackThrough()` cycle simply tries to remove it again (idempotent,
   harmless), or `cleanupOrphanSegments_()` at the next boot would NOT
   catch it (that function only removes segments numbered ABOVE the
   active one, not below/already-acked ones) — **a leaked, undeleted,
   fully-acked segment could accumulate flash usage over time if
   `LittleFS.remove()` genuinely and repeatedly fails**, a real,
   previously-undocumented finding from this session's code re-read,
   classified in 18_GAPS_AND_REMEDIATION_PLAN.md.
8. **Storage full**: `append()`'s write-failure path
   (`recordWriteFailure_`) durably records the failure (dual-slot
   CRC'd `FailureState`, survives reboot, monotonic counter) and
   surfaces it via `/api/v1/status`'s `failed_write_count` /
   `/api/v1/health`'s alarm builder — **the totalizer itself is
   untouched and keeps counting** (hardware-independent of queue
   writes), so a full-storage event loses queued *records*, never the
   underlying pulse count.
9. **Eviction policy**: none — `append()` simply starts failing (and
   recording that fact) once storage is genuinely exhausted; there is
   no oldest-record eviction.
10. **Backpressure**: none in the sense of slowing telemetry generation
    — sampling continues at its fixed 1Hz rate regardless of backlog
    size; `QUEUE_HIGHWATER` only flags `quality_code`, never throttles.
11. **Alarm before full capacity**: yes — `capacityAlarmLevel()`
    (queue.h) defines 80/90/95/100% thresholds, surfaced via
    `/api/v1/health`'s alarm builder (code-confirmed; not independently
    triggered at real 80%+ usage this session, since doing so would
    require actually filling the queue close to capacity, not
    attempted).
12. **Can the queue become permanently stuck?** Not by design — retries
    are stateless (every `pending()` call just re-reads from the
    cursor); no discovered code path leaves it permanently locked. The
    session's own interrupted-download test (a DIFFERENT queue — the
    OTA download buffer, not the telemetry queue) proved the OTA path
    specifically recovers without a permanent lock (doc 08).
13. **Can a record disappear without a valid server ack?** Only via the
    doc-03-§9 checkpoint-race window (one row, narrow, disclosed) or a
    genuine, unproven flash failure beyond CRC/truncate-before-append's
    coverage.

## Corruption/edge-case execution status

Per the mandate's own execution boundary, injecting real corruption into
this session's actual data-bearing device's queue was judged too risky
to perform blindly (no isolated disposable test unit available). The
following were therefore **code-inspected only, not executed this
session**: corrupt test row, corrupt segment tail, missing segment,
unreadable segment, full-filesystem simulation, ack-cursor-ahead/behind
edge cases, interrupted prune. This is disclosed as a genuine execution
gap, not silently upgraded to "proven."

## Part 7 — Acknowledgement mechanism

1. **HTTP status required**: `code != 200` is rejected (`sync.h`,
   `if (code != 200) { ...; return false; }`) — only exactly 200 is
   accepted as a candidate ack.
2. **Is HTTP 200 alone sufficient?** **No** — a bare 200 with no
   parseable `ack_seq` is explicitly not an ack (Invariant 6, already
   cited).
3. **Mandatory JSON fields**: `ack_seq` (required for any pruning to
   occur); `server_time_ms` (optional — its absence/malformance simply
   means `haveServerTime()` stays false, doesn't block the ack itself,
   post-fix doc 35/36).
4. **Is `ack_seq` authenticated by the same TLS session?** Not
   applicable in the current bench configuration (`http://`, no TLS at
   all) — see 10_SECURITY_AUDIT.md. In a genuine `https://` production
   deployment, the ack rides the same authenticated TLS connection as
   the request (no separate signing of the ack itself).
5. **Ack range validated?** Implicitly — the ack is entirely
   server-computed (never client-supplied), so "validating" an
   attacker-controlled ack range is not applicable; the real question is
   whether a malicious/broken SERVER response could push the device to
   accept a bad ack — see §14 below.
6. **Device ID checked?** The ack applies to whatever `device_id` field
   was in the SAME request body the device sent — no cross-device ack
   confusion is structurally possible from the wire format.
7. **Plant ID checked?** Not applicable — no plant ID field exists
   (§4 above).
8. **Batch identity checked?** No explicit batch/nonce ID — the ack is
   a plain cumulative sequence number, not tied to a specific batch
   token.
9. **Malformed bodies rejected?** Yes — `extractLong_()`'s `-1` sentinel
   for "absent/malformed" makes `ackSeq < 0` reject the response as a
   non-ack (`"200 but no ack_seq -- keeping queue"`).
10. **Empty bodies rejected?** Same mechanism — an empty body parses to
    no `ack_seq` found, correctly treated as no-ack.
11/12. **Stale/future acks**: `ackThrough(ackedSeq, ...)`'s own guard
    `if (ackedSeq <= ack_.acked_seq) return;` makes a stale
    (lower-or-equal) ack a safe no-op. A **future** ack (higher than any
    seq the device has ever actually sent) is **not independently
    bounds-checked against the device's own highest sent seq** — the
    device would advance its cursor based on whatever the server claims,
    walking forward through however many rows that implies. This is a
    real, disclosed gap: **the device trusts the server's ack range**,
    which is a reasonable trust boundary for THIS system's threat model
    (the server is the device's own configured, API-key-authenticated
    upstream, not an untrusted third party) but is worth stating
    explicitly rather than silently assuming it's bounds-checked.
13. **Local deletion vs. cursor persistence ordering**: cursor persisted
    FIRST, file deletion SECOND (doc 03/04, explicitly documented in the
    source itself).
14. **Power fails between cursor persistence and deletion**: the
    segment is orphaned (still present on disk, but the cursor has
    already moved past it) — harmless, reconciled by the next
    successful `ackThrough()` re-attempting the same (now redundant,
    idempotent) deletion.
15. **Deletion succeeds but cursor persistence fails**: **structurally
    impossible in the current code** — `persistAck_()` is called
    strictly BEFORE the deletion loop (line 550 before line 552-555);
    there is no code path that deletes before persisting.

**No valid local record was deleted merely because of an HTTP 200**
this session's live testing (§ above, malformed/empty-body tests both
correctly left the queue/ack state untouched — verified by the gap test
sequence's own predictable, correct ack progression).
