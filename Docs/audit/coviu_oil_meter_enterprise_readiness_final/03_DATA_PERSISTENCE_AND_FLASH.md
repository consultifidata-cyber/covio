# 03 — Data Persistence and Flash Reliability (Parts 1-2)

Evidence basis: direct read of `queue.h`, `totalizer.h`-referenced
checkpoint interface, `config.h`, the actual partition CSV
(`framework-arduinoespressif32/tools/partitions/default_16MB.csv`, read
directly this session), and live device `/api/v1/status`/`/metrics`
readings.

## Part 1 — Data Persistence

### 1. Where every reading is stored, stage by stage

```
Pulse edge -> PCNT hardware peripheral (counts in silicon, survives
              brief code stalls, does NOT survive power loss -- volatile)
           -> Totalizer (RAM accumulator + periodic NVS/flash checkpoint,
              totalizer.h -- not directly inspected this pass, referenced
              via its IQueueOffsetCheckpoint interface in queue.h)
           -> one QRow struct built once per TELEMETRY_PERIOD_MS (1000ms)
           -> EventQueue::append() -- CRC32'd, written to the ACTIVE
              segment file on LittleFS ("/queue/seg_NNNNNN.bin")
           -> durable at this point (survives reboot/power loss, subject
              to §9 below)
           -> Sync::pushOnce() reads via EventQueue::pending() (does NOT
              mutate anything), POSTs to the server
           -> server.py's push(): SQLite INSERT OR IGNORE, PRIMARY KEY
              (device_id, seq) -- durable server-side on commit
           -> server returns cumulative ack_seq
           -> EventQueue::ackThrough(): persists new cursor (AckRec,
              dual-slot CRC'd) FIRST, deletes now-fully-consumed segment
              files SECOND
```

### 2. Which stages exist only in volatile locations

- **PCNT peripheral register**: volatile, RAM/hardware-only, until the
  next `Totalizer` read absorbs it into its own counter.
- **The in-progress `QRow` being constructed this second**: RAM-only
  until `EventQueue::append()`'s `backend_->appendWrite()` call returns.
- Everything **after** a successful `append()` return is on flash
  (LittleFS), not RAM.

### 3. The precise durability event

A reading becomes durable at the exact moment `backend_->appendWrite()`
(`queue.h::append()`) returns `written == sizeof(QRow)` — i.e. the bytes
are confirmed written to the LittleFS segment file. Before that instant,
the sample exists only in RAM (the freshly-built `QRow`) or, before that,
only in the PCNT hardware register (already absorbed into `Totalizer`,
which — per this file's own read — checkpoints independently and more
frequently than every single sample; not independently re-verified this
pass, flagged as a residual gap since `totalizer.h` itself was not
re-read line-by-line this session).

### 4/5. Durability vs. sequence assignment / totalizer checkpointing

Sequence (`seq`) is assigned by `covio_firmware.ino`'s global monotonic
counter **before** the row is handed to `append()` — so durability
happens *after* sequence assignment (the row is already sequence-stamped
when it becomes durable). Totalizer checkpointing is a **separate,
independent** durability event (its own NVS/flash write, via the
`IQueueOffsetCheckpoint` interface) — the two are not the same
transaction. This means it is possible, in principle, for the totalizer's
own checkpoint to succeed/fail independently of a given queue row's own
durability — this specific interaction was not exercised by any test
this session (no test explicitly interleaved a totalizer-checkpoint
failure with a queue-append at the same instant) — **code-inspected
only**, not hardware- or fault-injection-proven this pass.

### 6. Can pulses exist only in a volatile register before capture?

Yes — between two `TELEMETRY_PERIOD_MS` ticks (up to ~1 second), pulses
counted by PCNT are represented only in the peripheral register + the
`Totalizer`'s own RAM state, not yet in any `QRow`. This is by design
(1Hz sampling), not a defect — the maximum pulse-loss window this implies
is bounded by the sampling interval (§7).

### 7. Maximum pulse-loss window

