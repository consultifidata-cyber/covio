# 02 — Flash Fault-Injection Harness Design (RISK-04)

## Why the prior phase's fix wasn't enough

Phase 1 implemented the real `FailureState` durable-counter fix in `queue.h`
and verified it by (a) code review and (b) real `pio run` compiles. The
mandate correctly identified that this is NOT the same as proving the
failure branches behave as coded — a compile only proves the code is
syntactically valid, not that `recordWriteFailure_()` is actually reached
and behaves correctly when `LittleFS.open()` genuinely fails.

## Design: StorageBackend interface (exactly as the mandate suggested)

`storage_backend.h` defines an abstract `StorageBackend` with the operations
`EventQueue::append()` and the `FailureState`/`AckRec` dual-slot persist/load
paths actually use:
`appendOpenSize`, `truncateTo`, `appendWrite`, `readWhole`, `writeWhole`,
`totalBytes`, `usedBytes`.

- **Production** (`NATIVE_TEST` undefined): `LittleFsBackend`, a thin
  pass-through wrapper. Verified byte-for-byte behaviorally identical to
  the pre-refactor direct calls (same `FILE_APPEND`/`FILE_READ`/
  `FILE_WRITE` modes, same "open, check size, close" pattern, same
  `"/littlefs"`-prefixed POSIX `truncate()`).
- **Native tests** (`NATIVE_TEST` defined): `FakeStorageBackend`
  (`test/native_cpp/fake_storage_backend.h`), an in-memory map of
  path→bytes with per-call fault-injection flags (`failNextAppendOpenSize`,
  `failNextTruncate`, `failNextAppendWrite`, `forceShortWriteLen`,
  `failNextWriteWhole`, `corruptPath`) and a controllable simulated
  total/used byte count.

`EventQueue::begin()` gained a second overload taking an explicit
`(IQueueOffsetCheckpoint*, StorageBackend*)` pair; the original
`begin(IQueueOffsetCheckpoint* tot = nullptr)` overload (guarded to
production builds only) supplies a static `LittleFsBackend` automatically,
so **every existing production call site
(`eventQueue.begin(&totalizer)` in `covio_firmware.ino`) is completely
unmodified** — it still compiles and behaves identically.

`Totalizer*` was narrowed to a new, dependency-free `IQueueOffsetCheckpoint`
interface (`queue_offset_checkpoint.h`) exposing the exact three methods
`EventQueue` ever calls on it. `Totalizer` now implements this interface
(pure addition, zero behavior change); native tests supply
`FakeQueueOffsetCheckpoint` (`test/native_cpp/fake_totalizer.h`) instead of
a real `Totalizer`, which cannot be constructed off-target (PCNT hardware
dependency).

## Deliberately bounded scope

**In scope** (routed through `StorageBackend`, genuinely fault-injectable):
`EventQueue::append()`'s four failure branches, `FailureState` load/persist,
`AckRec` load/persist (needed because `begin()` loads both in the same call
as `FailureState`), and `capacityPercentUsed()`.

**Out of scope** (unchanged, direct-LittleFS, compile-verified only,
excluded from `NATIVE_TEST` compilation via `#ifndef` guards so the header
does not require LittleFS to exist on host for logic this pass does not
touch): `pending()`/`pendingImpl_()`/`pendingLegacy_()` (segment-walk read),
`ackThrough()`/`ackThroughLegacy_()` (segment-walk + cursor advance),
`cleanupOrphanSegments_()`, `maybeCompact_()`, `computeUnackedCount_()`,
`appendLegacy_()` (the pre-Phase-2 single-file fallback, already dead code
in production). This was a deliberate choice, not an oversight: rewriting
these onto the same abstraction would require a partial-read/seek-based
interface extension and touches code with its own already-reviewed
correctness properties from the original audit — a larger, riskier change
than RISK-04 (specifically about *write failures*, not read-path
correctness) requires. The mandate's own instruction ("do not rewrite the
entire queue subsystem merely to make it testable") supports this
boundary.

## The capacity-alarm threshold logic is ALSO now a pure function

`capacityAlarmLevel(float pctUsed)` (queue.h) replaces the inline
if/else-chain that used to live directly in `diagnostics.h::computeHealth_()`
— the exact same threshold-selection decision `diagnostics.h` makes is now
a standalone, zero-dependency function, directly unit-tested (not just the
underlying percentage arithmetic).

## Host toolchain gap (read before trusting "tests pass")

**No host C++ compiler (g++/clang/MSVC) exists on the machine this harness
was authored on** — confirmed by checking PATH, PlatformIO's own package
directory (only cross-compilers for xtensa/riscv32 ESP targets), and any
bundled MinGW (Git for Windows ships `mingw64/share/licenses/gcc-libs` but
no actual compiler binary). This means **the harness described above was
written and carefully self-reviewed, but has not been locally compiled or
executed in this session.**

To get REAL execution evidence rather than none, a new CI job
(`firmware-native-fault-injection` in `.github/workflows/ci.yml`) was added
that compiles and runs it on `ubuntu-latest` (which has `g++` preinstalled
by default). See `03_FLASH_FAULT_TEST_RESULTS.md` for the exact, honest
status of that evidence.
