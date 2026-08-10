// ============================================================================
// totalizer.h  —  hardware pulse counter + crash-safe persistent total
// ----------------------------------------------------------------------------
// WHY PCNT: counting pulses in a GPIO interrupt drops counts under WiFi load
// (your Phase 6 proved this). The ESP32 PCNT peripheral counts in hardware, so
// the total is exact regardless of what the CPU is doing.
//
// CRASH SAFETY: the running total is checkpointed to internal flash (LittleFS,
// no SD card on this unit) in a fixed-size CRC32 record, written alternately
// to TWO slots (ping-pong). On boot we load the newest slot that passes CRC. A
// power cut mid-write corrupts at most ONE slot; the other still holds the
// previous good value. A torn write fails CRC and is skipped — it never
// yields a garbage number. (Architecture Invariant 3.)
//
// NOTE: totalizer stores RAW PULSES, never litres. Litres are derived from
// pulses / K-factor, and K lives on the server. This is what lets you
// re-calibrate historical data by editing K on the website. See telemetry.h.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "driver/pcnt.h"
#include "config.h"
#include "queue_offset_checkpoint.h"

#define PCNT_UNIT_USED   PCNT_UNIT_0
#define CKPT_MAGIC       0xC0A17071UL
#define CKPT_PATH_A      "/totA.bin"
#define CKPT_PATH_B      "/totB.bin"

struct __attribute__((packed)) Checkpoint {
  uint32_t magic;
  uint64_t total;      // cumulative raw pulses, lifetime
  uint32_t boot_id;
  uint32_t seq;        // last telemetry seq committed (see telemetry.h)
  uint32_t writes;     // monotonically increasing -> newest slot wins
  // ---- ADR-003 (Phase 2): persisted queue write-offset -------------------
  // "Last known good write offset" for EventQueue's active segment, reusing
  // this struct's existing dual-slot CRC checkpoint mechanism (see
  // Docs/PHASE2_DESIGN_REPORT.md §3.2). Set via setQueueOffset() below.
  // NOT YET CONSUMED by append()/truncate-before-append logic — that
  // consumer is a later Phase 2 task (P2-T5).
  uint32_t q_segment;  // active segment id the offset below applies to
  uint32_t q_offset;   // byte offset of the last confirmed-good write in it
  uint32_t crc32;      // over all prior bytes
};

// ---- ADR-003 (Phase 2) MIGRATION ONLY: pre-Phase-2 checkpoint layout ------
// 28 bytes -- the exact on-disk shape every device wrote before this
// upgrade. READ-ONLY: nothing after Phase 2 ever writes this layout again.
// Exists solely so an upgrading device's `total` (lifetime pulse count) is
// never silently zeroed by the new Checkpoint's size change -- see
// Totalizer::loadLegacy_() below.
struct __attribute__((packed)) LegacyCheckpoint {
  uint32_t magic;
  uint64_t total;
  uint32_t boot_id;
  uint32_t seq;
  uint32_t writes;
  uint32_t crc32;
};

