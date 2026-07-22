# 02 — End-to-End Data Flow (Phase 1)

Traced by direct code reading of `covio_firmware.ino`, `totalizer.h`, `queue.h`, `telemetry.h`, `sync.h`, `server/server.py`, `local_api.h`. Every step below cites the exact function and file:line. "Proof basis" states whether the step is proven by code inspection only, or additionally by a real runtime/test observation this session.

| # | Step | File:Function | Storage | Atomicity boundary | Failure behavior | Retry | Duplicate handling | Proof basis |
|---|---|---|---|---|---|---|---|---|
| 1 | Pulse enters GPIO1 | `totalizer.h:143` `setupPCNT_()` | Hardware PCNT register (volatile SRAM inside SoC) | N/A — hardware counts independent of CPU/WiFi load | Glitch filter (`PCNT_GLITCH_NS`) rejects sub-1µs noise | N/A | N/A | Code + real device confirmed counting (`totalizer_raw_pulses: 280` in live status) |
| 2 | PCNT increments | `totalizer.h:106` `total()` | Hardware register, drained into `accumulated_` (RAM) before 16-bit wrap | None needed — monotonic hardware counter | 16-bit register wraps at 65535; drained at 30000 (`totalizer.h:117`) | N/A | N/A | Code-only |
| 3 | Litres conversion (K-factor) | **Not on-device.** `telemetry.h`'s header comment: device sends raw pulses only; `litres = (totalizer_now - totalizer_prev) / K_factor` is computed **server-side** (`server.py:919-921` dashboard, and implicitly per-record via `kfactor_version`) | Server: `calibration` table (current K only) | N/A | N/A | N/A | N/A | Code-only. **Caveat found:** the server's own consumption dashboard (`server.py:944-951`) applies the *current* global `k_factor` to the *entire* raw-pulse range `(hi-lo)`, not a per-record historical K derived from each row's stored `kfactor_version`. A K-factor change therefore **silently recalculates all historical consumption** in that view, even though each record's own `kfactor_version` is stored and available — see `09_CONFIGURATION_AND_DEVICE_MANAGER_AUDIT.md`. |
| 4 | Sequence/idempotency identity assigned | `covio_firmware.ino:189` (`seq++`), combined with `store.h:31` (`bootId_`) | RAM (`seq` global) + NVS (`boot_id`, incremented once per boot) | `seq` is globally monotonic across reboots (never resets); `boot_id` increments every power-up | N/A | N/A | Identity = `(device_id, seq)` — `boot_id` is carried for diagnostics only, **not** part of the DB's uniqueness constraint (see row 12) | Code confirmed; real device `boot_id` observed incrementing 17→18 across a real reset |
| 5 | Sample/reading object built | `telemetry.h:30` `Telemetry::build()` | RAM (`QRow` struct, 36 bytes packed) | N/A | N/A | N/A | N/A | Code-only |
| 6 | Reading written durably | `queue.h:119` `EventQueue::append()` | LittleFS, `/queue/seg_NNNNNN.bin`, `FILE_APPEND` + `f.flush()` + `f.close()` before returning | Row written and flushed to flash **before** the totalizer checkpoint is updated (`covio_firmware.ino:192-193`, explicit comment on why this order matters) | Torn/short write detected on next read via CRC32 mismatch, silently skipped (not silently accepted as valid) | The unconfirmed tail is truncated by the *next* `append()`'s truncate-before-append logic (`queue.h:143-176`) | If power dies between row-write and checkpoint-write, the same `seq` regenerates next boot and the resulting duplicate row is absorbed by the server's `PRIMARY KEY (device_id, seq)` | Code confirmed; real device's serial log showed `[TOT] recovered total=... writes=...` and `[Q] acked_seq=...` correctly recovered across a real reset |
| 7 | Queue write-offset checkpoint updated | `totalizer.h:134` `setQueueOffset()`, called from `queue.h:209` **after** the row is durably flushed | LittleFS, dual-slot `/totA.bin`/`/totB.bin`, CRC32, ping-pong | Persisted only after step 6 completes | Torn checkpoint write fails CRC on the next boot; the other slot (previous good value) is used instead | N/A | N/A | Code-only for the torn-write case; real device confirmed dual-slot recovery of `total`/`writes` across a real reset |
| 8 | Sync worker selects a batch | `sync.h:94` `pushOnce()`, calls `queue.h:235` `pending()` | Reads from LittleFS starting at the persisted ack cursor | Read-only; does **not** mutate the cursor (`queue.h`'s own "single most important correctness property" comment, line 322-324) | Returns 0 rows if none pending; caller no-ops | N/A | N/A | Code confirmed; real device status showed a real batch of 8 records synced (`acked_seq` advanced by 8 in the serial log) |
| 9 | Request payload serialized | `telemetry.h:59` `Telemetry::toJson()` | RAM (String) | N/A | N/A | N/A | N/A | Code-only |
| 10 | HTTPS/HTTP request sent | `sync.h:106-118` | N/A (network) | 8-second `setTimeout` bounds the call | Non-200 → queue kept, retried later (`sync.h:124-128`) | Every `PUSH_PERIOD_MS` (5s) | N/A | Code confirmed; real device: `last_push_http_code: 200` |
| 11 | Server authenticates the device | `server.py:104` `require_api_key()`, called at the top of `push()` **before any body parsing** | SQLite `devices.api_key_hash` (SHA-256, never plaintext) | Auth check has zero side effect on failure — no row written, no ack computed | Missing/invalid/revoked key → `401`, and an `API_AUTH_FAILED` event is recorded | N/A | N/A | Code confirmed; 6/6 real passing tests in `test_dm_phase_0b_auth.py` (missing/wrong/revoked/valid key cases) |
| 12 | Server validates and persists the record | `server.py:531-579` `push()`'s per-record loop | SQLite `records` table, `PRIMARY KEY (device_id, seq)` — a **real DB-level uniqueness constraint**, `INSERT OR IGNORE` | Per-record: a malformed/unsupported-schema record is quarantined into a separate table and does **not** abort the rest of the batch | Schema/record-type/missing-field failures → `quarantined_records`, never a crash, never silently dropped | N/A (device will keep resending an unacked row every cycle) | Genuine duplicate `(device_id, seq)` → `INSERT OR IGNORE` no-ops it, DB-enforced, race-safe | Code confirmed; real schema constraint read directly from `server.py:161`; 46/46 real passing native tests including schema-acceptance and registry tests |
| 13 | Server returns an acknowledgement | `server.py:614-629` — highest **contiguous** `seq` for that `device_id`, computed via a full `SELECT DISTINCT seq ... ORDER BY seq` scan every push | JSON response `{"ack_seq":...,"server_time_ms":...}` | Computed after the same transaction commits (`c.commit()` at line 612, scan at line 618+) | If a record is quarantined, the contiguous-scan **permanently stops** at that seq (see finding below) | N/A | N/A | Code confirmed — **and this is where a real, code-proven defect was found**, see callout below |
| 14 | Firmware validates the acknowledgement | `sync.h:132-137`, naive string-scan `extractLong_()` | RAM | A bare HTTP 200 with no/garbled `ack_seq` is explicitly **not** treated as an ack — queue is kept (`sync.h`'s own "Invariant 6" comment, re-verified against the actual current line numbers in this audit, not just a paraphrase from an earlier report) | Missing/negative-parsed `ack_seq` → logged, queue untouched, retried next cycle | Every `PUSH_PERIOD_MS` | N/A | Code confirmed |
| 15 | Local acknowledgement cursor persisted | `queue.h:329-415` `ackThrough()` | LittleFS, dual-slot `/queue/ackA.bin`/`ackB.bin`, CRC32 | **Cursor is persisted before any segment file is deleted** (`queue.h:409-414`, explicit comment on why this ordering is crash-safe) | A crash between cursor-persist and segment-delete just re-recognizes and re-deletes the same already-acked segment on the next successful ack — never double-deletes unacked data | N/A | N/A | Code-only for the crash-mid-prune case (no such crash was induced this session) |
| 16 | Eligible local records pruned | `queue.h:411-414`, `LittleFS.remove()` per fully-consumed segment | LittleFS | Only segments strictly below the new cursor's segment are deleted; the active segment is never deleted | N/A | N/A | N/A | Code-only |
| 17 | Coviu Device Manager exposes reading + health | `tools/device-manager` (Electron app) reads the device's `/api/v1/info`/`/status`/`/health`/`/metrics` over LAN via `multicast-dns` discovery + HTTP GET | N/A (client-side display only) | N/A | Network failure → `ok:false`, never throws (confirmed by 44/44 real passing unit tests) | N/A | N/A | Code confirmed + 44/44 real passing tests this session |

## Code-proven defect found while tracing step 13

**A single permanently-quarantined record can freeze the device's queue-pruning forever, even while the server keeps successfully storing every record after it.**

`server.py`'s cumulative-ack computation (`push()`, lines 618-627) walks `SELECT DISTINCT seq ... ORDER BY seq` and breaks at the first gap:
```python
for row in rows:
    if row["seq"] == contig + 1:
        contig = row["seq"]
    elif row["seq"] > contig + 1:
        break
```
A record that fails the ADR-001 schema/record-type check, or is missing a required field, is written to `quarantined_records`, **not** `records` (lines 547-569) — so its `seq` value is a permanent gap in the `records` table for that device. Every subsequent push, the device's queue (`queue.h::pending()`) will keep resending that same unacked row first (FIFO order from the persisted cursor), the server will quarantine it again, insert everything after it successfully, but `ack_seq` returned to the device will **still be stuck at `gap_seq - 1`**, forever — because the contiguous-scan cannot skip over a permanent gap. The device's local queue for that boot's data past the gap will never be pruned, growing until `QUEUE_HIGHWATER`/physical flash limits are reached, even though the server has genuinely and durably stored all of that data. This is a real, code-provable defect (not requiring hardware to demonstrate — it follows directly from the two functions' logic), not a hypothetical. See `11_RISK_REGISTER.md` (RISK-01).

## Sequence diagram

```mermaid
sequenceDiagram
    participant Sensor
    participant PCNT as PCNT (hardware)
    participant Loop as covio_firmware.ino loop()
    participant Queue as EventQueue (LittleFS)
    participant Tot as Totalizer checkpoint (LittleFS)
    participant Sync as Sync::pushOnce()
    participant Server as server.py push()
    participant DB as SQLite records/quarantine

    Sensor->>PCNT: rising edge pulses
    loop every 1s (TELEMETRY_PERIOD_MS)
        Loop->>Tot: total() (base_+accumulated_+live PCNT)
        Loop->>Queue: append(row)  [1: durable write FIRST]
        Queue-->>Queue: flush+close before returning
        Loop->>Tot: service(seq)   [2: checkpoint AFTER]
    end
    loop every 5s (PUSH_PERIOD_MS)
        Sync->>Queue: pending(batch, 50)  [read-only, cursor unchanged]
        Sync->>Server: POST /api/iot/flow/push (X-Api-Key)
        Server->>Server: require_api_key() [reject before any parsing]
        loop each record
            Server->>DB: schema/record_type/field check
            alt valid
                Server->>DB: INSERT OR IGNORE records (device_id,seq) UNIQUE
            else invalid
                Server->>DB: INSERT quarantined_records (never blocks batch)
            end
        end
        Server->>Server: compute ack_seq = highest CONTIGUOUS seq
        Server-->>Sync: {ack_seq, server_time_ms}
        alt ack_seq parsed and valid
            Sync->>Queue: ackThrough(ack_seq)  [persist cursor, THEN delete segments]
        else bare 200 / malformed body
            Sync-->>Sync: keep queue, retry next cycle (Invariant 6)
        end
    end
```
