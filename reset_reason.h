// ============================================================================
// reset_reason.h — the ONE mapping from ESP-IDF's reset cause to our name
// ----------------------------------------------------------------------------
// WHY THIS IS ITS OWN HEADER.
//
// The cause of a reboot is not a diagnostic curiosity for this product — it is
// an accounting input. The oil meter's totalizer is checkpointed to flash and
// restored on boot, so a restart does not lose the running total; what a
// restart costs is whatever flowed while the device was OFF. How much that is
// depends entirely on WHY it was off:
//
//   * `power_on` / `brownout` — the device lost its supply. At the Balaji
//     plant the meter and the pump motor sit on the same supply, so no power
//     means no pump, which means no oil moved. That window is a CONFIRMED
//     ZERO, and the day's total is exact rather than a minimum.
//   * `watchdog` / `panic` / `software` — the device restarted itself while
//     the plant kept running. Oil may well have flowed past an unpowered
//     sensor, and that window stays unmeasured.
//   * anything else — say so, and let the reader fail closed. An unmapped
//     value must never be quietly filed under "power cut"; that would turn a
//     device fault into a fabricated zero, which is the one error this whole
//     mechanism exists to prevent.
//
// Because that distinction now drives a number the business relies on, the
// mapping must exist exactly ONCE. It previously lived as a private static
// inside diagnostics.h, which was fine while its only consumer was the LAN
// metrics endpoint; the push envelope (telemetry.h) now needs the same answer,
// and two switches over the same enum is precisely the duplication
// MASTER_GOVERNANCE §1 Rule 5 forbids — one of them would eventually gain a
// case the other lacked, and the LAN endpoint and the cloud would disagree
// about why the same meter rebooted.
//
// DESIGN. This returns `const char*` and takes the cause as a PARAMETER rather
// than calling esp_reset_reason() itself, for the same reason ota.h's
// otaStateStr() does: it makes the table host-testable with no ESP32 present
// (test/native_cpp/test_reset_reason.cpp compiles this file directly). It
// deliberately does NOT build a String — callers that want the "unknown(<n>)"
// form compose it themselves, so this header stays free of any Arduino
// dependency and the raw numeric value keeps its own dedicated field.
//
// NULL means "not a value this table names", never "no reset happened".
// ============================================================================
#pragma once

#ifdef NATIVE_TEST
// Host builds have no ESP-IDF. Mirror the enumerators this table switches on,
// with ESP-IDF's own values, so the host test exercises the real mapping
// rather than a re-typed copy of it.
typedef enum {
  ESP_RST_UNKNOWN   = 0,
  ESP_RST_POWERON   = 1,
  ESP_RST_EXT       = 2,
  ESP_RST_SW        = 3,
  ESP_RST_PANIC     = 4,
  ESP_RST_INT_WDT   = 5,
  ESP_RST_TASK_WDT  = 6,
  ESP_RST_WDT       = 7,
  ESP_RST_DEEPSLEEP = 8,
  ESP_RST_BROWNOUT  = 9,
  ESP_RST_SDIO      = 10,
} esp_reset_reason_t;
#else
#include "esp_system.h"
#endif

// The canonical name for a reset cause, or nullptr if unmapped.
//
// ⛔ These strings are a WIRE CONTRACT. The ERP keys its power-cut ruling on
// them, so renaming one silently reclassifies history: gaps that were proven
// to be power cuts stop matching and quietly revert to "unmeasured". Add new
// cases freely; do not rename existing ones.
inline const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_EXT:       return "external_pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default:                return nullptr;   // caller decides how to say "unknown"
  }
}

// True when this cause is evidence the DEVICE lost its electrical supply.
//
// ⚖ SCOPE — read before using this to clear a measurement gap. This says the
// device lost power. It does NOT say the plant did, and it cannot: the meter
// has no idea what else is on its circuit. Treating it as proof that the pump
// was also stopped is a claim about the WIRING, and is only safe where meter
// and motor share one supply with no UPS, inverter or separate breaker
// between them. That is true at Balaji and was confirmed by the founder; it
// is not a property of this firmware, and any new site must be checked before
// the same ruling is applied there.
//
// `brownout` counts: the ESP32 fires it when the rail sags below the detector
// threshold, which is a supply failure caught a moment earlier than a clean
// power_on. `unknown` deliberately does not count — an esptool-triggered
// reset reads as raw 0 on this hardware, which is exactly the case that must
// not be mistaken for a power cut.
inline bool resetReasonIsPowerLoss(esp_reset_reason_t r) {
  return r == ESP_RST_POWERON || r == ESP_RST_BROWNOUT;
}
