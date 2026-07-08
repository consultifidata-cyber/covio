# Firmware Architecture Freeze & Remediation Blueprint — Covio Oil Flow Meter

**Status:** Governing document. Supersedes ad-hoc interpretation of `Firmware Architecture Audit.md` for implementation planning purposes.
**Input:** `Firmware Architecture Audit.md` (treated here as the sole source of truth for "what exists today").
**Purpose:** classify every audit finding, close remaining architectural gaps, and freeze every decision that would otherwise let independent developers build divergent products from the same repository.
**Boundary:** this document contains architecture decisions, gap analysis, sequencing, and a freeze checklist only. It contains no code, no pseudocode, and no implementation detail. Every open item that still requires a concrete implementation-level decision is called out explicitly in §15 rather than silently resolved.

**ID scheme used throughout this document** (for unambiguous cross-referencing by independent teams):
- `F-nn` — a finding carried over directly from the Audit.
- `N-nn` — a new gap identified in this review that the Audit did not name.
- `C-nn` — a consolidated architectural change item (Task 2). Every `F-` and `N-` finding resolves into exactly one `C-` item (occasionally two, noted explicitly).

---

## Task 1 — Classification of Every Audit Finding

Severity definitions used below:
- **P0 — Critical.** Data loss, data corruption, or security compromise. Must be resolved before any field deployment, including pilots.
- **P1 — Required before production.** Not an active loss/compromise today, but blocks a defensible commercial launch (reliability, serviceability, fleet operation, or a documented product claim that is currently false).
- **P2 — Product improvement.** Real value, not launch-blocking. Schedulable after production launch or in parallel with it.
- **P3 — Nice to have.** Hygiene/polish with negligible product risk if deferred indefinitely.

### Storage

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-01 | Torn SD-append permanently desynchronizes the queue log; can cause silent deletion of unacknowledged data (`EventQueue::empty()` false-positive → `maybeCompact_()` deletes the file) | **P0** | This is data loss, triggered by a completely ordinary field event (power cut mid-write) that the rest of the architecture (dual-slot checkpoints) was explicitly built to survive. It directly falsifies the product's central "zero silent loss" claim. |
| F-02 | No SD free-space monitoring / full-card detection | **P1** | Degrades to silent data loss only once a card actually fills — a slower-developing, ops-preventable condition, but still must be closed before a fleet can be trusted unattended for years. |
| F-03 | No runtime re-check of SD presence after boot | **P1** | Requires an operator/environmental event (card dislodged) to trigger; real but less inevitable than F-01. |
| F-04 | Queue log is a single unbounded file; code comment claims "daily log file" but no rotation exists | **P1** | Not itself destructive, but it is both a documentation defect and the structural reason F-05/F-06 are as bad as they are. |

### Synchronization

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-05 | `pending()` rescans the queue log from byte 0 every push cycle — O(n²) cost across a long catch-up | **P1** | Doesn't lose data, but defeats the "zero-loss outage recovery" product claim in practice by making recovery from a realistic multi-day outage take far longer than documented. |
| F-06 | Fixed 50-records/5s drain rate, no burst/adaptive catch-up | **P1** | Same product claim at risk as F-05; compounds it. |
| F-07 | No differentiated backoff for application-level (server-reachable-but-rejecting) failures | **P2** | An efficiency/robustness refinement, not a correctness gap — current behavior (retry every 5s forever) is safe, just not optimal. |
| F-08 | Stale comments in `store.h` and `server.py` describe the superseded `(device_id, boot_id, seq)` uniqueness model | **P1** | Given this document's own premise — multiple developers implementing independently — a maintainer reading the wrong file could build an incorrect receiver. That is a real integration-correctness risk, not just prose hygiene. |

### Health & Observability

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-09 | `QUALITY_BACKLOG_HIGH` flag defined, never set | **P1** | Documented in ARCHITECTURE.md as a working alerting feature; it is not. Shipping with a false product claim is a production blocker. |
| F-10 | `QUALITY_TIME_UNSYNCED` flag defined, never set; no SNTP exists | **P1** | Same reasoning as F-09. |
| F-11 | `QUEUE_HIGHWATER` constant defined, never referenced | **P1** | Root cause shared with F-09; see C-02. |
| F-12 | No free-heap reporting | **P1** | Required for remote diagnosis (Task 9); a slow leak is currently invisible until a crash. |
| F-13 | No SD status/free-space reporting to server | **P1** | Same reasoning — required to remotely distinguish storage failure from every other failure mode. |
| F-14 | No reset-reason reporting | **P1** | Required to distinguish "clean reboot" from "crash" from "brownout" remotely. |
| F-15 | No watchdog-reset reporting | **P1** | Follows directly from F-16; nothing to report until a watchdog exists, but the reporting gap itself must still be closed in the same effort. |
| F-16 | No application-level watchdog configured | **P1** | A genuine hang has no independent recovery path today; unacceptable for an unattended field device. |
| F-17 | No "last successful sync" field reported by the device | **P1** | Required for remote diagnosis; today this must be inferred entirely from server-side `recv_ms` bookkeeping, which conflates "device is fine but network is down" with several other failure modes. |

### Reliability

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-18 | No brownout-handling configuration in application code | **P1** | Requires explicit verification (not necessarily new code) that SoC/toolchain defaults are adequate — this cannot be left unverified for a production commit. |
| F-19 | SD failure at boot causes an unrecoverable halt requiring physical intervention | **P1** | Direct driver of unnecessary truck-rolls; a field-serviceability blocker. |
| F-20 | SD removal mid-run degrades silently, no escalation | **P1** | Same category as F-19 — undetectable failure mode in production. |

