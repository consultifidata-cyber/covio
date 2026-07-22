# SCHEMA_REGISTRY.md — Covio Oil Flow Meter

**Status:** Living document (per `MASTER_GOVERNANCE.md` §4/§12). Authoritative registry of every `(schema_version, record_type)` layout ever published to the SD row format and the wire JSON envelope, per **ADR-001 — Self-Describing, Versioned Telemetry Schema**.

**Governing rule (frozen by ADR-001, do not soften or reinterpret):**
- A device only ever writes/emits the single `schema_version` its currently-running firmware understands. There is no runtime schema negotiation.
- Once published, a `(schema_version, record_type)` layout is never altered. Evolution happens only by introducing a new `schema_version`, never by mutating an already-published one.
- The receiver must accept and correctly interpret a batch containing a mix of exactly the **current** schema_version and the **immediately preceding** one — no more, no fewer — for as long as any fielded device might still be running the prior firmware.
- **Acceptance rule:** Current → accept. Previous → accept. Older than previous → reject (quarantine). Future/unknown → reject (quarantine). No silent fallback, no guessing.

---

## Current State

| Field | Value |
|---|---|
| Current schema_version (firmware) | `1` (`SCHEMA_VERSION_CURRENT` in `queue.h`) |
| Previous schema_version (receiver-tolerated) | **none yet** — a "previous" version only exists once schema_version 2 is published (see ACR-001) |
| Record types defined to date | `1` = `TELEMETRY` |

---

## record_type registry

| record_type value | Name | Introduced | Notes |
|---|---|---|---|
| `1` | `TELEMETRY` | schema_version 1 (Phase 1 / ADR-001) | The only record_type defined as of Phase 1. Future record types (`HEALTH`, `LOG_REQUEST_RESPONSE`, etc.) are introduced in later phases per their own ADRs (ADR-002, ADR-012) without altering this entry. |

---

## schema_version 0 — implicit pre-ADR-001 layout (legacy, retired via forced drain — NOT accepted)

This is **not** a formally versioned layout, and per **ACR-001** it is **not** an accepted "previous" version either — it is simply the fixed, un-versioned format every device wrote before ADR-001 existed. ADR-001's Migration Strategy requires any such device to be fully drained to an empty queue under its old firmware before the OTA introducing this schema is applied — a one-time, one-directional cutover, not a dual-acceptance window. The receiver therefore quarantines any record with no `schema_version` key (or any unregistered value), exactly like any other unsupported version. It is documented here only for historical/byte-layout reference.

### SD row (`QRow`, pre-Phase-1) — 32 bytes, packed

| Offset | Size | Field | Type |
|---|---|---|---|
| 0 | 4 | `magic` | uint32 (`QROW_MAGIC` = 0x51524F57) |
| 4 | 4 | `boot_id` | uint32 |
| 8 | 4 | `seq` | uint32 |
| 12 | 4 | `ts` | uint32 |
| 16 | 8 | `totalizer` | uint64 |
| 24 | 2 | `quality` | uint16 (bitfield) |
| 26 | 2 | `rssi_abs` | uint16 |
| 28 | 4 | `crc32` | uint32 |

### Wire JSON record (pre-Phase-1)

```json
{"boot_id":N,"seq":N,"ts":N,"totalizer":N,"quality":N,"rssi":-N}
```

No `schema_version`/`record_type` field was present at all. Per ACR-001, the receiver does **not** treat a missing `schema_version` key as an accepted version — it is quarantined, consistent with ADR-001's forced-drain migration strategy.

---

## schema_version 1, record_type 1 (TELEMETRY) — current, frozen

**Introduced:** Phase 1 / ADR-001.

### SD row (`QRow`) — 36 bytes, packed (`__attribute__((packed))`, no compiler-inserted padding)

| Offset | Size | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 4 | `magic` | uint32 | `QROW_MAGIC` = `0x51524F57` (`'QROW'`) |
| 4 | 2 | `schema_version` | uint16 | `SCHEMA_VERSION_CURRENT` = `1` |
| 6 | 2 | `record_type` | uint16 | `RECORD_TYPE_TELEMETRY` = `1` |
| 8 | 4 | `boot_id` | uint32 | |
| 12 | 4 | `seq` | uint32 | globally monotonic across boots |
| 16 | 4 | `ts` | uint32 | device uptime seconds |
| 20 | 8 | `totalizer` | uint64 | raw lifetime pulses |
| 28 | 2 | `quality` | uint16 | bitfield: `QUALITY_OK`=0x0000, `QUALITY_BACKLOG_HIGH`=0x0001, `QUALITY_TIME_UNSYNCED`=0x0002 |
| 30 | 2 | `rssi_abs` | uint16 | `\|RSSI\|` |
| 32 | 4 | `crc32` | uint32 | CRC-32 over the whole row with this field zeroed during computation |

**Total size: 36 bytes.**

