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
#include <unistd.h>      // ADR-003 (Phase 2): POSIX truncate() -- see append()
#include <errno.h>
#include "config.h"
#include "totalizer.h"   // ADR-003 (Phase 2): EventQueue reads Totalizer's
                          // write-offset checkpoint (see cleanupOrphanSegments_
                          // and append()'s truncate-before-append logic below).
                          // One-directional: Totalizer has no knowledge of
                          // EventQueue.

#define QROW_MAGIC   0x51524F57UL   // 'QROW'
#define ACK_MAGIC    0xAC4ED000UL   // valid hex ('ACKED' had a non-hex 'K')
#define LOG_PATH     "/queue/log.bin"
#define ACK_PATH_A   "/queue/ackA.bin"
#define ACK_PATH_B   "/queue/ackB.bin"

// ---------------------------------------------------------------------------
// ADR-001 (Self-Describing, Versioned Telemetry Schema): every record, on both
// the SD row and the wire JSON, is prefixed with a schema_version and a
// record_type. A device only ever writes/emits the single schema_version its
// currently-running firmware understands (no runtime negotiation). Each
// (schema_version, record_type) pair's layout is fixed forever once published
// — see SCHEMA_REGISTRY.md at the repository root for the authoritative,
// versioned field-layout registry. Do not bump SCHEMA_VERSION_CURRENT to
// change an already-published layout; introduce a new version instead.
// ---------------------------------------------------------------------------
#define SCHEMA_VERSION_CURRENT  1   // current schema_version this firmware writes
#define RECORD_TYPE_TELEMETRY   1   // the only record_type defined as of Phase 1

// One queued telemetry row. Fixed size => torn writes are detectable by size+CRC.
struct __attribute__((packed)) QRow {
  uint32_t magic;
  uint16_t schema_version; // ADR-001: schema this row was written under
  uint16_t record_type;    // ADR-001: RECORD_TYPE_* discriminator
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
  // ---- ADR-003 (Phase 2): persisted read cursor ---------------------------
  // First not-yet-acknowledged row's position (segment, byte offset).
  // Recomputed/persisted ONLY on a confirmed cumulative ack — never on a
  // mere read (ADR-003's "single most important correctness property").
  // NOT YET COMPUTED by this task: ackThrough()'s cursor-advance logic is a
  // later Phase 2 task (P2-T7); pending() does not consult these fields yet
  // either (P2-T6). Until then they simply persist/recover whatever value
  // they were last set to (zero, on a fresh queue).
  uint32_t cursor_segment;
  uint32_t cursor_offset;
  uint32_t crc32;
};

class EventQueue {
public:
  // `tot` is optional (default nullptr) so the existing call site in
  // covio_firmware.ino (`eventQueue.begin();`) keeps compiling unmodified.
  // Wiring the real Totalizer* through is a later Phase 2 task (P2-T8);
  // until then, orphan-segment cleanup below is correctly a no-op.
  void begin(Totalizer* tot = nullptr) {
    tot_ = tot;
    if (!SD.exists("/queue")) SD.mkdir("/queue");
    AckRec a, b;
    bool va = loadAck_(ACK_PATH_A, a), vb = loadAck_(ACK_PATH_B, b);
    if (va && vb) ack_ = (a.writes >= b.writes) ? a : b;
    else if (va)  ack_ = a;
    else if (vb)  ack_ = b;
    else { memset(&ack_, 0, sizeof(ack_)); ack_.magic = ACK_MAGIC; }
    Serial.printf("[Q] acked_seq=%u\n", ack_.acked_seq);
    cleanupOrphanSegments_();

    // DM-Phase 1: seed the maintained backlog counter with one full scan at
    // boot -- this is the ONLY unbounded scan this feature introduces, and
    // it runs once, not per push cycle (that's exactly the F-05 cost this
    // design avoids). Guarded on tot_ != nullptr, mirroring every other
    // segment-aware/legacy split in this class; the legacy path leaves the
    // counter at its default 0, matching pendingCount()'s own doc comment.
    if (tot_) unackedCount_ = computeUnackedCount_();
  }

