# Covio Device Manager — Live Readiness Plan
### (Provisioning, Live Monitoring, and Device Lifecycle Management for the Covio Oil Flow Meter Fleet)

**Status:** **Frozen for implementation.** No code has been written or changed as part of producing this document — this remains a specification only. Per explicit instruction, this document is not to be expanded further as of this revision; implementation should proceed against it as-is, with changes made only through the same deliberate-update discipline this document itself uses (state what changed and why, don't silently drift).
**Revision:** v4 — (v2) added lifecycle, identity, device-twin, alarms, event-timeline, API-versioning, diagnostics, fleet-UI, factory-SOP, calibration-governance-depth, and post-v1 roadmap sections (§11); (v3) added the "Covio Platform" naming clarification and a compact §12 note capturing 10 further operational-depth points without expanding them into full sections; (v4, this revision) corrected the governance mechanism for ADR-008/009 (a new superseding ADR — `ADR-017`/`ADR-018` — not the ACR workflow, which this project reserves for implementation-discovered impracticality; see §0), reordered §8's build sequence per review (contract-freeze and backend-auth first, a minimal desktop prototype before provisioning, ADR authorship before provisioning code), added DM-Phase 0A/0B/1.5/1.8, and added §13 — the full frozen REST/JSON/enum contract every later phase implements against. Nothing in §1–§7/§9–§12's substance was reverted; §8 was reordered and extended, and §0 was corrected for accuracy.
**Author context:** Produced after a full inspection of `covio_firmware/` — all firmware `.h`/`.ino` files, `server/server.py`, `SCHEMA_REGISTRY.md`, and every document in `Docs/` (`MASTER_GOVERNANCE.md`, the Audit-derived Freeze & Remediation Blueprint, the 16-entry ADR record, the Phase-wise Master Plan, `MILESTONES.md`, `ACR-001`).
**Relationship to existing governance:** This document does **not** silently override the project's existing frozen architecture. Where it conflicts with a frozen ADR, that conflict is called out explicitly in §0 and must be resolved through this project's own ADR-supersession process (`MASTER_GOVERNANCE.md` §4 — **not** the ACR workflow, §8; see §0's correction below) before the affected phase begins implementation.

**Framing — read this before anything else:** the single highest-leverage change in this revision is not a section, it's a mindset shift. Do not build this as a **"Provisioning App."** Build it as **Covio Device Manager** — provisioning is one tab in it, not its identity. Every mature device ecosystem converges on the same shape at scale: Siemens TIA Portal for PLCs, Ubiquiti UniFi for network gear, Hikvision SADP for cameras, Cisco Meraki for switches. All four do the same thing — discover, provision, monitor, diagnose, update, and manage a fleet from one console — regardless of how small they started. Designing §3.2/§8/§11.6 around that shape now costs little and avoids a rebuild in a year.

**One more naming note (v3):** "Covio Device Manager" is the desktop client, not the whole system. The umbrella is **Covio Platform** — firmware, desktop app, cloud/backend, API, Device Twin, OTA, fleet management, and (eventually) analytics all sit under it as components. "Covio Device Manager" names one of those components (the desktop app, §3.2/§11.6). This costs nothing to state now and means none of the above ever needs a rename as more components (a mobile client, §12 item 10; a real cloud fleet service, §11.9 V2) get added.

---

## 0. Governance Notice — Read This First

This project runs a formal architecture-governance process: `Firmware Detailed Architecture Decision Record (ADR).md` states plainly that **"any change to a decision recorded in ADR-001 through ADR-016 requires a new ADR that explicitly supersedes it,"** and `MASTER_GOVERNANCE.md` §4 requires that supersession to be recorded as **a new `ADR-0nn` entry, with the superseded one marked superseded, not deleted.**

**Correction from earlier drafts of this plan:** this is *not* the ACR (Architecture Change Request) workflow. `MASTER_GOVERNANCE.md` §8 is explicit that an ACR is raised "the moment a frozen ADR decision proves impractical during implementation" — a narrow, evidence-driven correction (see the real `ACR-001` for what that actually looks like: a specific sentence quoted, a specific defect encountered, a narrowest-possible fix). Nothing about ADR-008/ADR-009 has been "found impractical" — they were reasonable, deliberate decisions given the information available when they were written, and the business need has simply arrived since. The correct mechanism is a **new ADR that supersedes the old one**, authored using the exact same template as ADR-001–016 (Decision Name, Problem Statement, Business Goal, Architecture Decision, Reasoning, Alternatives Rejected, Trade-offs, Compatibility, Migration Strategy, Definition of Done, Future Extension), reviewed with the same rigor by the `Architecture Owner` — not the ACR template. This plan reserves the numbers **`ADR-017`** (supersedes `ADR-008`'s commissioning-mechanism clause only — its factory-self-test design otherwise stands) and **`ADR-018`** (extends `ADR-009`'s identity model, §11.2) for this. DM-Phase 1.8 (§8) is where these get authored and approved, before DM-Phase 2 begins.

**This plan directly revises ADR-008.** ADR-008 ("Factory Self-Test Firmware and Console-Based Field Verification") considered exactly this problem and made an explicit, reasoned decision:

> "AP/BLE/QR-based commissioning is explicitly deferred, not built, in this release... A full mobile app with BLE provisioning, built now — rejected for this release: disproportionate engineering investment relative to near-term fleet scale; recorded as a deliberate future extension, not silently dropped."

The business intent driving this plan — a factory user must be able to power on a device, discover it over WiFi, and configure it without a cable — is precisely the capability ADR-008 chose to defer. That deferral was reasonable at the time it was written; this document exists because the business need has now arrived. **This is not a defect in ADR-008; it is exactly the "Future Extension" ADR-008 itself names:** *"A mobile/BLE/QR commissioning tool can be added later as an additional front-end to the same underlying configuration setters the console already uses, without redesigning the provisioning data model."* This plan is that extension.

**Recommendation:** before DM-Phase 2 (provisioning mode) begins implementation, author and get approved a new `ADR-017 — LAN-Local Diagnostics & WiFi-Based Provisioning` that formally supersedes ADR-008's commissioning-mechanism deferral (see the correction above — this is a new superseding ADR, not an ACR). §5 and §6 below are written to be usable as that ADR's technical content directly; DM-Phase 1.8 (§8) is where it actually gets authored and reviewed. This document also touches a coupling risk the project's own Blueprint (Task 4/5) already flagged: `Sync` and `Ota` each independently query `WiFi.status()` rather than sharing one authority. Adding a third consumer (the new local API / provisioning logic) without resolving that first will make it worse — DM-Phase 2 explicitly includes fixing this as a prerequisite, not an afterthought.

Nothing below requires touching ADR-001 (schema) or ADR-003 (queue storage) — this plan is additive to those.

**This revision adds one more governance touch-point.** §11.2 (Device Identity) introduces a third identity tier alongside ADR-009's existing two (`hardware_id`, `asset_id`). This is additive — it does not change either existing field's meaning or remove anything ADR-009 already decided — but per `MASTER_GOVERNANCE.md` §4 it is still a change to a frozen ADR's identity model and should be recorded as a new superseding entry, reserved here as **`ADR-018`**, rather than added silently. Because it only adds a field, `ADR-018` is materially lower-risk and faster to approve than `ADR-017` above, and should not block DM-Phase 1/3 UI work that merely displays the new field once it exists. By contrast, §11.8 (Calibration Governance) requires **no new ADR at all** — the fields it adds were already named by ADR-010 itself as an anticipated future extension, not a conflict with it.

---

## 1. Current Firmware Limitations (Why WiFi-Based Reading/Provisioning Doesn't Exist Today)

Confirmed by direct inspection (`grep` across the full firmware tree for `softAP|ESPmDNS|WebServer|BLEDevice|BluetoothSerial|captive|WiFiManager|AsyncWebServer|mDNS` returned **zero matches**):

| Gap | Evidence | Consequence |
|---|---|---|
| **No local server on the device at all** | `sync.h`/`ota.h` only ever *originate* outbound `HTTPClient` requests; nothing calls `WiFiServer`/`WebServer::begin()` anywhere | The device cannot be *asked* anything over WiFi — it can only be told things by whatever `server_url` it already trusts. There is no LAN-local way to read or write it. |
| **No SoftAP / captive portal** | `sync.h::wifiConnect()` only ever calls `WiFi.mode(WIFI_STA)` | A device with wrong/no WiFi credentials has no way to be reached over WiFi at all — USB is the only recovery path. |
| **No mDNS/discovery** | No `ESPmDNS` include anywhere | Even a device that *is* on the LAN cannot be found without already knowing its IP (which requires router admin access or a cable). |
| **No BLE** | No `BLEDevice`/`BluetoothSerial` include anywhere | No proximity-based pairing channel exists as an alternative to WiFi. |
| **Provisioning is 100% USB-serial** | `provision.h` — `help`/`show`/`set url|key|wifi`/`reboot`/`factory`, all read from `Serial` | Every device requires a laptop + terminal + exact command syntax knowledge to configure — this is `F-28`/`F-29` in the Blueprint, both rated **P1**, and is the exact gap ADR-008 named and deferred. |
| **No health/diagnostic data is even collected yet**, let alone exposed | `ADR-002` (health channel), `ADR-004` (SD degradation states), `ADR-006` (watchdog/reset-reason) are all **fully designed, zero implemented** — confirmed no `esp_reset_reason`, no free-heap reporting, no SD-status field anywhere in `queue.h`/`totalizer.h`/`covio_firmware.ino` | Even once a local endpoint exists, most of what the user wants to *see* on it (SD health, reset reason, sensor-plausibility, OTA history) has to be built from scratch — it isn't sitting in the firmware waiting to be exposed. |
| **Receiver enforces zero authentication** | `server/server.py`'s `push()` route never reads `X-Api-Key` (confirmed via grep — zero matches) despite the firmware sending it on every request | Any client on the network the receiver is reachable from can inject fake telemetry or serve a malicious OTA manifest today — `F-23`, rated **P0**. Relevant here because a provisioning app that hands out API keys is meaningless if the receiver never checks them. |
| **`Sync` and `Ota` each independently poll `WiFi.status()`** | Blueprint Task 4/5 | Adding a third WiFi-state consumer (provisioning-mode logic) without consolidating this first compounds an already-documented coupling risk. |

**Bottom line:** today, "reading the device over WiFi" is not a partially-working feature that needs polish — it is a capability that does not exist in any form. Everything in §5–§7 below is new construction, not extension of existing code paths (with the one exception of reusing `Store`'s existing NVS setters, which ADR-008 itself already anticipated reusing).

---

## 2. Business Intent Restated

A factory or field operator must be able to:
1. Power on a Covio device with no meter/network yet configured.
2. Discover it over WiFi from a Windows PC (no USB cable).
3. Configure WiFi credentials, server URL, and API key entirely through a desktop app.
4. Subsequently monitor that device's live status — indefinitely, not just during setup — again with no cable.

This requires new capability on **three tiers**, all covered below: the **firmware** (a local API + provisioning mode), a **Windows desktop app** (discovery + config + monitoring UI), and the **backend/receiver** (a real device registry + enforced auth, since a provisioning tool that mints API keys is pointless against a receiver that ignores them).

---

## 3. Proposed Architecture

### 3.1 Firmware side

```
                    ┌─────────────────────────────────────────┐
                    │              ESP32 Device                │
                    │                                           │
  Boot ──────► WiFi station connect attempt (existing, Sync)    │
                    │        │                                  │
                    │   timeout / no creds                      │
                    │        ▼                                  │
                    │  SoftAP "Covio-Setup-XXXX" + captive       │
                    │  portal (NEW: wifi_provision.h)            │
                    │  DNSServer redirects all → 192.168.4.1     │
                    │  WebServer serves setup form                │
                    │        │  operator submits WiFi/URL/key     │
                    │        ▼                                  │
                    │  Store.setWifi/setServerUrl/setApiKey       │
                    │  (existing NVS setters, store.h — reused)   │
                    │        │  reboot                            │
                    │        ▼                                  │
                    │  Station mode connects normally             │
                    │        │                                  │
                    │        ▼                                  │
                    │  ESPmDNS advertises _covio._tcp.local       │
                    │  (NEW: local_api.h)                         │
                    │        │                                  │
                    │        ▼                                  │
                    │  WebServer :80  GET /status  (NEW)          │
                    │   — read-only, no secrets, always on        │
                    │        │                                  │
                    │        ▼                                  │
                    │  (existing) outbound push/config/OTA to     │
                    │  server_url — UNCHANGED                     │
                    └─────────────────────────────────────────┘
```

Key architectural choices and why:

- **`WebServer.h` (synchronous, built into `arduino-esp32` core), not `ESPAsyncWebServer`.** The existing loop is a deliberately simple, synchronous, cooperative scheduler (`covio_firmware.ino`). Introducing an async web stack now would be a bigger architectural shift than this problem needs, and would touch every module's assumption about single-threaded execution. `WebServer::handleClient()` called once per `loop()` iteration is a two-line addition, consistent with how `provision.service()` and `syncEngine.wifiService()` already work.
- **AP mode is a fallback state, not a permanent mode.** The device is normally in pure `WIFI_STA` mode. SoftAP only activates when station connection has not succeeded within a bounded timeout, or when explicitly triggered by a new `provision` console command (for reconfiguring an already-commissioned device without a factory reset). It automatically stops broadcasting once station WiFi is confirmed — an always-on open setup network is a standing attack surface and is explicitly rejected.
- **The local `/status` endpoint is read-only and unauthenticated by design.** It never returns the raw API key or WiFi password (only a masked status: `"api_key": "configured"` / `"default"`, never the value) — this makes it safe to leave always-on with no local auth scheme, matching the risk profile of exposing telemetry (not secrets) to anyone already on the same LAN.
- **Config *writes* are restricted to AP/provisioning mode only**, not exposed on the normal station-mode local API. This is the compensating control for not having real per-device TLS on a LAN-local self-hosted endpoint: physical/RF proximity to a device's own SoftAP is the trust boundary, exactly mirroring ADR-005's own stated principle that *"physical possession is already the architecture's own stated full-trust boundary."* A later phase (§6, DM-Phase 5) can add authenticated station-mode remote config once real per-device credentials exist.
- **mDNS, not IP scanning.** The desktop app should never need the operator to know or guess a device's IP. `ESPmDNS`'s `_covio._tcp.local` service (with `device_id`/`fw`/`model` as TXT records) is the discovery mechanism; manual "add by IP" remains a fallback for networks that block mDNS multicast.

### 3.1a Local API versioning & the full endpoint set

Every endpoint below is versioned from day one (`/api/v1/...`) even though only one or two exist at first — this is cheap insurance now and expensive to retrofit later, and it's exactly the same reasoning this project already applied to `ADR-001` (telemetry schema versioning) and `ADR-007` (NVS config schema versioning): a version prefix costs nothing when there's only one version, and saves a breaking change the day there's a second one.

| Path | Method | Purpose | Auth | Status |
|---|---|---|---|---|
| `/api/v1/info` | GET | Static identity: `hardware_id`, `logical_device_id` (§11.2), `asset_id`, firmware version, model | none (no secrets) | New — DM-Phase 1 |
| `/api/v1/status` | GET | Human-facing summary: WiFi state, server URL, masked API-key status, SD present/absent, queue backlog, total pulses, last sync, OTA state | none | New — DM-Phase 1 (this is what §4/§5/§8 refer to elsewhere in this document as "`/status`") |
| `/api/v1/health` | GET | Derived OK / DEGRADED / OFFLINE state plus any active alarms (§11.4) | none | New — DM-Phase 1/§11.4 |
| `/api/v1/metrics` | GET | Deep diagnostics, distinct from `/status` — see table below | none | New — see this subsection |
| `/api/v1/logs` | GET | Recent ring-buffer log lines, once `ADR-012`'s buffer exists | none | Depends on unimplemented ADR-012; return `501` honestly until then, don't fabricate |
| `/api/v1/config` | POST | Write WiFi/server/API-key | **AP-mode only** (§3.1, §5) | New — DM-Phase 2 |

**Diagnostics (`/api/v1/metrics`) is deliberately a different endpoint from status, not more fields bolted onto it** — status answers "is this device OK," diagnostics answers "why," and only an engineer investigating a specific unit needs the second one:

| Field | Source | Notes |
|---|---|---|
| Free heap / heap low-water mark | `ESP.getFreeHeap()` | New — cheap, also seeds `ADR-002`'s future health record |
| Reset reason | `esp_reset_reason()` | New — ties to `ADR-006` (watchdog), unimplemented today |
| CPU frequency, flash size/usage | `ESP.getCpuFreqMHz()`, `ESP.getFlashChipSize()` | New, trivial |
| SD write latency | timing around existing `totalizer.h`/`queue.h` writes | New instrumentation |
| Queue read latency | timing around `pending()` | New instrumentation |
| Instantaneous pulse frequency | derived from two `totalizer.total()` samples | New — also feeds the `PULSE_FREQ_IMPOSSIBLE` alarm, §11.4 |
| Network RTT | timing around the existing `HTTPClient` calls in `sync.h` | New |
| WiFi RSSI history (short buffer) | `WiFi.RSSI()` sampled periodically | New — a small in-RAM ring, not SD-persisted |
| Temperature | — | **Not applicable to this hardware build.** The MAX31865 RTD module was explicitly removed from this build (see `config.h`'s header comment); do not add a temperature field until/unless that sensor is reintroduced as its own scoped decision. |

### 3.2 Windows desktop app

**Recommendation: Electron**, packaged as a Windows installer (NSIS via `electron-builder`), for consistency with this ERP's existing stack (the weight-station/admin-ui/sync-service tooling already referenced elsewhere in this codebase are Electron-based) — this avoids introducing a second, unrelated desktop toolchain for one small tool. A lighter native alternative (e.g., a minimal .NET/WPF app) was considered and rejected for this plan: it would need its own mDNS client and HTTP stack built from scratch, whereas Electron gets both from existing npm packages (`bonjour`/`multicast-dns`, `node-fetch`) with far less code.

**Critical Windows-specific gotcha to plan for:** Windows has **no built-in mDNS resolver** the way macOS does. The app cannot rely on the OS to resolve `*.local` names — it must perform its own mDNS query over UDP 5353 using a bundled library (e.g., `multicast-dns` for Node). Do not assume `ping device.local` will work on a factory Windows PC without Bonjour installed; the app must not depend on that.

The app talks **only** to the firmware's new local HTTP API (§3.1/§3.1a) and, separately, to the backend registry (§3.3) for commissioning/key-issuance — it never needs direct DB access.

**Module map (per the framing note above — this is "Covio Device Manager," not "the provisioning tool"):** Dashboard (fleet-wide online/offline/alarm counts) · Devices (searchable, filterable, taggable list) · Discovery · Provisioning · Live Monitor (per-device `/api/v1/status`+`/health`) · Logs · OTA · Calibration · Diagnostics (`/api/v1/metrics`) · Factory Test · Settings. Full detail in §11.6. Provisioning is one tab among these, not the app's identity — building the navigation shell this way from DM-Phase 3 onward costs nothing extra now and avoids restructuring the whole app later when the other tabs' data sources (alarms, timeline, diagnostics) land.

**Fleet-scale from day one, even at 3 devices:** the Devices list should be built on a filterable table component with tag/group/location fields present in the data model from the start (unused at pilot scale, structurally there so 1,000 devices doesn't force a schema migration later) — see §11.6.

### 3.3 Backend/receiver integration

The reference `server/server.py` is explicitly self-documented as bench-only (and this plan does not propose growing it into a production system — see ADR-016, which already froze this: *"the reference receiver is explicitly designated bench/development-only... whichever team owns the production receiver must build it on a real multi-worker server"*). This plan adds exactly the minimum registry/auth surface needed to make the desktop app's commissioning flow real, in `server.py` for bench/pilot use, with an explicit note that production deployment must land this in this ERP's real receiver (LCS/Django), which was outside the scope of this inspection and should be coordinated with whoever owns that integration.

**Reframe the registry as a Device Twin, not a lookup table.** A row that only holds `device_id`/`api_key_hash` answers "does this device exist" — it doesn't answer "what is this device doing right now" or "what has happened to it." §11.3 defines the full model: a *current* half (firmware, calibration, config, heartbeat/health, queue depth, active alarms, OTA state — one row per device, overwritten in place) and a *historical* half (event timeline §11.5, calibration history per `ADR-010`, firmware/OTA history, connectivity history — append-only). DM-Phase 4 below builds the seed of this; treat it as the seed of the twin, not a separate thing to build later.

---

## 4. What the App Shows (Mapped to Data Source)

| Field | Source | Status today |
|---|---|---|
| Device list / discovery | mDNS `_covio._tcp.local` browse (app-side) | New — requires `local_api.h` (DM-Phase 1) |
| Device ID | `store.deviceId()` | Exists — just needs exposing via `/status` |
| Firmware version | `FW_VERSION` (`config.h`) | Exists |
| WiFi status (SSID/RSSI/connected) | `WiFi.status()`, `WiFi.RSSI()`, `store.wifiSsid()` | Exists — needs a getter exposed |
| Server URL | `store.serverUrl()` | Exists |
| API key status (masked, never raw) | `store.apiKey().length() > 0 && != DEFAULT_API_KEY` → boolean | Exists as data; the *masking logic* is new |
| SD card health | none today | **New** — needs a runtime check (ties to ADR-004, currently unimplemented) |
| Queue backlog | `EventQueue::pending()` exists but is a scan, not a cheap counter | Needs a maintained counter (see DM-Phase 1 design note — do not reintroduce Blueprint's own F-05 O(n) rescan cost for a status ping) |
| Acked / latest sequence | `ack_.acked_seq` (private in `queue.h`), `totalizer.lastSeq()` | Exists internally — needs getters |
| Total pulses | `totalizer.total()` | Exists |
| Last sync (timestamp) | none today | **New** — `Sync` needs to record `millis()` of its last successful `ackSeq` parse |
| Error logs | none today (Serial-only) | **New** — ties directly to unimplemented `ADR-012` (log ring buffer); until that lands, be honest in the UI ("not yet available") rather than fabricate |
| OTA status | `Ota::pendingVerify_`/`confirmed_` (private) | Exists internally — needs a getter/enum |
| Heartbeat / health state | none today (ADR-002 unimplemented) | **New** — a derived OK/DEGRADED/OFFLINE computed client-side from last-sync recency + SD status, until the real ADR-002 health record exists server-side |

---

## 5. Firmware Changes Required

| Change | New/modified file(s) | Ties to |
|---|---|---|
| Local diagnostics data model + getters on existing classes (`Sync::lastAckMs()`, `Sync::lastPushCode()`, `Ota::state()`, `EventQueue::pendingCount()` backed by a maintained counter, not a rescan) | `sync.h`, `ota.h`, `queue.h` | New — prerequisite for everything else |
| `GET /status` (JSON) + `GET /` (HTML) local read-only endpoint | new `local_api.h`; wired into `covio_firmware.ino` (`server.handleClient()` in `loop()`) | New |
| mDNS advertisement (`_covio._tcp.local`, TXT = device_id/fw/model) | `local_api.h` | New |
| Provisioning-mode SoftAP + captive portal (DNS redirect + config form → existing `Store` setters) | new `wifi_provision.h`; boot-time timeout logic in `covio_firmware.ino` | New; **requires ADR-017 (supersedes ADR-008) approved first** (§0, DM-Phase 1.8) |
| `provision` console command (re-enter AP mode on a running device without a factory reset) | `provision.h` | Small, low-risk addition |
| Consolidate WiFi-connectivity authority into one module (currently split across `Sync` and `Ota`) | `sync.h`, `ota.h` | Closes a Blueprint Task 4/5 finding this plan would otherwise make worse |
| Safe config write/read: writes accepted **only** while in AP/provisioning mode | `wifi_provision.h` | Compensating control in place of real local TLS (§3.1) |
| WiFi fallback (station timeout → AP mode) | `covio_firmware.ino`, `sync.h` | New |
| Basic SD status check exposed locally (present/absent at minimum; full degradation state machine is ADR-004's job, out of scope here) | `local_api.h` reusing existing `SD.begin()`/`SD.exists()` | Partial — do not attempt to reimplement all of ADR-004 as a side effect of this plan |
| Structured local error/status fields (reset reason via `esp_reset_reason()`, free heap via `ESP.getFreeHeap()`) | `local_api.h` | Cheap wins that also seed ADR-002/ADR-006 when those land later — build once, reuse |
| Root/`src/` duplication | all of the above | Per `ADR-013`, do the root/`src` dedup **before** this work, not after — otherwise every file in this table must be hand-mirrored twice, which is exactly the drift risk ADR-013 already flagged |

**Explicitly not in scope for this plan** (do not build as a side effect): the full ADR-002 health-record channel over the cloud push pipeline, ADR-004's complete SD degradation state machine, ADR-006's watchdog, ADR-012's log ring buffer. This plan builds the minimum real data needed for local/LAN visibility now; those ADRs remain the authoritative design for the cloud-side equivalents and should be implemented on their own schedule, ideally reusing the same accessor functions this plan introduces rather than duplicating them.

---

## 6. Backend/Server Changes Required

| Change | File | Priority |
|---|---|---|
| Enforce `X-Api-Key` on `/api/iot/flow/push`, `/config`, `/ota/manifest` — reject with 401 if missing/invalid/revoked | `server/server.py` | **P0 — do this immediately, independent of everything else in this plan.** Firmware already sends the header; the receiver simply never checks it. Zero firmware change required. |
| `devices` registry table (`device_id`, `api_key_hash`, `asset_label`, `provisioned_at`, `revoked`) | `server/server.py` | Required before auth enforcement, so legitimate existing bench devices aren't locked out |
| `POST /admin/devices/provision` — generates and returns a fresh unique API key for a new device, for the desktop app to write into a device during AP-mode setup | `server/server.py` | New — closes ADR-005's "unique per-device key at commissioning" requirement without waiting on full Secure Boot/TLS |
| Config-change audit trail (who/when/old/new for server_url/api_key/wifi) | `server/server.py` | Ties to ADR-007 |
| Calibration-change audit trail (immutable, append-only) | `server/server.py` | Ties to ADR-010 — already flagged **P0** in the original audit (`N-04`) independent of this plan |
| `/admin/devices` dashboard: registry + last-seen + revoke action | `server/server.py` | New |
| **Production note:** none of the above should be mistaken for "the production receiver." ADR-016 already froze that the reference `server.py` is bench-only and a real production receiver (this ERP's LCS/Django stack) must independently implement the same authenticated contract. This plan does not audit that receiver — coordinate separately with whoever owns it before treating any of this as field-ready. | — | — |
| Unified `device_events` table (Event Timeline, §11.5) that config-audit and calibration-audit both write into as typed rows, rather than three parallel history mechanisms | `server/server.py` | New — see §11.5 for why this should be one table, not three |
| Alarm rows (§11.4) derived from the same event stream, filtered to severity ≥ WARNING | `server/server.py` | New — an alarm and a timeline event are the same underlying row; alarms are a filtered, severity-tagged view, not separate storage |

---

## 7. P0 Security — Live Readiness

| Item | Plan |
|---|---|
| **Enforce auth on receiver** | §6, immediate, no firmware dependency. This alone closes `F-23`. |
| **Remove hardcoded default credentials** | `DEFAULT_API_KEY "dev-key-change-me"` and placeholder WiFi creds in `config.h` stay as *first-boot bootstrap values only* — the AP-mode provisioning flow (§5) is what's supposed to overwrite them on every real unit. Add a loud runtime warning (Serial + local `/status` field `"insecure_default_credentials": true`) if a device is still running on the default key past first boot, so this is visible instead of silent. |
| **TLS/HTTPS plan** | Follow ADR-005 as already frozen: HTTPS + CA pinning on all four cloud-facing endpoints, `WiFiClientSecure`/`setCACert` replacing the current `WiFiClient`/`setInsecure()`-free-but-still-plaintext path. The new local LAN endpoint is treated differently (§3.1) — read-only and unauthenticated, writes restricted to physical-proximity AP mode — as a deliberate, documented compensating control, not an oversight. |
| **OTA signing / Secure Boot** | Follow ADR-005 as already frozen: ESP-IDF Secure Boot V2 + Flash Encryption burned at first factory flash, RSA-3072 keys held by Covio, never in source control. **This is one-way (eFuses) and cannot be retrofitted** — must land before any unit destined for the field is flashed, not after. |
| **API key rotation/revocation** | New: `POST /admin/devices/<id>/rotate-key` + `revoked` flag (§6). Because the device has no inbound push channel (a deliberate, existing architectural constraint — see ADR-012's reasoning for why log retrieval is poll-based), a rotated key can only be delivered to an already-fielded device via a provisioning-mode re-touch (§5), not silently pushed. This limitation should be stated to operators, not hidden. |
| **Prevent malicious fake data injection** | Closed by the combination of: enforced auth (§6) + unique per-device keys issued at commissioning (§6) + eventual TLS (ADR-005). Auth enforcement alone is the single highest-leverage fix and should not wait for the rest. |

---

## 8. Phased Implementation Roadmap

*Phase numbering below is prefixed `DM-` (Device Manager) to avoid collision with this project's existing "Phase 1–9" (Blueprint sequencing) and "Phase 0–11" (Master Plan) numbering, which refer to unrelated architecture work already in flight.*

### Build Order (frozen — do not reorder without updating this note)

The phases below are listed in the order they must actually be **built**, which is not simply ascending phase-number order. Reasoning, in order:

1. **DM-Phase 0A (Interface & Data-Model Freeze) comes first, before any code.** Firmware, desktop app, and backend are three independently-built codebases from DM-Phase 1 onward. If any two of them guess at a JSON field name or an enum value independently, they will guess differently, and the mismatch surfaces late — usually as a confusing runtime bug, not a compile error. §13 (Appendix A) is this phase's deliverable and is authoritative; every later phase implements *against* it, never invents a field it doesn't define.
2. **DM-Phase 0B (backend auth) comes next, ahead of any firmware work**, because it is fully independent of firmware (the firmware already sends the header — see §1), is small, and permanently closes the single largest security hole (`F-23`) the moment it lands. There is no reason to wait.
3. **DM-Phase 1 (local diagnostics) comes before anything provisioning-related**, because it is the first user-visible win, has no governance dependency, and is what a technician actually notices first: power on, open a browser, see status, no cable.
4. **DM-Phase 1.5 (a deliberately tiny Electron prototype) comes before DM-Phase 2's provisioning UI**, not after, because mDNS-from-Windows, Electron packaging, and this project's local API are three new, unproven things. Proving they work together against a **read-only** endpoint first is cheap; discovering a problem with any of them *after* the provisioning wizard is also built is expensive.
5. **DM-Phase 1.8 (author and approve `ADR-017`/`ADR-018`) comes before DM-Phase 2's code**, not after, per this project's Rule 1 (§0, `MASTER_GOVERNANCE.md`): a frozen ADR is never quietly redesigned around. By this point in the build order, the API contract (0A) and the local-diagnostics UI (1, 1.5) already exist and work, so the ADR text being reviewed is describing something already partially proven, not a paper design — which should make review faster, not slower.
6. **DM-Phase 2 (provisioning) is the first phase that changes device *lifecycle* behavior** (boot-time AP fallback, a new console command) — it is deliberately last among the "make it work" phases, gated on everything above.
7. **DM-Phase 3 (full desktop app) → DM-Phase 4 (backend registry/Device Twin, minus what 0B already shipped) → DM-Phase 5 (security hardening) → DM-Phase 6 (factory SOP)** proceed in that order as originally sequenced — nothing about the reordering above changes their internal content, only what now precedes them.

### DM-Phase 0 — Repo & Current-State Audit
- **Files touched:** none (read-only).
- **Business outcome:** shared, accurate understanding before any code changes — this document.
- **Technical work:** completed as the basis for this plan (§1–§4).
- **Acceptance criteria:** this plan reviewed and accepted by the project's Architecture Owner.
- **Test cases:** N/A.
- **Rollback risk:** none.
- **What the end user sees:** nothing yet.

### DM-Phase 0A — Interface & Data-Model Freeze
- **Files touched:** none in `covio_firmware/` — this phase's only deliverable is documentation: §13 (Appendix A) of this same file.
- **Business outcome:** firmware, desktop app, and backend can now be built largely independently, each against a single frozen contract, instead of the three converging on the same guesses by accident or discovering mismatches at integration time.
- **Technical work:** (1) finalize every REST path, method, and auth rule; (2) finalize every JSON request/response field, type, and nullability; (3) finalize every enum's exact allowed values (`api_key_status`, `sd_status`, `ota_state`, `health_state`, `alarm_type`, `alarm_severity`, `reset_reason`, `device_lifecycle_state`); (4) finalize the HTTP status code and error-envelope convention; (5) finalize the mDNS service/TXT record format; (6) decide, explicitly, hand-rolled JSON string-building vs. introducing `ArduinoJson` (this plan's answer: hand-rolled, to stay compliant with `MASTER_GOVERNANCE.md` §3's "no new dependency without an ADR" rule without needing yet another governance artifact — revisit only if it becomes genuinely painful once the schema is this well-defined). §13 is the resulting, single source of truth — no other section of this document should be treated as authoritative for an exact field name or path if it conflicts with §13.
- **Acceptance criteria:** §13 exists, is internally consistent (every field referenced elsewhere in this document — §3.1a, §4, §11.4, §11.5 — matches §13 exactly), and is reviewed/accepted before any of DM-Phase 1/1.5/3/4 begins writing code against it.
- **Test cases:** N/A — this is a specification phase. The test is whether DM-Phase 1's implementation matches §13 without needing to change it (a §13 change discovered necessary during DM-Phase 1 is itself a signal this phase wasn't actually finished).
- **Rollback risk:** None — documentation only.
- **What the end user sees:** nothing yet.

### DM-Phase 0B — P0 Backend Authentication
- **Files touched:** `server/server.py` only. No firmware change — the device already sends `X-Api-Key` on every request (`sync.h`, `ota.h`); the receiver has simply never checked it.
- **Business outcome:** closes `F-23` (zero authentication enforcement) — the single highest-leverage, lowest-effort security fix identified in the original audit — independent of every other phase in this plan.
- **Technical work:** (1) add a `devices` table (`device_id`, `api_key_hash`, `revoked` — the fuller registry/audit schema is DM-Phase 4's job; this phase adds only what's needed to check a key); (2) for the bench/pilot transition, seed this table with the existing shared `DEFAULT_API_KEY` as a temporarily-valid entry so already-running bench units aren't locked out (§8's Build Order already flags this sequencing risk); (3) add a check at the top of `/api/iot/flow/push`, `/config`, and `/ota/manifest`: missing/unknown/revoked key → `401` with the §13 error envelope, request never reaches the records table.
- **Acceptance criteria:** a request with a valid key behaves exactly as today; a request with a missing, wrong, or revoked key is rejected with `401` and produces no side effect (no row written, no ack computed).
- **Test cases:** (1) valid key → 200, unchanged behavior; (2) missing `X-Api-Key` header → 401; (3) wrong key → 401; (4) a key marked `revoked` → 401; (5) confirm zero regression for every existing bench device already seeded in step 2 above.
- **Rollback risk:** Low, if sequenced per step 2 above (seed real/legacy keys before enforcing) — flipping enforcement on without that step first would lock out every currently-running bench unit.
- **What the end user sees:** no visible change for a correctly-configured device; a spoofed/unauthenticated client is now rejected instead of silently accepted.

### DM-Phase 1 — Local Diagnostics + Read-Only Status Endpoint
- **Files touched:** `covio_firmware.ino`, `sync.h`, `ota.h`, `queue.h` (new getters + maintained backlog counter), new `local_api.h`. (Do the ADR-013 root/`src` dedup first, or mirror every change by hand into `src/`.)
- **Business outcome:** a technician on the same WiFi can see live device status from a browser or the future app, with zero cable — this alone directly answers "how do I read my device when switched on."
- **Technical work — build in this exact order, each step compiling and bench-testable before the next:**
  1. **Getters only, no new behavior.** Add to `sync.h`: `uint32_t lastAckMs()` (record `millis()` at the point `ackThrough()` succeeds in `pushOnce()`), `int lastPushHttpCode()` (record `code` from the existing `http.POST()` result). Add to `ota.h`: an `OtaState state()` accessor returning `none`/`pending_verify`/`confirmed`/`failed` derived from the existing private `pendingVerify_`/`confirmed_` fields plus the last `doUpdate_()` result. Add to `queue.h`: `uint32_t pendingCount()` — **do not** implement this by calling `pending()` with a large buffer (that's the exact O(n) rescan the Blueprint's `F-05` already flags); instead add a maintained `unackedCount_` member, incremented once per `append()`, recomputed once at boot via a single full scan in `begin()`, and decremented by walking forward in `ackThrough()`'s existing cursor-advance loop (§ — see `queue.h`'s current `ackThrough()`) rather than rescanning. Bench-test each getter against the existing Serial `show` output before moving on.
  2. **`diagnostics.h` (new).** A small struct-building module with one function per §13 endpoint (`buildInfoJson()`, `buildStatusJson()`, `buildHealthJson()`, `buildMetricsJson()`) that calls the getters from step 1 plus existing public methods (`store.deviceId()`, `totalizer.total()`, `WiFi.RSSI()`, etc.) and hand-builds the exact JSON shape §13 defines — string concatenation, matching this project's existing dependency-free convention (`telemetry.h`'s `toJson()`), not a new parsing library (§0A's decision).
  3. **`local_api.h` (new).** A `WebServer` instance bound to port 80, routes for every GET path in §13's endpoint table, each handler calling the matching `diagnostics.h` builder function and calling `server.send(200, "application/json", json)`; the `/api/v1/logs` route returns the §13-specified `501` until `ADR-012` exists. Add an `ESPmDNS` `begin()` + `addService("covio","tcp",80)` call with the TXT records §13.6 specifies.
  4. **`covio_firmware.ino` wiring.** Instantiate the new `LocalApi` object as a new global (per `MASTER_GOVERNANCE.md` §2's "no new globals beyond what an approved phase calls for" — this phase is that approval); call `localApi.begin(&store, &totalizer, &eventQueue, &syncEngine, &ota)` once after `syncEngine.wifiConnect()` in `setup()`; call `localApi.service()` (which internally calls `server.handleClient()`) once per `loop()` iteration, positioned so a hung/slow HTTP client can never block the 1-second telemetry cadence — bench-test this specifically (step 6 below).
  5. **HTML fallback.** `GET /` renders the same data as `/api/v1/status` as plain HTML, for a factory tech with only a browser and no app.
  6. **Bench verification pass.** Confirm every field in a live `/api/v1/status`/`/api/v1/info`/`/api/v1/health`/`/api/v1/metrics` response matches §13 exactly (field names, types, enum values) — this is the concrete check that DM-Phase 0A's contract was actually followed, not reinterpreted.
- **Acceptance criteria:** every endpoint in §13's table is reachable at `http://<ip>/api/v1/...` and `http://covio-xxxxxx.local/api/v1/...`; every response matches §13's schema exactly; no secret value (`api_key`, `wifi_pass`) ever appears in any response body.
- **Test cases:** (1) fresh boot → status reachable within N seconds of WiFi join; (2) SD removed → `/api/v1/status`'s `sd_status` reflects `absent` without crashing the local server; (3) forced offline period → `queue.backlog` matches actual pending rows exactly, verified the counter never drifts from ground truth across a reboot (this is the specific regression the maintained-counter design in step 1 exists to prevent — test it deliberately, don't assume it); (4) OTA trial in progress → `ota.state` shows `pending_verify`; (5) simultaneous Serial console use + local HTTP request → no interference, no dropped telemetry cycle; (6) `ping covio-xxxxxx.local` and an app-side mDNS query both resolve from a real Windows machine (§3.2's Bonjour caveat); (7) a client that opens a TCP connection and sends nothing (deliberately slow-loris-style) does not stall the main loop's telemetry cadence beyond one cycle.
- **Rollback risk:** Low — purely additive; if `WebServer::begin()` fails, log and continue, never block `loop()`.
- **What the end user sees:** a status page in a browser; nothing else changes.

### DM-Phase 1.5 — Minimal Desktop Prototype (Discovery + Status Only)
- **Files/artifacts:** new `covio_firmware/tools/device-manager/` Electron project — but deliberately minimal: a discovery list and a single JSON viewer, nothing else. This is throwaway-validation code that DM-Phase 3 later grows from, not a separate codebase to maintain in parallel.
- **Business outcome:** proves the three riskiest, least-proven pieces of this whole plan — Windows + mDNS, Electron packaging, and the new local API — work together, before any provisioning complexity is added on top. Cheap to discover a problem here; expensive to discover the same problem after DM-Phase 2/3 are also built on the same assumptions.
- **Technical work:** (1) minimal Electron shell (main + renderer, no installer yet); (2) bundle a Node mDNS client (`multicast-dns` or `bonjour`) and browse for `_covio._tcp.local`; (3) on selecting a discovered device (or entering an IP manually), call `GET /api/v1/status` and `/api/v1/info` and render the raw JSON; (4) nothing else — no config writes, no wizard, no styling investment.
- **Acceptance criteria:** discovers a real DM-Phase-1 device on the same LAN within 10s from a real Windows machine (not just a dev machine with Bonjour already installed — test on a clean factory-floor-representative PC); displays its `/api/v1/status` JSON correctly.
- **Test cases:** (1) discovery works on a Windows PC with no prior Bonjour/mDNS software installed (this is the specific risk this phase exists to retire — see §3.2's gotcha); (2) discovery correctly lists 2+ devices on the same LAN with no duplicates or crashes; (3) manual add-by-IP fallback works when mDNS is blocked by network policy (test on a network with multicast disabled, if available, to confirm the fallback path is actually exercised).
- **Rollback risk:** None — this code is explicitly disposable/foundational, not a production artifact; DM-Phase 3 replaces it.
- **What the end user sees:** nothing yet — this phase is internal validation, not a shipped tool.

### DM-Phase 1.8 — Author & Approve ADR-017 / ADR-018
- **Files touched:** `Docs/Firmware Detailed Architecture Decision Record (ADR).md` (two new entries appended: `ADR-017`, `ADR-018`), `Docs/MASTER_GOVERNANCE.md`'s `PROJECT STATUS`-equivalent tracking (per §4's documentation rule), `Docs/Firmware Architecture Freeze & Remediation Blueprint.md` is **not** touched (Rule 9 — it's a frozen historical snapshot, never edited).
- **Business outcome:** the project's own governance is honored — DM-Phase 2 (the first phase that changes device *lifecycle* behavior) does not begin on a quiet, unreviewed reinterpretation of a decision the project already froze.
- **Technical work:** author `ADR-017` and `ADR-018` using the **exact same template** every existing ADR uses (Decision Name, Problem Statement, Business Goal, Architecture Decision, Reasoning, Alternatives Rejected, Trade-offs, Compatibility, Migration Strategy, Definition of Done, Future Extension) — not the ACR template (§0's correction: this is a deliberate supersession, not an implementation-discovered impracticality). `ADR-017`'s content is already drafted in this plan's §5/§6 (firmware/backend changes) and §3.1 (architecture); `ADR-018`'s content is already drafted in §11.2. Mark `ADR-008`'s commissioning-mechanism clause explicitly "superseded by ADR-017" (not deleted, per Rule 9) and `ADR-009` explicitly "extended by ADR-018."
- **Acceptance criteria:** both ADRs reviewed and marked `APPROVED` by whoever holds the `Architecture Owner` role (`MASTER_GOVERNANCE.md` §8's approval standard, applied here even though this isn't technically an ACR) before a single line of DM-Phase 2 code is written.
- **Test cases:** N/A — governance/documentation phase. The test is procedural: no DM-Phase 2 commit exists with a timestamp before both ADRs' `APPROVED` status.
- **Rollback risk:** None directly — but skipping this phase and proceeding straight to DM-Phase 2 is exactly the "quietly implement something smarter and call it a bug fix" pattern Rule 1 (§0, `MASTER_GOVERNANCE.md`) forbids on sight.
- **What the end user sees:** nothing — internal process only.

### DM-Phase 2 — Provisioning Mode (SoftAP + Captive Portal)
- **Governance gate:** requires `ADR-017` (§0, superseding ADR-008) and `ADR-018` (§0, extending ADR-009) approved before implementation starts — see DM-Phase 1.8, which now precedes this phase in the build order (§8's Build Order note).
- **Files touched:** new `wifi_provision.h`, `covio_firmware.ino` (boot-time station-timeout → AP fallback), `provision.h` (new `provision` command), `sync.h`/`ota.h` (WiFi-authority consolidation — do this as part of this phase, not deferred).
- **Business outcome:** a brand-new or WiFi-relocated device can be configured entirely from a phone/laptop over WiFi, with no cable and no console-syntax knowledge — closes `F-28`/`F-29` for real.
- **Technical work:** SoftAP `Covio-Setup-<last4>` (open, time-bounded — see §3.1's reasoning) + `DNSServer` captive-portal redirect + a setup form posting to the existing `Store` setters; auto-stop AP once station WiFi confirms; `provision` console command to re-enter AP mode on an already-configured unit without wiping data.
- **Acceptance criteria:** an unconfigured unit's AP is visible within 60s of boot; submitting valid WiFi+server+key results in a station-mode reboot and a successful first push within 2 minutes; AP stops broadcasting once station WiFi is confirmed; `provision` command re-enters AP mode without touching existing queue/totalizer state.
- **Test cases:** (1) fresh unconfigured unit auto-starts AP; (2) wrong WiFi password entered in the portal produces a visible error in the portal itself, not a silent reboot loop; (3) AP+STA concurrent operation doesn't starve the 1-second telemetry loop or destabilize PCNT counting; (4) re-provisioning via console preserves queue backlog and totalizer state across the mode switch; (5) AP auto-stop verified after successful station connect.
- **Rollback risk:** Medium — new boot-time conditional path, touches shared WiFi-state assumptions already flagged as a coupling risk (§1). Consolidating WiFi authority as part of this phase (not after) is what keeps this risk bounded.
- **What the end user sees:** a new WiFi network appears in their phone/laptop's WiFi list when a device needs setup; connecting opens a setup page; the device disappears from AP list and appears in the desktop app once configured.

### DM-Phase 3 — Windows Device Manager Desktop App
- **Files/artifacts:** new `covio_firmware/tools/device-manager/` (separate Electron project — its own `package.json`, build pipeline, installer, and release process, entirely independent of firmware OTA).
- **Business outcome:** the actual deliverable the user asked for — discover, configure, and monitor devices from a Windows desktop app, no cable, ever.
- **Technical work:** mDNS browse (bundled `multicast-dns`/`bonjour` library — do **not** rely on Windows' native resolver, §3.2) + manual add-by-IP fallback; device list + detail panel populated from `GET /status` (§4); AP-mode-aware setup wizard (detects `Covio-Setup-*` SSID, walks the operator through connecting, submitting config, and reconnecting); config *writes* only permitted while a device is in AP/provisioning mode, per §3.1/§5's security model.
- **Acceptance criteria:** installs via a standard Windows installer, no manual dependency steps; discovers a device on the same LAN within 10s; status panel auto-refreshes (poll cadence in the same order of magnitude as the device's own 5s push cadence); the setup wizard fully commissions a fresh device with zero console commands and zero cable; the app's navigation matches the §11.6 module map (Dashboard/Devices/Discovery/Provisioning/Live Monitor/Logs/OTA/Calibration/Diagnostics/Factory Test/Settings), with tabs whose data isn't ready yet (Logs, Calibration governance fields) shown as honestly empty/"not yet available" rather than hidden or faked; the Devices list supports search/filter/tag even with only a handful of pilot units.
- **Test cases:** (1) multiple devices on one LAN all discovered with no duplicates; (2) a device going offline mid-session degrades gracefully in the UI, no crash; (3) wrong-password retry handled gracefully in the wizard; (4) the raw API key is never shown in the UI, logs, or persisted insecurely on the PC; (5) uninstalling the app leaves no orphaned background processes.
- **Rollback risk:** Low for firmware — the app is a fully separate artifact and can be updated independently via normal installer updates with zero field-firmware risk. Main risk is operator error from a buggy config write — mitigate with client-side validation before submit.
- **What the end user sees:** a familiar desktop app; a device list; click-through live health; a guided setup wizard.

### DM-Phase 4 — Backend Registry, Device Twin, Audit Trails
- **Precondition:** DM-Phase 0B already shipped basic `X-Api-Key` enforcement and a minimal `devices` table — this phase extends that table into the full Device Twin (§11.3), it does not re-do the auth check.
- **Files touched:** `server/server.py` (bench/pilot scope only — see production note in §6).
- **Business outcome:** every device has a full current+historical record, not just a pass/fail key check; every calibration/config change is attributable — closes `N-04`, flagged **P0** in the original audit.
- **Technical work:** extend the `devices` table from DM-Phase 0B with the Device Twin's "current" fields (§11.3: firmware version, calibration/K-factor+version, config summary, last heartbeat/health state, live queue backlog, active alarms, OTA state); add the unified `device_events` table (§11.5) that config-audit and calibration-audit both write into as typed rows; add `/admin/devices/provision` — generates a fresh unique API key for a new device, for the desktop app to write during AP-mode setup (§5); add `/admin/devices` dashboard extension (registry + last-seen + revoke action).
- **Acceptance criteria:** a newly commissioned device's key is unique and traceable to a provisioning event; a K-factor change produces a `device_events` row with `type=CALIBRATION_CHANGED`; the Device Twin's "current" half stays in sync with each device's actual last-reported state.
- **Test cases:** (1) two devices' keys never collide; (2) three consecutive K-factor edits produce a correct, retrievable `device_events` history; (3) a device's twin "current" row updates correctly after each successful push; (4) revoking a key via the admin dashboard causes the next request from that device to be rejected (reusing DM-Phase 0B's enforcement, now driven by this phase's UI instead of direct DB edits).
- **Rollback risk:** Low — this phase extends an already-enforced auth model (DM-Phase 0B) rather than introducing enforcement for the first time, which is where the real risk was.
- **What the end user sees:** no visible change for correctly-configured devices; an admin page shows the registry, Device Twin state, and audit/event history.

### DM-Phase 5 — Security Hardening (P0 Live-Readiness)
- **Files touched:** `ota.h` (TLS client), `config.h` (remove committed defaults from ever reaching a shipped unit unrotated), factory process (not a firmware file — an eFuse-burning step at first flash).
- **Business outcome:** closes the two P0 findings from the original audit (`F-21` plaintext HTTP enabling MITM→RCE, `F-22` no signing/Secure Boot) using the scheme ADR-005 already fully designed.
- **Technical work:** HTTPS + CA pinning on all four cloud endpoints; Secure Boot V2 + Flash Encryption burned at first factory flash; key rotation/revocation admin action from DM-Phase 4, delivered to already-fielded devices only via a provisioning-mode re-touch (no inbound push channel exists — state this limitation to operators, don't hide it).
- **Acceptance criteria:** reuse ADR-005's own Definition of Done verbatim — HTTPS verified end-to-end against a real cert; a MITM proxy verified rejected; Secure Boot/Flash Encryption verified via an eFuse summary; an incorrectly signed OTA image verified rejected; no unit leaves the factory floor with the default API key active.
- **Test cases:** ADR-005's own bench tests (already specified in that ADR — reuse, don't reinvent).
- **Rollback risk:** **High if sequenced wrong.** Secure Boot/Flash Encryption are one-way per-device eFuse burns — a mistake on a real field-destined unit is permanent. Rehearse extensively on disposable bench units first.
- **What the end user sees:** `https://` in device config instead of `http://`; otherwise invisible functionally, except a device with tampered/unsigned firmware now visibly refuses to boot it instead of silently running it.

### DM-Phase 6 — Field Test / Factory Test Checklist
- **Files/artifacts:** a factory-test firmware build variant (ties to ADR-008's already-frozen design: SD write-read-CRC check, WiFi association check, PCNT test-signal check via the existing `SIM_PULSES`/`PIN_SIM` jumper pattern already built into `config.h` for exactly this purpose, single-line PASS/FAIL over Serial), a documented checklist, and a "Commission & Verify"/"Factory Test" flow in the desktop app surfacing the same checks visually. Full end-to-end SOP (Flash → Factory Test → Pulse Simulator → SD Test → WiFi Test → OTA Test → API Test → Heartbeat Test → PASS → Logical Device ID + API key generation → QR label print → production firmware flash → pack) detailed in §11.7.
- **Business outcome:** a non-engineer can commission and verify a unit end-to-end using only the desktop app, in a bounded time, with an unambiguous pass/fail result — and every unit leaves the floor with a traceable Logical Device ID (§11.2) and a printed QR label, not just a MAC address.
- **Technical work:** wire the factory-test firmware's PASS/FAIL signal and the field `verify` checks into the app's UI instead of requiring console literacy; add Logical Device ID generation + QR label printing as the final pre-pack step (§11.7).
- **Acceptance criteria:** a documented, repeatable procedure exists per §11.7 and has been dry-run successfully; a printed QR label correctly encodes the Logical Device ID and is scannable.
- **Test cases:** run the full checklist against a deliberately broken unit (disconnected SD, disconnected sensor jumper, wrong WiFi password) and confirm each failure is caught and clearly reported — never silently passed; confirm a QR-labeled unit's code matches what the app displays for that device.
- **Rollback risk:** Low — process/documentation, contingent on all prior phases actually working.
- **What the end user sees:** a checklist, a "Commission & Verify" button ending in a clear PASS/FAIL screen, and a physical QR label on the shipped unit.

---

## 9. Prototype-Safe vs. Production-Blocking

| Prototype-safe (fine for bench/internal trial today) | Production-blocking (must complete before any unattended field deployment) |
|---|---|
| DM-Phase 1 (local status endpoint) | Enforced receiver auth (DM-Phase 4) |
| DM-Phase 3 desktop app, read-only monitoring | TLS + CA pinning on all cloud endpoints (DM-Phase 5 / ADR-005) |
| Open/unencrypted AP-mode provisioning, bench-tested | Secure Boot V2 + Flash Encryption burned at first factory flash (DM-Phase 5 / ADR-005) — **cannot be retrofitted after shipment** |
| `server.py` bench/SQLite receiver for a handful of pilot units | Real production receiver (LCS/Django) implementing the same authenticated contract (ADR-016) — coordinate separately, not audited here |
| Reusing existing `config.h` default credentials during bench iteration | Removing/rotating default credentials before any unit ships (`F-26`) |
| — | `ADR-017`/`ADR-018` (§0, DM-Phase 1.8) formally approved, so this plan doesn't quietly violate the project's own frozen governance |
| — | WiFi-authority consolidation between `Sync`/`Ota` (prevents a documented coupling risk from becoming a real bug once a third consumer exists) |
| Alarm thresholds (§11.4) tuned loosely at first (e.g. queue>1000/10000) | Alarm thresholds validated against real operating data before being used to page anyone or drive automated action |
| Event timeline (§11.5) with a short/best-effort retention window | Event timeline retention policy explicitly decided once it's used for support/RMA history, not left implicit |
| Logical Device ID (§11.2) assigned manually for the first handful of bench units | QR label + automated ID generation (§11.7) required once manufacturing volume makes manual assignment error-prone |
| Device Twin "current" half only (no historical tables yet) | Historical half (event timeline, calibration/OTA/connectivity history) required before any RMA/support process depends on it |

---

## 10. Summary of Open Decisions (Stated Explicitly, Not Defaulted)

Consistent with this project's own ADR style of naming what remains open rather than silently assuming an answer:

1. **AP-mode security model** — this plan recommends an open, time-bounded SoftAP (physical/RF proximity as the trust boundary, mirroring ADR-005's own stated principle) over a pre-shared/printed password, since no physical label/QR mechanism currently exists in the manufacturing process. This should be explicitly ratified (or overridden) in `ADR-017` (§0), not left implicit.
2. **Desktop app framework** — this plan recommends Electron for stack consistency; a lighter native app remains a viable alternative if the team wants a much smaller installer and is willing to build mDNS/HTTP handling from scratch.
3. **Whether station-mode (non-AP) remote reconfiguration is ever allowed** — this plan defers it to after DM-Phase 5's real per-device security lands, rather than building a second, weaker auth scheme now.
4. **Production receiver ownership** — this plan's backend work (§6) targets the bench `server.py` only. The real production receiver is out of this document's scope and must be coordinated with whoever owns the LCS/Django integration.
5. **Logical Device ID format** (§11.2) — this plan suggests a short human-readable serial (e.g. `COV-000123`) but the exact format, checksum scheme, and whether it's sequential or random should be ratified alongside the ADR-009-REV mentioned in §0.
6. **Alarm severity taxonomy and exact thresholds** (§11.4) — several thresholds above (queue>1000/10000, reboot-loop rate, WiFi-reconnect-loop count) are proposed values, not frozen ones; this project's own style (see the Blueprint's own numeric thresholds, e.g. ADR-006's 15-second watchdog) is to state an exact number deliberately rather than leave it vague — do the same here before DM-Phase 4 ships alarms.
7. **Event timeline retention window and storage location** (§11.5) — backend-only, or also mirrored to a device-side ring buffer per ADR-012 once that lands? Not decided here.
8. **QR label content/format and exactly where in the factory workflow it's printed** (§11.7) — low priority for a first small-batch run; needs a real decision once volume justifies it.
9. **Calibration approval-workflow authority** (§11.8) — explicitly out of scope per ADR-010 itself; this is an organizational decision (who approves a K-factor change), not an engineering one, and this plan does not make it.

---

## 11. Fleet & Lifecycle Extensions (Added After Review)

This section incorporates the gaps identified in an end-to-end review of the v1 plan above: it does not change any decision made in §1–§10, it deepens them. Read alongside §0's updated governance notice before implementing §11.2 or §11.7.

### 11.1 Device Manufacturing Lifecycle

The v1 plan mostly starts at "power on device." Every unit that ships actually has a full lifecycle, and every stage of it already has a home in either an existing frozen ADR or elsewhere in this plan — the table below exists so "how do we replace a failed unit six months from now" has a documented answer instead of becoming a fire drill:

```
Factory ─────────────────► Flash firmware ─────────► Factory self-test
                                                             │
   ┌─────────────────────────────────────────────────────────┘
   ▼
Generate Logical Device ID ──► Generate unique API key ──► Provision (WiFi/server)
                                                             │
   ┌─────────────────────────────────────────────────────────┘
   ▼
Installed at customer ──► Commission & Verify ──► Active (monitored)
                                                             │
   ┌─────────────────────────────────────────────────────────┘
   ▼
Maintenance (OTA, calibration) ──► Replacement ──► Decommission
```

| Stage | Owning decision |
|---|---|
| Flash / Factory self-test | `ADR-008` (frozen design, unimplemented) + §11.7 |
| Generate Logical Device ID | **New** — §11.2 |
| Generate unique API key | `ADR-005` (frozen) + DM-Phase 4/5 |
| Provision (WiFi/server) | DM-Phase 2 (this plan) + `ADR-008-REV` per §0 |
| Commission & Verify | `ADR-008`'s `verify` console command, or DM-Phase 3/6's app-driven equivalent |
| Active / monitored | DM-Phase 1 (status) + §11.3 (Device Twin) + §11.4 (Alarms) |
| Maintenance — OTA | Existing `ota.h` + `ADR-005`/`ADR-006` hardening |
| Maintenance — Calibration | `ADR-010` + §11.8 |
| Replacement | `ADR-009` (hardware_id/asset_id split — this is exactly the problem that ADR exists to solve) |
| Decommission | `ADR-009`'s `decommission` console command (frozen design, unimplemented) |

The only genuinely new element in this entire lifecycle is the Logical Device ID (§11.2) — everything else already has an architectural owner; this table's value is making that visible in one place.

### 11.2 Device Identity — Three Tiers

`ADR-009` (frozen) already separates `hardware_id` (MAC-derived, permanent per board) from `asset_id` (installer-assigned, e.g. "Boiler 2 Oil Meter," represents the installation). This plan adds a third, distinct tier in between:

```
hardware_id            Logical Device ID          asset_id
(MAC / chip-derived)    (e.g. COV-000123)         (e.g. "Boiler 2 Oil Meter")
permanent per board     manufacturing serial       installer-assigned, per site
exists today            NEW                        exists in ADR-009's design
```

**Why a third tier, and why it isn't redundant with `asset_id`:** `asset_id` is free text an installer chooses at commissioning time — it doesn't exist yet for a freshly-manufactured unit sitting in a box with no installation. The factory line still needs *some* stable, globally-unique, QR-encodable identifier to track a unit through manufacturing, RMA, and warranty *before* it has a customer or an installation — that's the Logical Device ID, generated once during factory self-test (§11.7), stored in NVS as a new field, and printed on the unit's QR label. On a controller-board replacement (the scenario ADR-009 exists for), the Logical Device ID is what should be reused/reprinted onto the replacement board's own label if it's treated as the "durable" identity for warranty/RMA tracking purposes — `asset_id` stays with the installation regardless.

Console/config surface: a new `set serial <id>` companion to ADR-009's existing `set asset <id>`, or — preferably — the desktop app's factory workflow (§11.7) sets it directly via the local API rather than requiring a console command at all.

**Governance note:** per §0, this extends ADR-009 additively (new field, nothing removed or changed) and should be recorded as `ADR-009-REV`, but is lower-risk than the ADR-008 conflict and shouldn't block earlier phases.

### 11.3 Backend as Device Twin

Reframing §3.3/§6: the backend registry's job is not "does this device exist," it's "what is this device's complete current and historical state." Concretely:

| Current (one row per device, overwritten in place) | Historical (append-only) |
|---|---|
| Firmware version | Event timeline (§11.5) |
| Calibration (K-factor + version) | Calibration change history (`ADR-010`, already frozen) |
| Config summary (server URL, masked key status, WiFi SSID) | Firmware/OTA history |
| Last heartbeat / derived health state | Connectivity history (WiFi up/down) |
| Live queue backlog | — |
| Active alarms (§11.4) | — |
| OTA state | — |

This is not a new system to build alongside DM-Phase 4's registry — DM-Phase 4's `devices` table **is** the twin's current half; §11.5's unified `device_events` table (not three parallel audit tables) is its historical half. Keep it to these two halves, not a third parallel mechanism.

### 11.4 Alarm System

Health data (`/api/v1/health`, ADR-002 once implemented) answers "what is true right now." Production also needs alarms — discrete, severity-tagged, timestamped, acknowledgeable events an operator can act on. This plan defines alarms as **a filtered view over the same event stream as §11.5**, not a separate storage mechanism:

| Alarm | Trigger | Severity | Data source status |
|---|---|---|---|
| `SD_REMOVED` | SD absent/unreadable at runtime | CRITICAL | Needs `ADR-004` (frozen, unimplemented); DM-Phase 1 only delivers present/absent today |
| `QUEUE_HIGH` | Unacked backlog > 1,000 rows (tunable, §10 item 6) | WARNING | DM-Phase 1's maintained counter |
| `QUEUE_CRITICAL` | Unacked backlog > 10,000 rows | CRITICAL | Same source |
| `WIFI_RECONNECT_LOOP` | Excessive reconnect attempts in a short window | WARNING | **New** — `sync.h` has no attempt counter today, only a backoff timer |
| `OTA_FAILED` | Last OTA attempt returned `HTTP_UPDATE_FAILED` | WARNING | `ota.h` already knows this (Serial-only) — needs exposing via `/api/v1/status` |
| `OTA_ROLLBACK` | Bootloader auto-reverted after a failed trial | CRITICAL | **New** detection logic — ties to `ADR-006`'s reset-reason work |
| `SENSOR_STOPPED` | `ADR-015`'s `sensor_plausible` flag false | WARNING | `ADR-015` frozen, unimplemented |
| `PULSE_FREQ_IMPOSSIBLE` | Instantaneous rate exceeds a configured physical max | WARNING | **New** — no ADR defines this; max threshold is site/meter-specific, flag as open |
| `REBOOT_LOOP` | Excessive `boot_id` increments in a short window | CRITICAL | `boot_id` already increments every boot (`store.h`) — only the rate check is new |
| `BROWNOUT_DETECTED` | `reset_reason == brownout` | WARNING | Ties directly to `ADR-006`, unimplemented |
| `CLOCK_DRIFT` | — | **N/A** | This device has no wall clock by design (no SNTP) — Blueprint Task 9 already states this explicitly. Not a gap; a documented non-goal unless SNTP is added later as its own ADR. |
| `FLASH_CORRUPTION` | Both dual-slot checkpoint copies CRC-invalid | CRITICAL | Already a known open architectural gap (`ACR-002`, still OPEN per `MILESTONES.md`) — this alarm is the operational face of that unresolved risk, not new |
| `CALIBRATION_CHANGED` | Any K-factor edit | INFO | `ADR-010`'s audit trail already records this — the alarm is just a live view over that same table |
| `API_AUTH_FAILED` | Receiver rejects a bad/missing/revoked key | WARNING/CRITICAL by frequency | DM-Phase 4's auth enforcement — needs surfacing as an alarm, not just a log line |

### 11.5 Event Timeline

A single, unified, append-only `device_events` table — **not** three separate audit mechanisms for config changes, calibration changes, and everything else:

| Event | Source | Availability today |
|---|---|---|
| Boot | `boot_id` increment (`store.h`) | Exists (Serial-only) — needs pushing via `/api/v1/logs` or the future push pipeline |
| Provisioned | AP-mode config write completes | New — DM-Phase 2 |
| WiFi Connected / WiFi Lost | `WiFi.status()` transitions | New — today only the *current* state is polled, transitions aren't logged |
| OTA Started / Installed | `ota.h` | Exists (Serial-only) — needs pushing |
| OTA Rollback | New detection | See `OTA_ROLLBACK` alarm above |
| SD Error / Sensor Error | `ADR-004`/`ADR-015` | Both frozen, unimplemented |
| Calibration Updated | `ADR-010`'s audit trail | Already covered — project into this same timeline view, don't duplicate the table |
| Server Changed / API Key Rotated / Factory Reset | `provision.h` | Logged to Serial only today — needs pushing to backend |

**Design note:** implement one `device_events` table that DM-Phase 4's config-audit and calibration-audit both write into as typed rows. An `OTA_ROLLBACK` alarm (§11.4) and an "OTA Rollback" timeline entry are the same underlying row, differing only in whether its severity crosses the threshold that makes it also render as an alarm. Maintaining three parallel history mechanisms (alarms, config audit, calibration audit) would be the same mistake this project's Blueprint already flagged elsewhere (duplicated JSON-extraction logic in `sync.h`/`ota.h`, Task 5) — one source of truth, multiple views.

### 11.6 Desktop App Module Map & Fleet-Scale UI Requirements

Full tab list for "Covio Device Manager" (§3.2's reframing): **Dashboard** (fleet-wide online/offline/alarm counts) · **Devices** (searchable/filterable/taggable list) · **Discovery** · **Provisioning** · **Live Monitor** (`/api/v1/status`+`/health`) · **Logs** (`/api/v1/logs`, honest "not yet available" until `ADR-012` lands) · **OTA** · **Calibration** (§11.8) · **Diagnostics** (`/api/v1/metrics`, §3.1a) · **Factory Test** (§11.7) · **Settings**.

Design for 1 / 10 / 100 / 1,000 devices from day one, not after it hurts: the Devices list's data model should carry `tags: string[]`, `location: string`, and `firmware_version` as filterable fields from DM-Phase 3, even though a 3-unit pilot fleet has no practical use for filtering yet. This is the same reasoning `ADR-001` already applied to schema versioning — the field costs nothing empty, and retrofitting it after 1,000 devices exist without it is a real migration, not a quick add.

### 11.7 Factory Test SOP

Full flow, building entirely on infrastructure that already exists or is already scoped elsewhere in this plan — nothing here requires inventing new hardware:

```
Flash (production toolchain, ADR-013)
  ↓
Factory Test firmware boots (ADR-008 — frozen, unimplemented)
  ↓
Pulse Simulator check — reuses the existing SIM_PULSES/PIN_SIM jumper
  already built into config.h for exactly this purpose
  ↓
SD Test (write-read-CRC cycle, ADR-008)
  ↓
WiFi Test (associate to factory-floor test network, ADR-008)
  ↓
OTA Test (manifest poll + a dummy update against a factory-floor test receiver)
  ↓
API Test (push/config against the factory-floor receiver, confirm auth — DM-Phase 4)
  ↓
Heartbeat Test (confirm /api/v1/status reports healthy)
  ↓
PASS/FAIL over Serial (ADR-008, machine-parseable)
  ↓ (on PASS)
Generate Logical Device ID (§11.2) + unique API key (ADR-005/DM-Phase 4), write to NVS
  ↓
Print QR label encoding the Logical Device ID
  ↓
Flash production firmware (factory-test image must never ship — ADR-008's own requirement)
  ↓
Pack
```

The QR label is a genuinely new manufacturing artifact, but it directly fulfills what `ADR-009` already named as its own future extension: *"asset_id assignment can later be automated (e.g., via a QR code scanned by a future commissioning app) without changing the underlying data model."* Here the QR encodes the Logical Device ID specifically (§11.2); a later app enhancement letting an installer scan it to auto-fill `asset_id` at commissioning is exactly the mechanism ADR-009 already anticipated, not a new one.

### 11.8 Calibration Governance — Extending ADR-010

`ADR-010` (frozen) already decided the hard parts: `density`/`T_ref` removed as unimplemented dead fields, an immutable append-only calibration history (old value, new value, new version, timestamp), and retroactive litre recomputation retained as an intentional product feature. It explicitly named what it was *not* deciding: *"An approval workflow, if required later, is layered onto the append-only table as additional columns, without changing the recording mechanism itself."*

This plan exercises exactly that named extension point, not a new one: add a `reason` (free text) and `effective_from` (for a future scheduled-rather-than-immediate change) column to the existing audit table. **No new ADR is required for this** — see §0. What remains explicitly undecided, per ADR-010's own stated boundary, is *who* holds approval authority for a calibration change — an organizational decision, not an engineering one (§10 item 9).

### 11.9 Roadmap Beyond v1

| Version | Scope | Builds on |
|---|---|---|
| V1 | Provisioning & Monitoring | This plan, DM-Phase 0–6 |
| V2 | Cloud Fleet Management | `ADR-016`'s real production receiver + Device Twin at scale (§11.3) + fleet-wide OTA rollout tooling |
| V3 | Analytics (consumption trends, per-site comparisons) | If this ERP's existing Insight Engine subsystem lives in the same environment, this is a natural integration point worth its own scoping conversation later — not assumed or committed here |
| V4 | Predictive Maintenance | §11.4's alarm history + `ADR-015`'s sensor-plausibility trend data, once both exist |
| V5 | Remote Diagnostics | Largely already scoped by §3.1a's diagnostics API + `ADR-012`'s log retrieval — closer than it looks once DM-Phase 1 lands |
| V6 | Multi-tenant Fleet Management | Data isolation per customer/tenant + RBAC — flagged explicitly as a large addition requiring its own dedicated architecture review whenever it becomes real, not something to wedge into the V1–V5 data model as an afterthought |

---

## 12. Noted for Implementation Time (Not Expanded Further)

Per review: these are real, but they're operational depth that will matter at hundreds/thousands of devices, not architectural gaps that block starting DM-Phase 1 today. Captured here as one line each so they aren't lost — each gets its own short design note when its owning phase actually comes up, not now.

| # | Note | Surfaces naturally in |
|---|---|---|
| 1 | Multi-site/multi-customer hierarchy (Organization → Site → Plant → Machine → Device) belongs in the Device Twin | §11.3, when V2 (§11.9) is scoped |
| 2 | Configuration Profiles (assign one profile → N devices, instead of configuring each individually) | Backend/§6, once fleet size makes per-device config editing painful |
| 3 | OTA Release Channels (Dev/Internal/Pilot/Production/Legacy, device pinned to a channel) — flagged as probably the biggest of these 10 | DM-Phase 5 / `ota.h` manifest design — leave room for a `channel` field in the manifest contract now even if unused at v1 |
| 4 | "Every action is auditable" as an explicit stated principle, not just an implied one | §11.5's `device_events` table already structurally supports this — just needs the principle stated as a rule, not a new mechanism |
| 5 | User roles (Operator/Engineer/Factory/Administrator/Super Admin) with per-action permissions | Desktop app + backend auth, once the app has more than one operator using it |
| 6 | Explicitly state the desktop app works fully offline-from-cloud (PC + ESP32 on the same WiFi, backend/internet down) — a real selling point | §3.1/§3.2 already imply this since the app talks to the device's local API directly; worth a one-line explicit claim once DM-Phase 1/3 ship |
| 7 | Local network diagnostics (ping/latency/packet-loss/DNS/gateway/RSSI) alongside device diagnostics | §3.1a's `/api/v1/metrics`, as app-side additions rather than device-side |
| 8 | Explicit device-replacement workflow (old unit fails → new unit → reimport identity/calibration/config → resume) | §11.1/§11.2 (ADR-009's hardware_id/asset_id split already exists for this) — worth a dedicated runbook once the first real RMA happens |
| 9 | Recovery Mode distinct from Provisioning Mode (factory reset / safe-OTA-recovery / diagnostics-only / read-only), for partially-corrupted firmware | DM-Phase 2/5, once real field failures show what "partially corrupted" actually looks like for this hardware |
| 10 | State explicitly that all local/backend APIs are platform-independent, so a future Android/iOS client can reuse them — Windows desktop is just the first client | §3.1a's versioned API set already makes this true; worth saying explicitly rather than leaving implicit |

---

## 13. Appendix A — Frozen Interface Contract v1

**This section is authoritative.** If anything elsewhere in this document (§3.1a, §4, §11.4, §11.5) appears to conflict with a field name, type, or enum value defined here, **this section wins** — treat the other mentions as informal shorthand. This is DM-Phase 0A's deliverable (§8) and must be reviewed/accepted before DM-Phase 1 writes a line of code against it. Nothing here has been implemented yet; this is the specification, not a report of working code.

### A.1 Conventions

- **Transport:** plain HTTP on the local LAN (port 80), per §3.1's reasoning (no per-device TLS infrastructure exists for a self-hosted local endpoint yet). All cloud-facing endpoints (`push`/`config`/`ota/manifest`) are unaffected by this section and remain governed by `ADR-005`.
- **Success responses:** flat JSON, `Content-Type: application/json`, no envelope wrapper — consistent with this project's existing dependency-free flat-JSON convention (`telemetry.h`, `server/server.py`).
- **Error responses:** always the fixed envelope in §A.5, never a bare string or empty body.
- **Timestamps:** the device has no wall clock (no SNTP — Blueprint Task 9 already established this as a deliberate architectural fact, not a gap). Every device-relative time field is named `*_ms_ago` (an elapsed duration in milliseconds, computed from the device's own `millis()`) or `uptime_ms` (time since this boot) — **never** an absolute timestamp. The desktop app/backend, which do have a wall clock, are responsible for converting these into an absolute time if needed.
- **Numbers:** never `NaN`/`Infinity` (invalid JSON). A metric that cannot yet be computed is `null`, not a sentinel value — the one documented exception is noted inline where it applies.
- **`totalizer_raw_pulses` is a `uint64` sent as a JSON number.** JavaScript (the desktop app) only safely represents integers up to 2^53−1 (~9.2×10^15). At this device's maximum plausible pulse rate, reaching that value would take centuries of continuous flow — this is stated explicitly as a deliberate, bounded, accepted assumption, not an oversight.
- **Enums are lowercase `snake_case` strings** (e.g. `"pending_verify"`), except `alarm_severity` which is `UPPER_CASE` (`"WARNING"`) to visually distinguish a severity from a state at a glance in logs/UI. This asymmetry is deliberate, not an inconsistency to "fix" later.
- **JSON construction:** hand-rolled string building (matching `telemetry.h`'s existing `toJson()` pattern), not a new library — see DM-Phase 0A's technical work note on why.

### A.2 Endpoint Reference

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/api/v1/info` | none | Static identity (never changes without a reboot) |
| GET | `/api/v1/status` | none | Operational summary — what a technician checks day-to-day |
| GET | `/api/v1/health` | none | Derived health state + active alarms |
| GET | `/api/v1/metrics` | none | Deep engineering diagnostics, distinct from status (§3.1a) |
| GET | `/api/v1/logs` | none | Ring-buffer log lines — returns `501` until `ADR-012` exists |
| POST | `/api/v1/config` | **AP-mode only** (§3.1, §5) | Write WiFi/server/API-key |
| GET | `/` | none | Human-readable HTML mirror of `/api/v1/status`, for a browser with no app |

### A.3 Exact Schemas

**`GET /api/v1/info`** — 200 response:
```json
{
  "hardware_id": "esp32-1A2B3C4D5E6F",
  "logical_device_id": null,
  "asset_id": null,
  "fw_version": "1.0.0",
  "model": "covio-oilflow-v1",
  "boot_id": 42,
  "schema_version_current": 1
}
```
| Field | Type | Notes |
|---|---|---|
| `hardware_id` | string | MAC-derived, permanent. **Identical in value** to the existing wire telemetry envelope's `device_id` (`telemetry.h`) — this is a new, renamed *local-API* field, not a rename of the frozen wire/SD schema. Do not touch `queue.h`/`telemetry.h`/`SCHEMA_REGISTRY.md` to "match" this. |
| `logical_device_id` | string \| null | §11.2. `null` until assigned at factory commissioning (§11.7). |
| `asset_id` | string \| null | §11.2/`ADR-009`. `null` until an installer sets it. |
| `fw_version` | string | `== FW_VERSION` (`config.h`) |
| `model` | string | `== DEVICE_MODEL` (`config.h`) |
| `boot_id` | uint32 | `== store.bootId()` |
| `schema_version_current` | uint16 | `== SCHEMA_VERSION_CURRENT` (`ADR-001`, `queue.h`) — informational only; this is the wire-telemetry schema version, unrelated to this local API's own `/v1/` versioning |

**`GET /api/v1/status`** — 200 response:
```json
{
  "uptime_ms": 1234567,
  "wifi": { "connected": true, "ssid": "FactoryFloor-WiFi", "rssi_dbm": -61 },
  "server_url": "https://lcs.example.com",
  "api_key_status": "configured",
  "sd_status": "ok",
  "queue": { "backlog": 42, "acked_seq": 1004, "last_seq": 1046 },
  "totalizer_raw_pulses": 918203,
  "last_sync_ms_ago": 4321,
  "last_push_http_code": 200,
  "ota": { "state": "none", "running_version": "1.0.0" },
  "health_state": "ok"
}
```
| Field | Type | Notes |
|---|---|---|
| `wifi.connected` | bool | `WiFi.status() == WL_CONNECTED` |
| `wifi.ssid` | string | `store.wifiSsid()` |
| `wifi.rssi_dbm` | int | `WiFi.RSSI()` |
| `server_url` | string | `store.serverUrl()` — not a secret, sent in full |
| `api_key_status` | enum (A.4) | **Never** the raw key |
| `sd_status` | enum (A.4) | Only `ok`/`absent` are producible until `ADR-004` lands; `write_fail`/`low_space` are reserved values, not yet reachable |
| `queue.backlog` | uint32 | DM-Phase 1's maintained counter — **not** a live rescan |
| `queue.acked_seq` / `queue.last_seq` | uint32 | Internal `ack_.acked_seq` / `totalizer.lastSeq()` |
| `totalizer_raw_pulses` | uint64 | See A.1's precision note |
| `last_sync_ms_ago` | uint32 \| null | `null` if no successful push/config-poll yet this boot |
| `last_push_http_code` | int \| null | Last `HTTPClient` result code from `pushOnce()` |
| `ota.state` | enum (A.4) | |
| `ota.running_version` | string | `== FW_VERSION` |
| `health_state` | enum (A.4) | Derived — see A.4 for the exact precedence rule |

**`GET /api/v1/health`** — 200 response:
```json
{
  "health_state": "degraded",
  "alarms": [
    { "type": "QUEUE_HIGH", "severity": "WARNING", "raised_at_ms_ago": 60000, "message": "Unacked queue backlog exceeds 1000 rows" }
  ]
}
```
`alarms` is `[]` (empty array, never `null`) when nothing is active.

**`GET /api/v1/metrics`** — 200 response:
```json
{
  "free_heap_bytes": 123456,
  "heap_low_water_mark_bytes": 98765,
  "reset_reason": "power_on",
  "cpu_freq_mhz": 240,
  "flash_size_bytes": 4194304,
  "sd_write_latency_ms": 12,
  "queue_read_latency_ms": 3,
  "pulse_frequency_hz": 2.5,
  "network_rtt_ms": 145,
  "rssi_history_dbm": [-60, -61, -59],
  "temperature_c": null
}
```
| Field | Type | Notes |
|---|---|---|
| `sd_write_latency_ms` / `network_rtt_ms` | uint32 \| null | `null` if SD absent / device offline, respectively |
| `pulse_frequency_hz` | float | Instantaneous, derived from two `totalizer.total()` samples ~1s apart |
| `rssi_history_dbm` | int[] | Short in-RAM ring, last 10 samples — not SD-persisted |
| `temperature_c` | null (always) | **Not applicable to this hardware build.** The MAX31865 RTD module was explicitly removed (`config.h`'s header comment). The field exists only so a future hardware revision that reintroduces it doesn't require a `/v2/` bump for this one field. Do not populate it speculatively. |

**`GET /api/v1/logs`** — until `ADR-012` exists:
```json
{ "error": { "code": "NOT_IMPLEMENTED", "message": "Log ring buffer requires ADR-012" } }
```
HTTP `501`. Once `ADR-012` lands, `200` with `{"lines": [{"uptime_ms": 12345, "text": "[SYNC] acked_seq=5 (sent 5)"}]}`.

**`POST /api/v1/config`** — request (all four fields required together; a partial write is rejected, not partially applied):
```json
{ "wifi_ssid": "MyFactoryWiFi", "wifi_pass": "secret", "server_url": "https://lcs.example.com", "api_key": "abc123..." }
```
Success response (200): `{ "success": true }`
Rejected — not in AP mode (403):
```json
{ "error": { "code": "CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE", "message": "Config writes are only accepted while the device is in provisioning mode" } }
```
Rejected — missing field (400): `{ "error": { "code": "CONFIG_MISSING_FIELD", "message": "..." } }`
Rejected — malformed URL (400): `{ "error": { "code": "CONFIG_INVALID_URL", "message": "..." } }`

### A.4 Enums (exact allowed values — nothing outside this list)

| Enum | Values | Notes |
|---|---|---|
| `api_key_status` | `configured` \| `default` \| `missing` | `default` = still the compiled-in `DEFAULT_API_KEY` |
| `sd_status` | `ok` \| `absent` \| `write_fail` \| `low_space` | Last two reserved pending `ADR-004` |
| `ota_state` | `none` \| `pending_verify` \| `confirmed` \| `failed` | Mirrors `ota.h`'s internal `pendingVerify_`/`confirmed_` plus a new `failed` case |
| `health_state` | `ok` \| `degraded` \| `offline` | Derived — precedence, evaluated top-to-bottom, first match wins: (1) `offline` if `last_sync_ms_ago` is `null` or > **300000 ms (5 minutes — tunable, §10 item 6)**; (2) `degraded` if any active alarm has `severity == CRITICAL`, or `sd_status` is `write_fail` or `absent`; (3) `degraded` if any active alarm has `severity == WARNING`; (4) otherwise `ok`. |
| `alarm_severity` | `INFO` \| `WARNING` \| `CRITICAL` | Upper-case, deliberately (A.1) |
| `alarm_type` | `SD_REMOVED` \| `QUEUE_HIGH` \| `QUEUE_CRITICAL` \| `WIFI_RECONNECT_LOOP` \| `OTA_FAILED` \| `OTA_ROLLBACK` \| `SENSOR_STOPPED` \| `PULSE_FREQ_IMPOSSIBLE` \| `REBOOT_LOOP` \| `BROWNOUT_DETECTED` \| `FLASH_CORRUPTION` \| `CALIBRATION_CHANGED` \| `API_AUTH_FAILED` | 13 values, per §11.4. `CLOCK_DRIFT` deliberately excluded — N/A per Blueprint Task 9 (no wall clock exists by design) |
| `reset_reason` | `power_on` \| `software` \| `panic` \| `watchdog` \| `brownout` \| `deepsleep` \| `unknown` | Maps from ESP-IDF's `esp_reset_reason_t` — see mapping table below |
| `device_lifecycle_state` | `factory` \| `provisioning` \| `active` \| `degraded` \| `maintenance` \| `decommissioned` | §11.1. Backend/Device-Twin concept only — the device's own local API has no notion of its own lifecycle stage beyond "am I in AP mode or not" |

**`reset_reason` mapping (ESP-IDF → this contract's enum):**

| `esp_reset_reason_t` value | Maps to |
|---|---|
| `ESP_RST_POWERON` | `power_on` |
| `ESP_RST_SW` | `software` |
| `ESP_RST_PANIC` | `panic` |
| `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT`, `ESP_RST_WDT` | `watchdog` |
| `ESP_RST_BROWNOUT` | `brownout` |
| `ESP_RST_DEEPSLEEP` | `deepsleep` |
| anything else (`ESP_RST_UNKNOWN`, `ESP_RST_SDIO`, ...) | `unknown` |

### A.5 HTTP Status Codes & Error Envelope

| Code | Meaning |
|---|---|
| `200` | Success |
| `400` | Malformed JSON body or missing required field (`/api/v1/config` only) |
| `403` | `/api/v1/config` POST attempted while the device is not in AP/provisioning mode |
| `404` | Unknown path |
| `501` | `/api/v1/logs` before `ADR-012` exists |
| `500` | Reserved; should not normally occur. If it does, the firmware must still return a valid error-envelope JSON body, never an empty body or a raw crash. |

**Error envelope (every non-2xx response):**
```json
{ "error": { "code": "STRING_CODE", "message": "human-readable explanation" } }
```
**Defined codes:** `NOT_IMPLEMENTED`, `CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE`, `CONFIG_MISSING_FIELD`, `CONFIG_INVALID_URL`, `NOT_FOUND`.

### A.6 mDNS Record

- **Service type:** `_covio._tcp.local`
- **Instance name:** `covio-<last6hexofmac>` (matches the local hostname `covio-xxxxxx.local`)
- **Port:** `80`
- **TXT records:** `hardware_id=<value>`, `fw=<value>`, `model=<value>`, `logical_device_id=<value-or-empty>`

### A.7 Versioning Policy

- Every path carries `/v1/`. A **breaking** change (removing, renaming, or retyping a field; changing an enum's meaning) requires introducing `/v2/` served **alongside** `/v1/` for a deprecation window — mirroring `ADR-001`'s own "current + previous" philosophy for the wire telemetry schema. Never silently break `/v1/` once any device or the desktop app depends on it.
- **Additive** changes (a new optional field in a response) do **not** require a version bump — normal REST practice, and consistent with this contract's own design (e.g., `temperature_c` already reserved as a forward-compatible placeholder, A.3).
