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
// RISK-04 phase-2 remediation: EventQueue's append()/FailureState paths are
// routed through StorageBackend (storage_backend.h) so a native host test
// can fault-inject them without an ESP32. Production (NATIVE_TEST undefined)
// includes the real Arduino/LittleFS headers exactly as before; native tests
// include the minimal host shim instead. See
// Docs/audit/coviu_oil_meter_p0_remediation_phase2/02_FLASH_FAULT_INJECTION_DESIGN.md.
#ifndef NATIVE_TEST
#include <Arduino.h>
#include <LittleFS.h>
#include <unistd.h>      // ADR-003 (Phase 2): POSIX truncate() -- see append()
#include <errno.h>
#include "totalizer.h"   // ADR-003 (Phase 2): EventQueue reads Totalizer's
                          // write-offset checkpoint (see cleanupOrphanSegments_
                          // and append()'s truncate-before-append logic below).
                          // One-directional: Totalizer has no knowledge of
                          // EventQueue.
#else
#include "arduino_shim.h"
#include "queue_offset_checkpoint.h"
#endif
#include <string.h>      // memcpy (adoptValidRows_) -- harmless on both targets
#include "config.h"
#include "storage_backend.h"

#define QROW_MAGIC   0x51524F57UL   // 'QROW'
#define ACK_MAGIC    0xAC4ED000UL   // valid hex ('ACKED' had a non-hex 'K')
#define LOG_PATH     "/queue/log.bin"
#define ACK_PATH_A   "/queue/ackA.bin"
#define ACK_PATH_B   "/queue/ackB.bin"

// ---------------------------------------------------------------------------
// P0-4 remediation (RISK-04: silent measurement loss at flash-full/write
// failure): every append() failure branch below (segment-open failure,
// truncate failure, short/failed write) previously only logged to Serial and
// returned -- if nobody was watching the serial console at that exact
// instant, that one second's telemetry sample was gone with zero durable
// trace, while the totalizer (a SEPARATE, independent counter) kept
// climbing regardless. An operator reading /api/v1/status afterward had no
// way to tell "the meter was fine but nothing was recorded" from "nothing
// happened, all good."
//
// FAIL_MAGIC uses the exact same dual-slot ping-pong + CRC32 pattern already
// proven correct elsewhere in this file (AckRec) and in totalizer.h
// (Checkpoint) -- reused here specifically because it is already
// battle-tested in this codebase, not reinvented. Written ONLY on an actual
// failure (never on the hot per-second append path), so this adds no new
// wear-leveling burden (see 06_FLASH_LIFETIME_ANALYSIS.md).
//
// DESIGN DECISION (documented, not silently assumed): the failure counter is
// monotonic and NEVER auto-cleared by recovery -- a device that once lost
// data and later started writing successfully again must still show that
// history (mandate requirement: "a measurement must never be treated as
// safely captured unless its durable record has been successfully
// written... the device must enter a clearly observable degraded/fault
// state"). This is "Option A, fail loud" from the mandate's own two listed
// policies: no RAM-only fallback buffer is introduced (that would just move
// the same power-loss risk RISK-04 is about into an even less durable
// place), and the existing pulse-counter/totalizer evidence is untouched and
// keeps counting independent of queue write failures, matching the
// architecture's existing "hardware counts, software ships" separation.
// ---------------------------------------------------------------------------
// RISK-04 phase-2 remediation: the capacity-alarm threshold SELECTION logic
// (not just the underlying percentage arithmetic) is now a pure, dependency-
// free function so a native host test can exercise the exact same boundary
// decisions diagnostics.h's computeHealth_() makes, rather than only testing
// capacityPercentUsed()'s raw division. Highest threshold wins, matching
// diagnostics.h's existing checked-highest-first convention.
enum QueueCapacityAlarm { QCAP_NONE, QCAP_WARNING_80, QCAP_WARNING_90, QCAP_CRITICAL_95, QCAP_CRITICAL_100 };
inline QueueCapacityAlarm capacityAlarmLevel(float pctUsed) {
  if (pctUsed >= 100.0f) return QCAP_CRITICAL_100;
  if (pctUsed >= 95.0f)  return QCAP_CRITICAL_95;
  if (pctUsed >= 90.0f)  return QCAP_WARNING_90;
  if (pctUsed >= 80.0f)  return QCAP_WARNING_80;
  return QCAP_NONE;
}

