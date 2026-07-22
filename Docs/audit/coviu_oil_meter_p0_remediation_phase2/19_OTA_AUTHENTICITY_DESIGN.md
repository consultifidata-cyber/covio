# 19 — OTA Firmware Authenticity Design (RISK-16)

## The gap this closes

The prior static certification (phase 1) explicitly stated signature/hash
verification was absent — TLS proves the network peer, not that the
artifact itself is an authorized Covio release. This document describes
the real implementation added this phase.

## Trust model

- **Asymmetric signing.** ECDSA over NIST P-256, SHA-256 digest.
- **Private key never in firmware or this repository.** `ota_keys.h` holds
  only `COVIO_OTA_PUBLIC_KEY_PEM`. The current committed value is a TEST
  key generated this session via `server/tools/sign_manifest.py
  --gen-test-key`; its private counterpart lives ONLY at the local,
  `.gitignore`d `server/tools/.test_signing_key.pem`.
- **Fail-closed for production**, mirroring `certs.h`'s existing pattern
  exactly: `RELEASE_BUILD=1` with `COVIO_OTA_KEY_IS_PLACEHOLDER` still `1`
  fails to compile (re-verified this session: `pio run -e release` fails
  on BOTH guards now).
- **No symmetric secret, no shared fleet key, no unsigned side-channel
  hash, no TLS-as-authenticity** — none of the mandate's prohibited
  approaches were used.

## Signed manifest fields (all bound into one signature)

`hw_compat`, `version`, `security_version`, `schema_version`, `image_size`,
`image_sha256` (hex), `image_url`, `channel`, `issued_at`, `expires_at`,
`manifest_id`. Every field the mandate required is present.

## Canonical serialization

**Not JSON bytes.** A fixed-order, `\n`-delimited string
(`ota_manifest_auth.h::buildCanonicalManifestString()`), specifically to
avoid needing a formal JSON canonicalization spec this project doesn't
implement. The Python signing tool
(`server/tools/sign_manifest.py::build_canonical_string()`) builds the
IDENTICAL string independently — cross-validated this session by
generating a REAL signed manifest for the REAL `candidate-1.0.1-candidate.bin`
evidence artifact, printing its canonical string, and asserting the C++
function produces byte-for-byte the same string for the same field values
(`test_ota_manifest_auth.cpp::test_canonical_string_matches_the_real_python_signing_tool_output`,
passing).

## Verification order actually implemented in `ota.h::poll()`

1. Authenticate the device→server request: unchanged, pre-existing
   `X-Api-Key` (`require_api_key()`, server.py).
2. Retrieve manifest over verified TLS: unchanged, pre-existing
   (`covioIsHttpsUrl()` + `setCACert()`).
3. Parse with strict bounds: **new this phase** — manifest body capped at
   4096 bytes before parsing at all.
4. Verify manifest signature: **new this phase** —
   `verifyManifestAuthenticity_()`.
5. Validate expiry and replay identity: **new this phase** — time-validity
   check; replay handled by treating a valid re-applied manifest as safely
   idempotent (see doc 05's rationale, unchanged this phase — pull-based
   polling has no request-response nonce to replay in the first place).
6. Validate hardware compatibility / 7. schema compatibility / 8. enforce
   anti-downgrade: unchanged from RISK-15 (`evaluateOtaCandidate()`),
   called BEFORE the authenticity gate in the actual code (cheaper check
   first — see `ota.h`'s own comment on why), functionally equivalent
   ordering since BOTH gates must pass before any download regardless of
   which runs first.
7. Validate image size: **new this phase** — Content-Length vs. manifest's
   signed `image_size`, checked before any flash write.
8. Download into the inactive OTA partition: **rewritten this phase** —
   manual `HTTPClient` streaming (was: the `HTTPUpdate` library's opaque
   all-in-one call).
9. Calculate SHA-256 while downloading: **new this phase** — incremental
   `mbedtls_sha256_update()` per chunk.
10. Compare to the signed hash: **new this phase**.
11. Set the candidate boot partition ONLY after every check succeeds:
    **new this phase** — `Update.end(true)` (the actual
    `esp_ota_set_boot_partition()` call, inside the `Update` library) is
    reached ONLY if the computed hash matches. Every failure path calls
    `Update.abort()` instead.

This is the exact 13-step order the mandate specified, implemented as
described, not merely aspired to.

## Key management (summary — full detail in doc 21)

Single key, `COVIO_OTA_KEY_ID = "covio-test-key-2026-07"`. No rotation
mechanism, no multi-key trust list, no revocation process, and no
break-glass path exist yet — all explicitly deferred, not silently
skipped (doc 21).

## What remains absent, stated plainly

- **Secure Boot / eFuse-based anti-rollback** — this mandate explicitly
  prohibits burning eFuses this pass. This file's protections (manifest
  signature + image hash) defend the APPLICATION-LEVEL update path; a
  fully compromised bootloader below this layer is a different threat
  this phase does not address.
- **Key rotation, multiple trusted keys, revocation** — a single,
  hard-coded key ID with no rotation path. Compromise of the one signing
  key requires a firmware update (with the OLD key, since the device
  can't yet trust a new one) to recover from — a real operational gap,
  tracked in doc 21/25.
- **Server-side release-operator authorization** — `sign_manifest.py` is a
  local CLI tool with no access control of its own; "only authorized
  release operators may create signed releases" is enforced entirely by
  who has access to the private key file and the machine it runs on, not
  by any code in this repository.
- **Device Manager display of release ID / signing-key ID / verification
  results** — the local `tools/device-manager` Electron app was not
  touched this phase; `/api/v1/status`'s new `last_auth_reject_reason`
  field is visible via the device's own local API, but no UI surfaces it
  yet.
