# Covio V2 — Canonical Architecture

**Author role:** Chief Architect. **Status:** design document, not an
implementation plan — no code, no migrations, no ERP UI redesign. **Purpose:**
define the architecture Covio should converge on to serve 500+ devices across
multiple factories, tanks, and customers, without breaking the currently-live
Balaji deployment. This document is meant to be the durable reference other
design/implementation work cites, not a task list — the roadmap at the end
turns it into phases, but the design itself should not need to change as
those phases execute.

**Governing principle, stated once so it doesn't need repeating in every
section:** *firmware is a dumb, reliable edge agent; the ERP is the single
source of truth for identity, calibration, and fleet policy; Device Manager
is the human operational surface for both, and holds no authoritative state
of its own.* Every design decision below is a consequence of this one rule.

---

## Classification of every P0/P1 finding from the Product Readiness Review

**A = true architectural requirement. B = product enhancement (build on top
of the architecture, not part of it). C = nice-to-have. D = not worth
implementing as designed (either already correctly solved elsewhere, or not
an architecture question at all).**

| # | Finding | Class | Why |
|---|---|---|---|
| P0-1 | Per-device calibration (global singleton is wrong) | **A** | Wrong anchor for the data model breaks correctness at any scale beyond 1 device. Fixed in §2. |
| P0-2 | Device/asset registry + tank/plant assignment | **A** | No multi-tenant, multi-site fleet can exist without this. Fixed in §1. |
| P0-3 | NVS/flash encryption at rest | **A** | Secrets model is architectural, not a feature. Fixed in §7. |
| P0-4 | Execute production OTA signing-key ceremony | **D** | Not an architecture decision — the signing *design* is already correct (verified Solid in the prior review); this is a one-time operational action. Captured as a roadmap gate, not a design change. |
| P0-5 | Bulk/fleet provisioning | **A** | One-at-a-time provisioning cannot scale to 500 units/20 sites; the commissioning workflow must be designed for it from the start. Fixed in §3, §6. |
| P0-6 | Remote reconfiguration of deployed devices (no truck roll) | **A** | "Zero field surprises" and "long-term maintainability" both require this; it changes the security/recovery model, not just a UI button. Fixed in §7. |
| P0-7 | OTA trigger/rollout control in Device Manager | **A** | Fleet-wide firmware management is core to the platform, not a nice-to-have. Fixed in §3. |
| P0-8 | Real sensor/flow-accuracy validation | **D** | This is a manufacturing/QA gate (bench-test each meter model against a real flow rig), not an architecture question. The *calibration architecture* (§2) is what makes the result of that validation usable; the validation itself is a roadmap action item, not a design. |
| P1-1 | Cumulative failure counters (DNS/TLS/reconnect/watchdog/brownout/restart) | **A** | The observability *model* — persistent counters vs. last-reason-only — is architectural; it determines what "health" means at fleet scale. Fixed in §5. |
| P1-2 | Sensor-stuck-at-zero alarm | **A** | Silent wrong data is exactly the "field surprise" the goals rule out; detection must be architected into the health model, not bolted on. Fixed in §5. |
| P1-3 | Live Wi-Fi credential test before commit | **B** | Improves one workflow step; doesn't change any data model or system boundary. Folded into §6's commissioning design as a workflow detail, not a new architectural entity. |
| P1-4 | Config backup/restore | **B**, except the underlying need is **A** | A generic "backup/restore" button is a product enhancement. But the *capability it's standing in for* — moving a device's identity/calibration/history to a replacement unit — is a true architectural requirement, solved properly by the Installation model in §1/§2/§6's Replacement flow, which is a better answer than file-level backup/restore. |
| P1-5 | Working log export | **C** | Useful, doesn't affect architecture. |
| P1-6 | Multi-Wi-Fi-profile support | **B** | Real operational value, but it's a schema extension to the existing config contract, not a new architectural concept. |
| P1-7 | Wire `ota_debug` into Device Manager UI | **C** | Already computed device-side; purely a UI task. |
| P1-8 | Shared/cloud device registry (multi-operator) | **A** | This *is* the identity model in §1 being server-authoritative instead of per-laptop. Not separable from P0-2. |
| P1-9 | Unified commissioning wizard | **A** | Two disconnected identity-assignment screens is itself an architecture defect (split source of truth during commissioning). Fixed in §6. |
| P1-10 | Positive commissioning verification / final health-check gate | **A** | "Zero field surprises" requires a definitive, machine-checked Go-Live gate, not an inferred one. Fixed in §6. |
| P1-11 | Calibration governance UI (read/edit/wizard/history/lock/export) | **B** | Depends entirely on P0-1 landing first; once the per-Installation model exists, the UI on top of it is a product enhancement, not a separate architecture question. |
| P1-12 | Explicit watchdog configuration | **B** | Firmware hardening detail; doesn't change any boundary or data model. |

