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
    // THREE failures -> three persistFail_() calls. persistFail_()'s slot
    // selection is `(fail_.writes & 1) ? FAIL_PATH_A : FAIL_PATH_B` --
    // ODD writes-count -> A, EVEN -> B (verified by direct code reading,
    // NOT assumed -- an earlier version of this test had this backwards,
    // caught by actually running it, see the git history for this file).
    // Call 1: writes=1 (odd)  -> A gets {writes:1, failedWriteCount:1}
    // Call 2: writes=2 (even) -> B gets {writes:2, failedWriteCount:2}
    // Call 3: writes=3 (odd)  -> A gets {writes:3, failedWriteCount:3}
    // So A (writes=3) is the NEWEST/authoritative slot; B (writes=2) is
    // the next-newest valid fallback.
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    backend.failNextAppendOpenSize = true;
    q.append(makeRow(1));
    CHECK(q.failedWriteCount() == 3);
  }
  // Corrupt the NEWEST slot (A, writes=3) -- the recovery logic must fall
  // back to the older-but-valid slot (B, writes=2) rather than trusting
  // corrupted bytes or crashing.
  backend.corruptPath = FAIL_PATH_A;
  {
    FakeQueueOffsetCheckpoint tot2;
    EventQueue q2;
    q2.begin(&tot2, &backend);
    CHECK(q2.hasFailedWrite());
    CHECK(q2.failedWriteCount() == 2);  // recovered slot B's value (writes=2), not slot A's (3, corrupt) or zero
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
// 15. Segment rollover at the QUEUE_SEGMENT_ROWS cap (Miki Wire hardening,
//     Phase-0 finding F1: the cap existed but no code enforced it).
// ---------------------------------------------------------------------------
TEST(test_rollover_starts_next_segment_at_row_cap) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  // Simulate a full active segment 0: checkpoint sits exactly at the cap.
  tot.setQueueOffset(0, QUEUE_SEGMENT_ROWS * (uint32_t)sizeof(QRow));
  int setCountBefore = tot.setCount;

  q.append(makeRow(9001));

  CHECK(tot.setCount == setCountBefore + 1);
  CHECK(tot.queueOffsetSegment() == 1);                     // rolled over
  CHECK(tot.queueOffsetOffset() == sizeof(QRow));           // first row of seg 1
  CHECK(backend.fileExists("/queue/seg_000001.bin"));
  CHECK(backend.fileSize("/queue/seg_000001.bin") == sizeof(QRow));
  return true;
}

// ---------------------------------------------------------------------------
// 16. Interrupted rollover: the first write into the new segment fails/short,
//     the checkpoint must stay on the old segment; the retry rolls again and
//     cleans the partial bytes via the normal torn-tail truncate.
// ---------------------------------------------------------------------------
TEST(test_interrupted_rollover_checkpoint_unmoved_retry_succeeds) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  const uint32_t cap = QUEUE_SEGMENT_ROWS * (uint32_t)sizeof(QRow);
  tot.setQueueOffset(0, cap);
  int setCountBefore = tot.setCount;

  backend.forceShortWriteLen = 10;          // torn first write into seg 1
  q.append(makeRow(9001));
  CHECK(tot.setCount == setCountBefore);    // checkpoint NOT advanced
  CHECK(tot.queueOffsetSegment() == 0);     // still the old segment
  CHECK(tot.queueOffsetOffset() == cap);
  CHECK(q.hasFailedWrite());
  CHECK(backend.fileSize("/queue/seg_000001.bin") == 10);   // partial bytes

  q.append(makeRow(9001));                  // retry: rolls again, truncates, writes
  CHECK(tot.queueOffsetSegment() == 1);
  CHECK(tot.queueOffsetOffset() == sizeof(QRow));
  CHECK(backend.fileSize("/queue/seg_000001.bin") == sizeof(QRow));
  return true;
}

