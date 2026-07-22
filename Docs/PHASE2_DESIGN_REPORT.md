# Phase 2 Design Report — ADR-003: Truncate-Before-Append Framing with Segmented Rotation

**Status:** DESIGN ONLY — no firmware modified, no commits made, per explicit instruction.
**Prepared:** 2026-07-08 (session following Phase 1 certification review).
**Sources re-read in full for this report:** `Docs/Firmware Detailed Architecture Decision Record (ADR).md` (ADR-003 section, lines 84–123), `Docs/Firmware Phase-wise Implementation Master Plan.md` (ADR-003 Implementation Card, lines 63–77; Phase 2 Phase Plan, lines 315–333; Migration Checklist item 1, line 598), `Docs/MASTER_GOVERNANCE.md` (ACR workflow, §8). Current source read in full: `queue.h`, `totalizer.h`, `covio_firmware.ino`, `sync.h`, `ota.h`, `config.h`.

This report does not redesign ADR-003. Every mechanism below is either lifted directly from the ADR/Master Plan text, or — where the ADR is deliberately silent on a byte-level/API-level detail (exactly the category of "mechanical implementation step downstream of a fully-specified architecture" the ADR documents reserve for implementation time) — a specific, narrow implementation choice is proposed and explicitly labeled as such, so a reviewer can approve or override it without having to invent it themselves.

---

## 0. Blocking Precondition Not Yet Resolved

Before reading further: **ADR-003's own practicality flag has never been checked.** Phase 0 was supposed to verify that the SD/SdFat library used by this project supports truncating an open file to a smaller size (`File::truncate(offset)` or equivalent), and record the finding in `VERSIONS.md`/`SCHEMA_REGISTRY.md`. Phase 0 was never completed (confirmed in the Phase 1 certification review) — no such finding exists anywhere in this repository.

**This is a hard stop condition per both the ADR and the Master Plan**, quoted verbatim: *"If a truncate-to-offset capability is not available, implementation must stop and an Architecture Change Request must be raised against ADR-003 — do not substitute a different resync mechanism unilaterally."*

This report proceeds on the assumption that a suitable truncate API exists (it is referenced throughout as `File::truncate(offset)`), because the design cannot be written without assuming *some* mechanism. **This assumption is not verified and must be confirmed — or an ACR raised — before Task P2-T0 (Section 9) is executed.** This is listed again as Risk #1 in Section 10 and is not resolved by this report, per the instruction to document ADR conflicts/open items without solving them.

---

## 1. Current Queue Architecture

### 1.1 File format (as it exists today, pre-Phase-2)

