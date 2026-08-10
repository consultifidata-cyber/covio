"""
Native-host tests for RISK-16's offline signing tool
(server/tools/sign_manifest.py).

Verifies the tool's own canonical-string construction and that a signature
it produces actually verifies with the `cryptography` library against the
corresponding public key -- a real, independent-of-the-firmware round trip.
Cross-checking the exact canonical string against ota_manifest_auth.h's C++
implementation is done separately in
test/native_cpp/test_ota_manifest_auth.cpp (host-toolchain-dependent, not
run via this Python suite).

Run:
    python -m unittest discover -s test/native -v
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "server", "tools"))
import sign_manifest  # noqa: E402

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.exceptions import InvalidSignature


class SignManifestToolTests(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.key_path = os.path.join(self.tmpdir, "test_key.pem")
        self.image_path = os.path.join(self.tmpdir, "fake_fw.bin")
        with open(self.image_path, "wb") as f:
            f.write(b"\xde\xad\xbe\xef" * 1000)

    def _gen_key(self):
        sign_manifest.gen_test_key(self.key_path)
        with open(self.key_path, "rb") as f:
            return serialization.load_pem_private_key(f.read(), password=None)

    def test_gen_test_key_refuses_to_overwrite_existing_key(self):
        self._gen_key()
        with self.assertRaises(SystemExit):
            sign_manifest.gen_test_key(self.key_path)

    def test_gen_production_key_produces_a_real_p256_keypair(self):
        """Balaji V1 freeze remediation: --gen-production-key must be
        cryptographically identical to --gen-test-key (same curve, same
        real usable key) -- it differs only in destination path and
        operator messaging, never in the key material itself."""
        prod_key_path = os.path.join(self.tmpdir, "production_key.pem")
        sign_manifest.gen_production_key(prod_key_path)
        with open(prod_key_path, "rb") as f:
            priv = serialization.load_pem_private_key(f.read(), password=None)
        self.assertIsInstance(priv.curve, ec.SECP256R1)
        # A signature made with this key must actually verify -- proves the
        # production path produces a real, usable keypair, not a stub.
        pub = priv.public_key()
        sig = priv.sign(b"probe", ec.ECDSA(hashes.SHA256()))
        pub.verify(sig, b"probe", ec.ECDSA(hashes.SHA256()))  # raises if invalid

    def test_gen_production_key_refuses_to_overwrite_existing_key(self):
        prod_key_path = os.path.join(self.tmpdir, "production_key.pem")
        sign_manifest.gen_production_key(prod_key_path)
        with self.assertRaises(SystemExit):
            sign_manifest.gen_production_key(prod_key_path)

    def test_gen_production_key_writes_owner_only_permissions(self):
        if os.name == "nt":
            self.skipTest("POSIX file-mode bits are not meaningful on Windows")
        prod_key_path = os.path.join(self.tmpdir, "production_key.pem")
        sign_manifest.gen_production_key(prod_key_path)
        mode = os.stat(prod_key_path).st_mode & 0o777
        self.assertEqual(mode, 0o600)

    def test_canonical_string_field_order_and_delimiter(self):
        fields = {
            "hw_compat": "covio-oilflow-v1", "version": "1.0.1", "security_version": 1,
            "schema_version": 1, "image_size": 100, "image_sha256": "abc",
            "image_url": "http://x/y.bin", "channel": "stable",
            "issued_at": 111, "expires_at": 222, "manifest_id": "mid",
        }
        canonical = sign_manifest.build_canonical_string(fields)
        self.assertEqual(canonical,
                          "covio-oilflow-v1\n1.0.1\n1\n1\n100\nabc\nhttp://x/y.bin\nstable\n111\n222\nmid")

    def test_signed_manifest_produces_a_real_verifiable_signature(self):
        priv = self._gen_key()
        pub = priv.public_key()

        class Args:
            private_key = self.key_path
            hw_compat = "covio-oilflow-v1"
            version = "1.0.1"
            security_version = 1
            schema_version = 1
            image = self.image_path
            image_url = "http://192.168.1.3:8000/firmware/fw.bin"
            channel = "stable"
            key_id = "test-key"
            manifest_id = None
            valid_days = 30
            out = os.path.join(self.tmpdir, "manifest.json")
            print_canonical = False

        sign_manifest.sign_manifest(Args())

        import json, base64
        with open(Args.out) as f:
            manifest = json.load(f)

        # Independently recompute the canonical string and verify the
        # signature with a fresh cryptography.io call -- NOT reusing any of
        # sign_manifest.py's own verification logic (it doesn't have any;
        # this proves the SIGNATURE ITSELF is real and correct, not just
        # that the tool's internal bookkeeping is self-consistent).
        canonical = sign_manifest.build_canonical_string({
            "hw_compat": manifest["hw_compat"], "version": manifest["version"],
            "security_version": manifest["security_version"], "schema_version": manifest["schema_version"],
            "image_size": manifest["image_size"], "image_sha256": manifest["image_sha256"],
            "image_url": manifest["url"], "channel": manifest["channel"],
            "issued_at": manifest["issued_at"], "expires_at": manifest["expires_at"],
            "manifest_id": manifest["manifest_id"],
        })
        sig = base64.b64decode(manifest["signature"])
        pub.verify(sig, canonical.encode("utf-8"), ec.ECDSA(hashes.SHA256()))  # raises if invalid -- no exception = pass

    def test_tampered_manifest_field_fails_verification(self):
        priv = self._gen_key()
        pub = priv.public_key()

        class Args:
            private_key = self.key_path
            hw_compat = "covio-oilflow-v1"
            version = "1.0.1"
            security_version = 1
            schema_version = 1
            image = self.image_path
            image_url = "http://192.168.1.3:8000/firmware/fw.bin"
            channel = "stable"
            key_id = "test-key"
            manifest_id = None
            valid_days = 30
            out = os.path.join(self.tmpdir, "manifest.json")
            print_canonical = False

        sign_manifest.sign_manifest(Args())

        import json, base64
        with open(Args.out) as f:
            manifest = json.load(f)

        # Tamper: attacker bumps security_version after signing (the exact
        # attack anti-downgrade + signing together are meant to prevent).
        tampered_canonical = sign_manifest.build_canonical_string({
            "hw_compat": manifest["hw_compat"], "version": manifest["version"],
            "security_version": 999,  # tampered
            "schema_version": manifest["schema_version"],
            "image_size": manifest["image_size"], "image_sha256": manifest["image_sha256"],
            "image_url": manifest["url"], "channel": manifest["channel"],
            "issued_at": manifest["issued_at"], "expires_at": manifest["expires_at"],
            "manifest_id": manifest["manifest_id"],
        })
        sig = base64.b64decode(manifest["signature"])
        with self.assertRaises(InvalidSignature):
            pub.verify(sig, tampered_canonical.encode("utf-8"), ec.ECDSA(hashes.SHA256()))

    def test_image_hash_is_the_real_sha256_of_the_image_file(self):
        import hashlib
        with open(self.image_path, "rb") as f:
            expected = hashlib.sha256(f.read()).hexdigest()
        priv = self._gen_key()

        class Args:
            private_key = self.key_path
            hw_compat = "covio-oilflow-v1"
            version = "1.0.1"
            security_version = 1
            schema_version = 1
            image = self.image_path
            image_url = "http://x/y.bin"
            channel = "stable"
            key_id = "test-key"
            manifest_id = None
            valid_days = 30
            out = os.path.join(self.tmpdir, "manifest.json")
            print_canonical = False

        sign_manifest.sign_manifest(Args())
        import json
        with open(Args.out) as f:
            manifest = json.load(f)
        self.assertEqual(manifest["image_sha256"], expected)
        self.assertEqual(manifest["image_size"], os.path.getsize(self.image_path))


if __name__ == "__main__":
    unittest.main()
