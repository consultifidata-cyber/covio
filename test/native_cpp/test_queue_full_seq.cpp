// test_queue_full_seq.cpp -- a long outage fills the flash; nothing may break.
//
// The REAL totalizer.h and queue.h, compiled as the board compiles them (the
// production LittleFsBackend, segment rollover, pending(), ackThrough(), the
// reserve) on an in-memory flash that fills and survives power cuts
// (corun_shim/), against a server that walks seqs as protocol.ts
// resolveRecords does: in order, a duplicate skipped, and a GAP STOPS the walk.
// The flash model is pessimistic: once it is full, EVERY write is refused,
// the totalizer's checkpoint rewrite included.
//
// The loop's telemetry step (covio_firmware.ino) is modelled both ways:
//   BEFORE  append(row); service(seq)            a refused row's seq is
//                                                checkpointed anyway: a hole
//   AFTER   append(row); if refused seq--; service(seq)
//
// Scenarios (a simulated day and a half at 1 row a second, 6 Hz on and off):
//   1. seqs: BEFORE jams the server at the first hole for ever; AFTER drains.
//   2. a power cut WHILE the flash is full, then the network returns: the
//      counter comes back exactly at its last checkpoint (the rule every power
//      cut has: at most the last second's pulses are lost), the queue drains,
//      the server reaches the board's last seq.
//   3. three reboots while full, then partial and stale acks in the recovery.
// Built with -DQUEUE_RESERVE_BYTES=0 -DEXPECT_RESERVE_DEFECT=1 it must SHOW the
// defect the reserve closes (scenario 2's counter comes back wrong): the test
// proves it can see the failure it guards against.
//
//   g++ -std=c++14 -O2 -Wall -Icorun_shim -I. -I../.. test_queue_full_seq.cpp -o qfs && ./qfs
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../queue.h"
#include "../../totalizer.h"
#include "../../ack_validation.h"

#ifndef EXPECT_RESERVE_DEFECT
#define EXPECT_RESERVE_DEFECT 0
#endif

extern "C" int truncate(const char* path, off_t length) noexcept {
  const char* prefix = "/littlefs";
  size_t n = strlen(prefix);
  const char* p = strncmp(path, prefix, n) == 0 ? path + n : path;
  return shimTruncate(p, (size_t)length) ? 0 : -1;
}

struct Server {
  uint32_t watermark = 0;
  uint32_t deliver(const QRow* r, int n) {
    if (watermark == 0 && n > 0 && r[0].seq > 1) watermark = r[0].seq - 1;
    for (int i = 0; i < n; i++) {
      if (r[i].seq <= watermark) continue;
      if (r[i].seq != watermark + 1) break;  // a gap: STOP, never skip past
      watermark = r[i].seq;
    }
    return watermark;
  }
};

struct Opts {
  bool fixed;             // the #17 seq rule
  uint32_t hours;
  uint32_t outageFromS, outageToS;
  bool powerCutWhileFull; // one cut, 30 h into the outage
  bool rebootsWhileFull;  // three cuts while full
  bool messyAcks;         // partial and stale acks during the recovery
};

struct Outcome {
  uint32_t lastSeq = 0, watermark = 0, refused = 0, cuts = 0;
  uint64_t pulses = 0;      // what the hardware counted while powered
  uint64_t lostAtCuts = 0;  // after the last checkpoint, at each cut
  uint64_t finalTotal = 0;
  bool recoveredExact = true;
  uint64_t worstRecoveryError = 0;
  bool fullReached = false;
};

struct Board {
  Totalizer tot;
  EventQueue q;
  uint32_t seq = 0;
  uint32_t boot = 0;
};

