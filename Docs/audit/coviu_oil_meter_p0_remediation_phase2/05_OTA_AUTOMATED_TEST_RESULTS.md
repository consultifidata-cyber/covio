# 05 — OTA Anti-Downgrade Automated Test Results (RISK-15)

## Honest status: WRITTEN, STATICALLY REVIEWED, NOT LOCALLY EXECUTED

Same toolchain gap as `03_FLASH_FAULT_TEST_RESULTS.md` — no host C++
compiler exists on this machine. `test/native_cpp/test_ota_version_policy.cpp`
(**16 tests**) targets `ota_version_policy.h::evaluateOtaCandidate()`
directly — this file has **zero** Arduino/ESP-IDF dependency (confirmed by
reading it: only `<stdint.h>`/`<string.h>`), so it should be the most
portable, lowest-risk-of-a-silly-compile-error test in this entire
remediation. It is wired into the same new CI job as the queue
fault-injection tests (`.github/workflows/ci.yml`).

## Coverage against the mandate's 17-item OTA test list

| # | Requirement | Status |
|---|---|---|
| 1 | Upgrade N→N+1 accepted | Covered — `test_upgrade_higher_security_version_accepted` |
| 2 | Same version replay behavior | Covered — `test_same_security_version_is_accepted_not_a_downgrade` |
| 3 | Downgrade N+1→N rejected | Covered — `test_downgrade_rejected` |
| 4 | Much older version rejected | Covered — `test_much_older_version_rejected` |
| 5 | Higher semantic version, lower security version rejected | Covered — `test_higher_semantic_version_with_lower_security_version_rejected` |
| 6 | Lower semantic version, higher approved security version — explicit policy test | Covered — `test_lower_semantic_version_with_sufficient_security_version_accepted` |
| 7 | Wrong hardware revision rejected | Covered — `test_wrong_hardware_revision_rejected` (+ positive case `test_matching_hardware_revision_accepted`, + absent-field case `test_absent_hardware_field_does_not_block_acceptance`) |
| 8 | Wrong schema version rejected | Covered — `test_wrong_schema_version_rejected` |
| 9 | Invalid signature rejected | **NOT COVERED — no signing mechanism exists** (see doc 04) |
| 10 | Invalid hash rejected | **NOT COVERED — no hash-verification mechanism exists** (see doc 04) |
| 11 | Replayed manifest handled safely | Covered — `test_replayed_identical_manifest_is_idempotently_accepted` (pure-function idempotency) |
| 12 | Rollback to bootloader-designated previous image remains possible after candidate boot failure | **NOT COVERED by this test file** — this is `ota.h`'s (Arduino/ESP-IDF-coupled) responsibility, unchanged from the pre-existing rollback mechanism; not host-testable this pass (see doc 06) |
| 13 | A failed candidate does not incorrectly lower the accepted security floor | **NOT COVERED by a host test** — this is a property of `confirmHealthyBoot()`'s gating (Arduino-coupled), traced by code review (doc 04's "why the floor advances only in confirmHealthyBoot" section), not proven by an automated test this phase |
| 14 | Break-glass downgrade rejected without elevated authorization | **N/A — no break-glass path exists at all** (deliberately, see doc 04) |
| 15 | Break-glass action audited if implemented | **N/A**, same reason |
| 16 | Missing version metadata rejected | Covered — `test_missing_security_version_rejected` |
| 17 | Overflow/malformed version values rejected | Covered — `test_malformed_negative_security_version_rejected`, plus the boundary-clarifying `test_zero_security_version_against_zero_floor_is_valid_not_malformed` |

## Compatibility checks (mandate's explicit list)

| Scenario | Analysis |
|---|---|
| Old firmware (no anti-downgrade code) contacting the upgraded server | The manifest's new fields (`security_version`/`hw_compat`/`schema_version`) are additive JSON keys; old firmware's `extractStr_`/no-op parsing of unknown keys means it simply never looks at them — behaves exactly as before (accepts based on `version` string alone). This is a real compatibility gap in the OTHER direction worth naming: **an old, pre-RISK-15 device is not protected by this fix at all** until it is itself upgraded once (a one-time, one-directional bootstrap problem inherent to any anti-downgrade mechanism introduced after a fleet already exists) |
| New firmware (this fix) contacting the old server / a manifest missing the new fields | **Rejected** — `evaluateOtaCandidate()` requires `security_version` to be present; an old-style manifest is treated as `OTA_REJECT_MISSING_METADATA`, not silently accepted. This is intentional (see doc 04) but IS a breaking change for any manifest not yet updated to include the new field — operators must add `security_version` to `server/firmware/manifest.json` before this fixed firmware will ever accept ANY update again |
| Devices without stored security-version state | `Store::securityVersion()` defaults to `0` (NVS `getUInt` default) — a brand-new device accepts any manifest with `security_version >= 0`, i.e., effectively any well-formed manifest, matching "a new device has nothing to protect yet" |
| Factory-provisioned devices | Same as above — provisioning (`provision.h`) does not touch `NVS_NS_SECURITY`, confirmed by reading `provision.h` (unchanged by this phase) |
| Rolled-back devices | Floor unchanged (never was raised) — see doc 04 |
| Mixed firmware fleet | Each device's floor is independent (device-local NVS) — no fleet-wide security-version coordination exists or is implied by this design |

## RISK-15 final status

**NOT marked fully CLOSED** — the decision LOGIC is written, self-consistent
by manual trace, and will be proven by CI on next run; the actual
`ota.h::poll()`/`confirmHealthyBoot()` WIRING (Arduino-coupled) is
compile-verified only, and the manifest-compatibility breaking change above
requires an operator action (updating `manifest.json`) before this device
can receive ANY future update. Status:

**CODE-CLOSED / DECISION-LOGIC TESTS PENDING CI / OPERATOR MANIFEST UPDATE REQUIRED**
