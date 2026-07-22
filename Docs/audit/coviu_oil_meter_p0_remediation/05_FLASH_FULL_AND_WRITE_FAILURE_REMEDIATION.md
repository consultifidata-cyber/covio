# 05 — RISK-04 Remediation: Silent Measurement Loss at Flash-Full/Write Failure

## Authoritative capacity calculation

Previous reports cited "~3.4MB" as a **code-comment estimate**. This session
located and read the actual partition table file this build uses
(`platformio.ini`'s `board_build.partitions=default_16MB.csv`, found inside
the installed PlatformIO `espressif32` toolchain at
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/default_16MB.csv`):

```
spiffs,   data, spiffs,  0xc90000,0x360000,
```

`0x360000` = **3,538,944 bytes exactly** (3.375 MiB) — not an approximation.

- `QRow` size: 36 bytes (`sizeof`, verified against the packed struct).
- Raw theoretical ceiling: 3,538,944 / 36 = **98,304 rows**, if the partition
  held nothing but queue rows and LittleFS had zero overhead. It does not:
  the partition also holds `ackA.bin`/`ackB.bin` (28 bytes each),
  `failA.bin`/`failB.bin` (28 bytes each, new in this fix), and LittleFS's
  own per-file/per-block metadata.
- **Real usable capacity is now reported at runtime**, not estimated:
  `EventQueue::capacityPercentUsed()` (new in this fix) calls
  `LittleFS.usedBytes()`/`LittleFS.totalBytes()` directly — this reflects
  actual on-flash occupancy regardless of any overhead assumption, and is
  exposed via `/api/v1/status`'s new `capacity_pct_used` field.
- Buffering duration at 1 sample/sec: `QUEUE_HIGHWATER` (20,000 rows, a soft
  advisory threshold, unchanged) ≈ **5.5 hours** to the first WARNING-tier
  signal; the physical ceiling (~98,304 rows before LittleFS/file overhead)
  ≈ **27.3 hours** to true exhaustion, though the exact number depends on
  LittleFS's real overhead for this file layout (an on-device measurement
  this session could not perform without touching hardware — flagged
  explicitly as an estimate, not a measured figure).
- At 1 sample/10 sec (a plausible "expected real plant frequency" for a
  slower-changing flow signal): the same ~98,304-row ceiling stretches to
  roughly **11.4 days**.

**This is the authoritative, partition-table-derived figure this mandate
required in place of a comment-based assumption.** It was not independently
re-measured against actual on-flash byte usage on the physical device
(that would require touching COM6, outside this session's authority) — the
`capacityPercentUsed()` mechanism now exists specifically so that
measurement can be taken safely, remotely, the next time the device is
queried, without needing a repeat static estimate.

## Behavior at 80/90/95/100% capacity (now implemented, was previously absent)

`diagnostics.h::computeHealth_()` now checks `capacityPercentUsed()`
(highest threshold first, so exactly one is ever reported):

| Threshold | New alarm type | Severity |
|---|---|---|
| ≥100% | `STORAGE_FULL` | CRITICAL |
| ≥95% | `STORAGE_CRITICAL` | CRITICAL |
| ≥90% | `STORAGE_HIGH` | WARNING |
| ≥80% | `STORAGE_WARNING` | WARNING |

These are **in addition to**, not instead of, the pre-existing
`QUEUE_HIGH`/`QUEUE_CRITICAL` backlog-count alarms — the two signals are
correlated but not identical (disk usage also includes checkpoint/ack/
failure-state files and any not-yet-cleaned orphan segment), so both remain
useful defense in depth.

## Write-failure behavior (the core RISK-04 fix)

`queue.h::append()`'s four failure branches (segment-open failure,
truncate-before-append failure, segment-append-open failure, short/failed
write) now each call `recordWriteFailure_(code)`, which:
1. Updates a durable `FailureState` struct: `failed_write_count` (monotonic,
   never decremented), `first_failure_uptime_s`, `last_failure_uptime_s`,
   `last_error_code`.
2. Persists it via the **exact same dual-slot ping-pong + CRC32 pattern**
   already proven correct elsewhere in this codebase (`AckRec` in this same
   file, `Checkpoint` in `totalizer.h`) — reused deliberately, not
   reinvented.
3. Is written **only on an actual failure**, never on the hot per-second
   append path — adds zero new wear-leveling burden (see
   `06_FLASH_LIFETIME_ANALYSIS.md`'s equivalent section in the prior audit;
   unaffected by this addition).

`diagnostics.h` now exposes a `QUEUE_WRITE_FAILURE` CRITICAL alarm whenever
`hasFailedWrite()` is true, plus `failed_write_count`/`last_write_failure`
detail in `/api/v1/status`. **This alarm never auto-clears** — a documented
design decision (see `queue.h`'s `FAIL_MAGIC` header comment): a device that
once lost data and later recovered must still show that history, matching
the mandate's own "must never silently continue as if data were safe."

## Chosen policy: Option A (fail loud), explicitly not Option B

No RAM-only or alternate-partition fallback buffer was introduced. The
existing architecture's separation — hardware PCNT counter (independent,
keeps counting) vs. durable queue (the thing that can fail) — is preserved
exactly as-is; a queue write failure is now **loud** (a persisted counter +
a CRITICAL remote alarm) rather than silently absorbed into a fallback
store that would only relocate the same power-loss risk RISK-04 exists to
address, per the mandate's own explicit warning against that shortcut.

## Queue-full controls verified unchanged

- Never overwrites oldest unsynced records: confirmed — `append()` has no
  eviction path at all; a failure returns without writing, it never
  overwrites existing data.
- Never silently drops the newest record without a trace: fixed by this
  change (previously true; no longer true after this fix).
- Never advances the local ack cursor without server proof: unchanged,
  `ackThrough()` logic untouched.
- Reserved metadata space for the fault record: `FailureState` is 28 bytes,
  written to two small dedicated files, entirely independent of the
  segment/row storage budget.
- Durable state survives reboot to explain the failure: confirmed by design
  (dual-slot CRC, loaded in `begin()`) — not unit-tested (see below), but
  the same mechanism that already IS tested for `AckRec`/`Checkpoint`
  elsewhere in this codebase.

## Test evidence — and an honest gap

**No new C++ host-compilable unit tests were added for this fix.** This
repository has no host-compilable test harness for firmware logic at all —
`test/native/` has only ever tested `server/server.py` (confirmed by
reading every file in that directory: all four pre-existing test files, and
the two files this session added for P0-1/P0-3, exercise Flask routes via
`server.app.test_client()`; none touch `queue.h`/`totalizer.h`/`diagnostics.h`
directly). Building a LittleFS/Arduino host-mock harness capable of
fault-injecting `LittleFS.open()`/`f.write()` failures is a real, valuable,
but substantial separate undertaking — not attempted in this pass.

What WAS verified, honestly stated as its own category:
- **Real compilation** (`pio run`, build-only, no upload) succeeded for
  `env:esp32dev` and `env:factory`, and `env:release` still fails exactly as
  it did before this change (the placeholder-CA-cert guard, unrelated to
  this fix — confirms no regression). This proves the code is syntactically
  and type-correct and links successfully; it does NOT prove the runtime
  behavior of the four failure branches under an actual filesystem fault.
- **Code review** confirms each failure branch's control flow reaches
  `recordWriteFailure_()` before returning, and that the dual-slot
  persistence follows the identical structure already proven correct for
  `AckRec`.
- **NOT PROVEN this session:** actual behavior when LittleFS genuinely
  returns no-space, a real short write occurs, or the filesystem mount
  fails on the physical device — items 1-6 and 9-13 of the mandate's
  17-item P0-4 test list remain unexecuted. This is marked as an open gap
  in the risk register and remediation plan, not silently claimed as
  covered.
