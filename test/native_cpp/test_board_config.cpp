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

// Miki Wire hardening: the plant-profile layer must be compile-time ABSENT
// from a flag-less build (the deployed Balaji baseline). EXPECT_MIKI states
// what the harness passed, mirroring EXPECT_BOARD/EXPECT_SENSOR above; the
// default expectation is OFF.
#ifndef EXPECT_MIKI
#define EXPECT_MIKI 0
#endif
static_assert(MIKI_WIRE_PROFILE == EXPECT_MIKI,
              "BALAJI PROTECTION: MIKI_WIRE_PROFILE must be OFF in a flag-less build "
              "(and ON only when -DMIKI_WIRE_PROFILE=1 was explicitly passed)");

// ---------------------------------------------------------------------------
// PRODUCT IDENTITY / OTA CROSS-FLASH GUARD
// ---------------------------------------------------------------------------
// ota.h sends DEVICE_MODEL as the manifest's `hw_compat` field and refuses any
// candidate that does not match it exactly. That string is the ONLY barrier
// between the two products' OTA channels: if both built to the same value, an
// oil-flow image would be accepted by the Miki unit (and vice versa), and
// because the two boards read the sensor on different GPIOs (GPIO1 vs GPIO4),
// the cross-flashed unit would go on running while silently counting nothing.
// Freeze both literals here so that failure can only ever be a compile error.
constexpr bool ceStrEq(const char* a, const char* b) {
  return (*a == *b) && (*a == '\0' || ceStrEq(a + 1, b + 1));
}

// ---------------------------------------------------------------------------
// ANTI-DOWNGRADE FLOOR -- do not lower this without reading the story first.
// ---------------------------------------------------------------------------
// The commissioned Balaji meter carries accepted_security_floor = 2 in NVS.
// ota_version_policy.h refuses any candidate whose security_version is
// strictly below that floor, so an image built with FW_SECURITY_VERSION < 2
// is rejected as "downgrade_rejected" on every single poll -- which is exactly
// what silently blocked every OTA that device was ever offered.
//
// The floor is a monotonic ratchet in NVS and cannot be lowered from the
// cloud. Dropping this constant back to 1 would therefore not just regress a
// build flag, it would re-lock a production meter out of updates with no
// remote way to recover it.
static_assert(FW_SECURITY_VERSION >= 2,
              "FW_SECURITY_VERSION must stay >= 2: the deployed Balaji meter's "
              "anti-downgrade floor is 2, and anything lower is rejected as a "
              "downgrade on every OTA poll, permanently.");

constexpr const char* HW_COMPAT_OILFLOW  = "covio-oilflow-v1";
constexpr const char* HW_COMPAT_MIKIWIRE = "miki-wire-v1";
static_assert(!ceStrEq(HW_COMPAT_OILFLOW, HW_COMPAT_MIKIWIRE),
              "the two products must never share an OTA hw_compat identity");

#if MIKI_WIRE_PROFILE
static_assert(ceStrEq(DEVICE_MODEL, HW_COMPAT_MIKIWIRE),
              "Miki build must advertise hw_compat 'miki-wire-v1'");
static_assert(!ceStrEq(DEVICE_MODEL, HW_COMPAT_OILFLOW),
              "CROSS-FLASH RISK: a Miki build must NEVER claim the oil-flow hw_compat");
static_assert(ceStrEq(DEFAULT_SERVER_URL, "https://compliance.mikigroup.co.in"),
              "Miki build must default to the Miki backend");
#else
// ⚠ FROZEN: the commissioned Balaji meter compares against this exact string
// on every OTA poll. Changing it cuts that meter off from OTA until it is
// reflashed over USB.
static_assert(ceStrEq(DEVICE_MODEL, HW_COMPAT_OILFLOW),
              "BALAJI PROTECTION: the flag-less build must keep hw_compat 'covio-oilflow-v1'");
static_assert(!ceStrEq(DEVICE_MODEL, HW_COMPAT_MIKIWIRE),
              "CROSS-FLASH RISK: an oil-flow build must NEVER claim the Miki hw_compat");
static_assert(ceStrEq(DEFAULT_SERVER_URL, "https://data.funtastik.co.in"),
              "oil-flow build must default to the Balaji backend");
#endif

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
  printf("miki_profile=%d fw_version=%s hw_compat=%s server=%s\n",
         (int)MIKI_WIRE_PROFILE, FW_VERSION, DEVICE_MODEL, DEFAULT_SERVER_URL);
  printf("ALL BOARD-CONFIG ASSERTIONS PASSED (they are compile-time; reaching main() is the proof)\n");
  return 0;
}
