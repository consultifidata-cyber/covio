// ============================================================================
// test_boot_health.cpp — P1 hardening: app-level unhealthy-boot rollback
// safety net. Tests isBootOnTrial() against the ACTUAL boot_health.h
// function covio_firmware.ino's setup() calls. Zero Arduino/NVS dependency.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_boot_health.cpp -o test_boot_health
//   ./test_boot_health
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../../boot_health.h"

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

TEST(test_fresh_device_never_confirmed_is_on_trial) {
  // A never-confirmed device has lastConfirmedVersion == "" (store.h's
  // Preferences::getString default) -- must be on trial, not a special
  // case that skips the safety net.
  CHECK(isBootOnTrial("", 0, "1.0.0", 1) == true);
  return true;
}

TEST(test_matching_version_and_security_version_is_not_on_trial) {
  // The exact "already confirmed, this is an ordinary reboot" case.
  CHECK(isBootOnTrial("1.0.0", 1, "1.0.0", 1) == false);
  return true;
}

TEST(test_version_string_mismatch_is_on_trial) {
  CHECK(isBootOnTrial("1.0.0", 1, "1.0.1", 1) == true);
  return true;
}

TEST(test_security_version_mismatch_is_on_trial) {
  // Version string can be identical while security_version differs (a
  // rebuild that only bumped the anti-downgrade floor) -- must still be
  // treated as on trial, since this exact image has never been confirmed.
  CHECK(isBootOnTrial("1.0.0", 1, "1.0.0", 2) == true);
  return true;
}

TEST(test_both_mismatch_is_on_trial) {
  CHECK(isBootOnTrial("1.0.0", 1, "1.0.1", 2) == true);
  return true;
}

TEST(test_reverting_to_a_previously_confirmed_older_version_is_on_trial) {
  // Rolling back to an older, previously-confirmed version after running a
  // newer one: "last confirmed" now points at the newer version, so
  // re-running the older one is (correctly) on trial again -- this function
  // only ever compares against the SINGLE most recent confirmation, not a
  // history, by design (matches the security floor's own single-value
  // monotonic-forward semantics, not a allow-list of every past version).
  CHECK(isBootOnTrial("1.0.1", 2, "1.0.0", 1) == true);
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
