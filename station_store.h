// station_store.h -- the station's last known good config on flash.
//
// Part 1 §K, MC-015 / MC-051: a config is applied whole or not at all, and the
// last one applied survives a reboot. This file is the "survives" half, kept
// apart from the board-only runtime so a host test can cut the power at every
// step (test_station_config.cpp).
//
// The file is "<sha256 hex>\n<body>". save() writes a temporary file, reads it
// back and checks it, then renames it over the old one; LittleFS replaces the
// destination in one step. At no moment is there no good copy on flash: a cut
// before the rename leaves the old file, after it the new one. load() takes the
// main file if it is whole, else the temporary one (a cut inside a filesystem
// that cannot replace on rename), else nothing -- and the caller still runs
// the full config validation on whatever it gets.
//
// `Fs` is anything with LittleFS's open/remove/rename; `File` what it opens.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace station {

// Content-Length as the server declared it (-1 when it sent none) and the
// bytes actually read: both must be inside the cap, or nothing is applied.
inline bool configLengthOk(long declared, size_t got, size_t cap) {
  if (declared > (long)cap) return false;
  return got <= cap;
}

template <class Fs>
class ConfigStore {
 public:
  typedef void (*Sha256Hex)(const uint8_t* data, size_t len, char out[65]);

  ConfigStore(Fs& fs, Sha256Hex sha, const char* path, const char* tmp)
      : fs_(fs), sha_(sha), path_(path), tmp_(tmp) {}

  // True when the new body is the copy on flash.
  bool save(const char* body, size_t len) {
    char hex[65];
    sha_((const uint8_t*)body, len, hex);
    {
      auto f = fs_.open(tmp_, "w");
      if (!f) return false;
      size_t wrote = f.write((const uint8_t*)hex, 64);
      wrote += f.write((const uint8_t*)"\n", 1);
      wrote += f.write((const uint8_t*)body, len);
      f.close();
      if (wrote != 65 + len || !verify(tmp_, nullptr, 0, nullptr)) {
        fs_.remove(tmp_);
        return false;
      }
    }
    if (afterTmp_) afterTmp_();  // test hook: a power cut here
    if (!fs_.rename(tmp_, path_)) {
      // A filesystem that will not replace: remove, then rename. load() takes
      // the temporary copy if the cut lands between the two.
      fs_.remove(path_);
      if (afterRemove_) afterRemove_();  // test hook
      if (!fs_.rename(tmp_, path_)) return false;
    }
    return true;
  }

  // The newest whole copy into `buf` (NUL-terminated). Its length, or -1.
  long load(char* buf, size_t cap, bool* fromTmp) {
    size_t n = 0;
    if (verify(path_, buf, cap, &n)) {
      if (fromTmp) *fromTmp = false;
      return (long)n;
    }
    if (verify(tmp_, buf, cap, &n)) {
      if (fromTmp) *fromTmp = true;
      fs_.rename(tmp_, path_);  // finish the interrupted save
      return (long)n;
    }
    return -1;
  }

  // Test hooks: run after the temporary file is complete, and after the old
  // file was removed on a filesystem that cannot replace on rename.
  void (*afterTmp_)() = nullptr;
  void (*afterRemove_)() = nullptr;

 private:
  // A file is whole when its first line is the sha256 of the rest.
  bool verify(const char* path, char* out, size_t cap, size_t* outLen) {
    auto f = fs_.open(path, "r");
    if (!f) return false;
    size_t size = f.size();
    if (size < 65 || (out && size - 65 >= cap)) {
      f.close();
      return false;
    }
    char want[65];
    bool ok = f.read((uint8_t*)want, 65) == 65 && want[64] == '\n';
    want[64] = 0;
    size_t len = size - 65;
    // Into the caller's buffer when there is one, else a temporary one.
    char* body = out;
    char* own = nullptr;
    if (!body) {
      own = new char[len + 1];
      body = own;
    }
    ok = ok && f.read((uint8_t*)body, len) == len;
    f.close();
    if (ok) {
      body[len] = 0;
      char got[65];
      sha_((const uint8_t*)body, len, got);
      ok = memcmp(got, want, 64) == 0;
    }
    if (own) delete[] own;
    if (ok && outLen) *outLen = len;
    return ok;
  }

  Fs& fs_;
  Sha256Hex sha_;
  const char* path_;
  const char* tmp_;
};

}  // namespace station