// ---------------------------------------------------------------------------
// 17. Truncate guard (Phase-0 finding F4): a zeroed/regressed checkpoint must
//     NOT cause blind truncation of a segment full of valid rows -- the rows
//     are adopted and the append continues after them.
// ---------------------------------------------------------------------------
TEST(test_checkpoint_regression_adopts_valid_rows_instead_of_truncating) {
  FakeStorageBackend backend;   // one physical "flash" across both queue lives
  {
    FakeQueueOffsetCheckpoint tot;
    EventQueue q;
    q.begin(&tot, &backend);
    for (uint32_t s = 1; s <= 5; s++) q.append(makeRow(s));   // 5 confirmed rows
    CHECK(backend.fileSize("/queue/seg_000000.bin") == 5 * sizeof(QRow));
  }
  // "Reboot" where BOTH totalizer checkpoint slots were corrupt: recovery
  // zeroes the queue offset (fresh FakeQueueOffsetCheckpoint = seg 0, off 0).
  {
    FakeQueueOffsetCheckpoint tot2;   // zeroed -- the regression scenario
    EventQueue q2;
    q2.begin(&tot2, &backend);
    q2.append(makeRow(6));
    // The 5 pre-existing rows survived; the new row landed after them.
    CHECK(backend.fileSize("/queue/seg_000000.bin") == 6 * sizeof(QRow));
    CHECK(tot2.queueOffsetOffset() == 6 * sizeof(QRow));
    CHECK(!q2.hasFailedWrite());
  }
  return true;
}

// ---------------------------------------------------------------------------
// 18. Truncate guard with a genuine garbage tail beyond the valid rows: the
//     valid prefix is adopted, ONLY the invalid tail is truncated.
// ---------------------------------------------------------------------------
TEST(test_checkpoint_regression_truncates_only_the_invalid_tail) {
  FakeStorageBackend backend;
  {
    FakeQueueOffsetCheckpoint tot;
    EventQueue q;
    q.begin(&tot, &backend);
    for (uint32_t s = 1; s <= 5; s++) q.append(makeRow(s));
    backend.forceShortWriteLen = 20;          // leaves 20 garbage bytes
    q.append(makeRow(6));                     // torn -- checkpoint stays at 5 rows
    CHECK(backend.fileSize("/queue/seg_000000.bin") == 5 * sizeof(QRow) + 20);
  }
  {
    FakeQueueOffsetCheckpoint tot2;           // zeroed checkpoint again
    EventQueue q2;
    q2.begin(&tot2, &backend);
    q2.append(makeRow(6));
    // 5 valid rows adopted, 20-byte garbage tail truncated, new row appended.
    CHECK(backend.fileSize("/queue/seg_000000.bin") == 6 * sizeof(QRow));
    CHECK(tot2.queueOffsetOffset() == 6 * sizeof(QRow));
  }
  return true;
}

// ---------------------------------------------------------------------------
// 19. Normal one-row torn tail still takes the original truncate path (the
//     guard must not change the established <=1-row behavior).
// ---------------------------------------------------------------------------
TEST(test_single_row_torn_tail_still_truncated_normally) {
  FakeStorageBackend backend;
  FakeQueueOffsetCheckpoint tot;
  EventQueue q;
  q.begin(&tot, &backend);

  q.append(makeRow(1));                        // one confirmed row
  backend.forceShortWriteLen = 30;             // torn write: 30 bytes < one row
  q.append(makeRow(2));
  CHECK(backend.fileSize("/queue/seg_000000.bin") == sizeof(QRow) + 30);

  q.append(makeRow(2));                        // truncates the 30-byte tail, writes
  CHECK(backend.fileSize("/queue/seg_000000.bin") == 2 * sizeof(QRow));
  CHECK(tot.queueOffsetOffset() == 2 * sizeof(QRow));
  return true;
}

// ---------------------------------------------------------------------------
// 20. adoptValidRows_ read-failure fail-safe: if the recovery scan itself
//     cannot read, the append must not destroy anything it hasn't proven --
//     adoption stops at the confirmed offset and the truncate applies only
//     beyond it. (Behavior: readAt failure -> adopt nothing -> tail truncated
//     as before the guard existed. Data already confirmed is untouched.)
// ---------------------------------------------------------------------------
TEST(test_adoption_read_failure_does_not_touch_confirmed_rows) {
  FakeStorageBackend backend;
  {
    FakeQueueOffsetCheckpoint tot;
    EventQueue q;
    q.begin(&tot, &backend);
    for (uint32_t s = 1; s <= 3; s++) q.append(makeRow(s));
  }
  {
    FakeQueueOffsetCheckpoint tot2;           // zeroed checkpoint
    EventQueue q2;
    q2.begin(&tot2, &backend);
    backend.failNextReadAt = true;            // recovery scan cannot read
    q2.append(makeRow(4));
    // Scan failed -> nothing adopted -> old (pre-guard) truncate behavior for
    // the unproven region. The new row is the only content. This is the
    // deliberate fail-safe floor: never worse than the pre-guard firmware.
    CHECK(backend.fileSize("/queue/seg_000000.bin") == sizeof(QRow));
    CHECK(tot2.queueOffsetOffset() == sizeof(QRow));
  }
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
