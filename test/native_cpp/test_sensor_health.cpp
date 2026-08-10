// ============================================================================
// test_sensor_health.cpp — Miki Wire hardening (Phase-0 finding F6):
// host tests for sensor_health.h. Same minimal self-contained runner
// convention as the other suites here.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_sensor_health.cpp -o t && ./t
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../sensor_health.h"

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

TEST(test_first_update_is_starting_never_suspect) {
  SensorHealthMonitor m(3600 * 1000u);
  CHECK(m.update(0, 0) == SENSOR_HEALTH_STARTING);   // machine off at boot: no claim
  return true;
}

TEST(test_pulses_move_to_active) {
  SensorHealthMonitor m(3600 * 1000u);
  m.update(100, 0);
  CHECK(m.update(105, 1000) == SENSOR_HEALTH_ACTIVE);
  return true;
}

TEST(test_short_gap_stays_active_then_idle) {
  SensorHealthMonitor m(3600 * 1000u);
  m.update(100, 0);
  m.update(105, 1000);                                     // ACTIVE
  CHECK(m.update(105, 5000)  == SENSOR_HEALTH_ACTIVE);     // 4s gap: still "recent"
  CHECK(m.update(105, 12000) == SENSOR_HEALTH_IDLE);       // 11s gap: idle
  return true;
}

TEST(test_idle_is_normal_below_threshold_no_matter_how_long_active_hold_passed) {
  SensorHealthMonitor m(3600 * 1000u);                     // 1h plausible idle
  m.update(100, 0);
  m.update(105, 1000);
  CHECK(m.update(105, 30 * 60 * 1000u) == SENSOR_HEALTH_IDLE);   // 30min: normal stop
  return true;
}

TEST(test_gap_beyond_threshold_is_suspect) {
  SensorHealthMonitor m(3600 * 1000u);
  m.update(100, 0);
  m.update(105, 1000);
  CHECK(m.update(105, 3600 * 1000u + 2000) == SENSOR_HEALTH_SUSPECT);
  return true;
}

TEST(test_any_pulse_recovers_from_suspect_immediately) {
  SensorHealthMonitor m(3600 * 1000u);
  m.update(100, 0);
  m.update(105, 1000);
  m.update(105, 3600 * 1000u + 2000);                      // SUSPECT
  CHECK(m.update(106, 3600 * 1000u + 3000) == SENSOR_HEALTH_ACTIVE);
  return true;
}

TEST(test_threshold_zero_disables_suspect_entirely) {
  SensorHealthMonitor m(0);                                // shipped default: off
  m.update(100, 0);
  m.update(105, 1000);
  // Days of no pulses: worst case must still only be IDLE, never SUSPECT.
  CHECK(m.update(105, 48u * 3600u * 1000u) == SENSOR_HEALTH_IDLE);
  return true;
}

TEST(test_startup_with_machine_off_counts_gap_from_boot_not_instant) {
  SensorHealthMonitor m(60 * 1000u);                       // 1min threshold
  m.update(0, 0);                                          // boot, machine off
  CHECK(m.update(0, 30 * 1000u) != SENSOR_HEALTH_SUSPECT); // 30s: not yet
  CHECK(m.update(0, 61 * 1000u) == SENSOR_HEALTH_SUSPECT); // 61s of silence: now yes
  return true;
}

TEST(test_startup_with_machine_running_goes_active) {
  SensorHealthMonitor m(3600 * 1000u);
  m.update(500, 0);
  CHECK(m.update(510, 1000) == SENSOR_HEALTH_ACTIVE);      // counting from tick 1
  return true;
}

TEST(test_reconfigure_does_not_reset_gap_clock) {
  SensorHealthMonitor m(0);                                // starts disabled
  m.update(100, 0);
  m.update(105, 1000);
  m.update(105, 10 * 60 * 1000u);                          // 10min silent, IDLE (off)
  m.setSuspectThresholdMs(5 * 60 * 1000u);                 // operator arms 5min threshold
  // The 10min gap already on the clock must count -- arming must not mask it.
  CHECK(m.update(105, 10 * 60 * 1000u + 1000) == SENSOR_HEALTH_SUSPECT);
  return true;
}

TEST(test_millis_wrap_safe_gap) {
  SensorHealthMonitor m(3600 * 1000u);
  uint32_t now = 0xFFFFF000u;                              // near wrap
  m.update(100, now);
  m.update(105, now + 1000);                               // ACTIVE, wraps soon
  CHECK(m.update(105, now + 20000) == SENSOR_HEALTH_IDLE); // gap spans the wrap
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
