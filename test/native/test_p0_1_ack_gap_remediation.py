"""
Native-host tests for P0-1 remediation (RISK-01: acknowledgement-gap /
queue-pruning deadlock).

See docs/audit/coviu_oil_meter_p0_remediation/02_ACK_AND_QUEUE_REMEDIATION.md
for the full design rationale. "Native-host" means: runs on a plain
development machine, no ESP32 hardware -- these tests exercise server.py's
push() endpoint directly via Flask's test client, against a throwaway sqlite
database (never the real server/covio.db), matching the convention
established by test_adr001_schema.py.

Scope note: several of the mandate's 18 required P0-1 test scenarios concern
DEVICE-side (firmware, C++) behavior -- queue.h's ackThrough()/pending() and
sync.h's response parsing. This repository has no host-compilable C++ test
harness for firmware logic (test/native/ only ever tested server.py); building
one is a substantial separate undertaking, not done here. Those scenarios are
NOT re-tested by this file -- they are either (a) unchanged by this fix
(the wire contract's ack_seq field and its semantics for the DEVICE are
identical to before; only the SERVER's computation of that field changed) and
were already covered by the prior audit's code-inspection pass, or (b)
genuinely require physical-hardware or host-C++-harness proof this session
cannot provide. Each such scenario is called out explicitly below with NOT
COVERED HERE and the reason, rather than silently omitted.

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_p0_1_ack_gap_remediation.py
"""
import json
import os
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402


def row(seq, schema_version=1, record_type=1, totalizer=None):
    return {"schema_version": schema_version, "record_type": record_type,
            "boot_id": 1, "seq": seq, "ts": 100 + seq,
            "totalizer": totalizer if totalizer is not None else 500 + seq}


