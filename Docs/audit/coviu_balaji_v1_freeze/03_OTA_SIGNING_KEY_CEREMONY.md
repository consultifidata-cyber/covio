# 03 — OTA Signing-Key Ceremony (record + operational procedure)

**Freeze-list item 1.** This document is both the record of the one-time
ceremony performed for Balaji V1 and the standing operational procedure for
any future re-run (key rotation, disaster recovery). It does not change the
signing/verification design itself — that was reviewed and found sound in
the Product Readiness Review (`ota.h`'s ECDSA-P256 manifest signing,
SHA-256 image-hash verification, and the anti-downgrade floor are
unchanged). This closes the one remaining gap: the device was shipping with
a test placeholder key, and the release build was designed to refuse to
compile until that was replaced.

## What was done (this session, 2026-07-26)

1. Generalized `server/tools/sign_manifest.py`'s existing `--gen-test-key`
   path into a shared `_generate_keypair()` core, and added
   `--gen-production-key` as its production counterpart — same ECDSA P-256
   cryptography, same 0600-permission local file write, same
   refuse-to-overwrite safety; the only difference is destination path and
   the operator-facing handling instructions (a production key needs to
   leave local disk after generation; a test key doesn't).
2. Ran `python server/tools/sign_manifest.py --gen-production-key`. It wrote
   the private key to `server/tools/.production_signing_key.pem` (gitignored,
   0600 permissions) and printed **only the public key** to the terminal —
   the private key was at no point printed, logged, or transmitted anywhere.
3. Pasted the printed public key into `ota_keys.h::COVIO_OTA_PUBLIC_KEY_PEM`,
   set `COVIO_OTA_KEY_ID` to `"covio-prod-key-2026-07"`, and flipped
   `COVIO_OTA_KEY_IS_PLACEHOLDER` from `1` to `0`. The `#if RELEASE_BUILD &&
   COVIO_OTA_KEY_IS_PLACEHOLDER` compile-time guard in `ota_keys.h` now
   passes for `[env:release]`, which was verified by a real PlatformIO
   compile of that environment (see the implementation summary for the
   build log).

## What must happen next (not performable by this session)

The private key currently sits at
`server/tools/.production_signing_key.pem` on this laptop. That is a
**temporary** location for the ceremony only. Before this key is used to
sign the first real release manifest that Balaji's device will accept:

1. Move `server/tools/.production_signing_key.pem` to this organization's
   designated secrets vault / password manager / HSM. (No such system is
   selected yet in this project's tooling — this document intentionally
   does not prescribe one; use whatever the team's standing secrets-storage
   practice is for equivalent material, e.g. the TLS/CA key handling
   already established under ADR-005.)
2. Delete the local copy from this laptop once the vault copy is confirmed
   retrievable. `*.pem` is gitignored repo-wide, so it was never at risk of
   being committed, but a laptop disk is not a durable or access-controlled
   place to be the only copy of a production signing key.
3. Record, outside this repository (in whatever internal system tracks
   operational secrets), the key id (`covio-prod-key-2026-07`), the
   generation date (2026-07-26), and who performed the ceremony.

## How to sign a real release manifest with this key

Once the private key is retrieved from wherever it's stored:

```
python server/tools/sign_manifest.py --sign \
    --private-key <path-to-retrieved-key> \
    --hw-compat covio-oilflow-v1 \
    --version <new-fw-version> \
    --security-version <bump-only-if-security-relevant> \
    --schema-version 1 \
    --image <path-to-built .bin> \
    --image-url <https-url-the-device-will-fetch-it-from> \
    --channel stable \
    --key-id covio-prod-key-2026-07 \
    --valid-days 30 \
    --out manifest.json
```

`--key-id` **must** be `covio-prod-key-2026-07` — the device rejects any
manifest whose `key_id` doesn't match `COVIO_OTA_KEY_ID` in `ota_keys.h`
(`ota.h`'s existing, unchanged verification logic).

## Key rotation / disaster recovery (unchanged limitation, stated honestly)

As documented in `Docs/audit/coviu_oil_meter_p0_remediation_phase2/21_SIGNING_KEY_MANAGEMENT_PLAN.md`,
this firmware trusts exactly one compiled-in public key — there is no
trust-list and no in-band rotation mechanism yet. If this key is ever
compromised or needs replacing, the only path is: generate a new keypair
(`--gen-production-key` again, to a new path), ship a firmware update
**signed with the current key** that embeds the new key, then transition
signing to the new key. This is unchanged by today's ceremony and is
correctly out of scope for a single-device freeze (see the canonical
architecture document's key-rotation design for the eventual dual-trust-window
mechanism, which is Phase 3 fleet-scale work, not required for Balaji).

## Rules compliance

The private key was never printed, logged, or included in any tool output
beyond the gitignored file itself. Only the public key (not secret by
definition) appears in this document and in `ota_keys.h`.
