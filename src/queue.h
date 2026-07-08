// ============================================================================
// queue.h  —  persistent offline telemetry queue (SD, append-only)
// ----------------------------------------------------------------------------
// Records are appended as fixed-size CRC'd binary rows to a daily log file.
// A separate "acked-through" pointer (its own dual-slot CRC record) records the
// highest seq the server has cumulatively acknowledged.
//
// INVARIANTS enforced here:
//  - Never delete a record until the server ACKs it (cumulative ack_seq).
//  - A bare HTTP 200 is NOT an ack. Only a parsed ack_seq prunes. (Invariant 6.)
//  - Torn append fails CRC on read and is skipped, never parsed as garbage.
//  - FIFO order preserved by seq.
//
// SIMPLIFICATION for a one-file queue: we keep an in-flash single log
// "/queue/log.bin" of fixed-size rows. Pruning is logical (advance the acked
// pointer); physical compaction happens only when idle and fully drained, to
// avoid rewriting a large file mid-operation.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"

#define QROW_MAGIC   0x51524F57UL   // 'QROW'
#define ACK_MAGIC    0xAC4ED000UL   // valid hex ('ACKED' had a non-hex 'K')
#define LOG_PATH     "/queue/log.bin"
#define ACK_PATH_A   "/queue/ackA.bin"
#define ACK_PATH_B   "/queue/ackB.bin"

// One queued telemetry row. Fixed size => torn writes are detectable by size+CRC.
struct __attribute__((packed)) QRow {
  uint32_t magic;
  uint32_t boot_id;
  uint32_t seq;
  uint32_t ts;            // device uptime seconds (server stamps wall-clock)
  uint64_t totalizer;     // raw pulses, lifetime
  uint16_t quality;       // bitfield: see QUALITY_* below
  uint16_t rssi_abs;      // |RSSI|
  uint32_t crc32;
};

#define QUALITY_OK            0x0000
#define QUALITY_BACKLOG_HIGH  0x0001   // queue exceeded highwater
#define QUALITY_TIME_UNSYNCED 0x0002   // no wall-clock yet

struct __attribute__((packed)) AckRec {
  uint32_t magic;
  uint32_t acked_seq;     // cumulative: server has all seq <= this
  uint32_t boot_id;       // ack applies within this boot_id's numbering
  uint32_t writes;
  uint32_t crc32;
};

class EventQueue {
public:
  void begin() {
    if (!SD.exists("/queue")) SD.mkdir("/queue");
    AckRec a, b;
    bool va = loadAck_(ACK_PATH_A, a), vb = loadAck_(ACK_PATH_B, b);
    if (va && vb) ack_ = (a.writes >= b.writes) ? a : b;
    else if (va)  ack_ = a;
    else if (vb)  ack_ = b;
    else { memset(&ack_, 0, sizeof(ack_)); ack_.magic = ACK_MAGIC; }
    Serial.printf("[Q] acked_seq=%u\n", ack_.acked_seq);
  }

  void append(const QRow& rowIn) {
    QRow row = rowIn;
    row.magic = QROW_MAGIC;
    row.crc32 = 0;
    row.crc32 = crc32_((uint8_t*)&row, sizeof(row));
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (!f) { Serial.println("[Q] append open FAILED"); return; }
    f.write((uint8_t*)&row, sizeof(row));
    f.flush();
    f.close();
  }

  // Fill `out` with up to `maxN` un-acked rows in FIFO order. Returns count.
  int pending(QRow* out, int maxN) {
    File f = SD.open(LOG_PATH, FILE_READ);
    if (!f) return 0;
    int n = 0;
    QRow r;
    while (n < maxN && f.available() >= (int)sizeof(QRow)) {
      f.read((uint8_t*)&r, sizeof(r));
      if (r.magic != QROW_MAGIC) continue;         // skip junk
      uint32_t want = r.crc32; r.crc32 = 0;
      if (crc32_((uint8_t*)&r, sizeof(r)) != want) continue;  // torn -> skip
      r.crc32 = want;
      if (r.seq <= ack_.acked_seq) continue;   // seq is GLOBALLY monotonic across boots
      out[n++] = r;
    }
    f.close();
    return n;
  }

  // Advance the durable acked pointer. ONLY call with a real server ack_seq.
  // seq is globally monotonic (continues across reboots), so the compare is
  // on seq alone; boot_id is stored for diagnostics only.
  void ackThrough(uint32_t ackedSeq, uint32_t bootId) {
    if (ackedSeq <= ack_.acked_seq) return;
    ack_.acked_seq = ackedSeq;
    ack_.boot_id   = bootId;
    persistAck_();
    maybeCompact_();
  }

  bool empty() {
    QRow tmp[1];
    return pending(tmp, 1) == 0;
  }

private:
  static uint32_t crc32_(const uint8_t* d, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
      crc ^= d[i];
      for (int k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return ~crc;
  }

  bool loadAck_(const char* path, AckRec& out) {
    File f = SD.open(path, FILE_READ);
    if (!f || f.size() != sizeof(AckRec)) { if (f) f.close(); return false; }
    f.read((uint8_t*)&out, sizeof(out)); f.close();
    if (out.magic != ACK_MAGIC) return false;
    uint32_t want = out.crc32; out.crc32 = 0;
    bool ok = (crc32_((uint8_t*)&out, sizeof(out)) == want);
    out.crc32 = want; return ok;
  }

  void persistAck_() {
    ack_.magic = ACK_MAGIC;
    ack_.writes++;
    const char* path = (ack_.writes & 1) ? ACK_PATH_A : ACK_PATH_B;
    ack_.crc32 = 0;
    ack_.crc32 = crc32_((uint8_t*)&ack_, sizeof(ack_));
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    f.write((uint8_t*)&ack_, sizeof(ack_));
    f.flush(); f.close();
  }

  // Physically shrink the log only when everything is acked — safe point.
  void maybeCompact_() {
    if (!empty()) return;
    if (SD.exists(LOG_PATH)) SD.remove(LOG_PATH);   // fully drained; start fresh
  }

  AckRec ack_{};
};
