// ============================================================================
// fake_storage_backend.h — deterministic fault-injecting StorageBackend
// ----------------------------------------------------------------------------
// RISK-04 phase-2 remediation. An in-memory filesystem simulation, NOT a
// reimplementation of queue.h's logic -- it only stores/retrieves bytes by
// path and lets a test script the exact fault to inject on the NEXT call to
// each operation, so the test proves how the REAL EventQueue::append() (and
// the real FailureState/AckRec persist/load logic in queue.h) behaves when
// the filesystem misbehaves.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../../storage_backend.h"

class FakeStorageBackend : public StorageBackend {
public:
  // ---- fault injection controls (set by a test before calling into EventQueue) ----
  bool failNextAppendOpenSize = false;
  bool failNextTruncate = false;
  bool failNextAppendWrite = false;
  long forceShortWriteLen = -1;          // if >=0, appendWrite "writes" exactly this many bytes instead of `len`
  bool failNextWriteWhole = false;
  bool corruptOnNextReadWhole_path = false;  // see corruptPath below
  std::string corruptPath;                    // if set, readWhole() for this exact path returns garbage

  size_t totalBytesValue = 4 * 1024 * 1024;   // simulated partition size; tests override to hit thresholds
  size_t usedBytesOverride = (size_t)-1;      // if not -1, usedBytes() returns this instead of the real tally

  // ---- StorageBackend ----
  long appendOpenSize(const char* path) override {
    if (failNextAppendOpenSize) { failNextAppendOpenSize = false; return -1; }
    return (long)files_[path].size();
  }

  bool truncateTo(const char* path, size_t toSize) override {
    if (failNextTruncate) { failNextTruncate = false; return false; }
    auto& buf = files_[path];
    if (toSize < buf.size()) buf.resize(toSize);
    return true;
  }

  long appendWrite(const char* path, const uint8_t* data, size_t len) override {
    if (failNextAppendWrite) { failNextAppendWrite = false; return -1; }
    size_t actualLen = len;
    if (forceShortWriteLen >= 0) {
      actualLen = (size_t)forceShortWriteLen;
      forceShortWriteLen = -1;
    }
    auto& buf = files_[path];
    buf.insert(buf.end(), data, data + actualLen);
    return (long)actualLen;
  }

  long readWhole(const char* path, uint8_t* out, size_t outCap) override {
    if (corruptPath == path) {
      // Simulate a corrupted slot: return the right LENGTH (so the caller's
      // size check passes) but garbage bytes (so its CRC/magic check must
      // catch it) -- this is what a real torn/corrupted flash sector looks
      // like to the reader, not an outright missing file.
      size_t n = std::min(outCap, (size_t)32);
      memset(out, 0xFF, n);
      return (long)n;
    }
    auto it = files_.find(path);
    if (it == files_.end()) return -1;
    size_t n = std::min(outCap, it->second.size());
    memcpy(out, it->second.data(), n);
    return (long)n;
  }

  bool writeWhole(const char* path, const uint8_t* data, size_t len) override {
    if (failNextWriteWhole) { failNextWriteWhole = false; return false; }
    files_[path].assign(data, data + len);
    return true;
  }

  size_t totalBytes() override { return totalBytesValue; }
  size_t usedBytes() override {
    if (usedBytesOverride != (size_t)-1) return usedBytesOverride;
    size_t total = 0;
    for (auto& kv : files_) total += kv.second.size();
    return total;
  }

  // ---- test introspection ----
  bool fileExists(const char* path) const { return files_.count(path) > 0; }
  size_t fileSize(const char* path) const {
    auto it = files_.find(path);
    return it == files_.end() ? 0 : it->second.size();
  }

private:
  std::map<std::string, std::vector<uint8_t>> files_;
};
