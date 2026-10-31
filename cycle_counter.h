// ============================================================================
// cycle_counter.h  —  confirmed-state cycle counting (Covio D-315)
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- the same
// "pure class extracted for testability" pattern as sensor_stuck.h and
// pulse_plausibility.h. Host-tested by test/native_cpp/test_cycle_counter.cpp.
//
// WHY THIS EXISTS. The PCNT path counts EVERY rising edge longer than a 1 µs
// glitch filter. That is exact for a clean signal and wrong for a real one:
//   · Pravat Press 1 (30 Oct 2026): one stroke produced up to 348 counts --
//     the ram vibrating at the edge of the proximity sensor's range;
//   · Wire Line 1 from 28 Oct 12:00: bursts of 1,000-5,000 counts in one
//     5-second reading after a sensor/mount change.
// A count of edges cannot tell a flicker from a stroke. A count of CONFIRMED
// STATE CHANGES can: the input must HOLD its new level before it is believed,
// and a machine cannot complete a second cycle faster than its own minimum.
//
// THE RULE (per machine, delivered by the config poll, stored in NVS):
//   · the input must stay at its new level for `onStableMs` (rising) or
//     `offStableMs` (falling) before the level is CONFIRMED -- a flicker that
//     reverses inside that window never changes the confirmed state;
//   · one cycle is counted each time the confirmed level becomes HIGH -- the
//     same edge PCNT counts (PCNT_COUNT_INC on the positive edge), so a clean
//     signal gives the identical total in either mode;
//   · a confirmed HIGH less than `minGapMs` after the last counted one is NOT
//     a cycle: no hand-fed press strokes twice in 1.5 s, no wire line passes
//     ten pulses in 100 ms.
//
// WHAT IS REPORTED, NOT HIDDEN. `rawRises()` counts every rising change the
// sampler saw; `rawRises() - counted()` is what was rejected. The server shows
// it, so a sensor drifting toward the edge of its range is visible the same
// day instead of becoming production.
//
// STROKE TIMES (Covio D-320, firmware 1.6.0). Totals every few seconds cannot
// say HOW a count happened: two strokes 4 s apart and one stroke seen twice
// 2 s apart both read "2" in a 5-second report. So every decision is also kept
// in a small ring: the sample time of each counted cycle and of each confirmed
// HIGH refused as too soon, with a running index. The push sends the newest
// `kEventLog` entries every time (overlapping, de-duplicated by index on the
// server), so a failed push loses nothing and the server sees the exact rhythm.
//
// LIMIT, STATED: the caller samples at ~1 ms. A real cycle whose HIGH lasts
// less than `onStableMs` is not seen -- settings are a commissioning fact,
// proven by a hand count, exactly as a K-factor is.
// ============================================================================
#pragma once
#include <stdint.h>

struct CycleSettings {
  uint16_t onStableMs = 0;
  uint16_t offStableMs = 0;
  uint16_t minGapMs = 0;
  /** All zero = the feature is off and the caller keeps the PCNT path. */
  bool enabled() const { return onStableMs != 0 || offStableMs != 0 || minGapMs != 0; }
};

/** One decision the counter made: a counted cycle, or a HIGH refused as too soon. */
struct CycleEvent {
  uint32_t index;   // running index over both kinds, from 1; 0 = empty slot
  uint32_t atMs;    // the sample time, same clock as sample()'s nowMs
  uint16_t highMs;  // how long the input had been HIGH when it was confirmed
  bool counted;     // false = refused as too soon
};

class CycleCounter {
 public:
  static const uint8_t kEventLog = 24;

  void configure(const CycleSettings& s) { settings_ = s; }
  const CycleSettings& settings() const { return settings_; }

  /**
   * Feed one sample of the input level at `nowMs` (a millis()-style clock;
   * wrap-safe). Returns true when this sample completes a counted cycle.
   */
  bool sample(bool level, uint32_t nowMs) {
    if (!started_) {
      started_ = true;
      confirmed_ = level;
      candidate_ = level;
      candidateSince_ = nowMs;
      return false;
    }
    if (level != candidate_) {
      candidate_ = level;
      candidateSince_ = nowMs;
      if (level) rawRises_++;
    }
    if (candidate_ == confirmed_) return false;
    const uint32_t need = candidate_ ? settings_.onStableMs : settings_.offStableMs;
    if ((uint32_t)(nowMs - candidateSince_) < need) return false;

    confirmed_ = candidate_;
    if (!confirmed_) return false;
    const uint16_t held = (uint16_t)((nowMs - candidateSince_) > 65535 ? 65535 : (nowMs - candidateSince_));
    if (haveCounted_ && (uint32_t)(nowMs - lastCountMs_) < settings_.minGapMs) {
      tooSoon_++;
      log_(nowMs, held, false);
      return false;
    }
    haveCounted_ = true;
    lastCountMs_ = nowMs;
    counted_++;
    log_(nowMs, held, true);
    return true;
  }

  /**
   * The newest events, oldest first, into `out` (room for kEventLog).
   * Returns how many were written.
   */
  uint8_t recentEvents(CycleEvent* out) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kEventLog; i++) {
      const CycleEvent& e = log_buf_[(uint8_t)((logNext_ + i) % kEventLog)];
      if (e.index != 0) out[n++] = e;
    }
    return n;
  }

  uint32_t counted() const { return counted_; }
  uint32_t rawRises() const { return rawRises_; }
  /** Confirmed HIGHs refused only because they came inside `minGapMs`. */
  uint32_t tooSoon() const { return tooSoon_; }
  bool confirmedLevel() const { return confirmed_; }

 private:
  CycleSettings settings_;
  bool started_ = false;
  bool confirmed_ = false;
  bool candidate_ = false;
  uint32_t candidateSince_ = 0;
  bool haveCounted_ = false;
  uint32_t lastCountMs_ = 0;
  uint32_t counted_ = 0;
  uint32_t rawRises_ = 0;
  uint32_t tooSoon_ = 0;
  CycleEvent log_buf_[kEventLog] = {};
  uint8_t logNext_ = 0;
  uint32_t logIndex_ = 0;

  void log_(uint32_t atMs, uint16_t highMs, bool counted) {
    CycleEvent& e = log_buf_[logNext_];
    e.index = ++logIndex_;
    e.atMs = atMs;
    e.highMs = highMs;
    e.counted = counted;
    logNext_ = (uint8_t)((logNext_ + 1) % kEventLog);
  }
};
