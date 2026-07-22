# 18 — OTA Anti-Downgrade: Real Execution Results (RISK-15)

## Result: 14/14 PASSED (real, executed this session, zero fixes needed)

Unlike the queue fault-injection suite (doc 17), `test_ota_version_policy.cpp`
compiled and passed on the **first attempt**, no bugs found. This is
consistent with `evaluateOtaCandidate()` being a small, pure function with
no filesystem/preprocessor interaction to get subtly wrong.

## Coverage confirmation against the mandate's list

- Upgrade accepted: `test_upgrade_higher_security_version_accepted` PASS.
- Same-version policy explicitly proven: `test_same_security_version_is_accepted_not_a_downgrade` PASS.
- Downgrade rejected: `test_downgrade_rejected`, `test_much_older_version_rejected` PASS.
- Lower security version rejected even with higher semantic version: `test_higher_semantic_version_with_lower_security_version_rejected` PASS.
- Wrong hardware rejected: `test_wrong_hardware_revision_rejected` PASS (+ positive/absent-field cases also PASS).
- Wrong schema rejected: `test_wrong_schema_version_rejected` PASS.
- Missing/malformed metadata rejected: `test_missing_security_version_rejected`, `test_malformed_negative_security_version_rejected` PASS.
- Version overflow/malformed rejected: same tests, plus `test_zero_security_version_against_zero_floor_is_valid_not_malformed` confirming zero is NOT conflated with "malformed."
- Accepted security floor survives simulated reboot: **not directly tested by this C++ suite** — `evaluateOtaCandidate()` is stateless/pure and doesn't model NVS persistence at all; this property is instead a consequence of `store.h`'s `Preferences`-backed `securityVersion()`/`setSecurityVersion()` (Arduino/ESP-IDF-coupled, not host-testable this pass) combined with `ota.h::confirmHealthyBoot()`'s gating — traced by code review (doc 04), not proven by an executed test.
- Factory reset does not erase the security floor: same — a property of `store.h`'s SEPARATE NVS namespace (`NVS_NS_SECURITY`), confirmed by code review, not an executed test (Arduino/`Preferences`-coupled).
- Failed candidate rollback does not reduce the floor: confirmed by code review of `confirmHealthyBoot()`'s exclusive floor-advancement point (doc 04) — not an executed test.
- OTA rejection occurs before image download/write begins: confirmed by code review of `ota.h::poll()`'s control flow — `evaluateOtaCandidate()` and `verifyManifestAuthenticity_()` are both called, and must both return an accept verdict, strictly before `doVerifiedUpdate_()` is ever reached. Not independently executed on hardware.

## RISK-15 status: **CODE-CLOSED / CI-PROVEN (LOCAL) — HARDWARE-PENDING**

The decision-logic tests are real and pass. The three items above
(NVS persistence, factory-reset immunity, floor-never-lowers-on-failure)
are Arduino/ESP-IDF-coupled properties this session traced by code review
but did not — and could not, without a real ESP32 — execute as tests.
**Full closure requires the physical downgrade-rejection test** in the
revised physical test plan (doc 23), run through the actual OTA path, not
a USB flash.
