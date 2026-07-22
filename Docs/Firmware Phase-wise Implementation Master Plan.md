# Firmware Phase-wise Implementation Master Plan — Covio Oil Flow Meter

**Status:** Execution document. Governs *how* and *in what order* the ADR constitution is implemented. It does not alter, reinterpret, or re-derive any decision from `Firmware Detailed Architecture Decision Record (ADR).md` — every ADR-nnn reference below is a direct, unmodified citation of that document.
**Inputs (frozen, read in full before using this plan):** `Firmware Architecture Audit.md`, `Firmware Architecture Freeze & Remediation Blueprint.md`, `Firmware Detailed Architecture Decision Record (ADR).md`.
**Purpose:** make it impossible for a new implementing agent or engineer to (a) do work out of order, (b) duplicate work already assigned to another phase, (c) silently redesign a frozen decision, or (d) mark something "done" without the evidence this plan requires.

**Standing rule for every phase and every ADR card below:** if, during implementation, a frozen ADR decision appears impractical (an API doesn't exist, a library doesn't support an assumed capability, a number is wrong for the real hardware) — **stop and raise an Architecture Change Request (ACR) against the specific ADR-nnn. Do not substitute an alternate mechanism, do not "just make it work a different way," and do not proceed past the blocked step.** An ACR is a short, separate document: which ADR, which specific sentence is impractical, why, and the narrowest possible fix. It requires the same review rigor as the original ADR before implementation resumes. Two concrete risks already anticipated in this plan are flagged inline in Phase 0 and Phase 2 (SD truncate-API availability) and Phase 5 (factory Secure Boot tooling gap) — implementers should check these first, before finding out the hard way.

**Governing process document:** `MASTER_GOVERNANCE.md` (repository root) sets the standing rules for how *any* work on this repository is done — coding standards, naming conventions, commit/PR policy, the ACR workflow's exact template, and review/release/deployment checklists. This Master Plan governs *sequencing*; `MASTER_GOVERNANCE.md` governs *conduct*. Neither overrides the other, and neither overrides the ADR document, which alone governs *technical decisions*.

---

## PROJECT STATUS
*(Update this block at the end of every phase and at every review. This is the single source of truth for "where does the project currently stand" — a new agent reads this block before reading anything else below it.)*

| Field | Value |
|---|---|
| Current Phase | Phase 0 |
| Completed Phases | None |
| Blocked Phases | None |
| Open ACRs | None |
| Current Branch | *(repository is not yet under git version control — initialize per `MASTER_GOVERNANCE.md` §5 before Phase 0 work begins)* |
| Current ADR Version | v1.1 — `ADR-017`/`ADR-018` added (DM-Phase 1.8, Covio Device Manager Live Readiness Plan); both **`APPROVED` 2026-07-09** — see the ADR document's "Post-Freeze Supersession" section. DM-Phase 2 implementation authorized. |
| Current Schema Version | 1 |
| Last Reviewed | 2026-07-09 |
| Architecture Owner | _____________ |
| Implementation Owner | _____________ |

---

# PART 1 — ADR Implementation Cards (ADR-001 → ADR-016)

Each card gives the 11 required execution facts for its ADR. It does not repeat the ADR's Problem/Reasoning/Alternatives/Trade-offs — those remain solely in the ADR document. Read the cited ADR before touching its card.

## ADR-001 — Versioned Record Framing for All Device Data

1. **Implementation phase:** Phase 1.
2. **Exact objective:** Add `schema_version` and `record_type` discriminator fields to both the SD-persisted row and the wire JSON envelope; publish the canonical Schema Registry document; implement the "current + previous version, no more, no fewer" acceptance rule in the reference receiver.
3. **Files likely affected:** `queue.h` (row structure), `telemetry.h` (record construction and JSON serialization), `sync.h` (reads the envelope during push/ack), `server/server.py` (accepts and dispatches on schema_version/record_type), new `SCHEMA_REGISTRY.md`.
4. **Must NOT be changed:** the `(device_id, seq)` idempotency/uniqueness model; cumulative-ack semantics; the 1-second telemetry cadence; `PUSH_BATCH_MAX`/`PUSH_PERIOD_MS`; do not introduce a second wire format (protobuf/CBOR/etc.) — ADR-001 already rejected this.
5. **Backward compatibility impact:** Protocol change + storage migration — the first change to the SD row byte layout.
6. **Migration requirement:** any pre-existing device must fully drain its SD queue to empty under current firmware before the OTA introducing this schema; delivered as **one combined release together with Phase 2 (ADR-003)** — see Migration Checklist item 1. If no device has been fielded yet, this requirement is moot and should be recorded as such, not silently skipped.
7. **Test requirement:** native-host tests (harness stood up in Phase 0) covering: reading a batch containing only the current version; reading a batch mixing current and previous versions; correctly quarantining (never crashing on) an unrecognized version.
8. **Definition of Done:** fields present in both the SD row and the wire JSON; the reference receiver demonstrated accepting a mixed current+previous batch in a bench test; `SCHEMA_REGISTRY.md` exists and lists every `(schema_version, record_type)` layout published to date.
9. **Rollback safety:** because the migration precondition forces an empty queue before this OTA, a health-confirm failure and automatic rollback afterward has no old-format data at risk; any new-format rows written in the brief pre-rollback window are lost on rollback, a bounded and already-accepted property of the existing rollback window.
10. **Manual bench verification steps:** (a) on pre-ADR firmware, drain the queue to empty and confirm `acked_seq` equals total sent; (b) OTA to Phase-1 firmware and confirm `show` reports the new schema version; (c) using a scripted test client against the reference receiver, submit one batch containing both a current-version and a previous-version record and confirm both are accepted without error.
11. **Evidence required before "complete":** bench transcript of steps (a)–(c); `SCHEMA_REGISTRY.md` reviewed and merged; native test suite green for this ADR's cases.

## ADR-002 — Unified Health/Heartbeat Record

1. **Implementation phase:** Phase 3.
2. **Exact objective:** Define and ship `record_type = HEALTH` under ADR-001's schema, generated every 60 seconds on its own timer, flowing through the existing durable queue/push/ack pipeline exactly as telemetry does. **Fields shipped with real values in Phase 3:** `uptime`, `free_heap`, `queue_depth`, `rssi`, `last_ack_seq`, `ota_state` (exposes the already-existing `pendingVerify_`/`confirmed_` state from `ota.h`), `sd_status` as a simple OK/FAIL binary, `sd_free_bytes`. **Fields shipped as safe placeholders in Phase 3, filled with real logic by their owning later phase:** `reset_reason` (placeholder `UNKNOWN`; real value in Phase 4/ADR-006), `watchdog_reset_count` (placeholder `0`; real value in Phase 4/ADR-006), `sd_status`'s full four-state form (`OK`/`ABSENT`/`WRITE_FAIL`/`LOW_SPACE`; real state machine in Phase 4/ADR-004), `sensor_plausible` (placeholder `true`; real logic in Phase 8/ADR-015).
3. **Files likely affected:** `queue.h` (new record_type layout), `telemetry.h` (health-record builder), `covio_firmware.ino` (new 60-second timer), `sync.h` (no change beyond already handling any record_type generically), `server/server.py` (store and surface health fields per device).
4. **Must NOT be changed:** the schema versioning rule from ADR-001; the queue's ack/retry semantics — health records are not a special case for durability purposes; must NOT include `api_key` or `wifi_pass` in the payload under any circumstance.
5. **Backward compatibility impact:** Protocol change, additive, per ADR-001's tolerance rule — no breaking change to existing consumers.
6. **Migration requirement:** none — purely additive once ADR-001's schema exists.
7. **Test requirement:** native-host test confirming the health record serializes and deserializes correctly with all fields, including placeholder defaults; a bench test confirming the 60-second cadence is independent of the 1-second telemetry cadence.
8. **Definition of Done (Phase 3 scope only — see Phase 4 and Phase 8 for remaining fields):** HEALTH record type documented in `SCHEMA_REGISTRY.md`; firmware durably emits one every 60 seconds; the reference receiver stores and displays every field, including placeholders, on a per-device view; the record explicitly does not carry secrets (verified by a bench packet inspection).
9. **Rollback safety:** fully additive; a rollback to pre-ADR-002 firmware simply stops emitting HEALTH records with no data-format conflict.
10. **Manual bench verification steps:** (a) flash Phase-3 firmware and confirm one HEALTH record appears on the receiver every 60 seconds, distinct from telemetry rows; (b) inspect a captured HEALTH payload (e.g., via a packet capture or receiver-side raw log) and confirm no `api_key`/`wifi_pass` substring appears; (c) confirm placeholder fields read exactly `UNKNOWN` / `0` / `true` as specified, not garbage or uninitialized memory.
11. **Evidence required before "complete":** bench transcript of (a)–(c); a raw captured HEALTH payload attached to the phase record as evidence of the no-secrets check.

## ADR-003 — Truncate-Before-Append Framing with Segmented Rotation

