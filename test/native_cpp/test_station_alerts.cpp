// test_station_alerts.cpp -- the false-hooter matrix, on the firmware's own code.
//
// S6 §16, §17, §21. Every condition in the brief, run through station_logic.h
// (the C++ the board runs, parity-proven against the platform) and the output
// governor (station_io.h), recording for each what the station does:
//
//   LIGHT  every light state shown during the stop
//   SOUND  seconds of sound the logic asked for, and the pins actually gave
//   EVENTS the station events emitted (codes)
//
// and asserting the audible gate (Part 2 §K): sound only for an armed machine,
// stopped beyond its allowance, unacknowledged, not quiet, mode "light and
// sound", no input or output fault, and a known clock when quiet windows exist.
// Then: a stop left offline for 24 hours sounds a finite, small amount and then
// never again; and one physical presentation of a card is one business event.
//
//   g++ -std=c++11 -O2 -Wall -Wextra -I. -I../.. test_station_alerts.cpp -o alerts && ./alerts
#include <stdio.h>
#include <string.h>

#include <set>
#include <string>
#include <vector>

#include "station_io.h"
#include "station_logic.h"

using namespace station;

static int g_fail = 0;
#define CHECK(cond, ...)                    \
  do {                                      \
    if (!(cond)) {                          \
      printf("  FAIL line %d: ", __LINE__); \
      printf(__VA_ARGS__);                  \
      printf("\n");                         \
      g_fail++;                             \
    }                                       \
  } while (0)

// T=180 s allowance, TS=600 s spool allowance, sound 20 s after red, repeat
// every 180 s, 3 announcements. Cards: an action, a person, a quiet card (30
// min). A lunch window 12:30-13:00 local (TZ +330).
static std::string config(int mode, bool window, int spoolPulses = 0) {
  char buf[600];
  snprintf(buf, sizeof(buf),
           "COVIO-STATION 1\nVER=3\nMODE=%d\nT=180\nTS=600\nSPOOL=%d\nC=20\nR=180\nN=3\n"
           "Q=1800\nRECOVER=300\nCORRECT=120\nTZ=330\n%sK=04AAAA01,A,1\nK=04AAAA02,A,2\n"
           "K=04BBBB01,P,0\nK=04CCCC01,Q,1800\nEND=4\n",
           mode, spoolPulses, window ? "W=12:30-13:00\n" : "");
  return buf;
}

static Config g_live, g_scratch;

// What one scenario saw.
struct Seen {
  std::set<int> lights;
  int logicSoundS = 0;    // seconds the logic asked for sound
  int pinSoundMs = 0;     // milliseconds the governor actually drove the sounder
  int alerts = 0;
  int sounds = 0;
  int lastSoundAt = -1;   // seconds into the stop
  std::vector<int> events;
  std::vector<int> verdicts;
};

static const char* lightName(int l) {
  switch (l) {
    case L_OFF: return "off";
    case L_GREEN: return "green";
    case L_AMBER: return "amber";
    case L_RED_BLINK: return "RED";
    case L_AMBER_BLINK: return "amber-blink";
  }
  return "?";
}

// A scenario: the machine runs `runS` seconds (arming it after 60), stops for
// `stopS` seconds, and `hooks` change the inputs second by second.
struct Inputs {
  bool hasWall = false;
  int64_t wallMs = 0;
  bool readerUp = true;
  bool suspect = false;
  bool fault = false;
  const char* tap = nullptr;
  bool reboot = false;
  const char* applyBody = nullptr;
};
typedef void (*Hook)(int stopSecond, Inputs* in);

