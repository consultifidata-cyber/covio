// ============================================================================
// test_pulse_plausibility.cpp — Miki Wire hardening (Phase-0 finding F5):
// host tests for pulse_plausibility.h. Same minimal self-contained runner
// convention as the other suites here.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_pulse_plausibility.cpp -o t && ./t
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../pulse_plausibility.h"

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

// Drives one 1s-cadence update with `hz` pulses added over that second.
static bool tick(PulsePlausibilityMonitor& m, uint64_t& total, uint32_t& now, uint32_t hz) {
  total += hz;
  now += 1000;
  return m.update(total, now);
}

TEST(test_disabled_monitor_never_flags_but_tracks_peak) {
  PulsePlausibilityMonitor m(0);   // 0 = disabled (the shipped default)
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);            // init
  for (int i = 0; i < 10; i++) CHECK(!tick(m, total, now, 5000));  // absurd rate
  CHECK(!m.suspect());
  CHECK(m.peakHzObserved() >= 5000);   // evidence for choosing a real ceiling
  return true;
}

TEST(test_normal_rate_never_flags) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  for (int i = 0; i < 100; i++) CHECK(!tick(m, total, now, 40));   // ~40 Hz line
  CHECK(!m.suspect());
  CHECK(m.violationCount() == 0);
  return true;
}

TEST(test_single_spike_does_not_flag) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  tick(m, total, now, 40);
  CHECK(!tick(m, total, now, 3000));   // one EMI burst tick
  CHECK(!tick(m, total, now, 40));     // back to normal
  CHECK(!m.suspect());
  CHECK(m.violationCount() == 1);      // counted, but not sustained -> no flag
  return true;
}

TEST(test_three_consecutive_over_ceiling_flags) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  CHECK(!tick(m, total, now, 900));
  CHECK(!tick(m, total, now, 900));
  CHECK(tick(m, total, now, 900));     // 3rd consecutive -> suspect
  CHECK(m.suspect());
  return true;
}

TEST(test_flag_clears_after_five_normal_cycles) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  for (int i = 0; i < 3; i++) tick(m, total, now, 900);
  CHECK(m.suspect());
  for (int i = 0; i < 4; i++) { tick(m, total, now, 40); CHECK(m.suspect()); }
  CHECK(!tick(m, total, now, 40));     // 5th normal cycle clears
  CHECK(!m.suspect());
  return true;
}

TEST(test_boundary_rate_exactly_at_ceiling_is_plausible) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  for (int i = 0; i < 10; i++) CHECK(!tick(m, total, now, 500));   // == ceiling: OK
  CHECK(!m.suspect());
  return true;
}

TEST(test_reconfigure_resets_streaks_and_disabling_clears_flag) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  for (int i = 0; i < 3; i++) tick(m, total, now, 900);
  CHECK(m.suspect());
  m.setMaxHz(0);                       // operator disables
  CHECK(!m.suspect());                 // stale flag cannot outlive its config
  for (int i = 0; i < 5; i++) CHECK(!tick(m, total, now, 900));
  return true;
}

TEST(test_totalizer_regression_treated_as_zero_delta_not_underflow) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 1000; uint32_t now = 0;
  m.update(total, now);
  tick(m, total, now, 40);
  total = 100;                          // impossible: totalizer went backwards
  now += 1000;
  CHECK(!m.update(total, now));         // must not flag via unsigned underflow
  CHECK(!m.suspect());
  return true;
}

TEST(test_gap_after_blocking_call_computes_rate_over_real_elapsed) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0; uint32_t now = 0;
  m.update(total, now);
  // 8s blocking push: 8s of 400 Hz pulses arrive "at once" on return. Rate
  // over the REAL elapsed window is 400 Hz -- plausible, must not flag.
  total += 3200; now += 8000;
  CHECK(!m.update(total, now));
  CHECK(!m.suspect());
  return true;
}

TEST(test_millis_wrap_safe) {
  PulsePlausibilityMonitor m(500);
  uint64_t total = 0;
  uint32_t now = 0xFFFFFC00u;          // ~1s before millis wrap
  m.update(total, now);
  total += 40; now += 1000;            // wraps past 0
  CHECK(!m.update(total, now));
  CHECK(!m.suspect());
  CHECK(m.peakHzObserved() <= 41);     // rate computed over 1s, not a huge bogus dt
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