1. **Implementation phase:** Phase 2.
2. **Exact objective:** Implement (a) truncate-to-last-known-good-offset before every SD append, using a persisted offset in the same dual-slot CRC checkpoint pattern already used for the totalizer; (b) fixed 10,000-row segment files replacing the single unbounded log, with per-segment deletion once fully acknowledged; (c) a persisted "first unacknowledged position" (segment + offset) updated only on a confirmed server ack, never on a mere read attempt; (d) removal of the fixed post-push wait for full batches, allowing up to ten consecutive push attempts per loop pass during catch-up.
3. **Files likely affected:** `queue.h` (row framing, segment file management, ack-position persistence), `totalizer.h` (checkpoint structure extended with the persisted write-offset field, reusing its existing dual-slot CRC mechanism), `covio_firmware.ino` (adaptive push-loop change).
4. **Must NOT be changed:** the ack-gated pruning invariant (nothing is ever deleted before being acknowledged); the totalizer's own existing checkpoint cadence and recovery logic beyond the one added field; `PUSH_BATCH_MAX` per individual push request.
5. **Backward compatibility impact:** Storage migration — changes the on-disk queue layout.
6. **Migration requirement:** identical forced full-drain precondition as ADR-001, delivered as the same combined release (see Migration Checklist item 1).
7. **Test requirement:** native-host tests for the truncate-before-append logic (simulated torn-write scenarios), the cumulative-ack contiguous-sequence algorithm, and the persisted-cursor-only-advances-on-confirmed-ack property (a test that attempts a failed push and asserts the cursor did not move).
8. **Definition of Done:** a power-cut-during-append bench test confirms no more than the single most-recent unconfirmed row is ever lost; a simulated multi-day-outage bench test confirms drain time is bounded by network throughput, not rescan overhead; a bench soak test confirms correct segment creation and deletion across a full fill-drain cycle.
9. **Rollback safety:** see the phase-level note in Phase 2 below — rollback after a successful drain-then-upgrade has no conflicting old-format data, since the queue was empty at the moment of upgrade; only rows written in the narrow pre-health-confirm window are at risk, a bounded and accepted property.
10. **Manual bench verification steps:** (a) run the sim-pulse bench setup, let the queue accumulate rows, then physically cut power mid-append (yank power at a moment timed to land inside a write) and confirm on reboot that exactly the last in-flight row (at most) is missing and every prior row is intact and correctly ack'd; (b) simulate a multi-day outage (disconnect WiFi for an extended bench period, or synthetically inflate the queue) and time the drain — confirm it completes in a time bounded by network throughput, with no visible rescanning delay; (c) confirm segment files appear, fill to the 10,000-row cap, and are deleted once fully acknowledged.
11. **Evidence required before "complete":** bench transcript and timing data for (a)–(c); the power-cut test repeated at least three times at different write-timing offsets to build confidence the truncate logic behaves correctly regardless of exactly where in the write the power was lost.

> **PRACTICALITY FLAG — verify before implementing:** ADR-003's truncate-before-append mechanism requires the SD/SdFat library used by this project to support truncating a file to a smaller size. This must be verified against the exact pinned library version (Phase 0) before any code is written. **If a truncate-to-offset capability is not available, implementation must stop and an Architecture Change Request must be raised against ADR-003 — do not substitute a different resync mechanism unilaterally.** ADR-003 explicitly rejected byte-scanning resync as an alternative specifically for its non-determinism; that reasoning does not disappear just because truncate turns out to be inconvenient to implement.

## ADR-004 — SD Degradation State Machine

1. **Implementation phase:** Phase 4.
2. **Exact objective:** Replace the boot-time infinite halt on SD failure with an indefinite 30-second retry, running WiFi/sync/OTA regardless of SD state; implement the runtime `SD_DEGRADED` state (write attempts stop, best-effort non-durable HEALTH reporting continues, automatic recovery on card restoration); implement the full four-state `sd_status` enum (`OK`/`ABSENT`/`WRITE_FAIL`/`LOW_SPACE`) and the 5%-or-50MB (whichever larger) low-space threshold check, populating the field ADR-002/Phase 3 already reserved.
3. **Files likely affected:** `covio_firmware.ino` (boot sequence — remove the halt loop), `queue.h`/`totalizer.h` (write-failure detection hooks), `telemetry.h` (health-record `sd_status`/`sd_free_bytes` real values), `sync.h` (best-effort non-durable transmission path for the SD-absent case).
4. **Must NOT be changed:** the fail-loud philosophy itself is not reversed into silent failure — SD problems must still be visible, just not fatal; the existing SD-present happy-path boot sequence and checkpoint recovery logic.
5. **Backward compatibility impact:** none — additive runtime behavior only.
6. **Migration requirement:** none.
7. **Test requirement:** native-host test for the low-space threshold arithmetic; bench tests (below) for the state transitions, which cannot be fully simulated on a host.
8. **Definition of Done:** a bench test pulling the SD card mid-run confirms continued HEALTH reporting with `sd_status = WRITE_FAIL`; a bench test booting with no SD card confirms WiFi still connects and `sd_status = ABSENT` is reported; a low-space bench test (using a small or nearly-full card) confirms the threshold fires correctly; the boot-time halt loop no longer exists in the codebase.
9. **Rollback safety:** additive; a rollback to prior firmware restores the old halt-on-SD-failure behavior, which is safe (if regressive) rather than dangerous.
10. **Manual bench verification steps:** (a) boot with no SD card inserted and confirm the device still associates to WiFi and a HEALTH record with `sd_status=ABSENT` arrives at the receiver; (b) after a normal boot, physically remove the SD card mid-run and confirm the device transitions to `WRITE_FAIL`, keeps running, and recovers automatically when the card is reinserted; (c) using a card provisioned near its capacity (or a small card), confirm `LOW_SPACE` fires at the specified threshold and clears once space is freed.
11. **Evidence required before "complete":** bench transcripts for (a)–(c), including receiver-side screenshots/logs showing the correct `sd_status` transitions over time.

## ADR-005 — TLS, Secure Boot, and Per-Device Key Provisioning

1. **Implementation phase:** Phase 5.
2. **Exact objective:** Move all four API calls to HTTPS with a pinned CA; enable ESP-IDF Secure Boot V2 and Flash Encryption via factory-time eFuse burning with RSA-3072 signing keys; establish factory-time unique-API-key generation and NVS injection (the injection mechanism itself belongs to Phase 6/ADR-008; this phase delivers the cryptographic and transport capability that mechanism relies on).
3. **Files likely affected:** `sync.h` (all HTTP calls become `WiFiClientSecure` with `setCACert`), `ota.h` (HTTPUpdate moves to a secure client with the pinned CA), `config.h` (embed the pinned CA certificate constant; retain `DEFAULT_API_KEY` explicitly labeled as bench-only), factory-side signing/eFuse tooling (outside the `.ino`/`.h` set — this is a build/release-process artifact, tracked but not part of runtime firmware source).
4. **Must NOT be changed:** the four API paths/route names themselves; the existing `X-Api-Key` header mechanism (this phase changes the transport under it, not the header itself); do not implement mutual TLS (client certificates) — explicitly rejected in ADR-005.
5. **Backward compatibility impact:** major migration for the transport layer; Secure Boot/Flash Encryption are permanently non-retrofittable to already-manufactured devices.
6. **Migration requirement:** any already-deployed pre-hardening device receives exactly one final plain-HTTP OTA that switches it to HTTPS with the pinned CA and updates `server_url` to the `https://` form — the last plain-HTTP update it will ever receive; any such device without a unique API key gets one via the console as a manual, per-device operation. Secure Boot/Flash Encryption cannot be retrofitted at all; devices without them remain bench-only forever. See Migration Checklist item 2.
7. **Test requirement:** a bench MITM test (an intercepting proxy presenting an untrusted certificate) must be verified rejected; a bench test with a correctly signed OTA image must succeed; a bench test with a deliberately mis-signed image must be verified rejected by the bootloader.
8. **Definition of Done:** all four API calls verified over HTTPS against a real (non-bypassed) certificate; the MITM rejection test passes; Secure Boot and Flash Encryption verified enabled via an eFuse summary on a factory-flashed unit; a mis-signed OTA image is verified rejected; no unit leaves the factory floor with the default API key active.
9. **Rollback safety:** **Secure Boot/Flash Encryption fuse burns are one-way and have no rollback path whatsoever — this is a hardware fact, not a software choice, and must be double- and triple-checked on the very first factory-flashed unit before it is used as a template for volume production.** The HTTPS/pinned-CA transport change, separately, has a serious operational rollback risk: **the device's own OTA path depends on the same TLS+pinned-CA connectivity it is protecting — if the pinned CA is ever wrong, expired, or misconfigured server-side, the device cannot receive any further OTA update to fix itself, and requires physical USB re-flash to recover.** This must be treated as a hard stop condition (see Phase 5 stop conditions below), not a routine risk.
10. **Manual bench verification steps:** (a) confirm all four endpoints succeed over HTTPS against the real pinned CA; (b) run the intercepting-proxy MITM test and confirm the connection is refused, with the failure visible in Serial/health reporting, not a silent hang; (c) attempt an OTA with a binary signed by the wrong key and confirm the bootloader rejects it and the device continues running its current image; (d) run `espefuse.py summary` (or the equivalent tool for the actual toolchain used) on a freshly factory-flashed unit and confirm Secure Boot and Flash Encryption are both reported enabled.
11. **Evidence required before "complete":** transcripts/logs for (a)–(d); the eFuse summary output archived as a permanent manufacturing-QA record for the first production unit and spot-checked periodically thereafter.

> **PRACTICALITY FLAG — verify before implementing:** the project's documented build process today is the plain Arduino IDE "Upload" button. Enabling Secure Boot V2 and Flash Encryption typically requires `idf.py`/`espsecure.py`/`parttool.py`-level tooling not exposed by the stock Arduino IDE flow. Phase 0/Phase 5 must confirm the exact factory-flashing toolchain (Arduino IDE with manual esptool steps, or a switch to PlatformIO/ESP-IDF for this one step) **before** attempting the first Secure-Boot-enabled factory flash. This is a tooling-capability gap, not a disagreement with ADR-005's decision — it does not require an ACR, but it is a hard precondition that must be resolved in Phase 0, not discovered mid-Phase-5.

