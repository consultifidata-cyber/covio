// test_station_config.cpp -- configuration apply, last known good, and limits.
//
// S6 §7 and §8. The station takes a config whole or not at all, keeps the last
// good one through any power cut, and reports what it applied. Three parts:
//
//   1. VALIDATION -- every way a body can be wrong is refused with its reason
//      and the config already running stays running, unchanged.
//   2. LAST KNOWN GOOD ON FLASH -- station_store.h against an in-memory flash
//      (corun_shim/LittleFS.h), with the power cut at every step of a save,
//      a damaged copy, a full flash, and a filesystem that cannot replace a
//      file on rename.
//   3. LIMITS -- 4/10, 10/50, 20/100, 20/500 actions/people and the maximum
//      (64 actions, 1,024 cards, 6 quiet windows): body bytes against the
//      32 KB cap, the board's RAM for two Config copies, parse time and card
//      lookup time ON THIS HOST. The same figures on the ESP32-S3 come from the
//      board itself (`station` console: parse_us, tick_us_max): BENCH REQUIRED.
//
//   g++ -std=c++11 -O2 -Wall -Wextra -Icorun_shim -I. -I../.. test_station_config.cpp -o cfg
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <string>
#include <vector>

#include <LittleFS.h>  // corun_shim: in-memory flash

#include "../../station_logic.h"
#include "../../station_store.h"

using namespace station;

static int g_fail = 0;
#define CHECK(cond, ...)               \
  do {                                 \
    if (!(cond)) {                     \
      printf("  FAIL line %d: ", __LINE__); \
      printf(__VA_ARGS__);             \
      printf("\n");                    \
      g_fail++;                        \
    }                                  \
  } while (0)

// ---- SHA-256 (FIPS 180-4), for the store's checksum on the host ------------
// The board uses mbedtls; the store only needs the same hex digest.
static void sha256Hex(const uint8_t* data, size_t len, char out[65]) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<uint8_t> m(data, data + len);
  uint64_t bits = (uint64_t)len * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (i * 8)));
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 |
             (uint32_t)m[off + 4 * i + 2] << 8 | (uint32_t)m[off + 4 * i + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + mj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
#undef ROR
  for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
}

// ---- configs ---------------------------------------------------------------
static std::string configBody(uint32_t version, int actions, int people, int windows,
                              const char* extra = "", bool withEnd = true) {
  std::string s = "COVIO-STATION 1\n";
  char line[96];
  snprintf(line, sizeof(line),
           "VER=%u\nMODE=2\nT=180\nTS=600\nSPOOL=0\nC=20\nR=180\nN=3\nQ=3600\nRECOVER=300\n"
           "CORRECT=120\nTZ=330\n",
           version);
  s += line;
  const char* clocks[] = {"10:00-10:15", "12:30-13:00", "15:00-15:15",
                          "18:00-18:10", "21:00-21:15", "02:00-02:30"};
  for (int i = 0; i < windows; i++) {
    s += "W=";
    s += clocks[i];
    s += "\n";
  }
  int cards = 0;
  for (int i = 0; i < actions; i++, cards++) {
    snprintf(line, sizeof(line), "K=04A%011X,A,%d\n", i, i + 1);  // 16 hex: 8-byte UID
    s += line;
  }
  for (int i = 0; i < people; i++, cards++) {
    snprintf(line, sizeof(line), "K=04B%011X,P,0\n", i);
    s += line;
  }
  s += extra;
  if (withEnd) {
    snprintf(line, sizeof(line), "END=%d\n", cards);
    s += line;
  }
  return s;
}

static Config g_live, g_scratch;

static std::string err(const Event& e) { return e.hasErr ? std::string(e.err) : std::string(); }

