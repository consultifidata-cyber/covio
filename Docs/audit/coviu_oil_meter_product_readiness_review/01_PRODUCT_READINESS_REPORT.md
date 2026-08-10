# Covio Product Readiness Review — Local Project Scope

**Date:** 2026-07-26. **Scope:** ESP32 firmware, Covio Device Manager (Electron
app), OTA, provisioning, flashing tools, calibration, local utilities,
documentation — **as they exist on this laptop today**. Out of scope per
instruction: the ERP, production deployment, and any bug already fixed
(a full list of what's already fixed is in Appendix A so it isn't re-litigated
below). No code was written or modified to produce this review — it is a
read-only inspection with file:line evidence for every claim.

**Framing:** the physical commissioning of one unit is complete and proven
live (`Docs/audit/coviu_oil_meter_plant_commissioning/11_FINAL_LIVE_COMMISSIONING_PROOF.md`).
This report asks the next question: *is the product, as implemented, ready to
be manufactured and deployed as 500 units across 20 factories?* The honest
answer is **not yet** — the single-device engineering is unusually disciplined
(see the firmware findings below), but several fleet-scale and per-device
assumptions are baked into the current design that don't survive the jump
from "1 device, 1 bench" to "500 devices, 20 sites, multiple technicians."

---

## SECTION 1 — Device Manager

Device Manager is an Electron app with a narrow, sandboxed IPC surface
(`main.js:47-52` contextIsolation+sandbox; the full channel list at
`main.js:79-124` is exhaustive — nothing exists outside it).

| Capability | Status | Evidence |
|---|---|---|
| Flash firmware | ✗ Missing | No flash/esptool/serial IPC anywhere in `src/main`. Flashing is entirely outside this app (esptool CLI / PlatformIO). |
| Read configuration | ⚠ Partial | `deviceClient.getStatus/getInfo` surfaces `server_url`, `wifi_ssid`, `api_key_status` — calibration is not exposed (see Section 4). |
| Change Wi-Fi | ⚠ Partial | `postConfig` (`deviceClient.js:104-150`) only during initial AP-mode setup (`provisioning.js:5-8`) — no "change Wi-Fi on an already-live device" flow exists. |
| Change/rotate API key | ⚠ Partial | Same AP-mode-only path; no standalone rotate-key action for a field-deployed device. |
| Read logs | ⚠ Partial | UI (`logs.js`) is wired correctly, but the firmware endpoint (`/api/v1/logs`) returns `501 NOT_IMPLEMENTED` — stub end-to-end. |
| OTA | ⚠ Partial | `ota.js` is read-only: shows current version/state. Triggering an update, channels, and rollout history are explicitly not implemented ("a later phase," `ota.js:1-4`). |
| Device list | ✓ Exists | `devices.js` — searchable/filterable/taggable, backed by `deviceStore` + mDNS discovery + manual-IP fallback. |
| Backup | ✗ Missing | No export/save-config IPC or UI anywhere. |
| Restore | ✗ Missing | No import/apply-config IPC or UI anywhere. |
| Calibration | ✗ Missing | `app.js:31` routes the tab straight to a stub: "Calibration governance fields are not part of the local device API yet." |
| Factory reset | ✗ Missing | No IPC, no UI button — only reachable via serial console, outside this app. |
| Restart | ✗ Missing | No restart/reboot IPC or button anywhere. |
| Health | ✓ Exists | `getHealth` → `liveMonitor.js`, `dashboard.js` (fleet-wide online/degraded/offline counts + alarms), per-row pill in `devices.js`. |
| Diagnostics | ✓ Exists | `diagnostics.js` maps every field of `/api/v1/metrics` 1:1 into the UI — heap, flash, RSSI history, pulse frequency, network RTT, reset reason, temperature (null on this hardware). |

**Verdict:** the implemented half (device list, health, diagnostics) is clean
and honest — it shows real data or an explicit "not yet available" stub, never
fabricated values. The missing half (flash, backup/restore, calibration,
factory reset, restart, OTA trigger) means Device Manager today is a
**monitoring and initial-setup tool, not a full lifecycle-management tool**.

