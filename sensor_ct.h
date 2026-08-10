// ============================================================================
// sensor_ct.h — CT-clamp enhancement: current-presence -> pulse synthesis.
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- same "pure class
// extracted for testability" pattern this project already uses for
// sensor_stuck.h / ota_version_policy.h / timestamp_parse.h. Host-testable
// via test/native_cpp/test_sensor_ct.cpp. Compiled ONLY into
// SENSOR_MODE_CT builds (see config.h / covio_firmware.ino) -- an NPN build
// contains none of this code.
//
// WHAT THIS IS: the CT acquisition half of the sensor abstraction. A CT
// current-sensing switch (contact-output clamp on the monitored equipment's
// supply line) presents a binary level on a DI terminal: current
// present / current absent. This class converts that level into the SAME
// raw-pulse stream the NPN path produces, at CT_PULSE_HZ pulses per second
// of debounced-active time, so every layer above the acquisition seam
// (totalizer persistence, telemetry, queue, sync, server K-factor) operates
// unchanged and unaware of the sensor type.
//
// WHY TIME-INTEGRATION, NOT A SOFTWARE SQUARE WAVE: a toggled wave pauses
// during blocking network calls (the documented SIM_PULSES caveat,
// config.h). Here, each service() call instead converts the ELAPSED
// milliseconds since the previous call into whole pulses (fractional
// remainder carried in milli-pulse units, exact for any CT_PULSE_HZ) -- a
// 3-second HTTP push loses nothing; the integral catches up on return.
// This preserves the project invariant "pulses are never lost to network
// activity" in CT mode by construction rather than by hardware.
//
// HONEST ERROR BOUND: state changes that occur AND fully revert within a
// blocking gap are unobservable (inherent to level sampling), and each
// off->on->off cycle's counted time can differ from true active time by up
// to ~(debounceMs + one loop tick) at each edge, with the two edges'
// errors opposing in sign. At CT_PULSE_HZ=1 / CT_DEBOUNCE_MS=100 that is
// <=0.1s per edge on runtimes measured in minutes-to-hours -- negligible
// for the equipment-status use case, and the server-side K-factor remains
// the sole interpreting authority exactly as for NPN pulses.
// ============================================================================
#pragma once
#include <stdint.h>

class SensorCt {
public:
  // pulseHz: synthesized pulses per second of debounced-active time.
  // debounceMs: how long the raw level must hold steady before the
  // debounced state commits (kills CT-switch contact chatter; the board's
  // own DI optocoupler/RC stage filters faster noise before we see it).
  SensorCt(uint32_t pulseHz, uint32_t debounceMs)
      : pulseHz_(pulseHz), debounceMs_(debounceMs) {}

  // Call once per loop() iteration with the RAW level (true = current
  // present, i.e. DI2 reads LOW through the inverting opto -- the caller
  // does that inversion) and the current millis()-style timestamp.
  // Returns the number of whole pulses to inject into the totalizer NOW
  // (usually 0; >1 only when catching up after a blocking gap).
  //
  // The very first call adopts the raw level as the debounced state
  // immediately (equipment already running at boot starts counting without
  // waiting out a debounce it has trivially already satisfied) and returns
  // 0 -- there is no prior timestamp to integrate from yet.
  uint32_t service(bool rawActive, uint32_t nowMs) {
    if (!initialized_) {
      initialized_      = true;
      stable_           = rawActive;
      candidate_        = rawActive;
      candidateSinceMs_ = nowMs;
      lastServiceMs_    = nowMs;
      return 0;
    }
    uint32_t elapsedMs = nowMs - lastServiceMs_;   // wrap-safe unsigned math,
    lastServiceMs_ = nowMs;                        // same idiom as loop()'s timers

    if (rawActive != candidate_) {                 // raw level moved: restart the
      candidate_        = rawActive;               // debounce clock
      candidateSinceMs_ = nowMs;
    }
    if (candidate_ != stable_ &&
        (uint32_t)(nowMs - candidateSinceMs_) >= debounceMs_) {
      stable_ = candidate_;                        // held long enough: commit
    }

    if (!stable_) return 0;                        // no current -> no pulses
    fracMilliPulses_ += (uint64_t)elapsedMs * pulseHz_;
    uint32_t whole = (uint32_t)(fracMilliPulses_ / 1000u);
    fracMilliPulses_ -= (uint64_t)whole * 1000u;   // exact remainder carry
    return whole;
  }

  // Read-only: current debounced "equipment drawing current" state.
  bool active() const { return stable_; }

private:
  uint32_t pulseHz_;
  uint32_t debounceMs_;
  bool     initialized_      = false;
  bool     stable_           = false;   // debounced/committed level
  bool     candidate_        = false;   // raw level being debounce-timed
  uint32_t candidateSinceMs_ = 0;
  uint32_t lastServiceMs_    = 0;
  uint64_t fracMilliPulses_  = 0;       // remainder, milli-pulse units
};
