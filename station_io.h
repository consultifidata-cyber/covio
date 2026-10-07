// station_io.h -- the station's outputs: tower light, sounder, onboard beeper.
//
// Two layers, and the split is the point.
//
// OutputGovernor (pure, host-tested in test_station_io.cpp) turns what the
// logic ASKS for into what the pins DO, and enforces its own limits whatever
// it is asked -- a second, independent fence behind station_logic.h's caps:
//
//   * all outputs off until the first refresh, and off at begin();
//   * the sounder never on for more than CAP_BURST_S (10 s) at a stretch, then
//     off for at least SOUNDER_REST_MS before it may sound again;
//   * never more than SOUNDER_WINDOW_MAX_S (60 s) of sound in each ten-minute
//     window (fixed windows counted from begin(), not a rolling ten minutes);
//   * if the station task stops refreshing for HEARTBEAT_MS, everything goes
//     off -- the loop calls failsafe() every pass, so a hung task cannot leave
//     a hooter on (Part 2 §V rule 4; the product success test's last line).
//
// Pca9554Outputs (Arduino only) drives the Waveshare 8DI-8DO's PCA9554 at I2C
// 0x20 (SDA 42, SCL 41): it writes the OFF pattern to the output register
// BEFORE switching the port to outputs, so begin() never glitches a channel on.
//
// ⚠ BENCH VERIFICATION REQUIRED, all of it: which logic level turns a channel
// on (STATION_OUTPUT_ACTIVE_HIGH), the expander's state between power-on and
// begin() (its port powers up as inputs; what the opto/Darlington stage does
// with that is hardware, not firmware), the sounder's current against the
// 500 mA channel limit, and the light's wiring. Nothing here proves any of it.
#pragma once

#include <stdint.h>

#include "station_logic.h"

#ifndef STATION_CH_RED
#define STATION_CH_RED 0  // DO1
#endif
#ifndef STATION_CH_AMBER
#define STATION_CH_AMBER 1  // DO2
#endif
#ifndef STATION_CH_GREEN
#define STATION_CH_GREEN 2  // DO3
#endif
#ifndef STATION_CH_SOUNDER
#define STATION_CH_SOUNDER 3  // DO4
#endif
#ifndef STATION_OUTPUT_ACTIVE_HIGH
#define STATION_OUTPUT_ACTIVE_HIGH 1
#endif

namespace station {

static const uint32_t SOUNDER_REST_MS = 1000;
static const uint32_t SOUNDER_WINDOW_MS = 10UL * 60UL * 1000UL;
static const uint32_t SOUNDER_WINDOW_MAX_S = 60;
static const uint32_t HEARTBEAT_MS = 3000;
static const uint32_t BLINK_HALF_MS = 500;

struct OutputState {
  bool red, amber, green, sounder;
  uint8_t bits() const {
    uint8_t b = 0;
    if (red) b |= (uint8_t)(1u << STATION_CH_RED);
    if (amber) b |= (uint8_t)(1u << STATION_CH_AMBER);
    if (green) b |= (uint8_t)(1u << STATION_CH_GREEN);
    if (sounder) b |= (uint8_t)(1u << STATION_CH_SOUNDER);
    return b;
  }
};

class OutputGovernor {
 public:
  void begin(uint32_t nowMs) {
    started_ = false;
    lastRefreshMs_ = nowMs;
    sounderOn_ = false;
    sounderSinceMs_ = 0;
    sounderOffAtMs_ = 0;
    windowStartMs_ = nowMs;
    windowSoundMs_ = 0;
    lastAccountMs_ = nowMs;
    faultLatched_ = false;
  }

  // What the logic asks for, at `nowMs`. Returns the pins' state.
  OutputState refresh(Light light, bool sounding, uint32_t nowMs) {
    started_ = true;
    lastRefreshMs_ = nowMs;
    light_ = light;
    wantSound_ = sounding;
    return compute(nowMs);
  }

  // Called by the loop on every pass, independent of the task.
  OutputState failsafe(uint32_t nowMs) { return compute(nowMs); }

  // True once the governor has cut a sounder the logic still asked for.
  bool faultLatched() const { return faultLatched_; }
  uint32_t windowSoundMs() const { return windowSoundMs_; }

 private:
  OutputState compute(uint32_t nowMs) {
    OutputState s = {false, false, false, false};
    accountSound(nowMs);
    if (!started_ || nowMs - lastRefreshMs_ > HEARTBEAT_MS) {
      setSounder(false, nowMs);
      return s;  // never refreshed, or the task has gone quiet: all off
    }
    bool phase = ((nowMs / BLINK_HALF_MS) & 1u) == 0;
    switch (light_) {
      case L_GREEN:
        s.green = true;
        break;
      case L_AMBER:
        s.amber = true;
        break;
      case L_RED_BLINK:
        s.red = phase;
        break;
      case L_AMBER_BLINK:
        s.amber = phase;
        break;
      default:
        break;
    }
    bool allowed = wantSound_;
    if (allowed && sounderOn_ && nowMs - sounderSinceMs_ >= CAP_BURST_S * 1000UL) {
      allowed = false;
      faultLatched_ = true;
    }
    if (allowed && !sounderOn_ && sounderOffAtMs_ != 0 && nowMs - sounderOffAtMs_ < SOUNDER_REST_MS)
      allowed = false;
    if (allowed && windowSoundMs_ >= SOUNDER_WINDOW_MAX_S * 1000UL) {
      allowed = false;
      faultLatched_ = true;
    }
    setSounder(allowed, nowMs);
    s.sounder = sounderOn_;
    return s;
  }