// ---- 1. validation -----------------------------------------------------------
static void testValidation() {
  printf("1. validation: a wrong body is refused whole; the running config stays\n");
  StationLogic logic(&g_live);
  logic.boot(false);
  std::string good = configBody(7, 4, 10, 2);
  Event e = logic.applyConfig(good.c_str(), &g_scratch);
  CHECK(e.event == EV_CONFIG_APPLIED && e.cfg == 7, "a valid config was not applied");
  CHECK(logic.config().cardCount == 14 && logic.config().windowCount == 2,
        "applied config has %d cards / %d windows", logic.config().cardCount,
        logic.config().windowCount);

  struct Bad {
    const char* what;
    std::string body;
    const char* reason;
  };
  std::string noEnd = configBody(8, 4, 10, 0, "", false);
  std::string wrongEnd = configBody(8, 4, 10, 0);
  wrongEnd.replace(wrongEnd.find("END=14"), 6, "END=13");
  std::string missingT = configBody(8, 4, 10, 0);
  missingT.replace(missingT.find("T=180\n"), 6, "");
  std::string badMode = configBody(8, 4, 10, 0);
  badMode.replace(badMode.find("MODE=2"), 6, "MODE=9");
  std::string bigN = configBody(8, 4, 10, 0);
  bigN.replace(bigN.find("N=3"), 3, "N=6");
  std::string shortT = configBody(8, 4, 10, 0);
  shortT.replace(shortT.find("T=180\n"), 6, "T=30\n");
  std::string tooMany = configBody(8, 64, 961, 0);  // 1,025 cards
  std::string badCard = configBody(8, 4, 10, 0, "K=XYZ,A,1\n");
  std::string badWindow = configBody(8, 4, 10, 0, "W=1:00-2:00\n");
  std::string longUid = configBody(8, 4, 10, 0, "K=0123456789ABCDEF01234,A,1\n");
  std::vector<Bad> bad;
  bad.push_back({"wrong header", "COVIO-STATION 2\nVER=8\n", "header"});
  bad.push_back({"a line with no key", "COVIO-STATION 1\ngarbage\n", "line"});
  bad.push_back({"no END (truncated in transit)", noEnd, "truncated"});
  bad.push_back({"END disagrees with the cards", wrongEnd, "truncated"});
  bad.push_back({"a required field missing (T)", missingT, "missing:T"});
  bad.push_back({"mode out of range", badMode, "mode"});
  bad.push_back({"more announcements than the cap", bigN, "cap:N"});
  bad.push_back({"stop allowance under 60 s", shortT, "range:T"});
  bad.push_back({"1,025 cards (over the limit)", tooMany, "truncated"});
  bad.push_back({"a malformed card line", badCard, "card"});
  bad.push_back({"a malformed quiet window", badWindow, "window"});
  bad.push_back({"a 21-hex-character UID", longUid, "card"});
  bad.push_back({"empty body", "", "header"});
  for (size_t i = 0; i < bad.size(); i++) {
    Event r = logic.applyConfig(bad[i].body.c_str(), &g_scratch);
    bool ok = r.event == EV_CONFIG_REJECTED && err(r) == bad[i].reason;
    CHECK(ok, "%s: want rejected \"%s\", got event %u \"%s\"", bad[i].what, bad[i].reason,
          r.event, err(r).c_str());
    CHECK(logic.hasConfig() && logic.config().version == 7 && logic.config().cardCount == 14,
          "%s: the running config changed (v%u, %d cards)", bad[i].what, logic.config().version,
          logic.config().cardCount);
    printf("   refused %-34s -> %-11s running config still v%u\n", bad[i].what,
           err(r).c_str(), logic.config().version);
  }

  // An unknown field is ignored, exactly as the platform's parser ignores it:
  // a newer server can add a field without bricking older boards.
  std::string unknown = configBody(9, 4, 10, 0, "FUTURE_FIELD=1\n");
  e = logic.applyConfig(unknown.c_str(), &g_scratch);
  CHECK(e.event == EV_CONFIG_APPLIED && logic.config().version == 9,
        "an unknown field was not ignored");
  printf("   accepted an unknown field (FUTURE_FIELD=1)   -> applied v%u\n",
         logic.config().version);

  // Older version from the server: applied. The server sends its NEWEST
  // version whole (serve.ts); versions only go backwards after a database
  // restore, and then the server's answer is the truth. The board never
  // refuses the server's newest for being lower than what it holds.
  std::string older = configBody(5, 4, 10, 0);
  e = logic.applyConfig(older.c_str(), &g_scratch);
  CHECK(e.event == EV_CONFIG_APPLIED && logic.config().version == 5,
        "an older version from the server was not applied");
  printf("   server sends an older version (v5)           -> applied v5 (server is the truth)\n");
  // Same version: the server answers "UNCHANGED" and the board applies nothing
  // (station_runtime.h pollConfig); the logic is never called. Applying the
  // same body twice anyway is harmless:
  e = logic.applyConfig(older.c_str(), &g_scratch);
  CHECK(e.event == EV_CONFIG_APPLIED && logic.config().version == 5, "re-apply changed things");

  // Lengths: a declared or actual length over the cap is refused before parse.
  CHECK(configLengthOk(-1, 1000, 32768), "no Content-Length, small body: refused");
  CHECK(!configLengthOk(-1, 32769, 32768), "no Content-Length, oversized body: accepted");
  CHECK(!configLengthOk(40000, 0, 32768), "declared oversized: accepted");
  CHECK(configLengthOk(32768, 32768, 32768), "exactly the cap: refused");
  printf("   oversized: declared 40,000 B or read 32,769 B with no length -> refused\n");
}

// ---- 2. last known good on flash ----------------------------------------------
static ConfigStore<LittleFSShim>* g_store = nullptr;
static bool g_cutNow = false;
static void cutHere() { g_cutNow = true; }

