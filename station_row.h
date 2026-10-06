// station_row.h -- a station event in the durable queue (record_type 2).
//
// The same 36-byte QRow as telemetry (queue.h), so the queue, its CRC, its
// checkpoints and its replay are untouched: a station row is just another row
// with the next global `seq`. Only the meaning of the three payload slots
// differs, and only for record_type 2 (SCHEMA_REGISTRY.md):
//
//   totalizer (u64) -> `arg`:  card UID bytes (big-endian) | lifetime total
//                              (alert) | config version (config events) |
//                              quiet length (quiet) | count (sound events)
//   quality   (u16) -> event code (low byte) | flags (high byte):
//                              tap: verdict (4 bits) + UID byte length (4 bits);
//                              quiet: source; config rejected: error code
//   rssi_abs  (u16) -> `aux`:  tap: the card's number; alert / running:
//                              seconds from the stop's start (clamped);
//
// On the wire (telemetry.h) a station row becomes exactly the JSON the platform
// parses (covio/src/lib/devices/station/protocol.ts) and the simulator sends:
// never a `totalizer` key, so an older server reading telemetry by
// (boot_id, ts, totalizer) can never mistake an event for a count.
//
// Two deliberate limits, recorded in the platform's decision register (MC-066):
// a card tap does not carry its config version (12 bytes cannot hold a UID, a
// verdict, a number and a version; the platform resolves a card by the event's
// time, MC-054, and every envelope carries the version applied), and a UID is
// at most 8 bytes (ISO 14443 single and double size; triple-size cards are
// refused at the reader).
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "station_logic.h"

#define RECORD_TYPE_STATION 2

namespace station {

// Config-rejection reasons travel as a code (the reference's words below).
static const char* const CONFIG_ERRORS[] = {
    "",          "header",      "line",       "window",   "card",    "truncated",
    "mode",      "cap:N",       "cap:seconds", "range:T", "checksum", "toolarge",
    "missing:VER", "missing:MODE", "missing:T", "missing:TS", "missing:SPOOL", "missing:C",
    "missing:R", "missing:N", "missing:Q", "missing:RECOVER", "missing:CORRECT", "missing:TZ",
};
static const int CONFIG_ERROR_COUNT = sizeof(CONFIG_ERRORS) / sizeof(CONFIG_ERRORS[0]);

inline uint8_t errorCode(const char* err) {
  for (int i = 1; i < CONFIG_ERROR_COUNT; i++)
    if (strcmp(CONFIG_ERRORS[i], err) == 0) return (uint8_t)i;
  return 0;
}

static const int MAX_UID_BYTES = 8;

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// UID hex -> big-endian bytes in a u64. False for an odd length, a non-hex
// character, or more than eight bytes.
inline bool packUid(const char* hex, uint64_t* out, uint8_t* bytes) {
  size_t n = strlen(hex);
  if (n == 0 || n % 2 != 0 || n / 2 > (size_t)MAX_UID_BYTES) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < n; i++) {
    int h = hexVal(hex[i]);
    if (h < 0) return false;
    v = (v << 4) | (uint64_t)h;
  }
  *out = v;
  *bytes = (uint8_t)(n / 2);
  return true;
}

inline void unpackUid(uint64_t v, uint8_t bytes, char* hex) {
  static const char* D = "0123456789ABCDEF";
  int n = bytes * 2;
  for (int i = n - 1; i >= 0; i--) {
    hex[i] = D[v & 0xF];
    v >>= 4;
  }
  hex[n] = 0;
}

// The payload of one station row, independent of the queue's struct.
struct StationPayload {
  uint64_t arg;
  uint16_t quality;
  uint16_t aux;
};

inline uint16_t clampU16(int64_t x) {
  if (x < 0) return 0;
  if (x > 65535) return 65535;
  return (uint16_t)x;
}

