// ============================================================================
// sensor_health.h — Miki Wire profile: production-aware sensor health model.
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- same "pure class
// extracted for testability" pattern as sensor_ct.h / sensor_stuck.h.
// Host-tested by test/native_cpp/test_sensor_health.cpp. Compiled ONLY into
// MIKI_WIRE_PROFILE builds -- the Balaji flag-less build contains none of it.
//
// WHAT THIS IS (Phase-0 finding F6): the only pre-existing sensor
// diagnostic (sensor_stuck.h, 48h threshold, tuned for an oil meter's idle
// pattern) is far too slow for a production wire line and cannot separate
// "machine legitimately stopped" from "sensor dead". This model makes that
// separation explicit and operator-tunable:
//
//   STARTING        first cycle(s): no baseline yet, nothing claimed
//   ACTIVE          pulses observed within the last ACTIVE_HOLD window
//   IDLE            no pulses, but within the site's configured longest
//                   plausible idle -- NORMAL, never an alarm ("no
//                   production" != "sensor fault", the critical Phase-1
//                   distinction)
//   SUSPECT         no pulses for LONGER than the site's configured longest
//                   plausible idle -- an ADVISORY that the sensor deserves
//                   inspection, stamped into the record's quality bitfield
//                   (QUALITY_SENSOR_SUSPECT)
//
// HONEST LIMITATION, stated not hidden: on an NPN open-collector input,
// sensor-stuck-HIGH, sensor-stuck-LOW, wire-break, and "machine idle" are
// electrically indistinguishable (all present as "no edges"). Without a
// second signal (supervised input, machine-run contact) firmware can only
// bound how long "no edges" stays plausible. That bound is the ONE tunable
// here -- and it is INSUFFICIENT VERIFIED INFORMATION until Miki Wire's
// real shift/idle pattern is provided, so the shipped default (0) disables
// SUSPECT entirely: states move between STARTING/ACTIVE/IDLE only.
// A pulse at any moment returns the state to ACTIVE immediately -- recovery
// needs no separate state because it is instantaneous and unambiguous.
// ============================================================================
#pragma once
#include <stdint.h>

enum SensorHealthState : uint8_t {
  SENSOR_HEALTH_STARTING = 0,
  SENSOR_HEALTH_ACTIVE   = 1,
  SENSOR_HEALTH_IDLE     = 2,
  SENSOR_HEALTH_SUSPECT  = 3,
};

inline const char* sensorHealthStateStr(SensorHealthState s) {
  switch (s) {
    case SENSOR_HEALTH_STARTING: return "starting";
    case SENSOR_HEALTH_ACTIVE:   return "active";
    case SENSOR_HEALTH_IDLE:     return "idle";
    case SENSOR_HEALTH_SUSPECT:  return "suspect";
    default:                     return "unknown";
  }
}

class SensorHealthMonitor {
public:
  // suspectThresholdMs: how long "no pulses" stays plausible for this site
  // (longest legitimate idle). 0 = SUSPECT disabled (shipped default until
  // real site parameters exist). ACTIVE_HOLD_MS is fixed: it encodes
  // "recently pulsing", not a site property.
  explicit SensorHealthMonitor(uint32_t suspectThresholdMs)
      : suspectThresholdMs_(suspectThresholdMs) {}

  static const uint32_t ACTIVE_HOLD_MS = 10000;   // "recently" = within 10s

  // Validated by the caller (store.h bounds); 0 disables SUSPECT. Does not
  // reset the last-pulse clock -- reconfiguring must not mask a real gap.
  void setSuspectThresholdMs(uint32_t v) { suspectThresholdMs_ = v; }
  uint32_t suspectThresholdMs() const    { return suspectThresholdMs_; }

  // Call once per telemetry cycle with the CURRENT lifetime pulse total and
  // millis()-style timestamp. Returns the new state.
  SensorHealthState update(uint64_t total, uint32_t nowMs) {
    if (!initialized_) {
      initialized_ = true;
      lastTotal_ = total;
      lastPulseMs_ = nowMs;   // startup baseline: the gap clock starts now,
                              // NOT "guilty since boot" -- a machine that is
                              // off when the device powers up must not be
                              // instantly SUSPECT
      state_ = SENSOR_HEALTH_STARTING;
      return state_;
    }
    if (total != lastTotal_) {
      lastTotal_ = total;
      lastPulseMs_ = nowMs;
      state_ = SENSOR_HEALTH_ACTIVE;
      return state_;
    }
    uint32_t gap = nowMs - lastPulseMs_;   // wrap-safe unsigned math
    if (suspectThresholdMs_ != 0 && gap >= suspectThresholdMs_) {
      state_ = SENSOR_HEALTH_SUSPECT;
    } else if (gap >= ACTIVE_HOLD_MS) {
      state_ = SENSOR_HEALTH_IDLE;
    } else if (state_ != SENSOR_HEALTH_STARTING) {
      state_ = SENSOR_HEALTH_ACTIVE;       // pulsed <10s ago: still "recently active"
    }
    return state_;
  }

  SensorHealthState state() const { return state_; }
  uint32_t msSinceLastPulse(uint32_t nowMs) const {
    return initialized_ ? (nowMs - lastPulseMs_) : 0;
  }

private:
  uint32_t suspectThresholdMs_;
  bool     initialized_ = false;
  SensorHealthState state_ = SENSOR_HEALTH_STARTING;
  uint64_t lastTotal_ = 0;
  uint32_t lastPulseMs_ = 0;
};
