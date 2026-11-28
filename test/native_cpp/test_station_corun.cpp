// test_station_corun.cpp -- the counter and the station, running together.
//
// S6 §4 and §6: the existing measuring path (pulse counter -> durable queue ->
// seq/ack -> ingest) must stay exact while the station does everything it can
// do at once. This harness compiles the REAL totalizer.h (against the
// corun_shim PCNT and filesystem), the REAL queue.h (against the fake storage
// backend), and the REAL station logic, row encoding and tick clock. The loop
// itself (covio_firmware.ino) cannot compile on a host, so it is MODELLED here,
// step for step, from the code:
//
//   * the PCNT counts in hardware, continuously, whatever software is doing;
//   * the loop -- the only writer of the queue and of seq -- appends a
//     telemetry row each second and checkpoints, drains the station's events
//     into rows the same way, and runs ONE network transaction per pass; a push
//     can hold the loop for up to 80 s (HTTPS_WORST_CASE_TXN_MS);
//   * the station task keeps running during those stalls: it reads the PCNT
//     register (never clears it), ticks the logic and fills its 160-slot
//     in-memory queue, which the loop drains when it comes back;
//   * the server is the platform's resolveRecords: seqs in order, a duplicate
//     skipped, and a GAP STOPS the walk -- it is never skipped past;
//   * the network fails, acknowledges partly, repeats a stale ack, answers 500
//     and answers 200 without an ack; configs arrive valid, invalid and
//     unchanged; cards are tapped in bursts; the flash fills; power is cut at
//     the worst moments.
//
// Proven per scenario:
//   1. PULSES: the board's final lifetime total equals the pulses the hardware
//      saw minus EXACTLY the pulses after the last checkpoint before each power
//      cut -- the pre-station rule -- and the same scenario with no station
//      gives the same total.
//   2. The PCNT register never reaches its limit, and the station never clears
//      it (every clear is the totalizer's).
//   3. The server's watermark reaches the board's last seq: every seq once,
//      never two different rows for one seq.
//   4. Every station event the logic emitted is accounted for: delivered once,
//      or COUNTED as dropped (in-memory queue full), refused (flash full) or
//      lost to a power cut. None disappears silently.
//
// queue.h is compiled WITHOUT NATIVE_TEST -- exactly as the board compiles it,
// with the production LittleFsBackend, segment rollover, pending(),
// ackThrough() and the boot-time orphan cleanup -- on corun_shim/LittleFS.h,
// an in-memory flash that survives a power cut and can fill up.
//
// One scenario is a KNOWN PRE-EXISTING DEFECT, proven rather than hidden: a
// TELEMETRY row the full flash refuses still checkpoints its seq (the loop's
// unconditional `append(row); service(seq)`), the server stops at that hole,
// and every later row -- measurement and station alike -- waits behind it until
// `reset_ack`. Station rows give their seq back (covio_firmware.ino), so they
// never cause it. The same scenario with the proposed one-line telemetry fix
// shows the jam gone (see the separate firmware PR; the counting path is not
// changed by the station PR).
//
//   g++ -std=c++14 -O2 -Wall -Icorun_shim -I. -I../.. test_station_corun.cpp -o corun
//   ./corun
#include <stdio.h>
#include <string.h>

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

#include "../../queue.h"
#include "../../totalizer.h"
#include "../../ack_validation.h"
#include "../../station_io.h"
#include "../../station_row.h"

using namespace station;

// ---- deterministic randomness, one stream per concern ----------------------
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 11);
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  bool chance(uint32_t perMille) { return below(1000) < perMille; }
};

// LittleFsBackend truncates through POSIX with the VFS prefix; land it on the
// in-memory flash.
extern "C" int truncate(const char* path, off_t length) noexcept {
  const char* prefix = "/littlefs";
  size_t n = strlen(prefix);
  const char* p = strncmp(path, prefix, n) == 0 ? path + n : path;
  return shimTruncate(p, (size_t)length) ? 0 : -1;
}

