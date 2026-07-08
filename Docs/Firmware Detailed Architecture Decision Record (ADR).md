# Firmware Detailed Architecture Decision Record (ADR) — Covio Oil Flow Meter

**Status:** Constitution. Once approved, no architectural decision recorded here may change without a new ADR superseding it explicitly.
**Inputs treated as frozen source of truth:** `Firmware Architecture Audit.md`, `Firmware Architecture Freeze & Remediation Blueprint.md`.
**Scope:** this document makes exactly one final decision per consolidated architecture item (`C-01` through `C-16`) identified in the Blueprint. Every ADR below resolves the "could" left open by the Blueprint's Task 15 into a single, stated "will."
**Non-scope:** no code, no pseudocode, no class diagrams, no byte-level struct layouts. Where a decision necessarily implies an implementation detail (e.g., an exact numeric threshold), that number is frozen; the code that enforces it is not written here — see the closing section for the precise, deliberate boundary this draws.

---

## ADR-001 — Self-Describing, Versioned Telemetry Schema

**Decision Name:** Versioned Record Framing for All Device Data

**Problem Statement:** The telemetry wire format and SD row format (`QRow`) hardcode a single, fixed, un-versioned layout with exactly one payload field (`totalizer`). There is no way to add a new field, a new sensor type, or a new record purpose without breaking the existing format outright, and no way for a receiver to tell which layout a given batch of bytes was written under.

**Business Goal:** Allow the data model to evolve — new health fields, new sensor types, new diagnostic records — without a breaking flag-day migration, and without ever losing the ability to read historical data already on a device's SD card.

**Architecture Decision:** Every record, on both the SD queue and the wire JSON, is prefixed with a `schema_version` (an integer, starting at 1) and a `record_type` (an integer identifying what kind of record this is — telemetry, health, log-response, etc.). A device only ever emits records under the single schema_version its currently-running firmware understands; the schema_version is not negotiated at runtime. The receiver must accept and correctly interpret any batch containing a mix of exactly the current schema_version and the immediately preceding one — no more, no fewer — for as long as any fielded device might still be running the prior firmware version. Each schema_version, for each record_type, has one fixed, permanently-recorded field layout; once published, a schema_version's layout for a given record_type is never altered — evolution happens only by introducing a new schema_version.

**Reasoning:** A device only ever runs one firmware version at a time, so it only ever needs to write one schema_version at a time — this removes any need for runtime schema negotiation. Fixing the layout per (schema_version, record_type) pair keeps records fixed-size, preserving the simple, deterministic CRC and offset arithmetic the rest of the storage design depends on. Requiring the receiver to tolerate exactly a two-version window (current + previous) gives a precise, bounded compatibility contract that supports normal OTA rollout (where devices transition firmware versions at different times) without requiring indefinite support for every historical version ever shipped.

**Alternatives Rejected:**
- *Variable-length TLV or embedded JSON on the SD card* — rejected: parsing overhead is disproportionate on a memory-constrained device, and the "one version per device at a time" property removes the need for it entirely.
- *Separate files per schema version* — rejected: multiplies SD file-management complexity without benefit, since batches are naturally homogeneous.
- *Runtime schema negotiation (device asks the server which version to use)* — rejected: introduces a startup network dependency for something that must work correctly fully offline.
- *Protobuf/CBOR* — rejected: adds a build dependency and binary-size cost disproportionate to a small, bounded number of schema revisions, and breaks the codebase's existing, deliberate dependency-free-parsing philosophy.

**Trade-offs:**
- *Performance:* Negligible — two additional small integer fields per record.
- *Reliability:* Improves — schema_version becomes an additional corruption-detection signal for the queue's resync logic (ADR-003).
- *Complexity:* Moderate increase — every reader must dispatch on schema_version/record_type.
- *Maintenance:* Improves long-term — old firmware is never retrofitted; only new versions are added going forward.
- *Security:* None directly; this is the prerequisite that makes ADR-002's health fields addable without a breaking change.
- *Scalability:* Enables the multi-device-type platform ambition without repeated breaking migrations.
- *Cost:* One-time engineering cost for the version-dispatch logic, amortized over the product's life.

**Compatibility:** Storage — yes, changes the SD row format. Protocol — yes, adds a top-level `schema_version`/`record_type` discriminator to the wire JSON. Configuration — no. OTA — no direct mechanism impact, but this is the rule that governs safe OTA rollout of any future data-model change. Fleet — yes, a fleet always contains a mix of schema versions in normal operation, by design. Backward compatibility — receiver supports exactly current + previous version, permanently, as an ongoing operating rule, not a one-time transition.

**Migration Strategy:** Any device predating this ADR must fully drain its SD queue to empty under its current firmware before receiving the OTA that introduces this schema. This is a one-time, one-directional cutover. Every schema change from this point forward follows the current+previous rule and requires no forced-drain step.

**Definition of Done:** `schema_version` and `record_type` fields exist in both the SD row and the wire JSON; the reference receiver is demonstrated accepting a single batch containing a mix of the current and previous schema version; a canonical schema registry document exists, listing every schema_version/record_type pair and its exact field layout.

**Future Extension:** New sensor types are added as new `record_type` values without touching existing types' handling. New optional data (health fields, diagnostics) is added by introducing a new `schema_version`, never by mutating an already-published version's layout.

---

## ADR-002 — Device Health & Diagnostics Channel

**Decision Name:** Unified Health/Heartbeat Record

**Problem Statement:** There is no way to remotely determine why a silent device stopped reporting. Every plausible cause (WiFi, sensor, server, storage, firmware, OTA, power, configuration) currently produces the identical observable symptom: silence.

**Business Goal:** Convert "it stopped sending data" from an undiagnosable event into a remotely triageable one, for every failure category, without a site visit.

**Architecture Decision:** A new `record_type = HEALTH` is defined under ADR-001's schema. It is generated once every 60 seconds, on its own independent timer, and carries: uptime, free heap, reset reason, a persisted watchdog-reset counter, SD status (OK / ABSENT / WRITE_FAIL / LOW_SPACE), SD free-space estimate, current unacknowledged queue depth, WiFi RSSI, the device's own last-confirmed ack sequence number, OTA state (none / pending-verify / confirmed / last-failed), and the sensor-plausibility flag (ADR-015). A HEALTH record is generated into, queued in, acknowledged through, and retried by the exact same durable SD-queue-and-push pipeline as ordinary telemetry — it is not a separate channel, endpoint, or transport. It carries no secrets (API key, WiFi password are explicitly excluded).

**Reasoning:** Reusing the existing durable queue/ack pipeline gives HEALTH records the same durability, ordering, and retry guarantees as telemetry for free, and means a HEALTH record generated moments before a crash is not lost — valuable for post-mortem diagnosis. A dedicated 60-second cadence, decoupled from the 1-second telemetry rate, matches how slowly these values actually change without materially inflating storage or bandwidth.

**Alternatives Rejected:**
- *A separate dedicated health endpoint/channel* — rejected: would duplicate the retry/ack/queue logic that the existing pipe already solves, for no benefit.
- *MQTT-based heartbeat* — rejected: introduces a new protocol/dependency inconsistent with the already-frozen polling-only design.
- *Piggybacking health fields onto every 1-second telemetry record* — rejected: inflates payload size 60-fold for data that does not need second-level resolution.
- *On-demand health only, requested by the server* — rejected: requires an inbound channel the device does not have, and fails exactly when needed most (device already unreachable).