// Event (at uptime `ts`) -> payload. False when the event cannot be carried
// (a UID longer than eight bytes); the caller logs it and drops nothing else.
inline bool encodeEvent(const Event& e, uint32_t ts, StationPayload* p) {
  p->arg = 0;
  p->aux = 0;
  uint8_t flags = 0;
  switch (e.event) {
    case EV_CARD_TAPPED: {
      uint8_t bytes = 0;
      if (!e.hasUid || !packUid(e.uid, &p->arg, &bytes)) return false;
      flags = (uint8_t)(((e.hasV ? e.v : 0) & 0x0F) | ((bytes & 0x0F) << 4));
      p->aux = e.hasN ? clampU16(e.n) : 0;
      // A tap with a number of 0 (person, commissioning) and one with no
      // number are told apart by the verdict, exactly as the reference does.
      break;
    }
    case EV_ALERT_STARTED:
      p->arg = e.hasTotal ? e.total : 0;
      p->aux = e.hasStop ? clampU16((int64_t)ts - e.stopTs) : 0;
      break;
    case EV_RUNNING:
      p->aux = e.hasStop ? clampU16((int64_t)ts - e.stopTs) : 0;
      break;
    case EV_CONFIG_APPLIED:
    case EV_CONFIG_REJECTED:
      p->arg = e.hasCfg ? e.cfg : 0;
      flags = e.hasErr ? errorCode(e.err) : 0;
      break;
    case EV_QUIET_STARTED:
    case EV_QUIET_ENDED:
      flags = e.hasSrc ? e.src : 0;
      p->arg = e.hasN ? (uint64_t)e.n : 0;
      break;
    case EV_SOUND_PLAYED:
    case EV_ANNOUNCEMENTS_EXHAUSTED:
      p->arg = e.hasN ? (uint64_t)e.n : 0;
      break;
    default:
      break;
  }
  p->quality = (uint16_t)(e.event | ((uint16_t)flags << 8));
  return true;
}

// Payload -> the JSON fields after `"ts":N` (no braces), the platform's names.
inline void payloadJson(const StationPayload& p, uint32_t ts, char* buf, size_t len) {
  uint8_t ev = (uint8_t)(p.quality & 0xFF);
  uint8_t flags = (uint8_t)(p.quality >> 8);
  size_t o = 0;
  buf[0] = 0;
#define put(...) appendf(buf, len, &o, __VA_ARGS__)
  put("\"event\":%u", (unsigned)ev);
  switch (ev) {
    case EV_CARD_TAPPED: {
      char uid[2 * MAX_UID_BYTES + 1];
      unpackUid(p.arg, (uint8_t)(flags >> 4), uid);
      uint8_t v = flags & 0x0F;
      put(",\"uid\":\"%s\"", uid);
      put(",\"v\":%u", (unsigned)v);
      if (v == TV_ACTION || v == TV_ATTENDING || v == TV_QUIET) put(",\"n\":%u", (unsigned)p.aux);
      break;
    }
    case EV_ALERT_STARTED:
      put(",\"stop_ts\":%lld", (long long)ts - (long long)p.aux);
      put(",\"total\":%llu", (unsigned long long)p.arg);
      break;
    case EV_RUNNING:
      put(",\"stop_ts\":%lld", (long long)ts - (long long)p.aux);
      break;
    case EV_CONFIG_APPLIED:
      put(",\"cfg\":%llu", (unsigned long long)p.arg);
      break;
    case EV_CONFIG_REJECTED:
      put(",\"cfg\":%llu", (unsigned long long)p.arg);
      put(",\"err\":\"%s\"", flags < CONFIG_ERROR_COUNT ? CONFIG_ERRORS[flags] : "unknown");
      break;
    case EV_QUIET_STARTED:
      put(",\"src\":%u", (unsigned)flags);
      if (flags == 1) put(",\"n\":%llu", (unsigned long long)p.arg);
      break;
    case EV_QUIET_ENDED:
      put(",\"src\":%u", (unsigned)flags);
      break;
    case EV_SOUND_PLAYED:
    case EV_ANNOUNCEMENTS_EXHAUSTED:
      put(",\"n\":%llu", (unsigned long long)p.arg);
      break;
    default:
      break;
  }
#undef put
}

}  // namespace station
