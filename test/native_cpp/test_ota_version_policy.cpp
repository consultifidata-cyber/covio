// ============================================================================
// test_ota_version_policy.cpp — RISK-15 remediation: OTA anti-downgrade
// decision logic tests, against the ACTUAL evaluateOtaCandidate() function
// ota.h::poll() calls (ota_version_policy.h) -- zero Arduino/ESP-IDF
// dependency, so this compiles as plain, portable C++ with no shim needed.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_ota_version_policy.cpp -o test_ota_version_policy
//   ./test_ota_version_policy
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../../ota_version_policy.h"

static std::vector<std::pair<std::string, std::function<bool()>>> g_tests;
#define TEST(name) \
  static bool name(); \
  static bool name##_registered = ([]{ g_tests.push_back({#name, name}); return true; })(); \
  static bool name()

static int g_assertFailures = 0;
#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("    CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_assertFailures++; \
    return false; \
  } \
} while (0)

static const char* MODEL = "covio-oilflow-v1";
static const long SCHEMA = 1;

// ---------------------------------------------------------------------------
// 1. Upgrade N -> N+1 accepted
// ---------------------------------------------------------------------------
TEST(test_upgrade_higher_security_version_accepted) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 2;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/1, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

// ---------------------------------------------------------------------------
// 2. Same version replay behavior — equal is accepted, not a downgrade
// ---------------------------------------------------------------------------
TEST(test_same_security_version_is_accepted_not_a_downgrade) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 2;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/2, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

// ---------------------------------------------------------------------------
// 3. Downgrade N+1 to N rejected
// ---------------------------------------------------------------------------
TEST(test_downgrade_rejected) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 1;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/2, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_DOWNGRADE);
  return true;
}

// ---------------------------------------------------------------------------
// 4. Much older version rejected (same code path, larger gap)
// ---------------------------------------------------------------------------
TEST(test_much_older_version_rejected) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 1;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/99, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_DOWNGRADE);
  return true;
}

// ---------------------------------------------------------------------------
// 5/6. Semantic-version vs security-version interplay: this policy only
// ever looks at security_version, never at the semantic FW_VERSION string
// (that comparison already happened in ota.h::poll() before this function
// is even called, purely to decide "is this a different image at all").
// A higher semantic version with a LOWER security version must still be
// rejected (test 5); explicit confirmation that a lower semantic version
// is allowed through this policy when its security_version is at/above the
// floor (test 6) -- because a genuine hotfix branched from an older
// release could legitimately carry a HIGHER security_version than a newer,
// less-patched mainline build, and semantic version strings alone cannot
// be trusted to reflect that.
// ---------------------------------------------------------------------------
TEST(test_higher_semantic_version_with_lower_security_version_rejected) {
  // (the "semantic version" itself isn't part of OtaCandidate -- ota.h's
  // poll() already extracted `ver` separately; this test documents that
  // evaluateOtaCandidate() would reject this candidate on security_version
  // alone, regardless of what the semantic version string claimed.)
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 1;  // "1.9.0" claiming security_version=1
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/3, MODEL, SCHEMA);  // device has seen security_version=3
  CHECK(v == OTA_REJECT_DOWNGRADE);
  return true;
}

TEST(test_lower_semantic_version_with_sufficient_security_version_accepted) {
  // "1.0.1-hotfix" claiming security_version=5, offered to a device whose
  // floor is 3 -- accepted purely on security_version, independent of
  // whatever the semantic version string says.
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 5;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/3, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

// ---------------------------------------------------------------------------
// 7. Wrong hardware revision rejected
// ---------------------------------------------------------------------------
TEST(test_wrong_hardware_revision_rejected) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 5;
  c.hasHwCompat = true; c.hwCompat = "covio-oilflow-v2-different-board";
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/1, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_HW_MISMATCH);
  return true;
}

TEST(test_matching_hardware_revision_accepted) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 5;
  c.hasHwCompat = true; c.hwCompat = MODEL;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/1, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

TEST(test_absent_hardware_field_does_not_block_acceptance) {
  // An older-style manifest with no hw_compat field at all must not be
  // rejected purely for omitting an optional field -- only security_version
  // is mandatory (see test_missing_security_version_rejected below).
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 5;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/1, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

// ---------------------------------------------------------------------------
// 8. Wrong schema version rejected
// ---------------------------------------------------------------------------
TEST(test_wrong_schema_version_rejected) {
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 5;
  c.hasSchemaVersion = true; c.schemaVersion = 2;   // device's SCHEMA_VERSION_CURRENT is 1 (SCHEMA constant above)
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/1, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_SCHEMA_MISMATCH);
  return true;
}

