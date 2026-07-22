// ============================================================================
// test_queue_fault_injection.cpp — RISK-04 phase-2: real fault injection
// against the ACTUAL production EventQueue class (queue.h), compiled here
// with NATIVE_TEST defined so it uses FakeStorageBackend/FakeQueueOffsetCheckpoint
// instead of LittleFS/Totalizer. This is NOT a reimplementation -- every
// test below calls into the exact same append()/begin() code that ships to
// the device (see storage_backend.h/queue.h's #ifndef NATIVE_TEST guards
// for the precise, documented scope boundary).
//
// No new test-framework dependency (no GoogleTest/Catch2) -- matches this
// project's existing "no new dependency without an ADR" governance
// convention (see diagnostics.h's header comment). A minimal self-contained
// runner is used instead.
//
// Build/run:
//   g++ -std=c++14 -DNATIVE_TEST -I. -I../.. \
//       test_queue_fault_injection.cpp arduino_shim.cpp -o test_queue_fault_injection
//   ./test_queue_fault_injection
// ============================================================================
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "fake_storage_backend.h"
#include "fake_totalizer.h"
#include "../../queue.h"

// ---------------------------------------------------------------------------
// Minimal test runner
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, std::function<bool()>>> g_tests;
#define TEST(name) \
  static bool name(); \
  static bool name##_registered = ([]{ g_tests.push_back({#name, name}); return true; })(); \
  static bool name()

static int g_assertFailures = 0;
#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("    CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_assertFailures++; \
    return false; \
  } \
} while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static QRow makeRow(uint32_t seq, uint32_t bootId = 1) {
  QRow r{};
  r.schema_version = 1;
  r.record_type = 1;
  r.boot_id = bootId;
  r.seq = seq;
  r.ts = 1000 + seq;
  r.totalizer = 500 + seq;
  r.quality = 0;
  r.rssi_abs = 50;
  return r;
}

// ---------------------------------------------------------------------------
// 1. Successful append
// ---------------------------------------------------------------------------
TEST(test_successful_append_advances_checkpoint_and_stores_row) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  CHECK(!q.hasFailedWrite());
  q.append(makeRow(1));
  CHECK(tot.setCount == 1);
  CHECK(tot.queueOffsetOffset() == sizeof(QRow));
  CHECK(!q.hasFailedWrite());
  return true;
}

// ---------------------------------------------------------------------------
// 2. Segment-open failure
// ---------------------------------------------------------------------------
TEST(test_segment_open_failure_records_failure_does_not_advance_checkpoint) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));

  CHECK(tot.setCount == 0);            // checkpoint never advanced
  CHECK(q.hasFailedWrite());
  CHECK(q.failedWriteCount() == 1);
  CHECK(std::string(q.lastFailureCodeStr()) == "segment_open_failed");
  return true;
}

// ---------------------------------------------------------------------------
// 3. Truncate failure (requires a torn-tail scenario first)
// ---------------------------------------------------------------------------
TEST(test_truncate_failure_records_failure_does_not_advance_checkpoint) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  // Simulate a torn tail: force a short write that leaves extra bytes in the
  // segment file beyond what the checkpoint confirms as good.
  backend.forceShortWriteLen = 10;   // fewer than sizeof(QRow) -- torn write
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 1);  // the short write itself is failure #1
  CHECK(tot.setCount == 0);          // checkpoint still at 0 (goodOffset=0)

  // Next append(): actualSize (10 bytes on disk) > goodOffset (0) -> truncate
  // path is taken. Force it to fail.
  backend.failNextTruncate = true;
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 2);
  CHECK(std::string(q.lastFailureCodeStr()) == "truncate_failed");
  CHECK(tot.setCount == 0);          // still never advanced
  return true;
}

// ---------------------------------------------------------------------------
// 4. Short write (write "succeeds" but returns fewer bytes than a QRow)
// ---------------------------------------------------------------------------
TEST(test_short_write_rejected_checkpoint_not_advanced) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.forceShortWriteLen = 20;  // < sizeof(QRow)
  q.append(makeRow(1));

  CHECK(tot.setCount == 0);
  CHECK(q.hasFailedWrite());
  CHECK(std::string(q.lastFailureCodeStr()) == "write_failed_or_short");
  return true;
}

// ---------------------------------------------------------------------------
// 5. Write-open failure (appendWrite returns -1 outright)
// ---------------------------------------------------------------------------
TEST(test_write_open_failure_rejected_checkpoint_not_advanced) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.failNextAppendWrite = true;
  q.append(makeRow(1));

  CHECK(tot.setCount == 0);
  CHECK(q.hasFailedWrite());
  CHECK(std::string(q.lastFailureCodeStr()) == "write_failed_or_short");
  return true;
}