// Two rows are the same row when every field but the queue's own framing
// (magic, crc32) matches.
static bool sameRow(const QRow& a, const QRow& b) {
  return a.schema_version == b.schema_version && a.record_type == b.record_type &&
         a.boot_id == b.boot_id && a.seq == b.seq && a.ts == b.ts && a.totalizer == b.totalizer &&
         a.quality == b.quality && a.rssi_abs == b.rssi_abs;
}

// ---- the server: protocol.ts resolveRecords, the parts that matter ----------
struct Server {
  std::map<uint32_t, QRow> rows;
  uint32_t watermark = 0;
  uint64_t duplicates = 0;
  uint64_t collisions = 0;         // a dropped STATION row under a reused seq: must stay 0
  uint64_t absorbedTelemetry = 0;  // a dropped TELEMETRY row under a reused seq (by design)
  // Returns the ack_seq the server answers.
  uint32_t deliver(const QRow* r, int n) {
    std::vector<QRow> sorted(r, r + n);
    for (size_t i = 1; i < sorted.size(); i++)
      for (size_t j = i; j > 0 && sorted[j - 1].seq > sorted[j].seq; j--)
        std::swap(sorted[j - 1], sorted[j]);
    if (watermark == 0 && !sorted.empty() && sorted[0].seq > 1) watermark = sorted[0].seq - 1;
    for (size_t i = 0; i < sorted.size(); i++) {
      const QRow& row = sorted[i];
      if (row.seq <= watermark) {
        std::map<uint32_t, QRow>::iterator it = rows.find(row.seq);
        if (it != rows.end() && !sameRow(it->second, row)) {
          // A power cut between a row's write and its checkpoint: the row is on
          // flash, its seq is not, and the first row after boot reuses the
          // number. The server keeps the first and drops this one.
          if (row.record_type == RECORD_TYPE_TELEMETRY) absorbedTelemetry++;
          else collisions++;
        }
        duplicates++;
        continue;
      }
      if (row.seq != watermark + 1) break;  // a gap: STOP, never skip past
      rows[row.seq] = row;
      watermark = row.seq;
    }
    return watermark;
  }
};

static const char* CONFIG_BASE =
    "COVIO-STATION 1\nVER=%u\nMODE=2\nT=%u\nTS=600\nSPOOL=0\nC=20\nR=180\nN=3\nQ=3600\n"
    "RECOVER=300\nCORRECT=120\nTZ=330\nK=04AAAA01,A,1\nK=04AAAA02,A,2\nK=04BBBB01,P,0\n"
    "K=04CCCC01,Q,1800\nK=04DDDD01,C,0\nEND=5\n";
static const char* TAPS[] = {"04AAAA01", "04AAAA02", "04BBBB01", "04CCCC01", "DEADBEEF"};

struct Scenario {
  const char* name;
  uint64_t seed;
  uint32_t hours;
  uint32_t maxHz;         // production rate while running is drawn up to this
  bool station;           // false = the same scenario with no station at all
  bool powerCuts;
  uint32_t worstStallMs;  // the longest a push may hold the loop
  bool longOutage;        // a network outage of `outageHours` in the middle
  uint32_t outageHours;
  bool rapidTaps;         // bursts of taps, including during stalls
  size_t flashCapBytes;   // 0 = the real 3.5 MB partition
  bool telemetryFix;      // the proposed fix: a refused telemetry row gives its seq back
};

