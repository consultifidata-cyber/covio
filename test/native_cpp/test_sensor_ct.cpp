// ============================================================================
// test_sensor_ct.cpp — CT-clamp enhancement: tests SensorCt (sensor_ct.h)
// directly -- zero Arduino/ESP-IDF dependency, same pattern as
// test_sensor_stuck.cpp / test_ota_version_policy.cpp.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_sensor_ct.cpp -o test_sensor_ct
//   ./test_sensor_ct
//
// HONEST DISCLOSURE: written and statically reviewed but not locally
// compiled/executed this session -- no host C++ compiler (g++/gcc/clang++/
// cl) is available on this machine, the same disclosed gap
// test_sensor_stuck.cpp already states. sensor_ct.h IS compile-verified
// through PlatformIO's xtensa cross-toolchain (a -DSENSOR_MODE=1 build of
// the full firmware includes it via covio_firmware.ino). This file is
// compiled and run by CI (.github/workflows/ci.yml's existing
// firmware-native-fault-injection job), like the other native_cpp tests.
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../../sensor_ct.h"

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

// Production values (config.h): CT_PULSE_HZ=1, CT_DEBOUNCE_MS=100.
static const uint32_t HZ = 1;
static const uint32_t DEBOUNCE_MS = 100;

// ---------------------------------------------------------------------------
// 1. First-ever service() call returns 0 pulses whatever the level -- there
//    is no prior timestamp to integrate from yet -- but adopts the raw
//    level as the debounced state immediately (equipment already running at
//    boot must not wait out a debounce it has trivially satisfied).
// ---------------------------------------------------------------------------
TEST(test_first_call_returns_zero_and_adopts_level) {
  SensorCt off(HZ, DEBOUNCE_MS);
  CHECK(off.service(false, 0) == 0);
  CHECK(off.active() == false);

  SensorCt on(HZ, DEBOUNCE_MS);
  CHECK(on.service(true, 0) == 0);
  CHECK(on.active() == true);
  return true;
}

// ---------------------------------------------------------------------------
// 2. Level inactive forever -- zero pulses, however long it runs.
// ---------------------------------------------------------------------------
TEST(test_inactive_level_never_produces_pulses) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(false, 0);
  uint32_t total = 0;
  for (uint32_t t = 5; t <= 60000; t += 5) total += d.service(false, t);
  CHECK(total == 0);
  CHECK(d.active() == false);
  return true;
}

// ---------------------------------------------------------------------------
// 3. Steady active level accumulates at exactly CT_PULSE_HZ, with the
//    fractional remainder carried across calls (never dropped, never
//    double-counted).
// ---------------------------------------------------------------------------
TEST(test_steady_active_accumulates_at_pulse_hz_with_carry) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(true, 0);                       // adopt active at t=0
  CHECK(d.service(true, 400) == 0);         // 400ms -- under one pulse period
  CHECK(d.service(true, 900) == 0);         // 900ms cumulative
  CHECK(d.service(true, 1000) == 1);        // exactly 1s -> first pulse
  CHECK(d.service(true, 1999) == 0);        // 999ms of new remainder
  CHECK(d.service(true, 2000) == 1);        // remainder completes -> second pulse
  // 10 more seconds in odd 700ms steps (loop lands on t=11800, final call
  // tops up to exactly t=12000): total must come out exact.
  uint32_t total = 0;
  for (uint32_t t = 2700; t <= 12000; t += 700) total += d.service(true, t);
  total += d.service(true, 12000);
  CHECK(total == 10);                       // t=2000 -> t=12000 is exactly 10s
  return true;
}

// ---------------------------------------------------------------------------
// 4. Blocking-gap catch-up: a single service() call after a long gap (e.g.
//    a multi-second HTTP push) must emit ALL the pulses for the gap at
//    once -- the invariant "pulses are never lost to network activity".
// ---------------------------------------------------------------------------
TEST(test_blocking_gap_catches_up_exactly) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(true, 0);
  CHECK(d.service(true, 5000) == 5);        // 5s gap -> 5 pulses in one call
  CHECK(d.service(true, 5500) == 0);        // remainder logic still intact after
  CHECK(d.service(true, 6000) == 1);
  return true;
}

// ---------------------------------------------------------------------------
// 5. An active glitch shorter than the debounce window must not activate
//    the detector or produce any pulses (CT-switch contact chatter).
// ---------------------------------------------------------------------------
TEST(test_short_active_glitch_is_debounced_away) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(false, 0);
  uint32_t total = 0;
  total += d.service(true, 10);             // glitch starts
  total += d.service(true, 60);             // 50ms in -- still under 100ms
  total += d.service(false, 90);            // gone before the window elapsed
  total += d.service(false, 5000);
  CHECK(total == 0);
  CHECK(d.active() == false);
  return true;
}

// ---------------------------------------------------------------------------
// 6. An inactive dropout shorter than the debounce window, while active,
//    must not deactivate -- and integration continues through it.
// ---------------------------------------------------------------------------
TEST(test_short_inactive_dropout_keeps_counting) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(true, 0);                       // active from boot
  uint32_t total = d.service(true, 2000);   // 2 pulses banked
  total += d.service(false, 2040);          // dropout begins (40ms < 100ms)
  total += d.service(true, 2080);           // recovers before commit
  total += d.service(true, 4000);
  CHECK(d.active() == true);
  CHECK(total == 4);                        // full 4s counted, dropout invisible
  return true;
}

// ---------------------------------------------------------------------------
// 7. Real activation commits only after the debounce window; time observed
//    before the commit call is not integrated (bounded, documented edge
//    behavior -- see sensor_ct.h's HONEST ERROR BOUND note).
// ---------------------------------------------------------------------------
TEST(test_activation_commits_after_debounce_window) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(false, 0);
  CHECK(d.service(true, 50) == 0);          // raw active; debounce clock starts
  CHECK(d.active() == false);
  CHECK(d.service(true, 100) == 0);         // 50ms held -- not committed yet
  CHECK(d.active() == false);
  CHECK(d.service(true, 150) == 0);         // 100ms held -- commits; 50ms banked
  CHECK(d.active() == true);
  CHECK(d.service(true, 1100) == 1);        // 50+950 = 1000ms active -> 1 pulse
  return true;
}

// ---------------------------------------------------------------------------
// 8. Real deactivation: after the inactive level commits, pulses stop and
//    stay stopped.
// ---------------------------------------------------------------------------
TEST(test_deactivation_stops_pulses) {
  SensorCt d(HZ, DEBOUNCE_MS);
  d.service(true, 0);
  CHECK(d.service(true, 3000) == 3);
  d.service(false, 3010);                   // raw drops
  d.service(false, 3110);                   // 100ms held -- commits inactive
  CHECK(d.active() == false);
  uint32_t total = 0;
  for (uint32_t t = 3200; t <= 60000; t += 400) total += d.service(false, t);
  CHECK(total == 0);
  return true;
}

// ---------------------------------------------------------------------------
// 9. millis()-style wraparound: unsigned subtraction must still integrate
//    the correct elapsed time across a wrap, matching the codebase's
//    existing `now - tX >= PERIOD` idiom.
// ---------------------------------------------------------------------------
TEST(test_millis_wraparound_integrates_correctly) {
  SensorCt d(HZ, DEBOUNCE_MS);
  uint32_t nearWrap = 0xFFFFFFFFUL - 200;   // 200ms before wraparound
  d.service(true, nearWrap);
  CHECK(d.service(true, 800) == 1);         // true elapsed = 200+1+800 = 1001ms
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
