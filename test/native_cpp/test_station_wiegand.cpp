// test_station_wiegand.cpp -- the 125 kHz reader's frames become the right
// card, and nothing else does (wiegand.h).
//
// Frames are built here from a facility code and card number the way a reader
// sends them (parity included), then fed through the collector bit by bit with
// real timing, so the frame gap and the glitch filter are exercised too.
//
//   g++ -std=c++11 -O1 -Wall -Wextra -I. -I../.. test_station_wiegand.cpp -o wg && ./wg
#include <stdio.h>
#include <string.h>

#include "wiegand.h"

using namespace station;

static int g_fail = 0;

#define EXPECT(cond, what)                       \
  do {                                           \
    if (!(cond)) {                               \
      printf("FAIL %s (line %d)\n", what, __LINE__); \
      g_fail++;                                  \
    }                                            \
  } while (0)

// `dataBits` of data, most significant first, wrapped in E ... O parity.
static uint8_t frame(uint32_t data, uint8_t dataBits, uint8_t* bits) {
  uint8_t n = dataBits + 2;
  for (uint8_t i = 0; i < dataBits; i++) bits[1 + i] = (data >> (dataBits - 1 - i)) & 1;
  uint8_t half = n / 2;
  uint8_t ones = 0;
  for (uint8_t i = 1; i < half; i++) ones += bits[i];
  bits[0] = ones % 2;  // makes the first half even
  ones = 0;
  for (uint8_t i = half; i < n - 1; i++) ones += bits[i];
  bits[n - 1] = (ones % 2) ? 0 : 1;  // makes the second half odd
  return n;
}

static WiegandResult decode(const uint8_t* bits, uint8_t n, char* uid) {
  uid[0] = 0;
  return decodeWiegand(bits, n, uid);
}

// Plays a frame into the collector from `startUs`, `spacingUs` between bits.
// Returns the time of the last bit.
static uint32_t play(WiegandCollector& c, const uint8_t* bits, uint8_t n, uint32_t startUs,
                     uint32_t spacingUs) {
  uint32_t t = startUs;
  for (uint8_t i = 0; i < n; i++) {
    c.addBit(bits[i], t);
    if (i + 1 < n) t += spacingUs;
  }
  return t;
}