### Security

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-21 | Plaintext HTTP in the live code path for all 4 API calls | **P0** | Enables MITM substitution of OTA binaries — arbitrary remote code execution on a fielded device. |
| F-22 | No firmware image signing / Secure Boot | **P0** | Same consequence class as F-21; together they are the two halves of a single "device can be made to run attacker code" risk. |
| F-23 | Reference server enforces zero API-key authentication | **P0** | Scoped precisely: this is a defect in the bench stub, not the firmware. It is classified P0 as a **requirement on whatever production receiver is built** — shipping any production receiver derived from this reference without adding the check would be a full authentication bypass. |
| F-24 | Secrets stored unencrypted in NVS; console echoes them in cleartext with no lock | **P0** | Direct credential exposure via physical/USB access, with no mitigating control at all today. |
| F-25 | Single static, long-lived API key per device, no rotation/expiry | **P1** | Serious but not immediately catastrophic if per-device unique keys are actually issued (a provisioning-process requirement, C-08) — the absence of rotation is a hardening gap, not an open door by itself. |
| F-26 | Default API key committed to source control | **P1** | A process/build-hygiene fix (never ship the literal default unrotated) rather than an architectural one; still mandatory before any production build. |
| F-27 | No confirmation gate on the `factory` console command | **P2** | Requires physical access already (console is not remotely reachable); a convenience/safety nicety layered on top of an already-privileged channel. |

### Provisioning & Configuration

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-28 | No remote/fleet provisioning path; USB console only | **P1** | Blocks any deployment beyond a handful of hand-provisioned bench units — a commercial-launch blocker, not a bench concern. |
| F-29 | No AP/BLE/QR-based commissioning | **P1** | Same reasoning as F-28; together they define the commissioning gap. |
| F-30 | No versioning/audit trail for endpoint/API-key/WiFi config changes | **P2** | Valuable for fleet operations at scale; not blocking for initial launch. |
| F-31 | No configuration backup/export mechanism | **P2** | Same reasoning as F-30. |
| F-32 | No NVS schema/migration versioning | **P1** | Without this, a future OTA that adds/renames an NVS key has **undefined behavior** on already-deployed devices — a real production-safety gap for any fleet that will ever receive a second firmware version (i.e., every fleet). |

### Sensor & Extensibility

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-33 | No sensor abstraction/interface; `QRow` hardcodes a single `uint64_t` field | **P1** | Not blocking for shipping the oil-flow-meter product alone, but the product's own documentation commits to a reusable "Covio Device Sync Library" — under that stated goal, this is launch-blocking for the platform strategy, and retrofitting it after devices are in the field becomes a storage/protocol migration (Task 12) rather than a clean decision made once, now. |
| F-34 | No schema/version discriminator anywhere in the wire or storage format | **P1** | Same reasoning as F-33; the two are effectively one gap (see C-01). |

### Diagnostics & Logging

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-35 | No persisted/historical logging; Serial-only | **P2** | A real diagnosability improvement, but largely superseded in priority by the health channel (C-02), which answers most of what a persisted log would be used to investigate. |
| F-36 | No remote diagnostic capability for a "gone dark" device | **P1** | This is the single most consequential observability gap in the entire audit — see Task 9. |

### Maintainability

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-37 | `root/` and `src/` header copies manually duplicated, no build-enforced sync | **P2** | A real regression risk over time, not an active defect today (currently identical). |
| F-38 | No unit tests or CI in the repository | **P2** | Strongly recommended for long-term quality; not a go/no-go gate for an initial production commit, provided the manual bench test gates in `IMPLEMENTATION_AND_TESTING.md` continue to be run. |

### OTA

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-39 | OTA health-confirmation only checks network reachability, not correct behavior | **P1** | A broken image (e.g., sensor path silently broken) can self-validate and cancel rollback — a real production safety gap distinct from F-22's transport/signing concern. |
| F-40 | No on-device OTA attempt/history tracking | **P2** | Useful for diagnostics, not launch-blocking. |

### Fleet

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-41 | No fleet-management capability of any kind | **P1** | Required to operate more than a handful of devices as a commercial product; largely a receiver-side capability with a firmware data dependency (C-02). |

### Hygiene

| ID | Finding | Class | Why |
|---|---|:-:|---|
| F-42 | `config.h`'s `SD_QUEUE_DIR` is defined but unused | **P3** | Cosmetic; zero functional impact. |

### New Findings From This Review (Task 3)