struct Result {
  uint64_t generatedWhileUp = 0;
  uint64_t lostAtCuts = 0;
  uint64_t finalTotal = 0;
  uint64_t pcntLost = 0;
  uint64_t clearsInStation = 0;
  uint64_t stationReads = 0;
  uint32_t cuts = 0;
  bool recoveredExact = true;
  uint64_t emitted = 0, delivered = 0, droppedMemory = 0, refusedFull = 0, lostCut = 0;
  uint64_t stuckBehindHole = 0;
  uint64_t telemetryRows = 0, telemetryRefused = 0, duplicates = 0, collisions = 0;
  uint64_t absorbedTelemetry = 0;
  uint32_t lastSeq = 0, watermark = 0;
  uint64_t configsApplied = 0, configsRejected = 0, taps = 0, pushes = 0, stalls80 = 0;
  uint32_t maxBacklog = 0;
  bool totalsMonotone = true;
  std::set<uint32_t> burnedTelemetry;
};

struct QEv {
  Event e;
  uint32_t ts;
};

// One board's RAM: everything a power cut destroys.
struct Board {
  Totalizer tot;
  EventQueue q;
  uint32_t seq = 0;
  uint32_t bootId = 0;
};

static Config g_live;
static Config g_scratch;

static Result run(const Scenario& sc) {
  Result r;
  shimFs() = ShimFs();
  if (sc.flashCapBytes) shimFs().capBytes = sc.flashCapBytes;
  shimPcnt() = ShimPcnt();
  Server server;
  Rng rPulse(sc.seed), rNet(sc.seed + 1), rCut(sc.seed + 2), rTap(sc.seed + 3), rCfg(sc.seed + 4);

  Board* b = new Board();
  b->bootId = 1;
  b->tot.begin(b->bootId);
  b->q.begin(&b->tot);
  b->seq = resumeSeqFloor(b->tot.lastSeq(), b->q.ackedSeq());

  StationLogic logic(&g_live);
  logic.boot(false);
  uint32_t cfgVersion = 1;
  char body[1024];
  snprintf(body, sizeof(body), CONFIG_BASE, cfgVersion, 180u);
  std::string lastGoodBody = body;  // what flash holds: the last known good
  logic.applyConfig(body, &g_scratch);
  TickClock clock;
  clock.begin(0);
  std::deque<QEv> memq;  // the FreeRTOS queue, STATION_EVENT_QUEUE_LEN deep
  const size_t MEMQ = 160;
  bool telemetryThisBoot = false;  // covio_firmware.ino: no drain before it
  uint16_t stationLast = 0;
  uint64_t latestTotal = 0;

  // Station rows written AND checkpointed, by seq; a cut that truncates one
  // removes it. What the server must end up holding.
  std::map<uint32_t, QRow> expectStation;
  uint64_t generated = 0;  // what the board should hold if nothing were lost
  uint64_t generatedAtCheckpoint = 0;

  const uint32_t STEP = 100;  // ms
  const uint64_t endMs = (uint64_t)sc.hours * 3600000ULL;
  uint64_t now = 0, bootStart = 0;
  uint64_t stallUntil = 0, tTelemetry = 0, tPush = 0, tCfg = 0;
  const uint64_t NEVER = (uint64_t)-1;
  uint64_t nextCut = sc.powerCuts ? 1800000ULL + rCut.below(3600000) : NEVER;
  int cutKind = -1;  // 0 at a step boundary, 1 after a telemetry write, 2 after a station write
  uint64_t runUntil = 0, stopUntil = 0;
  uint32_t hz = 0;
  bool running = false;
  uint64_t outageFrom = sc.longOutage ? endMs / 4 : NEVER;
  uint64_t outageTo = sc.longOutage ? endMs / 4 + (uint64_t)sc.outageHours * 3600000ULL : 0;
  uint64_t lastTotalSeen = 0;

  auto checkpoint = [&](uint32_t s) {
    b->tot.service(s);
    generatedAtCheckpoint = generated;
  };
  auto armNextCut = [&]() {
    cutKind = -1;
    nextCut = now + 1800000ULL + rCut.below(3600000);
  };
  auto powerCut = [&]() {
    // Everything in RAM is gone: the board, the station's in-memory queue, the
    // PCNT register. Flash (shimFs and the queue backend) survives.
    r.lostAtCuts += generated - generatedAtCheckpoint;
    r.lostCut += memq.size();
    memq.clear();
    delete b;
    shimPcnt().powerCut();
    b = new Board();
    b->bootId = ++r.cuts + 1;
    b->tot.begin(b->bootId);
    b->q.begin(&b->tot);
    b->seq = resumeSeqFloor(b->tot.lastSeq(), b->q.ackedSeq());
    // The recovered total is the last checkpoint, exactly.
    if (b->tot.total() != generatedAtCheckpoint) r.recoveredExact = false;
    generated = generatedAtCheckpoint;
    lastTotalSeen = b->tot.total();
    // A fresh station task with the last known good config from flash.
    logic.boot(false);
    logic.applyConfig(lastGoodBody.c_str(), &g_scratch);
    stationLast = 0;
    stallUntil = now + 2000 + rCut.below(28000);  // off for a while
    bootStart = stallUntil;
    tPush = tCfg = stallUntil;
    tTelemetry = stallUntil - 1000;  // the first pass after boot writes telemetry at once
    telemetryThisBoot = false;
    clock.begin(0);
  };
  auto queueEvent = [&](const Event& e, uint32_t ts) {
    r.emitted++;
    if (memq.size() >= MEMQ) {
      r.droppedMemory++;
      return;
    }
    QEv q;
    q.e = e;
    q.ts = ts;
    memq.push_back(q);
  };

  while (now < endMs) {
    shimMillis() = (uint32_t)now;
    bool boardOn = now >= bootStart;
    // ---- the machine and the hardware counter (never waits for software) ----
    if (now >= (running ? runUntil : stopUntil)) {
      running = !running;
      if (now > endMs - 120000) running = false;  // a quiet tail so the last rows settle
      if (running) {
        hz = 1 + rPulse.below(sc.maxHz);
        runUntil = now + 60000 + rPulse.below(20 * 60000);
      } else {
        stopUntil = now + 30000 + rPulse.below(10 * 60000);
      }
    }
    if (running && boardOn) {
      uint32_t n = (uint32_t)((uint64_t)hz * STEP / 1000);
      if (rPulse.below(1000) < (hz * STEP) % 1000) n++;
      shimPcnt().pulse(n);
      generated += n;
      r.generatedWhileUp += n;
    }
    uint32_t upS = boardOn ? (uint32_t)((now - bootStart) / 1000) : 0;

    // ---- the station task: runs straight through the loop's stalls ---------
    if (sc.station && boardOn) {
      const char* tap = nullptr;
      if (rTap.chance(sc.rapidTaps ? 40 : 4)) {
        tap = TAPS[rTap.below(5)];
        r.taps++;
      }
      uint32_t second;
      if (clock.due(upS, tap != nullptr, &second)) {
        shimPcnt().inStation = true;
        int16_t c = 0;
        pcnt_get_counter_value(PCNT_UNIT_0, &c);
        shimPcnt().inStation = false;
        uint16_t cur = (uint16_t)c;
        uint32_t pulses = cur >= stationLast ? (uint32_t)(cur - stationLast) : (uint32_t)cur;
        stationLast = cur;
        TickInput in;
        in.uptimeS = second;
        in.hasWall = false;
        in.wallMs = 0;
        in.pulses = pulses;
        in.total = latestTotal;
        in.tap = tap;
        in.readerUp = true;
        in.inputSuspect = false;
        in.outputFault = false;
        TickOutput out;
        logic.tick(in, &out);
        for (int i = 0; i < out.eventCount; i++) queueEvent(out.events[i], second);
      }
    }

    // ---- power cuts --------------------------------------------------------
    if (boardOn && cutKind < 0 && now >= nextCut) {
      cutKind = (int)rCut.below(3);
      if (cutKind == 2 && !sc.station) cutKind = 1;
    }
    if (boardOn && cutKind == 0) {
      armNextCut();
      powerCut();
      now += STEP;
      continue;
    }

    // ---- the loop, unless a network call is holding it ---------------------
    if (boardOn && now >= stallUntil) {
      bool cut = false;
      if (now - tTelemetry >= 1000) {
        tTelemetry = now;
        latestTotal = b->tot.total();
        if (latestTotal < lastTotalSeen) r.totalsMonotone = false;
        lastTotalSeen = latestTotal;
        QRow row;
        memset(&row, 0, sizeof(row));
        row.schema_version = SCHEMA_VERSION_CURRENT;
        row.record_type = RECORD_TYPE_TELEMETRY;
        row.boot_id = b->bootId;
        row.seq = ++b->seq;
        row.ts = upS;
        row.totalizer = latestTotal;
        uint32_t failedBefore = b->q.failedWriteCount();
        b->q.append(row);  // 1) durable row FIRST
        bool stored = b->q.failedWriteCount() == failedBefore;
        r.telemetryRows++;
        if (cutKind == 1) {  // between the row's write and its checkpoint
          armNextCut();
          powerCut();
          cut = true;
        } else {
          if (!stored) {
            r.telemetryRefused++;
            if (sc.telemetryFix) b->seq--;  // the proposed fix
            else r.burnedTelemetry.insert(b->seq);  // today: the seq is checkpointed anyway
          }
          checkpoint(b->seq);  // 2) THEN checkpoint total+seq
          telemetryThisBoot = true;
        }
      }
      // drainInto: station events become rows, in order, each with the next seq
      while (!cut && telemetryThisBoot && !memq.empty()) {
        QEv qe = memq.front();
        memq.pop_front();
        StationPayload p;
        if (!encodeEvent(qe.e, qe.ts, &p)) {
          r.droppedMemory++;
          continue;
        }
        QRow srow;
        memset(&srow, 0, sizeof(srow));
        srow.schema_version = SCHEMA_VERSION_CURRENT;
        srow.record_type = RECORD_TYPE_STATION;
        srow.boot_id = b->bootId;
        srow.seq = ++b->seq;
        srow.ts = qe.ts;
        srow.totalizer = p.arg;
        srow.quality = p.quality;
        srow.rssi_abs = p.aux;
        uint32_t failedBefore = b->q.failedWriteCount();
        b->q.append(srow);
        bool stored = b->q.failedWriteCount() == failedBefore;
        if (cutKind == 2) {  // a station row written, its seq not yet checkpointed
          armNextCut();
          if (stored) expectStation[srow.seq] = srow;  // on flash: it will be delivered
          else r.refusedFull++;
          powerCut();
          cut = true;
          break;
        }
        if (!stored) b->seq--;  // covio_firmware.ino: a refused row gives its seq back
        checkpoint(b->seq);
        if (stored) expectStation[srow.seq] = srow;
        else r.refusedFull++;
      }
      if (cut) {
        now += STEP;
        continue;
      }
      if (b->q.pendingCount() > r.maxBacklog) r.maxBacklog = b->q.pendingCount();

      // ONE network transaction per pass: a push, else the station's config poll
      bool netDown = (now >= outageFrom && now < outageTo) || rNet.chance(20);
      if (now - tPush >= 5000) {
        tPush = now;
        r.pushes++;
        uint32_t stall;
        if (netDown) {
          stall = rNet.chance(300) ? sc.worstStallMs : 5000 + rNet.below(10000);
          if (stall > sc.worstStallMs) stall = sc.worstStallMs;
          if (stall >= 80000) r.stalls80++;
        } else {
          stall = 100 + rNet.below(400);
          QRow batch[PUSH_BATCH_MAX];
          int n = b->q.pending(batch, PUSH_BATCH_MAX);
          if (n > 0) {
            uint32_t outcome = rNet.below(100);
            if (outcome >= 4) {  // < 4: a 500, nothing taken
              uint32_t wm = server.deliver(batch, n);
              uint32_t highest = batch[n - 1].seq;
              long ack;
              if (outcome < 8) ack = -1;                                // 200 without ack_seq
              else if (outcome < 13) ack = (long)batch[rNet.below(n)].seq;  // a lower ack
              else if (outcome < 16) ack = (long)b->q.ackedSeq();       // a stale repeat
              else ack = (long)wm;
              if (ack > (long)wm) ack = (long)wm;  // the server never acks past its watermark
              if (ack > (long)highest) ack = (long)highest;  // the device's own clamp
              if (ack > 0) b->q.ackThrough((uint32_t)ack, b->bootId);
            }
          }
        }
        stallUntil = now + stall;
      } else if (sc.station && !netDown && now - tCfg >= 60000) {
        tCfg = now;
        uint32_t kind = rCfg.below(10);
        if (kind >= 6) {  // < 6: UNCHANGED, nothing applied
          char next[1024];
          if (kind < 9)
            snprintf(next, sizeof(next), CONFIG_BASE, cfgVersion + 1, 120u + rCfg.below(300));
          else
            snprintf(next, sizeof(next), "COVIO-STATION 1\nVER=%u\nMODE=9\n", cfgVersion + 1);
          Event e = logic.applyConfig(next, &g_scratch);
          if (e.event == EV_CONFIG_APPLIED) {
            cfgVersion++;
            lastGoodBody = next;
            r.configsApplied++;
          } else {
            r.configsRejected++;
          }
          queueEvent(e, upS);
        }
        stallUntil = now + 200 + rNet.below(800);
      }
    }
    now += STEP;
  }

  // ---- settle: network up, flush until nothing moves ------------------------
  for (int i = 0; i < 200000 && b->q.pendingCount() > 0; i++) {
    QRow batch[PUSH_BATCH_MAX];
    int n = b->q.pending(batch, PUSH_BATCH_MAX);
    if (n <= 0) break;
    uint32_t before = b->q.ackedSeq();
    uint32_t wm = server.deliver(batch, n);
    uint32_t ack = wm < batch[n - 1].seq ? wm : batch[n - 1].seq;
    b->q.ackThrough(ack, b->bootId);
    if (b->q.ackedSeq() == before) break;  // stuck: the server is waiting at a hole
  }

  r.finalTotal = b->tot.total();
  r.pcntLost = shimPcnt().lost;
  r.clearsInStation = shimPcnt().clearsInStation;
  r.stationReads = shimPcnt().stationReads;
  r.duplicates = server.duplicates;
  r.collisions = server.collisions;
  r.absorbedTelemetry = server.absorbedTelemetry;
  r.lastSeq = b->seq;
  r.watermark = server.watermark;
  for (std::map<uint32_t, QRow>::iterator it = server.rows.begin(); it != server.rows.end(); ++it) {
    if (it->second.record_type != RECORD_TYPE_STATION) continue;
    std::map<uint32_t, QRow>::iterator e = expectStation.find(it->first);
    if (e == expectStation.end() || !sameRow(e->second, it->second)) r.collisions++;
    else r.delivered++;
  }
  for (std::map<uint32_t, QRow>::iterator it = expectStation.begin(); it != expectStation.end();
       ++it)
    if (it->first > server.watermark) r.stuckBehindHole++;
  delete b;
  return r;
}