**Trade-offs:**
- *Performance:* One extra small row every 60 seconds — roughly 1.7% overhead relative to telemetry volume.
- *Reliability:* Major improvement — this is the architectural answer to the entire Task 9 diagnostic gap.
- *Complexity:* Moderate — one new record_type and timer, fully reusing existing plumbing.
- *Maintenance:* Improves — a single, canonical source of "is this device OK."
- *Security:* None directly; secrets are explicitly excluded from the payload.
- *Scalability:* Flat, tiny per-device overhead; negligible at any fleet size relative to telemetry volume.
- *Cost:* Low, given full reuse of existing transport.

**Compatibility:** Storage — yes, new record_type. Protocol — yes, additive under ADR-001's versioning rule. Configuration — no. OTA — reports OTA state, no mechanism change. Fleet — this is the primary enabling data for fleet visibility (ADR-016). Backward compatibility — fully additive; a receiver encountering an unrecognized record_type must store it raw and continue, never reject the batch.

**Migration Strategy:** None required beyond ADR-001's own migration — purely additive once the schema versioning exists.

**Definition of Done:** HEALTH record type documented in the schema registry; firmware durably emits one every 60 seconds; the reference receiver stores and surfaces every field on a per-device diagnostics view; every row of the Task 9 diagnostic matrix is re-evaluated and answerable without a site visit.

**Future Extension:** New health fields are added only via a new schema_version of the HEALTH record_type — e.g., a future cellular-modem signal-quality field is a new version, not a change to the existing one.

---

## ADR-003 — SD Queue Storage Redesign: Framing, Rotation, Read Cursor

**Decision Name:** Truncate-Before-Append Framing with Segmented Rotation

**Problem Statement:** A torn write during an SD append permanently misaligns every subsequent record read, which can cause the queue's "empty" check to falsely report true and trigger deletion of unacknowledged data. Separately, reading the queue rescans from the beginning of the file on every push cycle, and the single-file, all-or-nothing compaction model makes both the corruption blast radius and the recovery cost unbounded.

**Business Goal:** Make the "zero silent data loss" claim actually true under real power-loss conditions, and make outage-recovery time bounded and proportional to the true backlog, not to lifetime device history.

**Architecture Decision:** Three decisions, together:
1. **Framing:** the device persists a "last known good write offset" for the queue, in the same dual-slot CRC checkpoint mechanism already used for the totalizer. Before every append, the device compares the file's actual size to this persisted offset; if the file is larger (a torn write occurred), it truncates the file back to the last known good offset before appending. No row is ever appended on top of unconfirmed bytes, and no resynchronization scanning is ever required.
2. **Rotation:** the single unbounded log file is replaced with fixed-size segment files, each capped at 10,000 rows, created in increasing numbered order. A segment is deleted in its entirety the moment all of its rows are acknowledged — compaction is no longer all-or-nothing across the entire history.
3. **Read cursor:** the acknowledgment record (already dual-slot CRC) is extended with the exact segment and offset of the first not-yet-acknowledged row. This position is recomputed and persisted only when the server confirms a new cumulative ack — never on a mere read attempt — and every read begins from this position, never from the start of the file. Additionally, the fixed post-push wait is removed for full batches: if a push returns a full batch, the device immediately attempts another push (bounded at ten consecutive batches per loop pass) rather than waiting the full period, so catch-up drains at network speed rather than an artificial fixed rate.

**Reasoning:** Truncate-before-append is deterministic and reuses an already-proven pattern (dual-slot CRC) rather than inventing a probabilistic byte-scanning resync mechanism that could be fooled by a coincidental match in torn data. Persisting the cursor only on confirmed-ack advancement (not on read) is the specific detail that keeps unacknowledged rows from ever being skipped — this is the single most important correctness property in this ADR and is stated explicitly to remove any ambiguity for whoever implements it. Segmenting by fixed row count bounds both corruption blast radius and worst-case scan cost to one segment, independent of how long the device has been running.

