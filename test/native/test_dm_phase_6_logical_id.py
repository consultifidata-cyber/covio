"""
Native-host tests for DM-Phase 6 (Logical Device ID / §11.2, ADR-018).

See Docs/Covio_Device_Manager_Live_Readiness_Plan.md, §11.2 and §11.7, and
Docs/Firmware Detailed Architecture Decision Record (ADR).md's ADR-018, for
the identity model these exercise. "Native-host" means: runs on a plain
development machine, no ESP32 hardware -- these tests exercise server.py's
allocator and /admin/devices/provision directly via Flask's test client,
against a throwaway sqlite database (never the real server/covio.db),
matching the convention established by test_dm_phase_0b_auth.py/
test_dm_phase_4_registry.py.

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_dm_phase_6_logical_id.py
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402


class DmPhase6LogicalIdTests(unittest.TestCase):
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

    # P0-3 remediation (RISK-03): see test_dm_phase_4_registry.py's identical
    # comment -- /admin/* now requires HTTP Basic Auth.
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

    def _device_row(self, device_id):
        c = server.db()
        row = c.execute("SELECT * FROM devices WHERE device_id=?", (device_id,)).fetchone()
        c.close()
        return row

    # ---- migration -----------------------------------------------------------
    def test_migration_adds_logical_device_id_column_and_index(self):
        row = self._device_row("legacy-default-key")
        self.assertIsNotNone(row)
        self.assertIn("logical_device_id", row.keys())
        self.assertIsNone(row["logical_device_id"])

    def test_migration_is_idempotent_across_repeated_init_db_calls(self):
        server.init_db()
        server.init_db()
        c = server.db()
        n = c.execute(
            "SELECT COUNT(*) AS n FROM schema_migrations WHERE version=?",
            (server.DEVICES_SCHEMA_VERSION_LOGICAL_ID,),
        ).fetchone()["n"]
        c.close()
        self.assertEqual(n, 1)

    def test_migration_from_a_pre_dm_phase_6_devices_table_adds_the_column(self):
        # Simulate a DM-Phase-4-shape database (has asset_label etc., but no
        # logical_device_id yet) and confirm re-running init_db() adds it
        # without disturbing existing data.
        c = server.db()
        c.execute(
            "DELETE FROM schema_migrations WHERE version=?",
            (server.DEVICES_SCHEMA_VERSION_LOGICAL_ID,),
        )
        c.execute("UPDATE devices SET asset_label='keep-me' WHERE device_id='legacy-default-key'")
        c.commit()
        c.close()

        server.init_db()

        row = self._device_row("legacy-default-key")
        self.assertIn("logical_device_id", row.keys())
        self.assertEqual(row["asset_label"], "keep-me")

    # ---- allocator -------------------------------------------------------------
    def test_allocate_logical_device_id_format_and_sequential(self):
        c = server.db()
        first = server.allocate_logical_device_id(c)
        second = server.allocate_logical_device_id(c)
        c.close()
        self.assertRegex(first, r"^COV-\d{6}$")
        self.assertRegex(second, r"^COV-\d{6}$")
        self.assertEqual(int(second.split("-")[1]), int(first.split("-")[1]) + 1)

    def test_allocate_logical_device_id_never_collides_across_many_calls(self):
        c = server.db()
        ids = {server.allocate_logical_device_id(c) for _ in range(25)}
        c.close()
        self.assertEqual(len(ids), 25, "collision detected among allocated Logical Device IDs")

    # ---- test case: "two devices' keys never collide" extended to IDs ----------
    def test_provision_assigns_unique_sequential_logical_ids_to_different_devices(self):
        r1 = self._provision("device-A")
        r2 = self._provision("device-B")
        self.assertEqual(r1.status_code, 200)
        self.assertEqual(r2.status_code, 200)
        id_a = r1.get_json()["logical_device_id"]
        id_b = r2.get_json()["logical_device_id"]
        self.assertRegex(id_a, r"^COV-\d{6}$")
        self.assertRegex(id_b, r"^COV-\d{6}$")
        self.assertNotEqual(id_a, id_b)

    # ---- write-once / idempotent re-provisioning --------------------------------
    def test_reprovisioning_an_already_id_assigned_device_keeps_the_same_id(self):
        r1 = self._provision("device-C")
        id_first = r1.get_json()["logical_device_id"]

        # A second provision call (e.g. a routine key rotation/re-touch, not
        # the device's first-ever commissioning) must NOT burn a second ID.
        r2 = self._provision("device-C")
        id_second = r2.get_json()["logical_device_id"]
        self.assertEqual(id_first, id_second)

        row = self._device_row("device-C")
        self.assertEqual(row["logical_device_id"], id_first)

    def test_provisioned_at_ms_still_updates_on_reprovision_despite_stable_id(self):
        r1 = self._provision("device-D")
        first_provisioned_at = r1.get_json()["provisioned_at_ms"]
        r2 = self._provision("device-D")
        second_provisioned_at = r2.get_json()["provisioned_at_ms"]
        self.assertGreaterEqual(second_provisioned_at, first_provisioned_at)

    # ---- traceability ------------------------------------------------------------
    def test_provision_event_records_the_allocated_logical_device_id(self):
        r = self._provision("device-E", asset_label="Test Rig 1")
        logical_id = r.get_json()["logical_device_id"]
        events = self.client.get(
            "/admin/devices/device-E/events", auth=self._admin_auth()
        ).get_json()["events"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event_type"], "DEVICE_PROVISIONED")
        self.assertEqual(events[0]["detail"]["logical_device_id"], logical_id)

    # ---- dashboard renders the new column ---------------------------------------
    def test_devices_dashboard_shows_logical_id_column(self):
        r = self._provision("device-F")
        logical_id = r.get_json()["logical_device_id"]
        resp = self.client.get("/admin/devices", auth=self._admin_auth())
        self.assertEqual(resp.status_code, 200)
        self.assertIn(logical_id.encode(), resp.data)

    # ---- zero regression to DM-Phase 4's own response fields --------------------
    def test_provision_response_still_carries_every_pre_dm_phase_6_field(self):
        r = self._provision("device-G")
        data = r.get_json()
        for key in ("device_id", "api_key", "provisioned_at_ms"):
            self.assertIn(key, data)
        self.assertEqual(data["device_id"], "device-G")


if __name__ == "__main__":
    unittest.main()
