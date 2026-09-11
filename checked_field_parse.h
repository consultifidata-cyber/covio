// ============================================================================
// checked_field_parse.h — overflow-checked uint32 field parsing for sync.h
// ----------------------------------------------------------------------------
// sync.h's extractLong_() (ack_seq in pushOnce(), K-factor "version" in
// pollConfig()) accumulated its digits into a `long` -- 32 bits on
// arduino-esp32/Xtensa -- with NO overflow check: `v = v*10 + digit` on
// every digit, silently wrapping (undefined behaviour for signed overflow,
// in practice a wrong value) past 2^31-1. That is the exact defect class
// server_time_ms was already fixed for (see timestamp_parse.h's own
// header comment) -- this extends the identical fix to the two fields
// that never got it.
//
// WHY THIS MATTERS FOR THESE TWO FIELDS SPECIFICALLY:
//   - ack_seq wrong risks incorrect local queue pruning (sync.h's own
//     boundAckSeq() clamp only catches an ack that EXCEEDS the batch's
//     highest sent seq -- a wrapped value that happens to land at or
//     below that ceiling would sail through unclamped).
//   - "version" wrong is worse: pollConfig() only re-applies a server-sent
//     K-factor when the parsed version differs from the stored one, so a
//     wrapped version number can make the device silently accept (or
//     silently ignore) a real K-factor change -- the number the business's
//     litres calculation depends on (see telemetry.h's own "THE
//     CALCULATION MODEL" comment).
//
// Both fields are used as uint32_t downstream (boundAckSeq()'s first
// argument; Store::setCfgVer()), so the ceiling here is UINT32_MAX, not
// INT64_MAX -- narrower than parseNonNegativeInt64Checked() alone, and
// this is is the only new behaviour: everything below UINT32_MAX still
// goes through that exact same already-tested, checked-before-every-
// multiply accumulator (timestamp_parse.h), not a reimplementation of it.
// ============================================================================
#pragma once
#include <stdint.h>
#include "timestamp_parse.h"

// Parses a non-negative base-10 integer from s[start..end) into *out.
// Returns true and sets *out only if the entire range is one or more ASCII
// digits AND the value fits in a uint32_t; returns false (leaves *out
// untouched) for an empty range, any non-digit byte, or a value that would
// not fit in uint32_t -- fail closed, never wraps, same contract as
// parseNonNegativeInt64Checked() itself, just a narrower ceiling.
inline bool parseUint32Checked(const char* s, int start, int end, uint32_t* out) {
  int64_t v;
  if (!parseNonNegativeInt64Checked(s, start, end, &v)) return false;
  if (v > (int64_t)UINT32_MAX) return false;
  *out = (uint32_t)v;
  return true;
}
