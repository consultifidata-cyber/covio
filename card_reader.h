// card_reader.h -- one interface for the station's RFID reader.
//
// The reader is NOT CHOSEN (Machine Station Part 1 §L: 13.56 MHz, RS485,
// 12-24 V, a published protocol, IP-rated; MODEL TO BE SELECTED AFTER BENCH
// TEST). So this file defines what the station needs from any reader and two
// implementations that are honest about that:
//
//   ConsoleCardReader -- cards "tapped" from the serial console
//                        (`station tap <uid>`) or the bench script. It is how
//                        the whole station is exercised before a reader exists,
//                        and it stays in every build as a commissioning aid.
//   Rs485CardReader   -- BLOCKED ON BENCH HARDWARE. Reports the reader as
//                        absent and never invents a card. The driver for the
//                        selected model replaces poll() and keeps the contract.
//
// The contract: poll() is called from the station task at least every 250 ms;
// it returns at most one UID per call, upper-case hex, 8 to 16 characters
// (4 to 8 bytes; a longer UID is refused here, see station_row.h); up() is
// false while the reader cannot be heard. A lost reader is a reported state,
// never a fatal one.
#pragma once

#include <stdint.h>
#include <string.h>

namespace station {

class CardReader {
 public:
  virtual ~CardReader() {}
  // A card read since the last call: fills `uid` (at least 17 bytes) and
  // returns true. Never blocks for longer than a few milliseconds.
  virtual bool poll(char* uid) = 0;
  virtual bool up() const = 0;
  virtual const char* kind() const = 0;
};

// Normalise and check a UID the way the platform does (normaliseUid):
// strip separators, upper-case, 8-16 hex characters here (<= 8 bytes).
inline bool normaliseUid(const char* in, char* out) {
  size_t n = 0;
  for (const char* p = in; *p; p++) {
    char c = *p;
    if (c == ':' || c == '-' || c == ' ') continue;
    if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (!hex || n >= 16) return false;
    out[n++] = c;
  }
  out[n] = 0;
  return n >= 8 && n % 2 == 0;
}

class ConsoleCardReader : public CardReader {
 public:
  // Called from the console (another task): a one-slot mailbox. A second tap
  // before the station has read the first replaces it, as a person tapping
  // twice quickly would.
  bool inject(const char* raw) {
    char uid[17];
    if (!normaliseUid(raw, uid)) return false;
    memcpy(pending_, uid, sizeof(uid));
    __atomic_store_n(&has_, true, __ATOMIC_RELEASE);
    return true;
  }
  bool poll(char* uid) override {
    if (!__atomic_load_n(&has_, __ATOMIC_ACQUIRE)) return false;
    memcpy(uid, pending_, sizeof(pending_));
    __atomic_store_n(&has_, false, __ATOMIC_RELEASE);
    return true;
  }
  bool up() const override { return true; }
  const char* kind() const override { return "console"; }

 private:
  char pending_[17] = {0};
  bool has_ = false;
};

// BLOCKED ON BENCH HARDWARE: no reader model is selected, so there is no
// protocol to speak. It reports "no reader" and never a card.
class Rs485CardReader : public CardReader {
 public:
  bool poll(char*) override { return false; }
  bool up() const override { return false; }
  const char* kind() const override { return "rs485-unselected"; }
};

}  // namespace station