| ID | Finding | Class | Why |
|---|---|:-:|---|
| N-01 | No separation between hardware identity (MAC-derived `device_id`) and logical asset identity (the installed meter) | **P1** | A board repair/replacement today creates a brand-new, historyless device from the server's point of view, with no relink workflow anywhere (including the reference receiver). This is a normal support operation from day one of any hardware failure. |
| N-02 | No factory self-test / production test procedure | **P1** | Nothing automates confirming opto/SD/WiFi/PCNT function before a unit ships; blocks scaling manufacturing beyond hand-built bench units. |
| N-03 | `density`/`T_ref` are persisted, cached, transmitted, and stored end-to-end but never applied in any litre calculation, anywhere | **P1** | If temperature compensation has been promised to customers, this is a false product claim (same class as F-09/F-10). If it was never promised, this is dead plumbing (P2). Flagged P1 pending an explicit decision — see C-10. |
| N-04 | No immutable audit log for calibration (K-factor) changes; any dashboard user can retroactively rewrite all historical computed litres with no record of who/when/why | **P0** | For a device whose stated purpose is metering (a billing/custody-transfer-adjacent function), silent, unaudited retroactive rewriting of historical volumes is a business-integrity compromise, not a mere feature gap. |
| N-05 | No bidirectional remote command channel (reboot / raise log verbosity / force push / state dump) beyond whole-binary OTA | **P2** | High operational value, not launch-blocking; whole-binary OTA plus the health channel (C-02) covers the most urgent cases. |
| N-06 | No firmware/protocol version deprecation or long-term-support policy | **P2** | Matters once multiple firmware generations coexist in the field; not urgent for a first production release. |
| N-07 | No documented repair/RMA data-continuity procedure (which component — SD card or controller — is "the durable asset"; untested interaction between a reset `boot_id` and a continuing SD `seq`) | **P1** | Same root cause and urgency as N-01; consolidated together. |
| N-08 | SD card write-endurance/wear-leveling not addressed; checkpoint files are fully rewritten roughly once per second indefinitely (≈86,400+ rewrites/day to two fixed locations) for a multi-year field life | **P1** | A silent multi-year reliability time bomb if consumer-grade media is used without an explicit wear/lifespan decision; must be decided before committing to a storage architecture, since retrofitting it after thousands of units ship is expensive. |
| N-09 | No sensor plausibility/self-test check; cannot distinguish "genuinely no flow" from "sensor/wiring failure" | **P1** | Directly blocks the Task 9 diagnostic goal and is a concrete, everyday field-support pain point. |
| N-10 | No standardized field-commissioning verification step distinct from the engineering bench test | **P1** | Without it, a bad install (miswired opto, wrong K-factor) can go undetected until a customer complains, rather than being caught at commissioning time. |
| N-11 | Undocumented/untested behavior when re-pointing a device with a non-empty queue to a different receiver (e.g., staging → production) | **P2** | An edge case today, but must be decided before it is exercised for real during a production cutover. |
| N-12 | No jitter/dithering on push (5s), config-poll (60s), or OTA-poll (5min) timers — only WiFi reconnect has randomized backoff | **P1** | A mass-reconnect event (e.g., a regional power restoration) leaves many devices' timers phase-aligned, producing a recurring synchronized load spike against the receiver that worsens, not improves, as fleet size grows. This is a scale-dependent risk that becomes real exactly when the product succeeds. |
| N-13 | The reference receiver (single-threaded Flask + single-writer SQLite) is a hard ceiling around a few hundred devices | **P1** | Not a firmware defect, but a program-level dependency: nothing currently plans for the receiver architecture a real fleet requires. |
| N-14 | No decommissioning/end-of-life workflow distinct from `factory` reset (secure wipe + server-side unlink) | **P3** | Real but low-frequency operational gap. |
| N-15 | No pinned/documented toolchain (Arduino-ESP32 core) version | **P2** | Directly relevant given the project's own "rollback honesty note" already shows core-version-dependent behavior; should be closed alongside build/release engineering work. |

**Classification summary:** 6 P0, 33 P1, 15 P2, 3 P3, out of 57 total findings. The heavy P1 weighting is expected and appropriate for a pre-production architecture review — it means "known, scoped, and sequenced," not "surprising."

---

## Task 2 — Overlap Analysis & Consolidated Roadmap

Sixteen consolidated architectural change items (`C-01`–`C-16`) resolve all 57 findings above. Each is listed with the findings it closes and the one-sentence architectural decision it represents (decision, not implementation).

