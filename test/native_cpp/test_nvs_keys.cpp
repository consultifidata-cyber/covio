// ============================================================================
// test_nvs_keys.cpp — regression guard for the NVS key-name length limit.
//
// Sprint 4B physical bring-up (bench board E8:F6:0A:B8:B7:BC) found the P1
// unhealthy-boot rollback safety net silently dead on hardware:
//   [E][Preferences.cpp:202] putUInt(): nvs_set_u32 fail: unhealthy_streak KEY_TOO_LONG
// ESP-IDF NVS keys are at most 15 characters; that key was 16, every write
// failed, and the streak read 0 on every boot. nvs_keys.h now declares every
// key store.h uses and static_asserts each one; this test compiles that
// header on the host and re-checks the same table at runtime.
//
// Build/run:
//   g++ -std=c++14 -Wall -I../.. test_nvs_keys.cpp -o test_nvs_keys
//   ./test_nvs_keys
// ============================================================================
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../../nvs_keys.h"

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

TEST(test_limit_matches_esp_idf_nvs_key_name_max_size) {
  // nvs.h: NVS_KEY_NAME_MAX_SIZE == 16 including the terminator.
  CHECK(NVS_KEY_MAX_LEN == 15);
  return true;
}

TEST(test_fits_macro_rejects_a_16_char_literal) {
  // The exact shape of the defect: the macro must say NO to the old key.
  CHECK(NVS_KEY_FITS("unhealthy_streak") == false);
  CHECK(strlen("unhealthy_streak") == 16);
  // ...and YES to a 15-char one (boundary) and to the renamed key.
  CHECK(NVS_KEY_FITS("123456789012345") == true);
  CHECK(NVS_KEY_FITS(NVS_KEY_UNHEALTHY_STRK) == true);
  return true;
}

TEST(test_registry_is_non_empty_and_covers_both_namespaces) {
  CHECK(kNvsKeyRegistryCount >= 20);
  bool sawMain = false, sawSec = false;
  for (unsigned i = 0; i < kNvsKeyRegistryCount; i++) {
    if (strcmp(kNvsKeyRegistry[i].ns, NVS_NS) == 0)          sawMain = true;
    if (strcmp(kNvsKeyRegistry[i].ns, NVS_NS_SECURITY) == 0) sawSec  = true;
  }
  CHECK(sawMain);
  CHECK(sawSec);
  return true;
}

TEST(test_every_registered_key_and_namespace_fits) {
  for (unsigned i = 0; i < kNvsKeyRegistryCount; i++) {
    const NvsKeyEntry& e = kNvsKeyRegistry[i];
    size_t kl = strlen(e.key), nl = strlen(e.ns);
    if (kl == 0 || kl > NVS_KEY_MAX_LEN || nl == 0 || nl > NVS_KEY_MAX_LEN) {
      printf("    offending entry: ns=\"%s\" (%u) key=\"%s\" (%u)\n",
             e.ns, (unsigned)nl, e.key, (unsigned)kl);
    }
    CHECK(kl >= 1 && kl <= NVS_KEY_MAX_LEN);
    CHECK(nl >= 1 && nl <= NVS_KEY_MAX_LEN);
  }
  return true;
}

TEST(test_keys_are_unique_within_a_namespace) {
  for (unsigned i = 0; i < kNvsKeyRegistryCount; i++) {
    for (unsigned j = i + 1; j < kNvsKeyRegistryCount; j++) {
      bool sameNs  = strcmp(kNvsKeyRegistry[i].ns,  kNvsKeyRegistry[j].ns)  == 0;
      bool sameKey = strcmp(kNvsKeyRegistry[i].key, kNvsKeyRegistry[j].key) == 0;
      if (sameNs && sameKey) printf("    duplicate: %s/%s\n", kNvsKeyRegistry[i].ns, kNvsKeyRegistry[i].key);
      CHECK(!(sameNs && sameKey));
    }
  }
  return true;
}

TEST(test_unhealthy_streak_key_lives_in_security_namespace) {
  // The streak must survive factoryReset() (store.h's documented reason for
  // putting it beside sec_ver) -- guard the namespace as well as the length.
  bool found = false;
  for (unsigned i = 0; i < kNvsKeyRegistryCount; i++) {
    if (strcmp(kNvsKeyRegistry[i].key, NVS_KEY_UNHEALTHY_STRK) == 0) {
      CHECK(strcmp(kNvsKeyRegistry[i].ns, NVS_NS_SECURITY) == 0);
      found = true;
    }
  }
  CHECK(found);
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
