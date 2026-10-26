// wiegand.h -- the station's 125 kHz card reader, on two of the board's
// isolated inputs.
//
// The reader is a generic 125 kHz "ID" reader with a Wiegand output, powered
// from the same 12 V supply as the board:
//
//   GREEN  (D0) -> DI2 terminal (GPIO5)      RED   -> +12 V
//   WHITE  (D1) -> DI3 terminal (GPIO6)      BLACK -> 0 V (shared with the board)
//   DI COM      -> 0 V  (NOT +12 V)          BLUE, YELLOW, GRAY -> not connected
//   DI GND      -> 0 V
//
// Why COM on 0 V (bench, 13 Oct 2026): the reader holds D0/D1 at ~5 V when
// idle and pulls them to 0 V for a bit. With COM on +12 V the input sat ON at
// 12-5 V and a bit only made it "more on" -- the board saw nothing. With COM
// on 0 V the reader's own 5 V lights the input at idle and every bit switches
// it OFF: a clean pulse, measured 315-432 us wide every ~1.56 ms.
//
// Wiegand: a 0 bit is a pulse on D0, a 1 bit a pulse on D1, most significant
// bit first. A frame ends when the lines have been quiet for
// WIEGAND_FRAME_GAP_US. A bit is a whole PULSE, measured edge to edge
// (WiegandLines): too short is noise, too long is a held line; neither is a bit.
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
// A bit is a PULSE: the line leaves its idle level and comes back. Measured on
// the Waveshare 8DI-8DO's isolated inputs with a 125 kHz reader (13 Oct 2026):
// every real bit 315-432 us wide, one every ~1.56 ms; electrical noise came as
// bursts of 2-34 us spikes. Anything outside this window is not a bit.
#ifndef WIEGAND_MIN_PULSE_US
#define WIEGAND_MIN_PULSE_US 40UL
#endif
#ifndef WIEGAND_MAX_PULSE_US
#define WIEGAND_MAX_PULSE_US 2000UL
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

  // One whole pulse on D0 or D1, `widthUs` long, ending at `endUs`. A pulse
  // too short (a noise spike) or too long (a wire touched, a line stuck) is
  // counted as a glitch and is not a bit.
  __attribute__((always_inline)) void addPulse(uint8_t b, uint32_t widthUs, uint32_t endUs) {
    if (widthUs < WIEGAND_MIN_PULSE_US || widthUs > WIEGAND_MAX_PULSE_US) {
      glitches_++;
      return;
    }
    addBit(b, endUs);
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

// The two reader lines, edge by edge: turns level changes into pulses and
// pulses into bits. A line's IDLE level is learned while it is quiet, so the
// same code reads a reader whose bits arrive as high pulses (the 8DI-8DO's
// inputs with COM on 0 V) or as low pulses (a plain open-collector line).
// Plain C++: the board feeds it from the interrupt, the host test replays
// edges recorded on the bench.
class WiegandLines {
 public:
  void setIdle(uint8_t b, uint8_t level) {
    idle_[b] = level;
    inPulse_[b] = false;
  }

  // One edge: line `b` is now at `level`, at `us`.
  __attribute__((always_inline)) void edge(uint8_t b, uint8_t level, uint32_t us) {
    lastEdgeUs_ = us;
    anyEdge_ = true;
    if (level != idle_[b]) {
      // Leaving idle: a pulse starts (or restarts, if a spike hid its end).
      inPulse_[b] = true;
      startUs_[b] = us;
    } else if (inPulse_[b]) {
      inPulse_[b] = false;
      collector.addPulse(b, us - startUs_[b], us);
    } else {
      strays_++;  // back to idle with no pulse open: half a spike
    }
  }

  // True when neither line has moved for a frame gap: the idle level may be
  // re-learned now.
  bool quiet(uint32_t nowUs) const {
    return !inPulse_[0] && !inPulse_[1] &&
           (!anyEdge_ || (uint32_t)(nowUs - lastEdgeUs_) >= WIEGAND_FRAME_GAP_US);
  }

  uint32_t strays() const { return strays_; }
  uint8_t idle(uint8_t b) const { return idle_[b]; }

  WiegandCollector collector;

 private:
  uint8_t idle_[2] = {1, 1};
  bool inPulse_[2] = {false, false};
  uint32_t startUs_[2] = {0, 0};
  uint32_t lastEdgeUs_ = 0;
  bool anyEdge_ = false;
  uint32_t strays_ = 0;
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
#include <soc/gpio_reg.h>

#ifndef WIEGAND_EDGE_LOG
#define WIEGAND_EDGE_LOG 512  // edges kept for `station wiegand raw` (8 bytes each)
#endif

namespace station {

// One edge on a reader line, for the bench: when, which line, which level.
struct WiegandEdge {
  uint32_t us;
  uint8_t line;   // 0 = D0 (GREEN), 1 = D1 (WHITE)
  uint8_t level;  // the GPIO level just after the edge
};

// What the interrupt touches, and nothing else.
struct WiegandIsrState {
  WiegandLines lines;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  uint8_t pin[2] = {0, 0};
  WiegandEdge edges[WIEGAND_EDGE_LOG];
  uint16_t edgeHead = 0;   // next slot
  uint16_t edgeCount = 0;  // kept, up to WIEGAND_EDGE_LOG
  uint32_t edgeLost = 0;   // overwritten before they were printed
};

// The interrupt handlers live in IRAM: the board writes flash (the queue, the
// station config), and an interrupt that ran from flash during a write would
// crash it. They are file-level functions, not class members: an IRAM_ATTR
// function defined inline in a class fails to link on the ESP32-S3 ("literal
// placed after use"). micros() and the critical section are IRAM-safe, and
// addBit() is forced inline. This header is included by covio_firmware.ino
// only, so `static` gives one copy.
//
// The interrupt fires on BOTH edges: WiegandLines needs a pulse's start and
// end to measure it, and the bench log (`station wiegand raw`) shows both. The
// level is read straight from the GPIO input register (IRAM-safe; GPIO5/6 are
// below 32).
static void IRAM_ATTR wiegandEdge(WiegandIsrState* s, uint8_t b) {
  uint32_t us = micros();
  uint8_t level = (uint8_t)((REG_READ(GPIO_IN_REG) >> s->pin[b]) & 1u);
  portENTER_CRITICAL_ISR(&s->mux);
  WiegandEdge& e = s->edges[s->edgeHead];
  e.us = us;
  e.line = b;
  e.level = level;
  s->edgeHead = (uint16_t)((s->edgeHead + 1) % WIEGAND_EDGE_LOG);
  if (s->edgeCount < WIEGAND_EDGE_LOG) {
    s->edgeCount++;
  } else {
    s->edgeLost++;
  }
  s->lines.edge(b, level, us);
  portEXIT_CRITICAL_ISR(&s->mux);
}
static void IRAM_ATTR wiegandOnD0(void* s) { wiegandEdge(static_cast<WiegandIsrState*>(s), 0); }
static void IRAM_ATTR wiegandOnD1(void* s) { wiegandEdge(static_cast<WiegandIsrState*>(s), 1); }

class WiegandCardReader : public CardReader {
 public:
  bool begin(int pinD0, int pinD1) {
    pinD0_ = pinD0;
    pinD1_ = pinD1;
    isr_.pin[0] = (uint8_t)pinD0;
    isr_.pin[1] = (uint8_t)pinD1;
    pinMode(pinD0, INPUT_PULLUP);
    pinMode(pinD1, INPUT_PULLUP);
    isr_.lines.setIdle(0, (uint8_t)digitalRead(pinD0));
    isr_.lines.setIdle(1, (uint8_t)digitalRead(pinD1));
    attachInterruptArg(digitalPinToInterrupt(pinD0), wiegandOnD0, &isr_, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(pinD1), wiegandOnD1, &isr_, CHANGE);
    started_ = true;
    return true;
  }

  // Called from the station task every STATION_TASK_PERIOD_MS: at most one
  // finished frame, a few microseconds of work.
  bool poll(char* uid) override {
    uint8_t bits[WIEGAND_MAX_BITS + 1];
    uint8_t n = 0;
    uint32_t now = micros();
    // Read the levels outside the lock; use them only if nothing moved since.
    uint8_t g = (uint8_t)digitalRead(pinD0_);
    uint8_t w = (uint8_t)digitalRead(pinD1_);
    portENTER_CRITICAL(&isr_.mux);
    bool have = isr_.lines.collector.take(now, bits, &n);
    if (!have && isr_.lines.quiet(now)) {
      // Quiet for a frame gap: whatever level the lines sit at is idle. This
      // follows a rewiring (COM moved) without a reboot.
      isr_.lines.setIdle(0, g);
      isr_.lines.setIdle(1, w);
    }
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
    uint32_t glitches = isr_.lines.collector.glitches();
    uint32_t strays = isr_.lines.strays();
    uint8_t idleG = isr_.lines.idle(0), idleW = isr_.lines.idle(1);
    portEXIT_CRITICAL(&isr_.mux);
    out.printf("wiegand: D0=GPIO%d D1=GPIO%d idle=G%uW%u frames=%lu ok=%lu bad_length=%lu "
               "bad_parity=%lu glitches=%lu strays=%lu\n",
               pinD0_, pinD1_, (unsigned)idleG, (unsigned)idleW, (unsigned long)stats_.frames,
               (unsigned long)stats_.ok, (unsigned long)stats_.badLength,
               (unsigned long)stats_.badParity, (unsigned long)glitches, (unsigned long)strays);
    if (stats_.frames == 0) {
      out.println("wiegand: no frame yet -- tap a card");
      return;
    }
    out.printf("wiegand: last %u bits %s -> %s %s\n", (unsigned)stats_.lastN, stats_.lastBits,
               wiegandResultName(stats_.lastResult), stats_.lastUid);
  }

  // `station wiegand raw`: every edge since the last print, oldest first, as
  // "+<µs since the previous edge> <G|W><level>", then forgets them. A gap over
  // 25 ms starts a new line, so each tap's frame prints on its own lines.
  void printRaw(Print& out) {
    static WiegandEdge copy[WIEGAND_EDGE_LOG];
    portENTER_CRITICAL(&isr_.mux);
    uint16_t n = isr_.edgeCount;
    uint16_t start = (uint16_t)((isr_.edgeHead + WIEGAND_EDGE_LOG - n) % WIEGAND_EDGE_LOG);
    for (uint16_t i = 0; i < n; i++) copy[i] = isr_.edges[(start + i) % WIEGAND_EDGE_LOG];
    uint32_t lost = isr_.edgeLost;
    isr_.edgeCount = 0;
    isr_.edgeLost = 0;
    portEXIT_CRITICAL(&isr_.mux);
    out.printf("wiegand raw: %u edges (lost %lu); idle levels now G%d W%d\n", (unsigned)n,
               (unsigned long)lost, digitalRead(pinD0_), digitalRead(pinD1_));
    uint8_t onLine = 0;
    for (uint16_t i = 0; i < n; i++) {
      uint32_t gap = i ? copy[i].us - copy[i - 1].us : 0;
      if (i == 0 || gap > WIEGAND_FRAME_GAP_US) {
        out.printf("%sframe:", i ? "\n" : "");
        onLine = 0;
      } else if (onLine == 12) {
        out.print("\n      ");
        onLine = 0;
      }
      out.printf(" +%lu %c%u", (unsigned long)gap, copy[i].line ? 'W' : 'G',
                 (unsigned)copy[i].level);
      onLine++;
    }
    out.println();
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