| C-ID | Name | Resolves | Architectural decision |
|---|---|---|---|
| **C-01** | Self-Describing, Versioned Telemetry Schema | F-33, F-34 (and is a prerequisite for C-02, C-03, C-10) | Replace the fixed, un-versioned `QRow`/JSON contract with a schema carrying an explicit version/type discriminator and room for optional fields, on both the SD row and the wire JSON. **The single highest-leverage decision in this document** — nearly every other extensibility and observability item depends on it existing first. |
| **C-02** | Device Health & Diagnostics Channel | F-09, F-10, F-11, F-12, F-13, F-14, F-15, F-16 (reporting half), F-17, F-36, N-05 (reporting half); materially supports F-39, F-40, N-09 | Introduce one canonical, periodic health/heartbeat payload — separate cadence from the per-second measurement stream — carrying uptime, free heap, reset reason, watchdog-reset count, SD status/free space, live queue depth, RSSI, last-successful-sync timestamp, firmware version, OTA state, and a sensor-plausibility flag. |
| **C-03** | SD Queue Storage Redesign — Framing, Rotation, Read Cursor | F-01, F-04, F-05, F-06 | Redesign the queue log's on-disk format to be self-resynchronizing after corruption, add bounded rotation/segments in place of one unbounded file + all-or-nothing compaction, and persist a read cursor so catch-up scanning is not O(n) per cycle. |
| **C-04** | SD Health & Full-Card Monitoring | F-02, F-03, F-19, F-20; component-selection half of N-08 | Add a periodic runtime SD self-check (presence, free space, write-verify) feeding C-02, plus a defined degrade/alert state machine replacing silent continuation and the unrecoverable boot-time halt. |
| **C-05** | Transport & Firmware Security Hardening | F-21, F-22, F-23 (receiver-side requirement), F-24, F-25, F-26 | One coordinated security program: TLS/CA-pinning on all four endpoints, image signing + Secure Boot, per-device key provisioning at manufacturing time (not a shared source-control default), NVS flash encryption, and a mandated authentication check on any production receiver. |
| **C-06** | Reliability Hardening — Watchdog & Boot Safety | F-16 (mechanism half), F-18, F-19 (policy half) | Configure an application-level watchdog, explicitly verify/document brownout behavior, and replace the infinite SD-init-failure halt with a bounded, escalating retry policy. |
| **C-07** | Configuration Lifecycle Management | F-30, F-31, F-32, N-11 | Version the entire NVS configuration set as a whole (not only calibration), add export/import/backup, and explicitly define the behavior of re-pointing a device with a non-empty queue to a new receiver. |
| **C-08** | Provisioning & Commissioning Platform | F-28, F-29, N-02, N-10 | Cover both factory provisioning (a real test/burn-in procedure plus per-device secret injection, feeding C-05) and field commissioning (an installer-usable verification step beyond the engineering console). |
| **C-09** | Device & Asset Identity Model | N-01, N-07, N-14 (partial) | Separate hardware identity (chip-derived) from logical asset identity (the installed meter); define the repair/RMA/decommission workflows this separation enables. |
| **C-10** | Calibration Governance & Audit Trail | N-03, N-04 | Decide whether `density`/`T_ref` are wired into a real calculation or removed from the schema; add an immutable audit log for calibration changes given the billing/custody-transfer implications. |
| **C-11** | Fleet-Scale Timing Hygiene | N-12; partially mitigates N-13 | Add jitter/dithering to push/config/OTA polling cadences, independent of and in addition to receiver-side scaling work. |
| **C-12** | Diagnostics & Remote Log Retrieval | F-35, N-05 (command-channel half) | Persisted/retrievable device logs and a bidirectional remote command channel (reboot / verbosity / force-push / state-dump) beyond whole-binary OTA. |
| **C-13** | Build/Release Engineering & Test Discipline | F-37, F-38, N-15 | Eliminate root/src duplication via a single source of truth, add automated coverage for the CRC/ack/framing invariants, pin and document the toolchain version. |
| **C-14** | Console Safety & Minor UX Hardening | F-27, F-42 | Add a confirmation step to `factory`; remove dead configuration constants. |
| **C-15** | Sensor Plausibility & Self-Test | N-09 | Add an on-device check distinguishing "no flow" from "sensor/wiring failure," reporting through C-02. |
| **C-16** | Receiver/Fleet Infrastructure Scaling *(program-level, not firmware)* | N-13; supports F-41 | Not a firmware change, but tracked here because firmware's health/diagnostics output (C-02) is inert without a receiver able to aggregate it across a fleet. |

**Key consolidation insight:** what the audit reported as roughly a dozen separate "missing health field" findings (F-09 through F-17, plus F-36) is architecturally **one decision** (C-02), not twelve. Similarly, the sensor-extensibility gap (F-33/F-34) and the observability dead-flags (F-09/F-10/F-11) share the same root cause — the wire/storage format has no versioning or generic payload mechanism — which is why C-01 is listed as a prerequisite for C-02 rather than a peer of it. Implementing C-02 before C-01 would mean bolting new fields onto the existing un-versioned `QRow`, which is itself one more instance of the exact problem C-01 exists to fix.

---

## Task 3 — Additional Missing Commercial Capabilities

Addressed inline above as `N-01` through `N-15`. No further additions beyond what is captured in Task 1/2; the sweep covered every category the brief listed (provisioning, diagnostics, fleet management, sensor abstraction, storage, recovery, offline mode, OTA, manufacturing, factory testing, commissioning, device identity, versioning, health, security, scalability, maintainability, long-term support, field serviceability, configuration lifecycle, calibration lifecycle, remote debugging, manufacturing process, repair process).

---

## Task 4 — Module Single-Responsibility Review

No redesign proposed; this identifies where responsibility already leaks across a module boundary, which independent developers must be warned about before touching these files in parallel.

| Module | Finding |
|---|---|
| `covio_firmware.ino` | Beyond pure orchestration, it embeds two policy decisions inline: the hardcoded `false` passed as `backlogHigh` to `Telemetry::build()` (a decision that belongs to whichever module owns queue-depth awareness — see C-02), and the exact rule for *when* `ota.confirmHealthyBoot()` fires (currently duplicated ad hoc at two call sites in the loop, rather than owned by `Ota` or `Sync`). |
| `Store` | Otherwise clean, but its in-code comment describing the `(device_id, boot_id, seq)` uniqueness scheme (a sync/receiver-layer concern) does not belong conceptually to `Store`, which has no knowledge of how its data is used downstream — a documentation-placement leak (see F-08). |
| `EventQueue` | Bundles three responsibilities: (1) telemetry-row storage, (2) ack-pointer storage, (3) compaction policy. These are related but distinct concerns co-located in one class; a developer changing compaction policy (C-03) and a developer changing row storage (C-01) would be editing the same file for unrelated reasons. |
| `Telemetry` | Bundles record-construction (a sensor-state snapshot concern) with wire-serialization (a marshalling concern). These would need to evolve independently the moment a second wire format or a second sensor type exists. |
| `Sync` | The highest-fan-in module: owns WiFi lifecycle, push/batch transport, ACK parsing, *and* config polling — four responsibilities in one class. WiFi-lifecycle ownership is inconsistently applied: `Ota` independently re-checks `WiFi.status()` rather than asking `Sync`, meaning connectivity state has two independent code paths rather than one shared authority. |
| `Ota` | Reasonably single-responsibility for the manifest/update/rollback mechanism, but the *policy* of what counts as "healthy" is split across `Ota` (the mechanism, `confirmHealthyBoot()`) and `covio_firmware.ino` (the trigger conditions) — no single module owns the health-confirmation policy end-to-end (ties directly to F-39). |
| `Provision` | Single responsibility, clean; no finding. |
| `server.py` | Bundles four responsibilities in one file: ingestion API, calibration/config API, OTA hosting, and an admin HTML dashboard. Acceptable for a bench stub; not a pattern a production receiver should copy without deliberately deciding whether to keep them merged. |

