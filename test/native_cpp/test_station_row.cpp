// test_station_row.cpp -- every station event survives the 36-byte row.
//
// Replays the parity scenarios (station_parity_vectors.h) through
// station_logic.h, packs each event it emits into the record_type 2 payload
// (station_row.h), decodes it back to wire JSON and requires the JSON the
// platform's parser expects, built straight from the event. The one designed
// loss is the config version on a tap (MC-066); everything else is exact.
//
//   g++ -std=c++14 -O1 -Wall -I. -I../.. test_station_row.cpp -o row && ./row
#include <stdio.h>
#include <string.h>

#include "station_logic.h"
#include "station_parity_vectors.h"
#include "station_row.h"

using namespace station;

static Config g_live;
static Config g_scratch;
static int g_fail = 0;
static unsigned long g_events = 0;

static void expectedJson(const Event& e, uint32_t ts, char* buf, size_t len) {
  size_t o = 0;
  buf[0] = 0;
#define put(...) appendf(buf, len, &o, __VA_ARGS__)
  put("\"event\":%u", (unsigned)e.event);
  switch (e.event) {
    case EV_CARD_TAPPED:
      put(",\"uid\":\"%s\"", e.uid);
      put(",\"v\":%u", (unsigned)e.v);
      if (e.hasN) put(",\"n\":%lld", (long long)e.n);
      break;
    case EV_ALERT_STARTED: {
      long long gap = (long long)ts - e.stopTs;
      long long stop = gap > 65535 ? (long long)ts - 65535 : e.stopTs;
      put(",\"stop_ts\":%lld", stop);
      put(",\"total\":%llu", (unsigned long long)e.total);
      break;
    }
    case EV_RUNNING: {
      long long gap = (long long)ts - e.stopTs;
      long long stop = gap > 65535 ? (long long)ts - 65535 : e.stopTs;
      put(",\"stop_ts\":%lld", stop);
      break;
    }
    case EV_CONFIG_APPLIED:
      put(",\"cfg\":%lu", (unsigned long)e.cfg);
      break;
    case EV_CONFIG_REJECTED:
      put(",\"cfg\":%lu", (unsigned long)e.cfg);
      put(",\"err\":\"%s\"", e.err);
      break;
    case EV_QUIET_STARTED:
      put(",\"src\":%u", (unsigned)e.src);
      if (e.hasN) put(",\"n\":%lld", (long long)e.n);
      break;
    case EV_QUIET_ENDED:
      put(",\"src\":%u", (unsigned)e.src);
      break;
    case EV_SOUND_PLAYED:
    case EV_ANNOUNCEMENTS_EXHAUSTED:
      put(",\"n\":%lld", (long long)e.n);
      break;
    default:
      break;
  }
#undef put
}

static void check(const Event& e, uint32_t ts, const char* where) {
  g_events++;
  StationPayload p;
  if (!encodeEvent(e, ts, &p)) {
    printf("FAIL %s: event %u could not be encoded\n", where, (unsigned)e.event);
    g_fail++;
    return;
  }
  char got[256], want[256];
  payloadJson(p, ts, got, sizeof(got));
  expectedJson(e, ts, want, sizeof(want));
  if (strcmp(got, want) != 0) {
    printf("FAIL %s t=%u\n  want %s\n  got  %s\n", where, ts, want, got);
    g_fail++;
  }
}

int main() {
  for (uint32_t s = 0; s < PV_SCENARIO_COUNT; s++) {
    const PvScenario& sc = PV_SCENARIOS[s];
    StationLogic logic(&g_live);
    uint32_t lastT = 0;
    for (uint32_t i = 0; i < sc.count && g_fail < 10; i++) {
      const PvStep& st = sc.steps[i];
      if (st.kind == 2) {
        logic.boot(st.ack != 0);
      } else if (st.kind == 1) {
        Event e = logic.applyConfig(st.body, &g_scratch);
        check(e, lastT, sc.name);
      } else {
        TickInput in = {st.t, st.wall >= 0, st.wall, st.pulses, st.total, st.tap,
                        st.reader != 0, st.suspect != 0, st.fault != 0};
        TickOutput out;
        logic.tick(in, &out);
        lastT = st.t;
        for (int k = 0; k < out.eventCount; k++) check(out.events[k], st.t, sc.name);
      }
    }
  }

  // Limits, stated rather than discovered.
  Event big;
  memset(&big, 0, sizeof(big));
  big.event = EV_CARD_TAPPED;
  big.hasUid = true;
  strcpy(big.uid, "0102030405060708090A");  // 10 bytes: ISO 14443 triple size
  StationPayload p;
  if (encodeEvent(big, 1, &p)) {
    printf("FAIL a 10-byte UID was encoded; it must be refused\n");
    g_fail++;
  }
  Event leading;
  memset(&leading, 0, sizeof(leading));
  leading.event = EV_CARD_TAPPED;
  leading.hasUid = true;
  strcpy(leading.uid, "00000001");  // leading zero bytes survive (length carried)
  leading.hasV = true;
  leading.v = TV_UNKNOWN;
  check(leading, 5, "leading zeros");

  printf("%lu events round-tripped, %d failure(s)\n", g_events, g_fail);
  return g_fail ? 1 : 0;
}