int main() {
  uint8_t bits[WIEGAND_MAX_BITS + 1];
  char uid[17];

  // ── decoding ────────────────────────────────────────────────────────────
  // 26-bit: facility 19, card 24910 -> 0x13614E -> "0013614E".
  uint8_t n = frame((19UL << 16) | 24910UL, 24, bits);
  EXPECT(n == 26, "a 26-bit frame is 26 bits");
  EXPECT(decode(bits, n, uid) == WG_OK, "a good 26-bit frame decodes");
  EXPECT(strcmp(uid, "0013614E") == 0, "26-bit UID is 00 + facility + card, in hex");

  // Every 26-bit value the reader can send is accepted and round-trips.
  const uint32_t samples26[] = {0x000000UL, 0xFFFFFFUL, 0x000001UL, 0x800000UL, 0xA5A5A5UL};
  for (size_t i = 0; i < sizeof(samples26) / sizeof(samples26[0]); i++) {
    n = frame(samples26[i], 24, bits);
    char want[17];
    snprintf(want, sizeof(want), "%08lX", (unsigned long)samples26[i]);
    EXPECT(decode(bits, n, uid) == WG_OK && strcmp(uid, want) == 0, "26-bit sample round-trips");
  }

  // 34-bit: the full 32 bits.
  n = frame(0xDEADBEEFUL, 32, bits);
  EXPECT(n == 34, "a 34-bit frame is 34 bits");
  EXPECT(decode(bits, n, uid) == WG_OK, "a good 34-bit frame decodes");
  EXPECT(strcmp(uid, "DEADBEEF") == 0, "34-bit UID is the 32 data bits in hex");

  // The UID is one the station and the platform both accept.
  char norm[17];
  EXPECT(normaliseUid(uid, norm) && strcmp(norm, uid) == 0, "the UID passes normaliseUid as is");
  n = frame(0x000001UL, 24, bits);
  decode(bits, n, uid);
  EXPECT(normaliseUid(uid, norm) && strlen(norm) == 8, "a small 26-bit card is still 8 hex");

  // Any single flipped bit is refused: every bit is under one parity bit.
  for (uint8_t len = 24; len <= 32; len += 8) {
    uint8_t m = frame(0x5A5A5A5AUL & (len == 24 ? 0xFFFFFFUL : 0xFFFFFFFFUL), len, bits);
    for (uint8_t i = 0; i < m; i++) {
      bits[i] ^= 1;
      EXPECT(decode(bits, m, uid) == WG_BAD_PARITY && uid[0] == 0, "a flipped bit is refused");
      bits[i] ^= 1;
    }
  }

  // A stuck line is not a card: all zeros and all ones fail parity.
  memset(bits, 0, sizeof(bits));
  EXPECT(decode(bits, 26, uid) == WG_BAD_PARITY, "26 zeros (D0 stuck) is refused");
  memset(bits, 1, sizeof(bits));
  EXPECT(decode(bits, 26, uid) == WG_BAD_PARITY, "26 ones (D1 stuck) is refused");
  EXPECT(decode(bits, 34, uid) == WG_BAD_PARITY, "34 ones is refused");

  // Other lengths are refused, including a frame with one bit lost or gained.
  n = frame(0x13614EUL, 24, bits);
  EXPECT(decode(bits, 25, uid) == WG_BAD_LENGTH, "a lost bit is refused by length");
  EXPECT(decode(bits, 27, uid) == WG_BAD_LENGTH, "an extra bit is refused by length");
  EXPECT(decode(bits, 0, uid) == WG_BAD_LENGTH, "nothing is refused by length");
  EXPECT(decode(bits, 37, uid) == WG_BAD_LENGTH, "37-bit is not supported");

  // ── the collector ──────────────────────────────────────────────────────
  {
    WiegandCollector c;
    uint8_t out[WIEGAND_MAX_BITS + 1];
    uint8_t got = 0;
    EXPECT(!c.take(0, out, &got), "an idle line gives no frame");

    n = frame(0x13614EUL, 24, bits);
    uint32_t last = play(c, bits, n, 1000000UL, 2000UL);  // 2 ms apart, a typical reader
    EXPECT(!c.take(last + 1000UL, out, &got), "no frame while bits may still come");
    EXPECT(!c.take(last + WIEGAND_FRAME_GAP_US - 1, out, &got), "no frame before the gap");
    EXPECT(c.take(last + WIEGAND_FRAME_GAP_US, out, &got), "the frame after the gap");
    EXPECT(got == 26 && memcmp(out, bits, 26) == 0, "the collected frame is the sent one");
    EXPECT(decode(out, got, uid) == WG_OK && strcmp(uid, "0013614E") == 0, "it decodes");
    EXPECT(!c.take(last + 10 * WIEGAND_FRAME_GAP_US, out, &got), "a frame is handed over once");

    // The same card tapped again reads the same: 50 taps, 50 identical UIDs.
    uint32_t t = last + 500000UL;
    int same = 0;
    for (int tap = 0; tap < 50; tap++) {
      uint32_t end = play(c, bits, n, t, 1000UL);
      if (c.take(end + WIEGAND_FRAME_GAP_US, out, &got) && decode(out, got, uid) == WG_OK &&
          strcmp(uid, "0013614E") == 0)
        same++;
      t = end + 700000UL;
    }
    EXPECT(same == 50, "50 taps read 50 identical UIDs");
  }

  {
    // Edge ringing: a second edge 20 us after a bit is not a bit.
    WiegandCollector c;
    uint8_t out[WIEGAND_MAX_BITS + 1];
    uint8_t got = 0;
    n = frame(0xABCDEFUL, 24, bits);
    uint32_t t = 5000UL;
    for (uint8_t i = 0; i < n; i++) {
      c.addBit(bits[i], t);
      c.addBit(bits[i], t + 20);  // ringing on the opto edge
      t += 1500UL;
    }
    EXPECT(c.glitches() == 26, "each ringing edge is counted as a glitch");
    EXPECT(c.take(t + WIEGAND_FRAME_GAP_US, out, &got) && got == 26, "the frame survives ringing");
    EXPECT(decode(out, got, uid) == WG_OK && strcmp(uid, "00ABCDEF") == 0, "and decodes");
  }

  {
    // Noise that never stops is refused by length, and the next card reads.
    WiegandCollector c;
    uint8_t out[WIEGAND_MAX_BITS + 1];
    uint8_t got = 0;
    uint32_t t = 0;
    for (int i = 0; i < 100; i++) {
      c.addBit((uint8_t)(i & 1), t);
      t += 1000UL;
    }
    EXPECT(c.take(t + WIEGAND_FRAME_GAP_US, out, &got), "an overflowing burst is handed over");
    EXPECT(got == WIEGAND_MAX_BITS + 1, "marked as overflowed");
    EXPECT(decode(out, got, uid) == WG_BAD_LENGTH, "and refused by length");
    n = frame(0x13614EUL, 24, bits);
    uint32_t end = play(c, bits, n, t + 100000UL, 2000UL);
    EXPECT(c.take(end + WIEGAND_FRAME_GAP_US, out, &got) && decode(out, got, uid) == WG_OK &&
               strcmp(uid, "0013614E") == 0,
           "the next card after noise reads");
  }

  {
    // micros() wraps every ~71 minutes: a frame across the wrap still ends.
    WiegandCollector c;
    uint8_t out[WIEGAND_MAX_BITS + 1];
    uint8_t got = 0;
    n = frame(0x13614EUL, 24, bits);
    uint32_t end = play(c, bits, n, 0xFFFFFFFFUL - 20000UL, 2000UL);
    EXPECT(!c.take(end + 1000UL, out, &got), "no early frame across the wrap");
    EXPECT(c.take(end + WIEGAND_FRAME_GAP_US, out, &got) && got == 26, "the frame across the wrap");
    EXPECT(decode(out, got, uid) == WG_OK && strcmp(uid, "0013614E") == 0, "decodes");
  }

  // ── the bench readout ──────────────────────────────────────────────────
  {
    WiegandStats s;
    n = frame(0x13614EUL, 24, bits);
    decode(bits, n, uid);
    s.record(bits, n, WG_OK, uid);
    bits[3] ^= 1;
    s.record(bits, n, WG_BAD_PARITY, "");
    s.record(bits, 25, WG_BAD_LENGTH, "");
    EXPECT(s.frames == 3 && s.ok == 1 && s.badParity == 1 && s.badLength == 1, "counts by result");
    EXPECT(s.lastN == 25 && strlen(s.lastBits) == 25 && s.lastUid[0] == 0, "last frame shown");
  }

  printf("wiegand: %d failure(s)\n", g_fail);
  return g_fail ? 1 : 0;
}