**Consolidated implication:** before parallel implementation begins, ownership of (a) queue-depth/backlog awareness, (b) OTA health-confirmation policy, and (c) WiFi-connectivity-state authority must each be assigned to exactly one module. This is a freeze-checklist item (§14), not a redesign — it only requires stating which existing module owns each responsibility going forward.

---

## Task 5 — Dependency & Coupling Risk Review

No redesign proposed; risks only, ahead of parallel implementation.

- **`QRow` is a shared, un-versioned data contract touched by at least three files** (`queue.h` defines it, `telemetry.h` builds and serializes it, `sync.h` consumes it for batching). Until C-01 freezes its layout, two developers independently extending it (one for a new sensor field, one for a new SD-persistence concern) will produce a merge conflict at best and a silent behavioral mismatch at worst. **Must be frozen before any parallel work touches it.**
- **`Sync` has the highest fan-in/fan-out** of any module (depends on `Store`, `EventQueue`, and `Telemetry` simultaneously) and bundles two functionally unrelated concerns (push-transport, config-polling) in one class. Two developers independently changing "how batching works" and "how calibration polling works" would be editing the same file for unrelated reasons — a structural merge-conflict risk even though the two concerns have no functional dependency on each other.
- **`Ota` and `Sync` independently query WiFi connectivity** via two separate code paths rather than one shared authority (see Task 4). A future change to reconnect/retry logic made in one is not guaranteed to be mirrored in the other.
- **The minimal JSON field-extraction logic (`extractLong_`/`extractFloat_`/`extractStr_`) is independently reimplemented in both `sync.h` and `ota.h`** rather than shared — a genuinely new observation from this review. A parsing bug fixed in one location will not propagate to the other unless someone remembers both exist.
- **`config.h` is a single flat namespace of `#define`s with no encapsulation.** Low risk today given the small module count, but becomes a real collision risk under C-11 (Task 11) as more sensor/device-type modules are added to the same build.

---

## Task 6 — Is the Storage Architecture Frozen?

**No.** The following must be decided before storage can be declared frozen, because every one of these changes the physical byte layout devices will write in the field — and once real devices hold data in today's format, changing the layout later is a storage migration (Task 12), not a clean decision:

1. **Self-resynchronizing row framing** must replace the current fixed-offset-forever assumption (closes F-01) — C-03.
2. **A persisted read cursor** must be added so recovery scans are not O(n) per push cycle (closes F-05) — C-03.
3. **Bounded rotation/segmentation** must replace the single unbounded file + all-or-nothing compaction (closes F-04) — C-03.
4. **A schema/version discriminator** must be added to the row format itself, not only the wire JSON (closes F-34; enables C-02 to add health fields later without breaking historical row readability) — C-01.
5. **The fate of `density`/`T_ref`** must be explicitly decided — kept and wired into a real calculation, or removed from the schema entirely (closes N-03) — C-10.
6. **SD component/wear-endurance policy** must be decided (card spec tolerant of ~86,400+ rewrites/day to the same two files, indefinitely, for a multi-year field life) before committing to today's checkpoint-write pattern at scale (closes N-08) — C-04.

Until items 1–6 are decided, any claim that "the storage architecture is frozen" is false — the SD row format, the wire schema, and the calibration field set are all still open.

---

## Task 7 — Synchronization Scalability (100 / 500 / 1,000 / 10,000 devices)

**Firmware-side finding: no bottleneck scales with fleet size in the current design.** Each device operates on its own independent timers (push every 5s, config every 60s, OTA every 5min) with no shared client-side state or coordination between devices — the design is embarrassingly parallel from the firmware's perspective at every fleet size listed.

**Two architectural risks do scale with fleet size, and one of them (N-12) was not previously identified:**

| Fleet size | Aggregate request rate (push only, independent devices) | Assessment |
|---|---|---|
| 100 | ~20 req/s | Trivial for any receiver, including the bench stub, in steady state. |
| 500 | ~100 req/s | A single-threaded Flask dev server + SQLite (the reference implementation) begins showing serialization latency; a real WSGI server + real database is required — a receiver-side (not firmware) concern. |
| 1,000 | ~200 req/s sustained | SQLite's single-writer-lock model becomes a genuine architectural bottleneck **for the reference receiver specifically** — the wire protocol itself has no inherent ceiling here. |
| 10,000 | ~2,000 req/s sustained, plus synchronized-burst risk | Two independent problems compound: (a) the reference receiver's ceiling from the row above, and (b) **N-12 — no jitter/dithering exists on push, config-poll, or OTA-poll timers** (only WiFi reconnect has randomized backoff). A shared triggering event across the fleet (e.g., a regional power restoration) leaves every device's internal timer phase-aligned from the same reboot instant, producing a **recurring, synchronized load spike** against the receiver on a fixed, predictable cadence — a risk that gets *worse*, not better, as fleet size grows, and is entirely independent of receiver hardware. |

