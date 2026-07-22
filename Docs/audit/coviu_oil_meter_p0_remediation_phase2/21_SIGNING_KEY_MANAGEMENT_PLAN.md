# 21 — Signing Key Management Plan

## Current state (test-key stage, this session)

| Item | Value |
|---|---|
| Signing algorithm | ECDSA over NIST P-256 (secp256r1) |
| Key length / security level | 256-bit curve, ~128-bit symmetric-equivalent security |
| Digest | SHA-256 |
| Public-key location in firmware | `ota_keys.h::COVIO_OTA_PUBLIC_KEY_PEM` (compiled in, `RELEASE_BUILD` fail-closed guard) |
| Key identifier | `COVIO_OTA_KEY_ID = "covio-test-key-2026-07"` (currently a TEST key generated this session) |
| Private key location | `server/tools/.test_signing_key.pem` — local, `.gitignore`d, never committed, never printed in full in any log or doc this session |

## Multiple trusted public-key support

**Not implemented.** `ota.h::verifyManifestAuthenticity_()` checks against
exactly one compiled-in key (`keyId != COVIO_OTA_KEY_ID` → reject). Adding
a trust list (array of `{key_id, public_key_pem}` pairs, iterate until one
verifies) is a bounded, real future extension — deliberately not built
this pass to keep RISK-16's scope to "close the missing-authenticity gap,"
not "build a full PKI."

## Key rotation mechanism

**Not implemented.** There is no in-band way to introduce a second trusted
key without a firmware update carrying the new key compiled in. A future
rotation would require: (1) generate new keypair, (2) ship a firmware
update SIGNED WITH THE OLD KEY that adds the new key to a trust list (once
that exists), (3) begin signing new releases with the new key, (4) once
the fleet has updated, a later release can drop the old key. None of this
exists yet — stated as a real limitation, not a solved problem.

## Revocation process

**Not implemented.** If the current test/production key is ever
compromised, there is no revocation list or short-circuit — the ONLY
recovery path today is: generate a new keypair, ship a firmware update
(signed with the compromised key, since it's still the only one trusted)
embedding the new key, and treat the compromised key as untrusted from
that point forward in application logic that does not yet exist. This is
a materially weaker position than a real PKI would provide.

## Disaster-recovery procedure if the release key is compromised

Given the above, the honest disaster-recovery answer today is: **there is
no clean one.** A compromised signing key can still sign a superficially
legitimate-looking malicious update, and the anti-downgrade floor
(RISK-15) does not protect against a malicious update at or above the
current security_version. The only mitigations available without further
engineering: (a) physical device recovery via USB reflash (bypasses OTA
entirely, doc 09's runbook), (b) revoking device API keys server-side
(RISK-03's admin auth) to at least stop a compromised manifest from being
FETCHED, which does not help if the attacker also controls the bench
server. This is flagged as a real, unresolved risk in the updated register
(doc 25), not glossed over.

## CI access controls for the future production private key

**Not applicable yet** — there is no CI pipeline with access to any
signing key (see doc 16: no git remote, no CI dispatch capability this
session). When a real CI/release pipeline is built, the production
private key must be stored in a CI secret manager (e.g., GitHub Actions
encrypted secrets, or better, a dedicated HSM/KMS), never in a repository
file or environment variable printed to logs — this is a requirement to
design for later, not something implemented now, since no such pipeline
exists to design it into.

## Test-key separation

Confirmed this session:
- The test private key never leaves `server/tools/.test_signing_key.pem`,
  gitignored.
- The test PUBLIC key committed to `ota_keys.h` is clearly labeled as a
  test key in comments, with `COVIO_OTA_KEY_IS_PLACEHOLDER=1` still set
  (the same flag that fails a `RELEASE_BUILD` compile) — a release build
  cannot accidentally ship with this test key still active.