  // ADR-003 (Phase 2): truncate-before-append + active-segment write path.
  // Guarded on tot_ != nullptr (wired by a later Phase 2 task) so this stays
  // fully inert -- falling back to appendLegacy_(), the original single-file
  // behavior -- until pending()/ackThrough() are updated to read segments
  // (later Phase 2 tasks); otherwise rows written here would never be read
  // back. See the P2-T5 implementation report for the full rationale.
  void append(const QRow& rowIn) {
    if (!tot_) { appendLegacy_(rowIn); return; }

    QRow row = rowIn;
    row.magic = QROW_MAGIC;
    row.crc32 = 0;
    row.crc32 = crc32_((uint8_t*)&row, sizeof(row));

    uint32_t activeSeg  = tot_->queueOffsetSegment();
    uint32_t goodOffset = tot_->queueOffsetOffset();
    String path = segmentPath_(activeSeg);

    // ---- determine actual on-disk size --------------------------------------
    // FILE_APPEND creates the segment file if this is its first row (same,
    // already-proven behavior the original LOG_PATH append() relied on) and
    // never truncates existing content on open.
    uint32_t actualSize;
    {
      File f = SD.open(path, FILE_APPEND);
      if (!f) { Serial.println("[Q] segment open FAILED"); return; }
      actualSize = f.size();
      f.close();
    }

    // ---- truncate-before-append ----------------------------------------------
    // Compare actual on-disk size to the last CONFIRMED good offset. If the
    // file is larger, a prior append was torn (power loss between the write
    // and its checkpoint) -- discard the unconfirmed tail via a direct POSIX
    // truncate BEFORE any new bytes are written. No row is ever appended on
    // top of unconfirmed bytes (ADR-003). Uses POSIX truncate() against the
    // VFS-mounted path, not a File-class method -- see the truncate-
    // capability investigation: File::truncate() does not exist in this
    // core's SD/FS API, but the underlying ESP-IDF VFS layer's truncate()
    // does.
    if (actualSize > goodOffset) {
      if (truncate(path.c_str(), goodOffset) != 0) {
        Serial.printf("[Q] truncate FAILED path=%s to=%u (errno=%d)\n",
                      path.c_str(), (unsigned)goodOffset, errno);
        return;   // do not append on top of an unresolved torn tail
      }
    } else if (actualSize < goodOffset) {
      // Defensive only -- should not happen in normal operation: the
      // checkpoint claims MORE good bytes than the file physically has.
      // Clamp to the smaller, actually-true value rather than trust an
      // impossible number. Logged loudly, not a silent fallback. (This is
      // NOT the ACR-002 corrupted-checkpoint scenario -- that concerns BOTH
      // checkpoint slots being totally unreadable, handled by Totalizer's
      // existing recovery fallback, unchanged by this task.)
      Serial.printf("[Q] WARNING: checkpoint offset %u exceeds actual segment "
                    "size %u -- clamping\n", (unsigned)goodOffset, (unsigned)actualSize);
      goodOffset = actualSize;
    }

    // ---- write, always at true end-of-file after any truncation above --------
    // DM-Phase 1: §13 A.3 sd_write_latency_ms -- times this row's actual
    // open+write+flush+close, the SD I/O this diagnostic is meant to
    // reflect. Recorded regardless of outcome below (a slow failing write
    // is exactly what this metric should surface).
    uint32_t sdWriteStart = millis();
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
      // DM-Phase 1 validation-pass fix: a slow-then-failing open is exactly
      // the SD trouble this metric exists to surface -- record it here too,
      // not only after a successful open, matching this comment block's own
      // "regardless of outcome" claim above.
      lastRowWriteLatencyMs_ = millis() - sdWriteStart;
      haveRowWriteLatency_ = true;
      Serial.println("[Q] segment append open FAILED");
      return;
    }
    size_t written = f.write((uint8_t*)&row, sizeof(row));
    f.flush();
    f.close();
    lastRowWriteLatencyMs_ = millis() - sdWriteStart;
    haveRowWriteLatency_ = true;
    if (written != sizeof(row)) {
      // Short/failed write -- treat exactly like a torn write: do NOT
      // advance the checkpoint. The next append()'s truncate-before-append
      // check cleans up whatever partial bytes landed.
      Serial.printf("[Q] segment write SHORT (%u/%u bytes) -- checkpoint not advanced\n",
                    (unsigned)written, (unsigned)sizeof(row));
      return;
    }

