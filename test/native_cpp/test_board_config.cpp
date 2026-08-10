// ============================================================================
// test_board_config.cpp — multi-board enhancement: compile-time pin-table
// regression guard for config.h's BOARD_MODE / SENSOR_MODE selectors.
// ----------------------------------------------------------------------------
// config.h is a pure macro header (zero #includes, no Arduino/ESP-IDF
// dependency), so it is host-compilable directly -- this file freezes the
// deployed production plant's pin table (BOARD_RELAY1CH, commit e5a593b:
// PIN_PULSE=GPIO1) and the 8DI-8DO retarget's table as static_asserts.
// Any accidental edit-in-place of either table, or a change to either
// selector's #ifndef default, becomes a COMPILE FAILURE here rather than a
// silently miscounting field unit (the exact failure mode the plant-
// commissioning audit identified when the 8DI-8DO retarget overwrote the
// production pins in the working tree).
//
// Build/run (CI compiles this TWICE -- see .github/workflows/ci.yml):
//   default:  g++ -std=c++14 -Wall -I../.. test_board_config.cpp -o t && ./t
//   variant:  g++ -std=c++14 -Wall -I../.. -DBOARD_MODE=1 -DSENSOR_MODE=1
//               -DEXPECT_BOARD=1 -DEXPECT_SENSOR=1 test_board_config.cpp -o t && ./t
//             (single command line; wrapped here only for readability)
// EXPECT_BOARD/EXPECT_SENSOR state what the harness passed on the command
// line, so the assertions below also prove the -D override path works and
// that a FLAG-LESS compile really does default to the production baseline.
// ============================================================================
#include <cstdio>
#include "../../config.h"

#ifndef EXPECT_BOARD
#define EXPECT_BOARD  BOARD_RELAY1CH   // flag-less build must be the production board
#endif
#ifndef EXPECT_SENSOR
#define EXPECT_SENSOR SENSOR_MODE_NPN  // flag-less build must be the NPN/PCNT path
#endif

static_assert(BOARD_RELAY1CH == 0 && BOARD_8DI8DO == 1,
              "board selector values are part of the documented -DBOARD_MODE contract");
static_assert(SENSOR_MODE_NPN == 0 && SENSOR_MODE_CT == 1,
              "sensor selector values are part of the documented -DSENSOR_MODE contract");
static_assert(BOARD_MODE == EXPECT_BOARD,
              "BOARD_MODE did not resolve to what this compile's flags expect");
static_assert(SENSOR_MODE == EXPECT_SENSOR,
              "SENSOR_MODE did not resolve to what this compile's flags expect");

#if BOARD_MODE == BOARD_RELAY1CH
// The DEPLOYED production plant's table (HEAD e5a593b) -- frozen.
static_assert(PIN_PULSE == 1,
              "PRODUCTION REGRESSION: Relay-1CH pulse input must stay GPIO1 (breakout IO1)");
static_assert(PIN_CT_STATE == 2,
              "Relay-1CH CT input is breakout IO2/GPIO2 (unvalidated combo, but frozen)");
#elif BOARD_MODE == BOARD_8DI8DO
static_assert(PIN_PULSE == 4,
              "8DI-8DO pulse input must stay GPIO4 (DI1 terminal)");
static_assert(PIN_CT_STATE == 5,
              "8DI-8DO CT input must stay GPIO5 (DI2 terminal)");
#else
#error "test does not know this BOARD_MODE -- add a pin-table assertion block for it"
#endif

int main() {
  printf("board_mode=%d sensor_mode=%d pin_pulse=%d pin_ct_state=%d\n",
         (int)BOARD_MODE, (int)SENSOR_MODE, (int)PIN_PULSE, (int)PIN_CT_STATE);
  printf("ALL BOARD-CONFIG ASSERTIONS PASSED (they are compile-time; reaching main() is the proof)\n");
  return 0;
}
