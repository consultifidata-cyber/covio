# 03 — Flash Fault-Injection Test Results (RISK-04)

## Honest status: WRITTEN, STATICALLY REVIEWED, NOT LOCALLY EXECUTED

`test/native_cpp/test_queue_fault_injection.cpp` contains **14 tests**
against the real `EventQueue` class (see doc 02 for why this is the real
class, not a reimplementation). **This session could not compile or run
them** — no host C++ toolchain exists on this machine (see doc 02's final
section). They have been:
- Written against the actual `queue.h`/`storage_backend.h` interfaces.
- Manually traced line-by-line against `EventQueue::append()`'s real control
  flow to confirm the expected outcomes are consistent with the code as
  written (e.g., tracing exactly which branch `backend_->appendOpenSize()`
  returning `-1` reaches, confirming `recordWriteFailure_(QFAIL_SEGMENT_OPEN)`
  is called before the early `return`).
- Fixed for two real, caught-on-review compile-correctness issues before
  being considered final: (1) `parseSegmentId_()` used `String::indexOf()`/
  `length()`/`operator[]()`, which the minimal native-test `String` shim
  deliberately does not implement — moved inside the same `#ifndef
  NATIVE_TEST` guard as its only caller, `cleanupOrphanSegments_()`; (2) a
  missing `<cstdlib>` include for `std::getenv()` in the Arduino shim.
- Wired into a new CI job (`.github/workflows/ci.yml`,
  `firmware-native-fault-injection`) that WILL compile and run them for
  real, on `ubuntu-latest` (g++ preinstalled), the next time this branch's
  CI executes.

**Do not treat "14 tests, well-designed" as "14 tests, passing."** They are
not yet proven to pass. This is stated plainly rather than glossed over.

## What the 14 tests cover, against the mandate's fault list

| Mandate fault # | Covered? | Test name |
|---|---|---|
| 1. No-space error | Modeled as append-write failure | `test_write_open_failure_rejected_checkpoint_not_advanced` |
| 2. Segment-open failure | Yes | `test_segment_open_failure_records_failure_does_not_advance_checkpoint` |
| 3. Zero-byte write | Yes (forceShortWriteLen=0 is a special case of short write) | covered by short-write tests |
| 4. Short write | Yes | `test_short_write_rejected_checkpoint_not_advanced` |
| 5. Flush failure | **Modeled, not distinct** — this interface bundles write+flush+close into one `appendWrite()` call (see doc 02); a flush failure is indistinguishable at this abstraction level from a short/failed write. Documented simplification, not silently merged without comment. | — |
| 6. Metadata write failure | Yes (checkpoint = `IQueueOffsetCheckpoint::setQueueOffset`, proven never called on any failure path via `tot.setCount` assertions) | all failure tests assert `tot.setCount == 0` |
| 7/8. Failure-state slot A/B corruption | Yes | `test_valid_failure_slot_survives_corruption_of_alternate_slot`, `test_both_failure_slots_corrupted_falls_back_to_fresh_state_no_crash` |
| 9. Power interruption between failure-state write stages | **NOT COVERED** — the fake backend's `writeWhole()` is atomic (all-or-nothing); modeling a torn WRITE of the failure-state record itself (as opposed to a torn READ, which corruption-testing already covers) would need the fake to support partial-write injection on `writeWhole` specifically, not implemented this pass |
| 10. Append succeeds but checkpoint fails | **NOT APPLICABLE to this codebase's actual control flow** — `setQueueOffset()` (the "checkpoint") is an in-memory + persisted call on `Totalizer`/`IQueueOffsetCheckpoint`, called only AFTER a fully successful write, and cannot itself "fail" in a way `EventQueue` observes (it has no return value) — traced by code reading, not a gap in the harness |
| 11. Append fails before checkpoint | Yes — this is the invariant EVERY failure test proves (`tot.setCount == 0`) |
| 12/13. Ack cursor/segment-deletion failure | **NOT COVERED** — `ackThrough()`/segment deletion is explicitly out of scope (doc 02) |
| 14-17. Capacity 80/90/95/100% | Yes | `test_capacity_alarm_thresholds_exact_boundaries` (exact boundary values 79.9/80/89.9/90/94.9/95/99.9/100), `test_capacity_percent_used_reflects_real_backend_usage` |
| 18. Reboot while storage remains full | **NOT DIRECTLY COVERED** — reboot-persistence IS covered (test 8 below) but not combined with an at-capacity backend specifically; the two properties (reboot survival, capacity reporting) are proven independently, not in combination |
| 19. Recovery after space becomes available | Partially — `test_failure_counter_monotonic_across_multiple_failures`'s final assertion (a successful append after 3 failures) proves the device isn't permanently locked out, but doesn't specifically model "backend was full, now has space" |
| 20. Repeated failures across multiple reboots | Partially — single-reboot survival is covered (test 8); multi-reboot accumulation is not separately tested |

## Required invariants — traced against the actual test assertions

- "A measurement is never reported as durably captured unless queue append
  succeeds": every failure test asserts `tot.setCount == 0` (the checkpoint,
  which is what makes a row "durably captured" from the rest of the
  system's perspective, per `covio_firmware.ino`'s own append-then-
  checkpoint ordering) never advances on a failure path.
- "No existing unacknowledged row is overwritten": `test_existing_confirmed_records_preserved_across_a_later_failure`.
- "Every failed append increments a durable monotonic failure counter":
  `test_failure_counter_monotonic_across_multiple_failures`.
- "First-failure timestamp stable, last-failure timestamp updates":
  `test_first_failure_timestamp_stable_last_updates`.
- "A valid failure-state slot survives corruption of the alternate slot":
  `test_valid_failure_slot_survives_corruption_of_alternate_slot`.
- "A storage alarm remains visible after reboot": `test_failed_write_state_survives_reboot`
  proves the COUNTER survives; the diagnostics.h alarm JSON itself
  (Arduino-coupled) is not separately host-tested, traced by code reading
  only (it reads directly from `q.hasFailedWrite()`, which the reboot test
  does prove survives).
- "No silent-return path remains": every one of `append()`'s four failure
  branches now calls `recordWriteFailure_()` before returning — confirmed
  by direct code reading of the current `queue.h`, not by test coverage
  alone.

## RISK-04 final status given all of the above

**NOT marked CLOSED.** Per the mandate's own rule ("RISK-04 may be marked
CLOSED only if... [the real logic is] exercised by the harness... [and]
tests prove the listed invariants" — implicitly requiring the tests to
have actually RUN), and given this session could not execute them, the
honest status is:

**CODE-CLOSED / HARNESS WRITTEN, EXECUTION PENDING (CI)** — a new, more
specific status than phase 1's "CODE-CLOSED / UNIT-TEST GAP," reflecting
real progress (a harness exists, exercises the real code, is wired into
CI) without overclaiming a result this session did not actually observe.