**Alternatives Rejected:**
- *Byte-by-byte magic-word resync scanning after corruption* — rejected: probabilistic, adds ongoing read cost, and only mitigates blast radius after the fact rather than preventing it.
- *A journaling filesystem (e.g., LittleFS) replacing raw FAT32 access* — rejected: a disproportionate platform change that also loses the operational property of a plain FAT32 card being readable and inspectable on any PC.
- *A single unbounded file with periodic full compaction (today's design)* — rejected: already shown to cause unbounded worst-case scan cost and all-or-nothing cleanup.
- *An embedded database (e.g., SQLite) on the SD card* — rejected: far more complexity and flash-write overhead than this append-heavy, sequential-read workload justifies.

**Trade-offs:**
- *Performance:* Significant improvement — amortized scan cost drops from O(n²) to proportional to true backlog; adaptive draining removes the artificial catch-up ceiling.
- *Reliability:* Major improvement — eliminates the torn-write data-loss defect entirely.
- *Complexity:* Moderate increase — new persisted cursor fields, segment-file lifecycle management.
- *Maintenance:* Improves — segment files are individually inspectable, bounded slices of history.
- *Security:* None directly.
- *Scalability:* Bounds worst-case recovery time regardless of outage length.
- *Cost:* Moderate one-time engineering cost, justified by this being the single P0 storage finding.

**Compatibility:** Storage — yes, major change to on-disk layout and checkpoint/ack structures. Protocol — no. Configuration — no. OTA — no direct mechanism impact. Fleet — no direct impact. Backward compatibility — major migration (see below).

**Migration Strategy:** Any device with an existing single-file queue must fully drain to empty under its current firmware before the OTA introducing segmented storage is applied — a one-time forced-drain cutover, coordinated together with ADR-001's own migration since both touch overlapping structures, in practice delivered as one combined release.

**Definition of Done:** A power-cut-during-append bench test confirms no more than the single most-recent unconfirmed row is ever lost; a simulated multi-day-outage bench test confirms drain time is bounded by network throughput alone; a bench soak test confirms correct segment creation and deletion across a full fill-drain lifecycle.

**Future Extension:** The segment row-cap is a single tunable constant. The mechanism is unchanged by future record types or sizes introduced under ADR-001, since segmenting is by row count, not content.

---

## ADR-004 — SD Health & Full-Card Monitoring

**Decision Name:** SD Degradation State Machine

**Problem Statement:** There is no free-space monitoring, no runtime re-check of card presence, and a card failure at boot causes an unrecoverable halt with WiFi/OTA never started, while a card failure mid-run degrades silently.

**Business Goal:** No SD-related failure should ever be indistinguishable from the device simply being powered off, and none should require a site visit purely to learn that a visit is needed.

**Architecture Decision:** A boot-time SD failure no longer halts the device. Instead, `SD.begin()` is retried every 30 seconds indefinitely, while WiFi, sync, and OTA all start and run normally regardless of SD state. A device with no working SD card still connects to WiFi and reports a HEALTH record (ADR-002) with `sd_status = ABSENT`, sent best-effort/non-durable (since there is nowhere to durably queue it) — an explicit, deliberate, and sole exception to the "durability before transmission" principle, justified because the alternative is total silence. If SD fails mid-run after a successful boot, the device enters an `SD_DEGRADED` state: it stops attempting further telemetry writes to the known-broken filesystem, continues reporting `sd_status = WRITE_FAIL` via the same best-effort path, and retries `SD.begin()` every 30 seconds, resuming normal durable operation automatically the moment the card is confirmed restored. On every HEALTH cycle, free space is checked; below 5% free or 50 MB free (whichever is larger), `sd_status = LOW_SPACE` is reported — this is an advance-warning signal only and triggers no behavior change, since ADR-003's segmented rotation already bounds worst-case growth given appropriately sized media.

**Reasoning:** A boot-time failure and a runtime failure are treated differently because, at boot, nothing durable has started yet (fail-loud was defensible); at runtime, WiFi/OTA/telemetry-so-far are already valuable and a remote fix (OTA) could not even be delivered to a device that halts. Making both cases visible via the health channel, rather than silent, is what actually closes the diagnostic gap.

**Alternatives Rejected:**
- *Keep the infinite unconditional halt on SD failure* — rejected: guarantees a silent, undiagnosable, truck-roll-requiring failure, directly contradicting ADR-002's purpose.
- *Buffer telemetry in RAM while SD is unavailable* — rejected: RAM cannot cover any meaningful outage duration; pretending otherwise creates a false durability expectation. Data during an SD-absent window is genuinely and honestly lost, made visible via `sd_status`, rather than silently and unreliably half-mitigated.
- *Halt entirely on runtime SD failure, matching the boot-time philosophy* — rejected: disables OTA precisely when a remote fix might be needed most.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* Significant improvement — no SD failure mode is silent or unrecoverable-without-a-visit anymore.
- *Complexity:* Moderate — a new explicit state machine and a best-effort transmission exception.
- *Maintenance:* Improves — one clearly named state machine replaces implicit silent-continue behavior.
- *Security:* None directly.
- *Scalability:* None directly.
- *Cost:* Moderate.

**Compatibility:** Storage — defines new status semantics consumed by ADR-002's health record. Protocol — uses the existing health record, no new endpoint. Configuration — no. OTA — a device in any degraded SD state must still be able to receive OTA updates, since OTA does not require SD; this is an explicit, required property, not an assumption. Fleet — yes, core fleet-visibility data. Backward compatibility — additive, no data migration.

**Migration Strategy:** None required — new runtime behavior layered onto existing paths, not a data-format change.

**Definition of Done:** A bench test pulling the SD card mid-run confirms continued health reporting with `sd_status = WRITE_FAIL`; a bench test booting with no SD card confirms WiFi connects and `sd_status = ABSENT` is reported; a low-space bench test confirms the threshold fires correctly.

**Future Extension:** Additional `sd_status` values are added under a new schema_version (ADR-001) without altering the state machine's transitions.

---

## ADR-005 — Transport & Firmware Security Hardening

**Decision Name:** TLS, Secure Boot, and Per-Device Key Provisioning

**Problem Statement:** All four API calls use plaintext HTTP with no certificate validation; there is no firmware image signing or Secure Boot; the reference receiver enforces no authentication; secrets sit unencrypted in NVS; a single static, shared API key ships in source control.

**Business Goal:** Close the two P0 security findings — MITM firmware substitution and unverified code execution — with a scheme that is provisionable at real manufacturing scale, not a manual per-unit bench step.

**Architecture Decision:** All four endpoints move to HTTPS with a pinned CA certificate (the device trusts only the specific issuing CA for its known, single server host — not the general public CA ecosystem). ESP-IDF Secure Boot V2 and Flash Encryption are enabled, burned into each device's eFuses during the first factory USB flash, using RSA-3072 signing keys generated and held by Covio and never distributed to the field or committed to source control. Every device receives a unique, cryptographically random API key, generated during factory testing (ADR-008) and written into NVS during that same factory session — the shared `DEFAULT_API_KEY` placeholder never leaves the factory floor in a release build. NVS Flash Encryption protects secrets at rest against direct flash reads. The serial console continuing to echo the live API key/WiFi password in cleartext to a physically-present operator is retained deliberately, not as a residual gap — physical possession is already the architecture's own stated full-trust boundary and its own recovery mechanism.

**Reasoning:** CA-pinning is chosen over full public-CA trust because the device only ever talks to one known host, making pinning both simpler (no root-store bundle needed) and stricter (rejects any other CA-issued certificate). Secure Boot V2 (not V1) is the currently-supported, actively maintained scheme that composes correctly with Flash Encryption and OTA. Because Secure Boot and Flash Encryption depend on one-way eFuses, they must be burned at the very first factory flash — there is no later, OTA-deliverable path to enable them, which is why factory provisioning (ADR-008) is the anchor point for this entire ADR.

**Alternatives Rejected:**
- *`setInsecure()` / no certificate validation* — reaffirmed as permanently forbidden in any release build.
- *Mutual TLS (per-device client certificates)* — rejected: adds real provisioning and rotation complexity disproportionate to the actual threat model of a fleet of individually-low-value IoT sensors, given a pinned-CA server plus a strong per-device API key already closes the practical risk.
- *Secure Boot V1* — rejected: superseded by V2's actively maintained RSA-PSS scheme in current ESP-IDF/Arduino-core releases.
- *An application-level (non-hardware) signature check without chip-level Secure Boot* — rejected: enforceable only if an attacker's own malicious firmware chooses to honor the check; hardware Secure Boot moves enforcement below anything an attacker's code could disable.
- *Automatic periodic key rotation* — deferred, not rejected outright: the priority here is ensuring a unique key exists per device at all; rotation is a valuable later refinement, not required to close the P0 findings.

**Trade-offs:**
- *Performance:* Modest TLS handshake overhead per request, acceptable given request cadences measured in seconds/minutes.
- *Reliability:* Neutral-to-positive — CA-pinning fails closed by design (correct behavior, not a defect, even though it means server-side certificate rotation must be coordinated ahead of expiry).
- *Complexity:* Significant — the most complex ADR in this document, touching factory process, chip eFuses, and per-device secret tracking.
- *Maintenance:* Ongoing CA-rotation coordination becomes a recurring operational item.
- *Security:* The single largest security improvement in the entire architecture.
- *Scalability:* Fully compatible with any fleet size — factory-time key generation scales linearly with manufacturing throughput.
- *Cost:* Real one-time cost (factory process, key-management infrastructure) plus a small recurring cost (CA rotation).

**Compatibility:** Storage — no. Protocol — yes, all four endpoints move to `https://`. Configuration — yes, the API key is now factory-provisioned, not a shared default. OTA — yes, `HTTPUpdate` moves to a secure client with the pinned CA. Fleet — yes, every device needs a valid, non-expired pinned CA. Backward compatibility — major migration for transport; Secure Boot/Flash Encryption are not retrofittable to already-manufactured devices.

**Migration Strategy:** Any already-deployed pre-hardening device receives exactly one final plain-HTTP OTA that switches it to HTTPS with the pinned CA and updates `server_url` to the `https://` form — the last plain-HTTP update it will ever receive. Any such device without a unique API key has one generated and pushed via the console, a manual per-device operation acceptable for a small pre-existing pilot fleet. Secure Boot/Flash Encryption cannot be retrofitted at all; this must be communicated clearly to anyone holding pre-ADR hardware, which is treated as bench-only forever.

**Definition of Done:** All four API calls verified over HTTPS against a real certificate in a bench test; a MITM bench test (untrusted intercepting proxy) is verified rejected; Secure Boot and Flash Encryption verified enabled via an eFuse summary on a factory-flashed unit; an incorrectly signed OTA image is verified rejected by the bootloader; no device leaves the factory floor with the default API key active.

**Future Extension:** CA rotation is handled by a firmware update that carries both the new and old pinned CA for an overlap window, trying the new one first — this pattern is decided now so a future CA expiry does not require inventing an approach under time pressure.

---

## ADR-006 — Reliability Hardening: Watchdog & Boot Safety

**Decision Name:** Application Watchdog with Boot-Time Reset-Reason Reporting

**Problem Statement:** No application-level watchdog is configured; brownout handling is an unverified toolchain default; SD failure at boot causes an unrecoverable halt.

**Business Goal:** No single hang, brownout, or SD failure may leave a fielded device both non-functional and invisible/unrecoverable without a physical visit.

**Architecture Decision:** The ESP-IDF Task Watchdog Timer is enabled on the main loop task with a 15-second timeout, fed once per loop iteration at the very top of the loop (immediately after the console service call), so a hang anywhere later in that same iteration is still caught. The OTA download loop explicitly feeds the watchdog during its own operation, since a large binary download can exceed 15 seconds. `watchdog_reset_count`, persisted in NVS, is incremented on the boot immediately following a detected watchdog reset (detected via the standard reset-reason API), not inside the watchdog handler itself. The hardware brownout detector is left at its default threshold, explicitly verified (not silently assumed) enabled in the release build configuration; a brownout-triggered reset is reported as a `reset_reason` value through the same health channel (ADR-002), giving fleet-wide visibility into brownout frequency without new application logic or circuitry. SD-init failure at boot follows ADR-004's retry policy rather than halting.

**Reasoning:** Fifteen seconds is comfortably larger than the longest existing blocking call (an 8-second HTTP timeout) while still short enough to keep a hang-and-recover cycle visible within the 60-second health-reporting interval. Incrementing the reset counter on the next boot (rather than inside the watchdog panic handler) is chosen because the timing window during an in-progress watchdog panic is not reliably long enough to guarantee a successful NVS write — the boot-time-detection approach is simpler and more certain.

**Alternatives Rejected:**
- *No application-level watchdog, relying on whatever the board package's default provides* — rejected: exactly the unverified-default posture already flagged; an explicit, owned configuration is required so behavior does not silently change across core-version upgrades.
- *A very short watchdog timeout (2–3 seconds)* — rejected: too close to the existing 8-second push timeout, causing false-positive resets under normal slow-network conditions.
- *Incrementing the reset counter inside the watchdog panic handler* — rejected: the panic window is not reliably long enough for a certain NVS write.
- *Changing the hardware brownout threshold* — rejected: no evidence the default is wrong for this hardware; changing it without a specific, measured reason would be speculative.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* Significant improvement — closes the no-watchdog gap entirely and gives brownout-frequency visibility.
- *Complexity:* Low — a standard, well-documented API pattern.
- *Maintenance:* Improves — reset-reason visibility turns anecdote into fleet-wide trackable data.
- *Security:* None directly.
- *Scalability:* None directly (per-device, no fleet-size dependency).
- *Cost:* Low.

**Compatibility:** Storage — no. Protocol — uses ADR-002's health record, no new channel. Configuration — no. OTA — the download loop must explicitly feed the watchdog, a required exception noted here so it is not missed during implementation. Fleet — yes, reset-reason/watchdog-count become fleet-wide reliability metrics. Backward compatibility — additive.

**Migration Strategy:** None required — new runtime behavior delivered via a normal OTA update.

**Definition of Done:** A bench test with a deliberately hung main loop confirms a watchdog reset within 15 seconds, reported correctly on the next boot; a bench test confirms the watchdog does not spuriously trigger during a large OTA download over a throttled connection.

**Future Extension:** The 15-second timeout is a single tunable constant. Per-task watchdog registration can be added later if additional FreeRTOS tasks are introduced, without changing the reporting mechanism.

---

## ADR-007 — Configuration Lifecycle Management

**Decision Name:** Versioned NVS Schema with Bounded Change History

**Problem Statement:** Only calibration configuration is versioned; there is no versioning for endpoint/API-key/WiFi settings, no configuration backup/export, no NVS schema migration path, and no defined behavior for re-pointing a device with a non-empty queue to a different receiver.

**Business Goal:** Make configuration changes safe, traceable, and non-destructive across firmware upgrades and receiver changes, eliminating the risk of an OTA update silently corrupting or misinterpreting an older device's NVS layout.

**Architecture Decision:** A single `config_schema_version` NVS key, separate from the existing calibration-only version, is introduced. On boot, the persisted value is compared against the firmware's compiled-in current version; if older, a recorded, versioned migration step runs once before continuing boot; if newer (a downgrade), the device logs a warning and falls back to defaults for any unrecognized keys rather than failing to boot. Every console-driven change to endpoint/API-key/WiFi settings is appended to a bounded, ring-buffered, on-device configuration-change history (last 20 entries, timestamped by boot_id since no wall clock exists), retrievable via a new `history` console command — a lightweight support-triage trail, not a compliance-grade audit log (that requirement is reserved for ADR-010, which has different stakes). Re-pointing a device with a non-empty queue to a different receiver is explicitly declared safe and intended: queued rows are delivered under the same `device_id`/`seq` values as always, and the receiving server is responsible for treating an unfamiliar device with a high initial `seq` as entirely normal. `seq` is never reset when `server_url` changes.

**Reasoning:** An explicit version number with a recorded migration changelog is far more auditable than the current ad hoc `isKey()`-based defensive pattern. A lightweight on-device history complements (not replaces) server-side records, since a device that has been offline through a migration would otherwise have no local record of its own configuration history. Explicitly forbidding a `seq` reset on re-pointing is stated because it is the natural, tempting "fix" a future engineer might otherwise apply, and it would reintroduce the exact collision risk the global-monotonic `seq` design exists to avoid.

**Alternatives Rejected:**
- *No schema versioning, relying on scattered `isKey()` checks* — rejected: the exact fragile, undocumented pattern already flagged as a risk.
- *Full server-side-only audit trail, nothing recorded on-device* — rejected: a device offline during the audit period, or moved between receivers, would lose all record of its own history; a lightweight on-device trail is a cheap complement.
- *Resetting `seq` to 1 on any `server_url` change* — rejected: reintroduces collision risk against the new receiver's own historical records for that device_id.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* Improves — OTA updates that touch the NVS key set become a designed, tested migration path.
- *Complexity:* Low-moderate.
- *Maintenance:* Significantly improves — a documented, versioned changelog is far easier to reason about years later than implicit key checks.
- *Security:* None directly.
- *Scalability:* None directly.
- *Cost:* Low.

**Compatibility:** Storage — no, NVS only. Protocol — no. Configuration — yes, this is the configuration versioning mechanism itself. OTA — yes, every future OTA that changes the NVS key set must include a recorded migration step. Fleet — no direct impact. Backward compatibility — additive; a device with no `config_schema_version` present is treated as version 0 and migrated forward through every recorded step.

**Migration Strategy:** Existing pre-ADR devices are implicitly at schema version 0. The first OTA including this ADR performs a no-op 0→1 migration (since version 0 is today's actual layout) and stamps version 1; every future change follows the same recorded pattern.

**Definition of Done:** A bench test upgrading a device from a pre-ADR NVS state confirms correct migration to version 1 with no data loss; the `history` command is verified across a reboot; a re-pointing bench test confirms a device with a non-empty queue successfully delivers its backlog to a newly configured receiver with no seq collision.

**Future Extension:** Each future configuration-schema change is a new numbered migration step appended to the same changelog; the mechanism itself never changes.

---

## ADR-008 — Provisioning & Commissioning Platform

**Decision Name:** Factory Self-Test Firmware and Console-Based Field Verification

**Problem Statement:** There is no factory self-test procedure, no per-device unique-secret injection process, and no field-commissioning verification step distinct from the engineering bench test; installers cannot commission a device without engineering support.

**Business Goal:** Manufacture and commission units at real production volume without an engineer present at the factory line or the customer site, at an engineering investment proportional to actual near-term fleet scale.

**Architecture Decision:** A dedicated factory-test firmware build (a compile-time variant of the same codebase) is flashed first on every unit. It runs a fixed, automatic sequence at boot with no operator interaction: verifies the SD card is present and writable via a real write-read-CRC cycle, verifies WiFi association to a factory-floor test network, and verifies the pulse counter responds to a factory-jig-applied test signal (reusing the existing simulated-pulse jumper pattern already built into the hardware for bench testing). It reports a single-line, machine-parseable PASS/FAIL over Serial for the factory line's test-station software to log. On PASS, the factory station writes a freshly generated unique API key and the production server URL into NVS using the existing serial console commands — factory provisioning reuses the same console mechanism already built; it does not invent a new channel. The production firmware is flashed as the final step before shipment. In the field, the installer uses the existing serial console plus one new command, `verify`, which confirms WiFi connectivity, confirms the server responds to a config-poll (proving the API key and URL are correct), and samples the live pulse count twice five seconds apart so the installer can visually confirm counting behavior before leaving site. AP/BLE/QR-based commissioning is explicitly deferred, not built, in this release.

**Reasoning:** Reusing the existing console for both factory and field provisioning avoids inventing a second provisioning mechanism; the only new work is the automated factory self-test sequence and the field `verify` command, both of which are small, targeted additions on top of infrastructure that already exists. Deferring a mobile/BLE app is a deliberate scope decision: it is real future value, but a large, separately-justified investment not required to close the commissioning gap for a first commercial release at moderate fleet scale.

**Alternatives Rejected:**
- *A full mobile app with BLE provisioning, built now* — rejected for this release: disproportionate engineering investment relative to near-term fleet scale; recorded as a deliberate future extension, not silently dropped.
- *Continuing today's manual, ad hoc factory bring-up* — rejected: does not scale past hand-built bench units and leaves no manufacturing QA record.
- *Requiring engineering involvement for every field commissioning* — rejected: does not scale operationally.

**Trade-offs:**
- *Performance:* None directly.
- *Reliability:* Improves manufacturing QA (a PASS/FAIL record now exists) and reduces bad-install risk.
- *Complexity:* Moderate — one new firmware build variant and one new console command.
- *Maintenance:* Low ongoing cost, reusing existing console infrastructure.
- *Security:* The factory-test image must never be the image that ships to a customer — a build/release-process control owned by ADR-013.
- *Scalability:* Factory provisioning now scales with manufacturing line throughput, not engineer availability.
- *Cost:* Moderate for this release; the deferred BLE/app investment is a larger, separately scoped future cost.

**Compatibility:** Storage — no. Protocol — no. Configuration — reuses the existing console mechanism. OTA — no. Fleet — factory PASS/FAIL records feed the asset record established by ADR-009. Backward compatibility — not applicable; this is a manufacturing/installation process change, not a change to already-deployed devices.

**Migration Strategy:** Not applicable — this changes process going forward and has no effect on already-fielded devices.

**Definition of Done:** A factory-test firmware build is verified on the bench to PASS a known-good unit and FAIL a unit with a deliberately disconnected SD card or sensor signal; the `verify` console command is demonstrated in a bench commissioning rehearsal; documented factory-floor and field-commissioning procedures exist referencing these exact commands.

**Future Extension:** A mobile/BLE/QR commissioning tool can be added later as an additional front-end to the same underlying configuration setters the console already uses, without redesigning the provisioning data model.

---

## ADR-009 — Device & Asset Identity Model

**Decision Name:** Separate Hardware Identity from Logical Asset Identity

**Problem Statement:** The only identity is a MAC-derived `device_id`. A controller board repair creates a brand-new, historyless device from the server's point of view, with no relink or merge workflow anywhere.

**Business Goal:** Make hardware repair and replacement a routine, low-friction support operation that preserves the customer-visible asset's continuity, without inventing a fragile new device-side continuity mechanism.

**Architecture Decision:** Two distinct identifiers are frozen: `hardware_id` (unchanged, the existing MAC-derived identifier, permanently tied to one physical controller), and a new `asset_id`, a human-assignable identifier set by the installer via a new console command (`set asset <id>`) during field commissioning, representing the physical installation. Every push payload's envelope includes both fields. When a controller is replaced, the installer sets the same `asset_id` on the new board; the SD card is treated as part of the replaceable controller assembly, not a separately tracked component, so the new controller starts its own SD-side checkpoint history from zero exactly as the existing recovery logic already handles a fresh card. The receiver aggregates an asset's total cumulative litres as the sum across every `hardware_id` ever associated with that `asset_id`, rather than requiring one continuous device-side sequence across a hardware swap. A new `decommission` console command performs a factory reset and additionally zeroes the SD queue/checkpoint contents before deletion; unlinking the `asset_id` from a customer record is a separate, receiver-side administrative action.

**Reasoning:** Aggregating cumulative litres by `asset_id` on the receiver side achieves the customer-visible continuity goal without requiring any new device-side mechanism to carry a totalizer value across a hardware swap — which would require fragile reconciliation between a reset `boot_id` and a transplanted `seq` history. This is deliberately the simpler of the two available approaches.

**Alternatives Rejected:**
- *Transplanting/preserving the SD card's history across a controller swap* — rejected: requires new reconciliation logic between a fresh controller's `boot_id` and a transplanted card's `seq` state, for a benefit already achievable via receiver-side aggregation.
- *Using `hardware_id` as the sole identity with ad hoc manual relinking* — rejected: the status quo already identified as causing lost history on every repair.
- *A cryptographically issued asset identity (e.g., a signed certificate at commissioning)* — rejected as disproportionate: `asset_id` is a display/aggregation key, not a security boundary; security is already handled by ADR-005's per-device API key.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* None directly.
- *Complexity:* Low — one new NVS field, one new console command, one new envelope field.
- *Maintenance:* Significantly improves support/repair operations.
- *Security:* None directly; `asset_id` is not a secret and is not used for authentication.
- *Scalability:* None directly.
- *Cost:* Low.

**Compatibility:** Storage — no, NVS and envelope only, not the SD row format. Protocol — yes, additive envelope field. Configuration — yes, new NVS key. OTA — no. Fleet — yes, this becomes the primary key fleet-management views should organize around, not `hardware_id`. Backward compatibility — additive; an empty `asset_id` is a valid, expected "unassigned" state.

**Migration Strategy:** Newly commissioned devices receive an `asset_id` at commissioning time via ADR-008's `verify` workflow. Devices commissioned before this ADR existed retain no `asset_id` until an operator runs `set asset <id>` once during a scheduled maintenance touch — no forced migration is required, and the receiver must tolerate unassigned devices indefinitely, not just during a transition window.

**Definition of Done:** `set asset <id>` is implemented and verified; the push envelope carries both identifiers; a bench test simulating a controller replacement (fresh NVS and SD, same `asset_id` set manually) confirms the reference receiver correctly aggregates cumulative litres across both `hardware_id`s under one `asset_id`; `decommission` is verified to zero SD contents before deletion.

**Future Extension:** `asset_id` assignment can later be automated (e.g., via a QR code scanned by a future commissioning app) without changing the underlying data model — it is simply a different way of invoking the same mechanism.

---

## ADR-010 — Calibration Governance & Audit Trail

**Decision Name:** Immutable Calibration History; Removal of Unimplemented Temperature Fields

**Problem Statement:** `density` and `T_ref` are persisted, cached, and transmitted end-to-end but never applied in any litre calculation anywhere. Separately, any dashboard user can retroactively rewrite all historical computed litres with no record of who changed what, when, or why — a real integrity risk for a metering device.

**Business Goal:** Eliminate a false "temperature compensation exists" product claim, and make every retroactive change to billing-relevant history permanently attributable, without over-scoping this decision into an approval-workflow design that depends on business decisions this document is not positioned to make.

**Architecture Decision:** `density` and `T_ref` are removed from the calibration schema and the config-poll wire contract entirely; `K_factor` and `version` remain as the only calibration fields. If temperature compensation becomes a real requirement later, it is reintroduced deliberately as a new, versioned capability with an explicitly specified formula (a new schema_version under ADR-001), never by resurrecting the removed, never-implemented fields. Separately, every calibration change on the receiver is recorded as an immutable, append-only history row (old value, new value, new version, receiver-side wall-clock timestamp) — the current-value calibration table is retained as a fast-lookup view derived from this history, not the primary record. The existing behavior of retroactively recomputing all historical litres when K changes is retained unchanged, as the deliberate, core product feature it already is; this ADR makes every such change permanently attributable after the fact, without adding an approval/sign-off gate.

**Reasoning:** Carrying unused fields forward perpetuates a specific, already-identified false claim with no committed implementation date — worse than removing them now and reintroducing deliberately, with a real formula, later. Inventing a temperature-correction formula now would require a domain-specific physical model this review has no basis to specify, and risks freezing the wrong one. An approval workflow for calibration changes depends on an organizational decision (who has approval authority) outside this document's scope, so it is deferred explicitly rather than assumed.

**Alternatives Rejected:**
- *Keep `density`/`T_ref` "for future use," unimplemented* — rejected: perpetuates a specific false claim with no committed date to fix it.
- *Implement a generic temperature-correction formula now using the existing fields* — rejected: a domain/product decision outside architectural scope; risks freezing an incorrect formula.
- *A two-person approval workflow for calibration changes* — rejected for this freeze: depends on an organizational authority decision this document cannot make; explicitly deferred.
- *Making the calibration table itself append-only with no fast current-value lookup* — rejected: unnecessarily complicates every existing read path for no benefit over a derived materialized view.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* None directly.
- *Complexity:* Low-moderate on the receiver; firmware is simplified by fewer fields to cache/transmit.
- *Maintenance:* Improves — removing dead fields reduces confusion for future engineers.
- *Security:* Improves record-integrity posture for a billing-adjacent metric, without over-committing to an unscoped approval workflow.
- *Scalability:* None directly.
- *Cost:* Low.

**Compatibility:** Storage — no SD-side impact. Protocol — yes, `density`/`T_ref` removed from the config-poll response (a minor migration, since neither was ever consumed by any calculation). Configuration — yes, corresponding NVS fields removed. OTA — no. Fleet — no direct impact. Backward compatibility — minor; the practical blast radius is limited since nothing in this codebase's own receiver or firmware ever consumed the removed fields.

**Migration Strategy:** The receiver drops or ignores the `density`/`T_ref` columns as part of the same release that introduces the append-only audit-history table. The device simply stops sending/caching them on its next OTA — no data-loss risk, since the server-side computation never used them.

**Definition of Done:** The config-poll response no longer includes `density`/`T_ref`; a bench test verifies that a sequence of at least three K-factor changes on the reference receiver produces a correct, retrievable immutable audit history alongside the updated current value.

**Future Extension:** A real temperature-correction feature, if required later, is introduced as a new schema_version with its own explicitly specified formula and field names, never a resurrection of the removed fields. An approval workflow, if required later, is layered onto the append-only table as additional columns, without changing the recording mechanism itself.

---

## ADR-011 — Fleet-Scale Timing Hygiene

**Decision Name:** Per-Boot Timer Jitter

**Problem Statement:** No jitter exists on the push, config-poll, or OTA-poll timers. A fleet-wide simultaneous event (mass power restoration, mass OTA-triggered reboot) leaves every device's timers phase-aligned, producing a recurring synchronized load spike against the receiver that worsens as fleet size grows.

**Business Goal:** Ensure a fleet-wide reconnection event never produces a synchronized load spike against the receiver, at any fleet size.

**Architecture Decision:** At boot, after identity is established, the device computes a single random offset in the range zero to one-quarter of each affected timer's period (push, config-poll, OTA-poll, and the ADR-002 health timer), seeded from the ESP32 hardware random-number generator already used for WiFi-reconnect jitter, and applies that offset only to each timer's first firing after boot. Every subsequent firing of that timer remains on its normal fixed period.

**Reasoning:** The actual risk is synchronized reboots, not general timer drift — a single per-boot offset is sufficient to break fleet-wide synchronization after a mass-reboot event, while keeping the timer's steady-state cadence fully predictable, which ADR-003's segment-fill-rate assumptions and general operational reasoning both depend on.

**Alternatives Rejected:**
- *Per-cycle re-randomization of every firing* — rejected: unnecessary given the actual risk is reboot synchronization, not general drift; would make timing behavior harder to reason about for capacity planning and diagnosis.
- *No jitter, relying on the receiver to absorb bursts* — rejected: pushes the entire burden onto receiver infrastructure for a problem cheap to solve at the source.
- *A server-assigned per-device stagger schedule* — rejected: adds a network round-trip dependency for something that must work correctly from the very first boot, including before a device has ever reached the server.

**Trade-offs:**
- *Performance:* Negligible.
- *Reliability:* None directly.
- *Complexity:* Low — one extra per-boot random-offset computation per timer.
- *Maintenance:* None directly.
- *Security:* None directly.
- *Scalability:* Directly improves — this is specifically a fleet-scale mitigation.
- *Cost:* Very low.

**Compatibility:** Storage — no. Protocol — no. Configuration — no. OTA — no. Fleet — yes, this is a fleet-scale mitigation. Backward compatibility — fully additive, invisible to any external consumer.

**Migration Strategy:** None required — purely internal timing behavior, delivered via a normal OTA update.

**Definition of Done:** A bench test or simulation booting twenty simulated instances at an identical reference time confirms their first push/config/OTA-poll firings spread across the expected window rather than landing simultaneously.

**Future Extension:** The one-quarter-period jitter fraction is a single tunable constant, adjustable later if a future fleet-scale receiver architecture determines a different spread is optimal.

---

## ADR-012 — Diagnostics & Remote Log Retrieval

**Decision Name:** On-Demand Log Ring Buffer Retrieval

**Problem Statement:** No historical logging exists beyond a live Serial connection; there is no way to remotely retrieve a specific device's recent operational history.

**Business Goal:** Let support retrieve a specific device's recent operational history on demand, without a site visit, without proactively streaming logs from every device regardless of need.

**Architecture Decision:** A fixed-size circular buffer of the last 500 tagged log lines is added on the SD card, using the same fixed-size-record-and-CRC pattern already established for the queue, populated by the same logging calls that already write to Serial. This buffer is never proactively transmitted. A new record_type under ADR-001, `LOG_REQUEST_RESPONSE`, is emitted only when the device observes a `log_request` flag set in a config-poll response — an operator sets this flag via the receiver's admin interface when investigating a specific device. On the next config poll where the flag is seen, the device queues its current log-buffer contents through the existing durable queue/push pipeline, exactly as any other record type. A general-purpose remote command-execution channel (reboot-on-demand, verbosity control, forced push) is explicitly deferred as a separately scoped future extension, not built here.

**Reasoning:** On-demand retrieval targets transmission cost specifically at devices under active investigation, avoiding the disproportionate bandwidth/storage cost of continuous log streaming from an entire fleet that mostly has nothing noteworthy to report. Scoping this ADR to read-only log retrieval (rather than a general remote-command channel) avoids expanding the security surface with a write/command capability that deserves its own dedicated review.

**Alternatives Rejected:**
- *Proactively streaming all log lines continuously* — rejected: bandwidth/storage cost disproportionate to value for the vast majority of devices with nothing to report.
- *A general-purpose remote command-execution channel* — rejected for this ADR: meaningfully expands the security surface in a way that deserves its own dedicated review; log retrieval alone only ever reads and transmits data already being logged to Serial regardless.
- *A dedicated side-channel HTTP endpoint for log retrieval* — rejected: reuses no existing infrastructure for no benefit over piggybacking on the existing config-poll and push pipeline.

**Trade-offs:**
- *Performance:* Negligible in the common case; real cost only when actively requested.
- *Reliability:* None directly.
- *Complexity:* Moderate — a new SD ring buffer, a new record_type, a new config-poll response field.
- *Maintenance:* Significantly improves field-support capability.
- *Security:* The request flag is authenticated the same way any config-poll response already is (TLS plus API key from ADR-005); deliberately read-only to avoid expanding the security surface.
- *Scalability:* Negligible per-device overhead; cost incurred only for active investigations.
- *Cost:* Moderate.

**Compatibility:** Storage — yes, new ring-buffer file on SD, additive. Protocol — yes, new record_type and new config-poll response field, both additive. Configuration — no. OTA — no. Fleet — yes, a support/fleet-operations capability. Backward compatibility — fully additive.

**Migration Strategy:** None required beyond normal OTA rollout.

**Definition of Done:** A bench test confirms Serial output is mirrored into the ring buffer; a bench test setting the request flag on the reference receiver confirms the device responds with its recent history within one config-poll cycle; ring-buffer wraparound is verified to overwrite the oldest entries correctly without corruption.

**Future Extension:** A broader remote-command channel can be added later as its own explicitly scoped ADR, reusing the same "flag in config-poll response, response flows through the existing pipe as a new record_type" pattern established here — with its own dedicated security review for the write/command capability it would introduce.

---

## ADR-013 — Build/Release Engineering & Test Discipline

**Decision Name:** Single Source of Truth, Native Test Harness, Pinned Toolchain

**Problem Statement:** The `root/` and `src/` header copies are manually duplicated with no build-enforced sync; there is no automated test coverage for the invariants this architecture depends on; no toolchain version is pinned or documented.

**Business Goal:** Eliminate a real, previously-identified regression risk (silent root/src drift) and close the zero-automated-coverage gap for the invariants (CRC, ack, framing) the entire durability story depends on, without introducing disproportionate new process.

**Architecture Decision:** The `src/` folder is removed as a maintained duplicate; the root-level flat layout becomes the single source of truth, matching the primary target toolchain. PlatformIO support is restored via a build configuration that points its include path at the root folder, not via a second copy of the files. A native-host automated test suite is introduced, covering exactly the logic that can be isolated from hardware: CRC computation and validation, the cumulative-ack contiguous-sequence algorithm on both device and receiver sides, the truncate-before-append framing logic from ADR-003, and the schema-version dispatch logic from ADR-001. Hardware-dependent logic remains covered by the existing manual bench test gates, which are retained, not replaced. The exact Arduino-ESP32 core (and, if applicable, PlatformIO platform/framework) version used for the current release build is recorded in a new file at the repository root, updated whenever a release changes or verifies compatibility with a new core version.

**Reasoning:** Deleting the duplicate is strictly simpler than adding a drift-detection check to maintain two copies — it solves the same problem with less ongoing maintenance. A native-host test suite is scoped to pure logic deliberately, since hardware-in-the-loop automation is a disproportionate investment for this project's current scale; the existing manual bench gates remain the authority for hardware-dependent behavior. Recording the toolchain version is a near-zero-cost fix for a problem this project has already documented experiencing (OTA-rollback behavior varying by core version).

**Alternatives Rejected:**
- *Keep both layouts, add a CI check that diffs them and fails on drift* — rejected: solves the same problem as deletion with strictly more ongoing maintenance for no additional benefit.
- *Full hardware-in-the-loop CI* — rejected as disproportionate for this project's current scale; a substantial investment better justified at a later scale.
- *No formal toolchain pinning* — rejected: directly contradicts this project's own documented experience of core-version-dependent behavior.

**Trade-offs:**
- *Performance:* None directly.
- *Reliability:* Improves indirectly — regressions in the CRC/ack/framing invariants are caught before a field release.
- *Complexity:* Low addition.
- *Maintenance:* Significantly improves — this is squarely a maintainability-focused decision.
- *Security:* None directly.
- *Scalability:* None directly.
- *Cost:* Low-moderate.

**Compatibility:** Storage — no. Protocol — no. Configuration — no. OTA — no. Fleet — no. Backward compatibility — none; a repository/process change with zero effect on already-deployed devices.

**Migration Strategy:** The `src/` folder is deleted in the same change that adds PlatformIO's include-path configuration, verified by a successful PlatformIO build immediately afterward, so there is never a window where PlatformIO support is broken.

**Definition of Done:** `src/` is removed; a PlatformIO build against the root layout succeeds; a native-host test binary exists and passes, covering CRC, cumulative-ack, truncate-before-append, and schema-dispatch logic; the toolchain-version file exists and names the exact version the current release was built and tested against.

**Future Extension:** The native test suite grows to cover additional pure-logic modules (e.g., ADR-007's migration steps) as they are added, using the same host-build harness established here.

---

## ADR-014 — Console Safety & Minor UX Hardening

**Decision Name:** Confirmation-Coded Destructive Commands

**Problem Statement:** The `factory` console command executes immediately on a single line with no confirmation; an unused configuration constant (`SD_QUEUE_DIR`) sits alongside four independently hardcoded path literals.

**Business Goal:** Close a low-severity, easy-to-fix operator-safety gap and a piece of configuration dead-weight at minimal cost.

**Architecture Decision:** `factory` (and, by the same pattern, `decommission` from ADR-009) requires two steps: typing the command alone prints a warning and a randomly generated four-digit confirmation code; the destructive action proceeds only if the operator immediately retypes the command followed by that exact code. `SD_QUEUE_DIR` is removed as an unused constant, and the queue directory path is referenced from exactly one named source everywhere it is needed, rather than four independent literals.

**Reasoning:** A confirmation code prevents an accidental reset from a stray paste or typo while adding negligible friction for a deliberate reset — the operator simply reads and retypes four digits shown in the same console session.

**Alternatives Rejected:**
- *A physical jumper/button requirement in addition to the console command* — rejected: a disproportionate hardware change for a console-access-already-required operation.
- *Leaving `factory` unconfirmed* — rejected: the exact status quo this ADR closes.

**Trade-offs:** Performance — none. Reliability — minor improvement. Complexity — negligible. Maintenance — negligible. Security — minor improvement. Scalability — none. Cost — negligible.

**Compatibility:** Storage — no. Protocol — no. Configuration — no. OTA — no. Fleet — no. Backward compatibility — none; a console UX change only.

**Migration Strategy:** None required.

**Definition of Done:** A bench test confirms `factory` alone does not reset the device, and `factory` followed by the correct code does; `SD_QUEUE_DIR` is the sole referenced source of the queue directory path.

**Future Extension:** The same confirmation-code pattern is reused for any future destructive console command without inventing a new safety mechanism each time.

---

## ADR-015 — Sensor Plausibility & Self-Test

**Decision Name:** Fixed-Threshold Flow Plausibility Flag

**Problem Statement:** There is no way to distinguish "genuinely no flow" from "sensor or wiring failure" — a stuck totalizer looks identical in both cases.

**Business Goal:** Give fleet operators an early, low-effort signal that a specific device's sensor path may need attention.

**Architecture Decision:** A `sensor_plausible` boolean, reported in the ADR-002 health record, is defined by exactly one rule: it is false if the totalizer has recorded zero new pulses across three consecutive 60-second health-report intervals (180 seconds total) while the device otherwise has a current WiFi RSSI reading (a qualifier that scopes the flag to the sensor specifically, since a device experiencing a broader problem is already covered by the rest of the health record). This rule accepts a known false-positive rate — a legitimately idle line for more than three minutes also trips the flag — in exchange for a single, simple, unambiguous rule the architecture has a basis to specify, rather than a more elaborate anomaly model it does not. Deeper, more definitive sensor verification remains the responsibility of ADR-008's factory self-test and field `verify` command, which this runtime flag complements, not replaces.

**Reasoning:** The architecture has no independent knowledge of expected flow at a given site and time, so a model attempting to distinguish "idle" from "broken" beyond a fixed no-change threshold would require domain/analytics capability outside this review's scope. The fixed rule is framed to operators as "worth checking," not "definitely broken."

**Alternatives Rejected:**
- *A statistical/learned model of expected flow per site* — rejected: requires a substantial receiver-side analytics capability and per-site tuning outside architectural scope; the fixed rule is sufficient for a first release and can be superseded later without any device-side change.
- *No flag at all* — rejected: the status quo this ADR closes.

**Trade-offs:** Performance — negligible. Reliability — none directly. Complexity — low. Maintenance — none directly. Security — none. Scalability — none. Cost — low.

**Compatibility:** Storage — no. Protocol — yes, one new field in the health record, additive. Configuration — no. OTA — no. Fleet — yes, feeds fleet-wide dashboards. Backward compatibility — fully additive.

**Migration Strategy:** None required.

**Definition of Done:** A bench test holding the totalizer flat for over 180 seconds confirms the flag flips to false on the next health report, then flips back to true once pulses resume.

**Future Extension:** The 180-second/three-interval threshold is a single tunable constant. A future, more sophisticated anomaly model can replace the underlying computation without changing the reported field's position or type, so receiver-side consumption of this flag never needs to change even if its logic is later refined.

---

## ADR-016 — Receiver/Fleet Infrastructure Scaling (Program-Level)

**Decision Name:** Reference Receiver Is Bench-Only; Production Receiver Must Enforce the Frozen Contract

**Problem Statement:** The reference receiver (a single-threaded development server with a single-writer database) enforces no authentication and is a hard ceiling around a few hundred devices — a program-level dependency this firmware architecture cannot resolve on its own.

**Business Goal:** Ensure the firmware side of this architecture is never blocked by, or contradicted by, a receiver implementation that cannot actually support it at fleet scale — without this document overreaching into a decision that belongs to another team.

**Architecture Decision:** The reference receiver is explicitly designated bench/development-only and is not the basis for any production receiver. Whichever team owns the production receiver must build it on a real multi-worker server and a real multi-writer-capable database, and must independently implement the API-key authentication check the reference implementation omits. This document does not select a specific production framework or database — that choice belongs to the owning team — but it does freeze two non-negotiable requirements on whatever is chosen: it must accept the exact wire contract frozen across ADR-001, ADR-002, ADR-009, and ADR-010 without modification, and it must enforce authentication, since ADR-005's per-device key provisioning is meaningless if nothing checks it.

**Reasoning:** The receiver's technology stack is a separate system whose implementation does not constrain, and is not constrained by, the firmware beyond the wire contract itself — deciding it here would overreach this document's proper authority. Freezing the contract and the authentication requirement, while leaving the stack open, is the correct division of responsibility.

**Alternatives Rejected:**
- *This document selecting a specific production database/framework* — rejected: outside firmware-architecture scope.
- *Treating the reference receiver as adequate for a small pilot fleet* — rejected: even a small pilot must exercise the real authentication and concurrency model it will eventually run under, since the missing-authentication finding is P0 regardless of fleet size.

**Trade-offs:** Performance/Reliability/Scalability — entirely dependent on the receiver team's own implementation, unconstrained by this ADR beyond the frozen contract. Complexity/Cost — belongs to the receiver team's budget. Security — this ADR's only firm, non-negotiable requirement is that authentication must be enforced, regardless of the rest of the receiver's design.

**Compatibility:** Protocol — the receiver must implement the frozen wire contract exactly. Fleet — this is the fleet-scale enabling infrastructure. Backward compatibility — the receiver must support the "current + previous schema version" rule from ADR-001 for as long as any fielded device might still run the previous version.

**Migration Strategy:** Not applicable to firmware. The receiver team plans its own migration from the reference stub to a production system, constrained only by the frozen wire contract.

**Definition of Done:** A production receiver, whichever system is chosen, demonstrates in a load test correct handling of the target fleet size's aggregate request rate with authentication enforced and correctly rejecting any request presenting an invalid or missing API key.

**Future Extension:** ADR-001's schema-versioning rule is precisely what allows the receiver to be replaced or rearchitected in the future without requiring a synchronized firmware change.

---

## Final Review: Is the Firmware Architecture Now Fully Frozen?

**Test applied:** if another Principal Architect joined this company three years from now, with access only to the Audit, the Blueprint, and this ADR document, could they build exactly the same product?

**Answer: yes, for everything within this document's proper architectural authority — with three deliberately-scoped exceptions that are correctly delegated, not accidentally missing.**

Every decision that determines *what the firmware does* is now a single, stated "will," not a "could": the exact wire/storage schema-versioning rule (ADR-001), the exact health-record contents and cadence (ADR-002), the exact queue-corruption-prevention mechanism and the specific rule that keeps unacknowledged rows from ever being skipped (ADR-003), the exact SD-degradation state machine (ADR-004), the exact security scheme down to the signing algorithm and CA-pinning approach (ADR-005), the exact watchdog timeout and reset-reporting mechanism (ADR-006), the exact configuration-versioning and re-pointing rules (ADR-007), the exact factory and field commissioning procedures (ADR-008), the exact identity-and-repair model (ADR-009), the exact calibration governance decision including the specific fields removed (ADR-010), the exact jitter formula (ADR-011), the exact log-retrieval mechanism and its deliberate scope boundary (ADR-012), the exact build/test/toolchain discipline (ADR-013), the exact console-safety mechanism (ADR-014), the exact sensor-plausibility threshold (ADR-015), and the exact contractual requirements placed on any production receiver (ADR-016). Two developers reading any single ADR above would arrive at the same behavior, the same thresholds, and the same failure-mode handling.

**Three things remain open by design, not by oversight, and this is stated plainly rather than glossed over:**

1. **Exact byte-level wire encoding** (field order, padding, endianness within a record) is deliberately left unspecified, consistent with this document's no-code, no-pseudocode mandate. This is not an architectural ambiguity: the field list, semantics, versioning rule, and order of operations for every record type are fully frozen above. Two engineers implementing this would need to agree on one concrete byte layout with each other before writing code, but that agreement is a mechanical implementation step downstream of a fully-specified architecture, not a fork in the design itself.
2. **The production receiver's specific technology stack** (framework, database, hosting) is deliberately left to the team that owns it (ADR-016), constrained only by the frozen wire contract and the non-negotiable authentication requirement. This is a correct division of responsibility between firmware architecture and receiver/infrastructure architecture, not a gap in this document.
3. **Organizational/business policy decisions** — specifically, who holds approval authority for calibration changes (ADR-010) and the exact mechanical/jig design for factory testing (ADR-008) — are explicitly deferred because they depend on decisions outside any firmware architecture document's authority to make. Both are named, scoped, and given a stated extension path rather than left as silent unknowns.

**Conclusion: the firmware architecture is now frozen.** Any change to a decision recorded in ADR-001 through ADR-016 requires a new ADR that explicitly supersedes it. The three items above are not exceptions to this freeze — they are correctly identified as belonging to a different, adjacent authority, each with a clearly stated contract that whoever makes that decision must honor.