---

## SECTION 2 — Commissioning

**Can a new technician commission a device without serial commands?
PARTIAL.**

Numbered manual steps, current implementation:

1. Power on device. Firmware waits for a saved Wi-Fi network; a factory-fresh
   device has none, so it falls through immediately. *(no-serial)*
2. Device auto-starts a SoftAP `Covio-Setup-<last4-of-deviceId>`
   (`wifi_provision.h:129-133`). *(no-serial, automatic)*
3. Technician **manually** switches their laptop's Wi-Fi to that SSID — a
   real physical action; Device Manager can *scan for* nearby setup networks
   (`wifiScan.js`) but cannot join one itself (no OS API exposed for that).
   *(no-serial, but manual)*
4. DM wizard step 2: click "Check Connection" — calls the device's local API
   at `192.168.4.1`. *(no-serial)*
5. DM wizard step 3: technician types SSID/password/server URL/API key; DM
   POSTs to the device's `/api/v1/config` (frozen JSON contract,
   `wifi_provision.h:262-285`), which writes and reboots. **Not live-tested**
   before writing — only the device's own separate raw HTML captive-portal
   form live-tests credentials; the DM wizard's JSON path does not.
   *(no-serial)*
6. DM wizard step 4: polls mDNS for the device reappearing (90s timeout,
   `provisioning.js:195-220`) as a **proxy** for success — a wrong password
   and a successful join both look identical ("device rebooted") until the
   device either reappears or falls back to a new SoftAP. *(no-serial, but
   not a positive confirmation)*
7. Technician manually reconnects their own laptop to the normal network.
   *(manual, not serial)*
8. No dedicated "commissioning complete" screen exists — the wizard's only
   follow-through is a "View in Live Monitor" link; the technician must judge
   health themselves. **No pass/fail gate.**
9. **Tank/plant/site assignment does not exist anywhere** in firmware or
   Device Manager (zero hits for "tank"/"plant"/"site" outside a stray
   comment). The only assignable identity is a generic `logical_device_id`,
   assigned through a **separate, unlinked** DM screen (`factoryTest.js`) that
   allocates an ID + prints a QR label — this is device serialization, not
   tank/plant metadata, and a technician must know to run both this screen
   and the Wi-Fi wizard, in the right order, with no single guided flow
   tying them together.

**Bottom line:** Wi-Fi/server/key entry genuinely doesn't need a serial
console — that's real, working code, not aspirational. But full commissioning
at fleet scale needs three fixes: a positive success confirmation (not
mDNS-reappearance inference), one unified wizard instead of two disconnected
screens, and an actual tank/plant assignment concept, which doesn't exist at
any layer today.

---

## SECTION 3 — Network Management

| Capability | Status | Evidence |
|---|---|---|
| Change SSID / password | ⚠ Partial | AP-mode initial setup only (Section 2) — no live-device reconfiguration flow. |
| Multiple Wi-Fi profiles | ✗ Missing | `postConfig`'s schema is exactly one `{wifi_ssid, wifi_pass}` pair — no fallback list, no priority. |
| Test connectivity | ⚠ Partial | No direct "test these credentials" action from DM; only inferred via mDNS reappearance after commit. The wizard's own comments state it explicitly does **not** live-test credentials before writing+rebooting. |
| Show RSSI | ✓ Exists | `liveMonitor.js`, `factoryTest.js`, `diagnostics.js` (RSSI history even). |
| DHCP info | ✗ Missing | No lease/gateway/subnet data surfaced anywhere. |
| DNS status | ✗ Missing | No DNS resolution status field in `/api/v1/status` or any DM view — a DNS failure and a TLS failure and a server outage all look identical (`last_push_http_code`/`last_sync_ms_ago` conflate everything). |
| Internet reachability | ✗ Missing | No distinct check — same conflated inference as DNS above. |

---