**Conclusion:** the wire protocol and firmware design impose no architectural ceiling on fleet size by themselves. Two things must still be fixed before claiming 1,000+ or 10,000-device readiness: (1) jitter/dithering on all periodic timers (C-11 — a firmware change), and (2) a real fleet-grade receiver replacing the reference stub (C-16 — a program dependency, not a firmware change, but one that must be tracked and staffed regardless).

---

## Task 8 — Provisioning Review

- **Is factory provisioning complete?** No. There is no factory self-test/burn-in procedure (N-02) and no per-device unique-secret injection process (depends on C-05) — today's "provisioning" is a manual bench console session, not a manufacturing-line process.
- **Is field commissioning complete?** No. There is no lightweight, installer-facing verification step distinct from the engineering bench test (N-10), and no AP/BLE/QR-based commissioning path exists at all (F-29) — only a USB serial console.
- **Can installers configure devices without engineering support?** No. Provisioning requires a USB cable, a serial terminal, and knowledge of exact console command syntax (`set url`, `set key`, `set wifi`) — there is no installer-grade tool.
- **Can support replace a failed device quickly?** No. There is no defined RMA/replacement workflow, no way to relink a repaired device's new `device_id` to an existing customer/asset record (in either the firmware model or the reference receiver), and no documented procedure for whether the SD card or the controller board is the "durable" component of a physical unit across a repair (N-01, N-07).

**Everything above resolves into C-08 (Provisioning & Commissioning Platform) and C-09 (Device & Asset Identity Model).**

---

## Task 9 — Diagnostics: "It stopped sending data."

| Suspected cause | Can support diagnose this remotely today? |
|---|---|
| WiFi | **No.** RSSI is reported per-record only while connected; once fully offline, nothing arrives, and silence looks identical to every other failure mode. |
| Sensor | **No.** No sensor plausibility/self-test exists (N-09); a stuck-at-flat totalizer is indistinguishable from genuinely zero flow. |
| Server | **No**, by definition — if the server itself is the failure, there is no independent out-of-band channel to query it. |
| Storage | **No.** No SD health/free-space field is ever transmitted (F-13); a full or failed card produces exactly the same symptom as no data at all: silence. |
| Firmware | **Partially.** The last-reported `fw` version is known, but no reset-reason or crash history is ever transmitted (F-14) — cannot distinguish "still running fine on old firmware" from "crashed and boot-looping" from "OTA'd successfully but now subtly broken." |
| OTA | **No.** No OTA attempt/outcome history is transmitted (F-40); a device stuck in a bad post-update state is invisible. |
| Power | **No.** No way to distinguish "unplugged/dead" from any other silence cause remotely. |
| Clock | **Not a well-formed question in this architecture** — the device has no wall clock at all (no SNTP); only the server's `recv_ms` is authoritative, so there is no on-device drift to diagnose, but also no self-reported "how long since I last synced" signal. |
| Configuration | **No.** `show` reveals current config, but only over a live serial console; remotely, support has zero visibility into what `server_url`/`wifi_ssid`/`api_key` a silent device currently holds. |

**Honest conclusion:** as the architecture stands today, when a device goes silent, support can determine almost nothing beyond "it was last seen at time T with these values" — **every failure category above collapses to the identical observable symptom (silence)**, because there is no health/heartbeat channel independent of the main measurement stream, and the one signal that theoretically exists for this purpose (the `quality` bitfield) is inert (F-09/F-10). This is the clearest, most concrete argument anywhere in this document for why **C-02 (Device Health & Diagnostics Channel) is the single highest-priority remediation** — it is the only item that converts this entire table from "no" to "yes" across the board.

---

## Task 10 — Maintainability: The 3-Years-Later Engineer Test

**What already exists and is genuinely good:** `README.md`, `ARCHITECTURE.md`, and `IMPLEMENTATION_AND_TESTING.md` form an unusually strong documentation baseline for an embedded project of this size, and the in-code comments consistently explain *why* (not just what), often citing specific prior incidents. This is well above the norm and should be preserved as a practice, not just a snapshot.

**What is still missing for genuine 3-year maintainability:**
- No changelog/decision log tracking *why* specific tunables were chosen (batch size 50, cadences 1s/5s/60s/5min) beyond scattered comments — no consolidated historical record.
- No automated test suite proving the CRC/ack/framing invariants still hold after a change (F-38) — a future engineer has only the manual, hardware-dependent bench gates in `IMPLEMENTATION_AND_TESTING.md` to fall back on.
- No record of rejected alternatives (why not MQTT, why not a real JSON library) consolidated in one place.
- No hardware BOM/schematic artifact in the repository — wiring is documented in prose across three files, requiring reverse-engineering from comments rather than a reference diagram.
- No pinned/documented Arduino-ESP32 core version (N-15) — directly relevant given the project's own admission that OTA-rollback behavior is core-version-dependent.
- **This Blueprint itself** becomes the missing piece, provided a policy exists to keep it current — which does not yet exist and must be added to the freeze checklist (§14): any architecture-affecting change must update this document as part of the same review.

---

## Task 11 — Extensibility: Reusable IoT Platform, or Single-Purpose Firmware?

**Today: single-purpose**, despite the documentation's stated ambition of a reusable "Covio Device Sync Library." Three concrete architectural blockers, in order of impact:

