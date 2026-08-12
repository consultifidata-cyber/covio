// ============================================================================
// test_reset_reason.cpp — host-compilable guard on the reset-cause table
// ----------------------------------------------------------------------------
// reset_reason.h is a pure mapping header (no Arduino, no ESP-IDF when built
// with -DNATIVE_TEST), so it is host-compilable directly.
//
// This is not a formatting test. The ERP now clears a measurement gap — it
// declares that no oil moved and that the day's total is exact rather than a
// minimum — on the strength of the strings and the predicate below. Two
// failure modes are therefore worth failing a build over:
//
//   * a NAME drifting. The ERP matches on these strings, so renaming
//     "power_on" does not produce an error anywhere; it silently stops
//     matching, and gaps that were proven power cuts quietly revert to
//     "unmeasured". Reversible, but invisible while it lasts.
//   * a cause being ADDED to the power-loss set. That is the dangerous
//     direction: it converts a device fault into a fabricated zero, telling
//     an owner that no oil moved during a window in which the pump may have
//     been running the whole time. Nothing downstream can detect that.
//
// Build/run (mirrors the other native_cpp jobs in .github/workflows/ci.yml):
//   g++ -std=c++14 -Wall -DNATIVE_TEST -I../.. test_reset_reason.cpp -o t && ./t
// ============================================================================
#include <cstdio>
#include <cstring>

#include "../../reset_reason.h"

static int failures = 0;

static void expectName(esp_reset_reason_t r, const char* want) {
  const char* got = resetReasonName(r);
  if (got == nullptr || std::strcmp(got, want) != 0) {
    std::printf("FAIL: reason %d -> \"%s\", expected \"%s\"\n",
                (int)r, got ? got : "(null)", want);
    failures++;
  }
}

static void expectUnmapped(esp_reset_reason_t r) {
  if (resetReasonName(r) != nullptr) {
    std::printf("FAIL: reason %d should be unmapped, got \"%s\"\n",
                (int)r, resetReasonName(r));
    failures++;
  }
}

static void expectPower(esp_reset_reason_t r, bool want) {
  if (resetReasonIsPowerLoss(r) != want) {
    std::printf("FAIL: reason %d power-loss=%d, expected %d\n",
                (int)r, (int)resetReasonIsPowerLoss(r), (int)want);
    failures++;
  }
}

int main() {
  // ---- the wire contract the ERP matches on -------------------------------
  expectName(ESP_RST_POWERON,   "power_on");
  expectName(ESP_RST_BROWNOUT,  "brownout");
  expectName(ESP_RST_SW,        "software");
  expectName(ESP_RST_PANIC,     "panic");
  expectName(ESP_RST_EXT,       "external_pin");
  expectName(ESP_RST_DEEPSLEEP, "deepsleep");

  // All three watchdog flavours collapse to one name deliberately: an
  // interrupt watchdog and a task watchdog are the same fact to an owner.
  expectName(ESP_RST_INT_WDT,  "watchdog");
  expectName(ESP_RST_TASK_WDT, "watchdog");
  expectName(ESP_RST_WDT,      "watchdog");

  // ---- unmapped must stay unmapped ----------------------------------------
  // Raw 0 is not hypothetical: an esptool-triggered reset reads exactly this
  // on the deployed Balaji hardware, captured 2026-08-11. It is the single
  // most likely value to be mistaken for a power cut, so it must arrive
  // nameless and let the reader fail closed.
  expectUnmapped(ESP_RST_UNKNOWN);
  expectUnmapped(ESP_RST_SDIO);
  expectUnmapped((esp_reset_reason_t)99);

  // ---- the accounting predicate -------------------------------------------
  // EXACTLY two causes may clear a gap. Anything else flowing into this set
  // invents a zero, so this half of the test is the one that matters.
  expectPower(ESP_RST_POWERON,  true);
  expectPower(ESP_RST_BROWNOUT, true);

  expectPower(ESP_RST_SW,        false);
  expectPower(ESP_RST_PANIC,     false);
  expectPower(ESP_RST_INT_WDT,   false);
  expectPower(ESP_RST_TASK_WDT,  false);
  expectPower(ESP_RST_WDT,       false);
  expectPower(ESP_RST_EXT,       false);
  expectPower(ESP_RST_DEEPSLEEP, false);
  expectPower(ESP_RST_UNKNOWN,   false);
  expectPower(ESP_RST_SDIO,      false);
  expectPower((esp_reset_reason_t)99, false);

  if (failures) {
    std::printf("\n%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("reset_reason: all assertions passed\n");
  return 0;
}