static Outcome run(const Opts& o) {
  shimFs() = ShimFs();
  shimPcnt() = ShimPcnt();
  Outcome out;
  Server server;
  Board* b = new Board();
  b->boot = 1;
  b->tot.begin(b->boot);
  b->q.begin(&b->tot);
  b->seq = resumeSeqFloor(b->tot.lastSeq(), b->q.ackedSeq());
  uint64_t generated = 0, atCheckpoint = 0;
  bool cutDone = false;
  uint32_t rebootsDone = 0;

  // Power is cut: RAM and the PCNT register are gone, flash survives. The
  // board must come back with exactly the total of its last checkpoint.
  auto cut = [&]() {
    out.lostAtCuts += generated - atCheckpoint;
    delete b;
    shimPcnt().powerCut();
    b = new Board();
    b->boot = ++out.cuts + 1;
    b->tot.begin(b->boot);
    b->q.begin(&b->tot);
    b->seq = resumeSeqFloor(b->tot.lastSeq(), b->q.ackedSeq());
    uint64_t rec = b->tot.total();
    uint64_t err = rec > atCheckpoint ? rec - atCheckpoint : atCheckpoint - rec;
    if (err) out.recoveredExact = false;
    if (err > out.worstRecoveryError) out.worstRecoveryError = err;
    generated = rec;
    atCheckpoint = rec;
  };

  for (uint32_t s = 1; s <= o.hours * 3600; s++) {
    shimMillis() = s * 1000;
    uint32_t pulses = (s % 900) < 600 ? 6 : 0;  // 10 min running, 5 stopped
    shimPcnt().pulse(pulses);
    generated += pulses;
    out.pulses += pulses;

    QRow row;
    memset(&row, 0, sizeof(row));
    row.schema_version = SCHEMA_VERSION_CURRENT;
    row.record_type = RECORD_TYPE_TELEMETRY;
    row.boot_id = b->boot;
    row.seq = ++b->seq;
    row.ts = s;
    row.totalizer = b->tot.total();
    uint32_t failedBefore = b->q.failedWriteCount();
    b->q.append(row);
    if (b->q.failedWriteCount() != failedBefore) {
      out.refused++;
      out.fullReached = true;
      if (o.fixed) b->seq--;
    }
    b->tot.service(b->seq);
    atCheckpoint = generated;  // what a checkpoint that landed would hold

    bool outage = s > o.outageFromS && s < o.outageToS;
    if (outage && out.fullReached && o.powerCutWhileFull && !cutDone &&
        s > o.outageFromS + 30 * 3600) {
      cutDone = true;
      cut();
      continue;
    }
    if (outage && out.fullReached && o.rebootsWhileFull && rebootsDone < 3 && s % 1800 == 0) {
      rebootsDone++;
      cut();
      continue;
    }
    if (!outage && s % 5 == 0) {
      QRow batch[PUSH_BATCH_MAX];
      int n = b->q.pending(batch, PUSH_BATCH_MAX);
      if (n > 0) {
        uint32_t wm = server.deliver(batch, n);
        uint32_t ack = wm < batch[n - 1].seq ? wm : batch[n - 1].seq;
        if (o.messyAcks && s % 15 == 0 && batch[n / 2].seq < ack) ack = batch[n / 2].seq;
        if (o.messyAcks && s % 25 == 0) ack = b->q.ackedSeq();  // a stale repeat
        if (ack > 0) b->q.ackThrough(ack, b->boot);
      }
    }
  }
  for (int i = 0; i < 200000 && b->q.pendingCount() > 0; i++) {
    QRow batch[PUSH_BATCH_MAX];
    int n = b->q.pending(batch, PUSH_BATCH_MAX);
    if (n <= 0) break;
    uint32_t before = b->q.ackedSeq();
    uint32_t wm = server.deliver(batch, n);
    b->q.ackThrough(wm < batch[n - 1].seq ? wm : batch[n - 1].seq, b->boot);
    if (b->q.ackedSeq() == before) break;  // stuck behind a hole
  }
  out.lastSeq = b->seq;
  out.watermark = server.watermark;
  out.finalTotal = b->tot.total();
  delete b;
  return out;
}