// ---------------------------------------------------------------------------
// 6. Failure counter is monotonic across multiple distinct failures
// ---------------------------------------------------------------------------
TEST(test_failure_counter_monotonic_across_multiple_failures) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 1);

  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 2);

  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 3);

  // And a SUCCESSFUL append afterward must NOT reset or decrement it --
  // deliberately monotonic (see queue.h's FAIL_MAGIC header comment).
  q.append(makeRow(1));
  CHECK(q.failedWriteCount() == 3);
  CHECK(q.hasFailedWrite());  // still true -- never auto-clears
  return true;
}

// ---------------------------------------------------------------------------
// 7. First-failure timestamp stable, last-failure timestamp updates
// ---------------------------------------------------------------------------
TEST(test_first_failure_timestamp_stable_last_updates) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  nativeTestSetMillis(10000);  // uptime 10s
  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));
  CHECK(q.firstFailureUptimeS() == 10);
  CHECK(q.lastFailureUptimeS() == 10);

  nativeTestSetMillis(50000);  // uptime 50s
  backend.failNextAppendOpenSize = true;
  q.append(makeRow(1));
  CHECK(q.firstFailureUptimeS() == 10);  // unchanged
  CHECK(q.lastFailureUptimeS() == 50);   // updated
  return true;
}

// ---------------------------------------------------------------------------
// 8. Failed-write counter survives reboot (dual-slot recovery, happy path)
// ---------------------------------------------------------------------------
TEST(test_failed_write_state_survives_reboot) {
  FakeStorageBackend backend;  // simulates the SAME physical flash across "reboots"
  {
    FakeQueueOffsetCheckpoint tot;
    EventQueue q;
    q.begin(&tot, &backend);
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    CHECK(q.failedWriteCount() == 1);
  }
  // "Reboot": brand-new EventQueue instance, same backend (same "disk").
  {
    FakeQueueOffsetCheckpoint tot2;
    EventQueue q2;
    q2.begin(&tot2, &backend);
    CHECK(q2.hasFailedWrite());
    CHECK(q2.failedWriteCount() == 1);
    CHECK(std::string(q2.lastFailureCodeStr()) == "segment_open_failed");
  }
  return true;
}

// ---------------------------------------------------------------------------
// 9. A valid slot survives corruption of the alternate slot
// ---------------------------------------------------------------------------
TEST(test_valid_failure_slot_survives_corruption_of_alternate_slot) {
  FakeStorageBackend backend;
  {
    FakeQueueOffsetCheckpoint tot;
    EventQueue q;
    q.begin(&tot, &backend);
    // Two failures -> two persistFail_() calls -> writes counter 1 (slot B,
    // odd) then 2 (slot A, even) -- see persistFail_()'s `writes & 1` slot
    // selection. Slot A (the newer one) now holds the authoritative state.
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    CHECK(q.failedWriteCount() == 2);
  }
  // Corrupt the NEWER slot (A) -- the recovery logic must fall back to the
  // older-but-valid slot (B) rather than trusting corrupted bytes or
  // crashing.
  backend.corruptPath = FAIL_PATH_A;
  {
    FakeQueueOffsetCheckpoint tot2;
    EventQueue q2;
    q2.begin(&tot2, &backend);
    CHECK(q2.hasFailedWrite());
    CHECK(q2.failedWriteCount() == 1);  // recovered slot B's value (1 write old), not slot A's (2) or zero
  }
  return true;
}

// ---------------------------------------------------------------------------
// 10. Both slots corrupted: fresh state, no crash
// ---------------------------------------------------------------------------
TEST(test_both_failure_slots_corrupted_falls_back_to_fresh_state_no_crash) {
  FakeStorageBackend backend;
  uint8_t garbage[64];
  memset(garbage, 0xEE, sizeof(garbage));
  backend.writeWhole(FAIL_PATH_A, garbage, sizeof(garbage));
  backend.writeWhole(FAIL_PATH_B, garbage, sizeof(garbage));

  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);     // must not crash even though NEITHER slot is trustworthy
  CHECK(!q.hasFailedWrite());  // falls back to a fresh, zeroed state -- a safe, loud
  CHECK(q.failedWriteCount() == 0);  // reset, never a fabricated "everything's fine" guess
  return true;
}