static int g_failures = 0;
#define EXPECT(cond, ...)               \
  do {                                  \
    if (!(cond)) {                      \
      printf("    FAIL %s: ", sc.name); \
      printf(__VA_ARGS__);              \
      printf("\n");                     \
      g_failures++;                     \
    }                                   \
  } while (0)

static void report(const Scenario& sc, const Result& r) {
  printf(
      "%s\n"
      "  pulses: seen=%llu final=%llu lost_at_cuts=%llu (%u cuts) pcnt_lost=%llu "
      "station_reads=%llu station_clears=%llu\n"
      "  queue:  telemetry_rows=%llu refused=%llu last_seq=%u server_watermark=%u dup=%llu "
      "station_rows_dropped=%llu telemetry_absorbed=%llu backlog_max=%u pushes=%llu "
      "stalls_80s=%llu\n"
      "  station: emitted=%llu delivered=%llu dropped_mem=%llu refused_full=%llu lost_cut=%llu "
      "stuck_behind_hole=%llu taps=%llu configs applied=%llu rejected=%llu\n",
      sc.name, (unsigned long long)r.generatedWhileUp, (unsigned long long)r.finalTotal,
      (unsigned long long)r.lostAtCuts, r.cuts, (unsigned long long)r.pcntLost,
      (unsigned long long)r.stationReads, (unsigned long long)r.clearsInStation,
      (unsigned long long)r.telemetryRows, (unsigned long long)r.telemetryRefused, r.lastSeq,
      r.watermark, (unsigned long long)r.duplicates, (unsigned long long)r.collisions,
      (unsigned long long)r.absorbedTelemetry, r.maxBacklog, (unsigned long long)r.pushes, (unsigned long long)r.stalls80,
      (unsigned long long)r.emitted, (unsigned long long)r.delivered,
      (unsigned long long)r.droppedMemory, (unsigned long long)r.refusedFull,
      (unsigned long long)r.lostCut, (unsigned long long)r.stuckBehindHole,
      (unsigned long long)r.taps, (unsigned long long)r.configsApplied,
      (unsigned long long)r.configsRejected);
}