// ---------------------------------------------------------------------------
// 9/10. Invalid signature / invalid hash rejected
// ---------------------------------------------------------------------------
// NOT COVERED by evaluateOtaCandidate() or by this test file: this
// codebase's OTA has no manifest-signing or image-hash-verification
// mechanism at all (confirmed absent by the prior session's OTA static
// certification, unchanged by this phase -- see
// 06_OTA_STATIC_CERTIFICATION.md and 04_OTA_ANTI_DOWNGRADE_DESIGN.md). This
// phase closes RISK-15 (anti-downgrade) specifically; it does not add
// signing/hashing, which remains a separate, tracked, still-ABSENT gap.

// ---------------------------------------------------------------------------
// 11. Replayed manifest handled safely
// ---------------------------------------------------------------------------
TEST(test_replayed_identical_manifest_is_idempotently_accepted) {
  // Evaluating the exact same candidate twice against the same floor must
  // produce the exact same verdict both times -- this function is pure and
  // stateless, so idempotency is definitional, but asserted explicitly
  // rather than merely assumed.
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 3;
  OtaVerdict v1 = evaluateOtaCandidate(c, 1, MODEL, SCHEMA);
  OtaVerdict v2 = evaluateOtaCandidate(c, 1, MODEL, SCHEMA);
  CHECK(v1 == OTA_ACCEPT);
  CHECK(v1 == v2);
  return true;
}

// ---------------------------------------------------------------------------
// 12/13. Rollback / failed-candidate floor behavior — see ota.h's
// confirmHealthyBoot(): the floor is advanced ONLY there, never at
// poll()/doUpdate_() time. This function itself has no notion of "was the
// candidate applied" -- it only decides accept/reject BEFORE any flash
// write. The floor-advancement behavior (a failed candidate never raises
// the floor) is a property of ota.h's confirmHealthyBoot() gating, which
// (like the rest of ota.h) is Arduino/ESP-IDF-coupled and NOT covered by a
// host test in this phase -- documented here explicitly rather than
// silently assumed proven by this file's tests.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 14. Elevated authorization / break-glass downgrade
// ---------------------------------------------------------------------------
// NOT IMPLEMENTED: this phase deliberately does NOT add a break-glass
// downgrade path at all (the mandate permits one only under strict
// elevated-authorization/audit conditions, and explicitly forbids "an
// ordinary configuration switch that disables downgrade protection" --
// building a genuinely secure break-glass mechanism is a separate,
// larger undertaking than RISK-15 requires to close). There is currently
// NO way to legitimately downgrade a device's accepted security floor at
// all short of a physical NVS-partition erase (see store.h's
// factoryReset() comment) -- documented as a known operational
// consequence, not silently glossed over.

// ---------------------------------------------------------------------------
// 15. Missing version metadata rejected
// ---------------------------------------------------------------------------
TEST(test_missing_security_version_rejected) {
  OtaCandidate c;  // hasSecurityVersion defaults to false
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/0, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_MISSING_METADATA);
  return true;
}

// ---------------------------------------------------------------------------
// 16. Overflow / malformed version values rejected
// ---------------------------------------------------------------------------
TEST(test_malformed_negative_security_version_rejected) {
  OtaCandidate c;
  c.hasSecurityVersion = true;
  c.securityVersion = -1;  // ota.h's extractLong_ returns -1 for "absent or non-numeric"
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/0, MODEL, SCHEMA);
  CHECK(v == OTA_REJECT_MALFORMED_VERSION);
  return true;
}

TEST(test_zero_security_version_against_zero_floor_is_valid_not_malformed) {
  // A brand-new, never-updated device (floor defaults to 0, store.h) polling
  // a manifest that explicitly declares security_version=0 must be accepted
  // -- zero is a valid version number, not a sentinel for "malformed" (only
  // a genuinely negative parsed value is treated as malformed/absent).
  OtaCandidate c;
  c.hasSecurityVersion = true; c.securityVersion = 0;
  OtaVerdict v = evaluateOtaCandidate(c, /*floor=*/0, MODEL, SCHEMA);
  CHECK(v == OTA_ACCEPT);
  return true;
}

// ---------------------------------------------------------------------------
int main() {
  int passed = 0, failed = 0;
  for (auto& t : g_tests) {
    printf("RUN  %s\n", t.first.c_str());
    bool ok = t.second();
    if (ok) { printf("PASS %s\n", t.first.c_str()); passed++; }
    else    { printf("FAIL %s\n", t.first.c_str()); failed++; }
  }
  printf("\n%d passed, %d failed, %d total assertion failures\n", passed, failed, g_assertFailures);
  return failed == 0 ? 0 : 1;
}
