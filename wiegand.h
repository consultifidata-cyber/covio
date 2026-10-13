// wiegand.h -- the station's 125 kHz card reader, on two of the board's
// isolated inputs.
//
// The reader is a generic 125 kHz "ID" reader with a Wiegand output, powered
// from the same 12 V supply as the board:
//
//   GREEN  (D0) -> DI2 terminal (GPIO5)      RED   -> +12 V
//   WHITE  (D1) -> DI3 terminal (GPIO6)      BLACK -> 0 V (shared with the board)
//   DI COM      -> +12 V                     BLUE, YELLOW, GRAY -> not connected
//
// Wiegand: both lines idle; a 0 bit is a ~50-100 us pulse on D0, a 1 bit the
// same on D1, about 1-2 ms apart, most significant bit first. A frame ends
// when the lines have been quiet for WIEGAND_FRAME_GAP_US. The board's
// optocoupler inverts (an active input reads LOW), so each pulse gives the
// GPIO exactly one falling edge -- that edge is the bit.
//
// Two formats are accepted, the ones the reader's GRAY wire selects:
//   26 bits: E + 24 data + O   (even parity over bits 0-12, odd over 13-25)
//   34 bits: E + 32 data + O   (even parity over bits 0-16, odd over 17-33)
// Anything else, or a parity failure, is counted and dropped -- never a card.
//
// The UID is the data bits as upper-case hex, 8 characters: a 26-bit card
// reads "00" + 6 hex (facility byte, then the 16-bit card number), a 34-bit
// card 8 hex. THE SAME CARD READS DIFFERENTLY IN THE TWO FORMATS, so a reader
// switched between WG26 and WG34 needs its cards enrolled again.
//
// What this reader cannot do: Wiegand has no heartbeat, so a cut cable is
// silence, not a fault. up() means "the driver is running"; a dead reader is
// found by the bench row and by people, not by the board.
//
// The decoder and the frame collector are plain C++ (host-tested in
// test/native_cpp/test_station_wiegand.cpp); only WiegandCardReader at the
// bottom touches the hardware.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_reader.h"

#ifndef WIEGAND_FRAME_GAP_US
#define WIEGAND_FRAME_GAP_US 25000UL  // quiet for this long = the frame is over
#endif
#ifndef WIEGAND_MIN_BIT_GAP_US
// An edge closer than this to the previous bit is the optocoupler's edge
// ringing, not a bit: real bits are ~1-2 ms apart (Wiegand allows 200 us).
#define WIEGAND_MIN_BIT_GAP_US 150UL
#endif

namespace station {

static const uint8_t WIEGAND_MAX_BITS = 40;

enum WiegandResult : uint8_t {
  WG_OK = 0,
  WG_BAD_LENGTH = 1,
  WG_BAD_PARITY = 2,
};

inline const char* wiegandResultName(WiegandResult r) {
  switch (r) {
    case WG_OK: return "ok";
    case WG_BAD_LENGTH: return "bad-length";
    case WG_BAD_PARITY: return "bad-parity";
  }
  return "?";
}

// One frame, bits[0] first on the wire, each 0 or 1. Fills `uid` (at least
// 17 bytes) on WG_OK.
inline WiegandResult decodeWiegand(const uint8_t* bits, uint8_t n, char* uid) {
  if (n != 26 && n != 34) return WG_BAD_LENGTH;
  uint8_t half = n / 2;  // 13 or 17: each parity bit covers its half
  uint8_t ones = 0;
  for (uint8_t i = 0; i < half; i++) ones += bits[i];
  if (ones % 2 != 0) return WG_BAD_PARITY;  // leading bit: EVEN parity
  ones = 0;
  for (uint8_t i = half; i < n; i++) ones += bits[i];
  if (ones % 2 != 1) return WG_BAD_PARITY;  // trailing bit: ODD parity
  uint32_t data = 0;
  for (uint8_t i = 1; i < n - 1; i++) data = (data << 1) | bits[i];
  snprintf(uid, 17, "%08lX", (unsigned long)data);
  return WG_OK;
}

// Collects bits as they arrive (from the interrupt) and hands over a finished
// frame once the line is quiet. Not thread-safe by itself: the hardware
// reader below calls it inside a critical section.
class WiegandCollector {
 public:
  // One pulse on D0 (bit 0) or D1 (bit 1), at `us` (micros()). Always inlined:
  // on the board it runs inside the interrupt, which must stay in IRAM.
  __attribute__((always_inline)) void addBit(uint8_t b, uint32_t us) {
    if (n_ > 0 && (uint32_t)(us - lastUs_) < WIEGAND_MIN_BIT_GAP_US) {
      glitches_++;
      return;
    }
    lastUs_ = us;
    if (n_ < WIEGAND_MAX_BITS) {
      bits_[n_] = b;
      n_++;
    } else {
      overflow_ = true;
    }
  }

  // A finished frame, if the line has been quiet for the frame gap. A frame
  // that overflowed comes back with n = WIEGAND_MAX_BITS + 1 so the decoder
  // refuses it by length.
  bool take(uint32_t nowUs, uint8_t* bits, uint8_t* n) {
    if (n_ == 0) return false;
    if ((uint32_t)(nowUs - lastUs_) < WIEGAND_FRAME_GAP_US) return false;
    memcpy(bits, bits_, n_);
    *n = overflow_ ? (uint8_t)(WIEGAND_MAX_BITS + 1) : n_;
    n_ = 0;
    overflow_ = false;
    return true;
  }

