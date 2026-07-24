# 30 — OTA Success Physical Test Result (ABORTED BEFORE WRITE)

Executes the Test 1 authorization mandate. **Stopped in Phase 1, before
any manifest/binary was placed in `server/firmware/` and before the
device was exposed to anything.** No OTA, download, flash, or reboot was
triggered by this session.

## Verdict: TEST ABORTED BEFORE OTA WRITE

## What passed cleanly (artifact/crypto verification, Phase 1)

- `candidate-1.0.1-candidate.bin` SHA-256 independently recomputed:
  `5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89` —
  **matches** the authorized value, the manifest's own `image_sha256`
  claim, and the existing `.sha256` sidecar file.
- `signed_manifest_candidate.json` SHA-256 independently recomputed:
  `cfde9c1555f60420afd62305a9d92fee3ae0f697adaba533e32fab8aeaf3b968` —
  **matches** the authorized value.
- Manifest ECDSA-P256/SHA-256 signature **independently verified valid**
  against `ota_keys.h`'s compiled-in `COVIO_OTA_PUBLIC_KEY_PEM`, using the
  exact canonical-string construction from `ota_manifest_auth.h`
  (`buildCanonicalManifestString`), reproduced in Python against the
  `cryptography` library (not trusting the device or server to self-report
  this). `key_id` matches (`covio-test-key-2026-07`). Manifest time window
  (`issued_at`/`expires_at`) is currently valid.
- Fresh pre-test baseline captured (timestamp `1784807993`): device ID
  `esp32-F4E5B2858428`, endpoint `http://192.168.1.3:8000`, health `ok`,
  no alarms, `ota.state: "none"`. Server DB: 37,741 contiguous records
  (`1..37741`), 0 quarantined, exactly 2 device rows (bootstrap seed +
  our unit) — isolation re-confirmed live.

## Why this stopped anyway — the blocking finding

The device's **live** `/api/v1/status` response (content-length verified,
not truncated) does not contain `security_version`, `accepted_security_floor`,
`last_reject_reason`, or `last_auth_reject_reason` inside its `ota` object:

```
{"ota":{"state":"none","running_version":"1.0.0"},"health_state":"ok"}
```

The **currently committed** `diagnostics.h` (this repo, this branch,
commit `af11924`) emits those four fields **unconditionally** on every
call — see the code:
```
s += ",\"security_version\":" + String(FW_SECURITY_VERSION);
s += ",\"accepted_security_floor\":" + String(st.securityVersion());
... last_reject_reason ... (always either a string or null)
```
Their total absence from a non-truncated, well-formed response is not
explainable as a formatting quirk — it means **the physically-running
firmware image predates the code that would emit them.**

`git log` on the files this test depends on:
```
c83fcd7  diagnostics.h created (initial baseline snapshot)
ae4e037  diagnostics.h touched (P0-4 flash-full fix)
ac9bc19  diagnostics.h gains security_version/accepted_security_floor/
         last_reject_reason; ota_version_policy.h CREATED (RISK-15,
         anti-downgrade logic)
d24051f  diagnostics.h gains last_auth_reject_reason; ota_manifest_auth.h
         and ota_keys.h CREATED (RISK-16, signature verification)
```
The device's live diagnostic surface is consistent with a build **at or
before `ae4e037`** — i.e. it most likely predates `ac9bc19` (RISK-15)
**and** `d24051f` (RISK-16) both. Doc 22 independently corroborates this:
"Nothing was uploaded to any device" this phase, and every prior document
in this entire audit trail (docs 00–29) states no physical firmware write
has occurred at any point. The device has, as far as this evidence trail
shows, never been reflashed since whenever it was originally provisioned
— almost certainly before the anti-downgrade/authenticity code this test
exists to validate was even written.

## Why this matters enough to stop

This test's entire premise is "prove the device's manifest-authenticity
and anti-downgrade verification work on real hardware." If the currently-
running image doesn't contain `ota_manifest_auth.h`'s
`verifyManifestAuthenticity_()` or `ota_version_policy.h`'s
`evaluateOtaCandidate()` at all, then serving the signed manifest now
would exercise **whatever older, unknown OTA code path actually is
compiled in** — not the code this test claims to certify. Two concrete
risks follow, either of which the mandate's own stop conditions cover:

1. **A false-positive PASS.** An old, pre-hardening `ota.h` might simply
   read `version`/`url` from the manifest (ignoring the newer fields
   entirely, per this project's own tolerant-field-extraction pattern)
   and perform a plain, unauthenticated OTA. The device would legitimately
   end up on `1.0.1-candidate`, `ota.state` might read `confirmed` — and
   it would look exactly like a passed authenticated-OTA test while having
   verified nothing. That is a worse outcome than an honest abort: it
   would certify RISK-15/RISK-16 as hardware-proven when they are not.
2. **Unknown/undocumented behavior.** An old client parsing a manifest
   shape it was never built to expect is genuinely untested territory —
   not one of the six characterized test cases in doc 23.

This is squarely stop condition 9 from this test-family's original
mandate ("existing firmware/build identity does not match the expected
baseline and cannot be explained") — except here it *can* be explained
(an unreflashed old baseline), which is exactly why it must be resolved,
not guessed past, before spending the one authorized physical write on a
result that wouldn't mean what it's supposed to mean.

## What was NOT done

- No file was written to `server/firmware/`.
- The device was never exposed to the candidate manifest or binary.
- No download, verification, partition write, or reboot occurred.
- No USB recovery was needed (nothing failed — nothing was attempted).
- The bench server remains running, unmodified, isolated.

---

# Required Final Response

## 1. Test Verdict
**TEST ABORTED BEFORE OTA WRITE**

## 2. Device and Environment
- Device ID: `esp32-F4E5B2858428`
- COM port: COM6 (not re-opened this test — all checks done via HTTP)
- Device IP: `192.168.1.4`
- Bench endpoint: `http://192.168.1.3:8000`
- Server commit: `af11924200f4a8621f9d7b3186bf53e5ded64dfb`
- Database isolation: confirmed live — exactly 2 device rows, 37,741
  contiguous records, 0 quarantined, no external integration tables

## 3. Artifact Verification
- Candidate: `candidate-1.0.1-candidate.bin`, SHA-256
  `5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89` — **match**
- Manifest: `signed_manifest_candidate.json`, SHA-256
  `cfde9c1555f60420afd62305a9d92fee3ae0f697adaba533e32fab8aeaf3b968` — **match**
- Signing-key ID: `covio-test-key-2026-07` — matches firmware's compiled-in `COVIO_OTA_KEY_ID`
- Signature verification result: **VALID** (independently verified against `ota_keys.h`'s public key, not device-reported)
- Image-hash verification result: matches on all three independent sources (file, manifest claim, authorization mandate)

## 4. OTA Timeline
Not entered. `manifest_seen` through `server_result_received`: none occurred.

## 5. Firmware and Boot Result
- Version before: `1.0.0` / after: unchanged, `1.0.0`
- Boot ID before: `19` / after: unchanged, `19`
- Reset reason: n/a — no reset this test
- Active partition / rollback state: unchanged
- Health state: `ok`, unchanged
- OTA terminal state: `none`, unchanged

## 6. Queue Reconciliation
Not applicable — no OTA window opened. For the record, at abort time:
records fully contiguous `1..37866+` (device continues live telemetry
throughout), 0 quarantined, 0 duplicates, 0 unexplained gaps.

## 7. Configuration and Storage
All unchanged — configuration, endpoint, device ID, calibration untouched.
`sd_status: "ok"`. Failed-write counter and byte-level storage usage
remain unexposed by this firmware's status endpoint (documented gap,
unchanged from doc 29). No active alarms.

## 8. Recovery
Not needed. Nothing was attempted, nothing failed.

## 9. Remaining Tests
Not yet authorized: 1) Automatic rollback, 2) Anti-downgrade rejection,
3) Invalid-signature rejection, 4) Hash-mismatch rejection,
5) Interrupted-download recovery.

## 10. Next Authorization Recommendation
**REMEDIATION REQUIRED BEFORE FURTHER HARDWARE TESTS**

Specifically: confirm what firmware commit is actually flashed on the
device (the only reliable way is a new, separately authorized read — e.g.
adding a build-identifier field to `/api/v1/info` and reflashing, or
accepting that a fresh, current-source USB flash establishing a known
baseline is required first) before any OTA test can genuinely exercise
the RISK-15/RISK-16 code this test family exists to validate.

---

**FIRST OTA TEST COMPLETE:** No rollback, downgrade, invalid-signature,
hash-mismatch, or interrupted-download test was executed. Further
physical testing requires a new explicit authorization.