// RISK-04 phase-2 remediation: IQueueOffsetCheckpoint (the only three
// Totalizer methods EventQueue/queue.h ever calls) now lives in its own
// header (queue_offset_checkpoint.h), included above, specifically so it has
// zero Arduino/hardware dependency and a native host test can supply a
// FakeQueueOffsetCheckpoint instead of a real Totalizer (which depends on
// the PCNT hardware peripheral and cannot be constructed off-target).
// Totalizer's own behavior is completely unchanged by this -- it is a pure
// "implements this interface too" addition, not a redesign; every existing
// caller that uses a bare `Totalizer*`/`Totalizer&` continues to compile and
// behave identically.
class Totalizer : public IQueueOffsetCheckpoint {
public:
  void begin(uint32_t bootId) {
    setupPCNT_();
    // Recover newest CRC-valid checkpoint from either slot. Each slot is
    // tried as the NEW format first; only if that fails is it tried as the
    // pre-Phase-2 LEGACY format (loadLegacy_ upconverts it in-memory,
    // preserving total/boot_id/seq/writes and defaulting the two new
    // fields to 0 -- safe here because a genuine pre-Phase-2 device has no
    // seg_*.bin files yet at the moment of this migration).
    Checkpoint a, b;
    bool va = load_(CKPT_PATH_A, a);
    bool vb = load_(CKPT_PATH_B, b);
    bool legA = false, legB = false;
    if (!va) { va = loadLegacy_(CKPT_PATH_A, a); legA = va; }
    if (!vb) { vb = loadLegacy_(CKPT_PATH_B, b); legB = vb; }

    bool usedLegacy;
    if (va && vb) {
      bool useA = (a.writes >= b.writes);
      cp_ = useA ? a : b;
      usedLegacy = useA ? legA : legB;
    }
    else if (va) { cp_ = a; usedLegacy = legA; }
    else if (vb) { cp_ = b; usedLegacy = legB; }
    else { memset(&cp_, 0, sizeof(cp_)); cp_.magic = CKPT_MAGIC; usedLegacy = false; }

    cp_.boot_id = bootId;
    base_ = cp_.total;          // total accumulated before this power-up

    if (usedLegacy) {
      // Re-persist immediately in the NEW format so the legacy layout is
      // retired as soon as possible. Idempotent and safe if interrupted --
      // dual-slot CRC guarantees the untouched legacy slot survives a crash
      // here, and loadLegacy_() simply succeeds again on the next boot.
      Serial.printf("[TOT] migrated legacy checkpoint -> total preserved = %llu\n",
                    (unsigned long long)base_);
      persist_();
    }
    Serial.printf("[TOT] recovered total=%llu writes=%u\n",
                  (unsigned long long)base_, cp_.writes);
  }

  // lifetime raw pulses = persisted base + what PCNT has counted since boot
  uint64_t total() {
    int16_t c = 0;
    pcnt_get_counter_value(PCNT_UNIT_USED, &c);
    return base_ + accumulated_ + (uint16_t)c;
  }

  // Call periodically. PCNT is 16-bit; we drain it into a 64-bit accumulator
  // well before it can wrap. Then checkpoint the total to SD (ping-pong).
  void service(uint32_t seq) {
    int16_t c = 0;
    pcnt_get_counter_value(PCNT_UNIT_USED, &c);
    if ((uint16_t)c >= 30000) {          // drain before 16-bit wrap
      accumulated_ += (uint16_t)c;
      pcnt_counter_clear(PCNT_UNIT_USED);
    }
    cp_.total  = base_ + accumulated_;
    int16_t c2 = 0; pcnt_get_counter_value(PCNT_UNIT_USED, &c2);
    cp_.total += (uint16_t)c2;
    cp_.seq    = seq;
    persist_();
  }

  uint32_t lastSeq() { return cp_.seq; }

  // ---- CT-clamp enhancement (SENSOR_MODE_CT builds only) -------------------
  // Feeds software-synthesized pulses (sensor_ct.h's time-integrated
  // current-presence stream) into the SAME accumulator the PCNT drain path
  // adds to -- so total()/service()'s existing checkpoint/recovery machinery
  // persists them with zero further changes. The only call site lives behind
  // `#if SENSOR_MODE == SENSOR_MODE_CT` in covio_firmware.ino; an NPN build
  // never calls this and its PCNT hardware path is untouched either way.
  void injectSoftPulses(uint32_t n) { accumulated_ += n; }

