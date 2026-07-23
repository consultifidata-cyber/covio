// ============================================================================
// test_credential_display.cpp — plant-pilot activation remediation
// (serial-console credential-exposure closure). Tests classifyCredential()
// (credential_display.h) directly -- zero Arduino/mbedtls dependency, same
// pattern as test_ota_version_policy.cpp/test_timestamp_parse.cpp.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_credential_display.cpp -o test_credential_display
//   ./test_credential_display
//
// HONEST DISCLOSURE: written and statically reviewed, NOT locally
// compiled/executed this session -- this machine has no host C++
// compiler (confirmed directly, unchanged since every prior disclosure
// this remediation chain has made). Compile-verified indirectly: this
// header is included by provision.h, and the full firmware (including
// provision.h) compiles cleanly via PlatformIO's xtensa cross-toolchain
// (see this remediation's own build evidence). The fingerprint's own
// SHA-256 computation (a thin mbedtls call left directly in
// provision.h, deliberately not duplicated here) is NOT exercised by
// this file -- proven instead by a live serial "show" read against the
// real device after flashing (see this phase's own report).
// ============================================================================
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../../credential_display.h"

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

static const char* DEFAULT_KEY = "dev-key-change-me";

// ---------------------------------------------------------------------------
// 4. Empty token reports "missing" (not configured).
// ---------------------------------------------------------------------------
TEST(test_empty_key_reports_missing) {
  CHECK(classifyCredential("", DEFAULT_KEY) == CRED_STATUS_MISSING);
  CHECK(classifyCredential(nullptr, DEFAULT_KEY) == CRED_STATUS_MISSING);
  return true;
}

// ---------------------------------------------------------------------------
// Key exactly equal to the compiled-in default reports "default".
// ---------------------------------------------------------------------------
TEST(test_default_key_reports_default) {
  CHECK(classifyCredential(DEFAULT_KEY, DEFAULT_KEY) == CRED_STATUS_DEFAULT);
  return true;
}

// ---------------------------------------------------------------------------
// Any other non-empty value reports "configured".
// ---------------------------------------------------------------------------
TEST(test_real_key_reports_configured) {
  CHECK(classifyCredential("some-real-provisioned-key-value", DEFAULT_KEY) == CRED_STATUS_CONFIGURED);
  return true;
}

// ---------------------------------------------------------------------------
// A key that merely starts with or contains the default string, but isn't
// exactly equal, must NOT be misclassified as "default" -- exact match only.
// ---------------------------------------------------------------------------
TEST(test_near_match_is_not_default) {
  CHECK(classifyCredential("dev-key-change-me-2", DEFAULT_KEY) == CRED_STATUS_CONFIGURED);
  CHECK(classifyCredential("dev-key-change-m", DEFAULT_KEY) == CRED_STATUS_CONFIGURED);
  return true;
}

// ---------------------------------------------------------------------------
// String representations match this project's own existing convention
// (diagnostics.h's apiKeyStatus_()) exactly -- "missing"/"default"/"configured".
// ---------------------------------------------------------------------------
TEST(test_status_strings_match_existing_convention) {
  CHECK(std::string(credentialDisplayStatusStr(CRED_STATUS_MISSING)) == "missing");
  CHECK(std::string(credentialDisplayStatusStr(CRED_STATUS_DEFAULT)) == "default");
  CHECK(std::string(credentialDisplayStatusStr(CRED_STATUS_CONFIGURED)) == "configured");
  return true;
}

// ---------------------------------------------------------------------------
// Malformed configuration (no default key known, e.g. nullptr) never
// crashes and never reports "default" for a non-empty key -- degrades
// safely to "configured" rather than guessing.
// ---------------------------------------------------------------------------
TEST(test_missing_default_key_degrades_safely) {
  CHECK(classifyCredential("anything", nullptr) == CRED_STATUS_CONFIGURED);
  CHECK(classifyCredential("", nullptr) == CRED_STATUS_MISSING);
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