## ADR-006 — Application Watchdog with Boot-Time Reset-Reason Reporting

1. **Implementation phase:** Phase 4.
2. **Exact objective:** Enable the ESP-IDF Task Watchdog Timer on the main loop task at a 15-second timeout, fed once per loop iteration immediately after the console service call; ensure the OTA download loop explicitly feeds the watchdog during its own operation; increment a persisted `watchdog_reset_count` on the boot immediately following a detected watchdog reset (via `esp_reset_reason()`), never inside a panic handler; verify and document that the hardware brownout detector is enabled at its default threshold in the release build; populate the `reset_reason`/`watchdog_reset_count` fields ADR-002/Phase 3 reserved.
3. **Files likely affected:** `covio_firmware.ino` (watchdog registration and feed call), `ota.h` (watchdog feed during download), `store.h` (persisted `watchdog_reset_count`), `telemetry.h` (health-record real values for these fields).
4. **Must NOT be changed:** the existing 8-second HTTP push timeout, or any other existing timeout value that is already comfortably under the new 15-second watchdog window; do not implement the panic-handler-based counter-increment approach — ADR-006 explicitly rejected it in favor of boot-time detection.
5. **Backward compatibility impact:** none — additive runtime behavior.
6. **Migration requirement:** none.
7. **Test requirement:** a bench test with a deliberately hung main loop (a test-only build with an injected infinite blocking call) confirming a watchdog reset within 15 seconds; a bench test confirming the watchdog does not spuriously trigger during a large OTA download over a throttled connection.
8. **Definition of Done:** both bench tests above pass; `reset_reason` and `watchdog_reset_count` report real values (not the Phase-3 placeholders) in the HEALTH record; the release build's sdkconfig is confirmed to have the brownout detector enabled at default threshold, documented in `VERSIONS.md` (Phase 9) or an equivalent build-configuration record.
9. **Rollback safety:** additive; a rollback to prior firmware simply loses watchdog protection, a regression in robustness but not a new hazard.
10. **Manual bench verification steps:** (a) flash a test build with a deliberate infinite loop inserted only for this test and confirm the device resets within 15 seconds and reports `reset_reason = WATCHDOG` on the next HEALTH record; (b) run a full OTA update over an artificially slow/throttled network connection and confirm no spurious watchdog reset occurs during the download; (c) inspect the build's brownout configuration and confirm it is enabled at the ESP-IDF default.
11. **Evidence required before "complete":** bench transcripts for (a)–(c); the test-only hung-loop build must never be merged into the mainline firmware — this is flagged explicitly as a stop condition for this ADR's test procedure.

## ADR-007 — Versioned NVS Schema with Bounded Change History

1. **Implementation phase:** Phase 6.
2. **Exact objective:** Add a `config_schema_version` NVS key, separate from the calibration-only version; implement the boot-time migration-step-runner comparing persisted vs. compiled-in version; implement the bounded (last 20 entries) on-device configuration-change history and the new `history` console command; explicitly document and preserve the "never reset `seq` on `server_url` change" rule.
3. **Files likely affected:** `store.h` (new schema-version key, migration-runner logic, change-history ring buffer), `provision.h` (new `history` command).
4. **Must NOT be changed:** the existing `seeded` first-boot guard mechanism; the calibration-only `cfg_ver` field (unrelated to this new, broader schema version); the global-monotonic `seq` counter must never be reset by any configuration change, under any circumstance.
5. **Backward compatibility impact:** configuration migration.
6. **Migration requirement:** existing pre-ADR devices are implicitly at schema version 0; the first OTA including this ADR performs a no-op 0→1 migration and stamps version 1.
7. **Test requirement:** native-host test for the migration-step-runner logic (given a version 0 state, confirm it reaches version 1 correctly and idempotently if re-run); a bench test for the `history` command across a reboot.
8. **Definition of Done:** a bench test upgrading a device from a pre-ADR NVS state confirms correct migration to version 1 with no data loss; the `history` command is verified to survive a reboot; a re-pointing bench test (change `server_url` to a different, previously unaware receiver with a non-empty queue) confirms successful delivery with no `seq` collision.
9. **Rollback safety:** a rollback to pre-ADR firmware after a migration has already run is safe — the old firmware simply ignores the new `config_schema_version` key it doesn't recognize, per the existing `isKey()`-style tolerance already present in `Store`.
10. **Manual bench verification steps:** (a) start from a pre-ADR NVS state (or simulate one), OTA to this firmware, and confirm `show`/console output reports schema version 1; (b) run `set url`, `set key`, `set wifi` several times and confirm `history` reports the last N changes correctly across an intervening reboot; (c) with a non-empty queue, run `set url` to point at a second, independent bench receiver instance and confirm the backlog delivers there without a `seq` collision against that receiver's own records.
11. **Evidence required before "complete":** bench transcripts for (a)–(c).

## ADR-008 — Factory Self-Test Firmware and Console-Based Field Verification

1. **Implementation phase:** Phase 6.
2. **Exact objective:** Build a `FACTORY_TEST_MODE` compile-time firmware variant that automatically runs an SD write-read-CRC check, a WiFi-association check against a factory test network, and a pulse-counter response check (via the existing simulated-pulse jumper pattern), reporting a single-line machine-parseable PASS/FAIL over Serial; add a new mainline `verify` console command performing a WiFi-connectivity check, a config-poll round-trip check, and a two-sample (5 seconds apart) live pulse-count printout.
3. **Files likely affected:** new `factory_test.h` (compiled only under `FACTORY_TEST_MODE=1`, never included in a production build), `covio_firmware.ino` (conditional factory-test entry point), `provision.h` (new `verify` command, mainline).
4. **Must NOT be changed:** `factory_test.h`'s self-test sequence must never be reachable in a production (non-`FACTORY_TEST_MODE`) build — this is a hard build-configuration boundary, not a runtime flag; the existing `SIM_PULSES`/jumper mechanism is reused as-is, not redesigned, for the factory pulse check.
5. **Backward compatibility impact:** none — this is a manufacturing/installation process addition, not a change to already-deployed devices.
6. **Migration requirement:** not applicable.
7. **Test requirement:** a bench test confirming the factory-test build correctly reports PASS on a known-good unit and FAIL when the SD card or sensor signal is deliberately disconnected; a bench test for the mainline `verify` command's three checks.
8. **Definition of Done:** the factory-test firmware build exists as a clearly separate build target/configuration from production firmware; it is demonstrated PASS/FAIL-correct on the bench; `verify` is implemented and demonstrated in a bench commissioning rehearsal; a documented factory-floor SOP and field-commissioning SOP both exist, referencing these exact commands.
9. **Rollback safety:** not applicable to the factory-test build (never shipped to the field). The mainline `verify` command addition is purely additive to the console and carries no rollback risk.
10. **Manual bench verification steps:** (a) build and flash the `FACTORY_TEST_MODE` variant on a known-good bench unit and confirm a PASS line; (b) repeat with the SD card removed and confirm a FAIL line specifically identifying the SD check; (c) repeat with the sim-pulse jumper disconnected and confirm a FAIL line specifically identifying the sensor check; (d) on mainline firmware, run `verify` against a real (or bench-stub) server and confirm all three checks report correctly, including a visibly climbing pulse count with the sim jumper connected.
11. **Evidence required before "complete":** bench transcripts for (a)–(d); confirmation via a build-configuration review that `FACTORY_TEST_MODE` code is excluded from the production build artifact (e.g., binary size/symbol check).