static void checkPulses(const Scenario& sc, const Result& r) {
  EXPECT(r.pcntLost == 0, "the PCNT register reached its limit (%llu counts lost)",
         (unsigned long long)r.pcntLost);
  EXPECT(r.clearsInStation == 0, "the station cleared the PCNT");
  EXPECT(r.recoveredExact, "a recovered total was not the last checkpoint");
  EXPECT(r.finalTotal == r.generatedWhileUp - r.lostAtCuts,
         "final total %llu != pulses %llu - lost after the last checkpoint %llu",
         (unsigned long long)r.finalTotal, (unsigned long long)r.generatedWhileUp,
         (unsigned long long)r.lostAtCuts);
  EXPECT(r.totalsMonotone, "a telemetry row's total went backwards");
  EXPECT(r.collisions == 0, "%llu station rows dropped under a reused seq, or unexpected",
         (unsigned long long)r.collisions);
  if (!sc.powerCuts) EXPECT(r.finalTotal == r.generatedWhileUp, "not exact without power cuts");
  if (sc.station) EXPECT(r.stationReads > 0, "the station never read the counter");
}

static void checkDelivery(const Scenario& sc, const Result& r) {
  EXPECT(r.watermark == r.lastSeq, "server watermark %u stopped short of the board's seq %u",
         r.watermark, r.lastSeq);
  EXPECT(r.emitted == r.delivered + r.droppedMemory + r.refusedFull + r.lostCut,
         "station events unaccounted: emitted %llu != delivered %llu + dropped %llu + refused "
         "%llu + lost at cuts %llu",
         (unsigned long long)r.emitted, (unsigned long long)r.delivered,
         (unsigned long long)r.droppedMemory, (unsigned long long)r.refusedFull,
         (unsigned long long)r.lostCut);
}

