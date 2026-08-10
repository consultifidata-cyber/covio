// ============================================================================
// test_sensor_stuck.cpp — Balaji V1 freeze remediation (sensor-stuck-at-zero
// detection). Tests SensorStuckDetector (sensor_stuck.h) directly -- zero
// Arduino/ESP-IDF dependency, same pattern as test_ota_version_policy.cpp /
// test_timestamp_parse.cpp.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_sensor_stuck.cpp -o test_sensor_stuck
//   ./test_sensor_stuck
//
// HONEST DISCLOSURE: written and statically reviewed but not locally
// compiled/executed this session -- no host C++ compiler (g++/gcc/clang++/
// cl) is available on this machine (checked directly), matching the same
// disclosed gap test_timestamp_parse.cpp/test_ota_version_policy.cpp
// already state. sensor_stuck.h IS compile-verified indirectly: the full
// firmware (which includes it via covio_firmware.ino) compiles cleanly
// through PlatformIO's xtensa cross-toolchain -- see this remediation's own
// build evidence in the implementation summary. This file is intended to
// be compiled and run by CI (.github/workflows/ci.yml's existing
// firmware-native-fault-injection job), the same way
// test_ota_version_policy.cpp/test_ota_manifest_auth.cpp already are.
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../../sensor_stuck.h"

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

static const uint32_t THRESHOLD_MS = 1000; // small threshold for fast tests

// ---------------------------------------------------------------------------
// 1. First-ever update() call is never "stuck" -- there is no prior
//    observation to compare against yet.
// ---------------------------------------------------------------------------
TEST(test_first_update_is_never_stuck) {
  SensorStuckDetector d(THRESHOLD_MS);
  CHECK(d.update(100, 0) == false);
  return true;
}

// ---------------------------------------------------------------------------
// 2. Pulses changing every cycle -- never flagged stuck, however long it
//    runs.
// ---------------------------------------------------------------------------
TEST(test_continuously_changing_total_never_stuck) {
  SensorStuckDetector d(THRESHOLD_MS);
  CHECK(d.update(0, 0) == false);
  for (uint32_t t = 100; t <= 10000; t += 100) {
    CHECK(d.update(t, t) == false);   // total keeps rising with time
  }
  return true;
}

// ---------------------------------------------------------------------------
// 3. Total stays flat past the threshold -- must report stuck.
// ---------------------------------------------------------------------------
TEST(test_flat_total_past_threshold_is_stuck) {
  SensorStuckDetector d(THRESHOLD_MS);
  CHECK(d.update(500, 0) == false);          // establishes baseline at t=0
  CHECK(d.update(500, 500) == false);        // 500ms flat -- not yet at threshold
  CHECK(d.update(500, 999) == false);        // 999ms flat -- still just under
  CHECK(d.update(500, 1000) == true);        // exactly at threshold -- stuck
  CHECK(d.update(500, 5000) == true);        // stays stuck the longer it's flat
  return true;
}

// ---------------------------------------------------------------------------
// 4. A pulse arriving resets the "stuck" clock -- must stop being stuck
//    immediately, and take a full new threshold period before flagging
//    again.
// ---------------------------------------------------------------------------
TEST(test_pulse_change_clears_stuck_state) {
  SensorStuckDetector d(THRESHOLD_MS);
  d.update(500, 0);
  CHECK(d.update(500, 1000) == true);   // stuck
  CHECK(d.update(501, 1001) == false);  // one pulse arrives -- clears immediately
  CHECK(d.update(501, 1500) == false);  // only 499ms since the change -- not stuck yet
  CHECK(d.update(501, 2001) == true);   // full threshold elapsed again -- stuck once more
  return true;
}

// ---------------------------------------------------------------------------
// 5. isStuck()/msSinceLastChange() are pure reads -- calling them
//    repeatedly must not itself change the detector's state (unlike
//    update()).
// ---------------------------------------------------------------------------
TEST(test_isstuck_and_mssincelastchange_are_read_only) {
  SensorStuckDetector d(THRESHOLD_MS);
  d.update(10, 0);
  CHECK(d.isStuck(500) == false);
  CHECK(d.isStuck(500) == false);   // calling again changes nothing
  CHECK(d.isStuck(1000) == true);
  CHECK(d.isStuck(500) == false);   // going "back in time" still reflects true state, not cached
  CHECK(d.msSinceLastChange(0) == 0);
  CHECK(d.msSinceLastChange(750) == 750);
  return true;
}

// ---------------------------------------------------------------------------
// 6. Before the first update(), isStuck()/msSinceLastChange() must be
//    inert (never fabricate "stuck" before any observation exists).
// ---------------------------------------------------------------------------
TEST(test_uninitialized_detector_is_never_stuck) {
  SensorStuckDetector d(THRESHOLD_MS);
  CHECK(d.isStuck(999999) == false);
  CHECK(d.msSinceLastChange(999999) == 0);
  return true;
}

// ---------------------------------------------------------------------------
// 7. millis()-style wraparound: unsigned subtraction must still produce the
//    correct elapsed duration across a single wrap, matching this
//    codebase's existing `now - tX >= PERIOD` idiom elsewhere.
// ---------------------------------------------------------------------------
TEST(test_millis_wraparound_handled_correctly) {
  SensorStuckDetector d(THRESHOLD_MS);
  uint32_t nearWrap = 0xFFFFFFFFUL - 200;   // 200ms before wraparound
  d.update(42, nearWrap);
  uint32_t afterWrap = 300;                 // wrapped past 0; true elapsed = 200 + 300 = 500ms
  CHECK(d.update(42, afterWrap) == false);  // 500ms flat, still under 1000ms threshold
  uint32_t furtherAfterWrap = 900;          // true elapsed = 200 + 900 = 1100ms
  CHECK(d.update(42, furtherAfterWrap) == true);
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
