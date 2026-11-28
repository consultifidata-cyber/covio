// corun_shim/driver/pcnt.h -- the ESP32 pulse counter, simulated.
//
// The hardware counts edges into a 16-bit register whose high limit is set by
// totalizer.h (32767). Reaching it resets the register to 0 and the counts are
// gone: `lost` records every one, so a test can prove none were. `inStation`
// marks reads made on the station task's behalf; the station must never clear.
#pragma once
#include <stdint.h>

typedef int pcnt_unit_t;
typedef int esp_err_t;
#define PCNT_UNIT_0 0
#define PCNT_CHANNEL_0 0
#define PCNT_PIN_NOT_USED (-1)
#define PCNT_COUNT_INC 1
#define PCNT_COUNT_DIS 0
#define PCNT_MODE_KEEP 0

struct pcnt_config_t {
  int pulse_gpio_num;
  int ctrl_gpio_num;
  int lctrl_mode;
  int hctrl_mode;
  int pos_mode;
  int neg_mode;
  int16_t counter_h_lim;
  int16_t counter_l_lim;
  int unit;
  int channel;
};

struct ShimPcnt {
  int32_t value = 0;
  int32_t hlim = 32767;
  uint64_t lost = 0;
  uint64_t clears = 0;
  uint64_t clearsInStation = 0;
  uint64_t stationReads = 0;
  bool inStation = false;
  void pulse(uint32_t n) {
    value += (int32_t)n;
    while (value >= hlim) {  // the register's limit: reset to 0, counts gone
      lost += (uint64_t)hlim;
      value -= hlim;
    }
  }
  void powerCut() { value = 0; }
};
inline ShimPcnt& shimPcnt() {
  static ShimPcnt p;
  return p;
}

inline esp_err_t pcnt_unit_config(const pcnt_config_t* cfg) {
  shimPcnt().hlim = cfg->counter_h_lim;
  return 0;
}
inline esp_err_t pcnt_set_filter_value(pcnt_unit_t, uint16_t) { return 0; }
inline esp_err_t pcnt_filter_enable(pcnt_unit_t) { return 0; }
inline esp_err_t pcnt_counter_pause(pcnt_unit_t) { return 0; }
inline esp_err_t pcnt_counter_resume(pcnt_unit_t) { return 0; }
inline esp_err_t pcnt_counter_clear(pcnt_unit_t) {
  shimPcnt().clears++;
  if (shimPcnt().inStation) shimPcnt().clearsInStation++;
  shimPcnt().value = 0;
  return 0;
}
inline esp_err_t pcnt_get_counter_value(pcnt_unit_t, int16_t* count) {
  if (shimPcnt().inStation) shimPcnt().stationReads++;
  *count = (int16_t)shimPcnt().value;
  return 0;
}
