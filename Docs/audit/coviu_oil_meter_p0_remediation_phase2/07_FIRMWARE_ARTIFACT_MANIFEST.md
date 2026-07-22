# 07 — Firmware Artifact Manifest

All artifacts below were built with `pio run -e esp32dev` (compile only,
never `-t upload`) on this machine this session. None was uploaded to any
device. Hashes are real SHA-256 of the actual built `.bin` files, computed
this session.

## 1. Known-good (current committed firmware, `FW_VERSION "1.0.0"`)

- File: `evidence/known-good-1.0.0.bin`
- Size: 1,026,640 bytes
- SHA-256: `7cc6f373eb389069f88ebaccdd7db29467e8f8fc8adce3041abed09bf8cd0fa6`
- Built from: branch `fix/coviu-oil-meter-p0-enterprise-readiness`, commit `ac9bc19` (post RISK-04/RISK-15 code, pre this documentation commit) and unchanged since
- Contains: RISK-01/03/04/15 fixes from this branch's history

## 2. Candidate (throwaway version bump, `FW_VERSION "1.0.1-candidate"`)

- File: `evidence/candidate-1.0.1-candidate.bin`
- Size: 1,026,688 bytes
- SHA-256: `5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89`
- Built by temporarily editing `config.h`'s `FW_VERSION` line, building,
  capturing the artifact, then reverting via `git checkout -- config.h`
  (confirmed clean working tree afterward — see doc 01). **This edit was
  never committed** — it exists only as this one built binary.
- `FW_SECURITY_VERSION` was left at `1` (same as known-good) — this
  candidate is a same-security-version upgrade, suitable for exercising
  the OTA **success** path (Physical Test 1, doc 08) without also
  exercising the anti-downgrade gate (which needs a genuinely lower
  `security_version` candidate instead, see below).

## 3. Deliberately-unhealthy candidate — NOT PRE-BUILT

Per the physical test plan (doc 08), a "boots but never proves healthy"
candidate is needed for the forced-rollback test. This was **not
pre-built** this session — constructing one requires a real, if temporary,
code change (e.g., commenting out `syncEngine.wifiConnect()` in `setup()`
so the device can never reach a successful sync and therefore never calls
`confirmHealthyBoot()`), and building + hashing it now, before physical
authorization exists, would age the artifact relative to whatever the
actual authorized test branch state is at authorization time. Doc 08
documents the EXACT, minimal change needed to construct one, to be done
immediately before the authorized test, not in advance.

## 4. Downgrade-test candidate — NOT PRE-BUILT, construction documented

For Physical Test 3 (downgrade rejection), a manifest offering
`security_version: 0` (below the known-good's compiled-in
`FW_SECURITY_VERSION 1`) against a device whose accepted floor is already
`>= 1` is what's needed — this requires no new firmware BINARY at all,
only a manifest (`server/firmware/manifest.json`) entry with a lower
`security_version` pointing at the ALREADY-BUILT known-good `.bin` (or
even the candidate `.bin` above, relabeled) — the rejection is decided
before any download begins, so the actual binary offered doesn't matter
for this specific test, only the manifest's claimed `security_version`.

## Verification instructions (for whoever performs the physical test)

```
sha256sum Docs/audit/coviu_oil_meter_p0_remediation_phase2/evidence/known-good-1.0.0.bin
sha256sum Docs/audit/coviu_oil_meter_p0_remediation_phase2/evidence/candidate-1.0.1-candidate.bin
```
Confirm these match the values recorded above before using either as a
manifest target.
