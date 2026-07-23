"""
Native-host tests for the build-identity remediation
(scripts/build_identity.py). Pure decision logic, no PlatformIO/SCons
context needed -- exercises resolve_build_identity() directly with
injected fake git-query callables, exactly mirroring this project's own
established "pure function extracted for testability" pattern
(server.py's _resolve_admin_credentials(), test_p0_3_admin_auth.py).

Run:
    python -m unittest discover -s test/native -v
"""
import datetime
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
from build_identity import resolve_build_identity, BuildIdentityError  # noqa: E402

GOOD_SHA = "af11924200f4a8621f9d7b3186bf53e5ded64dfb"
FIXED_NOW = datetime.datetime(2026, 7, 23, 12, 0, 0)


def _now():
    return FIXED_NOW


class BuildIdentityReleaseTests(unittest.TestCase):
    """RELEASE_BUILD ("release" env): must fail closed on anything unsafe."""

    def test_clean_resolvable_sha_accepted(self):
        commit, dirty, build_time = resolve_build_identity(
            "release", lambda: GOOD_SHA, lambda: False, _now)
        self.assertEqual(commit, GOOD_SHA)
        self.assertFalse(dirty)
        self.assertEqual(build_time, "2026-07-23T12:00:00Z")

    def test_dirty_tree_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: GOOD_SHA, lambda: True, _now)

    def test_missing_git_identity_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: None, lambda: False, _now)

    def test_unknown_dirty_status_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: GOOD_SHA, lambda: None, _now)

    def test_malformed_sha_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: "not-a-real-sha", lambda: False, _now)

    def test_short_sha_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: GOOD_SHA[:7], lambda: False, _now)

    def test_placeholder_unknown_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: "unknown", lambda: False, _now)

    def test_placeholder_dev_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: "dev", lambda: False, _now)

    def test_placeholder_local_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: "local", lambda: False, _now)

    def test_all_zeros_sha_fails_closed(self):
        with self.assertRaises(BuildIdentityError):
            resolve_build_identity("release", lambda: "0" * 40, lambda: False, _now)


class BuildIdentityNonReleaseTests(unittest.TestCase):
    """esp32dev/factory: best-effort identity, must never fail the build."""

    def test_clean_resolvable_sha_reported_exactly(self):
        commit, dirty, _ = resolve_build_identity(
            "esp32dev", lambda: GOOD_SHA, lambda: False, _now)
        self.assertEqual(commit, GOOD_SHA)
        self.assertFalse(dirty)

    def test_dirty_tree_reported_not_raised(self):
        commit, dirty, _ = resolve_build_identity(
            "esp32dev", lambda: GOOD_SHA, lambda: True, _now)
        self.assertEqual(commit, GOOD_SHA)
        self.assertTrue(dirty)

    def test_missing_git_falls_back_without_raising(self):
        commit, dirty, _ = resolve_build_identity(
            "esp32dev", lambda: None, lambda: False, _now)
        self.assertEqual(commit, "dev-nogit")

    def test_malformed_sha_falls_back_without_raising(self):
        commit, _, _ = resolve_build_identity(
            "factory", lambda: "garbage", lambda: False, _now)
        self.assertEqual(commit, "dev-nogit")

    def test_unknown_dirty_status_defaults_true_not_silently_clean(self):
        # An unknown dirty status must never be reported as clean -- that
        # would be a false "this is exactly what's committed" claim.
        _, dirty, _ = resolve_build_identity(
            "esp32dev", lambda: GOOD_SHA, lambda: None, _now)
        self.assertTrue(dirty)

    def test_build_time_is_utc_and_machine_readable(self):
        _, _, build_time = resolve_build_identity(
            "esp32dev", lambda: GOOD_SHA, lambda: False, _now)
        # Round-trips through strptime with the exact format this project
        # commits to -- a machine-readable UTC timestamp, not a free-form string.
        parsed = datetime.datetime.strptime(build_time, "%Y-%m-%dT%H:%M:%SZ")
        self.assertEqual(parsed, FIXED_NOW)


if __name__ == "__main__":
    unittest.main()
