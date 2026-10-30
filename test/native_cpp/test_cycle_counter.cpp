// ============================================================================
// test_cycle_counter.cpp — CycleCounter (cycle_counter.h), Covio D-315.
//
// Build/run (Docker, no host compiler on the Windows laptop):
//   docker run --rm -v "$PWD":/w -w /w/test/native_cpp gcc:13 sh -c
//     "g++ -std=c++14 -Wall -Wextra -I../.. test_cycle_counter.cpp -o /tmp/t && /tmp/t"
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cycle_counter.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
      failures++;                                                       \
    }                                                                   \
  } while (0)

// A signal as (level, milliseconds) steps, sampled every 1 ms like the firmware.
struct Step {
  bool level;
  uint32_t ms;
};

static uint32_t run(CycleCounter& c, const std::vector<Step>& steps, uint32_t& t) {
  uint32_t counted = 0;
  for (const Step& s : steps)
    for (uint32_t i = 0; i < s.ms; i++, t++)
      if (c.sample(s.level, t)) counted++;
  return counted;
}

static CycleSettings press() {
  CycleSettings s;
  s.onStableMs = 30;
  s.offStableMs = 300;
  s.minGapMs = 1500;
  return s;
}

static CycleSettings wire() {
  CycleSettings s;
  s.onStableMs = 0;
  s.offStableMs = 0;
  s.minGapMs = 100;
  return s;
}

/** n flickers: `onMs` high then `offMs` low, repeated. */
static std::vector<Step> flicker(int n, uint32_t onMs, uint32_t offMs) {
  std::vector<Step> v;
  for (int i = 0; i < n; i++) {
    v.push_back({true, onMs});
    v.push_back({false, offMs});
  }
  return v;
}

static void cleanStroke() {
  CycleCounter c;
  c.configure(press());
  uint32_t t = 1000;
  // Ram away, then at the sensor for 600 ms, then away.
  uint32_t n = run(c, {{false, 2000}, {true, 600}, {false, 2500}}, t);
  CHECK(n == 1);
  CHECK(c.counted() == 1);
  CHECK(c.rawRises() == 1);
}

static void flickerStormAtArrival() {
  // 348 counts in one reading at Pravat: the ram trembling at the edge of range.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 2000}};
  for (const Step& f : flicker(170, 2, 3)) s.push_back(f);
  s.push_back({true, 500});
  s.push_back({false, 3000});
  uint32_t n = run(c, s, t);
  CHECK(n == 1);
  CHECK(c.rawRises() >= 170);
}

static void flickerWhileParkedIsNotACycle() {
  // At the sensor, with 50 short drop-outs: never off long enough to re-arm.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 2000}, {true, 200}};
  for (int i = 0; i < 50; i++) {
    s.push_back({false, 5});
    s.push_back({true, 20});
  }
  s.push_back({false, 2000});
  CHECK(run(c, s, t) == 1);
}

static void downAndUpBounceIsOneStroke() {
  // The ram leaves, a 100 ms bounce, back at the sensor: one stroke.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  CHECK(run(c, {{false, 2000}, {true, 400}, {false, 100}, {true, 400}, {false, 3000}}, t) == 1);
}

static void secondHighInsideTheGapIsRefused() {
  // Off long enough to re-arm (400 ms) but only 1.0 s after the count.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  CHECK(run(c, {{false, 2000}, {true, 300}, {false, 400}, {true, 300}, {false, 3000}}, t) == 1);
  CHECK(c.tooSoon() == 1);
}

static void realStrokesAreAllCounted() {
  // 50 strokes, one every 3 s: 500 ms at the sensor, 2.5 s away.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 2000}};
  for (int i = 0; i < 50; i++) {
    s.push_back({true, 500});
    s.push_back({false, 2500});
  }
  CHECK(run(c, s, t) == 50);
  CHECK(c.tooSoon() == 0);
}

static void fiftyStrokesWithChatterOnEvery() {
  // The acceptance test: 50 strokes, each arriving with a burst of chatter.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 2000}};
  for (int i = 0; i < 50; i++) {
    for (const Step& f : flicker(8, 3, 4)) s.push_back(f);
    s.push_back({true, 450});
    for (const Step& f : flicker(5, 2, 6)) s.push_back(f);
    s.push_back({false, 2400});
  }
  CHECK(run(c, s, t) == 50);
}

static void wireLineCountsEveryRealPulse() {
  // ~6 pulses a second, 80 ms high / 87 ms low: all counted.
  CycleCounter c;
  c.configure(wire());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 500}};
  for (int i = 0; i < 360; i++) {
    s.push_back({true, 80});
    s.push_back({false, 87});
  }
  CHECK(run(c, s, t) == 360);
}

static void wireLineBurstIsCappedAtTenPerSecond() {
  // A 1-second burst at 250 Hz (2 ms high, 2 ms low) yields at most 10.
  CycleCounter c;
  c.configure(wire());
  uint32_t t = 0;
  std::vector<Step> s = {{false, 500}};
  for (const Step& f : flicker(250, 2, 2)) s.push_back(f);
  s.push_back({false, 500});
  uint32_t n = run(c, s, t);
  CHECK(n >= 9 && n <= 10);
  CHECK(c.rawRises() == 250);
}

static void millisWrapIsSafe() {
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0xFFFFFFFFu - 2500u;
  CHECK(run(c, {{false, 1000}, {true, 500}, {false, 2500}, {true, 500}, {false, 2000}}, t) == 2);
}

static void startingAtTheSensorDoesNotCount() {
  // Booting with the ram already parked at the sensor is not a stroke.
  CycleCounter c;
  c.configure(press());
  uint32_t t = 0;
  CHECK(run(c, {{true, 2000}, {false, 2000}}, t) == 0);
}

static void disabledSettingsReportDisabled() {
  CycleSettings off;
  CHECK(!off.enabled());
  CHECK(press().enabled());
  CHECK(wire().enabled());
}

int main() {
  cleanStroke();
  flickerStormAtArrival();
  flickerWhileParkedIsNotACycle();
  downAndUpBounceIsOneStroke();
  secondHighInsideTheGapIsRefused();
  realStrokesAreAllCounted();
  fiftyStrokesWithChatterOnEvery();
  wireLineCountsEveryRealPulse();
  wireLineBurstIsCappedAtTenPerSecond();
  millisWrapIsSafe();
  startingAtTheSensorDoesNotCount();
  disabledSettingsReportDisabled();
  if (failures) {
    std::printf("%d FAILED\n", failures);
    return 1;
  }
  std::printf("ALL PASS (12 cases)\n");
  return 0;
}