  // ---- ADR-003 (Phase 2): queue write-offset checkpoint accessors --------
  // setQueueOffset() persists synchronously (same pattern as ackThrough()'s
  // synchronous persist in queue.h) via the existing dual-slot CRC persist_().
  // The getters serve EventQueue::begin()'s recovery step.
  void setQueueOffset(uint32_t segment, uint32_t offset) {
    cp_.q_segment = segment;
    cp_.q_offset  = offset;
    persist_();
  }
  uint32_t queueOffsetSegment() const { return cp_.q_segment; }
  uint32_t queueOffsetOffset()  const { return cp_.q_offset; }

private:
  void setupPCNT_() {
    // PRODUCTION BOARD UPDATE (8DI-8DO retarget): PIN_PULSE is now the DI1
    // terminal's GPIO (see config.h's retarget note); the ONBOARD
    // bidirectional optocoupler replaces the external module below, but the
    // electrical reasoning for INPUT_PULLUP is unchanged (opto output stage
    // sinks the GPIO low when active, needs a defined idle-high level).
    // Historical wiring note (Waveshare ESP32-S3-Relay-1CH unit): PIN_PULSE
    // (GPIO1/"IO1") was fed by a generic PC817 opto-isolator module's
    // open-collector output -- idles floating/undefined with nothing
    // externally pulling it up, and pulls LOW when the isolator's LED side
    // triggers. Enabling the internal pull-up here means only the
    // isolator's OUT + GND need to reach this pin -- no separate "VCC" wire
    // into the isolator's output side, sidestepping the ambiguity of an
    // unlabeled generic module's output pin identity entirely. Must be set
    // before pcnt_unit_config() below, which does NOT touch pull state.
    pinMode(PIN_PULSE, INPUT_PULLUP);

    pcnt_config_t cfg = {};
    cfg.pulse_gpio_num = PIN_PULSE;
    cfg.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
    cfg.channel        = PCNT_CHANNEL_0;
    cfg.unit           = PCNT_UNIT_USED;
    cfg.pos_mode       = PCNT_COUNT_INC;   // count rising edges
    cfg.neg_mode       = PCNT_COUNT_DIS;
    cfg.lctrl_mode     = PCNT_MODE_KEEP;
    cfg.hctrl_mode     = PCNT_MODE_KEEP;
    cfg.counter_h_lim  = 32767;
    cfg.counter_l_lim  = 0;
    pcnt_unit_config(&cfg);
    // hardware glitch filter — rejects noise shorter than the run threshold
    pcnt_set_filter_value(PCNT_UNIT_USED, (uint16_t)(PCNT_GLITCH_NS / 12.5)); // APB ticks
    pcnt_filter_enable(PCNT_UNIT_USED);
    pcnt_counter_pause(PCNT_UNIT_USED);
    pcnt_counter_clear(PCNT_UNIT_USED);
    pcnt_counter_resume(PCNT_UNIT_USED);
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

  bool load_(const char* path, Checkpoint& out) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f || f.size() != sizeof(Checkpoint)) { if (f) f.close(); return false; }
    f.read((uint8_t*)&out, sizeof(out));
    f.close();
    if (out.magic != CKPT_MAGIC) return false;
    uint32_t want = out.crc32;
    out.crc32 = 0;
    bool ok = (crc32_((uint8_t*)&out, sizeof(out)) == want);
    out.crc32 = want;
    return ok;
  }

  // ADR-003 (Phase 2) migration: loads and validates an OLD-format (28-byte)
  // checkpoint at `path`. On success, upconverts it into a NEW-format
  // Checkpoint (`out`), preserving total/boot_id/seq/writes exactly and
  // defaulting q_segment/q_offset to 0. `out.crc32` is left 0 here -- this
  // is an in-memory, not-yet-persisted value; persist_() computes and sets
  // it when begin() re-persists this in the new format.
  bool loadLegacy_(const char* path, Checkpoint& out) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f || f.size() != sizeof(LegacyCheckpoint)) { if (f) f.close(); return false; }
    LegacyCheckpoint legacy;
    f.read((uint8_t*)&legacy, sizeof(legacy));
    f.close();
    if (legacy.magic != CKPT_MAGIC) return false;
    uint32_t want = legacy.crc32;
    legacy.crc32 = 0;
    bool ok = (crc32_((uint8_t*)&legacy, sizeof(legacy)) == want);
    if (!ok) return false;

    memset(&out, 0, sizeof(out));
    out.magic     = CKPT_MAGIC;
    out.total     = legacy.total;
    out.boot_id   = legacy.boot_id;
    out.seq       = legacy.seq;
    out.writes    = legacy.writes;   // preserved -> A/B alternation parity
                                      // stays continuous across the upgrade
    out.q_segment = 0;
    out.q_offset  = 0;
    return true;
  }

  void persist_() {
    cp_.magic  = CKPT_MAGIC;
    cp_.writes++;
    const char* path = (cp_.writes & 1) ? CKPT_PATH_A : CKPT_PATH_B; // alternate
    cp_.crc32 = 0;
    cp_.crc32 = crc32_((uint8_t*)&cp_, sizeof(cp_));
    File f = LittleFS.open(path, FILE_WRITE);   // FILE_WRITE truncates+rewrites this slot
    if (!f) { Serial.println("[TOT] checkpoint open FAILED"); return; }
    f.write((uint8_t*)&cp_, sizeof(cp_));
    f.flush();
    f.close();
  }

  Checkpoint cp_{};
  uint64_t base_ = 0;         // total before this boot
  uint64_t accumulated_ = 0;  // drained-out counts this boot
};