**Implementation-level choice made in Phase 1 (documented per instruction, not a redesign):** the ADR deliberately leaves exact byte-level wire encoding unspecified ("Final Review" section, item 1). `schema_version` and `record_type` were each sized as `uint16_t` (matching the existing `quality`/`rssi_abs` field width already used in this struct) and placed immediately after `magic` — i.e., genuinely *prefixing* the rest of the record, consistent with the ADR's literal wording ("every record ... is prefixed with a schema_version ... and a record_type"). This is the smallest reasonable choice consistent with ADR-001's intent; it is not an architectural decision and does not require an ACR.

### Wire JSON record (per-record, current)

```json
{
  "schema_version": 1,
  "record_type": 1,
  "boot_id": 12,
  "seq": 4711,
  "ts": 8823,
  "totalizer": 991823,
  "quality": 0,
  "rssi": -61
}
```

`schema_version`/`record_type` are carried **per record**, not once per batch — this is what makes a batch mixing the current and previous schema version (normal and expected during any OTA rollout window per ADR-001) well-formed: the receiver validates and dispatches each record independently.

### Wire JSON batch envelope (unchanged by ADR-001, shown for completeness)

```json
{
  "device_id": "esp32-XXXXXXXXXXXX",
  "fw": "1.0.0",
  "model": "covio-oilflow-v1",
  "kfactor_version": 1,
  "records": [ /* array of per-record objects above */ ]
}
```

No batch-envelope field, cadence, batch size (`PUSH_BATCH_MAX`), idempotency key `(device_id, seq)`, or ack semantics were changed by this phase.

---

## Storage format notes

- The queue remains the existing single unbounded log file (`/queue/log.bin`) plus dual-slot CRC ack pointer (`/queue/ackA.bin` / `/queue/ackB.bin`). **Segmented storage, truncate-before-append, rotation, and the read-cursor redesign are explicitly out of scope for Phase 1 — they belong to Phase 2 / ADR-003.**
- Adding `schema_version`/`record_type` changes `sizeof(QRow)` from 32 to 36 bytes. This is a breaking change to the SD row byte layout, which is exactly why ADR-001's migration strategy requires any pre-existing device to fully drain its queue to empty before receiving the firmware that introduces this schema (see Migration Notes below).

## Receiver behavior notes (`server/server.py`)

- `records` table gains `schema_version` and `record_type` columns (defaulted for any pre-existing rows).
- A new `quarantined_records` table stores, verbatim, any record whose `schema_version` is outside the currently-registered accepted set (`{1}` as of Phase 1 — see ACR-001) or whose `record_type` is not in the known set (`{1}`), or which is otherwise malformed — the raw JSON, device_id, and a reason string are recorded. Quarantined records are never included in `ack_seq` computation and never crash the request; the rest of the batch is still processed normally.
- `CURRENT_SCHEMA_VERSION` / `PREVIOUS_SCHEMA_VERSION` / `ACCEPTED_SCHEMA_VERSIONS` and `RECORD_TYPE_TELEMETRY` / `KNOWN_RECORD_TYPES` are defined as module-level constants at the top of `server.py`, mirroring `queue.h`'s `SCHEMA_VERSION_CURRENT` / `RECORD_TYPE_TELEMETRY` constants exactly — keep these two files' numeric values in sync by hand until a shared schema-constants source exists (no such mechanism is introduced in Phase 1; not in scope).

## Migration Notes (ADR-001, Migration Checklist item 1 in the Master Plan)

- Any device that has ever run pre-ADR-001 (schema_version-less) firmware must be confirmed drained to an empty queue before this schema-introducing OTA is applied to it.
- **As of this Phase 1 implementation, no device has been fielded** — there is no existing bench/pilot fleet to drain. This is recorded here explicitly, per the Master Plan's Migration Checklist instruction, rather than silently skipped.
- Per ADR-003, this schema change is intended to ship as one combined release together with Phase 2's queue-framing redesign. **Phase 2 (ADR-003) is out of scope for this Phase 1 change** — the existing single-file queue, ack model, retry model, batching, and cadence are unchanged here.

## Change Log

| schema_version | record_type | Phase / ADR | Date | Change |
|---|---|---|---|---|
| 0 (implicit) | (none — no discriminator existed) | pre-ADR-001 | — | Original fixed, un-versioned `QRow`/JSON layout. |
| 1 | 1 (`TELEMETRY`) | Phase 1 / ADR-001 | 2026-07-08 | Added `schema_version`/`record_type` discriminator fields to both the SD row and the wire JSON; published this registry; implemented the current+previous receiver acceptance rule. |
| 1 | 1 (`TELEMETRY`) | Phase 1 / ADR-001 (ACR-001 correction) | 2026-07-08 | Corrected the receiver to stop treating `schema_version = 0` / missing `schema_version` as an accepted "previous" version — no registered previous version exists until schema_version 2 is published. Missing/unregistered versions are now quarantined, per ADR-001's forced-drain migration strategy. See `Docs/ACR-001-schema-version-zero-acceptance-window.md`. |