> **Explicit factory/runtime separation (per this plan's mandatory rule):** the factory-test firmware and the mainline production firmware are two distinct build outputs from the same source tree. Nothing in `factory_test.h` may be linked into, or reachable from, the artifact that actually ships to a customer. This separation is a Definition-of-Done gate, not a suggestion.

## ADR-009 — Separate Hardware Identity from Logical Asset Identity

1. **Implementation phase:** Phase 6 (implemented before/alongside ADR-008, since `verify`'s field-commissioning workflow is expected to also set `asset_id`).
2. **Exact objective:** Add a new `asset_id` NVS field and a new `set asset <id>` console command; add `hardware_id` and `asset_id` to the push payload envelope (both, every batch); add a new `decommission` console command (factory reset plus zeroing the SD queue/checkpoint contents before deletion) built with ADR-014's confirmation-code pattern from birth (see Phase 6 note below — this is where that pattern is first implemented, not retrofitted); document the receiver-side aggregation rule (sum of litres across every `hardware_id` ever linked to one `asset_id`) as a receiver-side requirement, cross-referenced to Phase 10.
3. **Files likely affected:** `store.h` (new `asset_id` field), `provision.h` (`set asset`, `decommission` commands), `telemetry.h` (envelope fields).
4. **Must NOT be changed:** `hardware_id` remains exactly the existing MAC-derived value, unchanged; do not attempt to transplant or reconcile a totalizer/`seq` history across a hardware swap on-device — ADR-009 explicitly rejected this in favor of receiver-side aggregation.
5. **Backward compatibility impact:** additive envelope field; configuration addition.
6. **Migration requirement:** none forced; existing devices retain no `asset_id` until an operator runs `set asset <id>` during a maintenance touch. The receiver must tolerate unassigned devices indefinitely, not just during a transition window.
7. **Test requirement:** native-host test for the envelope serialization including both identity fields; a bench test simulating a controller replacement.
8. **Definition of Done:** `set asset <id>` implemented and verified; the push envelope carries both identifiers on every batch; a bench test simulating a controller replacement (fresh NVS and fresh SD card, same `asset_id` set manually on the replacement) confirms the reference receiver correctly aggregates cumulative litres across both `hardware_id`s under one `asset_id`; `decommission` verified to zero SD contents before deletion, gated by the ADR-014 confirmation code.
9. **Rollback safety:** additive; a rollback to prior firmware simply stops reporting `asset_id` (reverts to unassigned from the receiver's point of view), no data conflict.
10. **Manual bench verification steps:** (a) run `set asset <id>` and confirm subsequent push payloads carry it; (b) simulate a controller swap on the bench (fresh NVS + fresh SD card, same physical bench setup) and assign the same `asset_id`; confirm the reference receiver's dashboard shows continuous cumulative litres across the two `hardware_id`s; (c) run `decommission` without the confirmation code and confirm it does NOT proceed; run it with the correct code and confirm SD contents are zeroed before deletion.
11. **Evidence required before "complete":** bench transcripts for (a)–(c), including a receiver-side screenshot showing correct cross-`hardware_id` aggregation under one `asset_id`.

## ADR-010 — Immutable Calibration History; Removal of Unimplemented Temperature Fields

1. **Implementation phase:** Phase 7.
2. **Exact objective (firmware side):** remove `density`/`T_ref` from the config-poll consumption path and from cached NVS calibration fields; retain `K_factor` and `version` only. **(Receiver side, cross-referenced to Phase 10):** remove or ignore the `density`/`T_ref` columns from the config-poll response; add an append-only calibration change-history table (old value, new value, new version, receiver wall-clock timestamp) alongside the existing current-value calibration table.
3. **Files likely affected:** `store.h` (remove `density`/`tref` NVS fields and `setCalib`/getter signatures accordingly), `sync.h` (config-poll parsing no longer extracts `density`/`T_ref`), `provision.h` (`show` command output updated to drop the now-removed fields), `server/server.py` (receiver-side: drop/ignore columns from the response, add the append-only history table — see Phase 10).
4. **Must NOT be changed:** the existing behavior that editing `K_factor` retroactively recomputes all historical litres — ADR-010 explicitly retains this as a deliberate, core product feature, not something this change restricts; do not implement an approval/sign-off gate on calibration changes — explicitly deferred, out of scope for this phase.
5. **Backward compatibility impact:** minor protocol migration (fields were never consumed by any calculation, so the practical blast radius is limited).
6. **Migration requirement:** the receiver drops/ignores the `density`/`T_ref` columns in the same release that adds the audit-history table; the device simply stops sending/caching them on its next OTA — no data-loss risk, since the server-side computation never used them.
7. **Test requirement:** native-host test confirming the config-poll parser no longer chokes on a response that omits `density`/`T_ref`; a bench test verifying a sequence of K-factor changes produces correct, retrievable audit-history rows.
8. **Definition of Done:** the config-poll response no longer includes `density`/`T_ref`; a bench test verifies at least three consecutive K-factor changes each produce a correct, immutable audit-history row (old value, new value, version, timestamp) alongside the updated current value.
9. **Rollback safety:** additive/removal-only; a rollback to prior firmware simply resumes requesting fields the (already-updated) receiver may or may not still return — the parser must tolerate their absence gracefully in either direction, which is itself part of this phase's test requirement.
10. **Manual bench verification steps:** (a) confirm the config-poll response no longer includes `density`/`T_ref` and the device's `show` output no longer displays them; (b) change K-factor three times on the reference receiver's admin page and confirm each change produces a new, correctly populated audit-history row without altering or deleting any prior row; (c) confirm the current-value calibration lookup (used for the live dashboard) still reflects only the latest value, correctly derived from the history table.
11. **Evidence required before "complete":** bench transcripts for (a)–(c); a database dump (or equivalent) showing the audit-history table's contents after the three-change sequence.

## ADR-011 — Per-Boot Timer Jitter

1. **Implementation phase:** Phase 5 or later is not required — this is scheduled as an independent, low-dependency item; per the phase-topic mapping in this plan it is grouped under **Phase 4** (Reliability hardening) since it is a low-risk, self-contained timing change with no dependency on Phase 5's security work, and fits naturally alongside the other robustness items in that phase. *(Note: if a stricter reading of the phase-topic names is preferred, this item can equally be executed any time after Phase 1, since its only dependency is the existence of the main loop's timers — this plan places it in Phase 4 for scheduling convenience, not because of a hard architectural dependency.)*
2. **Exact objective:** at boot, after identity is established, compute one random offset in [0, period/4) for each of the push, config-poll, OTA-poll, and health-report timers, applied only to each timer's first firing after boot; all subsequent firings remain on the timer's normal fixed period.
3. **Files likely affected:** `covio_firmware.ino` (timer initialization in `setup()`).
4. **Must NOT be changed:** the steady-state fixed periods themselves (`PUSH_PERIOD_MS`, `CONFIG_POLL_MS`, `OTA_POLL_MS`, the 60-second health period); do not implement per-cycle re-randomization — ADR-011 explicitly rejected this.
5. **Backward compatibility impact:** none — fully additive, invisible to any external consumer.
6. **Migration requirement:** none.
7. **Test requirement:** a bench/simulation test booting many simulated instances at an identical reference time and confirming their first-firing times are spread across the expected window.
8. **Definition of Done:** the bench/simulation test above passes; steady-state cadence is confirmed unchanged from the pre-ADR fixed periods.
9. **Rollback safety:** additive; a rollback simply removes the jitter, restoring exact phase-alignment risk, a regression but not a new hazard.
10. **Manual bench verification steps:** (a) using either physical bench units or a simulated harness, boot twenty instances at the same wall-clock instant and record each one's first push/config/OTA-poll firing time; confirm they are spread across the expected window rather than landing simultaneously; (b) confirm each instance's SECOND and later firings land back on the exact fixed period from its own (jittered) first firing.
11. **Evidence required before "complete":** timing data/logs from the twenty-instance test showing the spread and the subsequent fixed cadence.

## ADR-012 — On-Demand Log Ring Buffer Retrieval

1. **Implementation phase:** Phase 8.
2. **Exact objective:** add a fixed-size (last 500 lines) circular log buffer on SD, populated by the same logging calls that already write to Serial; add a new `record_type = LOG_REQUEST_RESPONSE` under ADR-001's schema, emitted only when the device observes a `log_request` flag in a config-poll response, flowing through the existing durable queue/push pipeline.
3. **Files likely affected:** `queue.h` (new ring-buffer file management, new record_type), a shared logging function touched across `covio_firmware.ino`/`totalizer.h`/`queue.h`/`sync.h`/`ota.h`/`provision.h` (redirecting existing Serial-only log calls to also write the ring buffer — this is the one item in this plan expected to touch nearly every file, precisely because it centralizes something currently scattered), `sync.h` (reads the `log_request` flag from the config-poll response), `server/server.py` (admin-side flag setter — cross-referenced to Phase 10).
4. **Must NOT be changed:** the existing tagged Serial-line format/content — the ring buffer mirrors what already exists, it does not redesign logging conventions; do not build a general-purpose remote command-execution channel in this phase — ADR-012 explicitly scoped this to read-only log retrieval and deferred anything broader.
5. **Backward compatibility impact:** protocol change (additive) and storage addition (new ring-buffer file).
6. **Migration requirement:** none.
7. **Test requirement:** native-host test for ring-buffer wraparound (writing past 500 entries correctly overwrites the oldest); a bench test for end-to-end log retrieval.
8. **Definition of Done:** Serial output is confirmed mirrored into the ring buffer; setting the `log_request` flag on the reference receiver is confirmed to produce a `LOG_REQUEST_RESPONSE` delivery within one config-poll cycle; wraparound is confirmed correct with no corruption.
9. **Rollback safety:** additive; a rollback to prior firmware simply removes log-retrieval capability, no data conflict.
10. **Manual bench verification steps:** (a) run the device through a period of normal operation, then inspect the SD ring-buffer file directly (or via a debug dump) and confirm its contents match recent Serial output; (b) set the `log_request` flag via the reference receiver's admin interface and confirm the device responds with a `LOG_REQUEST_RESPONSE` batch within one config-poll cycle; (c) run the device long enough to wrap the buffer past 500 lines and confirm the oldest entries are correctly overwritten with no corruption of the remaining entries.
11. **Evidence required before "complete":** bench transcripts for (a)–(c), including a before/after comparison of ring-buffer contents across the wraparound test.

## ADR-013 — Single Source of Truth, Native Test Harness, Pinned Toolchain

1. **Implementation phase:** split across **Phase 0** (delete `src/`, add PlatformIO include-path config, stand up the empty native-host test harness skeleton) and **Phase 9** (finalize CI wiring, confirm full coverage per this ADR's Definition of Done, finalize the toolchain-version record). This split is deliberate — see the Phase 0 and Phase 9 cards below for the exact reasoning; it is not a duplication of work, it is a single ADR whose infrastructure must exist early and whose completion is certified late.
2. **Exact objective:** eliminate the `root/`/`src/` duplication; establish a native-host (non-ESP32) automated test build covering CRC computation/validation, the cumulative-ack contiguous-sequence algorithm, the truncate-before-append framing logic, and the schema-version dispatch logic; pin and document the exact Arduino-ESP32 core (and PlatformIO platform/framework, if used) version.
3. **Files likely affected:** deletion of the entire `src/` folder; new `platformio.ini`; new `test/native/` directory (harness + per-phase test files, populated incrementally); new `VERSIONS.md`.
4. **Must NOT be changed:** the root-level flat file layout itself (this remains the single source of truth, per ADR-013 — it is not relocated into `src/`); the existing manual bench test gates in `IMPLEMENTATION_AND_TESTING.md` are retained, not replaced, by the native test suite.
5. **Backward compatibility impact:** none — repository/process change only, zero effect on already-deployed devices.
6. **Migration requirement:** none.
7. **Test requirement:** this ADR's implementation *is* the test infrastructure; its own "test requirement" is that the harness itself builds and runs on a native host with zero ESP32 hardware required.
8. **Definition of Done:** `src/` removed; a PlatformIO build against the root layout succeeds immediately after removal; the native-host test binary exists, builds, and passes, with coverage confirmed for all four logic areas named in this ADR (accumulated across Phases 1–8, each of which adds its own tests to this same harness); `VERSIONS.md` exists and names the exact toolchain version the current release was built and tested against.
9. **Rollback safety:** not applicable — repository/process artifacts, no device-side behavior.
10. **Manual bench verification steps:** (a) confirm a PlatformIO build succeeds against the root layout with `src/` absent; (b) confirm the native test binary builds and runs on a development machine with no ESP32 board attached; (c) confirm `VERSIONS.md` accurately names the toolchain version used to produce the currently tested release binary.
11. **Evidence required before "complete":** build logs for (a) and (b); the merged `VERSIONS.md`.

## ADR-014 — Confirmation-Coded Destructive Commands

1. **Implementation phase:** split across **Phase 6** (the `decommission` command, built with the confirmation-code pattern from birth, as part of ADR-009's implementation) and **Phase 9** (retrofitting the same pattern onto the pre-existing `factory` command, plus the unrelated `SD_QUEUE_DIR` dead-constant cleanup). This split avoids touching the console code twice for the same reason — see the Phase 6 and Phase 9 cards for the exact reasoning.
2. **Exact objective:** any destructive console command (currently: `factory`; from Phase 6 onward: also `decommission`) requires typing the command alone first, which prints a warning and a random four-digit confirmation code, then retyping the command with that exact code before the destructive action proceeds; remove the unused `SD_QUEUE_DIR` constant and consolidate the queue directory path to one named source.
3. **Files likely affected:** `provision.h` (confirmation-code logic for `factory` in Phase 9, and for `decommission` in Phase 6), `config.h` (remove `SD_QUEUE_DIR` in Phase 9), `queue.h` (reference the single consolidated path source in Phase 9).
4. **Must NOT be changed:** no other console command gains a confirmation requirement — this pattern applies only to explicitly destructive commands (`factory`, `decommission`); non-destructive commands (`show`, `set ...`, `history`, `verify`) remain single-step.
5. **Backward compatibility impact:** none — console UX change only.
6. **Migration requirement:** none.
7. **Test requirement:** a bench test confirming `factory` (and separately `decommission`) alone does not proceed, and with the correct code does proceed.
8. **Definition of Done:** both destructive commands require and correctly validate the confirmation code; `SD_QUEUE_DIR` is removed and the queue path has exactly one named source.
9. **Rollback safety:** additive; a rollback to prior firmware removes the confirmation requirement, a UX regression but not a new hazard (the underlying destructive actions themselves are unchanged).
10. **Manual bench verification steps:** (a) type `factory` alone and confirm the device does not reset, but prints a warning and a code; (b) type `factory <correct code>` and confirm it does reset; (c) repeat (a)/(b) for `decommission`; (d) grep the codebase (or inspect the build) to confirm `SD_QUEUE_DIR` no longer exists and the queue path is referenced from one place.
11. **Evidence required before "complete":** bench transcripts for (a)–(c); a code-search confirmation for (d).

## ADR-015 — Fixed-Threshold Flow Plausibility Flag

1. **Implementation phase:** Phase 8.
2. **Exact objective:** implement the real `sensor_plausible` computation — false if the totalizer records zero new pulses across three consecutive 60-second health-report intervals (180 seconds) while a current WiFi RSSI reading exists — populating the field ADR-002/Phase 3 shipped as a `true` placeholder.
3. **Files likely affected:** `totalizer.h` or `telemetry.h` (the plausibility check, tracking pulse-count deltas across health intervals).
4. **Must NOT be changed:** the 180-second/three-interval threshold itself is the frozen decision, not a starting point for tuning during this phase; do not attempt to build a statistical/learned anomaly model — ADR-015 explicitly rejected this in favor of the fixed rule.
5. **Backward compatibility impact:** none — additive field-value logic within an already-shipped field slot.
6. **Migration requirement:** none.
7. **Test requirement:** a bench test holding the totalizer flat (sim pulses paused) for over 180 seconds, confirming the flag flips to false on the next HEALTH record, and flips back to true once pulses resume.
8. **Definition of Done:** the bench test above passes; the field's Phase-3 placeholder (`true`) is confirmed replaced by real, threshold-driven logic.
9. **Rollback safety:** additive; a rollback restores the `true` placeholder behavior, a loss of a diagnostic signal, not a new hazard.
10. **Manual bench verification steps:** (a) with sim pulses running normally, confirm `sensor_plausible = true`; (b) pause sim pulses (or physically stop the meter) for over 180 seconds and confirm the next HEALTH record reports `sensor_plausible = false`; (c) resume pulses and confirm the flag returns to `true` on a subsequent HEALTH record.
11. **Evidence required before "complete":** bench transcript for (a)–(c), including the exact timestamps of the pause/resume relative to the flag's transition.

## ADR-016 — Reference Receiver Is Bench-Only; Production Receiver Must Enforce the Frozen Contract

1. **Implementation phase:** Phase 10.
2. **Exact objective:** designate the reference `server.py` as permanently bench/development-only; verify (not necessarily build, unless this team also owns the receiver) that whatever production receiver is used implements the exact wire contract frozen across ADR-001, ADR-002, ADR-009, ADR-010, and enforces API-key authentication.
3. **Files likely affected:** none within the firmware source tree — this ADR's work product is verification and, if this team owns it, the production receiver codebase (explicitly a separate system).
4. **Must NOT be changed:** the frozen wire contract itself is not renegotiated to suit whatever the production receiver finds convenient — the receiver conforms to the contract, not the reverse.
5. **Backward compatibility impact:** the receiver must support the current+previous schema version rule (ADR-001) for as long as any fielded device might run the previous version.
6. **Migration requirement:** receiver-side only; not a firmware migration.
7. **Test requirement:** a load test against the target fleet size's aggregate request rate (per the Blueprint's Task 7 math) with authentication enforced.
8. **Definition of Done:** the production receiver demonstrates correct handling of the target fleet size's aggregate request rate in a load test, with any request presenting an invalid or missing API key correctly and consistently rejected.
9. **Rollback safety:** not applicable to firmware; a receiver-side deployment concern for whichever team owns that system.
10. **Manual bench/verification steps:** (a) confirm the production receiver rejects a request with a missing or invalid `X-Api-Key`; (b) confirm the production receiver accepts and correctly stores a batch containing a mix of current and previous schema versions; (c) run a load test at the target fleet size's computed aggregate request rate and confirm no dropped/corrupted requests.
11. **Evidence required before "complete":** load-test report and authentication-rejection test transcript, both attached as Phase 10 evidence (see Phase 10 below).

---

# PART 2 — Phase Plans (Phase 0 → Phase 11)

## Phase 0 — Implementation Readiness and Exact Byte/Schema Specification

**Preconditions:** all three governing documents (Audit, Blueprint, ADR) have been read in full by whoever executes this phase; no prior phase exists.

**Implementation scope:**
- Produce the exact byte-level layout for every `(schema_version, record_type)` combination needed through at least Phase 3 (telemetry and health records) — this is the specific "mechanical implementation step downstream of a fully-specified architecture" the ADR document's closing section explicitly reserved for implementation time. Field order, sizes, and padding are decided here, once, and recorded in `SCHEMA_REGISTRY.md`.
- Verify the SD/SdFat library's file-truncate capability (see the ADR-003 practicality flag above) against the toolchain version selected for this project. Record the finding either way.
- Verify the factory-flashing toolchain gap for Secure Boot V2/Flash Encryption (see the ADR-005 practicality flag above). Record the finding and the selected tooling approach.
- Delete the `src/` duplicate folder and add `platformio.ini` restoring PlatformIO support against the root layout (the narrow, front-loaded slice of ADR-013 — see that card's phase-split note).
- Stand up the native-host test harness as an empty, buildable skeleton (no test content required yet beyond a proof that the harness itself compiles and runs on a development machine with no ESP32 hardware attached) — every subsequent phase adds its own tests into this same harness.
- Confirm and record the exact Arduino-ESP32 core / PlatformIO platform version in `VERSIONS.md`.

**Out of scope:** no telemetry/queue/health logic is implemented in this phase; no OTA, security, provisioning, or calibration work begins here.

**Files expected:** new `SCHEMA_REGISTRY.md`, new `platformio.ini`, new `test/native/` skeleton, new `VERSIONS.md`; deletion of `src/`.

**Tests required:** a build-only test — the native harness skeleton must compile and run (even with zero real test cases yet) on a development machine.

**Bench commands/checks required:** none involving physical hardware yet, except the toolchain verification checks above (which may require a bench unit to confirm actual library behavior for the truncate-capability check).

**Success criteria:** `SCHEMA_REGISTRY.md` exists with the full byte layout for telemetry and health records; the SD-truncate capability finding is recorded (capability confirmed present, or an ACR has already been raised against ADR-003 if it is not); the Secure-Boot tooling gap finding is recorded; `src/` is gone and PlatformIO still builds; the native test harness builds and runs empty; `VERSIONS.md` is populated.

**Stop conditions:** if the SD/SdFat library does not support truncate-to-offset, **stop this phase and raise an Architecture Change Request against ADR-003 before proceeding to Phase 1 or Phase 2** — do not invent a substitute mechanism. If the toolchain version cannot be pinned to something Secure-Boot-capable at all, flag this for Phase 5 planning but do not block Phase 0's other deliverables on it.

---

## Phase 1 — Schema and Storage Migration Foundation

**Preconditions:** Phase 0 complete, including `SCHEMA_REGISTRY.md`'s byte layout for the telemetry record and the SD-truncate finding recorded (a positive finding, or a resolved ACR).

**Implementation scope:** ADR-001 in full, per its Implementation Card above.

**Out of scope:** ADR-003's storage redesign is a separate phase — do not implement segmented rotation or the read-cursor fix here, only the schema-version/record_type framing itself, applied to the existing (still single-file) queue.

**Files expected:** `queue.h`, `telemetry.h`, `sync.h`, `server/server.py`, `SCHEMA_REGISTRY.md` (updated).

**Tests required:** native-host tests per ADR-001's card, added to Phase 0's harness.

**Bench commands/checks required:** the three-step verification in ADR-001's card (drain-then-upgrade, schema-version visibility via `show`, mixed-version batch acceptance).

**Success criteria:** ADR-001's Definition of Done is met in full.

**Stop conditions:** if any existing bench/pilot device cannot be confirmed drained to empty before this OTA is applied to it, do not apply the OTA to that device until it is drained — this is a hard per-device gate, not a suggestion.

---

## Phase 2 — SD Queue Integrity and Catch-Up Performance

**Preconditions:** Phase 1 complete (ADR-003's framing changes are versioned under the same schema mechanism Phase 1 established).

**Implementation scope:** ADR-003 in full, per its Implementation Card above. **This phase's OTA is delivered to the field together with Phase 1's, as one combined release** — do not ship two separate forced-drain migrations back to back (see Migration Checklist item 1).

**Out of scope:** the HEALTH record (ADR-002) is not implemented here — Phase 2 only fixes the existing telemetry queue's integrity and performance.

**Files expected:** `queue.h`, `totalizer.h`, `covio_firmware.ino`.

**Tests required:** native-host tests per ADR-003's card; the power-cut, multi-day-outage, and segment-lifecycle bench tests, which cannot be fully replicated on a native host.

**Bench commands/checks required:** the three bench procedures in ADR-003's card, with the power-cut test repeated at multiple write-timing offsets.

**Success criteria:** ADR-003's Definition of Done is met in full; rollback safety analysis (ADR-003 card, item 9) is confirmed to hold in an actual bench rollback test (force a health-confirm failure and observe the rollback behaves as analyzed).

**Stop conditions:** if the truncate-before-append mechanism cannot be implemented as specified due to a library limitation not caught in Phase 0, **stop and raise an ACR against ADR-003 — do not fall back to a byte-scanning resync approach.**

---

## Phase 3 — Health and Diagnostics Records

**Preconditions:** Phase 2 complete (HEALTH records ride the now-fixed queue).

**Implementation scope:** ADR-002, exactly as scoped in its Implementation Card — including the explicit placeholder-vs-real-value field split. This phase does **not** implement the logic behind `reset_reason`, `watchdog_reset_count`, the full four-state `sd_status`, or `sensor_plausible` — it ships the record with those fields present and defaulted, ready for Phases 4 and 8 to populate.

**Out of scope:** ADR-004's SD state machine, ADR-006's watchdog, and ADR-015's sensor-plausibility logic are not implemented here.

**Files expected:** `queue.h`, `telemetry.h`, `covio_firmware.ino`, `server/server.py`.

**Tests required:** native-host serialization test per ADR-002's card, added to the shared harness.

**Bench commands/checks required:** the three bench procedures in ADR-002's card, specifically including the no-secrets packet inspection.

**Success criteria:** ADR-002's Phase-3-scoped Definition of Done is met; the receiver correctly displays every field, including the placeholder values, distinguishably from "real" values (so Phase 4/8 completion is later visibly obvious on the same dashboard).

**Stop conditions:** if any secret (API key, WiFi password) is found in a captured HEALTH payload during the bench inspection, **stop immediately — this is a security defect, not a minor bug** — do not proceed to Phase 4 until resolved and re-verified.

---

## Phase 4 — Reliability Hardening

**Preconditions:** Phase 3 complete (HEALTH record exists with placeholder fields ready to receive real values).

**Implementation scope:** ADR-004 and ADR-006, per their Implementation Cards. These two ADRs are largely independent of each other (SD state machine vs. watchdog/reset-reason) and **may be implemented in parallel by two different engineers** once Phase 3 is complete, since neither depends on the other's internal logic — only both depend on Phase 3's HEALTH record existing. ADR-011 (timer jitter) is also grouped into this phase per this plan's scheduling note in its Implementation Card, and is independent of both ADR-004 and ADR-006.

**Out of scope:** ADR-005's security work; ADR-015's sensor-plausibility logic (a different HEALTH field, owned by Phase 8).

**Files expected:** `covio_firmware.ino`, `queue.h`, `totalizer.h`, `sync.h`, `store.h`, `telemetry.h`.

**Tests required:** native-host tests per ADR-004's and ADR-006's cards; the hardware-dependent bench tests for both (SD pull/removal, hung-loop watchdog trigger, low-space threshold).

**Bench commands/checks required:** all bench procedures listed under ADR-004 and ADR-006's cards, plus ADR-011's multi-instance boot-jitter test.

**Success criteria:** ADR-004, ADR-006, and ADR-011's Definitions of Done are all met; the HEALTH record now reports real `reset_reason`, `watchdog_reset_count`, and the full `sd_status` enum in place of Phase 3's placeholders.

**Stop conditions:** the deliberately-hung-loop test build used to verify the watchdog **must never be merged into mainline firmware** — if this discipline is not followed, stop and correct before proceeding.

---

## Phase 5 — Security Hardening

**Preconditions:** Phase 0's Secure-Boot tooling-gap finding is resolved (a concrete factory-flashing toolchain plan exists); Phase 4 complete (this phase does not strictly depend on Phase 4's content, but is sequenced after it per this plan's phase order, and there is no reason to reorder it).

**Implementation scope:** ADR-005 in full, per its Implementation Card.

**Out of scope:** ADR-008's factory-time key-injection *mechanism* (the actual console commands/process used to write a generated key into NVS during factory testing) belongs to Phase 6 — this phase delivers the cryptographic/transport capability (HTTPS, pinned CA, Secure Boot, Flash Encryption) that Phase 6's mechanism relies on, but does not itself build the factory workflow.

**Files expected:** `sync.h`, `ota.h`, `config.h`, plus factory-side signing/eFuse tooling artifacts (outside the runtime firmware source).

**Tests required:** the MITM-rejection, mis-signed-image-rejection, and eFuse-summary bench tests per ADR-005's card. These cannot be meaningfully replaced by native-host tests given their hardware/PKI nature.

**Bench commands/checks required:** all four bench procedures under ADR-005's card.

**Success criteria:** ADR-005's Definition of Done is met in full; the first Secure-Boot-enabled factory-flashed unit's eFuse summary is archived as a permanent manufacturing-QA record.

**Stop conditions:** **do not roll this phase's HTTPS/pinned-CA change out to any already-fielded device until the production server's certificate and pinned CA are independently verified correct and not near expiry** — per ADR-005's rollback-safety analysis, a misconfigured cert leaves a device unable to self-heal via OTA, requiring physical recovery. This is a hard go/no-go gate, not a routine checklist item. Additionally: **do not proceed with the first factory Secure Boot fuse burn on a volume batch until it has been verified correct on a single unit and that unit's eFuse summary has been reviewed** — eFuse burns are irreversible.

---

## Phase 6 — Configuration, Provisioning, and Identity

**Preconditions:** Phase 5 complete (per-device key provisioning capability exists for ADR-008's factory workflow to use).

**Implementation scope:** ADR-007, ADR-009, and ADR-008, in that internal order — ADR-009 (identity) is implemented first or alongside ADR-007 (both touch `store.h`/`provision.h` independently and can be parallelized), with ADR-008 (provisioning/commissioning workflow) implemented last within this phase since its `verify` field-commissioning step is expected to also invoke ADR-009's `set asset` command. **This phase also implements the `decommission` command with ADR-014's confirmation-code pattern built in from birth** (not retrofitted later) — see ADR-014's card for why this specific split avoids duplicate work.

**Out of scope:** ADR-014's retrofit of the confirmation-code pattern onto the pre-existing `factory` command is explicitly Phase 9's responsibility, not this phase's — do not touch `factory`'s confirmation behavior here, only `decommission`'s.

**Files expected:** `store.h`, `provision.h`, `telemetry.h`, new `factory_test.h`, `covio_firmware.ino`.

**Tests required:** native-host tests per ADR-007's and ADR-009's cards; bench tests per ADR-007's, ADR-008's, and ADR-009's cards.

**Bench commands/checks required:** all bench procedures under ADR-007, ADR-008, and ADR-009's cards, run in that order since ADR-008's `verify` rehearsal depends on ADR-009's `set asset` command already existing.

**Success criteria:** ADR-007, ADR-008, and ADR-009's Definitions of Done are all met; the factory-test firmware variant is confirmed separate from and absent in the production build.

**Stop conditions:** if `factory_test.h` code is found to be reachable in a production (non-`FACTORY_TEST_MODE`) build during the binary-size/symbol check, **stop and fix the build configuration before this phase is marked complete** — this is a hard, non-negotiable separation per this plan's mandatory rule.

---

## Phase 7 — Calibration Governance

**Preconditions:** Phase 6 complete (no direct dependency, but sequenced after per this plan's order; no reason to reorder).

**Implementation scope:** ADR-010's firmware-side scope (remove `density`/`T_ref` consumption and caching) in full. The receiver-side audit-history table is tracked here as a required deliverable but is explicitly receiver-side work — see the separation note below.

**Out of scope:** any approval/sign-off workflow for calibration changes — explicitly deferred by ADR-010, not to be implemented in this or any other phase without a new ADR.

**Files expected:** `store.h`, `sync.h`, `provision.h` (firmware side); `server/server.py` (receiver side — see below).

**Receiver-side separation note (per this plan's mandatory rule):** the append-only audit-history table, and the decision of whether to drop or merely ignore the `density`/`T_ref` columns, are receiver-side implementation choices. The firmware team's Phase 7 responsibility ends at "stop sending/consuming these fields correctly and gracefully." The receiver-side table build-out is verified in Phase 10, not asserted as done here just because the firmware side is done.

**Tests required:** native-host test per ADR-010's card; bench test for the audit-history rows (this specific bench test necessarily exercises the receiver, so it is a joint firmware+receiver verification, appropriately run once both sides are ready — likely at the Phase 10 boundary if the receiver isn't ready exactly when firmware Phase 7 finishes).

**Bench commands/checks required:** ADR-010's three bench procedures.

**Success criteria:** ADR-010's Definition of Done is met for the firmware side unconditionally; the receiver-side audit table's correctness is confirmed either now (if the receiver is ready) or explicitly deferred to Phase 10 with that deferral recorded, not silently assumed.

**Stop conditions:** none specific beyond the general ACR protocol.

---

## Phase 8 — Logging and Support Diagnostics

**Preconditions:** Phase 3 complete (HEALTH record exists, since ADR-015's field lives there); Phase 7 complete per this plan's sequential order (no direct dependency otherwise).

**Implementation scope:** ADR-012 in full, and ADR-015 in full (populating the placeholder Phase 3 shipped).

**Out of scope:** any general-purpose remote command-execution channel — ADR-012 explicitly scoped this phase to read-only log retrieval only.

**Files expected:** the shared logging touch-point across nearly every firmware file (per ADR-012's card), `queue.h`, `sync.h`, `totalizer.h`/`telemetry.h` (for ADR-015), `server/server.py` (receiver-side flag setter — see Phase 10 for verification).

**Tests required:** native-host tests per ADR-012's and ADR-015's cards; bench tests per both cards.

**Bench commands/checks required:** all bench procedures under ADR-012 and ADR-015's cards.

**Success criteria:** ADR-012 and ADR-015's Definitions of Done are both met; the HEALTH record's `sensor_plausible` field is confirmed no longer a static placeholder.

**Stop conditions:** if implementing the shared logging touch-point reveals a need to change the existing tagged Serial-line format/content beyond adding a second sink, **stop and confirm this is still within ADR-012's scope** (it should not require format changes) **before proceeding** — a scope creep here would mean quietly redesigning logging conventions ADR-012 did not authorize changing.

---

## Phase 9 — Build, Tests, CI, and Release Discipline

**Preconditions:** Phases 1–8 complete, each having added its own tests to the Phase-0-established native harness.

**Implementation scope:** finalize ADR-013 (confirm full test coverage across all four required logic areas, wire up CI execution of the native harness, finalize `VERSIONS.md`); implement ADR-014's remaining scope (retrofit the confirmation-code pattern onto the pre-existing `factory` command; remove the unused `SD_QUEUE_DIR` constant and consolidate the queue path reference).

**Out of scope:** no new architectural capability is added in this phase — this is exclusively a hardening/consolidation pass over work already done in Phases 0–8.

**Files expected:** CI configuration (whatever the project's chosen CI system is), `VERSIONS.md` (finalized), `provision.h` (factory confirmation retrofit), `config.h`/`queue.h` (`SD_QUEUE_DIR` cleanup).

**Tests required:** a full run of the native harness accumulated across all prior phases, now wired into CI so it runs automatically on every future change.

**Bench commands/checks required:** ADR-013's and ADR-014's bench procedures (the latter specifically re-run against `factory`, since Phase 6 already verified it against `decommission`).

**Success criteria:** ADR-013 and ADR-014's Definitions of Done are both fully met; CI is confirmed to run the native suite automatically and fail visibly on a deliberately broken test (a sanity check that CI wiring actually works, not just that tests exist).

**Stop conditions:** none specific beyond the general ACR protocol.

---

## Phase 10 — Receiver/Fleet Integration Verification

**Preconditions:** Phases 1–9 complete on the firmware side.

**Implementation scope (explicitly receiver-side, per this plan's mandatory separation rule — none of this is firmware source code work unless this team also owns the receiver):**
- Verify/implement API-key authentication enforcement on the production receiver (ADR-005/ADR-016).
- Verify/implement HEALTH record storage and per-device diagnostics display (ADR-002).
- Verify/implement `asset_id`-based cumulative-litre aggregation across `hardware_id`s (ADR-009).
- Verify/implement the append-only calibration audit-history table and its correct derivation of the current-value view (ADR-010).
- Verify/implement the `log_request` flag admin control and correct handling of `LOG_REQUEST_RESPONSE` records (ADR-012).
- Verify the production receiver correctly accepts a mixed current+previous schema-version batch (ADR-001) and correctly rejects malformed/unauthenticated requests.
- Run a load test at the target fleet size's computed aggregate request rate (per the Blueprint's Task 7 math) against the actual production receiver stack, not the bench stub.

**Out of scope:** no firmware source file is modified in this phase. If a firmware gap is discovered during this verification (e.g., a field the receiver needs but firmware doesn't send), that is a new finding requiring its own ACR against the relevant ADR — it is not silently patched into the firmware here.

**Files expected:** none in the firmware repository; production receiver codebase (a separate system/repository, out of this plan's file scope).

**Tests required:** end-to-end integration tests exercising every bullet above against the real production receiver.

**Bench commands/checks required:** items 10/11 from ADR-016's Implementation Card, plus the equivalent end-to-end checks cross-referenced from ADR-002, ADR-009, ADR-010, and ADR-012's cards (each of those cards' "receiver stores/displays/aggregates correctly" claims are only fully evidenced here, at production-receiver scale, not merely on the bench stub used during Phases 1–9).

**Success criteria:** every bullet above is independently verified against the actual production receiver; the load test at target fleet scale passes with authentication enforced throughout.

**Stop conditions:** if the production receiver cannot demonstrate correct authentication enforcement, **do not proceed to Phase 11** — this is the direct, non-negotiable closure of the audit's original P0 finding regarding unauthenticated ingestion.

---

## Phase 11 — Final Production Certification

**Preconditions:** Phases 0–10 all complete with their evidence archived.

**Implementation scope:** no new capability is built in this phase. This phase re-runs, in full, on a final release-candidate build: the six original bench test gates from `IMPLEMENTATION_AND_TESTING.md` (T1–T6, unchanged), every bench test listed in every ADR Implementation Card in Part 1 of this document, and a final go/no-go review against the Release Checklist (Part D below).

**Out of scope:** any new architectural decision — if this phase surfaces one, it is out of scope for Phase 11 and requires its own ACR, deferred to a future release.

**Files expected:** none — this is a verification-only phase; any defect found results in a targeted fix inside whichever phase's scope it actually belongs to, followed by a re-run of this phase, not a new phase.

**Tests required:** full native test suite (green); full bench test suite (every gate across all prior phases, green).

**Bench commands/checks required:** the complete union of every "Manual bench verification steps" list across Part 1 of this document, executed against one single, final release-candidate firmware build and one single, final production receiver deployment — not a patchwork of results from different intermediate builds across different phases.

**Success criteria:** every item in the Release Checklist (Part D) is checked; no open ACR exists against any ADR.

**Stop conditions:** any single bench gate failing on the final release-candidate build blocks certification until fixed and the full suite is re-run — partial sign-off is not permitted.

---

# PART 3 — Closing Reference Material

## A. Master Dependency Map

```
Phase 0  (readiness: byte spec, src/ removal, test-harness skeleton, toolchain checks)
   │
   ▼
Phase 1  (ADR-001: schema/record framing)                      ─┐
   │                                                             │  delivered to
   ▼                                                             │  the field as
Phase 2  (ADR-003: queue truncate/segments/cursor)              ─┘  ONE combined
   │                                                                 OTA release
   ▼
Phase 3  (ADR-002: health record, placeholder fields)
   │
   ├────────────────────────────┐
   ▼                            ▼
Phase 4  (ADR-004 SD state       (ADR-006 watchdog +          (ADR-011 jitter)
          machine — parallel      reset-reason — parallel      — parallel to
          with ADR-006)           with ADR-004)                 both, no dependency
   │
   ▼
Phase 5  (ADR-005: TLS + Secure Boot + per-device keys)
   │
   ▼
Phase 6  (ADR-009 identity  →  ADR-007 config lifecycle          (parallel to
          [prerequisite for      [independent, can run            ADR-009]
          ADR-008's verify        alongside ADR-009]
          workflow]          →  ADR-008 provisioning/commissioning
                                  [depends on ADR-009 + Phase 5's
                                   per-device key capability]
                              →  ADR-014's `decommission` half
                                  [built alongside ADR-009]        )
   │
   ▼
Phase 7  (ADR-010: calibration governance, firmware side)
   │
   ▼
Phase 8  (ADR-012 log retrieval  +  ADR-015 sensor plausibility
          — parallel to each other, both depend only on Phase 3)
   │
   ▼
Phase 9  (ADR-013 finalized  +  ADR-014's `factory` retrofit half
          — parallel to each other)
   │
   ▼
Phase 10 (receiver-side verification of every ADR above — NOT firmware code)
   │
   ▼
Phase 11 (final certification — full regression, no new work)
```

**Hard rule:** phases are strictly sequential end-to-end (each phase's preconditions require the prior phase's Definition of Done). Within a phase, ADRs explicitly marked "parallel" above have no dependency on each other and may be implemented by different engineers simultaneously. No ADR outside its assigned phase may be started early, even if it appears independent — the phase boundary itself is also the point at which that phase's evidence is archived and reviewed, which later phases may implicitly rely on for confidence, even without a hard code dependency.

## B. Full Test Matrix

| Phase | ADR(s) | Native-host tests | Bench tests | Owner of receiver-side counterpart |
|---|---|---|---|---|
| 0 | (readiness) | harness skeleton builds | SD-truncate capability check | — |
| 1 | ADR-001 | schema dispatch (current, mixed, unrecognized) | drain→upgrade→mixed-batch acceptance | Phase 10 |
| 2 | ADR-003 | truncate-before-append, cumulative-ack, cursor-only-advances-on-ack | power-cut, multi-day-outage, segment lifecycle | — |
| 3 | ADR-002 | health-record serialization | 60s cadence independence, no-secrets inspection | Phase 10 |
| 4 | ADR-004, ADR-006, ADR-011 | low-space threshold arithmetic | SD pull/absent, hung-loop watchdog, throttled-OTA watchdog feed, 20-instance jitter spread | — |
| 5 | ADR-005 | — | MITM rejection, mis-signed OTA rejection, eFuse summary | — |
| 6 | ADR-007, ADR-008, ADR-009 | migration-runner idempotency, envelope serialization | schema migration, `history`, re-pointing, factory PASS/FAIL, `verify`, asset aggregation, `decommission` confirmation | Phase 10 (asset aggregation) |
| 7 | ADR-010 | config-poll parser tolerates missing fields | audit-history row correctness | Phase 10 (audit table) |
| 8 | ADR-012, ADR-015 | ring-buffer wraparound | log mirroring, log-request round-trip, sensor-plausibility flip | Phase 10 (log-request flag) |
| 9 | ADR-013, ADR-014 | full suite green in CI | PlatformIO build, native binary run, `factory` confirmation retrofit | — |
| 10 | ADR-016 (+ receiver counterparts of all above) | — | authentication rejection, mixed-schema acceptance, fleet-scale load test | (this phase IS the receiver-side owner) |
| 11 | (all) | full suite green | full union of every bench test above, on one final build | — |

## C. Migration Checklist

1. **Combined Phase 1 + Phase 2 OTA (ADR-001 + ADR-003):** every device that has ever run pre-ADR firmware must be confirmed drained to an empty queue before this single combined OTA is applied. If no device has been fielded yet, record this explicitly as "not applicable — no pre-existing fleet" rather than silently skipping the checklist item.
2. **Phase 5 security OTA (ADR-005):** every already-deployed device receives exactly one final plain-HTTP OTA that switches it to HTTPS with the pinned CA and updates `server_url`; any such device without a unique API key receives one via the console as a manual operation. Secure Boot/Flash Encryption are confirmed not retrofittable — any pre-Phase-5 hardware is explicitly logged as bench-only, permanently.
3. **Phase 6 configuration schema migration (ADR-007):** the 0→1 no-op migration is confirmed to run correctly on first boot of Phase-6 firmware for any pre-existing device.
4. **Phase 6 asset-identity backfill (ADR-009):** no forced migration; each pre-existing device is scheduled for a `set asset <id>` console touch at its next maintenance visit, tracked as an operational task list, not a firmware migration.
5. **Phase 7 calibration field removal (ADR-010):** the receiver's release dropping/ignoring `density`/`T_ref` is coordinated to ship no later than the firmware release that stops sending them, in either order, since both sides are required to tolerate the field's absence gracefully.

## D. Release Checklist

- [ ] Every ADR-001 through ADR-016's Definition of Done is met and its evidence archived.
- [ ] Every Phase 0–11 success criterion is met.
- [ ] No open Architecture Change Request exists against any ADR.
- [ ] The Migration Checklist (Part C) is fully executed or explicitly marked not-applicable, item by item.
- [ ] Phase 11's full bench-test union has been run once, end-to-end, on a single final release-candidate build and a single final production receiver deployment.
- [ ] The production receiver's authentication enforcement and fleet-scale load test (Phase 10) have both passed.
- [ ] The first Secure-Boot-enabled factory unit's eFuse summary has been reviewed and archived.
- [ ] `SCHEMA_REGISTRY.md` and `VERSIONS.md` are both current and merged.
- [ ] `src/` does not exist; PlatformIO and Arduino IDE builds both succeed from the root layout.
- [ ] CI runs the native test suite automatically and has been confirmed to fail visibly on a deliberately broken test.

## E. "New Agent Start Here" Instructions

1. Read, in order: `Firmware Architecture Audit.md`, `Firmware Architecture Freeze & Remediation Blueprint.md`, `Firmware Detailed Architecture Decision Record (ADR).md`, then this document.
2. Determine the current phase by checking which phases' Definition-of-Done evidence already exists and is archived. Do not assume a phase is complete without its evidence — re-verify if unclear, do not take a status claim on faith.
3. Never start a phase whose preconditions are not met, even if it looks independent — check the Master Dependency Map (Part A) first.
4. Never modify a decision recorded in the ADR document. If a decision seems impractical during implementation, stop and write an Architecture Change Request against the specific ADR — do not substitute an alternate mechanism.
5. Never do receiver-side work under the assumption it is "the same as firmware work" — check whether the current phase's scope is explicitly marked receiver-side (Phase 10, and specific cross-referenced items within other phases) before writing any receiver code as part of a firmware phase.
6. Never touch `factory_test.h` content in a way that could make it reachable from a production build — this is a hard, repeatedly-stated boundary in this document.
7. Add tests for the current phase's work into the shared native harness established in Phase 0 — do not create a parallel or one-off test setup.
8. Before marking any phase complete, gather every item listed under that phase's ADR card(s)' "Evidence required before complete" and archive it alongside the phase record.

## F. First Implementation Prompt — Phase 0 Only

> You are implementing **Phase 0 — Implementation Readiness and Exact Byte/Schema Specification** of the Covio Oil Flow Meter firmware, per `Firmware Phase-wise Implementation Master Plan.md`. Do not implement any capability from Phase 1 onward. Your scope is exactly the following, and nothing else:
>
> 1. Produce the exact byte-level layout (field order, sizes, padding) for the telemetry record and the health record under ADR-001's schema-versioning rule, through at least what Phase 3 will need. Write this into a new `SCHEMA_REGISTRY.md` at the repository root.
> 2. Verify whether the SD/SdFat library used by this project (pin the exact version as part of this check) supports truncating an open file to a smaller size. Record the finding in `SCHEMA_REGISTRY.md` or a dedicated readiness note. **If truncate-to-offset is not supported, stop and write an Architecture Change Request against ADR-003 — do not proceed to design a workaround yourself.**
> 3. Investigate and record what toolchain (Arduino IDE alone, Arduino IDE plus manual esptool steps, or a switch to PlatformIO/ESP-IDF) is required to enable ESP-IDF Secure Boot V2 and Flash Encryption for the factory-flashing step Phase 5 will need. Record the finding; this does not block the rest of Phase 0.
> 4. Delete the `src/` folder (confirmed byte-identical to the root layout per the original Audit) and add a `platformio.ini` at the repository root that points PlatformIO's include path at the root layout. Confirm a PlatformIO build succeeds immediately after this change, in the same change.
> 5. Stand up an empty, buildable native-host (non-ESP32) test harness under `test/native/` — no real test cases are required yet, only proof that the harness itself compiles and runs on a development machine with no ESP32 hardware attached.
> 6. Record the exact Arduino-ESP32 core version (and PlatformIO platform/framework version, if used) in a new `VERSIONS.md` at the repository root.
>
> Do not modify `covio_firmware.ino`, `config.h`, `store.h`, `totalizer.h`, `queue.h`, `telemetry.h`, `sync.h`, `ota.h`, `provision.h`, or `server/server.py` in this phase — those changes begin in Phase 1 onward. When finished, report exactly which of Phase 0's six success-criteria items are met, and explicitly flag either finding from items 2 and 3 above (capability confirmed / not confirmed, tooling gap identified / not identified) — do not proceed to Phase 1 planning until this report is reviewed.
