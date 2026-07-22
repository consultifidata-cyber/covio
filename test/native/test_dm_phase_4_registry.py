"""
Native-host tests for DM-Phase 4 (Device Registry + Device Twin).

See Docs/Covio_Device_Manager_Live_Readiness_Plan.md, §8 DM-Phase 4 and §6/
§11.3/§11.5, for the acceptance criteria and test cases these exercise.
"Native-host" means: runs on a plain development machine, no ESP32 hardware,
no bench rig -- these tests exercise server.py's registry/twin/event routes
directly via Flask's test client, against a throwaway sqlite database (never
the real server/covio.db), matching the existing convention established by
test_adr001_schema.py and test_dm_phase_0b_auth.py.

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_dm_phase_4_registry.py
"""
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402


VALID_KEY = server.BOOTSTRAP_DEFAULT_API_KEY  # "dev-key-change-me"


class DmPhase4RegistryTests(unittest.TestCase):
    """Covers the DM-Phase 4 §8 test cases plus the migration/backward-
    compatibility guarantees requirement #7 of this phase's brief demands."""

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
    def _push(self, key=None, device_id="test-device", fw=None, kfactor_version=1,
              records=None):
        headers = {"X-Api-Key": key} if key is not None else {}
        body = {"device_id": device_id, "kfactor_version": kfactor_version,
                "records": records if records is not None else [
                    {"schema_version": 1, "record_type": 1,
                     "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 500},
                ]}
        if fw is not None:
            body["fw"] = fw
        return self.client.post(
            "/api/iot/flow/push",
            data=json.dumps(body),
            content_type="application/json",
            headers=headers,
        )

    def _config(self, key=None):
        headers = {"X-Api-Key": key} if key is not None else {}
        return self.client.get("/api/iot/flow/config", headers=headers)

    # P0-3 remediation (RISK-03): every /admin/* route now requires HTTP
    # Basic Auth (server.ADMIN_PASSWORD is the dev-mode-generated password
    # for THIS test process's own module import -- see
    # server._resolve_admin_credentials()/require_admin() -- never a
    # hardcoded credential, matching RISK-02's own lesson).
    ADMIN_AUTH = None  # set lazily so it reads server.ADMIN_PASSWORD after import

    def _admin_auth(self):
        return ("admin", server.ADMIN_PASSWORD)

    def _provision(self, device_id, asset_label=None):
        body = {"device_id": device_id}
        if asset_label is not None:
            body["asset_label"] = asset_label
        return self.client.post(
            "/admin/devices/provision",
            data=json.dumps(body),
            content_type="application/json",
            auth=self._admin_auth(),
        )

    def _revoke(self, device_id):
        return self.client.post(f"/admin/devices/{device_id}/revoke-key", auth=self._admin_auth())

    def _rotate(self, device_id):
        return self.client.post(f"/admin/devices/{device_id}/rotate-key", auth=self._admin_auth())

    def _set_kfactor(self, k, density=0.84, t_ref=15.0):
        return self.client.post("/admin/kfactor", data={
            "k_factor": str(k), "density": str(density), "t_ref": str(t_ref),
        }, auth=self._admin_auth())

    def _device_row(self, device_id):
        c = server.db()
        row = c.execute("SELECT * FROM devices WHERE device_id=?", (device_id,)).fetchone()
        c.close()
        return row

    def _events(self, device_id=None):
        if device_id:
            resp = self.client.get(f"/admin/devices/{device_id}/events", auth=self._admin_auth())
        else:
            resp = self.client.get("/admin/events", auth=self._admin_auth())
        return resp.get_json()["events"]

    # ---- migration + fresh install -----------------------------------------
    def test_fresh_install_devices_table_has_new_shape_and_bootstrap_row(self):
        row = self._device_row("legacy-default-key")
        self.assertIsNotNone(row)
        self.assertEqual(row["revoked"], 0)
        self.assertIn("asset_label", row.keys())
        self.assertIn("last_seen_ms", row.keys())

    def test_migration_from_dm_phase_0b_shape_preserves_existing_rows(self):
        # Simulate a pre-DM-Phase-4 database: drop the migrated table and
        # recreate the old (device_id, api_key_hash, revoked) shape by hand,
        # with a row that is NOT the bootstrap default, then re-run init_db()
        # and confirm the row survives the migration with its key intact.
        c = server.db()
        c.execute("DELETE FROM schema_migrations WHERE version=?", (server.DEVICES_SCHEMA_VERSION,))
        c.execute("DROP TABLE devices")
        c.execute("""CREATE TABLE devices (
            device_id TEXT, api_key_hash TEXT, revoked INTEGER NOT NULL DEFAULT 0
        )""")
        c.execute("INSERT INTO devices (device_id, api_key_hash, revoked) VALUES (?, ?, 0)",
                   ("pre-existing-device", server.hash_api_key("some-old-key")))
        c.commit(); c.close()

        server.init_db()  # re-run, as every process start does

        row = self._device_row("pre-existing-device")
        self.assertIsNotNone(row, "pre-existing row was lost during migration")
        self.assertEqual(row["api_key_hash"], server.hash_api_key("some-old-key"))
        self.assertEqual(row["revoked"], 0)
        # the bootstrap row must also still be present (re-seeded/untouched)
        self.assertIsNotNone(self._device_row("legacy-default-key"))

    def test_migration_survives_an_orphaned_devices_new_from_an_interrupted_attempt(self):
        # Simulates a crash between a previous migration's CREATE TABLE
        # devices_new and its final commit: an orphaned devices_new is left
        # behind, and schema_migrations was never recorded (so init_db()
        # will attempt the migration again). Audit fix regression test --
        # before the fix, the un-guarded CREATE TABLE devices_new here would
        # raise "table devices_new already exists" and permanently prevent
        # the server from starting.
        c = server.db()
        c.execute("DELETE FROM schema_migrations WHERE version=?", (server.DEVICES_SCHEMA_VERSION,))
        c.execute("DROP TABLE IF EXISTS devices_new")
        c.execute("CREATE TABLE devices_new (device_id TEXT PRIMARY KEY)")  # orphaned leftover
        c.commit(); c.close()

        server.init_db()  # must not raise

        row = self._device_row("legacy-default-key")
        self.assertIsNotNone(row)
        self.assertIn("asset_label", row.keys())

    def test_migration_is_idempotent_across_repeated_init_db_calls(self):
        server.init_db()
        server.init_db()
        c = server.db()
        n = c.execute("SELECT COUNT(*) AS n FROM schema_migrations WHERE version=?",
                      (server.DEVICES_SCHEMA_VERSION,)).fetchone()["n"]
        c.close()
        self.assertEqual(n, 1)

    # ---- zero regression to DM-Phase 0B auth --------------------------------
    def test_existing_bootstrap_key_still_works_unmodified(self):
        resp = self._push(VALID_KEY)
        self.assertEqual(resp.status_code, 200)
        resp = self._config(VALID_KEY)
        self.assertEqual(resp.status_code, 200)

    def test_missing_and_wrong_key_still_rejected_unmodified(self):
        self.assertEqual(self._push(None).status_code, 401)
        self.assertEqual(self._push("not-a-real-key").status_code, 401)

    # ---- test case 1 (§8): provisioning issues a unique, traceable key -----
    def test_provision_issues_unique_key_and_traceable_event(self):
        r1 = self._provision("device-A", asset_label="Boiler Room Meter")
        self.assertEqual(r1.status_code, 200)
        r2 = self._provision("device-B", asset_label="Warehouse Meter")
        self.assertEqual(r2.status_code, 200)

        key_a = r1.get_json()["api_key"]
        key_b = r2.get_json()["api_key"]
        self.assertNotEqual(key_a, key_b)

        row = self._device_row("device-A")
        self.assertEqual(row["api_key_hash"], server.hash_api_key(key_a))
        self.assertEqual(row["asset_label"], "Boiler Room Meter")
        self.assertIsNotNone(row["provisioned_at_ms"])

        events = self._events("device-A")
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event_type"], "DEVICE_PROVISIONED")

    def test_provision_requires_device_id(self):
        resp = self.client.post("/admin/devices/provision", data=json.dumps({}),
                                 content_type="application/json", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 400)
        self.assertEqual(resp.get_json()["error"]["code"], "DEVICE_ID_REQUIRED")

    def test_newly_provisioned_key_authenticates_device_requests(self):
        r = self._provision("device-C")
        api_key = r.get_json()["api_key"]
        resp = self._push(api_key, device_id="device-C")
        self.assertEqual(resp.status_code, 200)

    # ---- test case 4 (§8): revoke rejects the NEXT request ------------------
    def test_revoke_rejects_next_request_from_that_device(self):
        r = self._provision("device-D")
        api_key = r.get_json()["api_key"]
        self.assertEqual(self._push(api_key, device_id="device-D").status_code, 200)

        rr = self._revoke("device-D")
        self.assertEqual(rr.status_code, 200)
        self.assertTrue(rr.get_json()["success"])

        resp = self._push(api_key, device_id="device-D")
        self.assertEqual(resp.status_code, 401)
        self.assertEqual(resp.get_json()["error"]["code"], "INVALID_API_KEY")

        events = self._events("device-D")
        types = [e["event_type"] for e in events]
        self.assertIn("KEY_REVOKED", types)
        # DM-Phase 4 §11.4: the rejected request itself is also traceable.
        self.assertIn("API_AUTH_FAILED", types)

    def test_revoke_unknown_device_returns_404(self):
        resp = self._revoke("no-such-device")
        self.assertEqual(resp.status_code, 404)

    # ---- key rotation ---------------------------------------------------
    def test_rotate_replaces_key_old_key_stops_working_new_key_works(self):
        r = self._provision("device-E")
        old_key = r.get_json()["api_key"]

        rr = self._rotate("device-E")
        self.assertEqual(rr.status_code, 200)
        new_key = rr.get_json()["api_key"]
        self.assertNotEqual(old_key, new_key)

        self.assertEqual(self._push(old_key, device_id="device-E").status_code, 401)
        self.assertEqual(self._push(new_key, device_id="device-E").status_code, 200)

        events = self._events("device-E")
        self.assertIn("KEY_ROTATED", [e["event_type"] for e in events])

    def test_rotate_unknown_device_returns_404(self):
        resp = self._rotate("no-such-device")
        self.assertEqual(resp.status_code, 404)

    # ---- test case 3 (§8): Twin "current" row updates after a push ---------
    def test_twin_updates_after_successful_push(self):
        r = self._provision("device-F")
        api_key = r.get_json()["api_key"]
        resp = self._push(api_key, device_id="device-F", fw="1.2.3", kfactor_version=4,
                           records=[
                               {"schema_version": 1, "record_type": 1,
                                "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 700},
                               {"schema_version": 1, "record_type": 1,
                                "boot_id": 1, "seq": 2, "ts": 101, "totalizer": 900},
                           ])
        self.assertEqual(resp.status_code, 200)

        row = self._device_row("device-F")
        self.assertEqual(row["last_fw_version"], "1.2.3")
        self.assertEqual(row["last_kfactor_version"], 4)
        self.assertEqual(row["last_push_totalizer"], 900)
        self.assertEqual(row["derived_health_state"], "ok")
        self.assertIsNotNone(row["last_seen_ms"])

    def test_twin_created_by_first_push_from_a_never_provisioned_device(self):
        # Using the shared bootstrap key, matching DM-Phase 0B's own
        # "already-fielded devices keep working unmodified" guarantee.
        resp = self._push(VALID_KEY, device_id="brand-new-bootstrap-device")
        self.assertEqual(resp.status_code, 200)
        row = self._device_row("brand-new-bootstrap-device")
        self.assertIsNotNone(row)
        self.assertEqual(row["last_push_totalizer"], 500)

    def test_quarantined_records_do_not_poison_last_push_totalizer(self):
        r = self._provision("device-G")
        api_key = r.get_json()["api_key"]
        resp = self._push(api_key, device_id="device-G", records=[
            {"schema_version": 1, "record_type": 1,
             "boot_id": 1, "seq": 1, "ts": 100, "totalizer": 300},
            {"schema_version": 99, "record_type": 1,  # unsupported -> quarantined
             "boot_id": 1, "seq": 2, "ts": 101, "totalizer": 999999},
        ])
        self.assertEqual(resp.status_code, 200)
        row = self._device_row("device-G")
        self.assertEqual(row["last_push_totalizer"], 300)

    def test_config_poll_touches_last_seen_but_never_creates_a_row(self):
        # A bare config GET from an unknown/never-pushed key must not
        # fabricate a registry row (touch_last_seen()'s own contract).
        resp = self._config("not-a-real-key")
        self.assertEqual(resp.status_code, 401)

        r = self._provision("device-H")
        api_key = r.get_json()["api_key"]
        row_before = self._device_row("device-H")
        self.assertIsNone(row_before["last_seen_ms"])

        resp = self._config(api_key)
        self.assertEqual(resp.status_code, 200)
        row_after = self._device_row("device-H")
        self.assertIsNotNone(row_after["last_seen_ms"])

    # ---- test case 2 (§8): three consecutive K-factor edits -> 3 events ----
    def test_three_consecutive_kfactor_edits_produce_three_events(self):
        for k in (1000.0, 1010.0, 1020.5):
            resp = self._set_kfactor(k)
            self.assertEqual(resp.status_code, 303)

        events = self._events()
        cal_events = [e for e in events if e["event_type"] == "CALIBRATION_CHANGED"]
        self.assertEqual(len(cal_events), 3)
        # newest-first ordering
        self.assertEqual(cal_events[0]["detail"]["new"]["k_factor"], 1020.5)
        self.assertEqual(cal_events[2]["detail"]["new"]["k_factor"], 1000.0)
        # fleet-wide event -- not attributed to any single device
        self.assertIsNone(cal_events[0]["device_id"])

    # ---- event retrieval endpoints ------------------------------------------
    def test_fleet_events_endpoint_is_bounded_by_limit(self):
        for i in range(5):
            self._set_kfactor(1000.0 + i)
        resp = self.client.get("/admin/events?limit=2", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(len(resp.get_json()["events"]), 2)

    def test_events_endpoint_falls_back_to_default_limit_on_malformed_value(self):
        # Audit fix regression test: ?limit=not-a-number must not 500.
        resp = self.client.get("/admin/events?limit=not-a-number", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 200)

    def test_per_device_events_endpoint_filters_by_device(self):
        self._provision("device-I")
        self._provision("device-J")
        events_i = self._events("device-I")
        self.assertEqual(len(events_i), 1)
        self.assertEqual(events_i[0]["device_id"], "device-I")

    # ---- admin dashboard renders without error -------------------------
    def test_devices_dashboard_renders(self):
        self._provision("device-K", asset_label="Dashboard Smoke Test")
        resp = self.client.get("/admin/devices", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 200)
        self.assertIn(b"device-K", resp.data)
        self.assertIn(b"Dashboard Smoke Test", resp.data)

    # ---- derive_health_state() is actually applied, not dead code ----------
    def test_dashboard_shows_offline_for_a_stale_device_not_stuck_on_ok_forever(self):
        # Audit fix regression test: derived_health_state was written once
        # (as a hardcoded 'ok') at push time and never recomputed, so a
        # device that had gone silent for hours still showed "ok" forever.
        # The dashboard must recompute health live from last_seen_ms.
        self.assertEqual(self._push(VALID_KEY, device_id="device-L").status_code, 200)
        c = server.db()
        stale_ms = int(time.time() * 1000) - (10 * 60 * 1000)  # 10 min ago
        c.execute("UPDATE devices SET last_seen_ms=? WHERE device_id=?", (stale_ms, "device-L"))
        c.commit(); c.close()

        resp = self.client.get("/admin/devices", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 200)
        html = resp.data.decode("utf-8")
        row_start = html.index("device-L")
        row_fragment = html[row_start:row_start + 400]
        self.assertIn("offline", row_fragment)
        self.assertNotIn('class="ok">ok', row_fragment)


if __name__ == "__main__":
    unittest.main()
