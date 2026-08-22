// ============================================================================
// test_https_budget.cpp — v1.3.1: the watchdog must outlive one bounded
// HTTPS transaction, and that bound must be the one the core actually has.
// ----------------------------------------------------------------------------
// config.h is a pure macro header (zero #includes), so it is host-compilable
// directly -- this file freezes the v1.3.1 timing invariant as static_asserts
// plus a few runtime checks that print the arithmetic, so a reviewer can see
// the budget rather than trust a comment.
//
// WHY THIS EXISTS. On 2026-08-22 the Balaji meter reset 15 times on its task
// watchdog in one day and went dark. Nothing in the firmware bounded the TLS
// handshake (arduino-esp32 2.0.x defaults WiFiClientSecure::handshake_timeout
// to 120 s; HTTPClient::setTimeout() does not touch it), so one stalled
// handshake on a lossy link outlived the 60 s watchdog. v1.3.1 bounds every
// HTTPS call and runs at most one per watchdog feed; the numbers below are
// the contract. If someone later raises an HTTPS_* bound, lowers the
// watchdog, or "tidies" the worst-case formula, this fails in CI before it
// fails at the plant.
//
// Build/run (see .github/workflows/ci.yml, firmware-native-fault-injection):
//   g++ -std=c++14 -Wall -I../.. test_https_budget.cpp -o t && ./t
// ============================================================================
#include <cstdio>
#include "../../config.h"

// --- the values v1.3.1 shipped with --------------------------------------
// Pinned on purpose: these are what the live fix was reasoned against. A
// deliberate retune must update BOTH this test and the config.h comment that
// derives HTTPS_WORST_CASE_TXN_MS from the 2.0.14 core's code paths.
static_assert(HTTPS_HANDSHAKE_TIMEOUT_S == 10,
              "v1.3.1 pins the TLS handshake bound at 10 s (core default was 120 s)");
static_assert(HTTPS_CONNECT_TIMEOUT_MS == 5000,
              "v1.3.1 pins the TCP connect bound at 5 s");
static_assert(HTTPS_IO_TIMEOUT_MS == 8000,
              "v1.3.1 pins the idle I/O bound at 8 s (the long-standing push value)");
static_assert(WATCHDOG_TIMEOUT_S == 90,
              "v1.3.1 raised the task watchdog to 90 s -- above the DERIVED worst case");

// --- the invariant itself --------------------------------------------------
// The handshake bound must be a strict improvement on the core default, and
// must itself fit the watchdog with room for the rest of the transaction.
static_assert(HTTPS_HANDSHAKE_TIMEOUT_S * 1000UL < 120000UL,
              "handshake bound must be below the 2.0.x core default of 120 s");
static_assert(HTTPS_HANDSHAKE_TIMEOUT_S * 1000UL < (unsigned long)WATCHDOG_TIMEOUT_S * 1000UL / 2UL,
              "handshake alone must use less than half the watchdog window");

// The derived worst case must equal the documented arithmetic (80 000 ms
// for the shipped values) and the watchdog must clear it with >= 5 s margin.
static_assert(HTTPS_WORST_CASE_TXN_MS == 80000UL,
              "HTTPS_WORST_CASE_TXN_MS no longer equals the documented 80 s derivation -- "
              "re-derive it from the core's code paths and update config.h's table");
static_assert((unsigned long)WATCHDOG_TIMEOUT_S * 1000UL >= HTTPS_WORST_CASE_TXN_MS + 5000UL,
              "watchdog must clear one worst-case HTTPS transaction by at least 5 s");

// Two transactions back-to-back would NOT fit -- which is exactly why loop()
// allows only one per iteration. Keep this true so nobody reads the 90 s as
// "room for several" and removes the netSlotUsed gate.
static_assert((unsigned long)WATCHDOG_TIMEOUT_S * 1000UL < 2UL * HTTPS_WORST_CASE_TXN_MS,
              "two worst-case transactions must NOT fit one watchdog window -- the one-"
              "transaction-per-iteration gate in loop() is load-bearing");

int main() {
  unsigned long wdt_ms = (unsigned long)WATCHDOG_TIMEOUT_S * 1000UL;
  std::printf("[https-budget] handshake=%lu ms connect=%lu ms io=%lu ms\n",
              (unsigned long)HTTPS_HANDSHAKE_TIMEOUT_S * 1000UL,
              (unsigned long)HTTPS_CONNECT_TIMEOUT_MS,
              (unsigned long)HTTPS_IO_TIMEOUT_MS);
  std::printf("[https-budget] worst-case one transaction = %lu ms\n",
              (unsigned long)HTTPS_WORST_CASE_TXN_MS);
  std::printf("[https-budget] watchdog = %lu ms (margin %lu ms)\n",
              wdt_ms, wdt_ms - (unsigned long)HTTPS_WORST_CASE_TXN_MS);
  if (wdt_ms <= HTTPS_WORST_CASE_TXN_MS) {
    std::printf("FAIL: watchdog does not clear one transaction\n");
    return 1;
  }
  std::printf("PASS: v1.3.1 HTTPS/watchdog budget holds\n");
  return 0;
}