  void setSounder(bool on, uint32_t nowMs) {
    if (on && !sounderOn_) {
      sounderOn_ = true;
      sounderSinceMs_ = nowMs;
    } else if (!on && sounderOn_) {
      sounderOn_ = false;
      sounderOffAtMs_ = nowMs ? nowMs : 1;
    }
  }

  void accountSound(uint32_t nowMs) {
    if (nowMs - windowStartMs_ >= SOUNDER_WINDOW_MS) {
      windowStartMs_ = nowMs;
      windowSoundMs_ = 0;
    }
    if (sounderOn_) windowSoundMs_ += nowMs - lastAccountMs_;
    lastAccountMs_ = nowMs;
  }

  bool started_ = false;
  uint32_t lastRefreshMs_ = 0;
  Light light_ = L_OFF;
  bool wantSound_ = false;
  bool sounderOn_ = false;
  uint32_t sounderSinceMs_ = 0;
  uint32_t sounderOffAtMs_ = 0;
  uint32_t windowStartMs_ = 0;
  uint32_t windowSoundMs_ = 0;
  uint32_t lastAccountMs_ = 0;
  bool faultLatched_ = false;
};

// Which uptime second the station task ticks, and when.
//
// One tick per uptime second, in order, never two for the same second: that is
// what the logic (and its parity with the platform) assumes. But a tap must not
// wait for the next whole second -- that put up to a second between a card and
// its beep, against a target of 500 ms (S6 §20). So a tap takes the NEXT
// second's tick at once: the station runs at most one second ahead of its
// clock, never further, and a second tap inside that second waits for the
// boundary. Pure and host-tested (test_station_io.cpp).
class TickClock {
 public:
  void begin(uint32_t nowS) { last_ = nowS; }
  // True when a tick is due now; `second` is the uptime second to tick.
  bool due(uint32_t nowS, bool tapWaiting, uint32_t* second) {
    if ((int32_t)(nowS - last_) >= 1) {
      last_ = nowS;
      *second = nowS;
      return true;
    }
    if (tapWaiting && nowS == last_) {
      last_ = nowS + 1;
      *second = last_;
      return true;
    }
    return false;
  }
  uint32_t last() const { return last_; }

 private:
  uint32_t last_ = 0;
};

}  // namespace station

#if defined(ARDUINO)
#include <Wire.h>

namespace station {

// The Waveshare ESP32-S3-POE-ETH-8DI-8DO output expander.
class Pca9554Outputs {
 public:
  static const uint8_t ADDR = 0x20;
  static const uint8_t REG_OUTPUT = 0x01;
  static const uint8_t REG_CONFIG = 0x03;

  // The FIRST thing setup() does in a station build, before the filesystem,
  // the queue or the network. The expander is a separate chip: a watchdog or
  // panic reset restarts the ESP32 but NOT the PCA9554, which keeps whatever
  // it was driving. Without this, a sounder that was on when the CPU reset
  // would stay on through every slow or failing step of boot (a filesystem
  // that will not mount blocks setup() indefinitely). Writes only the output
  // register: if the port is still inputs (a power-on reset), nothing changes.
  // BENCH VERIFICATION REQUIRED (B01, B21, B25).
  static bool earlyOff() {
    Wire.begin(42, 41);
    Wire.beginTransmission(ADDR);
    Wire.write(REG_OUTPUT);
    Wire.write(levels(0));
    return Wire.endTransmission() == 0;
  }

  bool begin() {
    Wire.begin(42, 41);
    // OFF pattern first, THEN make the port outputs: no channel is ever
    // driven on by begin() itself.
    ok_ = write(REG_OUTPUT, levels(0)) && write(REG_CONFIG, 0x00);
    last_ = 0;
    return ok_;
  }

  // Drive the eight channels from the governor's state; false on an I2C
  // failure (reported as a station fault, never fatal).
  bool apply(const OutputState& s) {
    uint8_t bits = s.bits();
    if (bits == last_ && ok_) return true;
    ok_ = write(REG_OUTPUT, levels(bits));
    if (ok_) last_ = bits;
    return ok_;
  }

  void allOff() {
    write(REG_OUTPUT, levels(0));
    last_ = 0;
  }

  bool ok() const { return ok_; }

 private:
  static uint8_t levels(uint8_t onBits) {
#if STATION_OUTPUT_ACTIVE_HIGH
    return onBits;
#else
    return (uint8_t)~onBits;
#endif
  }

  bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  bool ok_ = false;
  uint8_t last_ = 0;
};

}  // namespace station
#endif