int main() {
  const size_t FULL_NEVER = 0;
  // name, seed, hours, maxHz, station, cuts, worstStall, outage, outageH, rapidTaps, flashCap, fix
  Scenario sc0[] = {
      {"A  wire line day (8 h, <=8 Hz)", 11, 8, 8, true, false, 15000, false, 0, false, FULL_NEVER,
       false},
      {"A' the same day, NO station", 11, 8, 8, false, false, 15000, false, 0, false, FULL_NEVER,
       false},
      {"B  80 s stalls + rapid taps (<=30 Hz)", 12, 8, 30, true, false, 80000, false, 0, true,
       FULL_NEVER, false},
      {"C  2000 Hz, healthy network (<=1 s calls)", 13, 6, 2000, true, false, 1000, false, 0, true, FULL_NEVER,
       false},
      {"D  3 h outage + taps + configs", 14, 8, 20, true, false, 15000, true, 3, true, FULL_NEVER,
       false},
      {"E  power cuts at the worst moments, 24 h", 15, 24, 20, true, true, 15000, false, 0, true,
       FULL_NEVER, false},
      {"E' the same cuts, NO station", 15, 24, 20, false, true, 15000, false, 0, false, FULL_NEVER,
       false},
      {"F  everything at once, 12 h", 17, 12, 30, true, true, 80000, true, 2, true, FULL_NEVER,
       false},
      {"G  flash fills in an outage (fix applied)", 16, 10, 20, true, false, 15000, true, 4, true,
       240000, true},
      {"G' flash fills in an outage (TODAY)", 16, 10, 20, true, false, 15000, true, 4, true,
       240000, false},
  };
  const int N = (int)(sizeof(sc0) / sizeof(sc0[0]));
  std::vector<Result> res(N);
  for (int i = 0; i < N; i++) {
    res[i] = run(sc0[i]);
    report(sc0[i], res[i]);
    checkPulses(sc0[i], res[i]);
    if (i != 9) checkDelivery(sc0[i], res[i]);
  }
  {
    const Scenario& sc = sc0[0];
    EXPECT(res[0].finalTotal == res[1].finalTotal &&
               res[0].generatedWhileUp == res[1].generatedWhileUp,
           "station on %llu != station off %llu", (unsigned long long)res[0].finalTotal,
           (unsigned long long)res[1].finalTotal);
    EXPECT(res[0].emitted > 0, "the station emitted nothing; the scenario proves nothing");
  }
  {
    const Scenario& sc = sc0[2];
    EXPECT(res[2].stalls80 > 0, "no 80 s stall happened");
  }
  {
    const Scenario& sc = sc0[5];
    EXPECT(res[5].cuts >= 10, "only %u power cuts", res[5].cuts);
  }
  {
    const Scenario& sc = sc0[8];
    EXPECT(res[8].refusedFull > 0 && res[8].telemetryRefused > 0,
           "the flash never filled; the scenario proves nothing");
  }
  {
    // TODAY's behaviour, asserted so that the fix flips this deliberately: a
    // refused TELEMETRY row burns its seq and the server waits at the hole.
    const Scenario& sc = sc0[9];
    EXPECT(!res[9].burnedTelemetry.empty(), "no telemetry seq was burned");
    EXPECT(res[9].watermark + 1 == *res[9].burnedTelemetry.begin(),
           "the server did not stop at the first burned telemetry seq (watermark %u, first hole "
           "%u)",
           res[9].watermark, *res[9].burnedTelemetry.begin());
    printf(
        "\nKNOWN PRE-EXISTING DEFECT (G'): %zu telemetry seqs burned by a full flash; the server "
        "stopped at seq %u of %u and %llu station events wait behind the hole until reset_ack.\n"
        "Station rows burned: 0 (they give their seq back). Fix: separate firmware PR.\n",
        res[9].burnedTelemetry.size(), res[9].watermark + 1, res[9].lastSeq,
        (unsigned long long)res[9].stuckBehindHole);
  }

  printf(
      "\nPCNT headroom (a property of totalizer.h, not the station): after a checkpoint the "
      "register can hold up to 29,999 undrained counts, leaving 2,768 before its 32,767 limit "
      "-> exact through an 80 s stall up to %u Hz sustained.\n",
      (unsigned)(2768 / 81));
  printf("\n%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