1. **`QRow`'s single hardcoded `uint64_t totalizer` field** — no generic/TLV payload exists; already covered as F-33/F-34, resolved by C-01.
2. **The API path structure itself bakes the device type into the URL** (`/api/iot/flow/push`, `/api/iot/flow/config`, `/api/iot/flow/ota/manifest`) rather than treating device-type as data on a generic route. `ARCHITECTURE.md`'s own extension-points section proposes "a parallel route family" per new device type — meaning every new sensor type multiplies receiver-side endpoints rather than being handled generically. *(New observation from this review.)*
3. **The calibration contract is flow-meter-specific vocabulary** (`K_factor`/`density`/`T_ref`) hardcoded into the config endpoint's JSON keys — a motor-runtime or weighing-machine device type (both explicitly named as future targets in `ARCHITECTURE.md` §12) would need entirely different calibration concepts, and no generic "device-type-specific config blob" mechanism exists. *(New observation from this review.)*
4. **No plugin/factory pattern in the firmware itself** — `covio_firmware.ino` directly instantiates and calls concrete classes; nothing lets a build select "flow sensor module" vs. "motor-runtime sensor module" while reusing one shared orchestrator.

**Conclusion:** the durable-queue + ACK + OTA pattern has good bones and genuinely is reusable in principle. But the claim of a reusable device library is aspirational, not actual, until items 1–4 are addressed together as a deliberate platform decision (they cannot be retrofitted piecemeal without repeated breaking migrations).

---

## Task 12 — Backwards Compatibility Classification

Every consolidated change item, classified by its impact on already-deployed devices/data.

| C-ID | Classification | Impact |
|---|---|---|
| C-01 Versioned telemetry schema | **Protocol change + Storage migration** | Changes both the wire JSON and the SD row format. Requires the receiver to accept both old and new schema versions during rollout, and requires a version-aware reader for any historical SD-resident rows written under the old fixed layout. **Major migration.** |
| C-02 Health channel | **Protocol change (additive)** | No breaking change to the existing push/config endpoints *if* implemented as a new, separate channel — but becomes a **major migration** if instead bolted onto the existing `QRow`, which is exactly why C-01 must precede it. |
| C-03 SD queue storage redesign | **Storage migration** | Changes the on-disk queue format. Devices in the field with an old-format queue need either a one-time drain-then-upgrade sequence (fully drain under old firmware before OTA-ing to the new format) or a dual-format reader during a transition window. **Major migration.** |
| C-04 SD health monitoring | **No breaking change** | Purely additive runtime checks and reporting via C-02. |
| C-05 Transport & security hardening | **Configuration migration + Firmware migration** | Existing devices must be re-provisioned with new certs/per-device keys (configuration migration); enabling signed OTA requires a careful bootstrap sequence — the *last* unsigned update must deliver the capability that then requires signatures thereafter. This bootstrapping step must be explicitly planned; it is not automatic. **Major migration.** |
| C-06 Watchdog & boot safety | **No breaking change** | Internal reliability hardening only; invisible to the contract. |
| C-07 Configuration lifecycle management | **Configuration migration** | Existing NVS layouts (implicitly unversioned today) must be read once under the old assumption and rewritten under the new versioned schema on first boot of updated firmware. |
| C-08 Provisioning & commissioning platform | **No breaking change** to already-deployed devices; **Minor migration** for the manufacturing/installation *process* (non-firmware). |
| C-09 Device & asset identity model | **Configuration migration + receiver-side data migration** | Existing devices' identity records must be mapped into the new hardware/asset split without losing history. **Major migration.** |
| C-10 Calibration governance | **Depends on the decision made:** removing unused `density`/`T_ref` is a **Minor migration** (they were never consumed); implementing real temperature correction is a **Major migration** (changes historical litre recomputation behavior retroactively). This decision must be made explicitly, not defaulted. |
| C-11 Timing jitter | **No breaking change** | Internal timing tweak, invisible to the contract. |
| C-12 Diagnostics & remote log/command channel | **Protocol change (additive)** | New, separate channel; no breaking change to existing endpoints. |
| C-13 Build/release engineering | **No breaking change** | Process/tooling only. |
| C-14 Console safety / hygiene | **No breaking change.** |
| C-15 Sensor plausibility/self-test | **No breaking change** if implemented within the C-01/C-02 schema; **Minor migration** if it requires seeding a new NVS calibration-baseline value. |
| C-16 Receiver/fleet infrastructure | **Not a firmware compatibility item** — imposes a **constraint** on the receiver (must remain backward-compatible with every already-deployed firmware version), not a migration on the firmware side. |

---

## Task 13 — Implementation Order (Architecture Sequencing, Not Coding Tasks)

Ordered by dependency, not by severity alone — several P0/P1 items (e.g., C-02) are architecturally *downstream* of a P1 prerequisite (C-01) and must wait for it regardless of their own severity.

**Phase 1 — Architecture Corrections & Schema Freeze**
C-01 (versioned telemetry schema), C-09 (identity model), C-10 (decision half only: keep-or-remove `density`/`T_ref`). *Nothing that touches the wire or storage format should proceed until this phase closes.*

**Phase 2 — Storage Integrity**
C-03 (SD queue framing/rotation/cursor), C-04 (SD health monitoring), N-08's component decision (folded into C-04's scope).

**Phase 3 — Reliability & Recovery**
C-06 (watchdog & boot safety), C-15 (sensor plausibility/self-test).