#define FAIL_MAGIC     0xFA17ED00UL
#define FAIL_PATH_A    "/queue/failA.bin"
#define FAIL_PATH_B    "/queue/failB.bin"

enum QueueFailCode {
  QFAIL_NONE = 0,
  QFAIL_SEGMENT_OPEN,       // LittleFS.open(path, FILE_APPEND) returned false
  QFAIL_TRUNCATE,           // truncate-before-append (torn-tail cleanup) failed
  QFAIL_WRITE_SHORT,        // f.write() wrote fewer bytes than sizeof(QRow), or open-for-write failed
};

struct __attribute__((packed)) FailureState {
  uint32_t magic;
  uint32_t failed_write_count;   // lifetime cumulative, NEVER decremented
  uint32_t first_failure_uptime_s;  // 0 == never failed (device uptime, not wall clock -- no RTC exists)
  uint32_t last_failure_uptime_s;
  uint32_t last_error_code;       // QueueFailCode
  uint32_t writes;                // monotonic -> newest slot wins (ping-pong selector)
  uint32_t crc32;
};

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
// Miki Wire hardening: advisory bits stamped by the MIKI_WIRE_PROFILE
// monitors (covio_firmware.ino). Additive bitfield VALUES within the
// existing uint16 quality field -- the QRow layout, schema_version, and
// wire contract are unchanged (ADR-001: layout frozen; bit semantics are
// data). A build without the profile never sets them.
#define QUALITY_SUSPECT_RATE    0x0004   // pulse rate exceeded configured plausibility ceiling
#define QUALITY_SENSOR_SUSPECT  0x0008   // no pulses beyond configured longest-plausible-idle

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
#ifndef NATIVE_TEST
  // `tot` is optional (default nullptr) so the existing call site in
  // covio_firmware.ino (`eventQueue.begin(&totalizer)`) keeps compiling
  // completely unmodified -- this overload supplies the real production
  // LittleFsBackend automatically, so no call site anywhere in the firmware
  // needs to know StorageBackend exists. RISK-04 phase-2 remediation.
  void begin(IQueueOffsetCheckpoint* tot = nullptr) {
    static LittleFsBackend defaultBackend;
    begin(tot, &defaultBackend);
  }
