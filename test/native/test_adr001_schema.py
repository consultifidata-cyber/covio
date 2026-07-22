"""
Native-host tests for ADR-001 (Self-Describing, Versioned Telemetry Schema).

"Native-host" means: runs on a plain development machine, no ESP32 hardware,
no bench rig. Per ADR-013's phase-split note, the schema-version dispatch
logic ADR-001 introduces is exactly the kind of hardware-independent logic
this harness exists to cover. The dispatch/acceptance/quarantine rule lives
in the receiver (server/server.py) — the firmware side only ever *writes* a
single schema_version, it never decides acceptance — so these tests exercise
server.py's push() endpoint directly via Flask's test client, against a
throwaway sqlite database (never the real server/covio.db).

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_adr001_schema.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402


def make_batch(records, device_id="test-device"):
    return {"device_id": device_id, "kfactor_version": 1, "records": records}


class Adr001SchemaAcceptanceTests(unittest.TestCase):
    """Covers the six required Phase 1 / ADR-001 native-host test cases."""

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
        # DM-Phase 0B: push now requires X-Api-Key. Using the bootstrap
        # default key here (not a hardcoded second copy of the string)
        # keeps this test exercising ADR-001's schema-acceptance logic
        # specifically -- it is not itself a test of authentication.
        return self.client.post(
            "/api/iot/flow/push",
            data=json.dumps(make_batch(records, device_id)),
            content_type="application/json",
            headers={"X-Api-Key": server.BOOTSTRAP_DEFAULT_API_KEY},
        )

    def _quarantine_count(self):
        c = server.db()
        n = c.execute("SELECT COUNT(*) AS n FROM quarantined_records").fetchone()["n"]
        c.close()
        return n

    def _accepted_count(self):
        c = server.db()
        n = c.execute("SELECT COUNT(*) AS n FROM records").fetchone()["n"]
        c.close()
        return n

    # 1. current schema accepted -------------------------------------------
    def test_current_schema_accepted(self):
        resp = self._push([{
            "schema_version": server.CURRENT_SCHEMA_VERSION, "record_type": 1,
            "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500,
        }])
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(self._accepted_count(), 1)
        self.assertEqual(self._quarantine_count(), 0)
        self.assertEqual(resp.get_json()["ack_seq"], 1)

    # 2. previous schema accepted --------------------------------------------
    # As of Phase 1, CURRENT_SCHEMA_VERSION == 1 and there is no registered
    # "previous" version yet (ACR-001 — the pre-ADR-001 unversioned layout is
    # retired via forced-drain, not accepted as "previous"). To genuinely
    # exercise the current+previous acceptance *mechanism* ADR-001 defines
    # for steady state (i.e. once a real schema_version 2 exists), this test
    # simulates that future rollout state by patching the module's version
    # globals for its duration, then restoring them.
    def test_previous_schema_accepted_once_registered(self):
        orig = (server.CURRENT_SCHEMA_VERSION, server.PREVIOUS_SCHEMA_VERSION,
                server.ACCEPTED_SCHEMA_VERSIONS)
        try:
            server.CURRENT_SCHEMA_VERSION = 2
            server.PREVIOUS_SCHEMA_VERSION = 1
            server.ACCEPTED_SCHEMA_VERSIONS = {1, 2}
            resp = self._push([{
                "schema_version": 1, "record_type": 1,
                "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500,
            }])
        finally:
            (server.CURRENT_SCHEMA_VERSION, server.PREVIOUS_SCHEMA_VERSION,
             server.ACCEPTED_SCHEMA_VERSIONS) = orig
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(self._accepted_count(), 1)
        self.assertEqual(self._quarantine_count(), 0)

    # 3. unknown schema rejected ---------------------------------------------
    def test_unknown_schema_rejected(self):
        resp = self._push([{
            "schema_version": 99, "record_type": 1,
            "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500,
        }])
        self.assertEqual(resp.status_code, 200)  # request itself still succeeds
        self.assertEqual(self._accepted_count(), 0)
        self.assertEqual(self._quarantine_count(), 1)

    # 4. mixed-version batch accepted ----------------------------------------
    def test_mixed_version_batch_accepted(self):
        orig = (server.CURRENT_SCHEMA_VERSION, server.PREVIOUS_SCHEMA_VERSION,
                server.ACCEPTED_SCHEMA_VERSIONS)
        try:
            server.CURRENT_SCHEMA_VERSION = 2
            server.PREVIOUS_SCHEMA_VERSION = 1
            server.ACCEPTED_SCHEMA_VERSIONS = {1, 2}
            resp = self._push([
                {"schema_version": 2, "record_type": 1,
                 "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500},
                {"schema_version": 1, "record_type": 1,
                 "boot_id": 1, "seq": 2, "ts": 101, "totalizer": 501},
            ])
        finally:
            (server.CURRENT_SCHEMA_VERSION, server.PREVIOUS_SCHEMA_VERSION,
             server.ACCEPTED_SCHEMA_VERSIONS) = orig
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(self._accepted_count(), 2)
        self.assertEqual(self._quarantine_count(), 0)
        self.assertEqual(resp.get_json()["ack_seq"], 2)

    # 5. unsupported version quarantined --------------------------------------
    def test_unsupported_version_quarantined_alongside_accepted(self):
        resp = self._push([
            {"schema_version": 1, "record_type": 1,
             "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500},
            {"schema_version": 7, "record_type": 1,
             "boot_id": 1, "seq": 2, "ts": 101, "totalizer": 501},
        ])
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(self._accepted_count(), 1)
        self.assertEqual(self._quarantine_count(), 1)
        c = server.db()
        row = c.execute("SELECT reason FROM quarantined_records").fetchone()
        c.close()
        self.assertIn("unsupported_schema_version", row["reason"])
        # accepted-but-not-contiguous quarantine must not stall the ack of
        # the record that IS valid.
        self.assertEqual(resp.get_json()["ack_seq"], 1)

    # 6. parser never crashes on unsupported/malformed schema -----------------
    def test_no_crash_on_malformed_or_missing_schema(self):
        # distinct `seq` per case: quarantined_records' PK is
        # (device_id, seq, schema_version), and several of these cases share
        # schema_version=None (or no key at all, which defaults to None), so
        # reusing the same seq would collide and silently under-count.
        malformed_batches = [
            [{"schema_version": None, "record_type": 1,
              "boot_id": 1, "seq": 1, "ts": 1, "totalizer": 1}],
            [{"record_type": 1, "boot_id": 1, "seq": 2, "ts": 1, "totalizer": 1}],  # no schema_version key
            [{"schema_version": 1, "record_type": 999,
              "boot_id": 1, "seq": 3, "ts": 1, "totalizer": 1}],  # unknown record_type
            [{"schema_version": 1, "record_type": 1, "boot_id": 1, "seq": 4}],  # missing required fields
            [{"schema_version": "not-a-number", "record_type": 1,
              "boot_id": 1, "seq": 5, "ts": 1, "totalizer": 1}],
        ]
        for records in malformed_batches:
            with self.subTest(records=records):
                resp = self._push(records)
                self.assertEqual(resp.status_code, 200)
                self.assertEqual(self._accepted_count(), 0)
        self.assertEqual(self._quarantine_count(), len(malformed_batches))


if __name__ == "__main__":
    unittest.main()
