// ============================================================================
// pulse_plausibility.h — Miki Wire profile: pulse-rate plausibility monitor.
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- same "pure class
// extracted for testability" pattern as sensor_ct.h / sensor_stuck.h.
// Host-tested by test/native_cpp/test_pulse_plausibility.cpp. Compiled ONLY
// into MIKI_WIRE_PROFILE builds (see config.h / covio_firmware.ino) -- the
// Balaji flag-less build contains none of this code.
//
// WHAT THIS IS (Phase-0 finding F5): the NPN/PCNT path counts any edge
// slower than the 1µs hardware glitch filter as production. A wire-drawing
// line has a physical maximum line speed, hence a maximum plausible pulse
// frequency; EMI bursts, contact chatter, or a failing sensor faster than
// that were previously counted as produced wire with no flag anywhere.
// This monitor watches the totalizer's per-cycle rate and, when a
// configured ceiling is exceeded for several consecutive cycles, raises an
// advisory the caller stamps into the record's quality bitfield
// (QUALITY_SUSPECT_RATE) -- so the SERVER sees exactly which records were
// produced under suspect conditions.
//
// WHAT THIS IS NOT: it never subtracts, gates, or "corrects" counts. Raw
// pulse totals remain the ground truth (the architecture's core invariant);
// the K-factor authority stays server-side. An operator/analyst decides
// what to do with flagged records -- firmware only observes and reports.
//
// CONFIGURATION: maxHz == 0 disables the monitor entirely (always
// plausible). This is the shipped default: the actual ceiling for Wire
// Drawing Machine 1 is INSUFFICIENT VERIFIED INFORMATION until real line
// parameters (max line speed, pulses/cm) are provided -- deliberately
// shipped inert rather than guessed (Phase-1 mandate).
//
// HYSTERESIS: 3 consecutive over-ceiling cycles raise the flag, 5
// consecutive normal cycles clear it -- a single noise spike between two
// telemetry ticks cannot flap the state. Fixed internal constants, not
// configuration: they encode "sustained", not a site property.
// ============================================================================
#pragma once
#include <stdint.h>

class PulsePlausibilityMonitor {
public:
  explicit PulsePlausibilityMonitor(uint32_t maxHz) : maxHz_(maxHz) {}

  // Runtime (re)configuration -- validated by the caller (store.h bounds);
  // 0 disables. Resets streak state so a stale flag can't outlive the
  // config that produced it.
  void setMaxHz(uint32_t maxHz) {
    maxHz_ = maxHz;
    overStreak_ = 0;
    normalStreak_ = 0;
    if (maxHz == 0) suspect_ = false;
  }
  uint32_t maxHz() const { return maxHz_; }

  // Call once per telemetry cycle with the CURRENT lifetime pulse total and
  // millis()-style timestamp. Returns the (possibly updated) suspect state.
  bool update(uint64_t total, uint32_t nowMs) {
    if (!initialized_) {
      initialized_ = true;
      lastTotal_ = total;
      lastMs_ = nowMs;
      return suspect_;
    }
    uint32_t dtMs = nowMs - lastMs_;   // wrap-safe unsigned math
    if (dtMs == 0) return suspect_;    // same-tick double call: no new information
    uint64_t delta = (total >= lastTotal_) ? (total - lastTotal_) : 0;
    lastTotal_ = total;
    lastMs_ = nowMs;

    uint32_t hz = (uint32_t)((delta * 1000u) / dtMs);
    if (hz > peakHz_) peakHz_ = hz;

    if (maxHz_ == 0) return suspect_;  // disabled: observe peak only, never flag

    if (hz > maxHz_) {
      violations_++;
      normalStreak_ = 0;
      if (++overStreak_ >= 3 && !suspect_) suspect_ = true;
    } else {
      overStreak_ = 0;
      if (suspect_ && ++normalStreak_ >= 5) suspect_ = false;
    }
    return suspect_;
  }

  bool     suspect() const         { return suspect_; }
  uint32_t peakHzObserved() const  { return peakHz_; }   // tracked even when disabled --
                                                          // this is how a real site ceiling
                                                          // gets chosen from evidence
  uint32_t violationCount() const  { return violations_; }

private:
  uint32_t maxHz_;
  bool     initialized_  = false;
  bool     suspect_      = false;
  uint64_t lastTotal_    = 0;
  uint32_t lastMs_       = 0;
  uint32_t peakHz_       = 0;
  uint32_t violations_   = 0;
  uint8_t  overStreak_   = 0;
  uint8_t  normalStreak_ = 0;
};
