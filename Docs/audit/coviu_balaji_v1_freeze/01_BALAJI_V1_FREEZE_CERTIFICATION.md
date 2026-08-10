# Balaji V1 Freeze Certification

**Question this document answers:** can the current firmware and Device
Manager, as they exist today, operate reliably for the next 12 months at
Balaji with exactly one production oil meter — and if not yet, what is the
smallest set of work that closes the gap? This is **not** an architecture
document (that's `Docs/COVIO_V2_CANONICAL_ARCHITECTURE.md`) and it does not
propose redesigning anything. It applies a single lens — **one device, one
tank, one plant, twelve months, then maintenance mode** — to every P0/P1
finding from the Product Readiness Review, and separately to any other
firmware/security finding worth carrying into this decision.

The two facts that do most of the work in this document: (1) most of the
Product Readiness Review's P0/P1 findings are **fleet-scale** concerns —
they exist because 500 devices across 20 factories need registries, bulk
tooling, and remote fleet management that one device simply does not; (2) a
small number of findings are about **single-device reliability and
correctness**, and those don't get cheaper by waiting — they apply exactly
as much to one meter as to five hundred.

---

## Disposition table — every P0/P1 item

| # | Finding | Disposition | One-line reason |
|---|---|---|---|
| P0-1 | Per-device calibration model (replace global singleton) | **Do later** | The *schema* is irrelevant at N=1 — one device using "the" calibration row is not a bug when there is only one row that could ever apply. (The *value* itself is handled separately, see Must-Do-Now.) |
| P0-2 | Device/asset registry + tank/plant assignment | **Never for Balaji** | There is exactly one device, one tank, one plant. No registry can be ambiguous with one row in every table. |
| P0-3 | NVS/flash encryption at rest | **Never for Balaji** | Enabling flash encryption on an *already-provisioned* device is a destructive operation (effectively a re-provision from scratch). Retrofitting is disproportionate risk for one physically-accessible, already-trusted device; new hardware should get this at manufacture time, not this unit after the fact. |
| P0-4 | Execute production OTA signing-key ceremony | **Must do now** | Blocks every future firmware update — the release build will not compile with the current test placeholder key. If a fix is ever needed at Balaji within the 12 months, this has to already be done. |
| P0-5 | Bulk/fleet provisioning | **Never for Balaji** | There is one device to provision, and it's already provisioned. |
| P0-6 | Remote reconfiguration (Wi-Fi/API key/restart, no truck roll) | **Do later** | A physical visit is a viable maintenance path for one device; it's only a bottleneck at fleet scale. Requires a documented fallback plan now (see Must-Do-Now), not the capability itself. |
| P0-7 | OTA trigger/rollout control in Device Manager | **Do later** | The underlying poll-and-verify OTA mechanism already works without a DM button; a manifest update server-side is sufficient to reach one device. The DM UI is a convenience that matters at fleet scale. |
| P0-8 | Real sensor/flow-accuracy validation | **Must do now** | The totalizer read a static value through the entire audit trail — the core measurement this product exists to make has never been proven correct against real oil flow. This is what Balaji's 12 months of data will actually mean. |
| P1-1 | Cumulative failure counters (DNS/TLS/reconnect/watchdog/brownout/restart) | **Must do now** | Cheap, low-risk, NVS-backed counters. Directly answers "is this device silently degrading" for the one device that has to run unattended for a year. |
| P1-2 | Sensor-stuck-at-zero alarm | **Must do now** | Cheap to add, and it's the single highest-value defense against exactly the kind of silent failure a 12-month unattended deployment is most exposed to. |
| P1-3 | Live Wi-Fi credential test before commit | **Do later** | Only matters during commissioning of a *new* device; Balaji's device is already live and stable. Only relevant again on a rare future replacement — see P1-4. |
| P1-4 | Config backup/restore / replacement flow | **Do later** | Relevant only if the physical device ever fails and needs replacing. A one-time manual re-provisioning (the same process already proven capable during commissioning) is an acceptable cost for a rare, low-frequency event on a single unit. |
| P1-5 | Working log export | **Do later** | Physical/serial access already gives full log visibility for one device; export automation earns its cost at fleet scale, not here. |
| P1-6 | Multi-Wi-Fi-profile support | **Never for Balaji** | Single plant, single network, no stated requirement for Wi-Fi failover at this site. |
| P1-7 | Wire `ota_debug` into Device Manager UI | **Do later** | The data is already computed and reachable via the raw JSON endpoint if ever needed; wiring it into the UI is a convenience that matters when many devices need triage at a glance. |
| P1-8 | Shared/cloud device registry (multi-operator) | **Never for Balaji** | One device, no multi-technician contention over its metadata to resolve. |
| P1-9 | Unified commissioning wizard | **Do later** | Only earns its cost when commissioning devices repeatedly; relevant again only on a rare replacement event, same as P1-4. |
| P1-10 | Positive commissioning verification / final health-check gate | **Do later** | Balaji's one device already went through a manual, rigorous, hands-on verification (the physical commissioning proof already on file) — that rigor is the acceptable substitute for automated tooling at N=1. Build the automated gate before the *next* device is commissioned anywhere, not for this one. |
| P1-11 | Calibration governance UI | **Do later** | Contingent on P0-1's schema change landing first, which is itself deferred — building UI on top of a deferred data model is out of order. |
| P1-12 | Explicit watchdog configuration | **Do later** | No evidence of hangs or crash-loops in the field to date (all resets in the commissioning logs are accounted for). Revisit if an actual incident occurs during the 12 months; don't speculatively touch working firmware otherwise. |

---

## 1. Must Do Now

These four items are the actual freeze list — the smallest set of work
required before declaring Balaji operational and moving into maintenance
mode. All are either physical/QA actions or small, low-risk, additive
firmware changes; none require the architecture changes in the canonical
design document.

**1. Execute the production OTA signing-key ceremony.**
The device's embedded public key is still the test placeholder
(`COVIO_OTA_KEY_IS_PLACEHOLDER=1`), and the release build is designed to
fail to compile until a real key is generated and embedded. This is a
one-time action, not a design change — the signing mechanism itself
(ECDSA-P256 manifest signing, SHA-256 image-hash verification,
anti-downgrade floor) was independently verified sound in the Product
Readiness Review and needs no changes. But if any firmware fix is needed at
Balaji during the 12-month window — and across a year, that's a realistic
possibility, not a hypothetical — this has to already be done, because
otherwise there is no way to ship that fix at all. Doing it now, while
nothing is urgent, is strictly better than doing it under pressure during an
incident.

**2. Validate real sensor/flow accuracy against actual oil flow, and confirm
the calibration value currently in effect is correct for Balaji's specific
meter and tank.**
Every session in this project's audit trail observed the totalizer holding
a static value — the actual accuracy of pulses-to-litres conversion for
this specific installation has never been confirmed against a real flow
reference. This is distinct from the calibration *schema* issue (P0-1,
deferred below): the schema question is "is the data model right for many
devices," and doesn't matter at N=1. This question is "is the *number*
correct for the one device that will produce Balaji's actual operational
and billing data for a year." That has to be answered before calling the
system production-ready, regardless of how many devices exist.

**3. Verify — and if necessary, rotate to — a real, uniquely-issued
production API key for Balaji's device.**
The Product Readiness Review flagged a direct conflict in the project's own
documentation: older docs describe the device's key as still the shared
development bootstrap key (`dev-key-change-me`), while the physical
commissioning session showed `api_key_status: "configured"` with a
fingerprint stated to match "the registered production key." These likely
describe two different points in time, but the freeze certification cannot
proceed on an unresolved ambiguity about the credential securing the one
production device. Confirm the current state directly; if it is still the
shared/dev key, rotate it now using the rotation mechanism that already
exists server-side — this is a same-day fix once the actual state is known,
not new development.

**4. Add persistent, NVS-backed cumulative failure counters (DNS
failures, TLS failures, Wi-Fi reconnects, watchdog resets, brownout events,
total restarts) and a sensor-stuck-at-zero alarm, and surface both in Device
Manager.**
These are the two cheapest, lowest-risk items in the entire Product
Readiness Review, and they are the direct answer to the question this
document is built around — "will we know if something is quietly going
wrong over 12 months without anyone watching." Today, only the single
most-recent reset reason is visible, so a device that reset ten times last
week looks identical to one that has been rock-solid since commissioning;
and a stuck sensor produces confident, silently wrong data with zero
operator signal. Both gaps are additive counters/checks layered onto
existing, already-working telemetry — they don't touch OTA, queue, Wi-Fi,
or calibration logic, which is exactly the kind of low-blast-radius change
appropriate to make right before a freeze.

**Also required before declaring "maintenance mode," as a process rather
than a code item:** since remote reconfiguration and remote OTA-trigger are
being deferred (below), Balaji needs a **documented physical-response plan**
— who visits the site, and within what timeframe, if the plant's Wi-Fi
changes, the API key needs emergency rotation, or a firmware fix needs to be
pushed and the automatic OTA poll isn't sufficient. Freezing the software
without freezing the operational plan around it would just relocate the
risk rather than close it.

---

## 2. Do Later (commercial rollout)

These eleven items are correctly out of scope for a one-device, one-plant
deployment. They aren't being rejected — they're exactly the items the
canonical architecture document already designs for — but building them now
would be work with no payoff until a second device, a second plant, or a
second customer exists.

- **P0-1, per-device calibration model.** The global-singleton schema is
  only a bug in the presence of a second device; Balaji's one row is
  correct by construction. Fix the schema when the second Installation is
  created, not before.
- **P0-6, remote Wi-Fi/API-key/restart reconfiguration.** A physical visit
  remains a viable maintenance path at N=1 (see the Must-Do-Now response
  plan) — this becomes necessary, not optional, once visiting every device
  in person stops scaling.
- **P0-7, OTA trigger/rollout control in Device Manager.** The mechanism
  functions today without it; the UI convenience earns its cost once
  someone has to manage a rollout across many devices at once, not one.
- **P1-3, live Wi-Fi credential test before commit.** Only exercised during
  commissioning; Balaji's device is done commissioning.
- **P1-4, config backup/restore / first-class replacement flow.** Relevant
  only if this specific device physically fails during the 12 months — an
  acceptable, rare, manually-handled event, not a standing requirement.
- **P1-5, working log export.** Physical/serial access is a complete
  substitute for one device.
- **P1-7, wiring `ota_debug` into the Device Manager UI.** The data already
  exists and is reachable directly if ever needed for troubleshooting;
  the UI polish pays off when many devices need triage at a glance.
- **P1-9, unified commissioning wizard.** Same reasoning as P1-3 — only
  earns its cost commissioning devices repeatedly.
- **P1-10, positive commissioning verification / final health-check gate.**
  Balaji's device already passed a manual, rigorous, hands-on verification;
  that substitutes for automated tooling at N=1. Build the automated gate
  before the *next* device anywhere is commissioned.
- **P1-11, calibration governance UI.** Sequenced after P0-1, which is
  itself deferred — building governance UI on a data model that hasn't
  changed yet would be built on the wrong foundation.
- **P1-12, explicit watchdog configuration.** No field evidence of hangs or
  crash-loops to justify touching working firmware speculatively; revisit
  if an actual incident occurs.

---

## 3. Never Needed for Balaji

These items are not being deferred — they are fleet-scale concerns that
this specific deployment, as scoped (one device, one tank, one plant, for
the foreseeable operational life of this installation), will structurally
never exercise. If Balaji's scope ever changes (a second tank or meter is
added at the same plant), that would be a new decision to revisit this
list, not an implicit trigger.

