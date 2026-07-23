// ============================================================================
// test_timestamp_parse.cpp — remediation for the OTA time-source overflow
// defect (34_OTA_SECURITY_HARDWARE_SUITE_RESULT.md). Tests
// parseNonNegativeInt64Checked() (timestamp_parse.h) directly -- zero
// Arduino/ESP-IDF dependency, so this compiles as plain, portable C++,
// same pattern as test_ota_version_policy.cpp/test_ota_manifest_auth.cpp.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_timestamp_parse.cpp -o test_timestamp_parse
//   ./test_timestamp_parse
//
// HONEST DISCLOSURE: this file was written and statically reviewed but
// NOT locally compiled/executed this session -- this machine has no host
// C++ compiler (g++/gcc/clang++/cl all absent; checked directly). It IS
// compile-verified indirectly: sync.h includes timestamp_parse.h and the
// full firmware (which includes sync.h) compiles cleanly via PlatformIO's
// xtensa cross-toolchain (see this remediation's own build evidence) --
// that proves syntactic/type correctness for the ESP32 target, not that
// these specific assertions pass on a host run. Cases 11/12 (missing
// field / truncated JSON) and 15/16 (Sync::haveServerTime() /
// no_time_source end-to-end) require sync.h's Arduino::String-dependent
// wrapper or full device behavior respectively -- NOT exercised by this
// file at all; see this remediation's report for how those are instead
// covered (PlatformIO compile + the actual hardware OTA re-run).
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../../timestamp_parse.h"

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

// Helper: parse a whole C-string as [0, strlen) -- most cases below pass
// an entire literal digit string, matching how sync.h's extractInt64_()
// hands over the digit range it already located.
static bool parseAll(const char* s, int64_t* out) {
  return parseNonNegativeInt64Checked(s, 0, (int)strlen(s), out);
}

// ---------------------------------------------------------------------------
// 1. Current real 13-digit Unix millisecond timestamp (the exact class of
//    value that overflowed the old 32-bit `long` accumulator).
// ---------------------------------------------------------------------------
TEST(test_current_valid_13_digit_ms_timestamp) {
  int64_t v = -1;
  CHECK(parseAll("1784816796695", &v));
  CHECK(v == 1784816796695LL);
  return true;
}

// ---------------------------------------------------------------------------
// 2. Timestamp 0.
// ---------------------------------------------------------------------------
TEST(test_zero_timestamp) {
  int64_t v = -1;
  CHECK(parseAll("0", &v));
  CHECK(v == 0);
  return true;
}

// ---------------------------------------------------------------------------
// 3. Small valid timestamp (well within the OLD 32-bit range too -- must
//    keep working exactly as before this fix).
// ---------------------------------------------------------------------------
TEST(test_small_valid_timestamp) {
  int64_t v = -1;
  CHECK(parseAll("12345", &v));
  CHECK(v == 12345);
  return true;
}

// ---------------------------------------------------------------------------
// 4. Maximum supported valid timestamp -- exactly INT64_MAX.
// ---------------------------------------------------------------------------
TEST(test_maximum_supported_value_int64_max) {
  int64_t v = -1;
  CHECK(parseAll("9223372036854775807", &v));   // INT64_MAX
  CHECK(v == INT64_MAX);
  return true;
}

// ---------------------------------------------------------------------------
// 5. One greater than the supported maximum -- must fail closed, never wrap.
// ---------------------------------------------------------------------------
TEST(test_one_greater_than_maximum_rejected) {
  int64_t v = 12345;   // sentinel -- must remain untouched on failure
  CHECK(!parseAll("9223372036854775808", &v));  // INT64_MAX + 1
  CHECK(v == 12345);   // untouched, not silently wrapped/corrupted
  return true;
}

// ---------------------------------------------------------------------------
// 6. Extremely long numeric field (far beyond int64_t's ~19 digits) --
//    must fail closed well before any wraparound, not crash/read OOB.
// ---------------------------------------------------------------------------
TEST(test_extremely_long_numeric_field_rejected) {
  std::string huge(50, '9');   // 50 nines
  int64_t v = 777;
  CHECK(!parseAll(huge.c_str(), &v));
  CHECK(v == 777);
  return true;
}

// ---------------------------------------------------------------------------
// 7. Negative timestamp -- '-' is not a digit; the located range in
//    practice (via sync.h's extractInt64_ wrapper) would never include a
//    leading '-' at all (its digit-scan simply never starts), but proven
//    directly here too: any non-digit byte anywhere in the range rejects.
// ---------------------------------------------------------------------------
TEST(test_negative_sign_content_rejected) {
  int64_t v = -1;
  CHECK(!parseAll("-123", &v));
  return true;
}

// ---------------------------------------------------------------------------
// 8. Decimal timestamp -- '.' is not a digit, rejected.
// ---------------------------------------------------------------------------
TEST(test_decimal_content_rejected) {
  int64_t v = -1;
  CHECK(!parseAll("123.456", &v));
  return true;
}

// ---------------------------------------------------------------------------
// 9. Alphabetic content -- rejected.
// ---------------------------------------------------------------------------
TEST(test_alphabetic_content_rejected) {
  int64_t v = -1;
  CHECK(!parseAll("abc", &v));
  return true;
}

// ---------------------------------------------------------------------------
// 10. Empty value -- start >= end, rejected outright.
// ---------------------------------------------------------------------------
TEST(test_empty_value_rejected) {
  int64_t v = -1;
  CHECK(!parseNonNegativeInt64Checked("", 0, 0, &v));
  return true;
}

// ---------------------------------------------------------------------------
// 13. The exact real timestamp captured during the failed hardware test
//     (doc 34) that demonstrated the original defect -- proves the fix
//     handles the SPECIFIC value that broke the old parser, not just a
//     synthetic one.
// ---------------------------------------------------------------------------
TEST(test_exact_value_from_failed_hardware_test) {
  int64_t v = -1;
  CHECK(parseAll("1784813824816", &v));
  CHECK(v == 1784813824816LL);
  return true;
}

// ---------------------------------------------------------------------------
// 14. Value survives round-trip unchanged through the parsing path for
//     several representative real-world magnitudes -- no truncation, no
//     off-by-one, no corruption anywhere in the accumulation loop.
// ---------------------------------------------------------------------------
TEST(test_value_unchanged_through_parse_for_several_real_magnitudes) {
  struct Case { const char* digits; int64_t expect; };
  Case cases[] = {
    {"1", 1}, {"99", 99}, {"1000", 1000},
    {"1700000000000", 1700000000000LL},   // ~2023-ish epoch ms
    {"1784816796695", 1784816796695LL},   // this session's real capture
    {"2000000000000", 2000000000000LL},   // ~2033-ish epoch ms, still fine at int64
  };
  for (const auto& c : cases) {
    int64_t v = -999;
    CHECK(parseAll(c.digits, &v));
    CHECK(v == c.expect);
  }
  return true;
}

int main() {
  int passed = 0;
  for (auto& t : g_tests) {
    printf("RUN  %s\n", t.first.c_str());
    bool ok = t.second();
    printf("%s %s\n", ok ? "PASS" : "FAIL", t.first.c_str());
    if (ok) passed++;
  }
  printf("\n%d/%zu tests passed (%d assertion failures)\n",
         passed, g_tests.size(), g_assertFailures);
  return (passed == (int)g_tests.size()) ? 0 : 1;
}
