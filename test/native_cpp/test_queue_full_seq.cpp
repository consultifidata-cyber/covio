// test_queue_full_seq.cpp -- a row the full flash refuses must not burn its seq.
//
// The REAL totalizer.h and queue.h, compiled as the board compiles them (the
// production LittleFsBackend, segment rollover, pending() and ackThrough()) on
// an in-memory flash that fills (corun_shim/), and a server that walks seqs as
// protocol.ts resolveRecords does: in order, a duplicate skipped, and a GAP
// STOPS the walk -- it is never skipped past.
//
// The loop's telemetry step (covio_firmware.ino) is modelled both ways:
//   BEFORE  append(row); service(seq)            -- a refused row's seq is
//           checkpointed anyway: a hole the server waits at, for ever
//   AFTER   append(row); if refused seq--; service(seq)
// A 31-hour outage fills the 3.5 MB partition, the network returns, and the test
// asks whether every row the board holds reaches the server.
//
//   g++ -std=c++14 -O2 -Wall -Icorun_shim -I. -I../.. test_queue_full_seq.cpp -o qfs && ./qfs
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <vector>

#include "../../queue.h"
#include "../../totalizer.h"
#include "../../ack_validation.h"

extern "C" int truncate(const char* path, off_t length) noexcept {
  const char* prefix = "/littlefs";
  size_t n = strlen(prefix);
  const char* p = strncmp(path, prefix, n) == 0 ? path + n : path;
  return shimTruncate(p, (size_t)length) ? 0 : -1;
}

struct Server {
  uint32_t watermark = 0;
  uint64_t stored = 0;
  uint32_t deliver(const QRow* r, int n) {
    if (watermark == 0 && n > 0 && r[0].seq > 1) watermark = r[0].seq - 1;
    for (int i = 0; i < n; i++) {
      if (r[i].seq <= watermark) continue;
      if (r[i].seq != watermark + 1) break;  // a gap: STOP
      watermark = r[i].seq;
      stored++;
    }
    return watermark;
  }
};

struct Outcome {
  uint32_t lastSeq, watermark, refused;
  uint64_t total, pulses;
};

static Outcome run(bool fixed) {
  shimFs() = ShimFs();
  shimPcnt() = ShimPcnt();
  Totalizer tot;
  EventQueue q;
  tot.begin(1);
  q.begin(&tot);
  uint32_t seq = resumeSeqFloor(tot.lastSeq(), q.ackedSeq());
  Server server;
  Outcome o = {0, 0, 0, 0, 0};
  const uint32_t hours = 34;
  for (uint32_t s = 1; s <= hours * 3600; s++) {
    shimMillis() = s * 1000;
    uint32_t pulses = (s % 900) < 600 ? 6 : 0;  // 10 min running, 5 min stopped
    shimPcnt().pulse(pulses);
    o.pulses += pulses;
    // one telemetry row a second
    QRow row;
    memset(&row, 0, sizeof(row));
    row.schema_version = SCHEMA_VERSION_CURRENT;
    row.record_type = RECORD_TYPE_TELEMETRY;
    row.boot_id = 1;
    row.seq = ++seq;
    row.ts = s;
    row.totalizer = tot.total();
    uint32_t failedBefore = q.failedWriteCount();
    q.append(row);
    if (q.failedWriteCount() != failedBefore) {
      o.refused++;
      if (fixed) seq--;
    }
    tot.service(seq);
    // a push every 5 s, except during a 31-hour outage (hours 1 to 32)
    bool outage = s > 1 * 3600 && s < 32 * 3600;
    if (!outage && s % 5 == 0) {
      QRow batch[PUSH_BATCH_MAX];
      int n = q.pending(batch, PUSH_BATCH_MAX);
      if (n > 0) {
        uint32_t wm = server.deliver(batch, n);
        uint32_t ack = wm < batch[n - 1].seq ? wm : batch[n - 1].seq;
        if (ack > 0) q.ackThrough(ack, 1);
      }
    }
  }
  // the network stays up: drain everything that can be drained
  for (int i = 0; i < 100000 && q.pendingCount() > 0; i++) {
    QRow batch[PUSH_BATCH_MAX];
    int n = q.pending(batch, PUSH_BATCH_MAX);
    if (n <= 0) break;
    uint32_t before = q.ackedSeq();
    uint32_t wm = server.deliver(batch, n);
    q.ackThrough(wm < batch[n - 1].seq ? wm : batch[n - 1].seq, 1);
    if (q.ackedSeq() == before) break;  // stuck behind a hole
  }
  o.lastSeq = seq;
  o.watermark = server.watermark;
  o.total = tot.total();
  return o;
}

int main() {
  int fail = 0;
  Outcome before = run(false), after = run(true);
  printf("BEFORE: %u rows refused by the full flash; board seq %u, server stopped at %u; "
         "total %llu of %llu pulses\n",
         before.refused, before.lastSeq, before.watermark, (unsigned long long)before.total,
         (unsigned long long)before.pulses);
  printf("AFTER:  %u rows refused by the full flash; board seq %u, server reached %u; "
         "total %llu of %llu pulses\n",
         after.refused, after.lastSeq, after.watermark, (unsigned long long)after.total,
         (unsigned long long)after.pulses);
  if (before.refused == 0) {
    printf("FAIL the flash never filled; nothing is proven\n");
    fail++;
  }
  if (before.watermark >= before.lastSeq) {
    printf("FAIL the old behaviour did not jam (the test no longer reproduces the defect)\n");
    fail++;
  }
  if (after.watermark != after.lastSeq) {
    printf("FAIL with the fix the server still stops at %u of %u\n", after.watermark,
           after.lastSeq);
    fail++;
  }
  if (after.total != after.pulses || before.total != before.pulses) {
    printf("FAIL the pulse total is not exact\n");
    fail++;
  }
  printf("%s: %d failure(s)\n", fail ? "FAIL" : "PASS", fail);
  return fail ? 1 : 0;
}