// A filesystem whose rename does not replace an existing file.
struct NoReplaceFs {
  File open(const char* p, const char* m) { return LittleFS.open(p, m); }
  bool remove(const char* p) { return LittleFS.remove(p); }
  bool rename(const char* a, const char* b) {
    if (LittleFS.exists(b)) return false;
    return LittleFS.rename(a, b);
  }
};

static std::string loadBody(long* n) {
  static char buf[40000];
  bool fromTmp = false;
  *n = g_store->load(buf, sizeof(buf), &fromTmp);
  return *n >= 0 ? std::string(buf, (size_t)*n) : std::string();
}

static void testStore() {
  printf("2. last known good on flash\n");
  shimFs() = ShimFs();
  ConfigStore<LittleFSShim> store(LittleFS, sha256Hex, "/station.cfg", "/station.cfg.tmp");
  g_store = &store;
  std::string v1 = configBody(1, 4, 10, 0), v2 = configBody(2, 4, 10, 0);
  long n;

  CHECK(store.save(v1.c_str(), v1.size()), "save v1 failed");
  CHECK(loadBody(&n) == v1, "v1 did not load back");
  printf("   save + load                                 -> v1 back, byte for byte\n");

  // Power cut after the new copy is complete, before the rename: the old copy
  // is untouched and loads; the server sends v2 again on the next poll.
  store.afterTmp_ = cutHere;
  g_cutNow = false;
  // Simulate the cut: save() runs to the hook; then we discard what follows by
  // restoring the main file as it was before the rename.
  std::vector<uint8_t> before = shimFs().files["/station.cfg"];
  store.save(v2.c_str(), v2.size());
  store.afterTmp_ = nullptr;
  CHECK(g_cutNow, "the hook did not run");
  shimFs().files["/station.cfg"] = before;  // the rename never happened
  {
    // The tmp is gone after a completed rename; recreate it as the cut left it.
    std::string full;
    char hex[65];
    sha256Hex((const uint8_t*)v2.data(), v2.size(), hex);
    full = std::string(hex, 64) + "\n" + v2;
    shimFs().files["/station.cfg.tmp"] = std::vector<uint8_t>(full.begin(), full.end());
  }
  CHECK(loadBody(&n) == v1, "a cut before the rename did not keep v1");
  printf("   cut after the new copy, before the rename   -> v1 (the old copy is untouched)\n");

  // Power cut DURING the write of the new copy: a torn temporary file.
  store.save(v1.c_str(), v1.size());
  {
    char hex[65];
    sha256Hex((const uint8_t*)v2.data(), v2.size(), hex);
    std::string torn = std::string(hex, 64) + "\n" + v2.substr(0, v2.size() / 2);
    shimFs().files["/station.cfg.tmp"] = std::vector<uint8_t>(torn.begin(), torn.end());
  }
  CHECK(loadBody(&n) == v1, "a torn temporary file displaced v1");
  printf("   cut during the write (a torn new copy)      -> v1\n");

  // A damaged main copy (one flipped byte) with no good temporary: nothing.
  shimFs().files.erase("/station.cfg.tmp");
  shimFs().files["/station.cfg"][100] ^= 0x01;
  CHECK(loadBody(&n).empty() && n == -1, "a damaged copy was loaded");
  printf("   one flipped byte, no other copy            -> nothing: silent and dark until the "
         "server sends one\n");

  // A damaged main copy with a whole temporary: the temporary, and it is made
  // the main copy.
  {
    store.save(v2.c_str(), v2.size());
    std::vector<uint8_t> good = shimFs().files["/station.cfg"];
    shimFs().files["/station.cfg.tmp"] = good;
    shimFs().files["/station.cfg"][80] ^= 0x40;
    CHECK(loadBody(&n) == v2, "the whole temporary copy was not taken");
    CHECK(shimFs().files.count("/station.cfg.tmp") == 0 &&
              shimFs().files["/station.cfg"] == good,
          "the interrupted save was not finished");
  }
  printf("   damaged main copy, whole temporary copy     -> the temporary (v2), promoted\n");

  // A full flash: the save fails and the old copy stays.
  store.save(v1.c_str(), v1.size());
  size_t cap = shimFs().capBytes;
  shimFs().capBytes = shimFs().used() + 100;
  CHECK(!store.save(v2.c_str(), v2.size()), "a save on a full flash reported success");
  shimFs().capBytes = cap;
  CHECK(loadBody(&n) == v1, "a failed save displaced v1");
  CHECK(shimFs().files.count("/station.cfg.tmp") == 0, "a failed save left its temporary file");
  printf("   flash full during the save                  -> save refused, v1 stays\n");

  // A filesystem that cannot replace on rename, cut between remove and rename.
  {
    shimFs() = ShimFs();
    NoReplaceFs nfs;
    ConfigStore<NoReplaceFs> ns(nfs, sha256Hex, "/station.cfg", "/station.cfg.tmp");
    CHECK(ns.save(v1.c_str(), v1.size()), "no-replace save v1");
    std::vector<uint8_t> tmpCopy;
    ns.afterRemove_ = cutHere;
    g_cutNow = false;
    CHECK(ns.save(v2.c_str(), v2.size()), "no-replace save v2");
    CHECK(g_cutNow, "the remove-then-rename path did not run");
    // The cut: the old file is removed and the rename never ran. Recreate the
    // state: no main, the whole new temporary.
    std::vector<uint8_t> v2File = shimFs().files["/station.cfg"];
    shimFs().files.erase("/station.cfg");
    shimFs().files["/station.cfg.tmp"] = v2File;
    static char buf[40000];
    bool fromTmp = false;
    long m = ns.load(buf, sizeof(buf), &fromTmp);
    CHECK(m >= 0 && std::string(buf, (size_t)m) == v2 && fromTmp,
          "the cut between remove and rename lost the config");
    printf("   no-replace rename, cut between remove/rename -> v2 from the temporary copy\n");
  }

  // Reboot during activation: the logic took v2 in RAM, the save never ran.
  // The board boots on v1 from flash and reports v1; the server sends v2 again.
  {
    shimFs() = ShimFs();
    ConfigStore<LittleFSShim> s2(LittleFS, sha256Hex, "/station.cfg", "/station.cfg.tmp");
    g_store = &s2;
    s2.save(v1.c_str(), v1.size());
    StationLogic logic(&g_live);
    logic.applyConfig(v2.c_str(), &g_scratch);  // applied in RAM ...
    // ... power cut before the save
    StationLogic after(&g_live);
    after.boot(false);
    std::string onFlash = loadBody(&n);
    Event e = after.applyConfig(onFlash.c_str(), &g_scratch);
    CHECK(e.event == EV_CONFIG_APPLIED && after.config().version == 1,
          "a reboot during activation did not come back on v1");
    printf("   reboot after applying in RAM, before saving -> boots on v1, reports v1; the "
           "server resends v2\n");
  }
  g_store = nullptr;
}

