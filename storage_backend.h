// ============================================================================
// storage_backend.h — narrow filesystem interface for host fault-injection
// ----------------------------------------------------------------------------
// RISK-04 remediation, phase 2: the mandate requires that the REAL production
// queue-append and failure-state logic be exercised by an automated fault-
// injection harness, not a parallel reimplementation. This header carves out
// exactly the filesystem operations queue.h's append() and FailureState
// persist/load paths use, behind an abstract interface, so:
//   - production firmware (NATIVE_TEST undefined) uses LittleFsBackend, a
//     thin pass-through wrapper with IDENTICAL behavior to the pre-refactor
//     direct LittleFS/File calls (same open modes, same truncate-before-
//     append semantics, same short-write detection);
//   - native host tests (NATIVE_TEST defined) use FakeStorageBackend
//     (test/native_cpp/fake_storage_backend.h), which deterministically
//     injects every fault the mandate's 20-item list requires.
//
// SCOPE, deliberately bounded (see 02_FLASH_FAULT_INJECTION_DESIGN.md for the
// full rationale): only EventQueue::append()'s filesystem operations and the
// FailureState dual-slot persist/load are routed through this interface.
// pending()/ackThrough()/cleanupOrphanSegments_() are UNCHANGED, still call
// LittleFS/File directly, and remain compile-verified-only, exactly as
// before this phase. Rewriting the entire EventQueue class onto this
// abstraction was rejected as out of proportion to what RISK-04 actually
// requires (the append-failure and failure-state paths ARE the risk), per
// the mandate's own "do not rewrite the entire queue subsystem merely to
// make it testable" instruction.
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

class StorageBackend {
public:
  virtual ~StorageBackend() {}

  // ---- segment-append path (EventQueue::append()) --------------------------
  // Opens `path` in append mode, returns its current size in bytes, closes it.
  // Returns -1 if the open itself failed (QFAIL_SEGMENT_OPEN's trigger).
  virtual long appendOpenSize(const char* path) = 0;

  // POSIX-style truncate of `path` to exactly `toSize` bytes. Returns true on
  // success. Returns false on failure (QFAIL_TRUNCATE's trigger) -- mirrors
  // the pre-refactor direct `truncate(vfsPath.c_str(), goodOffset) != 0` check.
  virtual bool truncateTo(const char* path, size_t toSize) = 0;

  // Opens `path` in append mode, writes exactly `len` bytes, flushes, closes.
  // Returns the number of bytes actually written (may be < len, the
  // QFAIL_WRITE_SHORT case), or -1 if the open itself failed.
  virtual long appendWrite(const char* path, const uint8_t* data, size_t len) = 0;

  // ---- whole-file checkpoint path (FailureState, AckRec, Checkpoint) -------
  // Reads the ENTIRE contents of `path` into `out` (caller-sized buffer of
  // `outCap` bytes). Returns the number of bytes read, or -1 if the file does
  // not exist or could not be opened. Used for the fixed-size dual-slot
  // checkpoint records, which are always read whole, never partially.
  virtual long readWhole(const char* path, uint8_t* out, size_t outCap) = 0;

  // Overwrites `path` with exactly `data`/`len` (FILE_WRITE semantics:
  // truncate-then-write, matching queue.h's existing persistFail_()/
  // persistAck_() pattern). Returns true on success.
  virtual bool writeWhole(const char* path, const uint8_t* data, size_t len) = 0;

  // ---- capacity (EventQueue::capacityPercentUsed()) ------------------------
  virtual size_t totalBytes() = 0;
  virtual size_t usedBytes() = 0;
};

// ---------------------------------------------------------------------------
// Production implementation. Compiled only for the real ESP32 build
// (NATIVE_TEST undefined) -- native host tests never link this class, since
// it depends on LittleFS/File, which do not exist off-target. This is a
// byte-for-byte behavioral match to what queue.h did directly before this
// refactor: same FILE_APPEND/FILE_READ/FILE_WRITE modes, same "open, check
// size, close" pattern for the pre-append size check, same
// "/littlefs"-prefixed POSIX truncate() call.
// ---------------------------------------------------------------------------
#ifndef NATIVE_TEST
#include <Arduino.h>
#include <LittleFS.h>
#include <unistd.h>
#include <errno.h>

class LittleFsBackend : public StorageBackend {
public:
  long appendOpenSize(const char* path) override {
    File f = LittleFS.open(path, FILE_APPEND);
    if (!f) return -1;
    long sz = (long)f.size();
    f.close();
    return sz;
  }

  bool truncateTo(const char* path, size_t toSize) override {
    String vfsPath = String("/littlefs") + String(path);
    return truncate(vfsPath.c_str(), toSize) == 0;
  }

  long appendWrite(const char* path, const uint8_t* data, size_t len) override {
    File f = LittleFS.open(path, FILE_APPEND);
    if (!f) return -1;
    size_t written = f.write(data, len);
    f.flush();
    f.close();
    return (long)written;
  }

  long readWhole(const char* path, uint8_t* out, size_t outCap) override {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return -1;
    size_t sz = f.size();
    if (sz > outCap) sz = outCap;
    size_t got = f.read(out, sz);
    f.close();
    return (long)got;
  }

  bool writeWhole(const char* path, const uint8_t* data, size_t len) override {
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(data, len);
    f.flush();
    f.close();
    return written == len;
  }

  size_t totalBytes() override { return LittleFS.totalBytes(); }
  size_t usedBytes()  override { return LittleFS.usedBytes(); }
};
#endif  // NATIVE_TEST