Bounded by `TELEMETRY_PERIOD_MS = 1000UL` — **up to ~1 second** of pulses
could be uncounted-into-a-row if power is lost between one sample and the
next, PROVIDED the `Totalizer`'s own checkpoint (independent of the
per-second `QRow`) has not separately captured that count more
frequently. Not independently timed/measured this session — a
code-derived bound, not a hardware-measured one.

### 8. Maximum reading-loss window

One full `QRow` (i.e. up to 1 second's sample) if power is lost strictly
between `append()`'s CRC-compute and its `appendWrite()` return — the
**torn-write detector** (CRC32 + truncate-before-append, §9) guarantees
this is the absolute ceiling; a torn write is detected and discarded on
the next boot's first `append()` call, never silently accepted as valid
data.

### 9. Power-fail behavior at each named point

| Point | What happens | Evidence |
|---|---|---|
| During pulse counting | PCNT register value lost if not yet absorbed by Totalizer | Code-inspected; not independently power-cut tested this session |
| During sample construction | The in-RAM `QRow` is lost; nothing written yet | Code-inspected |
| During queue append | `crc32_()` is computed over the FULL row before write; if power fails mid-`appendWrite()`, the file is left with a short/torn tail. **Next boot's `append()` call detects `actualSize != goodOffset` and calls `truncateTo()` BEFORE writing anything new** — the torn tail is discarded, never read back as valid (its `magic`/`crc32` would fail `pendingImpl_()`'s check even if not truncated first). | **Code-proven** (queue.h lines 274-302); not independently hardware-power-cut-tested this session (no physical power-cut capability available — see 04_POWER_FAILURE_AND_RECOVERY.md) |
| During checkpoint write | `tot_->setQueueOffset()` happens strictly AFTER the row write succeeds (queue.h line 339) — if power fails here, the row is durably on disk but the checkpoint doesn't yet reflect it; next boot's truncate-before-append logic reconciles this (the checkpoint's `goodOffset` would be stale-low, and the actual file would be longer — handled by the `actualSize > goodOffset` branch, which truncates to the OLD checkpoint value, meaning **a row genuinely written but not yet checkpointed is discarded on recovery** — a real, disclosed loss-of-one-row scenario, not a corruption scenario) | Code-inspected; the specific interleaving was not fault-injected this session |
| During ack-cursor write | `persistAck_()` uses the same dual-slot CRC pattern as `AckRec`/`FailureState` — a torn write leaves the OTHER slot (previous good `writes` counter) as the recovered value on next boot (`begin()`'s `(a.writes >= b.writes) ? a : b` selection, only among CRC-valid slots) | **Code-proven** (dual-slot, CRC-guarded, `queue.h` lines 189-194) |
| During segment deletion | `persistAck_()` (cursor advance) happens **before** `LittleFS.remove()` of consumed segments (queue.h line 550-555, explicit comment: "persist the new cursor FIRST, delete completed segments SECOND"). A crash here leaves an already-acked segment undeleted — reconciled by simply re-deleting it on the next successful `ackThrough()` call (idempotent — `LittleFS.remove()` on an already-gone file is a no-op) | **Code-proven, explicitly documented in the source itself** |

### 10. Maximum unsynced storage capacity — real calculation

```
LittleFS ("spiffs") partition, confirmed from the ACTUAL installed
partition table (framework-arduinoespressif32/tools/partitions/default_16MB.csv,
read directly this session):
  spiffs, data, spiffs, 0xc90000, 0x360000
  = 3,538,944 bytes (3.375 MiB)

QRow size (packed struct, confirmed from queue.h):
  4(magic)+2(schema_version)+2(record_type)+4(boot_id)+4(seq)+4(ts)
  +8(totalizer)+2(quality)+2(rssi_abs)+4(crc32) = 36 bytes

Theoretical raw ceiling (partition_size / row_size):
  3,538,944 / 36 = 98,304 rows  (ignoring ALL overhead)

QUEUE_SEGMENT_ROWS = 5000 rows/segment -> 180,000 bytes/segment file
AckRec (28 bytes) x2 files + FailureState (28 bytes) x2 files = 112 bytes
  -- negligible against the partition size
QUEUE_HIGHWATER = 20,000 rows -- a SOFT alarm threshold only (config.h's
  own comment: does not stop writes), NOT the hard capacity limit
```

**Real, live discrepancy found this session, not present in prior
documents**: the device's LIVE `capacity_pct_used` read **~59%** while
the actual pending backlog was only ~89-270 rows (far below even the
20,000-row soft threshold, let alone the 98,304-row theoretical ceiling).
`capacityPercentUsed()` reads REAL `LittleFS.usedBytes()/totalBytes()`
(queue.h's own comment confirms this is deliberately the *real* figure,
not an estimate) — meaning **actual on-flash usage is far higher than
the small live backlog would suggest**, most plausibly from historical
segment files / LittleFS metadata / wear-leveling reserve consuming a
meaningfully larger share of the partition than the naive
row-count-only ceiling assumes.

**This session did NOT perform a fill-to-capacity test** (that would
require sustained device time this session did not have available, or
writing a large volume of synthetic data neither authorized nor prudent
against a data-bearing device). **The true, empirically-proven maximum
offline buffering capacity is therefore NOT established** — only:
(a) a theoretical, overhead-ignoring ceiling (98,304 rows / ~27.3 hours
at 1 sample/sec), and (b) live evidence that real-world usable capacity
is meaningfully lower than that ceiling. **Reported honestly as an open
question, not resolved by either number alone.**

## Buffering time at various sampling rates (theoretical ceiling only — see caveat above)

| Sampling interval | Theoretical row capacity used | Time at ceiling |
|---|---|---|
| 1 second (`TELEMETRY_PERIOD_MS`, this firmware's actual configured rate) | 98,304 rows | ~27.3 hours (~1.14 days) |
| Hypothetical 5-second sampling | 98,304 rows | ~5.7 days |
| Hypothetical 10-second sampling | 98,304 rows | ~11.4 days |

**At the firmware's actual configured 1-second sampling rate, the
theoretical ceiling is ~1.14 days — not "several days."** This directly
fails Part 6/19's "offline buffering for several days" success criterion
at face value, unless (a) overhead turns out even more favorable than
feared (unverified), or (b) the sampling interval is deliberately
increased for a real deployment (a configuration decision, not a code
change). **Flagged as a genuine, quantified gap**, not glossed over.

## Part 2 — Flash Memory Reliability

### 1-4. File categories

| Category | Files |
|---|---|
| Rewritten repeatedly (ping-pong, dual-slot) | `ackA.bin`/`ackB.bin` (every `ackThrough()` call — i.e. every successful sync cycle), `failA.bin`/`failB.bin` (only on an actual write failure, never on the hot path) |
| Append-only | `/queue/seg_NNNNNN.bin` (each active segment; a new segment is a new file, never rewritten in place except its own append pointer advancing) |
| Erased/deleted (not "rewritten") | Fully-consumed segment files, removed via `LittleFS.remove()` once acked |
| NVS (separate partition entirely, `Preferences` library) | `server_url`, `api_key`, `wifi_ssid`/`wifi_pass`, `cfg_ver`, `boot_id` (namespace `covio`), `sec_ver` (namespace `covio_sec`, separate partition region reads/writes) |

### 5-8. Wear leveling

- **LittleFS wear leveling**: active by design (LittleFS is a
  wear-leveling filesystem; this is a property of the filesystem itself,
  not something this project's code opts into or out of) — **not
  independently verified via direct flash-wear measurement this
  session** (would require many write cycles over real time, out of
  scope for a single audit pass).
- **NVS wear leveling**: ESP-IDF's NVS library performs its own
  wear-leveling across the NVS partition's pages — same caveat, not
  independently measured.
- **Totalizer checkpoints**: not confirmed wear-distributed this pass
  (totalizer.h not read line-by-line this session) — flagged as an
  open item.
- **Queue writes**: distributed across segment files (each new segment
  is fresh LittleFS-block allocation, so writes are NOT concentrated on
  one fixed sector the way a single ever-growing file would be) — this
  is a real, positive design property, code-confirmed.

### 9-11. Write volume and lifetime estimate

```
Samples/day at 1Hz: 86,400
Bytes/day (queue rows only): 86,400 x 36 = 3,110,400 bytes/day (~3.0 MiB/day)
Segment rotations/day: 86,400 / 5,000 rows-per-segment = ~17.3 segment
  files created (and, once acked, deleted) per day
ack writes/day: one per successful sync cycle. PUSH_PERIOD_MS=5000ms =>
  up to 17,280 push attempts/day, each ack'd write alternating ackA/ackB
  -> up to ~17,280 NVS/LittleFS small-file writes/day to the ack slots
  IF every single push cycle results in a NEW cumulative ack (in
  practice, likely somewhat fewer, since a push with nothing new to
  acknowledge doesn't call ackThrough() at all -- pushOnce() only POSTs
  when there is pending data, per sync.h's own `if (n == 0) return false;`
  early return)
checkpoint writes/day: not independently quantified (totalizer.h not
  read this session)
```

**Estimated erase-cycle rate per "hot" sector**: the `ackA.bin`/`ackB.bin`
pair is this system's single most frequently rewritten location. At up
to ~17,280 writes/day split across 2 alternating slots (~8,640
writes/day/slot), and LittleFS's own wear-leveling spreading each
logical file's writes across many physical blocks over time (not one
fixed physical sector per logical file, by design) — a literal
"per-sector" erase-cycle count was **not measured directly** (would
require LittleFS-internal block-allocation tracing this session did not
perform). Using the two bounding assumptions requested:

```
At 10,000 erase-cycle NAND-class assumption, ~8,640 ack-slot writes/day:
  10,000 / 8,640 ≈ 1.16 days to exhaust a SINGLE FIXED SECTOR if wear
  leveling did NOT spread writes at all (worst-case, NOT what actually
  happens -- LittleFS wear-leveling exists specifically to prevent this)
At 100,000 erase-cycle assumption, same write rate:
  100,000 / 8,640 ≈ 11.6 days under the same no-wear-leveling worst case
```
These worst-case numbers are **not the realistic expected lifetime** —
they bound what would happen with wear-leveling disabled, to illustrate
why wear-leveling being genuinely active matters. With wear-leveling
genuinely spreading writes across the LittleFS partition's many blocks
(3.375 MiB / typical 4KB block ≈ 864 blocks), a rough, order-of-magnitude,
**not independently measured** estimate would put realistic sector life
at roughly 864× longer than the single-sector worst case above — i.e.
plausibly years, not days — but this is **an estimate from
wear-leveling's general design principle, not a measurement**, and is
reported as such.

### Does the design risk premature wear?

- Per-second checkpointing of the *queue row itself* is append-only
  (new bytes at the file's end within a segment) — this does NOT
  repeatedly erase the same sector; **low risk**.
- Frequent ack-slot rewrites (§ above) ARE the most concentrated write
  pattern in this system — **the single item most worth independent,
  measured verification** before an unattended multi-year deployment
  claim, not yet done.
- Alarm/log persistence: `failA.bin`/`failB.bin` are only written on an
  actual failure (queue.h's own comment: "adds no routine wear") — low
  risk by design.
- Configuration churn: NVS writes for `server_url`/`api_key`/Wi-Fi only
  happen on operator-initiated reconfiguration, not routinely — low risk.

### Flash lifetime verdict

**LIKELY ACCEPTABLE, NOT PROVEN.** The design (segmented append-only
queue, dual-slot CRC'd small metadata files, LittleFS's own
wear-leveling) follows sound low-wear patterns and the ack-slot
write-frequency arithmetic does not indicate an obviously dangerous
rate — but no direct, measured flash-wear or long-duration write-cycle
test was performed this session. **Not classified as "proven acceptable"
— classified as "likely acceptable, pending a real long-duration wear
measurement."**
