#!/usr/bin/env python3
"""
sign_manifest.py -- RISK-16 remediation: offline OTA manifest signing tool.

Implements the SAME canonical-string construction as ota_manifest_auth.h's
buildCanonicalManifestString() -- the two implementations are independently
maintained (Python here, C++ on-device) and must be kept byte-for-byte in
sync by hand. test/native_cpp/test_ota_manifest_auth.cpp cross-checks this
by comparing a canonical string this tool prints against what the C++
function produces for the identical input fields -- run that test after
changing either implementation.

This tool NEVER runs on the device and is NEVER shipped in firmware --
it is release-engineering tooling, run by whoever is authorized to cut a
signed Covio release, exactly matching ADR-005's existing "signing keys
generated and held by Covio, never distributed to the field or committed
to source control" discipline.

Algorithm: ECDSA over NIST P-256 (secp256r1), SHA-256 digest -- see
ota_keys.h's header comment for why this pairing was chosen (native
mbedTLS support on-device, no new firmware dependency).

Usage:
    # One-time: generate a TEST keypair (private key saved locally,
    # gitignored; public key printed for pasting into ota_keys.h).
    python server/tools/sign_manifest.py --gen-test-key

    # One-time PRODUCTION release ceremony (Balaji V1 freeze remediation --
    # see Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md):
    python server/tools/sign_manifest.py --gen-production-key

    # Sign a manifest:
    python server/tools/sign_manifest.py --sign \\
        --private-key server/tools/.test_signing_key.pem \\
        --hw-compat covio-oilflow-v1 --version 1.0.1 \\
        --security-version 1 --schema-version 1 \\
        --image server/firmware/covio-1.0.1.bin \\
        --image-url http://192.168.1.3:8000/firmware/covio-1.0.1.bin \\
        --channel stable --key-id covio-test-key-2026-07 \\
        --valid-days 30 \\
        --out server/firmware/manifest.json
"""

import argparse
import base64
import hashlib
import json
import os
import sys
import time

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

DEFAULT_TEST_KEY_PATH = os.path.join(os.path.dirname(__file__), ".test_signing_key.pem")
DEFAULT_PRODUCTION_KEY_PATH = os.path.join(os.path.dirname(__file__), ".production_signing_key.pem")


def build_canonical_string(fields):
    """MUST exactly match ota_manifest_auth.h::buildCanonicalManifestString()'s
    field order and delimiter ('\\n', no trailing newline)."""
    return "\n".join(
        str(x)
        for x in [
            fields["hw_compat"],
            fields["version"],
            fields["security_version"],
            fields["schema_version"],
            fields["image_size"],
            fields["image_sha256"],
            fields["image_url"],
            fields["channel"],
            fields["issued_at"],
            fields["expires_at"],
            fields["manifest_id"],
        ]
    )


def _generate_keypair(path):
    """Shared keypair-generation core for both --gen-test-key and
    --gen-production-key -- identical cryptography (ECDSA P-256), identical
    file-permission handling, identical refuse-to-overwrite safety. The two
    callers differ only in destination path and the operator-facing message
    (see gen_test_key()/gen_production_key() below), never in the key
    generation itself -- there is no cryptographic difference between a
    "test" and a "production" key, only in how carefully the private key is
    subsequently handled by a human, which this tool cannot enforce past the
    point of writing the file to local disk with 0600 permissions.
    """
    if os.path.exists(path):
        print(
            f"REFUSING to overwrite existing key at {path} -- "
            "remove it first if you really want a new one.",
            file=sys.stderr,
        )
        sys.exit(1)
    private_key = ec.generate_private_key(ec.SECP256R1())
    priv_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    with open(path, "wb") as f:
        f.write(priv_pem)
    os.chmod(path, 0o600)
    pub_pem = (
        private_key.public_key()
        .public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        .decode()
    )
    return pub_pem


def gen_test_key(path):
    pub_pem = _generate_keypair(path)
    print(f"Test private key written to: {path}")
    print(
        "This file is gitignored (server/tools/.test_signing_key.pem) -- "
        "it must NEVER be committed. Regenerate any time by deleting it "
        "and re-running --gen-test-key."
    )
    print()
    print(
        "Paste this PUBLIC key into ota_keys.h's COVIO_OTA_PUBLIC_KEY_PEM "
        "(TEST/non-production use only):"
    )
    print()
    print(pub_pem)


