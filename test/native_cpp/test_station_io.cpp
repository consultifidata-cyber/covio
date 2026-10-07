// test_station_io.cpp -- the output governor's own fence, whatever it is asked.
//
//   g++ -std=c++14 -O1 -Wall -I. -I../.. test_station_io.cpp -o io && ./io
#include <stdio.h>

#include "station_io.h"

using namespace station;

static int g_fail = 0;
#define EXPECT(cond, msg)                         \
  do {                                            \
    if (!(cond)) {                                \
      printf("FAIL %s (line %d)\n", msg, __LINE__); \
      g_fail++;                                   \
    }                                             \
  } while (0)

int main() {
  // Off until the first refresh.
  {
    OutputGovernor g;
    g.begin(1000);
    OutputState s = g.failsafe(1500);
    EXPECT(s.bits() == 0, "all off before the first refresh");
  }
  // Green is green; red blinks at 1 Hz.
  {
    OutputGovernor g;
    g.begin(0);
    EXPECT(g.refresh(L_GREEN, false, 100).green, "green");
    OutputState a = g.refresh(L_RED_BLINK, false, 1000);
    OutputState b = g.refresh(L_RED_BLINK, false, 1500);
    EXPECT(a.red != b.red, "red blinks");
    EXPECT(!a.green && !a.amber, "red blink lights nothing else");
  }
  // A logic that asks for sound for ever gets 10 s, then a rest.
  {
    OutputGovernor g;
    g.begin(0);
    uint32_t onMs = 0, longest = 0, run = 0;
    for (uint32_t t = 0; t <= 30000; t += 50) {
      OutputState s = g.refresh(L_RED_BLINK, true, t);
      if (s.sounder) {
        onMs += 50;
        run += 50;
        if (run > longest) longest = run;
      } else {
        run = 0;
      }
    }
    EXPECT(longest <= CAP_BURST_S * 1000UL, "no stretch longer than 10 s");
    EXPECT(g.faultLatched(), "the cut is reported");
    EXPECT(onMs > 0, "it did sound");
  }
  // And never more than 60 s in a ten-minute window, however the bursts are arranged.
  {
    OutputGovernor g;
    g.begin(0);
    uint32_t onMs = 0;
    for (uint32_t t = 0; t < 10UL * 60UL * 1000UL; t += 50) {
      bool want = (t % 5000) < 4000;  // 4 s on, 1 s off, all ten minutes
      if (g.refresh(L_RED_BLINK, want, t).sounder) onMs += 50;
    }
    EXPECT(onMs <= 60000UL + 50, "at most 60 s of sound in a ten-minute window");
  }
  // A task that stops refreshing: everything off within the heartbeat.
  {
    OutputGovernor g;
    g.begin(0);
    g.refresh(L_RED_BLINK, true, 1000);
    OutputState hung = g.failsafe(1000 + HEARTBEAT_MS + 1);
    EXPECT(hung.bits() == 0, "a hung task leaves nothing on");
    OutputState still = g.failsafe(60000);
    EXPECT(!still.sounder, "and it stays off");
  }
  // Mode off is dark.
  {
    OutputGovernor g;
    g.begin(0);
    EXPECT(g.refresh(L_OFF, false, 10).bits() == 0, "mode off: dark");
  }
  // A governor restarted (reboot, watchdog reset) starts dark, whatever the
  // previous one was doing.
  {
    OutputGovernor g;
    g.begin(0);
    g.refresh(L_RED_BLINK, true, 1000);
    OutputGovernor after;
    after.begin(1000);
    EXPECT(after.failsafe(1001).bits() == 0, "a restarted governor is dark");
  }

  // TickClock: one tick per uptime second, in order, never two for one second;
  // a tap is served at once (at most one second ahead), never waits a second.
  {
    TickClock c;
    c.begin(100);
    uint32_t s = 0;
    EXPECT(!c.due(100, false, &s), "no tick inside the same second");
    EXPECT(c.due(101, false, &s) && s == 101, "the boundary ticks");
    EXPECT(c.due(101, true, &s) && s == 102, "a tap takes the next second's tick at once");
    EXPECT(!c.due(101, true, &s), "a second tap in that second waits");
    EXPECT(!c.due(102, false, &s), "the early tick is not repeated at its boundary");
    EXPECT(c.due(103, false, &s) && s == 103, "then ticks resume in order");
    EXPECT(c.due(110, false, &s) && s == 110, "a late task ticks the current second");
  }
  // Tap-to-tick latency across a whole second of tap times: never a wait for
  // the boundary (the task polls every STATION_TASK_PERIOD_MS = 20 ms).
  {
    uint32_t worstMs = 0;
    for (uint32_t tapMs = 0; tapMs < 1000; tapMs += 10) {
      TickClock c;
      c.begin(0);
      uint32_t s = 0;
      uint32_t firstPollMs = ((tapMs + 19) / 20) * 20;  // the next 20 ms poll
      bool served = c.due(firstPollMs / 1000, true, &s);
      uint32_t waited = served ? firstPollMs - tapMs : 1000;
      if (waited > worstMs) worstMs = waited;
    }
    EXPECT(worstMs <= 20, "a tap is ticked within one poll period");
    printf("     tap -> tick: worst %u ms in the task (target < 500 ms tap-to-beep; the reader's "
           "own read time is BENCH REQUIRED)\n",
           worstMs);
  }

  printf("%s station_io: %d failure(s)\n", g_fail ? "FAIL" : "ok  ", g_fail);
  return g_fail ? 1 : 0;
}