static int g_fail = 0;
#define CHECK(cond, ...)   \
  do {                     \
    if (!(cond)) {         \
      printf("  FAIL: ");  \
      printf(__VA_ARGS__); \
      printf("\n");        \
      g_fail++;            \
    }                      \
  } while (0)

static void show(const char* name, const Outcome& r) {
  printf("%-48s refused=%u seq=%u server=%u pulses=%llu final=%llu lost_at_cuts=%llu cuts=%u "
         "recovered_exact=%d worst_recovery_error=%llu\n",
         name, r.refused, r.lastSeq, r.watermark, (unsigned long long)r.pulses,
         (unsigned long long)r.finalTotal, (unsigned long long)r.lostAtCuts, r.cuts,
         r.recoveredExact ? 1 : 0, (unsigned long long)r.worstRecoveryError);
}

int main() {
  printf("QUEUE_RESERVE_BYTES=%lu\n", (unsigned long)QUEUE_RESERVE_BYTES);
  // A 33-hour outage from hour 1 fills the 3.5 MB partition (~27 h of rows).
  Opts base = {true, 36, 3600, 34 * 3600, false, false, false};

  Opts cutOpts = base;
  cutOpts.powerCutWhileFull = true;
  Outcome c = run(cutOpts);

#if EXPECT_RESERVE_DEFECT
  show("2. NO RESERVE: power cut while full", c);
  CHECK(c.fullReached && c.cuts == 1, "the flash never filled, or no cut happened");
  CHECK(!c.recoveredExact,
        "with no reserve the counter still came back exact; this build should show the defect");
  if (!c.recoveredExact)
    printf("\nDEFECT REPRODUCED without the reserve: after a power cut while the flash was full "
           "the counter came back %llu pulses wrong.\n",
           (unsigned long long)c.worstRecoveryError);
#else
  Opts before = base;
  before.fixed = false;
  Outcome b = run(before), a = run(base);
  show("1. BEFORE #17 (a refused row's seq is burned)", b);
  show("1. AFTER #17", a);
  show("2. power cut while full, then network back", c);
  Opts messy = base;
  messy.rebootsWhileFull = true;
  messy.messyAcks = true;
  Outcome m = run(messy);
  show("3. three cuts while full + partial/stale acks", m);

  CHECK(b.refused > 0 && a.refused > 0, "the flash never filled; nothing is proven");
  CHECK(b.watermark < b.lastSeq, "BEFORE did not jam (the test no longer sees the defect)");
  CHECK(a.watermark == a.lastSeq, "AFTER: the server stopped at %u of %u", a.watermark,
        a.lastSeq);
  CHECK(a.finalTotal == a.pulses, "AFTER: total %llu != pulses %llu",
        (unsigned long long)a.finalTotal, (unsigned long long)a.pulses);
  CHECK(c.cuts == 1 && c.fullReached, "scenario 2 did not cut the power while full");
  CHECK(c.recoveredExact, "power cut while full: the counter came back %llu pulses wrong",
        (unsigned long long)c.worstRecoveryError);
  CHECK(c.finalTotal == c.pulses - c.lostAtCuts,
        "power cut: final %llu != pulses %llu - lost after the checkpoint %llu",
        (unsigned long long)c.finalTotal, (unsigned long long)c.pulses,
        (unsigned long long)c.lostAtCuts);
  CHECK(c.watermark == c.lastSeq, "after the cut the server stopped at %u of %u", c.watermark,
        c.lastSeq);
  CHECK(m.cuts == 3 && m.recoveredExact, "cuts while full: cuts=%u exact=%d", m.cuts,
        m.recoveredExact);
  CHECK(m.watermark == m.lastSeq, "messy acks: the server stopped at %u of %u", m.watermark,
        m.lastSeq);
  CHECK(m.finalTotal == m.pulses - m.lostAtCuts, "messy: final total wrong");
#endif
  printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
