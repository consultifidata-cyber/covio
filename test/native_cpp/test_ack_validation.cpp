// ============================================================================
// test_ack_validation.cpp — Miki Wire hardening (Phase-0 finding F8):
// host tests for the pure ack-bounding / seq-resume-floor logic in
// ack_validation.h. Same minimal self-contained runner convention as
// test_queue_fault_injection.cpp (no new test-framework dependency).
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_ack_validation.cpp -o test_ack_validation
//   ./test_ack_validation
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../ack_validation.h"

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

// ---- boundAckSeq ----------------------------------------------------------

TEST(test_normal_ack_within_bound_passes_through_unclamped) {
  bool clamped = true;   // pre-set opposite to prove it is written
  CHECK(boundAckSeq(50, 100, &clamped) == 50);
  CHECK(!clamped);
  return true;
}

TEST(test_ack_equal_to_max_sent_is_valid_not_clamped) {
  bool clamped = true;
  CHECK(boundAckSeq(100, 100, &clamped) == 100);
  CHECK(!clamped);
  return true;
}

TEST(test_oversized_ack_clamps_to_max_sent) {
  bool clamped = false;
  CHECK(boundAckSeq(1000000, 100, &clamped) == 100);
  CHECK(clamped);
  return true;
}

TEST(test_extreme_oversized_ack_uint32_max_clamps) {
  bool clamped = false;
  CHECK(boundAckSeq(0xFFFFFFFFu, 7, &clamped) == 7);
  CHECK(clamped);
  return true;
}

TEST(test_zero_ack_passes_through) {
  bool clamped = true;
  CHECK(boundAckSeq(0, 100, &clamped) == 0);
  CHECK(!clamped);
  return true;
}

TEST(test_null_clamped_pointer_is_tolerated) {
  CHECK(boundAckSeq(500, 100, nullptr) == 100);   // must not crash
  CHECK(boundAckSeq(50, 100, nullptr) == 50);
  return true;
}

// ---- resumeSeqFloor -------------------------------------------------------

TEST(test_healthy_boot_checkpoint_ahead_of_ack_uses_checkpoint) {
  CHECK(resumeSeqFloor(1000, 900) == 1000);
  return true;
}

TEST(test_checkpoint_equal_to_ack_uses_checkpoint) {
  CHECK(resumeSeqFloor(900, 900) == 900);
  return true;
}

TEST(test_checkpoint_regression_resumes_from_ack_watermark) {
  // Both checkpoint slots corrupt -> lastSeq recovered as 0, but the ack
  // record survived with 66137 (the live Balaji watermark magnitude). The
  // deadlock case this floor exists for.
  CHECK(resumeSeqFloor(0, 66137) == 66137);
  return true;
}

TEST(test_fresh_device_both_zero) {
  CHECK(resumeSeqFloor(0, 0) == 0);
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
