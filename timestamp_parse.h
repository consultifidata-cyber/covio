// ============================================================================
// timestamp_parse.h — overflow-safe checked base-10 integer parsing
// ----------------------------------------------------------------------------
// Remediation for the OTA time-source defect found by real hardware
// validation (Docs/audit/coviu_oil_meter_p0_remediation_phase2/
// 34_OTA_SECURITY_HARDWARE_SUITE_RESULT.md): sync.h's extractLong_()
// accumulated server_time_ms (a 13-digit Unix MILLISECOND timestamp) into
// a `long`, which is 32 bits on arduino-esp32/Xtensa -- any real epoch-ms
// value overflows it, wraps to negative, and permanently fails the
// `serverTimeMs >= 0` gate that lets the OTA manifest-authenticity check
// ever see a valid time source.
//
// Deliberately dependency-free (no Arduino.h, no WiFi.h, no String) --
// same pattern this project already uses for ota_version_policy.h/
// ota_manifest_auth.h: the ARITHMETIC-CRITICAL part is extracted into a
// pure function operating on a raw char range, genuinely host-testable
// without any Arduino/ESP-IDF shim. sync.h's extractInt64_() is a thin
// wrapper that only does the Arduino-String field-location work (finding
// "key":<value> inside the response body) before handing the located
// digit range to parseNonNegativeInt64Checked() below.
// ============================================================================
#pragma once
#include <stdint.h>

// Parses a non-negative base-10 integer from s[start..end) into *out,
// checked against int64_t overflow BEFORE every multiply-add (never
// wraps, never relies on signed-overflow UB). Returns true and sets *out
// only if the entire range is one or more ASCII digits and the value
// fits in an int64_t; returns false (leaves *out untouched) for an empty
// range, any non-digit byte, or a value that would overflow --
// "fail closed on numeric overflow", per this remediation's own
// requirement, never silently wrap or truncate.
inline bool parseNonNegativeInt64Checked(const char* s, int start, int end, int64_t* out) {
  if (s == nullptr || start >= end) return false;
  int64_t v = 0;
  for (int i = start; i < end; i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;   // non-digit content anywhere in range -> reject
    int digit = c - '0';
    // Overflow check BEFORE the multiply-add, not after: `v*10+digit`
    // itself would already be undefined behavior for signed overflow if
    // computed first and only checked afterward. INT64_MAX/10 bounds how
    // large `v` may already be; the exact remaining headroom for this
    // specific digit is (INT64_MAX - digit) / 10 (integer division,
    // rounds toward zero, which is the correct conservative bound here).
    if (v > (INT64_MAX - digit) / 10) return false;
    v = v * 10 + digit;
  }
  *out = v;
  return true;
}