def gen_production_key(path):
    """Balaji V1 freeze remediation (OTA signing-key ceremony): the
    production counterpart of --gen-test-key. Same cryptography, same
    on-disk handling -- the difference is entirely operational, spelled out
    in the printed instructions below and in
    Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md. This
    tool NEVER transmits, uploads, or prints the private key material itself
    -- only its file path and the public key (which is not secret by
    definition) are ever displayed.
    """
    pub_pem = _generate_keypair(path)
    print(f"PRODUCTION private key written to: {path}")
    print()
    print("THIS FILE IS THE REAL COVIO RELEASE SIGNING KEY. It is gitignored")
    print("(matches this repo's *.pem catch-all) and must NEVER be committed,")
    print("emailed, pasted into chat, or stored anywhere this repository is")
    print("cloned to routinely. Immediately after this ceremony:")
    print("  1. Move this file to your organization's secrets vault / HSM /")
    print("     password manager (whichever this project's release process")
    print("     designates) -- do not leave it sitting on a laptop disk.")
    print("  2. Delete the local copy once it is safely stored elsewhere.")
    print("  3. Record the key id you assign it (see --key-id on --sign)")
    print("     in the signing key management log.")
    print("See Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md")
    print("for the full, step-by-step operational procedure this key feeds into.")
    print()
    print("Paste this PUBLIC key into ota_keys.h's COVIO_OTA_PUBLIC_KEY_PEM")
    print("and flip COVIO_OTA_KEY_IS_PLACEHOLDER to 0:")
    print()
    print(pub_pem)


def sign_manifest(args):
    with open(args.private_key, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    with open(args.image, "rb") as f:
        image_bytes = f.read()
    image_sha256 = hashlib.sha256(image_bytes).hexdigest()
    image_size = len(image_bytes)

    now = int(time.time())
    fields = {
        "hw_compat": args.hw_compat,
        "version": args.version,
        "security_version": args.security_version,
        "schema_version": args.schema_version,
        "image_size": image_size,
        "image_sha256": image_sha256,
        "image_url": args.image_url,
        "channel": args.channel,
        "issued_at": now,
        "expires_at": now + args.valid_days * 86400,
        "manifest_id": args.manifest_id or f"{args.version}-{now}",
    }

    canonical = build_canonical_string(fields)
    signature = private_key.sign(canonical.encode("utf-8"), ec.ECDSA(hashes.SHA256()))
    sig_b64 = base64.b64encode(signature).decode()

    manifest = {
        "version": fields["version"],
        "url": fields["image_url"],
        "security_version": fields["security_version"],
        "hw_compat": fields["hw_compat"],
        "schema_version": fields["schema_version"],
        "image_size": fields["image_size"],
        "image_sha256": fields["image_sha256"],
        "channel": fields["channel"],
        "issued_at": fields["issued_at"],
        "expires_at": fields["expires_at"],
        "manifest_id": fields["manifest_id"],
        "key_id": args.key_id,
        "signature": sig_b64,
    }

    out = json.dumps(manifest, indent=2)
    if args.out:
        with open(args.out, "w") as f:
            f.write(out)
        print(f"Signed manifest written to {args.out}")
    else:
        print(out)

    if args.print_canonical:
        print(
            "\n--- canonical string (for cross-checking against the C++ implementation) ---",
            file=sys.stderr,
        )
        print(repr(canonical), file=sys.stderr)


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--gen-test-key", action="store_true", help="Generate a new local test signing keypair"
    )
    p.add_argument(
        "--gen-production-key",
        action="store_true",
        help="Generate a new PRODUCTION signing keypair (one-time release ceremony -- "
        "see Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md)",
    )
    p.add_argument(
        "--production-key-path",
        default=DEFAULT_PRODUCTION_KEY_PATH,
        help=(
            "Destination for --gen-production-key "
            "(default: server/tools/.production_signing_key.pem)"
        ),
    )
    p.add_argument("--sign", action="store_true", help="Sign a manifest")
    p.add_argument("--private-key", default=DEFAULT_TEST_KEY_PATH)
    p.add_argument("--hw-compat", default="covio-oilflow-v1")
    p.add_argument("--version", default="1.0.1")
    p.add_argument("--security-version", type=int, default=1)
    p.add_argument("--schema-version", type=int, default=1)
    p.add_argument("--image", help="Path to the firmware .bin to sign")
    p.add_argument("--image-url", help="URL the device will download the .bin from")
    p.add_argument("--channel", default="stable")
    p.add_argument("--key-id", default="covio-test-key-2026-07")
    p.add_argument("--manifest-id")
    p.add_argument("--valid-days", type=int, default=30)
    p.add_argument("--out")
    p.add_argument("--print-canonical", action="store_true")
    args = p.parse_args()

    if args.gen_test_key:
        gen_test_key(args.private_key)
    elif args.gen_production_key:
        gen_production_key(args.production_key_path)
    elif args.sign:
        if not args.image or not args.image_url:
            p.error("--sign requires --image and --image-url")
        sign_manifest(args)
    else:
        p.print_help()


if __name__ == "__main__":
    main()