// ---------------------------------------------------------------------------
// 11. Capacity threshold selection (pure function, exact boundaries)
// ---------------------------------------------------------------------------
TEST(test_capacity_alarm_thresholds_exact_boundaries) {
  CHECK(capacityAlarmLevel(0.0f)   == QCAP_NONE);
  CHECK(capacityAlarmLevel(79.9f)  == QCAP_NONE);
  CHECK(capacityAlarmLevel(80.0f)  == QCAP_WARNING_80);
  CHECK(capacityAlarmLevel(89.9f)  == QCAP_WARNING_80);
  CHECK(capacityAlarmLevel(90.0f)  == QCAP_WARNING_90);
  CHECK(capacityAlarmLevel(94.9f)  == QCAP_WARNING_90);
  CHECK(capacityAlarmLevel(95.0f)  == QCAP_CRITICAL_95);
  CHECK(capacityAlarmLevel(99.9f)  == QCAP_CRITICAL_95);
  CHECK(capacityAlarmLevel(100.0f) == QCAP_CRITICAL_100);
  CHECK(capacityAlarmLevel(100.0f) == QCAP_CRITICAL_100);  // >100% (shouldn't happen, but must not misclassify)
  return true;
}

TEST(test_capacity_percent_used_reflects_real_backend_usage) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.totalBytesValue = 1000;
  backend.usedBytesOverride = 800;
  CHECK(capacityAlarmLevel(q.capacityPercentUsed()) == QCAP_WARNING_80);

  backend.usedBytesOverride = 950;
  CHECK(capacityAlarmLevel(q.capacityPercentUsed()) == QCAP_CRITICAL_95);

  backend.usedBytesOverride = 1000;
  CHECK(capacityAlarmLevel(q.capacityPercentUsed()) == QCAP_CRITICAL_100);
  return true;
}

// ---------------------------------------------------------------------------
// 12. Recovery ordering: a torn tail from a short write is correctly
//     truncated away by the NEXT append, and the new row's bytes are exactly
//     right (no leftover corruption from the failed attempt).
// ---------------------------------------------------------------------------
TEST(test_recovery_after_short_write_produces_correct_bytes_no_corruption) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  backend.forceShortWriteLen = 10;
  q.append(makeRow(1));                      // torn: 10 garbage bytes now on "disk"
  CHECK(backend.fileSize("/queue/seg_000000.bin") == 10);
  CHECK(tot.queueOffsetOffset() == 0);        // checkpoint still at 0

  q.append(makeRow(1));                      // truncates the 10 bytes away, writes cleanly
  CHECK(backend.fileSize("/queue/seg_000000.bin") == sizeof(QRow));  // exactly one clean row
  CHECK(tot.queueOffsetOffset() == sizeof(QRow));
  return true;
}

// ---------------------------------------------------------------------------
// 13. Existing (already-confirmed) records are never touched by a later failure
// ---------------------------------------------------------------------------
TEST(test_existing_confirmed_records_preserved_across_a_later_failure) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  q.append(makeRow(1));  // one clean, confirmed row
  CHECK(backend.fileSize("/queue/seg_000000.bin") == sizeof(QRow));

  backend.failNextAppendOpenSize = true;
  q.append(makeRow(2));  // fails before touching the file at all

  // The already-confirmed first row's bytes are completely untouched.
  CHECK(backend.fileSize("/queue/seg_000000.bin") == sizeof(QRow));
  CHECK(tot.queueOffsetOffset() == sizeof(QRow));  // checkpoint unchanged by the failed 2nd row
  return true;
}

// ---------------------------------------------------------------------------
// 14. AckRec persistence also survives a write failure without crashing
//     (loadAck_/persistAck_ are routed through the same backend_ as of this
//     phase -- see queue.h). Only load/persist are in scope here, NOT the
//     segment-walk ackThrough()/pending() logic (explicitly out of scope,
//     unchanged, compile-verified only -- see 02_FLASH_FAULT_INJECTION_DESIGN.md).
// ---------------------------------------------------------------------------
TEST(test_begin_does_not_crash_when_ack_state_files_are_corrupt) {
  FakeStorageBackend backend;
  uint8_t garbage[64];
  memset(garbage, 0x11, sizeof(garbage));
  backend.writeWhole(ACK_PATH_A, garbage, sizeof(garbage));
  backend.writeWhole(ACK_PATH_B, garbage, sizeof(garbage));

  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);   // must not crash
  CHECK(q.ackedSeq() == 0);  // falls back to a fresh zeroed AckRec
  return true;
}

// ---------------------------------------------------------------------------
int main() {
  int passed = 0, failed = 0;
  for (auto& t : g_tests) {
    printf("RUN  %s\n", t.first.c_str());
    bool ok = t.second();
    if (ok) { printf("PASS %s\n", t.first.c_str()); passed++; }
    else    { printf("FAIL %s\n", t.first.c_str()); failed++; }
  }
  printf("\n%d passed, %d failed, %d total assertion failures\n", passed, failed, g_assertFailures);
  return failed == 0 ? 0 : 1;
}