    // ---- checkpoint update (AFTER the row is durably flushed) ----------------
    tot_->setQueueOffset(activeSeg, goodOffset + sizeof(QRow));
    unackedCount_++;   // DM-Phase 1: this row is now durably pending
  }

  // Pre-Phase-2 behavior, preserved verbatim as the fallback path while
  // tot_ is not yet wired. Identical to the original append().
  void appendLegacy_(const QRow& rowIn) {
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

  // ADR-003 (Phase 2): persisted-cursor, segment-aware read path. Fill `out`
  // with up to `maxN` un-acked rows in FIFO order, starting at the persisted
  // cursor (ack_.cursor_segment/cursor_offset) instead of the start of the
  // file, and walking forward across segment boundaries as needed. Guarded
  // on tot_ != nullptr, mirroring append()/appendLegacy_() -- falls back to
  // pendingLegacy_() while tot_ is not yet wired. The cursor itself is NOT
  // advanced here (that is ackThrough()'s job, a later Phase 2 task) -- this
  // task only reads from wherever the cursor already points.
  int pending(QRow* out, int maxN) {
    // DM-Phase 1: §13 A.3 queue_read_latency_ms -- wraps the whole call
    // (whichever path -- legacy or segment-aware -- actually executes)
    // rather than instrumenting either internal path separately.
    uint32_t readStart = millis();
    int result = pendingImpl_(out, maxN);
    lastPendingLatencyMs_ = millis() - readStart;
    havePendingLatency_ = true;
    return result;
  }

  // Same access level as pending() above (this class's existing convention
  // already keeps *Legacy_ trailing-underscore helpers in this same public
  // section, e.g. pendingLegacy_/appendLegacy_/ackThroughLegacy_ below --
  // not changing that existing pattern here).
  int pendingImpl_(QRow* out, int maxN) {
    if (!tot_) return pendingLegacy_(out, maxN);

    int n = 0;
    uint32_t seg = ack_.cursor_segment;
    uint32_t off = ack_.cursor_offset;
    const uint32_t activeSeg = tot_->queueOffsetSegment();

    while (n < maxN && seg <= activeSeg) {
      String path = segmentPath_(seg);
      File f = SD.open(path, FILE_READ);
      if (!f) {
        // No such segment (e.g. the cursor is already past the only
        // segment that has ever existed). Nothing more to read.
        break;
      }

      // Never read past the persisted write offset for the ACTIVE segment:
      // its raw on-disk size may still include an unconfirmed torn tail
      // left over from a crash, not yet cleaned up by the next append()'s
      // truncate-before-append check. A segment strictly below activeSeg is
      // already fully written and closed, so its full size is safe to read.
      uint32_t limit = (seg == activeSeg) ? tot_->queueOffsetOffset() : f.size();
      if (off > limit) off = limit;   // defensive clamp, should not occur
      f.seek(off);

      while (n < maxN && (off + sizeof(QRow)) <= limit) {
        QRow r;
        size_t got = f.read((uint8_t*)&r, sizeof(r));
        if (got != sizeof(r)) break;   // short read -- stop, don't guess
        off += sizeof(QRow);
        if (r.magic != QROW_MAGIC) continue;                    // skip junk
        uint32_t want = r.crc32; r.crc32 = 0;
        if (crc32_((uint8_t*)&r, sizeof(r)) != want) continue;  // torn -> skip
        r.crc32 = want;
        if (r.seq <= ack_.acked_seq) continue;  // already acked (seq is GLOBALLY monotonic)
        out[n++] = r;
      }
      f.close();

      // This segment is exhausted (no more full rows fit within `limit`).
      // Move to the next segment only if this wasn't the active one -- the
      // active segment is where new data still lands; a not-yet-created
      // next segment simply doesn't exist.
      if (seg == activeSeg) break;
      seg++;
      off = 0;
    }
    return n;
  }

  // Pre-Phase-2 behavior, preserved verbatim as the fallback path while
  // tot_ is not yet wired. Identical to the original pending().
  int pendingLegacy_(QRow* out, int maxN) {
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

  // ADR-003 (Phase 2): persisted-cursor advancement + per-segment deletion,
  // performed ONLY on a confirmed cumulative ack — never on a mere read
  // (pending(), P2-T6, never mutates the cursor; this is ADR-003's "single
  // most important correctness property"). Guarded on tot_ != nullptr,
  // mirroring append()/pending() — falls back to ackThroughLegacy_() while
  // tot_ is not yet wired. seq is globally monotonic (continues across
  // reboots), so the compare is on seq alone; boot_id is stored for
  // diagnostics only.
  void ackThrough(uint32_t ackedSeq, uint32_t bootId) {
    if (!tot_) { ackThroughLegacy_(ackedSeq, bootId); return; }
    if (ackedSeq <= ack_.acked_seq) return;

    // ---- walk the cursor forward, mirroring pending()'s own segment-
    // boundary rules exactly, so the read path and the ack path never
    // disagree about what counts as "read" vs "acknowledged." -------------
    uint32_t seg = ack_.cursor_segment;
    uint32_t off = ack_.cursor_offset;
    const uint32_t activeSeg = tot_->queueOffsetSegment();
    // DM-Phase 1: counts every row this walk consumes (torn/junk OR
    // genuinely newly-acked) -- exactly the rows pending()'s own skip logic
    // would no longer return, so this is the correct, exact decrement for
    // the maintained backlog counter below.
    uint32_t consumedRows = 0;

    while (seg <= activeSeg) {
      String path = segmentPath_(seg);
      File f = SD.open(path, FILE_READ);
      if (!f) break;   // nothing here (yet) -- cursor stays where it is

      uint32_t limit = (seg == activeSeg) ? tot_->queueOffsetOffset() : f.size();
      if (off > limit) off = limit;
      f.seek(off);

      while (off + sizeof(QRow) <= limit) {
        QRow r;
        size_t got = f.read((uint8_t*)&r, sizeof(r));
        if (got != sizeof(r)) break;   // short read -- stop, don't guess
        bool consumable;
        if (r.magic != QROW_MAGIC) {
          consumable = true;   // junk -- pending() skips these unconditionally too
        } else {
          uint32_t want = r.crc32; r.crc32 = 0;
          bool crcOk = (crc32_((uint8_t*)&r, sizeof(r)) == want);
          consumable = !crcOk || (r.seq <= ackedSeq);  // torn (skip like pending()), or genuinely acked
        }
        if (!consumable) break;   // first not-yet-acked real row -- stop here
        off += sizeof(QRow);
        consumedRows++;
      }
      f.close();

      if (seg < activeSeg && off >= limit) {
        // This CLOSED segment is now fully consumed/acknowledged -- the
        // cursor advances into the next segment.
        seg++;
        off = 0;
        continue;
      }
      break;   // active segment's confirmed frontier reached, or a real
               // not-yet-acked row found -- stop walking
    }

    ack_.acked_seq      = ackedSeq;
    ack_.boot_id        = bootId;
    ack_.cursor_segment = seg;
    ack_.cursor_offset  = off;

    // DM-Phase 1: decrement the maintained backlog counter by exactly what
    // this call's walk consumed. Defensive clamp (never trust an impossible
    // number, matching this file's existing goodOffset-clamp convention in
    // append()) -- should not occur, but a logged clamp beats a silent
    // uint32 underflow if it ever does.
    if (consumedRows <= unackedCount_) {
      unackedCount_ -= consumedRows;
    } else {
      Serial.printf("[Q] WARNING: ackThrough consumed %u rows but only %u were "
                    "tracked pending -- clamping backlog counter to 0\n",
                    (unsigned)consumedRows, (unsigned)unackedCount_);
      unackedCount_ = 0;
    }

    // ---- persist the new cursor FIRST, delete completed segments SECOND ----
    // If a crash lands between these two steps, the persisted cursor (or
    // the prior valid one, via dual-slot CRC fallback) always points at a
    // segment id <= however many segments still physically exist -- a
    // segment that "should" have been deleted but wasn't is simply
    // re-recognized and removed on the next successful ack. A segment is
    // NEVER deleted before its corresponding cursor advance is durable.
    persistAck_();

    for (uint32_t s = 0; s < seg; s++) {
      SD.remove(segmentPath_(s));   // range is always < seg <= activeSeg --
                                     // the active segment is never deleted
    }
  }

  // Pre-Phase-2 behavior, preserved verbatim as the fallback path while
  // tot_ is not yet wired. Identical to the original ackThrough().
  void ackThroughLegacy_(uint32_t ackedSeq, uint32_t bootId) {
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

  // DM-Phase 1 (local diagnostics, §13 A.3 /api/v1/status "queue" object) --
  // maintained counter, NOT a rescan -- see append()/ackThrough() above for
  // where it's kept in sync. Only accurate once tot_ != nullptr (segment-
  // aware path, which is the only live path per current covio_firmware.ino
  // wiring); the legacy path never updates it, matching every other
  // dual-path split in this class.
  uint32_t pendingCount() { return unackedCount_; }
  uint32_t ackedSeq()     { return ack_.acked_seq; }

  // DM-Phase 1 (local diagnostics, §13 A.3 /api/v1/metrics) -- "have" flags
  // distinguish "never measured yet this boot" from a genuine 0ms result.
  bool     haveRowWriteLatency()  { return haveRowWriteLatency_; }
  uint32_t lastRowWriteLatencyMs() { return lastRowWriteLatencyMs_; }
  bool     havePendingLatency()   { return havePendingLatency_; }
  uint32_t lastPendingLatencyMs() { return lastPendingLatencyMs_; }

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

  // DM-Phase 1: unbounded counting-only mirror of pending()'s segment walk,
  // used ONCE at boot (begin()) to seed unackedCount_ from ground truth.
  // Never called from the hot path -- append()/ackThrough() above are what
  // keep the counter in sync afterward without a rescan. Only called when
  // tot_ != nullptr (caller-guarded in begin()).
  uint32_t computeUnackedCount_() {
    uint32_t count = 0;
    uint32_t seg = ack_.cursor_segment;
    uint32_t off = ack_.cursor_offset;
    const uint32_t activeSeg = tot_->queueOffsetSegment();

    while (seg <= activeSeg) {
      String path = segmentPath_(seg);
      File f = SD.open(path, FILE_READ);
      if (!f) break;

      uint32_t limit = (seg == activeSeg) ? tot_->queueOffsetOffset() : f.size();
      if (off > limit) off = limit;
      f.seek(off);

      while (off + sizeof(QRow) <= limit) {
        QRow r;
        size_t got = f.read((uint8_t*)&r, sizeof(r));
        if (got != sizeof(r)) break;   // short read -- stop, don't guess
        if (r.magic != QROW_MAGIC) { off += sizeof(QRow); continue; }         // junk
        uint32_t want = r.crc32; r.crc32 = 0;
        if (crc32_((uint8_t*)&r, sizeof(r)) != want) { off += sizeof(QRow); continue; } // torn
        if (r.seq <= ack_.acked_seq) { off += sizeof(QRow); continue; }       // already acked
        count++;
        off += sizeof(QRow);
      }
      f.close();

      if (seg == activeSeg) break;
      seg++;
      off = 0;
    }
    return count;
  }

  AckRec ack_{};
  Totalizer* tot_ = nullptr;   // ADR-003 (Phase 2): may be null until P2-T8
  uint32_t unackedCount_ = 0;  // DM-Phase 1: maintained backlog counter

  // DM-Phase 1: metrics instrumentation state (§13 A.3 /api/v1/metrics)
  bool     haveRowWriteLatency_ = false;
  uint32_t lastRowWriteLatencyMs_ = 0;
  bool     havePendingLatency_ = false;
  uint32_t lastPendingLatencyMs_ = 0;

  // ---- ADR-003 (Phase 2): segment naming + orphan cleanup ----------------
  static const int MAX_ORPHAN_CANDIDATES = 32;

  static String segmentPath_(uint32_t id) {
    char buf[40];
    snprintf(buf, sizeof(buf), QUEUE_SEGMENT_PATH_FMT, (unsigned)id);
    return String(buf);
  }

  // Extracts the numeric id from a "..seg_NNNNNN.bin"-shaped name. Returns
  // false (leaves idOut untouched) for any non-matching filename, e.g.
  // "ackA.bin" — so cleanupOrphanSegments_() safely ignores non-segment
  // files in the same directory.
  static bool parseSegmentId_(const String& name, uint32_t& idOut) {
    int i = name.indexOf("seg_");
    if (i < 0) return false;
    i += 4;
    if (i + 6 > (int)name.length()) return false;
    uint32_t v = 0;
    for (int k = 0; k < 6; k++) {
      char c = name[i + k];
      if (c < '0' || c > '9') return false;
      v = v * 10 + (uint32_t)(c - '0');
    }
    idOut = v;
    return true;
  }

  // Deletes any segment file numbered strictly above the checkpoint's
  // active segment. The only way such a file can exist is an interrupted
  // rollover (segment file created, but the write-offset checkpoint never
  // advanced to point at it before a crash) — see
  // Docs/PHASE2_DESIGN_REPORT.md §5.6. A no-op if tot_ is null (pre-P2-T8).
  //
  // Two-pass by design: directory-mutation-during-iteration safety is
  // unverified for this codebase's SD/VFS toolchain. Pass 1 only
  // enumerates; Pass 2 only deletes, after the directory handle is closed.
  void cleanupOrphanSegments_() {
    if (!tot_) return;
    uint32_t activeSeg = tot_->queueOffsetSegment();

    uint32_t orphanIds[MAX_ORPHAN_CANDIDATES];
    int orphanCount = 0;

    File dir = SD.open(SD_QUEUE_DIR);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File f = dir.openNextFile();
    while (f) {
      bool isDir = f.isDirectory();
      String name = String(f.name());
      f.close();
      if (!isDir) {
        uint32_t id;
        if (parseSegmentId_(name, id) && id > activeSeg) {
          if (orphanCount < MAX_ORPHAN_CANDIDATES) {
            orphanIds[orphanCount++] = id;
          } else {
            Serial.println("[Q] orphan-segment candidate list full -- "
                            "remainder will be retried next boot");
          }
        }
      }
      f = dir.openNextFile();
    }
    dir.close();   // directory handle fully closed before any delete

    for (int i = 0; i < orphanCount; i++) {
      SD.remove(segmentPath_(orphanIds[i]));
    }
  }
};