  uint32_t glitches() const { return glitches_; }

 private:
  uint8_t bits_[WIEGAND_MAX_BITS] = {0};
  uint8_t n_ = 0;
  bool overflow_ = false;
  uint32_t lastUs_ = 0;
  uint32_t glitches_ = 0;
};

// What `station wiegand` prints: enough to prove on the bench that the
// board's inputs hear every bit (tap one card 50 times: 50 frames, all ok,
// all the same UID).
struct WiegandStats {
  uint32_t frames = 0;
  uint32_t ok = 0;
  uint32_t badLength = 0;
  uint32_t badParity = 0;
  uint8_t lastN = 0;
  WiegandResult lastResult = WG_OK;
  char lastBits[WIEGAND_MAX_BITS + 2] = {0};
  char lastUid[17] = {0};

  void record(const uint8_t* bits, uint8_t n, WiegandResult r, const char* uid) {
    frames++;
    if (r == WG_OK) ok++;
    if (r == WG_BAD_LENGTH) badLength++;
    if (r == WG_BAD_PARITY) badParity++;
    lastN = n;
    lastResult = r;
    uint8_t shown = n > WIEGAND_MAX_BITS ? WIEGAND_MAX_BITS : n;
    for (uint8_t i = 0; i < shown; i++) lastBits[i] = bits[i] ? '1' : '0';
    lastBits[shown] = 0;
    if (r == WG_OK) {
      memcpy(lastUid, uid, sizeof(lastUid));
    } else {
      lastUid[0] = 0;
    }
  }
};

}  // namespace station

#ifdef ARDUINO
#include <Arduino.h>

namespace station {

// What the interrupt touches, and nothing else.
struct WiegandIsrState {
  WiegandCollector collector;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
};

// The interrupt handlers live in IRAM: the board writes flash (the queue, the
// station config), and an interrupt that ran from flash during a write would
// crash it. They are file-level functions, not class members: an IRAM_ATTR
// function defined inline in a class fails to link on the ESP32-S3 ("literal
// placed after use"). micros() and the critical section are IRAM-safe, and
// addBit() is forced inline. This header is included by covio_firmware.ino
// only, so `static` gives one copy.
static void IRAM_ATTR wiegandEdge(WiegandIsrState* s, uint8_t b) {
  uint32_t us = micros();
  portENTER_CRITICAL_ISR(&s->mux);
  s->collector.addBit(b, us);
  portEXIT_CRITICAL_ISR(&s->mux);
}
static void IRAM_ATTR wiegandOnD0(void* s) { wiegandEdge(static_cast<WiegandIsrState*>(s), 0); }
static void IRAM_ATTR wiegandOnD1(void* s) { wiegandEdge(static_cast<WiegandIsrState*>(s), 1); }

class WiegandCardReader : public CardReader {
 public:
  bool begin(int pinD0, int pinD1) {
    pinD0_ = pinD0;
    pinD1_ = pinD1;
    pinMode(pinD0, INPUT_PULLUP);
    pinMode(pinD1, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(pinD0), wiegandOnD0, &isr_, FALLING);
    attachInterruptArg(digitalPinToInterrupt(pinD1), wiegandOnD1, &isr_, FALLING);
    started_ = true;
    return true;
  }

  // Called from the station task every STATION_TASK_PERIOD_MS: at most one
  // finished frame, a few microseconds of work.
  bool poll(char* uid) override {
    uint8_t bits[WIEGAND_MAX_BITS + 1];
    uint8_t n = 0;
    portENTER_CRITICAL(&isr_.mux);
    bool have = isr_.collector.take(micros(), bits, &n);
    portEXIT_CRITICAL(&isr_.mux);
    if (!have) return false;
    char decoded[17] = {0};
    WiegandResult r = decodeWiegand(bits, n, decoded);
    stats_.record(bits, n, r, decoded);
    if (r != WG_OK) return false;
    memcpy(uid, decoded, sizeof(decoded));
    return true;
  }

  bool up() const override { return started_; }  // not sensed -- see the header
  const char* kind() const override { return "wiegand"; }

  void printStats(Print& out) {
    portENTER_CRITICAL(&isr_.mux);
    uint32_t glitches = isr_.collector.glitches();
    portEXIT_CRITICAL(&isr_.mux);
    out.printf("wiegand: D0=GPIO%d D1=GPIO%d frames=%lu ok=%lu bad_length=%lu bad_parity=%lu "
               "glitches=%lu\n",
               pinD0_, pinD1_, (unsigned long)stats_.frames, (unsigned long)stats_.ok,
               (unsigned long)stats_.badLength, (unsigned long)stats_.badParity,
               (unsigned long)glitches);
    if (stats_.frames == 0) {
      out.println("wiegand: no frame yet -- tap a card");
      return;
    }
    out.printf("wiegand: last %u bits %s -> %s %s\n", (unsigned)stats_.lastN, stats_.lastBits,
               wiegandResultName(stats_.lastResult), stats_.lastUid);
  }

 private:
  WiegandIsrState isr_;
  WiegandStats stats_;
  int pinD0_ = -1;
  int pinD1_ = -1;
  bool started_ = false;
};

}  // namespace station
#endif  // ARDUINO