## SECTION 4 — Calibration

This is the most consequential finding in the review.

| Capability | Status | Evidence |
|---|---|---|
| Read K-factor | ⚠ Partial | Serial console `show` only (`provision.h:54-55`). Not in `/api/v1/status`. Not in Device Manager. |
| Edit K-factor | ✗ Missing (device-side) | `setCalib()` (`store.h:106-108`) is only ever called from `sync.h:220`, driven by a server push — no serial command, no local-API endpoint, no DM control edits it. |
| Wizard | ✗ Missing | `app.js:31` — stub, as in Section 1. |
| Multi-point calibration | ✗ Missing | Model is one scalar `K_factor` + `density`/`T_ref` — a single linear factor, not a curve. |
| Calibration certificate | ✗ Missing | No PDF/CSV/certificate generation anywhere. |
| History | ⚠ Partial | Server-side `device_events` audit log exists (change tracking with old/new values) but is **not device-attributable** (see below) and not visible in Device Manager. |
| Lock calibration | ✗ Missing | No auth gate beyond generic server session; no device-side lock flag. |
| Export calibration | ✗ Missing | No export function anywhere. |

**The critical finding:** calibration is **entirely server-authoritative and
fleet-global, not per-device**. The firmware's own code comment
(`store.h:78-80`) states K-factor "lives server-side... we only cache the
version + values." The backing table is schema-enforced as a **global
singleton**: `id INTEGER PRIMARY KEY CHECK (id = 1)` — there is structurally
**one** K-factor/density/Tref value for the entire fleet, not one per device.
The code's own comment confirms it outright: "this bench server's calibration
is one shared, fleet-wide K-factor... not per-device."

At 500 devices across 20 factories with different tanks and (implicitly)
different pipe sizes and fluid densities, this means every meter computes
litres against the **same** calibration constants regardless of its actual
physical installation — correct for at most one device, wrong for the rest,
unless every single meter happens to be mechanically and fluid-identical.
Device Manager cannot help here even in principle: the tab is a stub, so a
technician has no way to confirm which calibration is active on a given
device after a physical meter/tank swap beyond eyeballing serial `show`
output.

---

## SECTION 5 — Observability

Source of truth: `diagnostics.h`, which builds `/api/v1/info`, `/api/v1/status`,
and `/api/v1/metrics` from one well-disciplined module (explicit `null`-vs-real
rules, no fabricated values).

| Metric | Status | Evidence |
|---|---|---|
| Firmware version / build SHA | ✓ | `diagnostics.h:43,51` |
| Free heap / heap low-water mark | ✓ | `diagnostics.h:178-179` |
| Heap fragmentation | ✗ | Not tracked |
| Flash usage / capacity_pct_used | ✓ | `diagnostics.h:85`, real LittleFS-derived, alarms at 80/90/95/100% |
| RSSI | ✓ | `diagnostics.h:73,210-215` (live + history) |
| IP address | ✗ | Not self-reported anywhere; only known via mDNS |
| MAC address | ✗ | Not self-reported anywhere; only known via USB descriptor |
| Boot/reset reason | ✓ | `diagnostics.h:264-278`, full `esp_reset_reason()` decode |
| Restart count | ✗ | Only last reason, no cumulative counter — code's own comment names this a known gap (`REBOOT_LOOP` alarm listed as not-yet-buildable) |
| Queue size / backlog / ack_seq / last push HTTP code | ✓ | `diagnostics.h:79-99` |
| HTTP/network latency | ✓ | `diagnostics.h:206-208` `network_rtt_ms` |
| DNS failures | ✗ | No counter anywhere, only a transient one-off log line |
| TLS failures | ✗ | Same — invisible except as opaque non-200 |
| Wi-Fi reconnect count | ✗ | Same gap, explicitly acknowledged in code comments |
| Watchdog reset count / brownout count | ⚠ | Last-reset-reason can say "watchdog"/"brownout" but no cumulative counters exist |
| Temperature | ✗ | Field exists but hard-coded `null` — sensor physically removed from this hardware build |
| Pulse frequency | ✓ | `diagnostics.h:202-203` |
| Sensor stuck-at-zero detection | ✗ | No sensor-health alarm; explicitly deferred in code comments to a future ADR |
| Instantaneous flow rate (L/min) | ✗ | Only raw Hz exposed; litre conversion is server-side only |
| Uptime | ✓ | `diagnostics.h:69` |
| Clock/time sync status | ✗ | No wall-clock/NTP status anywhere |
| Battery | ✗ | Mains-only device by hardware design, no battery/RTC backup |

