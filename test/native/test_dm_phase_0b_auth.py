"""
Native-host tests for DM-Phase 0B (P0 Backend Authentication).

See Docs/Covio_Device_Manager_Live_Readiness_Plan.md, §8 DM-Phase 0B and §6,
for the acceptance criteria and test cases these exercise. "Native-host"
means: runs on a plain development machine, no ESP32 hardware, no bench rig
-- these tests exercise server.py's push()/config()/ota_manifest() routes
directly via Flask's test client, against a throwaway sqlite database (never
the real server/covio.db), matching the existing convention established by
test_adr001_schema.py.

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_dm_phase_0b_auth.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402


VALID_KEY = server.BOOTSTRAP_DEFAULT_API_KEY  # "dev-key-change-me" -- every
                                               # already-running bench device
                                               # holds this key today.
WRONG_KEY = "not-a-real-key"


class DmPhase0bAuthTests(unittest.TestCase):
    """Covers the 5 required DM-Phase 0B test cases from §8 of the plan."""

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

    # ---- helpers -----------------------------------------------------------
    def _push(self, key=None):
        headers = {"X-Api-Key": key} if key is not None else {}
        body = {"device_id": "test-device", "kfactor_version": 1, "records": [
            {"schema_version": 1, "record_type": 1,
             "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500},
        ]}
        return self.client.post(
            "/api/iot/flow/push",
            data=json.dumps(body),
            content_type="application/json",
            headers=headers,
        )

    def _config(self, key=None):
        headers = {"X-Api-Key": key} if key is not None else {}
        return self.client.get("/api/iot/flow/config", headers=headers)

    def _ota_manifest(self, key=None):
        headers = {"X-Api-Key": key} if key is not None else {}
        return self.client.get("/api/iot/flow/ota/manifest", headers=headers)

    def _accepted_count(self):
        c = server.db()
        n = c.execute("SELECT COUNT(*) AS n FROM records").fetchone()["n"]
        c.close()
        return n

    def _revoke(self, key):
        c = server.db()
        c.execute("UPDATE devices SET revoked=1 WHERE api_key_hash=?",
                   (server.hash_api_key(key),))
        c.commit(); c.close()

    # 1. valid key -> 200, unchanged behavior, for all three routes ----------
    def test_valid_key_unchanged_behavior(self):
        resp = self._push(VALID_KEY)
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(self._accepted_count(), 1)
        self.assertEqual(resp.get_json()["ack_seq"], 1)

        resp = self._config(VALID_KEY)
        self.assertEqual(resp.status_code, 200)
        self.assertIn("K_factor", resp.get_json())

        resp = self._ota_manifest(VALID_KEY)
        self.assertEqual(resp.status_code, 200)

    # 2. missing X-Api-Key header -> 401, no side effect ----------------------
    def test_missing_key_rejected(self):
        resp = self._push(None)
        self.assertEqual(resp.status_code, 401)
        self.assertEqual(resp.get_json()["error"]["code"], "MISSING_API_KEY")
        self.assertEqual(self._accepted_count(), 0)

        self.assertEqual(self._config(None).status_code, 401)
        self.assertEqual(self._ota_manifest(None).status_code, 401)

    # 3. wrong key -> 401, no side effect -------------------------------------
    def test_wrong_key_rejected(self):
        resp = self._push(WRONG_KEY)
        self.assertEqual(resp.status_code, 401)
        self.assertEqual(resp.get_json()["error"]["code"], "INVALID_API_KEY")
        self.assertEqual(self._accepted_count(), 0)

        self.assertEqual(self._config(WRONG_KEY).status_code, 401)
        self.assertEqual(self._ota_manifest(WRONG_KEY).status_code, 401)

    # 4. revoked key -> 401, no side effect -----------------------------------
    def test_revoked_key_rejected(self):
        self._revoke(VALID_KEY)
        resp = self._push(VALID_KEY)
        self.assertEqual(resp.status_code, 401)
        self.assertEqual(resp.get_json()["error"]["code"], "INVALID_API_KEY")
        self.assertEqual(self._accepted_count(), 0)

    # 5. zero regression for existing bench devices already seeded -----------
    # Every device in the field today holds config.h's DEFAULT_API_KEY with
    # no per-device key ever issued. This confirms init_db()'s bootstrap seed
    # (§8 DM-Phase 0B step 2) actually keeps that shared key valid.
    def test_bootstrap_seed_keeps_existing_bench_devices_working(self):
        c = server.db()
        row = c.execute(
            "SELECT revoked FROM devices WHERE api_key_hash=?",
            (server.hash_api_key(server.BOOTSTRAP_DEFAULT_API_KEY),)).fetchone()
        c.close()
        self.assertIsNotNone(row, "bootstrap default key was not seeded")
        self.assertEqual(row["revoked"], 0)
        self.assertEqual(self._push(VALID_KEY).status_code, 200)

    # ---- side-effect precision: a rejected push must not even reach the ----
    # ---- quarantine table, not just the accepted-records table -------------
    def test_rejected_push_writes_nothing_at_all_not_even_quarantine(self):
        self._push(WRONG_KEY)
        c = server.db()
        n_records = c.execute("SELECT COUNT(*) AS n FROM records").fetchone()["n"]
        n_quarantine = c.execute(
            "SELECT COUNT(*) AS n FROM quarantined_records").fetchone()["n"]
        c.close()
        self.assertEqual(n_records, 0)
        self.assertEqual(n_quarantine, 0)


if __name__ == "__main__":
    unittest.main()
