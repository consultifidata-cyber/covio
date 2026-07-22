// ============================================================================
// test_ota_manifest_auth.cpp — RISK-16 remediation: canonical manifest string
// + time-validity tests, against the ACTUAL ota_manifest_auth.h functions
// ota.h calls. Zero Arduino/mbedTLS dependency (portable C++).
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_ota_manifest_auth.cpp -o test_ota_manifest_auth
//   ./test_ota_manifest_auth
// ============================================================================
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../../ota_manifest_auth.h"

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

// ---------------------------------------------------------------------------
// Cross-implementation consistency: this EXACT string was produced this
// session by `python server/tools/sign_manifest.py --sign ... --print-canonical`
// against the real candidate-1.0.1-candidate.bin evidence artifact --
// proving the Python signing tool and this C++ header agree byte-for-byte
// on the canonical format for a REAL set of field values, not just
// hand-picked test data. If either implementation's field order/delimiter
// ever drifts, this test catches it.
// ---------------------------------------------------------------------------
TEST(test_canonical_string_matches_the_real_python_signing_tool_output) {
  SignedManifestFields f;
  f.hwCompat = "covio-oilflow-v1";
  f.version = "1.0.1-candidate";
  f.securityVersion = 1;
  f.schemaVersion = 1;
  f.imageSize = 1026688;
  f.imageSha256Hex = "5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89";
  f.imageUrl = "http://192.168.1.3:8000/firmware/covio-1.0.1-candidate.bin";
  f.channel = "stable";
  f.issuedAt = 1784750026;
  f.expiresAt = 1787342026;
  f.manifestId = "1.0.1-candidate-1784750026";

  char buf[512];
  int n = buildCanonicalManifestString(f, buf, sizeof(buf));
  CHECK(n > 0);

  const char* expected =
    "covio-oilflow-v1\n1.0.1-candidate\n1\n1\n1026688\n"
    "5ab296975ede95f876804da20673fb351b604414464f2c9727d8b759d7e19e89\n"
    "http://192.168.1.3:8000/firmware/covio-1.0.1-candidate.bin\nstable\n"
    "1784750026\n1787342026\n1.0.1-candidate-1784750026";
  CHECK(strcmp(buf, expected) == 0);
  return true;
}

TEST(test_buffer_too_small_returns_failure_not_truncated_data) {
  SignedManifestFields f;
  f.hwCompat = "covio-oilflow-v1"; f.version = "1.0.1"; f.securityVersion = 1;
  f.schemaVersion = 1; f.imageSize = 12345; f.imageSha256Hex = "abc123";
  f.imageUrl = "http://example/x.bin"; f.channel = "stable";
  f.issuedAt = 1; f.expiresAt = 2; f.manifestId = "x";

  char tiny[8];
  int n = buildCanonicalManifestString(f, tiny, sizeof(tiny));
  CHECK(n == -1);   // must fail loudly, never silently truncate what gets signed/verified
  return true;
}

TEST(test_time_validity_ok_within_window) {
  CHECK(checkManifestTimeValidity(/*issued*/1000, /*expires*/2000, /*now*/1500, /*skew*/0) == MANIFEST_TIME_OK);
  return true;
}

TEST(test_time_validity_expired) {
  CHECK(checkManifestTimeValidity(1000, 2000, /*now*/2001, /*skew*/0) == MANIFEST_TIME_EXPIRED);
  return true;
}

TEST(test_time_validity_expired_respects_skew) {
  // 1 second past expiry, but within a 60s skew allowance -- OK.
  CHECK(checkManifestTimeValidity(1000, 2000, 2001, /*skew*/60) == MANIFEST_TIME_OK);
  // 61 seconds past expiry, beyond a 60s skew allowance -- expired.
  CHECK(checkManifestTimeValidity(1000, 2000, 2061, /*skew*/60) == MANIFEST_TIME_EXPIRED);
  return true;
}

TEST(test_time_validity_not_yet_valid) {
  CHECK(checkManifestTimeValidity(/*issued*/5000, /*expires*/6000, /*now*/1000, /*skew*/0) == MANIFEST_TIME_NOT_YET_VALID);
  return true;
}

TEST(test_time_validity_not_yet_valid_respects_skew) {
  CHECK(checkManifestTimeValidity(1060, 2000, /*now*/1000, /*skew*/60) == MANIFEST_TIME_OK);
  CHECK(checkManifestTimeValidity(1061, 2000, /*now*/1000, /*skew*/60) == MANIFEST_TIME_NOT_YET_VALID);
  return true;
}

TEST(test_time_validity_exact_boundary_values) {
  // issuedAt == now: valid (not "not yet valid").
  CHECK(checkManifestTimeValidity(1000, 2000, 1000, 0) == MANIFEST_TIME_OK);
  // now == expiresAt exactly: still valid (strictly-greater-than is what expires it).
  CHECK(checkManifestTimeValidity(1000, 2000, 2000, 0) == MANIFEST_TIME_OK);
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