#endif

  // RISK-04 phase-2 remediation: native host tests call this overload
  // directly, supplying a FakeQueueOffsetCheckpoint and a
  // FakeStorageBackend -- exercising the REAL EventQueue class, not a
  // reimplementation.
  void begin(IQueueOffsetCheckpoint* tot, StorageBackend* backend) {
    tot_ = tot;
    backend_ = backend;
#ifndef NATIVE_TEST
    if (!LittleFS.exists("/queue")) LittleFS.mkdir("/queue");
#endif
    AckRec a, b;
    bool va = loadAck_(ACK_PATH_A, a), vb = loadAck_(ACK_PATH_B, b);
    if (va && vb) ack_ = (a.writes >= b.writes) ? a : b;
    else if (va)  ack_ = a;
    else if (vb)  ack_ = b;
    else { memset(&ack_, 0, sizeof(ack_)); ack_.magic = ACK_MAGIC; }
    Serial.printf("[Q] acked_seq=%u\n", ack_.acked_seq);
#ifndef NATIVE_TEST
    // RISK-04 phase-2: orphan-segment cleanup and the backlog-count boot
    // scan are OUT OF SCOPE for this fault-injection harness (see
    // 02_FLASH_FAULT_INJECTION_DESIGN.md) -- they remain exactly as before,
    // direct-LittleFS, compile-verified-only. Skipped (not fault-injected,
    // not exercised) under NATIVE_TEST so this file can compile on host at
    // all without pulling in LittleFS/File for logic this pass does not
    // touch.
    cleanupOrphanSegments_();
#endif

    // P0-4 remediation (RISK-04): recover the failed-write history same as
    // AckRec above -- this is what makes "failed-write counter survives
    // reboot" (a mandatory P0-4 test) true; a device that lost data before a
    // reboot must still report that history afterward, not start clean.
    FailureState fa, fb;
    bool fva = loadFail_(FAIL_PATH_A, fa), fvb = loadFail_(FAIL_PATH_B, fb);
    if (fva && fvb) fail_ = (fa.writes >= fb.writes) ? fa : fb;
    else if (fva)   fail_ = fa;
    else if (fvb)   fail_ = fb;
    else { memset(&fail_, 0, sizeof(fail_)); fail_.magic = FAIL_MAGIC; }
    if (fail_.failed_write_count > 0) {
      Serial.printf("[Q] WARNING: %u historical write failure(s), last=%s at uptime=%us\n",
                    (unsigned)fail_.failed_write_count, failCodeStr_((QueueFailCode)fail_.last_error_code),
                    (unsigned)fail_.last_failure_uptime_s);
    }

    // DM-Phase 1: seed the maintained backlog counter with one full scan at
    // boot -- this is the ONLY unbounded scan this feature introduces, and
    // it runs once, not per push cycle (that's exactly the F-05 cost this
    // design avoids). Guarded on tot_ != nullptr, mirroring every other
    // segment-aware/legacy split in this class; the legacy path leaves the
    // counter at its default 0, matching pendingCount()'s own doc comment.
#ifndef NATIVE_TEST
    if (tot_) unackedCount_ = computeUnackedCount_();
#endif
  }

  // ADR-003 (Phase 2): truncate-before-append + active-segment write path.
  // Guarded on tot_ != nullptr (wired by a later Phase 2 task) so this stays
  // fully inert -- falling back to appendLegacy_(), the original single-file
  // behavior -- until pending()/ackThrough() are updated to read segments
  // (later Phase 2 tasks); otherwise rows written here would never be read
  // back. See the P2-T5 implementation report for the full rationale.
  void append(const QRow& rowIn) {
#ifndef NATIVE_TEST
    // RISK-04 phase-2: the legacy fallback itself is out of scope for the
    // fault-injection harness (see the #ifndef NATIVE_TEST guard around its
    // definition below) -- guarded here too so this call is never even
    // COMPILED under NATIVE_TEST (not just "never taken"), since native
    // tests always supply a real tot_ (FakeQueueOffsetCheckpoint) and never
    // exercise this pre-Phase-2 path at all.
    if (!tot_) { appendLegacy_(rowIn); return; }
#endif

    QRow row = rowIn;
    row.magic = QROW_MAGIC;
    row.crc32 = 0;
    row.crc32 = crc32_((uint8_t*)&row, sizeof(row));

    uint32_t activeSeg  = tot_->queueOffsetSegment();
    uint32_t goodOffset = tot_->queueOffsetOffset();

    // ---- ADR-003 segment rollover (Miki Wire hardening, Phase-0 finding F1) --
    // Activates the QUEUE_SEGMENT_ROWS cap that the segment architecture was
    // designed around but never enforced: once the active segment holds that
    // many confirmed rows, this row starts the next segment. The checkpoint
    // is NOT advanced here -- it moves only in the single setQueueOffset()
    // call after the durable write below, so an interruption anywhere in
    // between leaves the old checkpoint intact and the partially-created
    // next segment is exactly the "interrupted rollover" orphan that
    // cleanupOrphanSegments_()'s design comment has always described (and
    // deletes at next boot). With rollover live, ackThrough()'s existing
    // per-segment deletion finally reclaims acked flash -- closing the
    // partition-fills-in-~27h failure MW-001 hit in the field.
    if (goodOffset >= QUEUE_SEGMENT_ROWS * (uint32_t)sizeof(QRow)) {
      activeSeg  += 1;
      goodOffset  = 0;
    }
    String path = segmentPath_(activeSeg);

    // ---- determine actual on-disk size --------------------------------------
    // FILE_APPEND creates the segment file if this is its first row (same,
    // already-proven behavior the original LOG_PATH append() relied on) and
    // never truncates existing content on open. RISK-04 phase-2: routed
    // through backend_->appendOpenSize(), identical semantics to the
    // pre-refactor direct "open FILE_APPEND, read size, close" sequence.
    long actualSizeL = backend_->appendOpenSize(path.c_str());
    if (actualSizeL < 0) {
      Serial.println("[Q] segment open FAILED");
      recordWriteFailure_(QFAIL_SEGMENT_OPEN);
      return;
    }
    uint32_t actualSize = (uint32_t)actualSizeL;

    // ---- truncate-before-append ----------------------------------------------
    // Compare actual on-disk size to the last CONFIRMED good offset. If the
    // file is larger, a prior append was torn (power loss between the write
    // and its checkpoint) -- discard the unconfirmed tail via truncateTo()
    // BEFORE any new bytes are written. No row is ever appended on top of
    // unconfirmed bytes (ADR-003). RISK-04 phase-2: backend_->truncateTo()
    // wraps the same "/littlefs"-prefixed POSIX truncate() call the
    // pre-refactor code made directly (see LittleFsBackend::truncateTo in
    // storage_backend.h) -- production behavior is unchanged; a native test
    // can now deterministically fail this specific call.
    if (actualSize > goodOffset) {
      // ---- truncate guard (Miki Wire hardening, Phase-0 finding F4) ---------
      // A genuine torn tail can never exceed one row: every successful
      // append truncates the previous tail before writing, and every
      // confirmed row advances the checkpoint. Finding MORE than one row of
      // "unconfirmed" bytes therefore means the CHECKPOINT went backwards
      // (e.g. both dual-slot copies failed CRC at boot and recovery zeroed
      // it) -- the bytes beyond the checkpoint are overwhelmingly real,
      // previously-confirmed telemetry. Blind truncation here is what turns
      // a 36-byte checkpoint fault into multi-megabyte data destruction.
      // Prove safety before the destructive operation: adopt every
      // contiguous CRC-valid row, and truncate only the genuinely invalid
      // tail beyond them.
      if (actualSize - goodOffset > (uint32_t)sizeof(QRow)) {
        uint32_t adopted = adoptValidRows_(path.c_str(), goodOffset, actualSize);
        Serial.printf("[Q] CRITICAL: checkpoint regression on %s -- confirmed "
                      "offset %u but %u bytes on disk; adopted %u bytes of "
                      "valid rows instead of truncating them (pending-count "
                      "diagnostic may read low until next reboot)\n",
                      path.c_str(), (unsigned)goodOffset,
                      (unsigned)actualSize, (unsigned)adopted);
        goodOffset = adopted;
      }
      if (actualSize > goodOffset &&
          !backend_->truncateTo(path.c_str(), goodOffset)) {
        Serial.printf("[Q] truncate FAILED path=%s to=%u\n",
                      path.c_str(), (unsigned)goodOffset);
        recordWriteFailure_(QFAIL_TRUNCATE);
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
    long written = backend_->appendWrite(path.c_str(), (uint8_t*)&row, sizeof(row));
    lastRowWriteLatencyMs_ = millis() - sdWriteStart;
    haveRowWriteLatency_ = true;
    if (written < 0) {
      // DM-Phase 1 validation-pass fix: a slow-then-failing open is exactly
      // the SD trouble this metric exists to surface -- recorded above
      // regardless of outcome, matching this comment block's own existing
      // claim.
      Serial.println("[Q] segment append open FAILED");
      recordWriteFailure_(QFAIL_WRITE_SHORT);
      return;
    }
    if ((size_t)written != sizeof(row)) {
      // Short/failed write -- treat exactly like a torn write: do NOT
      // advance the checkpoint. The next append()'s truncate-before-append
      // check cleans up whatever partial bytes landed.
      Serial.printf("[Q] segment write SHORT (%u/%u bytes) -- checkpoint not advanced\n",
                    (unsigned)written, (unsigned)sizeof(row));
      // P0-4 remediation (RISK-04): this IS the silent-loss moment this
      // fix exists for -- a short/failed write here means this specific
      // telemetry sample (the totalizer value + timestamp this loop
      // iteration captured) is gone; the totalizer itself is untouched and
      // keeps counting (by design, see totalizer.h), so this failure record
      // is the ONLY durable trace that a sample was lost at this point.
      recordWriteFailure_(QFAIL_WRITE_SHORT);
      return;
    }

    // ---- checkpoint update (AFTER the row is durably flushed) ----------------
    tot_->setQueueOffset(activeSeg, goodOffset + sizeof(QRow));
    unackedCount_++;   // DM-Phase 1: this row is now durably pending
  }

#ifndef NATIVE_TEST
  // RISK-04 phase-2: everything from here to the matching #endif below (the
  // legacy single-file fallback path, segment-aware pending()/ackThrough(),
  // orphan-segment cleanup, and the backlog boot-scan) is OUT OF SCOPE for
  // this fault-injection harness -- unchanged from before this phase,
  // direct-LittleFS, compile-verified only. See
  // 02_FLASH_FAULT_INJECTION_DESIGN.md for the scope rationale. Excluded
  // from NATIVE_TEST builds entirely (rather than left in and simply
  // unused) so this header does not require LittleFS/File to exist on host
  // for logic this pass does not touch or claim to test.

  // Pre-Phase-2 behavior, preserved verbatim as the fallback path while
  // tot_ is not yet wired. Identical to the original append().
  void appendLegacy_(const QRow& rowIn) {
    QRow row = rowIn;
    row.magic = QROW_MAGIC;
    row.crc32 = 0;
    row.crc32 = crc32_((uint8_t*)&row, sizeof(row));
    File f = LittleFS.open(LOG_PATH, FILE_APPEND);
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
      File f = LittleFS.open(path, FILE_READ);
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
    File f = LittleFS.open(LOG_PATH, FILE_READ);
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
    const uint32_t firstSeg = seg;   // rollover hardening: lowest segment this
                                     // ack could possibly free -- bounds the
                                     // deletion loop below (anything lower was
                                     // freed by an earlier ack; crash-stranded
                                     // leftovers are swept at boot instead)
    const uint32_t activeSeg = tot_->queueOffsetSegment();
    // DM-Phase 1: counts every row this walk consumes (torn/junk OR
    // genuinely newly-acked) -- exactly the rows pending()'s own skip logic
    // would no longer return, so this is the correct, exact decrement for
    // the maintained backlog counter below.
    uint32_t consumedRows = 0;

    while (seg <= activeSeg) {
      String path = segmentPath_(seg);
      File f = LittleFS.open(path, FILE_READ);
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

    // Rollover hardening: bounded to the segments THIS ack's walk actually
    // crossed ([firstSeg, seg)), not [0, seg) -- with real rollover the old
    // unbounded range would re-attempt thousands of removes of long-gone
    // files on every ack. A segment stranded by a crash between
    // persistAck_() above and its remove below is swept at the next boot
    // (cleanupOrphanSegments_'s below-cursor sweep), preserving the original
    // "never deleted before its cursor advance is durable" guarantee.
    for (uint32_t s = firstSeg; s < seg; s++) {
      LittleFS.remove(segmentPath_(s));   // range is always < seg <= activeSeg --
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

  // MW-001 queue storage recovery: deletes the single, never-rotated
  // segment file (proven via live `ls` enumeration to be the sole segment,
  // consuming the entire queue partition) and resets ONLY the write-
  // position cursors -- Totalizer's q_segment/q_offset (via the existing
  // setQueueOffset(0,0)) and this queue's own cursor_segment/cursor_offset
  // -- to 0, so future appends start a fresh, empty segment 0. Deliberately
  // does NOT touch ack_.acked_seq (already correctly fast-forwarded by
  // resetAckToCurrentPosition in a prior step) or any NVS/totalizer.total
  // state. Persists via the existing persistAck_() verbatim -- no new
  // persistence mechanism. Explicit operator command only (provision.h's
  // `recover_queue`), never automatic.
  void recoverQueueStorage() {
    if (!tot_) return;
    LittleFS.remove(segmentPath_(0));
    tot_->setQueueOffset(0, 0);
    ack_.cursor_segment = 0;
    ack_.cursor_offset  = 0;
    persistAck_();
  }
#endif  // NATIVE_TEST -- see the matching #ifndef above appendLegacy_()

  // DM-Phase 1 (local diagnostics, §13 A.3 /api/v1/status "queue" object) --
  // maintained counter, NOT a rescan -- see append()/ackThrough() above for
  // where it's kept in sync. Only accurate once tot_ != nullptr (segment-
  // aware path, which is the only live path per current covio_firmware.ino
  // wiring); the legacy path never updates it, matching every other
  // dual-path split in this class.
  uint32_t pendingCount() { return unackedCount_; }
  uint32_t ackedSeq()     { return ack_.acked_seq; }

  // MW-001 commissioning reset: fast-forwards the ack cursor (and its
  // persisted read cursor) to the CURRENT write position, so pending()
  // stops filtering newly-built records against a stale acked_seq
  // inherited from a prior backend relationship. Explicit, operator-
  // triggered only (provision.h's `reset_ack` command) -- never automatic.
  // Reuses persistAck_() verbatim -- no new persistence mechanism. Does
  // NOT touch the Totalizer's own checkpoint (total/seq) at all -- lifetime
  // pulse count history is completely unaffected by this call.
  void resetAckToCurrentPosition(uint32_t currentSeq) {
    if (!tot_) return;
    ack_.acked_seq      = currentSeq;
    ack_.cursor_segment = tot_->queueOffsetSegment();
    ack_.cursor_offset  = tot_->queueOffsetOffset();
    unackedCount_ = 0;   // everything up to currentSeq is now acked/abandoned
    persistAck_();
  }

  // DM-Phase 1 (local diagnostics, §13 A.3 /api/v1/metrics) -- "have" flags
  // distinguish "never measured yet this boot" from a genuine 0ms result.
  bool     haveRowWriteLatency()  { return haveRowWriteLatency_; }
  uint32_t lastRowWriteLatencyMs() { return lastRowWriteLatencyMs_; }
  bool     havePendingLatency()   { return havePendingLatency_; }
  uint32_t lastPendingLatencyMs() { return lastPendingLatencyMs_; }

  // P0-4 remediation (RISK-04): remotely-visible failure state -- consumed
  // by diagnostics.h's alarm builder so a lost measurement is loud (an
  // operator-visible CRITICAL alarm), never silent. hasFailedWrite() is
  // true from the moment ANY failure has ever occurred (this boot or a
  // prior one, recovered via begin() above) and stays true forever --
  // deliberately monotonic, never auto-cleared by later successful writes
  // (see this file's FAIL_MAGIC header comment for why).
  bool     hasFailedWrite()        { return fail_.failed_write_count > 0; }
  uint32_t failedWriteCount()      { return fail_.failed_write_count; }
  uint32_t firstFailureUptimeS()   { return fail_.first_failure_uptime_s; }
  uint32_t lastFailureUptimeS()    { return fail_.last_failure_uptime_s; }
  const char* lastFailureCodeStr() { return failCodeStr_((QueueFailCode)fail_.last_error_code); }

  // Real filesystem-reported usage, NOT an estimated row-count percentage --
  // LittleFS.usedBytes()/totalBytes() reflect actual on-flash occupancy
  // (queue segments + ack/checkpoint/failure-state files + LittleFS's own
  // metadata), so this is accurate regardless of any per-row overhead
  // assumption. See 06_FLASH_LIFETIME_ANALYSIS.md for why an estimated
  // capacity constant was rejected in favor of this.
  // RISK-04 phase-2: no longer static -- reads via backend_ (real
  // LittleFsBackend in production, FakeStorageBackend in native tests) so
  // capacity-threshold behavior (80/90/95/100%) is genuinely testable, not
  // just compile-verified.
  float capacityPercentUsed() {
    size_t total = backend_->totalBytes();
    if (total == 0) return 0.0f;
    return (100.0f * (float)backend_->usedBytes()) / (float)total;
  }

private:
  static const char* failCodeStr_(QueueFailCode c) {
    switch (c) {
      case QFAIL_SEGMENT_OPEN: return "segment_open_failed";
      case QFAIL_TRUNCATE:     return "truncate_failed";
      case QFAIL_WRITE_SHORT:  return "write_failed_or_short";
      default:                 return "none";
    }
  }

  // Persists ONLY when called (i.e. only on an actual failure) -- never on
  // the hot per-second append path, so this adds no routine wear (see
  // FAIL_MAGIC's header comment and 06_FLASH_LIFETIME_ANALYSIS.md).
  void recordWriteFailure_(QueueFailCode code) {
    uint32_t nowS = millis() / 1000;
    if (fail_.failed_write_count == 0) fail_.first_failure_uptime_s = nowS;
    fail_.failed_write_count++;
    fail_.last_failure_uptime_s = nowS;
    fail_.last_error_code = code;
    persistFail_();
  }

  // RISK-04 phase-2: routed through StorageBackend::readWhole/writeWhole,
  // the same fixed-size-whole-record primitive used for AckRec below --
  // identical on-disk behavior to the pre-refactor direct LittleFS calls
  // (same FILE_READ/FILE_WRITE semantics), now fault-injectable on host.
  bool loadFail_(const char* path, FailureState& out) {
    long got = backend_->readWhole(path, (uint8_t*)&out, sizeof(out));
    if (got != (long)sizeof(FailureState)) return false;
    if (out.magic != FAIL_MAGIC) return false;
    uint32_t want = out.crc32; out.crc32 = 0;
    bool ok = (crc32_((uint8_t*)&out, sizeof(out)) == want);
    out.crc32 = want; return ok;
  }

  void persistFail_() {
    fail_.magic = FAIL_MAGIC;
    fail_.writes++;
    const char* path = (fail_.writes & 1) ? FAIL_PATH_A : FAIL_PATH_B;
    fail_.crc32 = 0;
    fail_.crc32 = crc32_((uint8_t*)&fail_, sizeof(fail_));
    // Deliberately NOT gated on this same write path succeeding -- if the
    // filesystem is unable to even persist a 28-byte failure record, that
    // is itself surfaced via this call's own Serial log (unchanged
    // existing behavior: callers already log to Serial before calling
    // this); there is no more-durable fallback to escalate to on this
    // hardware, and retrying indefinitely here would risk the failure path
    // itself blocking the main loop.
    if (!backend_->writeWhole(path, (uint8_t*)&fail_, sizeof(fail_))) {
      Serial.println("[Q] failure-state checkpoint open FAILED");
    }
  }


  static uint32_t crc32_(const uint8_t* d, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
      crc ^= d[i];
      for (int k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return ~crc;
  }

  // Miki Wire hardening (Phase-0 finding F4): scans `path` forward from
  // `from` in whole-QRow strides, validating magic+CRC exactly as
  // pendingImpl_() does, and returns the end offset of the last contiguous
  // valid row (>= from). Bytes beyond the returned offset failed validation
  // -- a genuine torn/garbage tail, safe to truncate. Reads through backend_
  // in multi-row chunks so recovering even a multi-MB segment is bounded
  // I/O per call, and so the fault-injection harness can exercise this path
  // (readAt is part of the StorageBackend seam). Only ever called on the
  // regression paths (append()'s truncate guard, boot reconciliation) --
  // never on the per-second hot path.
  uint32_t adoptValidRows_(const char* path, uint32_t from, uint32_t fileSize) {
    uint8_t buf[sizeof(QRow) * 32];
    uint32_t off = from;
    while (off + (uint32_t)sizeof(QRow) <= fileSize) {
      size_t want = fileSize - off;
      if (want > sizeof(buf)) want = sizeof(buf);
      want -= want % sizeof(QRow);
      long got = backend_->readAt(path, off, buf, want);
      if (got < (long)sizeof(QRow)) break;   // read failure/EOF: adopt what we have
      size_t rows = (size_t)got / sizeof(QRow);
      bool stop = false;
      for (size_t i = 0; i < rows; i++) {
        QRow r;
        memcpy(&r, buf + i * sizeof(QRow), sizeof(r));
        if (r.magic != QROW_MAGIC) { stop = true; break; }
        uint32_t wantCrc = r.crc32; r.crc32 = 0;
        if (crc32_((uint8_t*)&r, sizeof(r)) != wantCrc) { stop = true; break; }
        off += (uint32_t)sizeof(QRow);
      }
      if (stop) break;
    }
    return off;
  }

  // RISK-04 phase-2: routed through StorageBackend, same rationale as
  // loadFail_/persistFail_ above -- AckRec recovery happens in the SAME
  // begin() call as FailureState recovery, so both must work identically
  // whether backend_ is a real LittleFsBackend or a native-test fake.
  bool loadAck_(const char* path, AckRec& out) {
    long got = backend_->readWhole(path, (uint8_t*)&out, sizeof(out));
    if (got != (long)sizeof(AckRec)) return false;
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
    backend_->writeWhole(path, (uint8_t*)&ack_, sizeof(ack_));
  }

#ifndef NATIVE_TEST
  // RISK-04 phase-2: out of scope for this harness, same rationale as the
  // public-section guard above.
  // Physically shrink the log only when everything is acked — safe point.
  void maybeCompact_() {
    if (!empty()) return;
    if (LittleFS.exists(LOG_PATH)) LittleFS.remove(LOG_PATH);   // fully drained; start fresh
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
      File f = LittleFS.open(path, FILE_READ);
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
#endif  // NATIVE_TEST -- see the matching #ifndef above maybeCompact_()

  AckRec ack_{};
  FailureState fail_{};   // P0-4 remediation (RISK-04)
  IQueueOffsetCheckpoint* tot_ = nullptr;   // ADR-003 (Phase 2): may be null until P2-T8.
                                            // RISK-04 phase-2: narrowed from Totalizer* to
                                            // this dependency-free interface (see
                                            // queue_offset_checkpoint.h) so a native host
                                            // test can supply a fake without a real Totalizer.
  StorageBackend* backend_ = nullptr;      // RISK-04 phase-2: real LittleFsBackend in
                                            // production (see the NATIVE_TEST-guarded begin()
                                            // overload above), FakeStorageBackend in tests.
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
  //
  // RISK-04 phase-2: guarded out under NATIVE_TEST along with
  // cleanupOrphanSegments_() below (its only caller) -- it uses
  // String::indexOf()/length()/operator[], which the minimal native-test
  // arduino_shim.h deliberately does NOT implement (out of scope for this
  // harness; see 02_FLASH_FAULT_INJECTION_DESIGN.md). segmentPath_() above
  // stays unguarded because append() (in scope) needs it, and it only uses
  // the small String subset the shim DOES implement.
#ifndef NATIVE_TEST
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

  // Boot-time segment reconciliation (rollover hardening extends the
  // original orphan cleanup). Three concerns, in safety order:
  //
  //  1. CHECKPOINT-REGRESSION DETECTION: a legitimately interrupted rollover
  //     creates EXACTLY activeSeg+1 (the checkpoint advances before a second
  //     rollover can ever start). Segment ids beyond activeSeg+1 mean the
  //     checkpoint went BACKWARDS (both slots corrupt -> zeroed recovery) --
  //     those "orphans" are real, live data, and deleting them would be the
  //     boot-time twin of append()'s blind-truncate hazard. Adopt the
  //     highest segment as active (valid-row prefix as confirmed offset),
  //     delete nothing this boot.
  //  2. INTERRUPTED-ROLLOVER ORPHAN: exactly activeSeg+1, deleted as always
  //     (its rows were never confirmed -- their seqs regenerate).
  //  3. BELOW-CURSOR STRAYS: segments below ack_.cursor_segment are fully
  //     acknowledged by the durable-cursor-before-delete ordering; one can
  //     survive only via a crash between persistAck_() and its remove in
  //     ackThrough(). Swept here since ackThrough()'s delete loop is now
  //     bounded to the segments each ack actually crosses.
  //
  // Two-pass by design: directory-mutation-during-iteration safety is
  // unverified for this codebase's SD/VFS toolchain. Pass 1 only
  // enumerates; Pass 2 only deletes, after the directory handle is closed.
  // A no-op if tot_ is null (pre-P2-T8).
  void cleanupOrphanSegments_() {
    if (!tot_) return;
    uint32_t activeSeg = tot_->queueOffsetSegment();

    uint32_t deleteIds[MAX_ORPHAN_CANDIDATES];
    int deleteCount = 0;
    uint32_t maxSegSeen = 0;
    bool     anySeg = false;

    File dir = LittleFS.open(SD_QUEUE_DIR);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File f = dir.openNextFile();
    while (f) {
      bool isDir = f.isDirectory();
      String name = String(f.name());
      f.close();
      if (!isDir) {
        uint32_t id;
        if (parseSegmentId_(name, id)) {
          if (!anySeg || id > maxSegSeen) maxSegSeen = id;
          anySeg = true;
          if (id > activeSeg || id < ack_.cursor_segment) {
            if (deleteCount < MAX_ORPHAN_CANDIDATES) {
              deleteIds[deleteCount++] = id;
            } else {
              Serial.println("[Q] segment cleanup candidate list full -- "
                              "remainder will be retried next boot");
            }
          }
        }
      }
      f = dir.openNextFile();
    }
    dir.close();   // directory handle fully closed before any delete

    if (anySeg && maxSegSeen > activeSeg + 1) {
      // Concern 1: checkpoint regression. Adopt, don't delete.
      String p = segmentPath_(maxSegSeen);
      File sf = LittleFS.open(p, FILE_READ);
      uint32_t sz = sf ? (uint32_t)sf.size() : 0;
      if (sf) sf.close();
      uint32_t adopted = adoptValidRows_(p.c_str(), 0, sz);
      Serial.printf("[Q] CRITICAL: checkpoint regression at boot -- active "
                    "segment is %u but segments up to %u exist on flash; "
                    "adopting seg=%u offset=%u as the live frontier; no "
                    "segment deletion this boot\n",
                    (unsigned)activeSeg, (unsigned)maxSegSeen,
                    (unsigned)maxSegSeen, (unsigned)adopted);
      tot_->setQueueOffset(maxSegSeen, adopted);
      return;
    }

    for (int i = 0; i < deleteCount; i++) {
      LittleFS.remove(segmentPath_(deleteIds[i]));
    }
  }
#endif  // NATIVE_TEST -- see the matching #ifndef above cleanupOrphanSegments_()
};