// ---- 3. limits ------------------------------------------------------------------
static double nowUs() {
  return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
             .count() /
         1000.0;
}

static void testLimits() {
  printf("3. limits (times ON THIS HOST; the board's own numbers are BENCH REQUIRED)\n");
  printf("   sizeof(Config) = %zu bytes; the board holds two (live + scratch) = %zu bytes, plus a "
         "%d-byte download buffer\n",
         sizeof(Config), 2 * sizeof(Config), 32 * 1024 + 1);
  struct Size {
    const char* name;
    int actions, people, windows;
  };
  Size sizes[] = {{"4 actions + 10 people", 4, 10, 1},
                  {"10 actions + 50 people", 10, 50, 2},
                  {"20 actions + 100 people", 20, 100, 3},
                  {"20 actions + 500 people", 20, 500, 4},
                  {"maximum: 64 actions + 960 people", 64, 960, 6}};
  StationLogic logic(&g_live);
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    std::string body = configBody(100 + (uint32_t)i, sizes[i].actions, sizes[i].people,
                                  sizes[i].windows);
    const int runs = 20;
    double t0 = nowUs();
    Event e = logic.applyConfig(body.c_str(), &g_scratch);
    for (int r = 1; r < runs; r++) e = logic.applyConfig(body.c_str(), &g_scratch);
    double parseUs = (nowUs() - t0) / runs;
    CHECK(e.event == EV_CONFIG_APPLIED, "%s was refused (%s)", sizes[i].name, err(e).c_str());
    CHECK(body.size() <= 32 * 1024, "%s is %zu bytes, over the 32 KB cap", sizes[i].name,
          body.size());
    // Lookup: hits and misses, as a tap would.
    const int lookups = 200000;
    char uid[21];
    int hits = 0;
    double l0 = nowUs();
    for (int k = 0; k < lookups; k++) {
      int which = k % (sizes[i].people + 1);
      snprintf(uid, sizeof(uid), "04B%011X", which);  // the last one misses
      if (findCard(logic.config(), uid)) hits++;
    }
    double lookupUs = (nowUs() - l0) / lookups;
    CHECK(hits > 0, "no card found");
    printf("   %-33s %5d cards  %6zu B (%4.1f%% of cap)  parse %8.1f us  lookup %.3f us\n",
           sizes[i].name, logic.config().cardCount, body.size(),
           100.0 * (double)body.size() / (32 * 1024), parseUs, lookupUs);
  }
}

int main() {
  testValidation();
  testStore();
  testLimits();
  printf("\n%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