Everything classified **A** gets a canonical design below. **B** items are
addressed as extensions of that design where they naturally attach (noted
inline). **C**/**D** items are roadmap entries only — they don't need
architecture, just scheduling.

---

## 1. Device Identity Model

The single biggest structural fix versus V1: **calibration, ownership, and
history must not be anchored to the physical Device.** A physical device is
a replaceable, interchangeable unit; what matters for correctness and
continuity is the *installation* — the pairing of a device to a specific
measured point, for a specific span of time. Every other entity hangs off
that pairing.

### Entities

**Customer** — a tenant. `id, name, contract_status, contacts`. Owns Plants.
Multi-customer support (a stated goal) is just "more than one row here" —
nothing else in the model needs to special-case it if this entity exists
from day one.

**Plant** — a factory/site. `id, customer_id (FK), name, address, timezone,
site_network_profile_id`. One customer can have many plants (the "20
factories" case); Balaji is one row.

**Asset** — the thing being measured. Deliberately more general than "Tank,"
because oil flow metering will eventually cover pipelines, loading bays, or
other measurement points that aren't storage tanks. `id, plant_id (FK),
asset_type (tank | pipeline | loading_bay | ...), name, fluid_type,
physical_metadata (capacity, pipe_diameter, ...)`. **Tank is one value of
`asset_type`, not a separate table** — this keeps the model open without a
schema change when the second asset type shows up.

**Device** — the physical hardware unit. `id (permanent — the ESP32's
hardware MAC, immutable for the device's life), logical_device_id (the
human-facing serial, e.g. COV-000123, allocated once at factory Registration,
printed on the QR label), hardware_rev, manufactured_at, lifecycle_status,
current_installation_id (nullable FK — null while in warehouse or
decommissioned)`.

**Installation** — the join entity that binds a Device to an Asset for a
time span. This is the anchor everything else attaches to. `id, device_id
(FK), asset_id (FK), plant_id (denormalized for query convenience),
installed_at, removed_at (null = currently active), installed_by`. A device
can have many Installations over its life (moved between sites, or
re-deployed after refurbishment). An Asset can have many Installations over
its life (devices get swapped). **Exactly one Installation per Asset may be
active (`removed_at IS NULL`) at a time** — that's the invariant that makes
"which device is currently metering this tank" always answerable with one
query.

**Calibration** — versioned, bound to **Installation**, not to Device or
Asset alone (see §2 for why). `id, installation_id (FK), version, k_factor,
density, t_ref, effective_from, effective_to (null = current), created_by,
certificate_ref (optional)`. Append-only; never mutated in place.

**Firmware** — a build identity. `version, build_sha, signing_key_id,
released_at, security_version_floor, rollout_channel`. A Device has a
`running_firmware_version` (reported) and the fleet has a `target_firmware`
per rollout ring (policy, defined in §3) — the two are compared to know
whether a device needs an update.

**Ownership** — deliberately **not a stored field**. It's the transitive
closure `Device → Installation (active) → Asset → Plant → Customer`. Storing
a redundant `owner_customer_id` on Device would go stale the moment a device
is reassigned; deriving it means reassignment is just closing one
Installation and opening another; ownership updates itself.

**Status** — a Device lifecycle state machine:

```
MANUFACTURED → FACTORY_TESTED → PROVISIONED (identity assigned, no site yet)
    → COMMISSIONING (Installation created, calibration + verification pending)
    → ACTIVE (verified, live, billable)
    → MAINTENANCE (temporary flag, still installed — e.g. known degraded state)
    → DECOMMISSIONED (Installation closed, secrets wiped)
    → RETIRED (end of life) | PROVISIONED again (refurbished, reassigned)
```

Every transition is a fact worth recording (who, when, why) — this is the
backbone of "zero field surprises": if a device's state ever doesn't match
reality, the transition log tells you where the process broke down.

---

## 2. Calibration Architecture

**Anchor: per-Installation, not per-device and not per-asset alone.**

The reasoning: the "true" conversion factor from pulses to litres is a
function of three things that don't share one owner —

1. The flow sensor's own pulses-per-litre constant (a property of the
   *device/hardware model*),
2. The pipe/tank geometry at the specific mounting point (a property of the
   *asset*),
3. The fluid's density/temperature reference for that site's product (a
   property that can vary by *plant* or even by *batch*, independent of the
   hardware).

Binding calibration to Device alone breaks the moment a device is replaced
(new hardware, same tank — should the new device get a fresh guess, or
inherit the site's known-good values?). Binding it to Asset alone ignores
that different device generations may have different pulse constants.
**Installation is the only entity that captures "this specific device, at
this specific point, during this specspecific time span,"** which is
exactly what a calibration value is true for.

- **Per device?** No — wrong anchor (see above).
- **Per meter (device)?** No, same reason — a meter's pulse constant is an
  input to calibration, not the calibration itself.
- **Per tank (asset)?** Not alone — correct component, incomplete without
  the device and time-span context.
- **Historical calibration?** Yes, mandatory. Every Installation retains its
  full calibration history even after the device is swapped out and the
  Installation itself is closed — so a litre total for August can always be
  recomputed against whatever calibration was actually in effect in August,
  regardless of what's active today.
- **Versioning:** monotonic version number per Installation;
  `effective_from`/`effective_to` date range per version; exactly one
  current version (`effective_to IS NULL`) per active Installation.
- **Audit trail:** every calibration write is logged — who, when, old value,
  new value, reason/certificate reference — scoped to the Installation. This
  replaces V1's fleet-wide `device_events` log (which couldn't attribute a
  change to a specific device/site) with one that can.

**Where the math happens:** the ERP remains the sole authority for
converting raw pulses into litres, using whichever calibration version was
in effect at each reading's timestamp. **Firmware stays "dumb" about
calibration** — it reports raw pulse counts/totalizer plus which calibration
version it last cached (for its own local display estimate only, never
authoritative). This means calibration changes never require a firmware
change or a device re-flash — they're a data change in the ERP, applied
retroactively-correct to historical data and prospectively to new readings.

This directly replaces the V1 defect (`CHECK(id=1)` global singleton) with a
`(installation_id, version)`-keyed table — the fix is a data model change,
not a firmware change, and it's fully backward-compatible with the currently
running Balaji device (see the roadmap).

---

## 3. Fleet Architecture

Six lifecycle phases, each with one clear owner. (Terminology note: this
section's phase names — Registration/Commissioning/Provisioning — are the
architectural vocabulary; §6 maps them onto the specific factory-floor
sequence the review asked for, using the review's own step names.)

**Registration** — creating the Device row and allocating its permanent
identity. Happens once, at factory test. The **logical_device_id must always
be allocated by the ERP**, never generated locally by Device Manager or the
firmware itself — it has to be globally unique across the whole company's
fleet, and only the ERP has that view. Device Manager's factory-test flow
calls the ERP to get the ID, writes it to the device, and prints the QR
label. (This already exists in roughly this shape in V1 — the canonical
change is making ERP-issued the *only* path, with no local fallback.)

**Provisioning** — writing network/credential config (Wi-Fi, server URL,
API key) to a specific physical device. A sub-step of Commissioning, not a
separate top-level phase — it's the mechanism, Commissioning is the outcome.

**Commissioning** — binding a Registered, Provisioned device to a specific
Plant + Asset: creating the Installation record, assigning calibration, and
running the device through the verification gate before it's allowed to go
ACTIVE. Full workflow in §6.

**OTA** — fleet-wide, staged. The ERP owns *rollout policy* (which firmware
version each ring/segment of the fleet should be running — canary %, then
broader waves, matching the existing anti-downgrade/signing design, which
stays as-is). Device Manager is the *operational surface* — a technician or
release manager views rollout progress and triggers a wave from here.
Firmware only ever asks "what's my target version" and executes the
existing (already-solid) signed-manifest download/verify/apply/confirm
sequence. No entity duplicates another's job: ERP decides, DM operates,
firmware executes.

**Retirement** — Device transitions to `DECOMMISSIONED`: its active
Installation is closed (`removed_at = now`), a factory reset wipes secrets
(this already works correctly in V1 — the anti-downgrade security floor is
correctly preserved across the reset, and everything else is correctly
wiped), and its API key is revoked immediately. The `logical_device_id` and
its full Installation/Calibration history remain in the registry
permanently — retiring a device never deletes its historical record.

**Replacement** — the workflow that makes the whole model pay off. When a
device fails: the old device's Installation is closed exactly as in
Retirement above; a new (or refurbished) device goes through Registration if
it doesn't already have identity, then Commissioning — but the wizard
**pre-fills the Plant + Asset from the closed Installation** and **offers to
inherit the current calibration version** (a technician can accept it or
re-verify/adjust, since the physical world may have changed slightly, e.g. a
new meter model). This makes replacement a fast, guided path instead of
"start from zero," and it's the real answer to the V1 finding about
backup/restore — the state that matters (identity of the *site*, not the
*device*) was never something a device-level backup should have owned in the
first place.

---

## 4. Device Manager Responsibilities

Restating the governing principle as a strict boundary table, to eliminate
duplication:

| Capability | Firmware | Device Manager | ERP |
|---|---|---|---|
| Pulse counting, totalizer, local queue/durability | **Owns** | — | — |
| Wi-Fi/TLS transport, local HTTP status API | **Owns** | reads it | — |
| Calibration math (pulses → litres) | — | — | **Owns** |
| Calibration values (source of truth) | caches a display copy only | shows the cached/authoritative value | **Owns** |
| Device/Asset/Plant/Customer identity | reports its own logical_device_id | reads/searches it | **Owns** |
| Installation records | — | creates/closes via wizard | **Owns** (stores) |
| OTA execution (download/verify/apply/confirm) | **Owns** | triggers/monitors | defines target version/rings |
| OTA rollout policy | — | operational trigger surface | **Owns** (policy) |
| Firmware image signing | verifies signature | — | **Owns** (signs, distributes manifest) |
| Real-time device diagnostics (heap, RSSI, queue depth, live alarms) | **Owns** (computes) | **Owns** (displays, per-device) | doesn't duplicate this |
| Fleet-wide historical/trend observability | reports raw counters | rolls up what's currently connected | **Owns** (long-term store, cross-device analytics) |
| Multi-operator shared device metadata (tags, notes, assignment) | — | reads/writes through ERP, no local authoritative copy | **Owns** |
| Secrets (Wi-Fi password, API key) | stores encrypted (§7) | never displays raw value, only rotates/writes | issues, tracks fingerprint/rotation history |
| Bulk/fleet operations (provisioning, config push) | executes one device's instructions | **Owns** (the operational tool for this) | provides the fleet list/targets to operate on |

The one V1 defect this table fixes structurally: **Device Manager's local
per-laptop JSON store must stop being authoritative for anything** (tags,
location, logical IDs). It becomes a thin client over the ERP's identity
API, with local storage used only as a connection cache (last-known IP,
recently-viewed devices) — never as the record of truth. This is what makes
multi-operator, multi-factory use possible at all (P1-8/P0-2).

---

## 5. Observability Architecture

Three tiers, each with a distinct job — the mistake to avoid is putting
fleet-wide analytics on the device, or putting device-local real-time state
in the ERP (both are duplication in different directions).

**On-device (firmware):** real-time, ephemeral-by-default values (heap,
RSSI, queue depth, uptime — largely already correct in V1) **plus
persistent, NVS-backed cumulative counters** for DNS failures, TLS failures,
Wi-Fi reconnects, watchdog resets, brownout events, and total restarts. These
counters must survive reboots (stored in NVS, incremented on each event, not
reset to zero on boot) — that's the specific architectural fix for the V1
gap where only the *most recent* reset reason was visible, making a
constantly-flapping device indistinguishable from a healthy one. Also
on-device: fast, offline-capable **fault detection** that doesn't need a
round-trip to be useful — specifically sensor-stuck-at-zero detection (flow
expected but pulse count isn't moving), which firmware can flag locally the
moment it happens, queued for delivery like any other telemetry.

**Device Manager:** the real-time, single-device operational view (already
mostly correct in V1 — the diagnostics screen mirrors the local API well)
**plus** a fleet-wide rollup view driven by ERP data (not by polling every
device live) for "which devices are currently degraded/offline across all
20 plants" — this is the piece V1 has for a single device but not across a
fleet.

**ERP:** the durable, long-term store — every heartbeat's counters land
here as a time series, enabling trend dashboards ("Plant B has had 3x the
reconnect rate of the rest of the fleet this month"), cross-device
correlation, and alerting/escalation policy (who gets notified, and how,
when a device crosses from OK to ALARM). ERP is also the only place that can
correctly compute **OFFLINE** status — that requires knowing "when did we
last hear from this device" across the whole fleet, which no single device
can determine about itself.

**Health states**, defined once so DM/ERP/firmware all agree on the same
vocabulary: `OK` (no active faults) → `DEGRADED` (elevated but non-critical
counters — e.g. occasional reconnects) → `ALARM` (a definite fault: sensor
stuck, flash exhausted, repeated auth rejects) → `OFFLINE` (ERP-computed,
missed heartbeats past a threshold). Firmware and Device Manager can both
determine OK/DEGRADED/ALARM locally in real time; only ERP can determine
OFFLINE, since it requires a fleet-wide, time-aware view.

---

## 6. Factory Commissioning Workflow

Mapped onto the exact sequence requested, with owner and exit gate for each
stage:

| Stage | What happens | Owner | Exit gate |
|---|---|---|---|
| **New ESP32** | Bare hardware arrives on the factory line. | — | — |
| **Flash** | Base firmware image written via PlatformIO/esptool. (Phase 2: NVS/flash encryption enabled at this step for all new units, per §7.) | Flashing tool | Image boots, reports expected build SHA. |
| **Provision** *(= Registration, §3)* | Device Manager's factory-test flow calls the ERP to allocate a `logical_device_id` and a **per-device unique API key** (not a shared one), writes both to device NVS, prints the QR label. `Device.status = PROVISIONED`. | Device Manager ↔ ERP | ERP confirms the ID/key pair was durably recorded; QR label printed. |
| **Assign** *(= Commissioning, §3)* | On-site: technician scans the device's QR (avoids manual-typo mis-assignment), connects to its SoftAP via the **unified** commissioning wizard, selects Plant + Asset from the ERP's registry (dropdown, never free text), the wizard **live-tests** the entered Wi-Fi credentials before committing (fixes P1-3), writes config, device reboots onto site Wi-Fi. ERP creates the Installation record. `Device.status = COMMISSIONING`. | Device Manager (wizard) ↔ ERP | Device reappears and reports its `logical_device_id` back through a positive confirmation call (not inferred from mDNS presence alone) — the wizard correlates this against the Installation it just created. |
| **Calibrate** | Technician enters calibration for this Installation (or, on a Replacement, is offered the prior Installation's values to confirm/adjust) via Device Manager's calibration screen, submitted to the ERP, versioned per §2. | Device Manager ↔ ERP | A calibration version exists for the Installation with `effective_from = now` (or later, if scheduled). |
| **Verify** | The explicit, machine-checked gate V1 lacks: device must show one full successful heartbeat cycle (Wi-Fi + TLS + push 200 + ack advancing), its cached calibration version must match what the ERP just set, and it must have zero active ALARM-level faults. | Device Manager reads, ERP evaluates the gate | All three conditions true, together, before the next stage is reachable. |
| **Go Live** | `Device.status = ACTIVE`. ERP begins normal ingestion, reporting, and billing accounting for this Installation. | ERP | Status flip is atomic and logged. |
| **Maintenance** | Ongoing: Device Manager surfaces DEGRADED/ALARM states (§5); a field engineer can remotely reconfigure Wi-Fi, rotate the API key, or trigger an OTA update for this device — all without a truck roll (§7). `Device.status` may flip to `MAINTENANCE` temporarily without closing the Installation. | Device Manager ↔ ERP ↔ firmware | Returns to `ACTIVE` once resolved; every transition logged. |
| **Replacement** | A first-class "Replace Device" flow: closes the old Installation, decommissions the old device (secrets wiped, key revoked), runs Registration for the new unit if needed, then re-enters **Assign** with Plant/Asset pre-filled and the prior calibration offered for inheritance. | Device Manager ↔ ERP | Same Verify → Go Live gate as any new Installation — replacement is fast, never silent. |

---

## 7. Security Architecture

**API key lifecycle.** Move from V1's single shared-scheme static key to a
**per-device unique key**, issued by the ERP at the Provision stage.
Rotation triggers, all ERP-orchestrated: scheduled (e.g., annual), on-demand
(suspected compromise), and mandatory-immediate at Decommission/Replacement
(old key revoked the instant the Installation closes). Rotation must be
possible **remotely**, without requiring physical access to the device —
this is P0-6, and it's a security-architecture decision, not just a
convenience feature: without it, a suspected-compromised key can only be
revoked by dispatching someone to every affected site.

**Secret storage.** Device-side: enable NVS/flash encryption for all new
manufacturing (Phase 2) so the Wi-Fi password and API key are not
plaintext-recoverable via physical/JTAG access — this closes the most
serious single finding from the security review. ERP-side: keys stored
encrypted at rest; the raw value is returned to the operator exactly once
(at issuance/rotation) and never again — only a fingerprint is ever shown
afterward, matching the pattern already proven correct in the serial
console's redacted `show` command.

**OTA signing.** The existing design (ECDSA-P256 manifest signing + SHA-256
image-hash verification + anti-downgrade floor, verified Solid in the
architecture review) is retained as-is — it does not need to change. What's
needed: (1) the one-time production key-generation ceremony, replacing the
embedded test placeholder (already a hard compile-time gate — the release
build won't build without it, so this can't be silently skipped); (2) a
formal **key-rotation path** for the signing key itself, since rotating a
key that's embedded in every device in the field is a chicken-and-egg
problem — the canonical answer is a **dual-trust window**: a rotation is
itself shipped as a normal signed OTA (signed by the *current* key) whose
payload adds the *new* public key to the device's trusted set, with the old
key remaining valid for a defined overlap period before being retired in a
follow-up release. This means key rotation is a firmware capability that
needs to exist (accepting more than one trusted public key at a time) even
though it isn't exercised until it's actually needed.

**Recovery.** Retain serial-console-only recovery as the fallback of last
resort (a real security property — no remote attacker can wipe or
reconfigure a device this way) — but it should no longer be the *only* path,
now that remote reconfiguration (above) exists for the common case. Serial
recovery remains for the case where the device can't reach the network at
all.

**Secure Boot.** Not required for Phase 1 or Phase 2 — enabling it on
already-manufactured hardware is destructive, and V1's OTA signing/hash
verification already prevents any *unsigned* firmware from installing. It
becomes relevant once flash encryption (Phase 2) and the physical-compromise
threat model are fully addressed; scheduled as a Phase 3 hardening step
(one-time eFuse burn at manufacture, applied to new units going forward —
retrofitting deployed units is out of scope indefinitely).

---

## 8. Future Roadmap

**Phase 1 — needed for Balaji** (the single live deployment must not be put
at risk; changes here are additive/backward-compatible, not a re-flash of
the live device unless explicitly noted):

- Introduce the Installation/Asset/Calibration schema in the ERP; backfill
  Balaji's existing device as Installation #1 (one Plant, one Asset, its own
  calibration row) — **fixes the calibration-correctness defect without
  touching firmware**, since the device already only reports raw pulses.
- Issue Balaji's device a real per-device API key via the existing
  server-side rotate-key mechanism, replacing the shared/dev key.
- Execute the production OTA signing-key ceremony (blocks any real OTA push
  to Balaji until done, per the existing compile-time gate).
- Add NVS-backed cumulative failure counters and the sensor-stuck-at-zero
  check to firmware — small, additive, backward-compatible changes that
  immediately give Balaji real observability without changing its identity
  model.
- Do **not** attempt flash encryption / Secure Boot on the already-deployed
  Balaji unit — track it as legacy hardware, address at physical
  replacement time (§6, Replacement flow) rather than a risky field
  retrofit.

**Phase 2 — needed for commercial rollout** (the 500-device/20-factory
manufacturing wave):

- Full identity model (Customer/Plant/Asset/Device/Installation) live and
  driving both Device Manager and the ERP.
- Unified commissioning wizard (Provision → Assign → Calibrate → Verify →
  Go Live as one guided flow, replacing V1's two disconnected screens).
- Per-device unique API keys as the manufacturing standard.
- NVS/flash encryption enabled at manufacture for every new unit.
- Remote Wi-Fi/API-key reconfiguration (no-truck-roll maintenance).
- Bulk provisioning / per-plant Wi-Fi profile templates.
- The Replacement flow as a first-class Device Manager capability.
- Real sensor/flow-accuracy validation and certification per device/pipe
  combination (manufacturing QA gate, not a code change).

**Phase 3 — fleet management** (long-term scale features):

- OTA staged rollout rings + jitter, with a fleet-wide rollout dashboard in
  Device Manager.
- Fleet-wide observability and alerting/escalation policy in the ERP
  (trend dashboards, degraded-device triage queues across all plants).
- Secure Boot rollout for all newly manufactured units.
- Calibration governance UI (history, certificates, export; multi-point
  calibration only if real sensor-linearity data from Phase 2 shows a
  single K-factor scalar is insufficient).
- Formal signing-key rotation exercised in production (dual-trust window,
  designed in §7, used for the first time).
- Multi-tenant customer-level reporting/billing hooks in the ERP.
