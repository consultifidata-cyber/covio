"""
Native-host tests for the Balaji V1 freeze remediation's API key fingerprint
tool (scripts/verify_api_key_fingerprint.py).

Verifies the fingerprint computation independently reproduces
provision.h::apiKeyDisplay_()'s exact algorithm (SHA-256, first 6 bytes,
lowercase hex) -- computed here via a completely separate hashlib call, not
by importing/reusing the tool's own function for the expected value, so
this actually cross-checks the algorithm rather than just testing that the
function returns whatever it returns.

Run:
    python -m unittest discover -s test/native -v
"""

import hashlib
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import verify_api_key_fingerprint as vf  # noqa: E402


class FingerprintTests(unittest.TestCase):
    def test_matches_the_exact_commissioning_session_fingerprint(self):
        # This is NOT the real production key -- it is a synthetic string
        # chosen only because independently hashing it here reproduces the
        # exact fingerprint format observed in this project's own
        # commissioning documentation (12 lowercase hex chars). The real
        # key was never available to, or handled by, this test.
        key = "probe-key-for-fingerprint-format-check"
        expected = hashlib.sha256(key.encode("utf-8")).digest()[:6].hex()
        self.assertEqual(vf.fingerprint(key), expected)
        self.assertEqual(len(vf.fingerprint(key)), 12)

    def test_fingerprint_is_lowercase_hex(self):
        fp = vf.fingerprint("SomeMixedCaseKeyValue123")
        self.assertTrue(all(c in "0123456789abcdef" for c in fp))

    def test_different_keys_produce_different_fingerprints(self):
        self.assertNotEqual(vf.fingerprint("key-one"), vf.fingerprint("key-two"))

    def test_same_key_is_deterministic(self):
        self.assertEqual(vf.fingerprint("stable-key"), vf.fingerprint("stable-key"))

    def test_independently_recomputed_against_raw_hashlib(self):
        """The tool's own fingerprint() must be nothing more than SHA-256
        truncated to 6 bytes -- proven here against a hand-rolled
        computation using only the stdlib, not the tool's internals."""
        for key in ("dev-key-change-me", "a", "a-much-longer-example-api-key-value-1234567890"):
            raw = hashlib.sha256(key.encode("utf-8")).hexdigest()[:12]
            self.assertEqual(vf.fingerprint(key), raw)


if __name__ == "__main__":
    unittest.main()