static Seen run(const std::string& body, int runS, int stopS, Hook hook, int pulsesPerS = 6,
                bool rebootBeforeRun = false) {
  Seen seen;
  StationLogic logic(&g_live);
  logic.boot(false);
  logic.applyConfig(body.c_str(), &g_scratch);
  OutputGovernor gov;
  gov.begin(0);
  uint32_t t = 1;
  uint64_t total = 0;
  if (rebootBeforeRun) logic.boot(false);
  for (int s = 0; s < runS + stopS; s++, t++) {
    Inputs in;
    int stopSecond = s - runS;
    if (hook) hook(stopSecond, &in);
    if (in.reboot) {
      logic.boot(false);
      gov.begin(t * 1000);
    }
    if (in.applyBody) logic.applyConfig(in.applyBody, &g_scratch);
    TickInput ti;
    ti.uptimeS = t;
    ti.hasWall = in.hasWall;
    ti.wallMs = in.wallMs + (int64_t)t * 1000;
    uint32_t p = s < runS ? (uint32_t)pulsesPerS : 0;
    total += p;
    ti.pulses = p;
    ti.total = total;
    ti.tap = in.tap;
    ti.readerUp = in.readerUp;
    ti.inputSuspect = in.suspect;
    ti.outputFault = in.fault;
    TickOutput out;
    logic.tick(ti, &out);
    if (stopSecond >= 0) seen.lights.insert(out.light);
    if (out.sounding) seen.logicSoundS++;
    for (int i = 0; i < out.eventCount; i++) {
      seen.events.push_back(out.events[i].event);
      if (out.events[i].event == EV_ALERT_STARTED) seen.alerts++;
      if (out.events[i].event == EV_SOUND_PLAYED) {
        seen.sounds++;
        seen.lastSoundAt = stopSecond;
      }
      if (out.events[i].event == EV_CARD_TAPPED) seen.verdicts.push_back(out.events[i].v);
    }
    // The pins, at the task's 20 ms refresh, through the governor.
    for (int ms = 0; ms < 1000; ms += 20)
      if (gov.refresh(out.light, out.sounding, t * 1000 + (uint32_t)ms).sounder) seen.pinSoundMs += 20;
  }
  return seen;
}

static std::string lights(const Seen& s) {
  std::string out;
  for (std::set<int>::const_iterator it = s.lights.begin(); it != s.lights.end(); ++it) {
    if (!out.empty()) out += "+";
    out += lightName(*it);
  }
  return out;
}

static std::string eventCodes(const Seen& s) {
  std::set<int> u(s.events.begin(), s.events.end());
  std::string out;
  for (std::set<int>::const_iterator it = u.begin(); it != u.end(); ++it) {
    char b[8];
    snprintf(b, sizeof(b), "%s%d", out.empty() ? "" : ",", *it);
    out += b;
  }
  return out.empty() ? "-" : out;
}

static void row(const char* name, const Seen& s, bool soundAllowed) {
  printf("  %-44s LIGHT %-24s SOUND %3d s (pins %3d.%d s)  alerts %d  events %s\n", name,
         lights(s).c_str(), s.logicSoundS, s.pinSoundMs / 1000, (s.pinSoundMs % 1000) / 100,
         s.alerts, eventCodes(s).c_str());
  if (!soundAllowed) CHECK(s.logicSoundS == 0 && s.pinSoundMs == 0, "%s: it sounded", name);
  CHECK(s.pinSoundMs <= s.logicSoundS * 1000, "%s: the pins sounded more than asked", name);
  CHECK(s.alerts <= 1, "%s: %d alerts for one stop", name, s.alerts);
}

// ---- hooks ----------------------------------------------------------------
static const int64_t LUNCH_UTC_MS = 7LL * 3600 * 1000;  // 12:30 IST on day 0
static void hLunch(int, Inputs* in) {
  in->hasWall = true;
  in->wallMs = LUNCH_UTC_MS - 70LL * 1000;  // the stop begins inside the window
}
static void hQuietCard(int s, Inputs* in) {
  if (s == 30) in->tap = "04CCCC01";
}
static void hRebootMidStop(int s, Inputs* in) {
  if (s == 240) in->reboot = true;  // during the alert
}
static void hSuspect(int s, Inputs* in) { in->suspect = s >= 0; }
static void hFault(int s, Inputs* in) { in->fault = s >= 0; }
static void hAckBefore(int s, Inputs* in) {
  if (s == 60) in->tap = "04AAAA01";
}
static void hAckDuringSound(int s, Inputs* in) {
  if (s == 201) in->tap = "04AAAA01";  // red at 180, first sound at 200 (4 s)
}
static void hPersonDuringAlert(int s, Inputs* in) {
  if (s == 190) in->tap = "04BBBB01";
}
static std::string g_badBody = "COVIO-STATION 1\nVER=99\nMODE=7\n";
static void hInvalidConfig(int s, Inputs* in) {
  if (s == 100) in->applyBody = g_badBody.c_str();
}
static void hNoClock(int, Inputs* in) { in->hasWall = false; }
static void hReaderDown(int, Inputs* in) { in->readerUp = false; }
static void hNoTaps(int, Inputs*) {}

