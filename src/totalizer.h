// ============================================================================
// totalizer.h  —  hardware pulse counter + crash-safe persistent total
// ----------------------------------------------------------------------------
// WHY PCNT: counting pulses in a GPIO interrupt drops counts under WiFi load
// (your Phase 6 proved this). The ESP32 PCNT peripheral counts in hardware, so
// the total is exact regardless of what the CPU is doing.
//
// CRASH SAFETY: the running total is checkpointed to SD in a fixed-size CRC32
// record, written alternately to TWO slots (ping-pong). On boot we load the
// newest slot that passes CRC. A power cut mid-write corrupts at most ONE slot;
// the other still holds the previous good value. A torn write fails CRC and is
// skipped — it never yields a garbage number. (Architecture Invariant 3.)
//
// NOTE: totalizer stores RAW PULSES, never litres. Litres are derived from
// pulses / K-factor, and K lives on the server. This is what lets you
// re-calibrate historical data by editing K on the website. See telemetry.h.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "driver/pcnt.h"
#include "config.h"

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
  uint32_t crc32;      // over all prior bytes
};

class Totalizer {
public:
  void begin(uint32_t bootId) {
    setupPCNT_();
    // recover newest CRC-valid checkpoint from either slot
    Checkpoint a, b;
    bool va = load_(CKPT_PATH_A, a);
    bool vb = load_(CKPT_PATH_B, b);
    if (va && vb) cp_ = (a.writes >= b.writes) ? a : b;
    else if (va)  cp_ = a;
    else if (vb)  cp_ = b;
    else { memset(&cp_, 0, sizeof(cp_)); cp_.magic = CKPT_MAGIC; }
    cp_.boot_id = bootId;
    base_ = cp_.total;          // total accumulated before this power-up
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

private:
  void setupPCNT_() {
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
    File f = SD.open(path, FILE_READ);
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

  void persist_() {
    cp_.magic  = CKPT_MAGIC;
    cp_.writes++;
    const char* path = (cp_.writes & 1) ? CKPT_PATH_A : CKPT_PATH_B; // alternate
    cp_.crc32 = 0;
    cp_.crc32 = crc32_((uint8_t*)&cp_, sizeof(cp_));
    File f = SD.open(path, FILE_WRITE);   // FILE_WRITE truncates+rewrites this slot
    if (!f) { Serial.println("[TOT] checkpoint open FAILED"); return; }
    f.write((uint8_t*)&cp_, sizeof(cp_));
    f.flush();
    f.close();
  }

  Checkpoint cp_{};
  uint64_t base_ = 0;         // total before this boot
  uint64_t accumulated_ = 0;  // drained-out counts this boot
};