- **P0-2, device/asset registry + tank/plant assignment.** No registry can
  be ambiguous with exactly one row in every table it would populate.
- **P0-3, NVS/flash encryption at rest, for the currently deployed unit
  specifically.** Retrofitting encryption onto an already-provisioned
  device is a destructive re-provisioning event undertaken purely for a
  hardening benefit that a single, physically-supervised industrial device
  doesn't urgently need. New hardware should get this at manufacture time
  under the commercial-rollout process — this specific already-deployed
  unit is not a candidate for a risky retrofit.
- **P0-5, bulk/fleet provisioning.** There is one device, already
  provisioned. This capability has no object to act on at this site.
- **P1-6, multi-Wi-Fi-profile support.** Single plant, single network, no
  stated requirement for Wi-Fi failover.
- **P1-8, shared/cloud device registry (multi-operator).** One device
  means no contention between technicians over its metadata to resolve.

Two related items surfaced in the underlying security/firmware review that
aren't formally P0/P1-numbered but follow the same logic, included here for
completeness:

- **Secure Boot (eFuse-based signed app images).** Same reasoning as
  flash encryption above — a destructive, one-time hardware commitment that
  belongs in the manufacturing process for new units, not retrofitted onto
  Balaji's device.
- **Firmware flashing capability built into Device Manager.** The existing
  esptool/PlatformIO workflow was used successfully throughout this
  engagement; there's no operational gap to close for one device.

---

## Balaji V1 Freeze List (summary)

The complete, minimal set of work before declaring Balaji operational and
moving to maintenance mode:

1. Run the production OTA signing-key ceremony; replace the embedded test
   placeholder.
2. Validate real sensor/flow accuracy against actual oil flow at Balaji, and
   confirm the currently-active calibration value is correct for this
   specific installation.
3. Verify Balaji's device is running a real, uniquely-issued production API
   key (not the shared/dev key); rotate now if it isn't.
4. Add persistent cumulative failure counters (DNS/TLS/reconnect/watchdog/
   brownout/restart) and a sensor-stuck-at-zero alarm to firmware; surface
   both in Device Manager.
5. Document and agree a physical-visit response plan (who, and how fast)
   for Wi-Fi changes, emergency key rotation, or an urgent firmware fix,
   since remote reconfiguration is intentionally not part of this freeze.

Everything else raised in the Product Readiness Review is real, correctly
identified, and already has a canonical design waiting for it — it simply
isn't required to responsibly operate one meter, at one plant, for the next
twelve months.