int main() {
  printf("1. the false-hooter matrix (LIGHT, SOUND, station EVENTS for each condition)\n");
  std::string ls = config(2, false), lso = config(1, false), off = config(0, false),
              win = config(2, true);
  Seen s;

  s = run(ls, 300, 900, hNoTaps);
  row("normal stop, 15 min, light+sound", s, true);
  CHECK(s.alerts == 1 && s.sounds == 3 && s.logicSoundS == 12, "normal stop: %d alerts %d sounds %d s",
        s.alerts, s.sounds, s.logicSoundS);
  CHECK(s.lights.count(L_RED_BLINK), "normal stop never went red");

  s = run(ls, 300, 120, hNoTaps);
  row("short normal pause, 2 min (< 3 min)", s, false);
  CHECK(s.alerts == 0 && !s.lights.count(L_RED_BLINK), "a short pause alerted");

  s = run(win, 300, 900, hLunch);
  row("stop inside the quiet window (lunch)", s, false);
  CHECK(s.alerts == 0, "the quiet window alerted");

  s = run(ls, 300, 900, hQuietCard);
  row("manual Quiet card 30 s into the stop", s, false);
  CHECK(s.alerts == 0, "a quiet card did not hold the alert");

  s = run(off, 300, 900, hNoTaps);
  row("station switched off (mode Off)", s, false);
  CHECK(s.lights.size() == 1 && s.lights.count(L_OFF), "mode off lit something");

  s = run(lso, 300, 900, hNoTaps);
  row("mode Light only (the pilot setting)", s, false);
  CHECK(s.alerts == 1 && s.lights.count(L_RED_BLINK), "light only did not go red");

  s = run(ls, 300, 900, hRebootMidStop);
  row("reboot during the alert", s, true);
  CHECK(s.lastSoundAt < 240, "it sounded again after the reboot (at %d s)", s.lastSoundAt);

  s = run(ls, 0, 1200, hNoTaps, 6, true);
  row("power returns to a stopped machine", s, false);
  CHECK(s.alerts == 0, "a machine that never ran this boot alerted");

  s = run(ls, 300, 900, hSuspect);
  row("sensor suspect", s, false);
  CHECK(s.alerts == 0 && s.lights.count(L_AMBER_BLINK), "suspect: alerted or no fault light");

  s = run(ls, 300, 900, hFault);
  row("output fault (expander not answering)", s, false);

  s = run(ls, 300, 900, hNoTaps);
  row("network lost (logic has no network input)", s, true);
  CHECK(s.logicSoundS == 12, "offline changed the sound");

  s = run(ls, 300, 900, hAckBefore);
  row("call acknowledged before red (action card)", s, false);
  CHECK(s.alerts == 0, "an acknowledged stop alerted");

  s = run(ls, 300, 900, hAckDuringSound);
  row("action card during the first sound", s, true);
  CHECK(s.logicSoundS <= 2 && s.sounds == 1, "the sound did not stop at the card (%d s)",
        s.logicSoundS);

  s = run(ls, 300, 900, hPersonDuringAlert);
  row("person card during the alert (attending)", s, false);

  {
    // A full spool: the machine produced ~96 % of the spool, so the stop gets
    // the spool allowance (600 s), not the ordinary 180 s.
    std::string spool = config(2, false, 1800);
    s = run(spool, 300, 480, hNoTaps);
    row("spool/changeover: 8 min stop on a full spool", s, false);
    CHECK(s.alerts == 0, "a full-spool changeover alerted inside its allowance");
    s = run(spool, 300, 900, hNoTaps);
    row("spool/changeover: 15 min stop on a full spool", s, true);
    CHECK(s.alerts == 1, "a long full-spool stop never alerted");
  }

  s = run(ls, 300, 900, hInvalidConfig);
  row("invalid config arrives mid-stop", s, true);
  CHECK(s.logicSoundS == 12, "an invalid config changed the running behaviour");

  s = run(win, 300, 900, hNoClock);
  row("clock unknown, quiet windows configured", s, false);
  CHECK(s.alerts == 1 && s.lights.count(L_RED_BLINK), "no clock: the light did not go red");

  s = run(ls, 300, 900, hReaderDown);
  row("card reader down", s, true);
  CHECK(s.sounds == 1, "reader down: %d announcements (want 1: nobody can answer)", s.sounds);

  printf("\n2. sound stays finite offline: one stop, no cloud, 24 hours\n");
  s = run(ls, 300, 24 * 3600, hNoTaps);
  printf("  sound asked %d s, pins %d.%d s, %d announcements, the last %d s into the stop; "
         "light at the end: %s\n",
         s.logicSoundS, s.pinSoundMs / 1000, (s.pinSoundMs % 1000) / 100, s.sounds, s.lastSoundAt,
         lights(s).c_str());
  CHECK(s.sounds == 3 && s.logicSoundS == 12, "24 h offline: %d sounds, %d s", s.sounds,
        s.logicSoundS);
  CHECK(s.lastSoundAt < 900, "it sounded %d s into the stop", s.lastSoundAt);
  CHECK(s.pinSoundMs <= (int)(CAP_SECONDS_PER_STOP * 1000), "over the per-stop cap");
  {
    // The hard ceiling, whatever the config says: N is capped at 5, each
    // announcement at 4 s, 60 s a stop. A config asking for more is refused.
    std::string greedy = config(2, false);
    greedy.replace(greedy.find("N=3"), 3, "N=5");
    greedy.replace(greedy.find("R=180"), 5, "R=60");
    s = run(greedy, 300, 24 * 3600, hNoTaps);
    printf("  the most a valid config allows (N=5, R=60): %d s of sound, then never again\n",
           s.logicSoundS);
    CHECK(s.logicSoundS == 20 && s.sounds == 5, "max config: %d s, %d sounds", s.logicSoundS,
          s.sounds);
  }

  printf("\n3. one presentation of a card is one business event\n");
  {
    // Held at the reader: the reader reports it on every read.
    struct Case {
      const char* name;
      std::vector<std::pair<int, const char*> > taps;  // (stop second, uid)
      int wantTapEvents;
    };
    std::vector<Case> cases;
    Case held = {"same card held at the reader for 10 s", {}, 1};
    for (int i = 0; i < 10; i++) held.taps.push_back(std::make_pair(200 + i, "04AAAA01"));
    cases.push_back(held);
    Case rapid = {"same card tapped 3 times inside 3 s", {}, 1};
    rapid.taps.push_back(std::make_pair(200, "04AAAA01"));
    rapid.taps.push_back(std::make_pair(201, "04AAAA01"));
    rapid.taps.push_back(std::make_pair(202, "04AAAA01"));
    cases.push_back(rapid);
    Case two = {"two different cards one after the other", {}, 2};
    two.taps.push_back(std::make_pair(200, "04AAAA01"));
    two.taps.push_back(std::make_pair(201, "04BBBB01"));
    cases.push_back(two);
    Case pa = {"person, then action", {}, 2};
    pa.taps.push_back(std::make_pair(200, "04BBBB01"));
    pa.taps.push_back(std::make_pair(205, "04AAAA01"));
    cases.push_back(pa);
    Case ap = {"action, then person", {}, 2};
    ap.taps.push_back(std::make_pair(200, "04AAAA01"));
    ap.taps.push_back(std::make_pair(205, "04BBBB01"));
    cases.push_back(ap);
    Case uv = {"unknown, then valid", {}, 2};
    uv.taps.push_back(std::make_pair(200, "DEADBEEF"));
    uv.taps.push_back(std::make_pair(201, "04AAAA01"));
    cases.push_back(uv);
    Case dup = {"reader reconnects and repeats its last frame", {}, 1};
    dup.taps.push_back(std::make_pair(200, "04AAAA01"));
    dup.taps.push_back(std::make_pair(202, "04AAAA01"));  // the replayed frame
    cases.push_back(dup);
    Case later = {"same card again after 5 s (a new tap)", {}, 2};
    later.taps.push_back(std::make_pair(200, "04AAAA02"));
    later.taps.push_back(std::make_pair(205, "04AAAA02"));
    cases.push_back(later);

    for (size_t c = 0; c < cases.size(); c++) {
      StationLogic logic(&g_live);
      logic.boot(false);
      std::string body = config(2, false);
      logic.applyConfig(body.c_str(), &g_scratch);
      int tapEvents = 0;
      std::string verdicts;
      for (int sec = 1; sec <= 300 + 300; sec++) {
        int stopSecond = sec - 300;
        const char* tap = nullptr;
        for (size_t k = 0; k < cases[c].taps.size(); k++)
          if (cases[c].taps[k].first == stopSecond) tap = cases[c].taps[k].second;
        TickInput ti;
        ti.uptimeS = (uint32_t)sec;
        ti.hasWall = false;
        ti.wallMs = 0;
        ti.pulses = sec <= 300 ? 6 : 0;
        ti.total = 0;
        ti.tap = tap;
        ti.readerUp = true;
        ti.inputSuspect = false;
        ti.outputFault = false;
        TickOutput out;
        logic.tick(ti, &out);
        for (int i = 0; i < out.eventCount; i++) {
          if (out.events[i].event != EV_CARD_TAPPED) continue;
          tapEvents++;
          char b[8];
          snprintf(b, sizeof(b), "%sv%u", verdicts.empty() ? "" : ",", out.events[i].v);
          verdicts += b;
        }
      }
      printf("  %-46s presentations %zu -> tap events %d (verdicts %s)\n", cases[c].name,
             cases[c].taps.size(), tapEvents, verdicts.c_str());
      CHECK(tapEvents == cases[c].wantTapEvents, "%s: %d tap events, want %d", cases[c].name,
            tapEvents, cases[c].wantTapEvents);
    }
  }

  printf("\n%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