**Phase 4 — Synchronization & Scale Hygiene**
The F-05/F-06/F-07 fixes (now unblocked by Phase 2's cursor/framing work), C-11 (timing jitter).

**Phase 5 — Security Hardening**
C-05 (transport/signing/per-device keys/NVS encryption).

**Phase 6 — Health, Diagnostics & Observability**
C-02 (health channel — depends on Phase 1's schema versioning and on Phase 2/3's SD-health and watchdog/reset-reason data actually existing to report), C-12 (remote log retrieval & command channel).

**Phase 7 — Provisioning & Commissioning**
C-08 (factory + field commissioning — benefits from Phase 5's per-device-key model and Phase 1's identity model both being settled first), C-07 (configuration lifecycle management).

**Phase 8 — Fleet & Governance**
C-16 (receiver/fleet infrastructure — program dependency, tracked here even though it is not firmware code), the build-out half of C-10 (calibration audit log), F-41 (fleet management capability).

**Phase 9 — Maintainability & Long-Term Support**
C-13 (build/release engineering, test discipline, toolchain pinning), C-14 (console safety, hygiene), N-06 (LTS/deprecation policy), N-14 (decommission workflow).

---

## Task 14 — Architecture Freeze Checklist

Every item below is mandatory before implementation begins on the corresponding phase. None of these are satisfied by this document alone — each represents a decision this document identifies as *required* but does not itself make (see Task 15).

- ☐ Telemetry schema frozen — versioned, self-describing (C-01)
- ☐ SD queue row format frozen — framing + resync strategy (C-03)
- ☐ SD queue rotation/segmentation policy frozen (C-03)
- ☐ Push JSON contract frozen — fields, batch size, cadence (C-01)
- ☐ Config/calibration JSON contract frozen, including the `density`/`T_ref` decision (C-10)
- ☐ OTA manifest contract frozen (C-01/C-05)
- ☐ Health/heartbeat packet schema frozen — fields, cadence, transport (C-02)
- ☐ Device identity model frozen — hardware ID vs. asset ID (C-09)
- ☐ NVS configuration schema + version field frozen (C-07)
- ☐ Security model frozen — TLS approach, signing approach, per-device key provisioning process (C-05)
- ☐ Watchdog policy frozen — timeout, scope, recovery action (C-06)
- ☐ SD full-card / card-removal behavior frozen — degrade-vs-halt policy (C-04)
- ☐ SD wear/component policy frozen — card spec, expected lifespan (N-08)
- ☐ Sensor plausibility/self-test policy frozen — thresholds, flag semantics (C-15)
- ☐ Jitter/dithering policy frozen for every periodic timer (C-11)
- ☐ Diagnostics/remote command channel contract frozen (C-12)
- ☐ Provisioning workflow frozen — factory process and field commissioning process (C-08)
- ☐ Calibration governance/audit policy frozen (C-10)
- ☐ Fleet-scale receiver architecture decision made and staffed (C-16)
- ☐ Firmware/protocol versioning and deprecation policy frozen (N-06)
- ☐ Repair/RMA data-continuity procedure frozen (N-01/N-07)
- ☐ Toolchain/dependency versions pinned and documented (N-15)
- ☐ Single source of truth for firmware headers established — root/src duplication resolved (C-13)
- ☐ Test strategy defined — what must have automated coverage before merge (C-13)
- ☐ Migration plan defined for every item in Task 12 classified as Major/Storage/Protocol/Configuration migration, explicitly covering any bench/pilot units already in existence
- ☐ Module-ownership assignments from Task 4 recorded (queue-depth awareness, OTA health-confirmation policy, WiFi-connectivity authority each assigned to exactly one module)
- ☐ This document's own maintenance policy adopted — any architecture-affecting change updates this Blueprint as part of the same review (Task 10)

---

## Task 15 — Final Review: Would Five Developers Build the Same Product?

**Honest answer: not yet.** This Blueprint freezes the *what* (every finding classified) and the *order* (Task 13's phases), and it closes the ambiguity that existed purely from incomplete gap analysis. But it deliberately does not — and per the stated brief, must not — specify the *how* for each `C-` item. That means the following concrete decisions remain open, and five developers handed only this document would still diverge on each of them:

1. The exact versioned schema layout for telemetry — which fields, which types, the version-negotiation rule (C-01).
2. The exact SD queue framing/resync strategy — e.g., length-prefixed records vs. an in-band sync marker (C-03).
3. The exact health-packet cadence, and whether it shares a transport/endpoint with telemetry or uses a separate one (C-02).
4. The exact security approach — PKI/CA model, which signing scheme (ESP-IDF Secure Boot v1/v2 vs. an application-level signature check), and how per-device keys are generated and injected at manufacturing (C-05).
5. The exact device/asset identity scheme — e.g., a manufacturing serial burned into NVS at factory time vs. a QR-linked external asset tag (C-09).
6. The exact watchdog timeout value and recovery action (C-06).
7. The exact SD card specification/vendor decision for wear tolerance (N-08).
8. The exact jitter formula and bounds for each periodic timer (C-11).
9. The final decision on `density`/`T_ref` — kept-and-implemented, or removed (C-10).
10. The exact receiver architecture for fleet scale — database choice, framework, sharding strategy (C-16) — outside firmware scope but blocking Phase 8 regardless.
11. The exact commissioning tool form factor for C-08 — mobile app, browser-based Web Serial, or a dedicated factory jig.
12. The exact audit-log storage/immutability mechanism for C-10's calibration governance — append-only table, external ledger, or something else.

**Recommendation:** treat this Blueprint as the gate that gets a project from "undirected" to "sequenced and scoped," not as the gate that gets it to "ready for five parallel developers." Before Phase 1 implementation starts, each `C-` item in that phase needs its own short **Detailed Design Decision** (still architecture, not code) that closes items 1–12 above for that item specifically. That per-item design decision — not this document — is the actual gate before any two developers can be trusted to build compatible, interoperable pieces of the same system independently.
