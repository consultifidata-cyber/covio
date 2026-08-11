"""
Native-host tests for P0-3 remediation (RISK-03: unauthenticated admin APIs).

See docs/audit/coviu_oil_meter_p0_remediation/04_ADMIN_API_SECURITY_REMEDIATION.md
for the design rationale. "Native-host" means: runs on a plain development
machine, no ESP32 hardware -- these tests exercise server.py's /admin/* routes
directly via Flask's test client, against a throwaway sqlite database (never
the real server/covio.db).

Run:
    python -m unittest discover -s test/native -v
or simply:
    python test/native/test_p0_3_admin_auth.py
"""

import base64
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server"))
import server  # noqa: E402

ADMIN_ROUTES_ADMIN_ROLE = [
    ("POST", "/admin/kfactor", {"data": {"k_factor": "1000", "density": "0.84", "t_ref": "15"}}),
    (
        "POST",
        "/admin/devices/provision",
        {"data": json.dumps({"device_id": "d1"}), "content_type": "application/json"},
    ),
    ("POST", "/admin/devices/some-device/revoke-key", {}),
    ("POST", "/admin/devices/some-device/rotate-key", {}),
]
ADMIN_ROUTES_VIEWER_OK = [
    ("GET", "/", {}),
    ("GET", "/admin/devices", {}),
    ("GET", "/admin/events", {}),
    ("GET", "/admin/devices/some-device/events", {}),
]


