#!/usr/bin/env python3
"""
verify_api_key_fingerprint.py -- Balaji V1 freeze remediation (freeze-list
item 3: "production API key verification").

Computes the SAME SHA-256-derived 6-byte fingerprint the firmware's serial
console prints for its `show` command (provision.h::apiKeyDisplay_()) --
first 6 bytes of SHA-256(key), lowercase hex, 12 characters -- so an
operator holding the actual key value (e.g. freshly returned by the ERP's
/admin/devices/.../rotate-key endpoint) can confirm it matches what a
device reports over its serial console, WITHOUT ever transmitting, logging,
printing, or pasting the raw key value anywhere.

The key is NEVER accepted as a command-line argument (it would land in
shell history and process listings) and this tool NEVER prints it back --
only the computed fingerprint. Read from stdin (pipe it from wherever it's
currently stored) or typed interactively (hidden input via getpass).

Usage:
    # Compare against a fingerprint already read off the device's serial
    # console (`show` command's "api_key: configured (fingerprint=...)"):
    python scripts/verify_api_key_fingerprint.py --expected-fingerprint 18519bce8d2a
    (then paste/type the key at the hidden prompt)

    # Non-interactive (e.g. piped from a secrets manager CLI):
    echo -n "$THE_KEY" | python scripts/verify_api_key_fingerprint.py \\
        --from-stdin --expected-fingerprint 18519bce8d2a

    # Just compute a fingerprint with no comparison:
    python scripts/verify_api_key_fingerprint.py
"""
import argparse
import getpass
import hashlib
import sys


def fingerprint(key):
    """MUST exactly match provision.h::apiKeyDisplay_()'s computation:
    SHA-256(key), first 6 bytes, lowercase hex (12 characters)."""
    digest = hashlib.sha256(key.encode("utf-8")).digest()
    return digest[:6].hex()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--expected-fingerprint",
                    help="If given, compare the computed fingerprint against this value "
                         "(e.g. read off the device's serial console 'show' command)")
    p.add_argument("--from-stdin", action="store_true",
                    help="Read the key from stdin (one line, no trailing newline) instead of "
                         "an interactive hidden prompt -- for piping from a secrets manager")
    args = p.parse_args()

    if args.from_stdin:
        key = sys.stdin.readline().rstrip("\n")
    else:
        key = getpass.getpass("API key (input hidden, never echoed or logged): ")

    if not key:
        print("ERROR: empty key -- nothing to fingerprint.", file=sys.stderr)
        sys.exit(2)

    fp = fingerprint(key)
    print(f"fingerprint = {fp}")

    if args.expected_fingerprint:
        expected = args.expected_fingerprint.strip().lower()
        if fp == expected:
            print("MATCH -- this key produces the expected fingerprint.")
            sys.exit(0)
        else:
            print(f"MISMATCH -- expected {expected}, computed {fp}. "
                  f"This is NOT the key the device is currently configured with "
                  f"(or the device's fingerprint was read incorrectly).", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