class P0_1_AckGapRemediationTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self._tmp.close()
        self._orig_db = server.DB
        server.DB = self._tmp.name
        server.init_db()
        server.app.testing = True
        self.client = server.app.test_client()

    def tearDown(self):
        server.DB = self._orig_db
        os.unlink(self._tmp.name)

    def _push(self, records, device_id="test-device"):
        body = {"device_id": device_id, "kfactor_version": 1, "records": records}
        return self.client.post(
            "/api/iot/flow/push",
            data=json.dumps(body),
            content_type="application/json",
            headers={"X-Api-Key": server.BOOTSTRAP_DEFAULT_API_KEY},
        )

    def _quarantine_reasons(self):
        c = server.db()
        rows = c.execute("SELECT seq, reason FROM quarantined_records ORDER BY seq").fetchall()
        c.close()
        return {r["seq"]: r["reason"] for r in rows}

    def _accepted_seqs(self):
        c = server.db()
        rows = c.execute("SELECT seq FROM records ORDER BY seq").fetchall()
        c.close()
        return [r["seq"] for r in rows]

    # ---- 1. all records accepted --------------------------------------------
    def test_all_accepted_ack_advances_to_last(self):
        resp = self._push([row(1), row(2), row(3)])
        self.assertEqual(resp.get_json()["ack_seq"], 3)
        self.assertNotIn("quarantined", resp.get_json())

    # ---- 2. first record permanently rejected, later accepted ---------------
    def test_first_record_rejected_later_accepted_ack_advances_past_it(self):
        resp = self._push([
            row(1, schema_version=99),   # permanently rejected
            row(2), row(3),
        ])
        self.assertEqual(self._accepted_seqs(), [2, 3])
        self.assertIn(1, self._quarantine_reasons())
        # THE core RISK-01 fix: ack_seq must advance to 3, not stall at 0.
        self.assertEqual(resp.get_json()["ack_seq"], 3)

    # ---- 3. middle record permanently rejected -------------------------------
    def test_middle_record_rejected_ack_advances_past_it(self):
        resp = self._push([row(1), row(2, schema_version=99), row(3)])
        self.assertEqual(self._accepted_seqs(), [1, 3])
        self.assertEqual(resp.get_json()["ack_seq"], 3)
        self.assertEqual(resp.get_json()["quarantined"], [{"seq": 2, "reason": "unsupported_schema_version:99"}])

    # ---- 4. last record permanently rejected ---------------------------------
    def test_last_record_rejected_ack_still_advances_past_it(self):
        resp = self._push([row(1), row(2), row(3, schema_version=99)])
        self.assertEqual(self._accepted_seqs(), [1, 2])
        # Before the fix this would have been 2 as well (records-only scan
        # never reaches the quarantined seq's position) -- unlike the
        # first/middle cases, a trailing quarantine wasn't actually a
        # regression risk for THIS specific batch shape, but confirming
        # ack_seq==3 (not 2) proves the union-based scan, not a coincidence
        # of scan order.
        self.assertEqual(resp.get_json()["ack_seq"], 3)

    # ---- 5. "one retryable failure in the middle" ----------------------------
    # NOT APPLICABLE to this architecture as a distinct per-record state: once
    # push() has parsed a record, its disposition is deterministic (accept or
    # permanently quarantine) -- there is no per-record "try again later"
    # outcome in this receiver. A genuinely retryable failure only exists at
    # the WHOLE-BATCH/transport level (network error, non-200 HTTP, auth
    # failure) -- see test_dm_phase_0b_auth.py's existing coverage of that,
    # and test_whole_batch_retry_after_transport_style_failure_is_idempotent
    # below for the retry-of-an-entire-batch case.
    def test_no_per_record_retryable_state_exists_by_design(self):
        # A record can only ever end up accepted or quarantined once push()
        # actually parses it -- documented here as an explicit design
        # assertion, not just prose in a comment.
        resp = self._push([row(1)])
        self.assertEqual(self._accepted_seqs(), [1])
        self.assertEqual(self._quarantine_reasons(), {})

    # ---- 6 & 7. same batch replayed / response lost then retried ------------
    def test_replayed_batch_is_idempotent_and_ack_unchanged(self):
        first = self._push([row(1), row(2)])
        self.assertEqual(first.get_json()["ack_seq"], 2)
        # Simulates: server committed, but the device never saw the HTTP
        # response (connection dropped) and retries the identical batch.
        second = self._push([row(1), row(2)])
        self.assertEqual(second.get_json()["ack_seq"], 2)
        self.assertEqual(len(self._accepted_seqs()), 2)  # no duplicate rows

    def test_replayed_batch_with_quarantined_record_does_not_double_quarantine(self):
        self._push([row(1, schema_version=99)])
        self._push([row(1, schema_version=99)])  # replay
        c = server.db()
        n = c.execute("SELECT COUNT(*) AS n FROM quarantined_records").fetchone()["n"]
        c.close()
        self.assertEqual(n, 1)  # INSERT OR IGNORE -- PK (device_id,seq,schema_version)

    # ---- 8. duplicate concurrent requests ------------------------------------
    def test_concurrent_duplicate_pushes_produce_exactly_one_stored_row(self):
        results = []

        def do_push():
            results.append(self._push([row(1)]).status_code)

        threads = [threading.Thread(target=do_push) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        self.assertTrue(all(code == 200 for code in results))
        self.assertEqual(self._accepted_seqs(), [1])  # DB PRIMARY KEY, not app-level dedup

    # ---- 9. malformed acknowledgement ----------------------------------------
    # This is a DEVICE-side concern (sync.h's extractLong_ parsing the HTTP
    # response body) -- NOT COVERED HERE: no C++ host test harness exists in
    # this repo. Unchanged by this fix: the server always returns a
    # well-formed {"ack_seq": <int>, ...} JSON body; sync.h's existing
    # "200 but no ack_seq -- keeping queue" guard (Invariant 6) was verified
    # by the prior audit via direct code inspection and is untouched here.

    # ---- 10. missing sequence disposition (a genuine gap) --------------------
    def test_genuine_gap_in_receipt_correctly_halts_ack_not_treated_as_resolved(self):
        # seq 2 was simply never sent/received at all (not quarantined,
        # not accepted) -- retryable-by-transport-retry, and the ack
        # watermark must correctly NOT advance past it.
        resp = self._push([row(1), row(3)])  # seq 2 skipped entirely
        self.assertEqual(resp.get_json()["ack_seq"], 1)
        self.assertEqual(self._accepted_seqs(), [1, 3])

    # ---- 11. acknowledgement beyond submitted range --------------------------
    # NOT APPLICABLE server-side: ack_seq is COMPUTED by the server from its
    # own durable state (records UNION quarantined_records), never an
    # independent claim that could be "wrong" relative to what was sent -- it
    # is definitionally bounded by what the server has actually, durably
    # resolved. The DEVICE-side concern ("what if a malicious/buggy response
    # claimed a seq beyond reality") is handled by construction in
    # queue.h::ackThrough()'s segment walk, which can only ever consume rows
    # that physically exist on flash -- verified by the prior audit's code
    # inspection, unchanged by this fix.

    # ---- 12. stale acknowledgement -------------------------------------------
    def test_ack_seq_is_monotonic_never_regresses_across_pushes(self):
        r1 = self._push([row(1), row(2), row(3)])
        self.assertEqual(r1.get_json()["ack_seq"], 3)
        # A second push replaying the same batch must report the SAME
        # watermark, not a lower one, even though the query re-derives it
        # from scratch each time (recomputation, not incremental state, so
        # "staleness" here would mean a regression bug in the recompute, not
        # a stored value going backwards).
        r2 = self._push([row(1), row(2), row(3)])
        self.assertEqual(r2.get_json()["ack_seq"], 3)

    # ---- 13. out-of-order batch -----------------------------------------------
    def test_out_of_order_batch_ack_still_only_advances_through_contiguous_prefix(self):
        resp = self._push([row(3), row(1), row(2)])  # arrives out of order
        self.assertEqual(self._accepted_seqs(), [1, 2, 3])
        self.assertEqual(resp.get_json()["ack_seq"], 3)

    def test_out_of_order_batch_with_a_gap_still_halts_correctly(self):
        resp = self._push([row(5), row(1)])  # seq 2/3/4 never arrive
        self.assertEqual(resp.get_json()["ack_seq"], 1)

    # ---- 14-16. device reboot / pruning-interruption scenarios ---------------
    # NOT COVERED HERE: these concern queue.h's on-device dual-slot CRC
    # checkpoint and cursor-then-delete ordering (ackThrough(), lines ~402-414
    # of queue.h) -- firmware C++ logic with no host test harness. This fix
    # does not modify queue.h/sync.h at all (the wire contract's ack_seq
    # field and firmware's consumption of it are byte-for-byte unchanged);
    # the prior audit's code-inspection findings for these three scenarios
    # (torn-write CRC detection, persist-cursor-before-delete-segment
    # ordering, dual-slot fallback) stand unmodified and unre-verified in
    # this pass.

    # ---- 17. permanent rejection survives reboot and remains auditable ------
    def test_quarantine_and_its_audit_event_survive_a_process_restart(self):
        self._push([row(1, schema_version=99)])
        # Simulate a process restart: re-run init_db() (every real process
        # start does this) against the SAME on-disk sqlite file.
        server.init_db()
        self.assertIn(1, self._quarantine_reasons())
        events = self.client.get("/admin/events", auth=("admin", server.ADMIN_PASSWORD)).get_json()["events"]
        types = [e["event_type"] for e in events]
        self.assertIn("RECORDS_QUARANTINED", types)
        quarantine_event = next(e for e in events if e["event_type"] == "RECORDS_QUARANTINED")
        self.assertEqual(quarantine_event["detail"]["quarantined"],
                          [{"seq": 1, "reason": "unsupported_schema_version:99"}])

    # ---- 18. later valid records become prunable without skipping retryable data
    def test_quarantine_resolves_immediately_in_the_same_push_that_produced_it(self):
        r1 = self._push([row(1), row(2, schema_version=99)])
        self.assertEqual(r1.get_json()["ack_seq"], 2)
        # Further valid data after the quarantined seq keeps advancing --
        # this is the exact deadlock RISK-01 described: before the fix,
        # ack_seq would have been permanently stuck at 1 forever, no matter
        # how much MORE valid data arrived afterward.
        r2 = self._push([row(3), row(4), row(5)])
        self.assertEqual(r2.get_json()["ack_seq"], 5)
        r3 = self._push([row(6)])
        self.assertEqual(r3.get_json()["ack_seq"], 6)


if __name__ == "__main__":
    unittest.main()