class P0_3_AdminAuthTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self._tmp.close()
        self._orig_db = server.DB
        server.DB = self._tmp.name
        server.init_db()
        server.app.testing = True
        self.client = server.app.test_client()
        # Each test gets a clean rate-limit slate -- the limiter is
        # process-global (by design, see server.py's module docstring), so
        # without this, an earlier test's failures could spuriously trip the
        # next test's rate limit.
        server._admin_auth_failures.clear()

    def tearDown(self):
        server.DB = self._orig_db
        os.unlink(self._tmp.name)

    def _call(self, method, path, auth=None, **kwargs):
        fn = self.client.get if method == "GET" else self.client.post
        return fn(path, auth=auth, **kwargs)

    def _basic_header(self, user, pw):
        token = base64.b64encode(f"{user}:{pw}".encode()).decode()
        return {"Authorization": f"Basic {token}"}

    # ---- 1. anonymous access rejected for every admin route ------------------
    def test_anonymous_access_rejected_for_every_admin_route(self):
        for method, path, kwargs in ADMIN_ROUTES_ADMIN_ROLE + ADMIN_ROUTES_VIEWER_OK:
            with self.subTest(route=path):
                # Cleared per-route: this test's intent is "route X rejects
                # anonymous access", checked independently per route -- the
                # rate limiter itself (which WOULD otherwise trip partway
                # through this loop, since Flask's test client always
                # presents the same remote_addr) is covered by its own
                # dedicated tests above/below, not here.
                server._admin_auth_failures.clear()
                resp = self._call(method, path, **kwargs)
                self.assertEqual(resp.status_code, 401, f"{path} did not reject anonymous access")

    # ---- 2. read-only (viewer) user rejected on mutating routes --------------
    def test_viewer_role_rejected_on_admin_only_mutating_routes(self):
        # Configure a real viewer password for this test process.
        server.VIEWER_PASSWORD = "viewer-secret-for-test"
        try:
            for method, path, kwargs in ADMIN_ROUTES_ADMIN_ROLE:
                with self.subTest(route=path):
                    resp = self._call(
                        method, path, auth=("viewer", "viewer-secret-for-test"), **kwargs
                    )
                    self.assertEqual(
                        resp.status_code, 401, f"{path} accepted a viewer-role credential"
                    )
        finally:
            server.VIEWER_PASSWORD = None

    def test_viewer_role_accepted_on_read_only_routes(self):
        server.VIEWER_PASSWORD = "viewer-secret-for-test"
        try:
            for method, path, kwargs in ADMIN_ROUTES_VIEWER_OK:
                with self.subTest(route=path):
                    resp = self._call(
                        method, path, auth=("viewer", "viewer-secret-for-test"), **kwargs
                    )
                    self.assertNotEqual(
                        resp.status_code, 401, f"{path} rejected a valid viewer credential"
                    )
        finally:
            server.VIEWER_PASSWORD = None

    # ---- 3. correct admin role succeeds --------------------------------------
    def test_admin_role_succeeds_on_every_admin_route(self):
        auth = ("admin", server.ADMIN_PASSWORD)
        for method, path, kwargs in ADMIN_ROUTES_ADMIN_ROLE + ADMIN_ROUTES_VIEWER_OK:
            with self.subTest(route=path):
                resp = self._call(method, path, auth=auth, **kwargs)
                self.assertNotEqual(
                    resp.status_code, 401, f"{path} rejected a valid admin credential"
                )

    # ---- 4. wrong admin password rejected -------------------------------------
    def test_wrong_admin_password_rejected(self):
        resp = self._call("GET", "/admin/devices", auth=("admin", "definitely-not-the-password"))
        self.assertEqual(resp.status_code, 401)

    # ---- 5. auth failures are rate-limited ------------------------------------
    def test_repeated_auth_failures_are_rate_limited(self):
        for _ in range(server.ADMIN_RATE_LIMIT_MAX_FAILURES):
            resp = self._call("GET", "/admin/devices", auth=("admin", "wrong"))
            self.assertEqual(resp.status_code, 401)
        # One more, over the threshold -- must now be throttled (429), not
        # just another 401, even with the CORRECT password: the rate limit
        # gates on the SOURCE, not on whether this particular attempt would
        # have succeeded (preventing further credential-guessing attempts).
        resp = self._call("GET", "/admin/devices", auth=("admin", server.ADMIN_PASSWORD))
        self.assertEqual(resp.status_code, 429)

    def test_rate_limit_window_is_per_source_address_not_global(self):
        # Flask's test client always presents the same remote_addr, so this
        # test documents the LIMITER's key (remote_addr), not a full
        # multi-client simulation -- verified directly against the limiter
        # function rather than faking a second IP through the test client.
        self.assertFalse(server._admin_rate_limited("1.2.3.4"))
        for _ in range(server.ADMIN_RATE_LIMIT_MAX_FAILURES):
            server._record_admin_auth_failure("1.2.3.4")
        self.assertTrue(server._admin_rate_limited("1.2.3.4"))
        self.assertFalse(
            server._admin_rate_limited("5.6.7.8"), "rate limit leaked across source addresses"
        )

    # ---- 6. every failed AND successful sensitive operation is audited ------
    def test_failed_admin_auth_is_recorded_in_device_events(self):
        self._call("GET", "/admin/devices", auth=("admin", "wrong"))
        c = server.db()
        rows = c.execute(
            "SELECT event_type FROM device_events WHERE event_type='ADMIN_AUTH_FAILED'"
        ).fetchall()
        c.close()
        self.assertGreaterEqual(len(rows), 1)

    def test_successful_sensitive_operations_remain_individually_audited(self):
        # Provisioning/revoke/rotate/calibration-change events already exist
        # (DM-Phase 4) -- confirm they still fire now that auth wraps them
        # (i.e. the decorator does not swallow or short-circuit the view).
        auth = ("admin", server.ADMIN_PASSWORD)
        self.client.post(
            "/admin/devices/provision",
            data=json.dumps({"device_id": "d1"}),
            content_type="application/json",
            auth=auth,
        )
        c = server.db()
        rows = c.execute(
            "SELECT event_type FROM device_events WHERE event_type='DEVICE_PROVISIONED'"
        ).fetchall()
        c.close()
        self.assertEqual(len(rows), 1)

    # ---- 7. missing production admin config fails closed at startup ---------
    def test_production_mode_without_password_fails_closed(self):
        with self.assertRaises(RuntimeError):
            server._resolve_admin_credentials(env={"COVIO_ADMIN_MODE": "production"})

    def test_production_mode_with_obvious_placeholder_password_fails_closed(self):
        for bad in ("admin", "password", "changeme", "dev-key-change-me", ""):
            with self.subTest(bad_password=bad):
                with self.assertRaises(RuntimeError):
                    server._resolve_admin_credentials(
                        env={"COVIO_ADMIN_MODE": "production", "COVIO_ADMIN_PASSWORD": bad}
                    )

    def test_production_mode_with_a_real_password_starts_cleanly(self):
        creds = server._resolve_admin_credentials(
            env={
                "COVIO_ADMIN_MODE": "production",
                "COVIO_ADMIN_PASSWORD": "a-real-random-secret-value",
            }
        )
        self.assertEqual(creds["admin_password"], "a-real-random-secret-value")

    def test_dev_mode_with_no_password_set_generates_one_never_a_hardcoded_default(self):
        creds1 = server._resolve_admin_credentials(env={})
        creds2 = server._resolve_admin_credentials(env={})
        self.assertTrue(creds1.get("_generated"))
        self.assertNotEqual(
            creds1["admin_password"],
            creds2["admin_password"],
            "dev-mode generated password must not be a fixed/shared constant",
        )

    # ---- 8. secrets are never returned in plaintext by an admin route -------
    def test_admin_routes_never_echo_the_admin_password_itself(self):
        resp = self._call("GET", "/admin/devices", auth=("admin", server.ADMIN_PASSWORD))
        self.assertNotIn(server.ADMIN_PASSWORD.encode(), resp.data)

    # ---- 9. Basic-Auth header for a browser <form> POST works end-to-end ----
    def test_kfactor_form_post_authenticates_via_basic_auth_header(self):
        headers = self._basic_header("admin", server.ADMIN_PASSWORD)
        resp = self.client.post(
            "/admin/kfactor",
            data={"k_factor": "1234.5", "density": "0.9", "t_ref": "20"},
            headers=headers,
        )
        self.assertEqual(resp.status_code, 303)  # redirect back to dashboard, per existing behavior
        c = server.db()
        row = c.execute("SELECT k_factor FROM calibration WHERE id=1").fetchone()
        c.close()
        self.assertEqual(row["k_factor"], 1234.5)


if __name__ == "__main__":
    unittest.main()
