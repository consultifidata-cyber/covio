// station_logic.h -- THE MACHINE STATION, AS A PURE MACHINE.
//
// A line-for-line port of the platform's certified reference,
// covio/src/lib/devices/station/station-logic.ts (Machine Station Part 2 §D,
// §K, §V; Part 3 §AD). One call per second, the clock passed in, nothing
// Arduino: it is host-tested in test/native_cpp/test_station_parity.cpp
// against vectors GENERATED from that TypeScript, step by step. A difference
// between this file and those vectors is a difference between the board and
// the product, and the test fails.
//
// What the board knows: card CLASSES (A action, P person, Q quiet, C
// commissioning) and opaque NUMBERS. It never knows what an action means, a
// team, a person's name or an escalation step, and it never takes a sound
// command from the cloud. Sound is bounded HERE (SOUND_CAPS) whatever a config
// says, and again in the output driver (station_io.h).
//
// ⚠ This is logic. Light and sound here are values. Nothing about the real
// expander's reset state, the sounder's current or the light's wiring is
// proven by this file or its tests -- BENCH VERIFICATION REQUIRED.
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace station {

// ---- event and verdict catalogue (Part 2 §D; protocol.ts) -------------------
enum : uint8_t {
  EV_ARMED = 1,
  EV_ALERT_STARTED = 2,
  EV_SOUND_PLAYED = 3,
  EV_ANNOUNCEMENTS_EXHAUSTED = 4,
  EV_CARD_TAPPED = 5,
  EV_QUIET_STARTED = 6,
  EV_QUIET_ENDED = 7,
  EV_READER_LOST = 8,
  EV_READER_BACK = 9,
  EV_CONFIG_APPLIED = 10,
  EV_CONFIG_REJECTED = 11,
  EV_STATION_FAULT = 12,
  EV_SELF_TEST = 13,
  EV_RUNNING = 14,
};
enum : uint8_t {
  TV_ACTION = 1,
  TV_PERSON = 2,
  TV_QUIET = 3,
  TV_COMMISSIONING = 4,
  TV_UNKNOWN = 5,
  TV_NO_SITUATION = 6,
  TV_ATTENDING = 7,
};

// ---- firmware hard caps (Part 2 §V rule 4) ---------------------------------
static const uint32_t CAP_BURST_S = 10;
static const uint32_t CAP_PER_STOP = 5;
static const uint32_t CAP_SECONDS_PER_STOP = 60;
static const uint32_t ARM_AFTER_RUNNING_S = 60;
static const uint32_t SAME_CARD_IGNORE_S = 3;
static const double FULL_SPOOL_SHARE = 0.96;
static const uint32_t ANNOUNCEMENT_S = 4;

static const int MAX_CARDS = 1024;
static const int MAX_WINDOWS = 6;
static const int UID_MAX = 20;  // hex characters

enum Light : uint8_t { L_OFF = 0, L_GREEN = 1, L_AMBER = 2, L_RED_BLINK = 3, L_AMBER_BLINK = 4 };
enum Beep : uint8_t { B_NONE = 0, B_ACCEPTED = 1, B_PERSON = 2, B_REFUSED = 3 };

struct Card {
  char uid[UID_MAX + 1];
  char cls;  // 'A' 'P' 'Q' 'C'
  uint32_t number;
};

struct Window {
  char startsAt[6];
  char endsAt[6];
};

struct Config {
  uint32_t version;
  int mode;  // 0 off, 1 light, 2 light and sound
  int64_t stopAllowanceS;
  int64_t spoolAllowanceS;
  int64_t spoolPulses;
  int64_t soundDelayS;
  int64_t repeatS;
  int64_t announcements;
  int64_t quietS;
  int64_t recoverS;
  int64_t correctS;
  int64_t tzMinutes;
  int windowCount;
  Window windows[MAX_WINDOWS];
  int cardCount;
  Card cards[MAX_CARDS];  // sorted by uid once parsed (binary search)
};

// One event out. Every optional field carries its own presence flag, exactly
// as the reference's optional keys.
struct Event {
  uint8_t event;
  bool hasUid;
  char uid[UID_MAX + 1];
  bool hasV;
  uint8_t v;
  bool hasN;
  int64_t n;
  bool hasStop;
  int64_t stopTs;
  bool hasSrc;
  uint8_t src;
  bool hasCfg;
  uint32_t cfg;
  bool hasErr;
  char err[24];
  bool hasTotal;
  uint64_t total;
};

static const int MAX_EVENTS = 8;

struct TickInput {
  uint32_t uptimeS;
  bool hasWall;
  int64_t wallMs;
  uint32_t pulses;
  uint64_t total;
  const char* tap;  // nullptr or "" = no tap
  bool readerUp;
  bool inputSuspect;
  bool outputFault;
};

struct TickOutput {
  int eventCount;
  Event events[MAX_EVENTS];
  Light light;
  bool sounding;
  Beep beep;
};

// ---- small pure helpers -----------------------------------------------------

// parseClock (model.ts): ^([01][0-9]|2[0-3]):([0-5][0-9])$ -> minute of day, or -1.
inline int parseClock(const char* v) {
  if (strlen(v) != 5 || v[2] != ':') return -1;
  static const int DIGITS[4] = {0, 1, 3, 4};
  for (int k = 0; k < 4; k++)
    if (v[DIGITS[k]] < '0' || v[DIGITS[k]] > '9') return -1;
  int h = (v[0] - '0') * 10 + (v[1] - '0');
  int m = (v[3] - '0') * 10 + (v[4] - '0');
  if (v[0] > '2' || h > 23 || m > 59) return -1;
  return h * 60 + m;
}

inline bool insideWindow(int minute, const char* startsAt, const char* endsAt) {
  int s = parseClock(startsAt), e = parseClock(endsAt);
  if (s < 0 || e < 0 || s == e) return false;
  return s < e ? (minute >= s && minute < e) : (minute >= s || minute < e);
}

inline bool isDigits(const char* p, size_t n) {
  if (n == 0) return false;
  for (size_t i = 0; i < n; i++)
    if (p[i] < '0' || p[i] > '9') return false;
  return true;
}

// /^-?\d{1,9}$/ -> value; false when it does not match.
inline bool parseSmallInt(const char* v, int64_t* out) {
  const char* p = v;
  bool neg = false;
  if (*p == '-') {
    neg = true;
    p++;
  }
  size_t n = strlen(p);
  if (n < 1 || n > 9 || !isDigits(p, n)) return false;
  int64_t x = 0;
  for (size_t i = 0; i < n; i++) x = x * 10 + (p[i] - '0');
  *out = neg ? -x : x;
  return true;
}

inline int cardCmp(const void* a, const void* b) {
  return strcmp(static_cast<const Card*>(a)->uid, static_cast<const Card*>(b)->uid);
}

inline const Card* findCard(const Config& c, const char* uid) {
  Card key;
  strncpy(key.uid, uid, UID_MAX);
  key.uid[UID_MAX] = 0;
  return static_cast<const Card*>(bsearch(&key, c.cards, c.cardCount, sizeof(Card), cardCmp));
}

// ---- parseConfigBody --------------------------------------------------------
//
// The whole body or nothing (MC-051). `err` receives the reference's error
// word. Mirrors the TypeScript exactly, including the order checks are made
// in, a later duplicate key winning, and a duplicate card UID counting once.
inline bool parseConfigBody(const char* body, Config* out, char* err, size_t errLen) {
  static const char* REQUIRED[] = {"VER", "MODE", "T", "TS", "SPOOL", "C",
                                   "R",   "N",    "Q", "RECOVER", "CORRECT", "TZ"};
  const int NREQ = 12;
  struct KV {
    bool seen;
    char value[16];
    bool longValue;
  } kv[12];
  memset(kv, 0, sizeof(kv));
  memset(out, 0, sizeof(*out));
  bool haveEnd = false;
  bool endNaN = false;
  int64_t endValue = 0;
  bool overflowCards = false;

  const char* p = body;
  int lineNo = 0;
  char line[96];
  auto setErr = [&](const char* e) { snprintf(err, errLen, "%s", e); };
  while (true) {
    const char* nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    bool tooLong = len >= sizeof(line);
    size_t copy = tooLong ? sizeof(line) - 1 : len;
    memcpy(line, p, copy);
    line[copy] = 0;
    if (lineNo == 0) {
      if (tooLong || strcmp(line, "COVIO-STATION 1") != 0) {
        setErr("header");
        return false;
      }
    } else if (len != 0) {
      const char* eq = (const char*)memchr(p, '=', len);
      if (!eq || eq == p) {
        setErr("line");
        return false;
      }
      size_t klen = (size_t)(eq - p);
      const char* v = eq + 1;
      size_t vlen = len - klen - 1;
      char key[16];
      bool keyFits = klen < sizeof(key);
      if (keyFits) {
        memcpy(key, p, klen);
        key[klen] = 0;
      }
      if (keyFits && strcmp(key, "W") == 0) {
        // ^(\d\d:\d\d)-(\d\d:\d\d)$
        if (vlen != 11 || v[2] != ':' || v[5] != '-' || v[8] != ':' || !isDigits(v, 2) ||
            !isDigits(v + 3, 2) || !isDigits(v + 6, 2) || !isDigits(v + 9, 2)) {
          setErr("window");
          return false;
        }
        if (out->windowCount < MAX_WINDOWS) {
          Window& w = out->windows[out->windowCount++];
          memcpy(w.startsAt, v, 5);
          w.startsAt[5] = 0;
          memcpy(w.endsAt, v + 6, 5);
          w.endsAt[5] = 0;
        }
      } else if (keyFits && strcmp(key, "K") == 0) {
        // ^([0-9A-F]{8,20}),([APQC]),(\d{1,6})$
        const char* c1 = (const char*)memchr(v, ',', vlen);
        if (!c1) {
          setErr("card");
          return false;
        }
        size_t ulen = (size_t)(c1 - v);
        bool ok = ulen >= 8 && ulen <= 20;
        for (size_t i = 0; ok && i < ulen; i++)
          ok = (v[i] >= '0' && v[i] <= '9') || (v[i] >= 'A' && v[i] <= 'F');
        size_t rest = vlen - ulen - 1;
        const char* r = c1 + 1;
        ok = ok && rest >= 3 && (r[0] == 'A' || r[0] == 'P' || r[0] == 'Q' || r[0] == 'C') &&
             r[1] == ',';
        size_t nlen = ok ? rest - 2 : 0;
        ok = ok && nlen >= 1 && nlen <= 6 && isDigits(r + 2, nlen);
        if (!ok) {
          setErr("card");
          return false;
        }
        char uid[UID_MAX + 1];
        memcpy(uid, v, ulen);
        uid[ulen] = 0;
        uint32_t number = 0;
        for (size_t i = 0; i < nlen; i++) number = number * 10 + (uint32_t)(r[2 + i] - '0');
        // Map.set: a later line for the same UID replaces the earlier one.
        bool replaced = false;
        for (int i = 0; i < out->cardCount; i++) {
          if (strcmp(out->cards[i].uid, uid) == 0) {
            out->cards[i].cls = r[0];
            out->cards[i].number = number;
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          if (out->cardCount >= MAX_CARDS) {
            overflowCards = true;
          } else {
            Card& cd = out->cards[out->cardCount++];
            strcpy(cd.uid, uid);
            cd.cls = r[0];
            cd.number = number;
          }
        }
      } else if (keyFits && strcmp(key, "END") == 0) {
        // Number(v): trimmed; "" is 0; anything not a plain integer is NaN here.
        haveEnd = true;
        size_t a = 0, b = vlen;
        while (a < b && (v[a] == ' ' || v[a] == '\t')) a++;
        while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t')) b--;
        endNaN = false;
        endValue = 0;
        if (b > a) {
          if (!isDigits(v + a, b - a) || b - a > 12)
            endNaN = true;
          else
            for (size_t i = a; i < b; i++) endValue = endValue * 10 + (v[i] - '0');
        }
      } else if (keyFits) {
        for (int i = 0; i < NREQ; i++) {
          if (strcmp(key, REQUIRED[i]) == 0) {
            kv[i].seen = true;
            kv[i].longValue = vlen >= sizeof(kv[i].value);
            size_t n = kv[i].longValue ? sizeof(kv[i].value) - 1 : vlen;
            memcpy(kv[i].value, v, n);
            kv[i].value[n] = 0;
          }
        }
      }
    }
    lineNo++;
    if (!nl) break;
    p = nl + 1;
  }
  if (!haveEnd || endNaN || overflowCards || endValue != out->cardCount) {
    setErr("truncated");
    return false;
  }
  int64_t values[12];
  for (int i = 0; i < NREQ; i++) {
    if (!kv[i].seen || kv[i].longValue || !parseSmallInt(kv[i].value, &values[i])) {
      snprintf(err, errLen, "missing:%s", REQUIRED[i]);
      return false;
    }
  }
  // values: 0 VER 1 MODE 2 T 3 TS 4 SPOOL 5 C 6 R 7 N 8 Q 9 RECOVER 10 CORRECT 11 TZ
  if (values[1] != 0 && values[1] != 1 && values[1] != 2) {
    setErr("mode");
    return false;
  }
  if (values[7] < 1 || values[7] > (int64_t)CAP_PER_STOP) {
    setErr("cap:N");
    return false;
  }
  if (values[7] * (int64_t)ANNOUNCEMENT_S > (int64_t)CAP_SECONDS_PER_STOP) {
    setErr("cap:seconds");
    return false;
  }
  if (values[2] < 60) {
    setErr("range:T");
    return false;
  }
  out->version = (uint32_t)values[0];
  out->mode = (int)values[1];
  out->stopAllowanceS = values[2];
  out->spoolAllowanceS = values[3];
  out->spoolPulses = values[4];
  out->soundDelayS = values[5];
  out->repeatS = values[6];
  out->announcements = values[7];
  out->quietS = values[8];
  out->recoverS = values[9];
  out->correctS = values[10];
  out->tzMinutes = values[11];
  qsort(out->cards, out->cardCount, sizeof(Card), cardCmp);
  return true;
}

// The version a refused body claimed: /^VER=(\d+)$/m, or 0.
inline uint32_t claimedVersion(const char* body) {
  const char* p = body;
  while (p && *p) {
    const char* nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (len > 4 && strncmp(p, "VER=", 4) == 0 && isDigits(p + 4, len - 4)) {
      uint64_t x = 0;
      for (size_t i = 4; i < len; i++) x = x * 10 + (uint64_t)(p[i] - '0');
      return (uint32_t)x;
    }
    p = nl ? nl + 1 : nullptr;
  }
  return 0;
}

// ---- the machine ------------------------------------------------------------

class StationLogic {
 public:
  // A board's config can be large (1,024 cards): it lives where the caller
  // puts it (static storage on the device), never on a task's stack.
  explicit StationLogic(Config* storage) : cfg_(storage) {}

  bool hasConfig() const { return haveConfig_; }
  const Config& config() const { return *cfg_; }
  bool armed() const { return armed_; }
  bool acknowledged() const { return acknowledged_; }

  void boot(bool durableAck) {
    armed_ = false;
    hasRunSince_ = false;
    hasLastPulse_ = false;
    stopOpen_ = false;
    alerting_ = false;
    exhausted_ = false;
    sounds_ = 0;
    soundSeconds_ = 0;
    soundUntil_ = -1;
    acknowledged_ = durableAck;
    hasAckAction_ = false;
    attended_ = false;
    hasQuietUntil_ = false;
    quietBy_ = Q_NONE;
  }

  // Apply a downloaded body whole, or keep the old config and say why.
  // `scratch` is a second Config the caller owns: the body is parsed into it,
  // and only a body that passes every check replaces the live one.
  Event applyConfig(const char* body, Config* scratch) {
    Event e = blankEvent(EV_CONFIG_REJECTED);
    char err[24];
    if (!parseConfigBody(body, scratch, err, sizeof(err))) {
      e.hasCfg = true;
      e.cfg = claimedVersion(body);
      e.hasErr = true;
      snprintf(e.err, sizeof(e.err), "%s", err);
      return e;
    }
    memcpy(cfg_, scratch, sizeof(Config));
    haveConfig_ = true;
    e.event = EV_CONFIG_APPLIED;
    e.hasCfg = true;
    e.cfg = cfg_->version;
    return e;
  }

  // Restore a config already validated and applied before (NVS last-known-good).
  void restore(const Config& c) {
    memcpy(cfg_, &c, sizeof(Config));
    haveConfig_ = true;
  }

  void tick(const TickInput& in, TickOutput* out) {
    const int64_t t = in.uptimeS;
    const Config* c = haveConfig_ ? cfg_ : nullptr;
    out->eventCount = 0;
    out->beep = B_NONE;

    // ── pulses: running, arming, recovery ──────────────────────────────────
    if (in.pulses > 0) {
      if (stopOpen_ && hasLastPulse_) {
        Event& e = push(out, EV_RUNNING);
        e.hasStop = true;
        e.stopTs = stopSince_;
        stopOpen_ = false;
        hasRecoveredRunStart_ = true;
        recoveredRunStart_ = t;
      }
      if (!hasRunSince_) {
        hasRunSince_ = true;
        runSince_ = t;
        runPulses_ = 0;
      }
      runPulses_ += in.pulses;
      hasLastPulse_ = true;
      lastPulseS_ = t;
      if (!armed_ && t - runSince_ >= (int64_t)ARM_AFTER_RUNNING_S) {
        armed_ = true;
        push(out, EV_ARMED);
      }
      if (hasRecoveredRunStart_ && c && t - recoveredRunStart_ >= c->recoverS) {
        endStopContext();
      }
    } else if (hasLastPulse_ && !stopOpen_ && t - lastPulseS_ >= 1) {
      lastRunPulses_ = runPulses_;
      hasRunSince_ = false;
      hasRecoveredRunStart_ = false;
      bool fullSpool = c != nullptr && c->spoolAllowanceS > 0 && c->spoolPulses > 0 &&
                       (double)lastRunPulses_ >= FULL_SPOOL_SHARE * (double)c->spoolPulses;
      stopOpen_ = true;
      stopSince_ = lastPulseS_;
      stopInfinite_ = (c == nullptr);
      stopAllowance_ = c ? (fullSpool ? c->spoolAllowanceS : c->stopAllowanceS) : 0;
    }

    // ── quiet ──────────────────────────────────────────────────────────────
    bool windowQuiet = inWindow(in);
    bool cardQuiet = hasQuietUntil_ && t < quietUntil_;
    if (hasQuietUntil_ && t >= quietUntil_) {
      Event& e = push(out, EV_QUIET_ENDED);
      e.hasSrc = true;
      e.src = 1;
      hasQuietUntil_ = false;
      quietBy_ = Q_NONE;
      if (stopOpen_) stopSince_ = t;
    }
    if (windowQuiet && quietBy_ != Q_WINDOW) {
      quietBy_ = Q_WINDOW;
      Event& e = push(out, EV_QUIET_STARTED);
      e.hasSrc = true;
      e.src = 2;
    } else if (!windowQuiet && quietBy_ == Q_WINDOW) {
      quietBy_ = Q_NONE;
      Event& e = push(out, EV_QUIET_ENDED);
      e.hasSrc = true;
      e.src = 2;
      if (stopOpen_) stopSince_ = t;
    }
    bool quiet = windowQuiet || cardQuiet;

    // ── a tap ──────────────────────────────────────────────────────────────
    if (in.tap && in.tap[0]) {
      const char* uid = in.tap;
      bool repeat = hasLastTap_ && strcmp(lastTapUid_, uid) == 0 &&
                    t - lastTapAt_ < (int64_t)SAME_CARD_IGNORE_S;
      hasLastTap_ = true;
      snprintf(lastTapUid_, sizeof(lastTapUid_), "%s", uid);
      lastTapAt_ = t;
      if (!repeat) {
        const Card* card = c ? findCard(*c, uid) : nullptr;
        uint8_t v = TV_UNKNOWN;
        bool hasN = false;
        int64_t n = 0;
        if (!card) {
          out->beep = B_REFUSED;
        } else if (card->cls == 'A') {
          hasN = true;
          n = card->number;
          if (attended_ && hasAckAction_ && ackAction_ != card->number) {
            v = TV_ATTENDING;
            out->beep = B_REFUSED;
          } else {
            v = TV_ACTION;
            out->beep = B_ACCEPTED;
            if (stopOpen_ || stopContextOpen()) {
              acknowledged_ = true;
              hasAckAction_ = true;
              ackAction_ = card->number;
            }
          }
        } else if (card->cls == 'P') {
          if (stopContextOpen()) {
            v = TV_PERSON;
            out->beep = B_PERSON;
            acknowledged_ = true;
            attended_ = true;
          } else {
            v = TV_NO_SITUATION;
            out->beep = B_REFUSED;
          }
        } else if (card->cls == 'Q') {
          v = TV_QUIET;
          hasN = true;
          n = card->number;
          out->beep = B_ACCEPTED;
          hasQuietUntil_ = true;
          quietUntil_ = t + card->number;
          quietBy_ = Q_CARD;
          Event& e = push(out, EV_QUIET_STARTED);
          e.hasSrc = true;
          e.src = 1;
          e.hasN = true;
          e.n = card->number;
        } else {
          v = TV_COMMISSIONING;
          out->beep = B_ACCEPTED;
          push(out, EV_SELF_TEST);
        }
        Event& e = push(out, EV_CARD_TAPPED);
        e.hasUid = true;
        snprintf(e.uid, sizeof(e.uid), "%s", uid);
        e.hasV = true;
        e.v = v;
        e.hasN = hasN;
        e.n = n;
        e.hasCfg = true;
        e.cfg = c ? c->version : 0;
      }
    }

    // ── the alert and the audible gate (Part 2 §K) ─────────────────────────
    const int mode = c ? c->mode : 0;
    const bool fault = in.outputFault;
    if (!alerting_ && stopOpen_ && !stopInfinite_ && !acknowledged_ && !quiet && armed_ &&
        mode > 0 && !fault && !in.inputSuspect && t - stopSince_ >= stopAllowance_) {
      alerting_ = true;
      alertAt_ = t;
      nextSoundAt_ = t + (c ? c->soundDelayS : 0);
      Event& e = push(out, EV_ALERT_STARTED);
      e.hasStop = true;
      e.stopTs = stopSince_;
      e.hasTotal = true;
      e.total = in.total;
    }

    bool sounding = t <= soundUntil_;
    int64_t maxSounds = c ? c->announcements : 0;
    if (maxSounds > (int64_t)CAP_PER_STOP) maxSounds = CAP_PER_STOP;
    int64_t readerCap = in.readerUp ? (int64_t)CAP_PER_STOP : 1;
    if (maxSounds > readerCap) maxSounds = readerCap;
    bool gate = alerting_ && !acknowledged_ && !quiet && mode == 2 && !fault && !in.inputSuspect &&
                !(c && c->windowCount > 0 && !in.hasWall);
    if (gate && !sounding && t >= nextSoundAt_ && sounds_ < maxSounds) {
      int64_t length = ANNOUNCEMENT_S;
      if (length > (int64_t)CAP_BURST_S) length = CAP_BURST_S;
      int64_t left = (int64_t)CAP_SECONDS_PER_STOP - soundSeconds_;
      if (length > left) length = left;
      if (length > 0) {
        sounds_ += 1;
        soundSeconds_ += length;
        soundUntil_ = t + length - 1;
        nextSoundAt_ = t + (c ? c->repeatS : 0);
        sounding = true;
        Event& e = push(out, EV_SOUND_PLAYED);
        e.hasN = true;
        e.n = sounds_;
        if (sounds_ >= maxSounds) {
          exhausted_ = true;
          Event& x = push(out, EV_ANNOUNCEMENTS_EXHAUSTED);
          x.hasN = true;
          x.n = sounds_;
        }
      }
    }
    if (acknowledged_ || quiet || fault) {
      soundUntil_ = -1;
      sounding = false;
    }

    // ── light ──────────────────────────────────────────────────────────────
    Light light;
    if (mode == 0)
      light = L_OFF;
    else if (fault || in.inputSuspect)
      light = L_AMBER_BLINK;
    else if (alerting_ && !acknowledged_ && !quiet)
      light = L_RED_BLINK;
    // Green means pulses are arriving. A board that has not seen one since it
    // booted is UNARMED and shows amber steady (Part 2 §D; rows 54-56).
    else if (stopOpen_ || quiet || acknowledged_ || !hasLastPulse_)
      light = L_AMBER;
    else
      light = L_GREEN;
    out->light = light;
    out->sounding = sounding;
  }

 private:
  enum QuietBy : uint8_t { Q_NONE, Q_CARD, Q_WINDOW };

  static Event blankEvent(uint8_t code) {
    Event e;
    memset(&e, 0, sizeof(e));
    e.event = code;
    return e;
  }

  Event& push(TickOutput* out, uint8_t code) {
    // MAX_EVENTS is above the most one second can produce (running, armed,
    // quiet ended, quiet started, self-test, tap, alert, sound, exhausted is
    // nine only in a combination the rules exclude); the last slot is reused
    // rather than overrun.
    int i = out->eventCount < MAX_EVENTS ? out->eventCount++ : MAX_EVENTS - 1;
    out->events[i] = blankEvent(code);
    return out->events[i];
  }

  bool stopContextOpen() const { return alerting_ || acknowledged_; }

  void endStopContext() {
    stopOpen_ = false;
    alerting_ = false;
    exhausted_ = false;
    sounds_ = 0;
    soundSeconds_ = 0;
    acknowledged_ = false;
    hasAckAction_ = false;
    attended_ = false;
    hasRecoveredRunStart_ = false;
  }

  bool inWindow(const TickInput& in) const {
    if (!haveConfig_ || cfg_->windowCount == 0 || !in.hasWall) return false;
    double x = (double)in.wallMs / 60000.0 + (double)cfg_->tzMinutes;
    int minute = ((int)floor(fmod(x, 1440.0) + 1440.0)) % 1440;
    for (int i = 0; i < cfg_->windowCount; i++)
      if (insideWindow(minute, cfg_->windows[i].startsAt, cfg_->windows[i].endsAt)) return true;
    return false;
  }

  Config* cfg_;
  bool haveConfig_ = false;
  bool armed_ = false;
  bool hasRunSince_ = false;
  int64_t runSince_ = 0;
  bool hasLastPulse_ = false;
  int64_t lastPulseS_ = 0;
  bool stopOpen_ = false;
  bool stopInfinite_ = false;
  int64_t stopSince_ = 0;
  int64_t stopAllowance_ = 0;
  bool alerting_ = false;
  int64_t alertAt_ = 0;
  int64_t sounds_ = 0;
  int64_t soundSeconds_ = 0;
  int64_t soundUntil_ = -1;
  int64_t nextSoundAt_ = 0;
  bool exhausted_ = false;
  bool acknowledged_ = false;
  bool hasAckAction_ = false;
  uint32_t ackAction_ = 0;
  bool attended_ = false;
  bool hasRecoveredRunStart_ = false;
  int64_t recoveredRunStart_ = 0;
  bool hasQuietUntil_ = false;
  int64_t quietUntil_ = 0;
  QuietBy quietBy_ = Q_NONE;
  bool hasLastTap_ = false;
  char lastTapUid_[UID_MAX + 1] = {0};
  int64_t lastTapAt_ = 0;
  int64_t runPulses_ = 0;
  int64_t lastRunPulses_ = 0;
};

// Append printf-style output at *o, never past len (C++11: the board's toolchain).
inline void appendf(char* buf, size_t len, size_t* o, const char* fmt, ...) {
  if (*o >= len) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *o, len - *o, fmt, ap);
  va_end(ap);
  if (n > 0) *o += (size_t)n;
}

// The reference's event text, for the parity test and the console.
inline void eventText(const Event& e, char* buf, size_t len) {
  size_t o = 0;
  buf[0] = 0;
#define put(...) appendf(buf, len, &o, __VA_ARGS__)
  put("e=%u", (unsigned)e.event);
  if (e.hasUid) put(",uid=%s", e.uid);
  if (e.hasV) put(",v=%u", (unsigned)e.v);
  if (e.hasN) put(",n=%lld", (long long)e.n);
  if (e.hasStop) put(",stop=%lld", (long long)e.stopTs);
  if (e.hasSrc) put(",src=%u", (unsigned)e.src);
  if (e.hasCfg) put(",cfg=%lu", (unsigned long)e.cfg);
  if (e.hasErr) put(",err=%s", e.err);
  if (e.hasTotal) put(",total=%llu", (unsigned long long)e.total);
#undef put
}

}  // namespace station
