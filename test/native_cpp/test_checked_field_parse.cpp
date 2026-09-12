// ============================================================================
// test_checked_field_parse.cpp — host tests for checked_field_parse.h's
// parseUint32Checked(), the fix for sync.h's old unchecked extractLong_()
// (ack_seq / K-factor "version" parsing). See checked_field_parse.h's own
// header comment for the full rationale.
//
// The OLD behavior is reproduced verbatim below (oldUncheckedExtractLong_)
// -- a byte-for-byte copy of sync.h's former accumulator loop -- purely so
// this test can PROVE, not assert, that it actually misbehaves on inputs
// the new checked function handles correctly. It is a fixed historical
// reference, not production code, and is never called by the firmware.
//
// Build/run: g++ -std=c++14 -Wall -I../.. test_checked_field_parse.cpp -o t && ./t
// ============================================================================
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../../checked_field_parse.h"

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

// Historical reproduction of sync.h's old extractLong_() digit accumulator
// -- the field-location scan is irrelevant to the defect, so this isolates
// just the vulnerable part. Accumulates in uint32_t rather than the real
// (signed) `long` the old code used: unsigned overflow is well-defined
// modular arithmetic in C++, so this reproduces the identical wrong VALUE
// the old 32-bit accumulator would land on (two's-complement wraparound is
// what any real toolchain actually emits for signed overflow too) without
// invoking undefined behaviour itself -- host sanitizers correctly refuse
// to let this test reproduce true signed-overflow UB deterministically,
// which is itself a second, independent proof that the old code's exact
// construct (`long v; v = v*10+digit;`, no check) was never safe to begin
// with.
static bool oldUncheckedExtractLong_(const char* s, int start, int end, uint32_t* out) {
  uint32_t v = 0;
  bool any = false;
  for (int i = start; i < end; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (uint32_t)(s[i] - '0');   // the defect: no overflow check, ever
    any = true;
  }
  if (!any) return false;
  *out = v;
  return true;
}

// ---- the historical defect, proven, not asserted ----

TEST(test_old_unchecked_loop_silently_wraps_a_value_above_uint32_max) {
  // 4,294,967,297 (2^32 + 1) doesn't fit in uint32_t at all. The old loop
  // still "succeeds" with a silently wrapped value (1) instead of failing.
  const char* s = "4294967297";
  uint32_t oldResult = 999;
  CHECK(oldUncheckedExtractLong_(s, 0, (int)strlen(s), &oldResult));
  CHECK(oldResult == 1);   // PROVEN: the old code returns 1, not an error, for this input

  uint32_t newResult = 0xDEADBEEF;
  CHECK(!parseUint32Checked(s, 0, (int)strlen(s), &newResult));   // PROVEN: new code rejects it
  CHECK(newResult == 0xDEADBEEF);   // and leaves *out untouched, per contract
  return true;
}

TEST(test_old_unchecked_loop_on_a_legitimate_uint32_value_the_real_signed_long_would_get_wrong) {
  // 3,000,000,000 is a perfectly legitimate ack_seq/version value (fits
  // easily in uint32_t, the type both fields are actually used as) but
  // exceeds INT32_MAX (2,147,483,647) -- the real production code's actual
  // accumulator type, a signed 32-bit `long`, cannot represent this value
  // at all; reinterpreting this test's well-defined uint32_t result as
  // that signed type is exactly the negative value the firmware's old
  // "ver >= 0" / "ackSeq < 0" guards would have (by luck) caught -- but by
  // relying on unspecified signed-overflow reinterpretation, not by design.
  const char* s = "3000000000";
  uint32_t oldResult = 0;
  CHECK(oldUncheckedExtractLong_(s, 0, (int)strlen(s), &oldResult));
  int32_t reinterpretedAsLong;
  memcpy(&reinterpretedAsLong, &oldResult, sizeof(reinterpretedAsLong));
  CHECK(reinterpretedAsLong < 0);   // PROVEN: a legitimate value reads as negative

  // The new checked function accepts it correctly, as the legitimate
  // uint32_t value it actually is.
  uint32_t newResult = 0;
  CHECK(parseUint32Checked(s, 0, (int)strlen(s), &newResult));
  CHECK(newResult == 3000000000u);
  return true;
}

// ---- new function's own boundary behavior ----

TEST(test_zero_is_valid) {
  uint32_t out = 123;
  CHECK(parseUint32Checked("0", 0, 1, &out));
  CHECK(out == 0);
  return true;
}

TEST(test_small_ordinary_value) {
  uint32_t out = 0;
  const char* s = "42";
  CHECK(parseUint32Checked(s, 0, 2, &out));
  CHECK(out == 42u);
  return true;
}

TEST(test_exactly_uint32_max_is_valid) {
  const char* s = "4294967295";
  uint32_t out = 0;
  CHECK(parseUint32Checked(s, 0, (int)strlen(s), &out));
  CHECK(out == 4294967295u);
  return true;
}

TEST(test_uint32_max_plus_one_is_rejected) {
  const char* s = "4294967296";
  uint32_t out = 0;
  CHECK(!parseUint32Checked(s, 0, (int)strlen(s), &out));
  return true;
}

TEST(test_empty_range_rejected) {
  uint32_t out = 0;
  CHECK(!parseUint32Checked("123", 1, 1, &out));
  return true;
}

TEST(test_non_digit_content_rejected) {
  uint32_t out = 0;
  CHECK(!parseUint32Checked("12a4", 0, 4, &out));
  return true;
}

TEST(test_int64_overflow_also_rejected_not_just_uint32_ceiling) {
  // Confirms the delegation to parseNonNegativeInt64Checked() actually
  // happens -- a value far beyond even int64_t must not somehow "succeed".
  const char* s = "99999999999999999999999999";
  uint32_t out = 0;
  CHECK(!parseUint32Checked(s, 0, (int)strlen(s), &out));
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
