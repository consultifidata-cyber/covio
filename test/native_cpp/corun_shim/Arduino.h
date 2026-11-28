// corun_shim/Arduino.h -- just enough Arduino for totalizer.h and queue.h.
//
// Used ONLY by test_station_corun.cpp, which compiles the REAL totalizer.h and
// the REAL queue.h exactly as the board does (no NATIVE_TEST: the production
// LittleFsBackend, segment rollover, pending(), ackThrough() and the boot-time
// orphan cleanup all run) on top of corun_shim/LittleFS.h. The include path
// puts this directory first, so `#include <Arduino.h>` lands here.
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String operator+(const String& rhs) const { return String(s_ + rhs.s_); }
  String operator+(const char* rhs) const { return String(s_ + rhs); }
  bool operator==(const char* rhs) const { return s_ == rhs; }
  char operator[](int i) const { return i >= 0 && (size_t)i < s_.size() ? s_[(size_t)i] : 0; }
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.length(); }
  int indexOf(const char* needle) const {
    size_t at = s_.find(needle);
    return at == std::string::npos ? -1 : (int)at;
  }

 private:
  std::string s_;
};
inline String operator+(const char* lhs, const String& rhs) {
  return String(std::string(lhs) + rhs.c_str());
}

class SerialShim {
 public:
  void printf(const char* fmt, ...) {
    if (!verbose()) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
  void println(const char* s) {
    if (verbose()) ::printf("%s\n", s);
  }
  void println(const String& s) { println(s.c_str()); }

 private:
  static bool verbose() { return getenv("COVIO_NATIVE_TEST_VERBOSE") != nullptr; }
};
static SerialShim Serial __attribute__((unused));

inline uint32_t& shimMillis() {
  static uint32_t ms = 0;
  return ms;
}
inline uint32_t millis() { return shimMillis(); }
inline void delay(uint32_t) {}

#define INPUT_PULLUP 0x05
inline void pinMode(int, int) {}
