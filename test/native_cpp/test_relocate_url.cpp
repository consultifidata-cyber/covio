// test_relocate_url.cpp — isAllowedRelocateUrl (relocate_url.h), Covio D-315.
// Build/run: g++ -std=c++14 -Wall -I../.. test_relocate_url.cpp -o t && ./t
#include <cstdio>

#include "relocate_url.h"

static int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
      failures++;                                                  \
    }                                                              \
  } while (0)

int main() {
  CHECK(isAllowedRelocateUrl("https://covio.in/fleet/miki-wire"));
  CHECK(isAllowedRelocateUrl("https://covio.in/fleet/pravat-fabricators"));
  CHECK(isAllowedRelocateUrl("https://covio.in/fleet/a1"));

  CHECK(!isAllowedRelocateUrl(nullptr));
  CHECK(!isAllowedRelocateUrl(""));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/"));
  CHECK(!isAllowedRelocateUrl("http://covio.in/fleet/miki-wire"));
  CHECK(!isAllowedRelocateUrl("https://evil.example/fleet/miki-wire"));
  CHECK(!isAllowedRelocateUrl("https://covio.in.evil.example/fleet/miki-wire"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/miki-wire/"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/miki-wire?x=1"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/Miki-Wire"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/-miki"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/miki-"));
  CHECK(!isAllowedRelocateUrl("https://covio.in/fleet/../api"));
  CHECK(!isAllowedRelocateUrl(
      "https://covio.in/fleet/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

  if (failures) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("ALL PASS (16 cases)\n");
  return 0;
}
