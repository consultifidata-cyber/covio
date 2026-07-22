# 22 — Final Hardware-Test Artifact Manifest

All firmware binaries below were built via `pio run -e esp32dev` (compile
only, never `-t upload`). All manifests were signed for real via
`server/tools/sign_manifest.py` using the test keypair generated this
session (`server/tools/.test_signing_key.pem`, gitignored, private key
never committed). Nothing was uploaded to any device.

## 1. Known-good current image

| Field | Value |
|---|---|
| Filename | `evidence/known-good-1.0.0.bin` |
| Commit | `d24051f` (this phase) and unchanged since |
| Environment | `esp32dev` |
| Semantic version | `1.0.0` |
| Security version | `1` |
| Hardware compatibility ID | `covio-oilflow-v1` |
| Schema version | `1` |
| File size | 1,026,640 bytes (may differ slightly from phase-1's recorded hash if this session's compile flags changed anything — re-verify at test time) |
| SHA-256 | `7cc6f373eb389069f88ebaccdd7db29467e8f8fc8adce3041abed09bf8cd0fa6` |
| Signing-key ID | N/A — recovery/rollback baseline, not offered via a signed manifest |
| Intended test | USB recovery baseline (doc 09) ONLY — per the mandate's correction, this is NOT how OTA success is proven |
| Expected device behavior | Currently-running firmware; used to flash back to a known state if needed |

## 2. Valid signed higher-version candidate

| Field | Value |
|---|---|
| Filename | `evidence/candidate-1.0.1-candidate.bin` + `evidence/signed_manifest_candidate.json` |
| Commit | `d24051f` |
| Environment | `esp32dev`, `FW_VERSION` temporarily bumped (never committed — see phase-1 doc 07) |
| Semantic version | `1.0.1-candidate` |
| Security version | `1` (same as known-good — a same-security-version upgrade) |
| Hardware compatibility ID | `covio-oilflow-v1` |
| Schema version | `1` |
| File size | 1,026,688 bytes |
| SHA-256 | `5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89` (real, computed by the signing tool from the actual file, matches phase-1's independently-computed value) |
| Signing-key ID | `covio-test-key-2026-07` |
| Manifest SHA-256 | `cfde9c1555f60420afd62305a9d92fee3ae0f697adaba533e32fab8aeaf3b968` |
| Intended test | Physical Test 1 — successful authenticated OTA (doc 23) |
| Expected device behavior | Manifest verifies, downloads, hash matches, commits, reboots, `fw_version` reports `1.0.1-candidate` |

## 3. Validly-signed but intentionally unhealthy rollback candidate

**Not pre-built** (same rationale as phase 1's doc 07: constructing this
requires a throwaway `covio_firmware.ino` edit — e.g. commenting out
`syncEngine.wifiConnect()` so `confirmHealthyBoot()` never fires — that
should be built fresh immediately before the authorized test, not aged in
advance). Exact construction and signing commands are in doc 23, Physical
Test 2.

## 4. Lower-security-version signed downgrade candidate

| Field | Value |
|---|---|
| Filename | `evidence/known-good-1.0.0.bin` (reused — the rejection happens before download, so the actual binary offered doesn't matter) + `evidence/signed_manifest_downgrade_test.json` |
| Security version | `0` (below the known-good's own compiled-in floor of `1`) |
| Manifest SHA-256 | `034fce2ca380b81e65dc02c8bbce93ea43fc55092e0b3eed688ea37ab61531d3` |
| Intended test | Physical Test 3 — anti-downgrade rejection (doc 23) |
| Expected device behavior | Rejected at the `evaluateOtaCandidate()` gate with `downgrade_rejected`, visible via `/api/v1/status`'s `last_reject_reason`, BEFORE the authenticity gate or any download |

## 5. Tampered-signature candidate manifest

| Field | Value |
|---|---|
| Filename | `evidence/signed_manifest_tampered.json` (identical to artifact 2's manifest, with `security_version` changed from `1` to `999` AFTER signing) |
| Manifest SHA-256 | `03701e80b3bad99be9c39dadac88f99e6d7cef5982858716f8473061aa575614` |
| Intended test | Physical Test 4 — invalid-signature rejection (doc 23) |
| Expected device behavior | `mbedtls_pk_verify()` fails (the signature was computed over the ORIGINAL canonical string, which no longer matches the tampered field) → `OTA_AUTH_SIG_VERIFY_FAILED`, rejected before download |

## 6. Hash-mismatch candidate/manifest

**Not pre-built** — construction is trivial at test time: take artifact 2's
signed manifest, edit `image_sha256` to any other valid-looking 64-hex-char
string, and re-save WITHOUT re-signing (or, to test a subtler case, point
`url` at a DIFFERENT, unsigned-for binary while keeping the original
signed hash — either produces the same `OTA_AUTH...`-then-download,
hash-mismatch-after-download outcome, exercising a different point in the
pipeline). Documented here as a construction recipe rather than a
pre-built file to avoid two near-duplicate artifacts.

## 7. Interrupted-download test configuration

Not a file — an operational step (doc 23, Physical Test 6): interrupt
network delivery mid-transfer (e.g., temporarily block the bench server's
firmware route) while artifact 2's legitimate manifest/image are being
served.

## Verification instructions

```
sha256sum Docs/audit/coviu_oil_meter_p0_remediation_phase2/evidence/*.bin
sha256sum Docs/audit/coviu_oil_meter_p0_remediation_phase2/evidence/*.json
```
Confirm these match the values recorded above, and confirm
`signed_manifest_candidate.json`'s `signature` field verifies against
`ota_keys.h`'s currently-committed `COVIO_OTA_PUBLIC_KEY_PEM` before using
any artifact in a physical test.