Three files under `/queue/` (plus the totalizer's own two checkpoint files at SD root, not `/queue/`):

| File | Purpose | Format |
|---|---|---|
| `/queue/log.bin` | Single unbounded append-only log of all telemetry rows ever written and not yet compacted | Sequence of fixed-size 36-byte `QRow` records, no header, no index |
| `/queue/ackA.bin` | Ack pointer, slot A | Single fixed-size 20-byte `AckRec`, dual-slot alternating with slot B |
| `/queue/ackB.bin` | Ack pointer, slot B | Same as above |

(`/totA.bin`, `/totB.bin` at SD root are the totalizer's own dual-slot checkpoint, unrelated to the queue today — but Phase 2 couples them, see §3.)

### 1.2 Row layout (current, post-Phase-1/ADR-001)

`QRow`, `__attribute__((packed))`, 36 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `magic` (`QROW_MAGIC` = `0x51524F57`) |
| 4 | 2 | `schema_version` |
| 6 | 2 | `record_type` |
| 8 | 4 | `boot_id` |
| 12 | 4 | `seq` (globally monotonic across reboots) |
| 16 | 4 | `ts` |
| 20 | 8 | `totalizer` |
| 28 | 2 | `quality` |
| 30 | 2 | `rssi_abs` |
| 32 | 4 | `crc32` (computed over the whole struct with this field zeroed) |

`AckRec`, `__attribute__((packed))`, 20 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `magic` (`ACK_MAGIC`) |
| 4 | 4 | `acked_seq` (cumulative — server has all `seq <= this`) |
| 8 | 4 | `boot_id` |
| 12 | 4 | `writes` (monotonic counter — determines which slot is newest, and which slot the next write targets) |
| 16 | 4 | `crc32` |

### 1.3 Append flow (`EventQueue::append`, `queue.h:82-92`)

```
append(row):
  row.magic = QROW_MAGIC
  row.crc32 = 0
  row.crc32 = crc32(row)
  f = SD.open(LOG_PATH, FILE_APPEND)   // blind append — OS positions at current EOF
  f.write(row, 36 bytes)
  f.flush()
  f.close()
```

No verification of the previous append's integrity happens before this write. No return-value check on `f.write()`. No offset bookkeeping at all.

### 1.4 Read flow (`EventQueue::pending`, `queue.h:95-111`)

```
pending(out, maxN):
  f = SD.open(LOG_PATH, FILE_READ)     // ALWAYS starts at file offset 0
  n = 0
  while n < maxN and bytes_available >= 36:
    read 36 bytes into r
    if r.magic != QROW_MAGIC: continue        // skip this 36-byte slot, advance by 36
    if crc(r) mismatch: continue               // "torn" row skipped, advance by 36
    if r.seq <= ack_.acked_seq: continue        // already acked, skip
    out[n++] = r
  f.close()
  return n
```

**Every call rescans from byte 0 of the entire file**, re-skipping every already-acked row every single time, regardless of how large the file has grown. This is the defect ADR-003's Problem Statement describes as "reading the queue rescans from the beginning of the file on every push cycle."

**Critical defect this exposes:** the "skip on mismatch" logic advances exactly `sizeof(QRow)` (36 bytes) per failed match — it is a fixed-stride skip, not a byte-by-byte resync scan. If a torn write ever leaves a non-36-byte-aligned partial row at the file's tail, and a subsequent append writes the next row directly after those stray bytes, **every row boundary from that point forward in the file is permanently misaligned**. Every 36-byte chunk read from then on straddles two real rows, magic will essentially never match, and the fixed-stride skip can never re-synchronize. All rows physically present after the corruption point become permanently, silently unreadable — never counted, never acked, never deleted — even though they remain durably on the SD card forever. This is precisely the failure mode ADR-003 exists to eliminate.

### 1.5 Acknowledgement flow (`Sync::pushOnce` in `sync.h`, `EventQueue::ackThrough` in `queue.h:116-122`)

```
pushOnce():
  batch = q.pending(..., PUSH_BATCH_MAX)   // up to 50 rows, full rescan (§1.4)
  if batch empty: return false
  POST batch as JSON
  if HTTP code != 200: return false          // queue untouched, retry next cycle
  ackSeq = parse "ack_seq" from response body
  if ackSeq not parseable: return false       // bare 200 is NOT an ack (Invariant 6)
  q.ackThrough(ackSeq, bootId)
  return true

ackThrough(ackedSeq, bootId):
  if ackedSeq <= ack_.acked_seq: return       // never regress
  ack_.acked_seq = ackedSeq
  ack_.boot_id = bootId
  persistAck_()          // dual-slot CRC write, alternates path on `writes` parity
  maybeCompact_()
```

### 1.6 Deletion flow (`maybeCompact_`, `queue.h:163-166`)

```
maybeCompact_():
  if not empty():  return      // empty() == pending(tmp,1) == 0, i.e. a FULL RESCAN
  SD.remove(LOG_PATH)          // deletes the ENTIRE file, all-or-nothing
```

Compaction only ever happens when the **entire file** has zero unacked rows anywhere in it. There is no partial/segment-level deletion — this is the "single-file, all-or-nothing compaction model" ADR-003's Problem Statement names directly.

### 1.7 Retry flow

Retry is purely time-based and non-adaptive: `covio_firmware.ino`'s `loop()` calls `pushOnce()` once every `PUSH_PERIOD_MS` (5000ms), regardless of outcome. A failed push (offline, non-200, unparseable ack) leaves the queue untouched and simply waits for the next fixed-period tick — including when a push returns a **full** batch (meaning more backlog certainly remains), the device still waits the full 5 seconds before trying again. This is the "artificial fixed rate" ADR-003's third sub-decision (adaptive push) removes.

### 1.8 Startup recovery (`EventQueue::begin`, `queue.h:71-80`)

```
begin():
  if /queue doesn't exist: create it
  load ackA, load ackB (each validated: right size + magic + CRC)
  ack_ = whichever slot is valid and has the higher `writes` counter
         (or the only valid one, or zero-initialized if neither is valid)
```

There is **no recovery step for `log.bin` itself** — no truncation, no validation pass, nothing. Any misalignment described in §1.4 simply persists silently until the next full compaction (which, per §1.6, can only ever happen when the entire file — including the unreadable tail — reports as "empty," which it structurally cannot if real unacked data is trapped past a misalignment point).

### 1.9 Checkpoint handling

The **only** persisted checkpoint today is the ack pointer (`AckRec`, dual-slot CRC, §1.2). There is no persisted "last known good write offset" for `log.bin` — this is exactly the missing piece ADR-003's framing decision adds, explicitly reusing the totalizer's already-proven dual-slot CRC pattern (`totalizer.h`'s `Checkpoint` struct, described in full in §3).

### 1.10 SD interactions (summary)

Per push/append cycle today: up to 1 open+read+close of `log.bin` (full rescan) for `pending()`, 1 open+append+flush+close of `log.bin` for `append()`, at most 1 open+write+flush+close of `ackA.bin`/`ackB.bin` for a confirmed ack, at most 1 `SD.remove()` of `log.bin` on full drain. No per-append checkpoint I/O exists today (that is new in Phase 2).

### 1.11 Complete data flow (current, pre-Phase-2)

```
 PCNT hw counter
      │
      ▼
 Totalizer.total()  ──service()──►  /totA.bin /totB.bin  (dual-slot CRC checkpoint)
      │
      ▼
 Telemetry::build()  ──►  QRow
      │
      ▼
 EventQueue.append() ──blind append──► /queue/log.bin  (single unbounded file)
      │
      │            (every PUSH_PERIOD_MS, independent of append cadence)
      ▼
 EventQueue.pending() ──FULL RESCAN FROM OFFSET 0──► batch[≤50]
      │
      ▼
 Sync.pushOnce() ──POST JSON──► server
      │
      ▼ (only if 200 + parseable ack_seq)
 EventQueue.ackThrough(ackSeq)
      │
      ├──► persistAck_() ──► /queue/ackA.bin or ackB.bin (dual-slot CRC)
      │
      └──► maybeCompact_() ──► if fully empty: SD.remove(/queue/log.bin)  [all-or-nothing]
```

---

## 2. ADR-003 Mapping

| Requirement | Current Implementation | Required Change | Affected Function | Affected File | Implementation Risk | Testing Needed |
|---|---|---|---|---|---|---|
| Persisted "last known good write offset," dual-slot CRC, reusing totalizer's pattern | Does not exist | Extend `totalizer.h`'s `Checkpoint` struct with `q_segment`/`q_offset`; add accessor methods (§3, §5) | `Totalizer::begin/persist_/load_` (extend), new `setQueueOffset()` | `totalizer.h` | Medium — couples two subsystems (see Risk #3) | Native: round-trip persist/load of new fields |
| Compare file size to persisted offset before every append; truncate if larger | No comparison, no truncation, blind `FILE_APPEND` | Rewrite `append()` to open r/w, stat size, compare to checkpoint, truncate if needed, then write | `EventQueue::append` | `queue.h` | **High** — depends on unverified truncate API (§0, Risk #1) | Native: torn-write simulation; Bench: B1–B4 (§8) |
| No row ever appended on top of unconfirmed bytes; no resync scanning | Not applicable (no such mechanism exists) | Same as above — determinism replaces the fixed-stride resync entirely | `EventQueue::append`, `pending()` (resync logic removed) | `queue.h` | High (same as above) | Native + Bench (same as above) |
| Fixed-size segment files, 10,000 rows each, increasing numbered order | Single unbounded `log.bin` | New segment-file naming/lifecycle (§5); `QUEUE_SEGMENT_ROWS` constant | `EventQueue::append` (rollover), new helpers | `queue.h`, `config.h` | Medium | Native: rollover-at-boundary test |
| Segment deleted in entirety once all its rows acknowledged | `maybeCompact_()` deletes the whole file only when 100% empty | Per-segment deletion once cursor advances past it | `EventQueue::ackThrough` | `queue.h` | Medium | Native: deletion-only-below-cursor test |
| Ack record extended with segment+offset of first unacked row | `AckRec` has no cursor fields | Extend `AckRec` with `cursor_segment`/`cursor_offset` | `AckRec` struct, `loadAck_`/`persistAck_` | `queue.h` | Low–Medium (struct size change → boot-compat implication, §4) | Native: struct round-trip; Bench: first-boot-after-migration check |
| Cursor recomputed/persisted ONLY on confirmed cumulative ack advance, never on mere read | Not applicable (no cursor exists) | `ackThrough()` walks forward from current cursor to find new cursor; `pending()` never writes to it | `EventQueue::ackThrough`, `pending()` | `queue.h` | **High** — this is "the single most important correctness property" per the ADR's own text; any code path that mutates the cursor outside a confirmed ack is a direct ADR violation | Native: "failed push must not move cursor" test (explicitly named in Master Plan item 7) |
| Every read begins from persisted cursor, never file start | `pending()` always opens at offset 0 | Rewrite `pending()` to seek to cursor segment+offset first | `EventQueue::pending` | `queue.h` | Medium | Native: no-rescan-of-acked-prefix test; Performance test (§8) |
| Remove fixed post-push wait for full batches; up to 10 consecutive pushes per loop pass during catch-up | Fixed `PUSH_PERIOD_MS` wait every cycle regardless of outcome | Loop change: if last batch was full, immediately retry, capped at 10 consecutive attempts | `loop()` push-timing block | `covio_firmware.ino` | Low | Bench: catch-up burst timing; Native (loop logic extracted if feasible) |
| Invariant preserved: never delete unacked data | Preserved today via all-or-nothing compaction | Must remain true under per-segment deletion — deletion only after cursor is durably past a segment | `EventQueue::ackThrough` | `queue.h` | High if violated — this is the core "zero silent data loss" claim | Native + Bench: ordering-invariant test (§5, §7) |
| Migration: forced full drain before combined Phase 1+2 OTA | N/A (no prior migration existed) | Operational procedure, not code (§4) | — | — | Medium (process risk, not code risk) | Bench: drain verification procedure |

---

## 3. File Impact Analysis

### 3.1 `queue.h`

**Structs that must change:**
- `AckRec` — add `uint32_t cursor_segment;` and `uint32_t cursor_offset;` (placed before `crc32` so CRC coverage naturally includes them). New size: 28 bytes (was 20). **This is a breaking on-disk format change** — see §4 for the boot-compatibility consequence.
- `QRow` — **no change**. ADR-003 is a file-level/framing concern; it does not alter row content. (Confirming this explicitly for the reviewer, since ADR-001 did change `QRow` and it would be easy to conflate the two.)
- New struct: none directly in `queue.h` — the write-offset checkpoint lives in `totalizer.h`'s `Checkpoint` per the Master Plan's explicit file-impact statement (see §3.2).

**Functions that must change:**
- `begin()` — must additionally: (a) obtain the recovered write-offset checkpoint from the (already-`begin()`'d) `Totalizer` instance — requires a new parameter, `begin(Totalizer* tot)`; (b) scan `/queue/` for `seg_*.bin` files and delete any numbered higher than the checkpoint's segment (orphan-rollover cleanup, §5.6); (c) load the extended `AckRec` (now carrying the cursor).
- `append()` — complete rewrite: truncate-before-append check, rollover-on-cap check, write, flush, checkpoint update, in that exact order (§6.1 has the full sequence).
- `pending()` — rewrite to begin reading from the persisted cursor (segment + offset) instead of file offset 0; must handle crossing a segment boundary mid-call if `maxN` spans more than one segment's remaining rows.
- `ackThrough()` — extend to: walk forward from the current cursor, find the new cursor position for the given `ackedSeq`, persist the extended `AckRec` (cursor included), THEN delete any segment file now strictly below the new cursor segment (ordering invariant, §5.4).
- `empty()` — semantics unchanged in spirit ("no unacked rows anywhere") but its implementation must be cursor-aware rather than doing a fresh `pending()` scan from zero each time it's convenient to call it.
- `maybeCompact_()` — **removed**, replaced by the per-segment deletion inside `ackThrough()` (§2, §5.4). No more all-or-nothing compaction.
- New private helpers needed: `segmentPath(uint32_t id)` (naming, §5.1), a rollover-trigger check (row-count-from-file-size, §5.3), orphan-segment cleanup (§5.6).

**Persistence changes:** three files become a growing/shrinking set of `seg_NNNNNN.bin` files (§5.1) instead of one `log.bin`; `ackA.bin`/`ackB.bin` grow from 20 to 28 bytes; a new dependency on `totalizer.h`'s checkpoint files (`/totA.bin`, `/totB.bin`) for the write-offset (no new files of `queue.h`'s own for this purpose, per §3.2).

**API changes:** `EventQueue::begin()` signature changes from `begin()` to `begin(Totalizer* tot)` — this is a call-site change required in `covio_firmware.ino` (§3.3).

**Dependencies:** `queue.h` gains a compile-time dependency on `totalizer.h` (needs `Totalizer`'s new accessor methods declared before `queue.h` can call them) — confirm include order in `covio_firmware.ino` already has `totalizer.h` included before `queue.h` (it does, line 22 before line 23) — no reordering needed, but `queue.h` itself does not currently `#include "totalizer.h"`; it will need to either add that include or take a forward-declared pointer and rely on the `.ino`'s existing include order. **Recommend forward declaration** (`class Totalizer;` + pointer-only usage in the header, calling only through the public accessor methods) to avoid a circular/heavy include — flagged as an implementation-level choice for the assigned engineer, not an architectural one.

### 3.2 `totalizer.h`

**Structs that must change:**
- `Checkpoint` — add `uint32_t q_segment;` and `uint32_t q_offset;` (placed before `crc32`). New size: 28 bytes (was 24). This is exactly what the Master Plan's file-impact line specifies: *"totalizer.h (checkpoint structure extended with the persisted write-offset field, reusing its existing dual-slot CRC mechanism)."*

**Functions that must change:**
- `persist_()` / `load_()` — no logic change needed (they already serialize/CRC the whole struct generically); the new fields ride along automatically once added to the struct.
- New public methods required (none exist today for external mutation of `cp_` outside `service()`):
  - `void setQueueOffset(uint32_t segment, uint32_t offset)` — sets `cp_.q_segment`/`cp_.q_offset` and calls the existing private `persist_()` immediately (synchronous, mirroring how `ackThrough()` persists synchronously today).
  - `uint32_t queueOffsetSegment() const` and `uint32_t queueOffsetOffset() const` — read accessors for `EventQueue::begin()`'s recovery step.

**Persistence changes:** `/totA.bin`/`/totB.bin` grow from 24 to 28 bytes each.

**API changes:** three new public methods added to `Totalizer` (additive only — no existing signature changes).

**Dependencies:** none new — `totalizer.h` does not need to know anything about `queue.h`, segments, or rows. It only stores two opaque `uint32_t`s on behalf of the queue. (This one-directional dependency — `queue.h` depends on `totalizer.h`'s new accessors, not the reverse — is what keeps this workable; seenotedas Risk #3 in §10 regardless, since it's still a cross-subsystem coupling even if one-directional.)

### 3.3 `covio_firmware.ino`

**Functions that must change:**
- `setup()` — the call `eventQueue.begin();` (line 62) becomes `eventQueue.begin(&totalizer);`. No reordering needed: `totalizer.begin(store.bootId());` already executes on line 61, immediately before, so the totalizer's checkpoint (including the new queue-offset fields) is already recovered by the time `EventQueue::begin()` needs to read it.
- `loop()` — the push-timing block (lines 111–119) must change from an unconditional fixed-period call to an adaptive one: on a full batch, immediately attempt another push (bounded at 10 consecutive attempts within one loop pass) instead of waiting `PUSH_PERIOD_MS`. Exact proposed shape (implementation-level detail, not yet written as code per the "no firmware code" rule):
  ```
  if (now - tPush >= PUSH_PERIOD_MS) {
    tPush = now;
    int attempts = 0;
    bool acked;
    do {
      acked = syncEngine.pushOnce();
      attempts++;
    } while (acked && syncEngine.lastBatchWasFull() && attempts < 10);
    ... (existing healthySignalled logic unchanged)
  }
  ```
  This requires `Sync::pushOnce()` to expose whether the batch it just sent was "full" (i.e., `n == PUSH_BATCH_MAX`), which it does not today — a small new accessor (`bool lastBatchWasFull()`) on `Sync`, set inside `pushOnce()`. **This touches `sync.h`, which the Master Plan's file-impact list for ADR-003 does not name explicitly** — flagged as a discovered scope gap, see §10 Risk register and the note at the end of this subsection.

**Structs that must change:** none in this file (it has none of its own).

**Persistence changes:** none directly (all persistence lives in `queue.h`/`totalizer.h`).

**API changes:** the `eventQueue.begin()` call-site signature change (above) is the only breaking change in this file.

**Dependencies:** none new.

> **Scope-gap note (not an ADR conflict, a Master Plan completeness gap):** the Master Plan's ADR-003 card lists only `queue.h`, `totalizer.h`, `covio_firmware.ino` as "files likely affected." Implementing the adaptive-push sub-decision as designed above requires a small, additive change to `sync.h` (exposing whether the last batch was full) that the Master Plan does not mention. This is documented here rather than silently added — it is a minimal, additive accessor, not a redesign of `sync.h`'s responsibilities, but a reviewer should explicitly bless "the file-impact list also includes `sync.h` (one new accessor method)" before implementation proceeds, since the Master Plan's own file list is treated as ADR-adjacent scope in this project's governance.

---

## 4. Storage Migration Plan

### 4.1 Forced drain process

Per ADR-003's Migration Strategy and the Master Plan's Migration Checklist item 1: any device that has ever run pre-Phase-2 firmware must be run under its **current** (pre-Phase-2) firmware until its queue reaches empty (`EventQueue::empty()` true, meaning `maybeCompact_()` has already deleted `log.bin` — today's all-or-nothing compaction, still in effect on the *old* firmware during this drain window) — **before** the combined Phase 1+2 OTA is flashed. This is identical in kind to ADR-001's own migration requirement and is delivered as **one** combined release, not two sequential forced drains (explicit instruction, Master Plan line 319 and 598).

As recorded in the Phase 1 certification report: **no device has been fielded yet**, so as of today this requirement is moot and should be recorded as "not applicable — no pre-existing fleet" per the Migration Checklist's own instruction, rather than silently skipped, at whatever point Phase 2 is formally closed out.

### 4.2 Empty-queue verification

Before flashing the combined Phase 1+2 OTA to any device that has run prior firmware, an operator must confirm **both**:
1. Device-side: the serial console's `show` command reports zero pending/queued rows (exact wording depends on `provision.h`'s current `show` output — not modified by this design, only referenced; confirming its output format is an implementation-time detail for whichever task touches the console, not part of ADR-003 itself).
2. Server-side: the receiver's last-known `ack_seq` for that device equals the highest `seq` the device has ever sent (i.e., nothing outstanding from the server's perspective either).

Both must agree before flashing — device-side alone is not sufficient, since a device could show zero *locally* if a bug caused it to under-report, while the server still expects more.

### 4.3 First boot after upgrade

This is where a real compatibility wrinkle exists, **discovered during this design pass, not previously documented**:

`AckRec` grows from 20 to 28 bytes (§3.1). `EventQueue::begin()`'s `loadAck_()` rejects any file whose size doesn't exactly match `sizeof(AckRec)` (`queue.h:142`, unchanged logic). This means: on first boot of the new (Phase 2) firmware, the **old-format** `ackA.bin`/`ackB.bin` (20 bytes each, left over from the pre-Phase-2 firmware) will both fail the size check and be treated as invalid — falling through to the zero-initialization branch, i.e. `ack_.acked_seq` resets to `0`.

**This is safe *only* because of the mandatory full-drain precondition in §4.1**: since the queue was fully drained (and `log.bin` deleted) before the upgrade, there is no historical un-acked data that a reset `acked_seq` could cause to be mishandled — `pending()` on the new firmware starts against an empty, freshly-created segment set, and the very next successful push simply receives a fresh, correct `ack_seq` from the server (which computes it independently from its own `records` table, not from anything the device tells it). No data loss results.

**This link between the drain precondition and the ack-struct format change must be treated as load-bearing**, not incidental: if the full-drain precondition is ever skipped (e.g., an operator flashes Phase 2 firmware onto a device with a non-empty pre-Phase-2 queue, against instructions), the old `log.bin` would be **abandoned but not deleted** (new firmware never opens/reads `log.bin`, only `seg_*.bin`), silently stranding whatever was in it forever, while the reset `acked_seq`/fresh empty segment set would give no indication anything was wrong. This must be called out explicitly in the Definition of Done's bench verification and in any field rollout runbook — flagged here as a required operational safeguard, not a code fix.

### 4.4 Rollback scenario

Per ADR-003's card item 9 (quoted in full): *"rollback after a successful drain-then-upgrade has no conflicting old-format data, since the queue was empty at the moment of upgrade; only rows written in the narrow pre-health-confirm window are at risk, a bounded and accepted property."*

Mechanically, per `ota.h`'s actual rollback trigger (§ read in full for this report): rollback is the ESP-IDF bootloader's automatic mechanism — it fires if the newly-booted image never calls `esp_ota_mark_app_valid_cancel_rollback()` (via `Ota::confirmHealthyBoot()`, itself gated on a successful WiFi connection + server contact) before the image crashes/resets. If the new (Phase 2) image runs briefly, writes some `seg_*.bin` files, and then rolls back before confirming health:

- The **old** firmware, once running again, only ever opens/reads `/queue/log.bin` — it has no code path that even looks at `seg_*.bin` files. Any rows the new firmware wrote during its trial window are therefore **structurally invisible** to the rolled-back old firmware — not corrupted, not misread, simply never touched.
- This means those rows are **silently stranded** on the SD card (occupying space, never sent, never counted) until/unless a future successful Phase 2 upgrade eventually reads them — this is the "bounded and accepted" property the ADR names. It is bounded specifically because the trial window (WiFi connect + one server contact) is short, so at most a handful of telemetry rows (at `TELEMETRY_PERIOD_MS` = 1/sec) could be written before either confirmation or rollback resolves the trial.
- This analysis is exactly what the Phase 2 Master Plan card requires be **bench-confirmed**, not just reasoned about (Success Criteria: "rollback safety analysis... is confirmed to hold in an actual bench rollback test"). See Bench Test B8, §8.

### 4.5 Corrupted queue scenario

If the write-offset checkpoint (in `totalizer.h`'s `Checkpoint`, §3.2) is itself unreadable — both dual-slot copies fail CRC — the natural fallback (mirroring the existing pattern used everywhere else in this codebase, e.g. `queue.h:78`, `totalizer.h:49`) is to zero-initialize: `q_segment = 0, q_offset = 0`.

**This is flagged as an unresolved, potentially dangerous edge case — not silently resolved by this report.** Under the truncate-before-append rule (§6), a zero-initialized checkpoint means the very next `append()` call will see `actualSize > 0 == q_offset` for segment 0 (assuming it has any content at all) and will **truncate segment 0 down to zero bytes** — destroying every row in that segment, even if segment 0 is fully intact and simply had the misfortune of its *checkpoint* (not its data) becoming unreadable. This could discard far more than "the single most-recent unconfirmed row" the ADR's Definition of Done bounds data loss to — potentially up to `QUEUE_SEGMENT_ROWS` (10,000) rows.

This exact scenario is carried forward into §7 (Failure Analysis) and §10 (Risk #2) and is **explicitly left for architect decision** — two candidate mitigations are named there without either being adopted, per the instruction to document ADR conflicts/open questions rather than resolve them.

### 4.6 Power failure during migration

Power loss during the drain window itself (§4.1) is not a new risk — the device is still running its *old*, unmodified firmware at that point, so all existing crash-safety guarantees (dual-slot CRC ack pointer, all-or-nothing compaction) apply unchanged.

Power loss during the **flashing** step (OTA write to the inactive partition) is entirely handled by the existing ESP-IDF OTA/bootloader mechanism (unchanged by ADR-003) — an incomplete flash simply fails to boot into the new partition and the device continues running the old, still-intact image.

Power loss during the **first boot** of the new firmware, before its own checkpoints stabilize, is covered by the same dual-slot CRC durability guarantee described throughout this report (§6, §7) — a cut mid-write to either the write-offset checkpoint or the ack/cursor record corrupts at most the slot currently being written; the other slot retains the last fully-valid value.

### 4.7 Upgrade failure

Covered by the existing, unmodified `ota.h` rollback mechanism (§4.4) — Phase 2 introduces no new upgrade-failure path beyond the new-format-data-stranding consequence already analyzed in §4.4.

---

## 5. Segment Design

### 5.1 Segment naming

Proposed (implementation-level choice, ADR does not specify a literal filename format): `/queue/seg_%06u.bin` — e.g. `seg_000000.bin`, `seg_000001.bin`, .... Zero-padded to 6 digits, decimal. **Rationale:** lexical directory-listing sort order equals numeric order, preserving the project's explicit design principle (ADR-003's "Alternatives Rejected" section, re: rejecting a journaling filesystem specifically to keep the card "readable and inspectable on any PC") — an operator plugging the card into a laptop sees segments in true chronological order without any tooling.

### 5.2 Segment lifecycle (state diagram)

```
                 ┌─────────────┐
   (rollover     │   CREATED   │  file exists, 0 rows
    from prior   │  (empty)    │
    segment, or  └──────┬──────┘
    first boot)         │ append() writes row 0
                         ▼
                 ┌─────────────┐
                 │   ACTIVE    │◄──┐ append() writes row N
                 │ (accepting  │───┘ (row count < QUEUE_SEGMENT_ROWS)
                 │  appends)   │
                 └──────┬──────┘
                         │ row count reaches QUEUE_SEGMENT_ROWS
                         ▼
                 ┌─────────────┐
                 │    FULL     │  no longer accepts appends;
                 │ (read-only, │  next segment becomes ACTIVE
                 │  awaiting   │
                 │    ack)     │
                 └──────┬──────┘
                         │ ackThrough() cursor advances past
                         │ this segment's last row
                         ▼
                 ┌─────────────┐
                 │  DELETABLE  │  SD.remove() — only AFTER the
                 │             │  new cursor is durably persisted
                 └─────────────┘
```

### 5.3 Segment rollover

Trigger: at the top of `append()`, before writing, compute `rowCount = q_offset / sizeof(QRow)` for the active segment (reusing the file-size read already required for the truncate check — no extra SD access). If `rowCount >= QUEUE_SEGMENT_ROWS` (proposed constant, `config.h`, value `10000` per the ADR's literal number):
1. Treat the current segment as FULL (no more appends to it, ever).
2. Advance the active segment id (`q_segment + 1`).
3. Create the new segment file (if it doesn't already exist — see §5.6 for why it might).
4. Reset the local working offset to 0 for the new file.
5. Proceed with the normal append (§6.1) into the new, now-active segment.

`QUEUE_SEGMENT_ROWS` is, per the ADR's own "Future Extension" note, "a single tunable constant" — defined once in `config.h`, not hardcoded inline.

### 5.4 Segment deletion

Occurs **only** inside `ackThrough()`, and **only** after the new cursor position has been durably persisted. Exact ordering invariant (must not be violated):

```
ackThrough(ackedSeq, bootId):
  if ackedSeq <= ack_.acked_seq: return                 // no regression, no cursor move
  newCursor = walk forward from current cursor,
              skipping rows with seq <= ackedSeq,
              stop at first row with seq > ackedSeq
              (or end-of-current-segment -> continue into next segment)
  ack_.acked_seq      = ackedSeq
  ack_.boot_id        = bootId
  ack_.cursor_segment = newCursor.segment
  ack_.cursor_offset  = newCursor.offset
  persistAck_()                                          // <-- STEP 1: persist FIRST
  for each segment id strictly less than newCursor.segment:
      SD.remove(segmentPath(id))                          // <-- STEP 2: delete SECOND
```

**Why this order, specifically:** if a reboot happens between "delete" and "persist," and the (now-)stale cursor still points at a since-deleted segment, `begin()`'s recovery would fail to find a file it expects to exist. Persisting first means: worst case after a reboot mid-operation, a segment that *should* have been deleted still physically exists — harmless (a few extra kilobytes on the SD card until the next successful ack cycle re-derives the same deletion), never data loss, never a dangling reference. This mirrors the exact same "row first, checkpoint second" safety reasoning already used between `eventQueue.append()` and `totalizer.service()` in today's `.ino` (comment at `covio_firmware.ino:104-107`).

### 5.5 First unacknowledged pointer / persistent cursor

Lives in the **extended `AckRec`** (`queue.h`, §3.1) as `(cursor_segment, cursor_offset)` — deliberately **not** in `totalizer.h`'s `Checkpoint` (which holds the **write**-side offset, a different concept — see the explicit warning in §3.2/§10 Risk #3 about not conflating the two). Two independent, purpose-specific checkpoints:

| Checkpoint | Lives in | Represents | Updated when |
|---|---|---|---|
| Write-offset (`q_segment`, `q_offset`) | `totalizer.h` `Checkpoint` (extended) | Last known-good **append** position | Every successful `append()` |
| Read cursor (`cursor_segment`, `cursor_offset`) | `queue.h` `AckRec` (extended) | First **not-yet-acknowledged** row position | Only on a confirmed server ack (`ackThrough()`) |

### 5.6 Recovery after reboot

`EventQueue::begin(Totalizer* tot)`:
1. Create `/queue` if missing (unchanged).
2. Load `AckRec` from both slots (as today, now including cursor fields) — pick the newer valid one, or zero-init if neither valid.
3. Read `tot->queueOffsetSegment()` / `tot->queueOffsetOffset()` (already recovered by `Totalizer::begin()`, which must run first — already true in `covio_firmware.ino`'s existing call order).
4. Scan `/queue/` directory for all `seg_*.bin` files present. For any segment numbered **strictly greater** than `tot->queueOffsetSegment()`: delete it. **Rationale:** the only way such a file could exist is an interrupted rollover (§5.3 step 3 completed — file created — but step 2's checkpoint advance, §6, never got persisted before a crash). Since the write-offset checkpoint is the sole authority on which segment is "really" active, any higher-numbered file is an orphan by definition and must not be treated as containing confirmed data (nothing was ever durably appended to it if the checkpoint never advanced to point at it).
5. No proactive truncation happens here — truncation is deferred to the first `append()` call, matching the ADR's literal phrasing ("before every append... compares... truncates... before appending" — describing the check as part of the append operation itself, not a separate boot step). This is a deliberate implementation choice, stated explicitly for reviewer visibility, not left implicit.

### 5.7 Directory layout diagram (steady state, mid-operation)

```
/queue/
├── seg_000012.bin   ← DELETABLE (fully acked, awaiting next ack cycle's cleanup)
├── seg_000013.bin   ← FULL (all 10,000 rows written, some still unacked)
├── seg_000014.bin   ← ACTIVE (currently accepting appends)
├── ackA.bin         ← AckRec incl. cursor = (segment=13, offset=88*36)
└── ackB.bin         ← previous AckRec (older `writes` counter)

/totA.bin            ← Totalizer Checkpoint incl. q_segment=14, q_offset=4212*36
/totB.bin            ← previous Checkpoint (older `writes` counter)
```

---

## 6. Truncate-Before-Append Design

### 6.1 Full `append()` sequence (proposed, Phase 2)

```
append(rowIn):
  row = rowIn; row.magic = QROW_MAGIC; row.crc32 = 0; row.crc32 = crc32(row)

  activeSeg   = tot->queueOffsetSegment()
  goodOffset  = tot->queueOffsetOffset()
  path        = segmentPath(activeSeg)

  f = SD.open(path, READ_WRITE)     // create if missing (new/rolled-over segment)
  actualSize  = f.size()

  // --- truncate-before-append -------------------------------------------
  if actualSize > goodOffset:
      f.truncate(goodOffset)        // [UNVERIFIED API — see §0]  discard torn tail
  else if actualSize < goodOffset:
      // impossible-in-theory state: checkpoint claims MORE good bytes than
      // the file physically has. Defensive handling only — see §7.
      goodOffset = actualSize
      log loudly (Serial)

  // --- rollover check ------------------------------------------------------
  rowCount = goodOffset / sizeof(QRow)
  if rowCount >= QUEUE_SEGMENT_ROWS:
      f.close()
      activeSeg += 1
      goodOffset = 0
      path = segmentPath(activeSeg)
      f = SD.open(path, READ_WRITE)   // create new segment

  // --- write -----------------------------------------------------------
  f.seek(goodOffset)
  bytesWritten = f.write(row, 36)      // MUST check return value (§7, new requirement)
  f.flush()
  f.close()
  if bytesWritten != 36:
      return                            // do NOT persist checkpoint on short write

  // --- checkpoint update (AFTER the row is durably flushed) -----------------
  newOffset = goodOffset + 36
  tot->setQueueOffset(activeSeg, newOffset)   // internally: dual-slot CRC persist
```

### 6.2 Current write (today, for contrast)

`SD.open(LOG_PATH, FILE_APPEND)` → write → flush → close. No size check, no truncation, no checkpoint. (Repeated from §1.3 for side-by-side comparison.)

### 6.3 Interrupted write

Power lost during the `f.write()`/`f.flush()` call in §6.1: the segment file's physical size grows by some partial amount (0–35 bytes) beyond `goodOffset`. The checkpoint (`tot->setQueueOffset`) is never reached — it still reflects the *previous* `goodOffset`. This is the exact condition the next boot's first `append()` call detects and truncates away (§6.1's `actualSize > goodOffset` branch).

### 6.4 Boot recovery

As designed (§5.6), no truncation happens proactively at boot — only orphan-segment deletion. The truncate itself happens lazily, the moment the first `append()` after reboot runs. This means a torn tail can sit on the SD card, untouched, for an arbitrary time between reboot and the next telemetry tick (`TELEMETRY_PERIOD_MS` = 1s in practice) — harmless, since nothing reads past the persisted cursor/checkpoint anyway, and `pending()` never looks past `goodOffset`'s implied row count either (it stops reading based on the ack cursor, which can never exceed confirmed, checkpointed data).

### 6.5 Truncate operation — exact semantics

`f.truncate(goodOffset)` must shrink the file to exactly `goodOffset` bytes, discarding everything after. This is the operation flagged as unverified in §0. If the actual library API differs in name or behavior (e.g., only supports truncating to the *current* file position after a seek, rather than an arbitrary offset argument), the sequence in §6.1 needs the smallest possible adjustment (seek then truncate-at-current-position) — **this is exactly the class of finding Phase 0 was supposed to produce**, and this report explicitly declines to guess further than flagging it.

### 6.6 Checkpoint update ordering — the key correctness insight

The checkpoint (`tot->setQueueOffset`) is updated **strictly after** the row's `write()`+`flush()` completes (§6.1). This has a subtle but load-bearing consequence, worth stating explicitly because it is easy to mistake for a bug: **if power is lost between the flush completing and the checkpoint persisting, the row is fully, physically present and intact on the SD card — and yet the next boot's truncate-before-append logic will still discard it**, because from the checkpoint's perspective nothing distinguishes "row fully written but not yet confirmed" from "row never written at all" — both look identical as `actualSize > goodOffset`.

This is **intentional, not a defect**, and matches ADR-003's Definition of Done verbatim: *"no more than the single most-recent unconfirmed row is ever lost."* The word "unconfirmed" is doing the work here — confirmation is defined as "the checkpoint says so," full stop, never "the bytes happen to look intact." This determinism is exactly why the ADR's Reasoning section rejects byte-scanning/salvage approaches — any attempt to "rescue" a not-yet-checkpointed row (even a genuinely intact one) reintroduces the non-determinism the ADR is designed to eliminate.

### 6.7 CRC validation (two independent domains, both retained)

1. **Per-row `QRow.crc32`** (existing, `queue.h`, unchanged) — still checked on every read in `pending()` as defense-in-depth, even though truncate-before-append should make a torn row structurally impossible to encounter going forward (nothing is ever appended on top of unconfirmed bytes; nothing writes into the middle of a segment). **Behavioral note for the implementer:** a CRC-invalid row encountered *after* Phase 2 ships should now be treated as a stronger anomaly signal than it is today (today it's "expected occasional torn write"; post-Phase-2 it likely indicates real media corruption, not a torn-write race) — worth a distinct log line, though not a new mechanism (out of scope: hooking this into ADR-002/004's diagnostics is a natural extension, not built here).
2. **Checkpoint-record CRC** — both the write-offset `Checkpoint` (totalizer.h) and the cursor-carrying `AckRec` (queue.h) are already, and remain, individually CRC-protected by their existing `persist_`/`loadAck_` routines — no change to the CRC mechanism itself, only to what fields it covers (the struct grew, the CRC call covers the whole struct as before).

### 6.8 Write ordering summary

```
row write+flush  ─────strictly before─────►  checkpoint persist (write-offset)
                                                        │
                                             strictly before
                                                        │
new cursor persist (ack-side)  ─────strictly before─────►  segment deletion
```

### 6.9 Sequence diagrams

**(a) Normal append, no prior corruption:**
```
append()                SD card
   │  stat size ────────►│  (actualSize == goodOffset)
   │◄────────────────────│
   │  seek(goodOffset)   │
   │  write(row) ────────►│
   │  flush() ───────────►│  [row durable]
   │  setQueueOffset() ──►│  [checkpoint durable — row now CONFIRMED]
```

**(b) Torn write, then recovery on next append:**
```
 (boot N)  append()          SD card
             write(row) ────►│  ...POWER LOST mid-write...
                              │  [partial bytes, checkpoint NOT updated]
 (boot N+1) append()          SD card
             stat size ──────►│  actualSize > goodOffset
             truncate(goodOffset) ──►│  [torn bytes discarded]
             seek/write(newRow) ────►│
             flush() ───────────────►│  [new row durable]
             setQueueOffset() ──────►│  [checkpoint durable]
```

**(c) Rollover:**
```
append()  rowCount(seg N) >= 10000
            close(seg N)                       [seg N now FULL, read-only]
            open/create(seg N+1)
            write(row 0 of seg N+1)
            flush()
            setQueueOffset(N+1, 36)             [checkpoint now points at N+1]
```

**(d) Ack → cursor advance → segment deletion:**
```
Sync.pushOnce()  ──ack_seq──►  EventQueue.ackThrough(ackSeq)
                                    │
                                    ▼
                          walk cursor forward to ackSeq
                                    │
                                    ▼
                          persistAck_()  [cursor durable]
                                    │
                                    ▼
                    delete segments strictly below new cursor.segment
```

---

## 7. Failure Analysis

| # | Scenario | Expected Behavior |
|---|---|---|
| 1 | Power loss mid-append, **before** flush | Partial/no bytes on disk beyond `goodOffset`; next boot's next `append()` truncates the fragment away before writing; the in-flight row is lost. **Bounded, expected** (matches DoD). |
| 2 | Power loss **after** flush, **before** checkpoint persist | Row is physically complete and intact, but unconfirmed; next boot's truncate-before-append discards it anyway. **Intentional** (§6.6) — no salvage attempted, by design. |
| 3 | Power loss **during** the checkpoint persist itself | Dual-slot CRC guarantees at most the currently-written slot is corrupted; the other slot retains the last valid checkpoint. Reboot may then see `actualSize` ahead of the *recovered* (older) checkpoint — same truncate-and-lose-one-row bound applies, once. |
| 4 | SD card physically removed while running | Not defined by ADR-003 itself — the runtime `SD_DEGRADED` state and non-durable fallback reporting are explicitly ADR-004/Phase 4 scope. Phase 2's only obligation here: a failed `SD.open()`/`write()` must not crash and must not proceed to persist a checkpoint for data that was never actually written (see #6, #7 below). Full degraded-mode UX is out of scope and must not be invented here. |
| 5 | Corrupted row (CRC mismatch) **mid**-segment, not at the tail | Should be **structurally impossible** post-Phase-2 (only the physical tail can ever be torn; nothing ever writes into the middle of an already-written segment). If observed anyway, it indicates media corruption unrelated to this ADR. `pending()` must keep skipping the single bad row without crashing (unchanged from today's behavior) — flagged as a natural future hook for a diagnostic counter (ADR-002/004 territory), not built here. |
| 6 | Corrupted checkpoint — **both** write-offset slots fail CRC | **Unresolved open question, flagged for architect decision (§4.5, §10 Risk #2)** — the naive zero-init fallback (consistent with existing precedent elsewhere in this codebase) would cause the very next append to truncate the active segment to zero, potentially destroying up to 10,000 legitimate rows instead of the ADR's bounded "at most one row" loss. Two candidate mitigations, neither adopted here: (i) if the checkpoint is entirely unreadable AND the segment file already has content, trust the file's existing size instead of assuming 0 (accepts a small risk of retaining a possibly-torn tail, in exchange for not guaranteed-destroying a whole segment); (ii) accept the zero/full-segment-loss outcome as consistent with how `AckRec`'s own total-invalidity fallback already behaves today. **Do not implement either without confirming this isn't itself an ADR deviation requiring an ACR** — this is new severity that ADR-003's stated bound does not cover. |
| 7 | Partial write (`f.write()` returns fewer bytes than requested, no full power loss) | **Currently unchecked anywhere in the codebase** (neither `queue.h` nor `totalizer.h` inspects `write()`'s return value today). Phase 2 must add this check (§6.1's `if bytesWritten != 36: return` step) — a short write must be treated identically to a torn/power-loss write: no checkpoint update, so the next append's truncate logic cleans it up. This is a **new defensive requirement**, not present in the current codebase even though the underlying gap already exists today (only surfaced as a real risk once an offset checkpoint exists to desynchronize from). |
| 8 | SD full (write fails / short-write due to no space) | Same handling as #7 — must not corrupt the checkpoint or crash. ADR-004's `LOW_SPACE` detection is the proper long-term mitigation (out of scope for Phase 2), but Phase 2 must not make an SD-full condition behave worse than "nothing new gets durably recorded until space is freed." |
| 9 | Reboot during the `truncate()` call itself | Filesystem-dependent outcome the ADR/Master Plan themselves flag as unverified (§0) — this report cannot characterize it without the missing Phase 0 finding. **Explicit test requirement**: bench power-cut tests (§8, B2) must specifically time cuts to land inside the truncate call, not only inside row writes, to empirically observe the real behavior. |
| 10 | Reboot during `ackThrough()`'s cursor persist, before segment deletion | Per the strict ordering invariant (§5.4): the new cursor may or may not have persisted (dual-slot CRC falls back to the prior valid cursor if the write was torn) — but in **either** case, no segment is ever deleted before its corresponding cursor advance is durably confirmed. Worst case: a segment that should have been deleted still exists and gets correctly re-recognized and removed on the next successful ack cycle. **No data loss, bounded extra SD usage only.** |
| 11 | Reboot during the segment-deletion `SD.remove()` call itself | Same reasoning as #10 — deletion only ever follows a persisted cursor advance, so a reboot here just means the removal is retried (or was already complete) next cycle. Requires confirming `SD.remove()` on an already-partially-removed or nonexistent file does not itself crash — an **assumption to verify natively/on-bench, not assumed silently** (added to the native test list, §8). |

---

## 8. Test Plan

### 8.1 Native tests

> **Gap discovered while planning this section:** Phase 1 stood up a native test harness for the **receiver** (`test/native/test_adr001_schema.py`, plain Python `unittest`). ADR-003's logic lives in **firmware C++** (`queue.h`/`totalizer.h`), which has no native (non-Arduino) build target anywhere in this repository — Phase 0's `platformio.ini`/native-C++-harness deliverable was never created either. **This is a precondition gap for Section 8's native tests, not something this design report resolves** — it must be addressed (likely as, or immediately before, Task P2-T11 in §9) before these tests can actually be written and run, even though their content can be fully specified now.

Required native tests (content specified now; harness to be stood up before P2-T11):

1. **Truncate-before-append**: given a checkpoint offset less than the actual file size, the next `append()` truncates to the checkpoint offset before writing the new row.
2. **Cumulative-ack contiguous-sequence / cursor-advance algorithm**: given a set of stored `seq` values and an `ackedSeq`, the computed new cursor lands at the correct (segment, offset).
3. **Cursor-only-advances-on-confirmed-ack** (explicitly named in the Master Plan's ADR-003 card, item 7): simulate a failed push (no ack) and assert the persisted cursor is byte-for-byte unchanged.
4. **Segment rollover at exactly `QUEUE_SEGMENT_ROWS`**: the (`QUEUE_SEGMENT_ROWS`+1)-th append lands in a new segment file, not the original.
5. **Segment deletion only strictly below the cursor's segment, never at or above it.**
6. **Orphan-segment cleanup on boot**: given segment files numbered above the recovered write-offset checkpoint's segment, `begin()` deletes them and no others.
7. **Persist-before-delete ordering** (fault-injection or mock-based): assert the ack/cursor persist call happens before any `SD.remove()` call in `ackThrough()`.
8. **CRC validation regression**: existing per-row and checkpoint CRC behavior is unchanged (still rejects tampered/torn records).
9. **Short-write handling** (new, from §7 #7): a write that returns fewer bytes than requested must not result in a checkpoint update.
10. **`SD.remove()` on an already-removed/nonexistent file does not crash** (§7 #11) — confirm against the actual library behavior, not assumed.

### 8.2 Bench tests (hardware required)

| ID | Test | Purpose |
|---|---|---|
| B1 | Power-cut during append, ≥3 different timing offsets within the write (per Master Plan item 11's explicit evidence requirement) | Confirm at most the single most-recent unconfirmed row is lost, everything prior intact and correctly acked |
| B2 | Power-cut during the `truncate()` call itself | Characterize the unverified §0/§7-#9 behavior empirically |
| B3 | Power-cut during checkpoint persist (write-offset) | Confirm dual-slot CRC fallback behaves as designed |
| B4 | Power-cut during segment rollover (new file created, checkpoint not yet advanced) | Confirm orphan-segment cleanup (§5.6) correctly handles this on next boot |
| B5 | Power-cut during ack persist / during segment deletion | Confirm §5.4's ordering invariant holds under real power loss, not just in reasoning |
| B6 | Multi-day-outage simulation (WiFi disconnected for an extended period, or synthetically inflated backlog) | Confirm drain time is bounded by network throughput alone, no rescan delay — this is the ADR's core performance claim, must be measured, not assumed |
| B7 | Segment lifecycle soak test — full fill-drain cycle across many segments, long duration | Confirm correct creation at the row cap and deletion once fully acked, repeatedly, over time |
| B8 | Rollback safety bench test — force a health-confirm failure post-Phase-2-OTA | Confirm the old firmware boots normally and the §4.4 "new-format data is structurally invisible, not corrupting" analysis holds in practice |
| B9 | SD-full condition (fill card near capacity) | Confirm no crash, no checkpoint desync (ties to ADR-004 boundary — must not regress) |
| B10 | SD truncate-capability confirmation | **The Phase 0 precondition itself (§0)** — must run and pass before B1–B5 are meaningful, since they all assume truncate works at all |

### 8.3 Long-duration soak tests

B7 (above), plus an unattended multi-day run at simulated pulse rate with connectivity present throughout, confirming no unbounded segment accumulation and no memory/resource leak under sustained normal operation.

### 8.4 Power-cut tests

B1–B5 collectively — each repeated at a minimum of 3 different timing offsets per the Master Plan's explicit evidence requirement (item 11).

### 8.5 Performance tests

- Drain-time comparison: a large simulated backlog (e.g., 50,000+ rows) drained under the current single-file implementation (baseline, measurable today) versus the Phase 2 segmented implementation — to empirically validate the claimed shift from unbounded/quadratic rescan cost to cost proportional to true backlog.
- Adaptive-push burst behavior: confirm no more than 10 consecutive push attempts occur per loop pass, and that the burst stops immediately once a non-full batch is returned (not just after exhausting the cap).

### 8.6 Recovery tests

Every scenario in §7 that is native-reproducible (corrupted checkpoint slot A only / slot B only / both invalid; corrupted mid-segment row CRC; orphan-segment cleanup) as native tests (§8.1); every scenario requiring genuine power loss (§7 #1–3, #9–11) as the corresponding bench test above.

---

## 9. Implementation Order

Every task below is scoped to **at most one logical commit**, per the instruction. Tasks are sequential unless noted.

| Task ID | Objective | Files | Dependencies | Expected commit | Expected verification |
|---|---|---|---|---|---|
| **P2-T0** | Resolve the SD-truncate capability precondition (§0) — verify `File::truncate()` (or equivalent) exists and behaves as assumed, on the actual pinned Arduino-ESP32/SdFat version | none (research/bench task) | none — **blocks everything below** | None, or an ACR document if the finding is negative | A minimal standalone sketch/test exercising the truncate call on real hardware; finding recorded in `VERSIONS.md` or a dedicated readiness note |
| P2-T1 | Add `QUEUE_SEGMENT_ROWS` and the segment path-format constant to `config.h` only — no logic change | `config.h` | T0 (positive finding) | "Phase 2 prep: add segment-size and path constants" | Compiles; no behavior change (constants unused so far) |
| P2-T2 | Extend `totalizer.h`'s `Checkpoint` struct with `q_segment`/`q_offset`; add `setQueueOffset()`/`queueOffsetSegment()`/`queueOffsetOffset()` accessors; no callers changed yet | `totalizer.h` | T1 | "Extend totalizer checkpoint with queue write-offset fields (ADR-003)" | Native: existing total/seq behavior unaffected; new accessors round-trip correctly |
| P2-T3 | Extend `queue.h`'s `AckRec` struct with `cursor_segment`/`cursor_offset`; update `loadAck_`/`persistAck_` for the new size — struct+persistence plumbing only, no cursor-computation logic yet | `queue.h` | T2 | "Extend AckRec with persisted read-cursor fields (ADR-003)" | Native: persist/load round-trip of new fields; CRC still validates |
| P2-T4 | Implement segment naming/open helpers (`segmentPath(id)`, row-count-from-size) and orphan-segment boot cleanup, wired into `begin()`; `append()`/`pending()` untouched so far | `queue.h` | T3 | "Add segment file management + orphan cleanup on boot (ADR-003)" | Native: given seg_0000..seg_0003 on disk and checkpoint segment=1, confirm seg_0002/seg_0003 deleted, seg_0000/seg_0001 retained |
| P2-T5 | Implement truncate-before-append + rollover inside `append()` (§6.1), using T2's accessors and T4's helpers | `queue.h` | T4 | "Implement truncate-before-append + segment rollover (ADR-003)" | Native: pre-seed a file larger than checkpoint offset, confirm truncation before the new row lands; append `QUEUE_SEGMENT_ROWS`+1 rows, confirm rollover at the boundary |
| P2-T6 | Implement cursor-aware `pending()` — reads start at the persisted cursor, span segment boundaries as needed, never re-read an already-passed segment | `queue.h` | T5 | "Read pending rows from persisted cursor position, not file start (ADR-003)" | Native: confirm no re-scan of passed segments; cost proportional to unread backlog only |
| P2-T7 | Implement cursor-advance + segment-deletion inside `ackThrough()` (§5.4) — persist-before-delete ordering | `queue.h` | T6 | "Advance persisted cursor and delete fully-acked segments only on confirmed ack (ADR-003)" | Native: cursor unchanged on a call that never happens (no false ack); segments strictly below new cursor deleted, others retained; persist-before-delete ordering test |
| P2-T8 | Wire `covio_firmware.ino`'s `setup()` to `eventQueue.begin(&totalizer)` | `covio_firmware.ino` | T7 | "Wire EventQueue to Totalizer's write-offset checkpoint (ADR-003)" | Compiles; manual boot smoke-test — Serial log shows recovered segment/offset |
| P2-T9 | Implement adaptive push-loop (§3.3) — remove fixed wait on full batches, cap at 10 consecutive attempts; requires the small `Sync::lastBatchWasFull()` accessor flagged in §3.3 | `covio_firmware.ino`, `sync.h` | T8 | "Adaptive catch-up push loop: bounded consecutive pushes on full batches (ADR-003)" | Bench/native: confirm ≤10 consecutive pushes before yielding; confirm immediate stop on a non-full batch |
| P2-T10 | Add short-write/return-value defensive checks (§7 #7, #8) | `queue.h`, `totalizer.h` | T9 | "Treat short/failed SD writes as unconfirmed, never persist their checkpoint (ADR-003 hardening)" | Native: mock/fault-injected short write; confirm checkpoint not advanced |
| P2-T11 | Stand up the native C++ firmware test harness (gap noted in §8.1) and add all native tests from §8.1 not already covered incrementally above, as one coherent suite | `test/native/` (new) | T10 | "Add complete ADR-003 native test suite" | Full native suite green |
| P2-T12 | Document the segment/checkpoint storage format (extends `SCHEMA_REGISTRY.md`, or a new dedicated doc — reviewer's call, not presumed here) | `SCHEMA_REGISTRY.md` (or new doc) | T11 | "Document ADR-003 segment/checkpoint storage format" | Doc review only |
| P2-T13 | Execute the full bench verification pass (B1–B10, §8.2) | none (evidence-gathering) | T12 (feature-complete) | Optional: a final "Phase 2 bench evidence" doc commit | Bench transcripts and timing data per Master Plan item 11, power-cut tests repeated ≥3× at varied offsets |

---

## 10. Risk Register

### High Risk

| # | Cause | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| 1 | SD truncate-to-offset capability has never been verified in this repository (Phase 0 was never completed) | The entire ADR-003 mechanism as specified may be inviable, requiring an ACR against ADR-003 before any of the rest of this design can be implemented | Unknown/unverified — must be resolved first, not estimated | P2-T0 must run before any other Phase 2 task; if unsupported, stop and raise an ACR — explicitly do not substitute a byte-scanning resync (ADR/Master Plan both state this directly) |
| 2 | Naive zero-init fallback when the write-offset checkpoint is totally unreadable, combined with truncate-before-append's blind trust of the checkpoint | Potential loss of up to one full segment (10,000 rows) instead of the ADR's bounded "at most one row" — a severity the ADR's Definition of Done does not cover | Low (requires both checkpoint slots to fail CRC simultaneously) but consequence severity is high | **Flagged for architect decision (§4.5, §7 #6), not resolved here.** Do not implement either candidate mitigation without confirming it isn't itself an ADR deviation requiring its own ACR |
| 3 | Cross-subsystem coupling: the write-offset checkpoint lives inside `totalizer.h`'s `Checkpoint` struct (pulse totals) rather than a queue-owned structure, per the Master Plan's explicit file-impact direction | Two conceptually unrelated subsystems now share one persisted record and one class's persistence cadence — a future totalizer-only change could accidentally affect queue durability guarantees, or vice versa | Certain (structural property, not probabilistic) | Not something to redesign around (it is explicitly directed by the Master Plan) — flagged for awareness; keep the coupling one-directional and accessor-mediated (§3.2) to minimize blast radius |

### Medium Risk

| # | Cause | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| 4 | `AckRec` format change (20→28 bytes) breaks compatibility with pre-Phase-2 ack files | `acked_seq` resets to 0 on first boot after upgrade | Certain, by design | Safe **only** because of the mandatory full-drain precondition (§4.3) — must be bench-confirmed (B-series), not just reasoned about; runbook must not skip the drain step |
| 5 | No native C++ firmware test harness exists (Phase 0 gap; Phase 1 only built a Python receiver-side harness) | §8.1's native tests cannot literally run until this exists | Certain (confirmed gap) | Must be resolved as/before P2-T11 — does not block design, only execution |
| 6 | `f.write()`'s return value is never checked anywhere in the current codebase | Short writes / SD-full conditions could silently desync the new offset checkpoint from actual file contents | Low under normal operation, higher near SD-full conditions | P2-T10 |
| 7 | Rollback stranding new-format telemetry rows during the trial window (§4.4) | Bounded, accepted per the ADR's own text — but only reasoned about, never empirically confirmed in this codebase | Low (short trial window) | B8 |

### Low Risk

| # | Cause | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| 8 | Segment file count grows under sustained high backlog (many 10,000-row files) | Cosmetic/operational only, not a correctness risk | Proportional to backlog | Bounded by the tunable `QUEUE_SEGMENT_ROWS` constant |
| 9 | Directory-scan cost at boot for orphan-segment cleanup | Negligible — bounded by the (small) number of segment files present | Low | None needed |
| 10 | CRC collision on a torn row coincidentally passing validation | Pre-existing, statistically negligible, unchanged by this ADR | Negligible | None needed (unchanged from today) |

---

## Summary for the Reviewer

- Section 0 and Risk #1 both surface the same unresolved blocker: **the SD truncate-capability finding Phase 0 was supposed to produce still does not exist.** This report proceeds on an explicit, stated assumption so the design could be written, but Task P2-T0 must close this before implementation starts for real.
- Two genuine open questions were discovered during this design pass that this report deliberately does **not** resolve, per instruction: the corrupted-checkpoint fallback behavior (§4.5/§7#6/Risk#2), and the small `sync.h` scope addition needed for the adaptive-push sub-decision (§3.3) that the Master Plan's file list doesn't name.
- No firmware file was modified. No commit was made. This document itself (`Docs/PHASE2_DESIGN_REPORT.md`) is new and is **left uncommitted**, per the "do not create commits" instruction — it is on disk for review only.

Waiting for approval before writing any Phase 2 code.