**UI coverage:** `diagnostics.js` mirrors every `/api/v1/metrics` field 1:1 —
no gap there. But the richer `ota_debug` block (running/boot partition, raw
OTA image state, confirm-attempt result, bootloader-rollback-engaged flag) is
fully computed device-side and **never wired into any Device Manager view** —
an engineer debugging a field OTA failure would have to curl the raw JSON by
hand.

**Net gap for 500-device fleet monitoring:** the absence of *cumulative*
failure counters (DNS/TLS/reconnect/watchdog/brownout/restart) means a device
that resets every 10 minutes looks identical, at a glance, to one that reset
once in its lifetime — only the single most-recent reset reason is visible.

---

## SECTION 6 — Firmware

Read-only review; no code changed.

- **OTA** (`ota.h`): Signed manifest (ECDSA-P256), SHA-256 image-hash
  verification before install, monotonic anti-downgrade floor, expiry checks
  that fail closed without server time. The recent security-floor/rollback
  decoupling (commit `856972e`) is sound — it correctly found that
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` doesn't actually arm a rollback
  trial on this board/toolchain, and re-gated floor advancement on genuine
  app-level health instead. Openly disclosed residual gap: **Secure Boot is
  not enabled** (`ota.h:41-50`) — an eFuse burn deliberately out of scope so
  far. **Verdict: Solid, with a disclosed Secure Boot gap.**
- **Queue** (`queue.h`): flash-backed, CRC32'd, globally monotonic seq,
  ack-only pruning, torn-write recovery. Never refuses writes at capacity —
  keeps writing until LittleFS itself fails, at which point `FailureState`
  durably records it (fail-loud, not silent). **Verdict: Solid.**
- **Flash wear**: relies on LittleFS's native wear-leveling; capacity
  reporting uses real filesystem usage, not an estimate. No explicit
  erase-cycle/endurance alarm (distinct from the capacity alarm that does
  exist). **Verdict: Adequate-with-caveat.**
- **ISR/pulse counting** (`totalizer.h`): uses the ESP32 hardware PCNT
  peripheral specifically because GPIO-interrupt counting was proven to drop
  counts under Wi-Fi load — a real documented prior failure this design
  fixes. Hardware glitch-filter debounce. 16-bit counter drained into a
  64-bit accumulator well before overflow. **Verdict: Solid.**
- **Brownout recovery**: no explicit brownout tuning in firmware — relies on
  framework defaults; not distinguishable as its own alarm type (acknowledged
  gap, deferred to a future ADR). What does survive a brownout — the
  totalizer/queue checkpoints — is proven solid (live in commissioning log
  `[TOT] recovered total=...`). **Verdict: Adequate-with-caveat.**
- **Watchdog**: no explicit watchdog configuration anywhere in firmware
  source — relies entirely on framework defaults, never tuned or reasoned
  about. A watchdog-caused reset is decodable after the fact, but there's no
  proactive strategy. **Verdict: Gap.**
- **Power failure / NVS atomicity**: dual-slot ping-pong + CRC32 checkpoints
  for the totalizer, truncate-before-append for the queue — a power cut
  corrupts at most one slot/row, never silently. **Verdict: Solid.**
- **NVS** (`store.h`): stores device identity, server URL, API key, Wi-Fi
  credentials, calibration cache, and — correctly in a separate namespace
  never cleared by factory reset — the anti-downgrade security floor.
  **No encryption at rest anywhere** — confirmed by grep across the whole
  firmware tree. **Verdict: Gap** (compounds with the Secure Boot gap above:
  no Secure Boot means no barrier to a rogue image reading these plaintext
  secrets either).
- **Retry logic** (`sync.h`): push retries every 5s regardless of outcome
  (fine, since resends are idempotent); Wi-Fi reconnect uses real exponential
  backoff with jitter, capped at 2 minutes. No backoff for a *persistently
  dead server* — a fleet of 500 devices all retrying a downed server every 5s
  is a thundering-herd risk worth reconsidering at that scale, though not a
  single-device defect. **Verdict: Solid for single-device; worth revisiting
  at fleet scale.**
- **Offline operation**: ~3.375 MiB queue partition, a soft
  ~20,000-row/~5.5-hour advisory threshold that does **not** block further
  writes — the device keeps buffering until the filesystem itself is full,
  at which point failure is recorded loudly, never silently. **Verdict:
  Adequate-with-caveat** — no data loss short of actual exhaustion, but no
  proactive field alarm before that point beyond the existing 80/90/95%
  capacity thresholds.
- **Time sync**: no NTP/RTC at all — wall-clock is only ever an estimate
  piggybacked on a successful server push, with zero drift correction. OTA
  manifest expiry correctly fails closed if this estimate has never been
  established. Correctly designed given the constraint, but the constraint
  itself (no RTC/NTP) is a real architectural gap, not a documentation nit.
  **Verdict: Adequate-with-caveat.**

No doc-vs-code conflicts were found in this section — every prior audit claim
about OTA/queue/anti-downgrade still holds against current code, and the one
place current code has moved past an older assumption (the bootloader-rollback
fix) is itself openly documented as a deliberate root-cause correction.

---

## SECTION 7 — Field Service

| Capability | Status | Evidence |
|---|---|---|
| Diagnose | ✓ Exists | Live Monitor + Diagnostics + Dashboard alarms cover this well. |
| Upgrade (trigger OTA) | ✗ Missing | `ota.js` is read-only (Section 1). |
| Backup / Restore | ✗ Missing | Doesn't exist (Section 1). |
| Export logs | ✗ Missing | Firmware endpoint is a 501 stub; no export/save-as action even if it worked. |
| Replace Wi-Fi (on a live, deployed device) | ✗ Missing | `postConfig` is architecturally AP-mode-only — a technician must send the device back through factory reset + SoftAP (serial console) to touch Wi-Fi at all once it's live in the field. |
| Replace API key (on a live device) | ✗ Missing | Same limitation. |
| Verify calibration | ✗ Missing | No calibration surface exists at all. |
| Check sensor | ⚠ Partial | Only indirectly via `pulse_frequency_hz`; a dedicated sensor self-test exists only as a **serial-only** factory test, invisible to Device Manager. |

**Bottom line:** a field engineer standing in front of a misconfigured or
drifted device in a factory **cannot** fix its Wi-Fi, rotate its key, push a
firmware update, or check its calibration through Device Manager alone — every
one of those falls back to a USB-and-laptop visit. At 500 units, that is not
a viable operating model for routine maintenance.

---

## SECTION 8 — Fleet Readiness (500 devices / 20 factories)

| Requirement | Status | Evidence |
|---|---|---|
| Multi-device list/monitoring | ✓ Exists | `devices.js`/`dashboard.js` built device-list-first with tags/location/model/fw filtering from the start. |
| Bulk operations | ✗ Missing | Every write path takes one device at a time; no multi-select-and-apply UI or IPC exists anywhere. |
| Device registry / unique fleet ID | ⚠ Partial | `logical_device_id` allocation exists, but only as a **factory-provisioning-time-only** flow — not usable for reassigning/reprovisioning a device already in the field. Confirmed elsewhere in the audit trail that this field commonly reads `null` in practice. |
| Tank/plant assignment | ✗ Missing | No field, form, or schema anywhere maps a device to a tank or plant — only a free-text `location`/`tags` string, purely local to one technician's laptop. |
| Different Wi-Fi per factory | ⚠ Partial | Works, but only by hand-running the wizard once per device — no templating, no bulk-apply-by-site. |
| Different firmware versions across fleet | ⚠ Partial | Version is tracked per-device for filtering, but there's no rollout dashboard or staged-rollout control (consistent with OTA-trigger being entirely absent). |
| Multi-operator shared state | ✗ Missing | `deviceStore.js` is a single local JSON file **per PC** — tags/location/notes made by one technician's laptop never appear on a colleague's. With multiple technicians across 20 factories, this is a real operational gap, not a hypothetical one. |

**Bottom line:** Device Manager is well-architected for **one technician
managing devices one at a time**, with a clean security model and honest
not-yet-available stubs instead of fake data. It has no bulk/fleet
operations, no shared multi-operator registry, no tank/plant data model, and
no OTA rollout control — all of which stop being optional at 500 units across
20 sites.

---

## SECTION 9 — Security

- **Provisioning secrets in transit/logging**: the serial console's `show`
  command was already remediated (commit `443dc44`, confirmed live in code)
  to print only a fingerprint, never the raw API key; every local-API route
  is confirmed read-only-by-design and never emits secrets. **Verdict:
  Solid.**
- **Secrets at rest (NVS)**: confirmed unencrypted (see Section 6) — Wi-Fi
  password and API key are plaintext in flash. Anyone with physical
  UART/JTAG access (which this board exposes natively) can read them with
  `esptool read_flash` + an NVS parser, no login required. At 500 field-
  deployed devices, physical compromise (theft, tampering, resale) is a
  realistic threat model, not a hypothetical one. **Verdict: Gap.**
- **API key model**: a single static bearer-style shared secret per device.
  Rotation exists **server-side** (`/admin/devices/.../rotate-key`, out of
  this review's scope) but the device's own local API has no writable
  surface at all in a release build (`RELEASE_BUILD`/`FACTORY_TEST_BUILD`
  flag correctly fences the only writable route out of shipping images) — so
  there's no local-network attack surface to rate-limit against in the first
  place. **Verdict: Solid for the release image.**
- **Local HTTP API access control**: confirmed that in a real `[env:release]`
  build, the device's local HTTP API has **zero write/admin surface** — no
  reboot, no factory-reset, no config-write endpoint reachable over the
  network. Config writes only ever happen through the AP-mode-only
  provisioning server, never concurrently with normal operation. **Verdict:
  Solid.**
- **OTA signing/authenticity**: real ECDSA-P256 manifest signing + SHA-256
  image-hash verification, both checked before install. The build system
  **fails closed**: a `RELEASE_BUILD=1` image cannot compile while the
  embedded public key is still the test placeholder. As of today, it is
  still the test placeholder — expected pre-production state, not a defect,
  but it means **someone must run the real key-generation ceremony before
  the first commercial release build can even compile.** **Verdict: Solid
  design, one mandatory action item before shipping.**
- **Recovery path**: serial-console-only by design — a real security
  property (no remote attacker can reconfigure or wipe a device), but the
  flip side of the Section 7 field-service gap: legitimate ops can't fix a
  device remotely either.
- **Factory reset completeness**: genuinely wipes API key, Wi-Fi
  credentials, server URL, logical ID, and calibration cache from active NVS
  (correctly does **not** wipe the separate anti-downgrade security floor,
  which is the right call — that must survive a reset to prevent a
  downgrade-attack reset vector). The same at-rest-encryption gap applies
  here too: `p_.clear()` marks NVS entries deleted but doesn't cryptographically
  erase flash, so a forensic read of a "factory reset" device could still
  recover previously-written secrets until overwritten. **Verdict:
  Adequate-with-caveat**, contingent on the NVS-encryption gap above.

**Two items to carry into prioritization:** (1) NVS/flash encryption at
rest is not enabled — the most serious single finding for a 500-device fleet
where physical compromise is realistic; (2) the OTA signing key is still the
test placeholder — expected right now, but the release build's own compile
gate means this cannot be silently forgotten before the first ship.

---

## SECTION 10 — Commercial Readiness: Prioritized Action List

### P0 — Must have before manufacturing/deploying 500 units

1. **Per-device calibration.** Replace the fleet-global `CHECK(id=1)`
   singleton with a per-device K-factor/density/Tref record. Without this,
   volumetric readings are provably wrong for any device that doesn't share
   the one global calibration's physical characteristics — this affects data
   correctness, not just usability. (Section 4)
2. **Real device/asset registry with tank & plant assignment.** No layer of
   the system today can answer "which physical tank does device X meter" or
   "which factory is device X in" in a structured, queryable way. This is a
   day-one requirement at 20-factory scale, not a nice-to-have. (Sections 2, 8)
3. **NVS/flash encryption at rest.** Plaintext Wi-Fi passwords and API keys
   on 500 physically-distributed devices is a real breach vector (theft,
   decommissioning, resale). (Sections 6, 9)
4. **Execute the production OTA signing-key ceremony** and replace the
   embedded test placeholder — the release build already refuses to compile
   until this is done, so treat it as a hard release gate, not an optional
   hardening step. (Section 9)
5. **Bulk/fleet provisioning capability.** One-device-at-a-time Wi-Fi setup
   does not scale to 500 units across 20 networks — needs at minimum
   per-site Wi-Fi profile templating and a bulk-apply path. (Sections 2, 3, 8)
6. **Remote reconfiguration of already-deployed devices** (Wi-Fi, API key)
   without a truck roll — today this requires physically returning the
   device to AP mode via factory reset + USB. (Sections 3, 7)
7. **OTA trigger and fleet rollout control in Device Manager.** OTA is
   currently monitor-only; there is no way to push, stage, or track a
   firmware rollout across a fleet from this tool. (Sections 1, 8)
8. **Real sensor/flow-accuracy validation** against actual oil flow — every
   prior audit session observed a static totalizer value; this has never
   been certified against real product use. (carried from prior audit trail,
   still open)

### P1 — Strongly recommended

1. **Cumulative failure counters** (DNS/TLS/Wi-Fi-reconnect/watchdog/
   brownout/restart) — today only the single most-recent reset reason is
   visible, so a flapping device is indistinguishable from a healthy one
   across a 500-device dashboard. (Section 5)
2. **Sensor-stuck-at-zero / sensor-health alarm** — a genuinely failed
   sensor currently produces confidently-wrong zero-flow data with no
   operator signal. (Section 5, carried from prior audit trail)
3. **Live Wi-Fi credential test before commit** in the provisioning wizard,
   instead of inferring success from mDNS reappearance. (Section 2, 3)
4. **Config backup/restore** in Device Manager — doesn't exist at all today.
   (Section 1, 7)
5. **Working log export** — the UI is wired, the firmware endpoint is a
   stub. (Section 1, 7)
6. **Multi-Wi-Fi-profile support** with fallback ordering — currently
   exactly one profile, no fallback. (Section 3)
7. **Wire the existing `ota_debug` diagnostic block into Device Manager** —
   it's fully computed device-side today and simply never surfaced. (Section 5)
8. **Shared/cloud device registry** instead of a per-laptop local JSON
   store — multiple technicians across 20 factories currently cannot see
   each other's device metadata. (Section 8)
9. **One unified commissioning wizard** combining logical-ID assignment +
   Wi-Fi setup + verification, instead of two disconnected screens.
   (Section 2)
10. **Positive commissioning verification / final health-check gate** in
    Device Manager, replacing the current "infer success from mDNS, then
    manually eyeball the dashboard" flow. (Section 2)
11. **Calibration governance UI** (read/edit/wizard/history/lock/export) —
    contingent on P0 item 1 (per-device calibration) landing first, since
    building UI on top of the current global singleton would be
    counterproductive. (Section 4)
12. **Explicit watchdog configuration and reasoning**, rather than relying
    entirely on framework defaults. (Section 6)

### P2 — Nice to have

1. Device self-reports IP/MAC in its local API (currently only known via
   mDNS/USB descriptor). (Section 5)
2. Instantaneous flow-rate (L/min) computed and exposed device-side, not
   just raw pulse Hz. (Section 5)
3. Calibration certificate / export artifact. (Section 4)
4. Distinct DHCP/DNS/internet-reachability diagnostics in Device Manager,
   replacing the current conflated "last push HTTP code" signal.
   (Section 3, 5)
5. OTA poll jitter / staged rollout to avoid synchronized fleet-wide update
   waves. (Section 6, carried from prior audit trail)
6. Heap fragmentation metric (beyond current free/min-free tracking).
   (Section 5)
7. Explain/bound the observed filesystem-usage upward trend. (carried from
   prior audit trail)

### P3 — Future

1. Secure Boot / eFuse-based signed app images — a one-time factory
   procedure already documented (`Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md`)
   but not yet executed; compounds with the NVS-encryption gap above.
   (Section 6, 9)
2. NTP/RTC time sync — architecturally absent; the current design correctly
   fails closed in the meantime, but real wall-clock precision would remove
   a whole category of edge cases. (Section 6)
3. Multi-point calibration curve, if real sensor linearity testing (P0 item
   8) shows a single K-factor scalar is insufficient. (Section 4)
4. Battery/RTC backup — not supported by this hardware revision (mains-only
   by design); a future hardware decision, not a firmware one. (Section 5)
5. Desktop app auto-update mechanism for Device Manager itself — already
   disclosed as an accepted scope boundary in prior audits.

---

## Appendix A — Already fixed/certified (not re-flagged above)

Per the project's own audit trail, the following are resolved and were
deliberately **not** re-investigated in this review, per the user's
instruction not to re-litigate fixed bugs:

- Server-side ack-watermark/queue-poisoning stall (RISK-01) — closed, and
  independently re-proven live on hardware this session.
- Zero admin authentication on `/admin/*` (RISK-03) — closed (Basic Auth,
  rate limiting, audit logging, fail-closed startup, 18 tests).
- Silent data loss at flash-full (RISK-04) — code-closed with a fault-
  injection harness.
- OTA anti-downgrade floor (RISK-15) — code-closed and hardware-proven.
- OTA signature/hash/hardware-compat/interrupted-download protections —
  hardware-proven across the engagement.
- Server-side exactly-once storage and sequence-gap handling — proven live
  with real duplicate-POST tests.
- Reset-reason mapping and `server_time_ms` overflow parsing — fixed
  (commit `ffef438`).
- OTA security-floor/bootloader-rollback-state decoupling — fixed (commit
  `856972e`), independently confirmed sound in Section 6 above.
- Serial console `show` leaking the raw API key — fixed (commit `443dc44`),
  confirmed still in effect in Section 9 above.
- Device pointed at a bench workstation over plain HTTP — resolved; device
  now runs against production HTTPS with a pinned Let's Encrypt CA (commit
  `e5a593b`), confirmed live in the commissioning session.
- Physical commissioning / the ack-sequence stall at 58736–58785 — complete,
  per this session's final live commissioning proof.

## Appendix B — Conflicting or unverified claims flagged for follow-up

- Whether the RISK-04 flash-fault-injection test harness has actually been
  run and passed in CI (older docs left this as "written, execution
  pending") was not re-verified in this pass — check current CI run
  history directly if this matters for the go/no-go decision.
- Whether the device's API key is still the shared dev bootstrap key
  (`dev-key-change-me`) or a real per-device production key is
  inconsistently stated across older docs vs. the recent commissioning
  session (which showed a fingerprint matching "the registered production
  key"). These likely describe different points in time — worth an explicit
  current-state check before relying on either claim for the 500-device
  rollout, since if per-device keys aren't yet real, that becomes a P0 item
  alongside the OTA signing key.
