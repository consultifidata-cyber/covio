// ============================================================================
// sensor_stuck.h — Balaji V1 freeze remediation (Product Readiness Review
// P1-2 / freeze-list item 4: sensor-stuck-at-zero detection).
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- same
// "pure class/function extracted for testability" pattern this project
// already uses for credential_display.h / ota_version_policy.h /
// timestamp_parse.h. Host-testable via test/native_cpp/test_sensor_stuck.cpp.
//
// WHAT THIS IS: a heuristic advisory, not a definitive sensor-failure
// diagnosis. A real oil meter can legitimately see zero flow for extended
// idle periods (no dispensing happening) -- this only flags "the totalizer
// has not moved for an unusually long time," which an operator should
// interpret using their own knowledge of the site's actual flow pattern.
// SENSOR_STUCK_THRESHOLD_MS (config.h) is deliberately generous by default
// and tunable per-site once real flow patterns are observed.
//
// WHAT THIS IS NOT: it does not reset on reboot in a way that loses
// information across a power cycle (a fresh boot simply restarts the
// "time since last observed change" clock at 0 -- this is a soft,
// operational simplification, not a correctness requirement, since this is
// an advisory alarm, not a safety-critical measurement).
// ============================================================================
#pragma once
#include <stdint.h>

class SensorStuckDetector {
public:
  // thresholdMs: how long the observed pulse total must remain UNCHANGED
  // before update()/isStuck() report "stuck".
  explicit SensorStuckDetector(uint32_t thresholdMs) : thresholdMs_(thresholdMs) {}

  // Call periodically (once per telemetry cycle) with the CURRENT lifetime
  // pulse total and the current millis()-style timestamp. Mutates internal
  // "last observed change" state. Returns the same value isStuck() would.
  bool update(uint64_t total, uint32_t nowMs) {
    if (!initialized_) {
      initialized_ = true;
      lastTotal_ = total;
      lastChangeMs_ = nowMs;
      return false;
    }
    if (total != lastTotal_) {
      lastTotal_ = total;
      lastChangeMs_ = nowMs;
      return false;
    }
    return elapsedSinceChange_(nowMs) >= thresholdMs_;
  }

  // Read-only query -- does NOT mutate state, safe to call from a different
  // code path (e.g. the local HTTP API's own periodic sampler) than the one
  // driving update(), without double-counting or racing it.
  bool isStuck(uint32_t nowMs) const {
    return initialized_ && elapsedSinceChange_(nowMs) >= thresholdMs_;
  }

  uint32_t msSinceLastChange(uint32_t nowMs) const {
    return initialized_ ? elapsedSinceChange_(nowMs) : 0;
  }

private:
  // Unsigned subtraction -- correct across a single millis() wraparound
  // (~49.7 days), same idiom this codebase already uses throughout
  // covio_firmware.ino's own `now - tX >= PERIOD` checks.
  uint32_t elapsedSinceChange_(uint32_t nowMs) const { return nowMs - lastChangeMs_; }

  uint32_t thresholdMs_;
  bool     initialized_ = false;
  uint64_t lastTotal_ = 0;
  uint32_t lastChangeMs_ = 0;
};
