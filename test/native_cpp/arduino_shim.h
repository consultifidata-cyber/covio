// ============================================================================
// arduino_shim.h — minimal host-compilable stand-ins for the small subset of
// Arduino.h that queue.h's append()/FailureState code actually uses.
// ----------------------------------------------------------------------------
// RISK-04 phase-2 remediation: this is what lets queue.h's REAL, unmodified
// EventQueue::append() and FailureState logic compile and run on a plain
// host machine (g++, no Arduino, no ESP-IDF) -- the mandate's own bar
// ("if only a parallel reimplementation is tested, RISK-04 remains open").
// Included ONLY when NATIVE_TEST is defined; production ESP32 builds never
// see this file and use the real Arduino.h instead.
//
// Deliberately minimal: only the operations queue.h's append()/loadFail_()/
// persistFail_()/recordWriteFailure_() paths actually call. This is NOT a
// general Arduino compatibility shim.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>

// ---- String --------------------------------------------------------------
// Only what queue.h's append() uses: construct from const char*, operator+
// with String or const char*, c_str().
class String {
public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String operator+(const String& rhs) const { return String(s_ + rhs.s_); }
  String operator+(const char* rhs) const { return String(s_ + rhs); }
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.length(); }
private:
  std::string s_;
};
inline String operator+(const char* lhs, const String& rhs) { return String(std::string(lhs) + rhs.c_str()); }

// ---- Serial ----------------------------------------------------------------
// Test runs are quiet by default -- these mirror the real device's boot-time
// log lines to stdout only when COVIO_NATIVE_TEST_VERBOSE is set, so a CI
// log isn't flooded with expected fault-injection chatter by default.
class SerialShim {
public:
  void printf(const char* fmt, ...) {
    if (!verbose()) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
  void println(const char* s) { if (verbose()) printf("%s\n", s); }
private:
  static bool verbose() { return std::getenv("COVIO_NATIVE_TEST_VERBOSE") != nullptr; }
};
extern SerialShim Serial;

// ---- millis() ---------------------------------------------------------------
// A settable fake clock, NOT wall-clock time -- deterministic tests need to
// control "device uptime" precisely (e.g. to assert first/last failure
// timestamps advance exactly as expected), which a real clock cannot
// guarantee across a test run.
void nativeTestSetMillis(uint32_t ms);
uint32_t millis();
