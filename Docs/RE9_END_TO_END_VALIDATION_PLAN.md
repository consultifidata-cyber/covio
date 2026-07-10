# RE-9 — End-to-End Production Validation Plan

**Status:** Plan only (RE-9, Release Engineering — End-to-End Production
Validation). **No test in this document has been executed.** Per this
phase's own explicit instruction ("Do not execute the tests. Only prepare
the validation plan"), and consistent with this project's own established
discipline of never claiming a result that wasn't actually observed, every
Pass/Fail field below is recorded as **NOT EXECUTED — PENDING HARDWARE /
REMOTE**, not "assumed pass."

## Why this plan cannot be executed in the current environment (stated up front)

Every prior RE-phase disclosed the same underlying constraints; RE-9 is
where they finally become directly blocking, not just a documentation
caveat:

- **No physical ESP32 hardware, SD card, or flow meter/pulse source exists
  in this environment.** No firmware build in this project's history has
  ever been flashed to real hardware.
- **No compiler/PlatformIO toolchain or Python interpreter has ever been
  available in this environment** (`VERSIONS.md`) — firmware and backend
  have never been executed here, only reviewed and, since RE-5/RE-4,
  compiled/tested by CI configuration that has itself never run for real.
- **This repository has no configured GitHub remote** (`CONTRIBUTING.md`,
  disclosed since RE-1) — `.github/workflows/ci.yml` and `release.yml`
  have never executed on a real runner. GitHub Actions and GitHub Releases
  validation (§4, §5) are hard-blocked on this precondition alone.
- **This development machine cannot locally package the Windows
  installer** (`Docs/DESKTOP_PACKAGING.md`'s disclosed `winCodeSign`
  symbolic-link restriction) — installer validation (§6) requires either a
  real GitHub Actions `windows-latest` runner or a differently-configured
  Windows machine (`SETUP.md` §9's documented workaround).

This plan is written so that **once those preconditions are met**, each
section below can be executed and its Pass/Fail field updated in place —
it is the complete, ready-to-run procedure, not a placeholder to be
rewritten later.

## How to read each subsystem section

Every subsystem below defines exactly six fields, per this phase's own
required structure: **Preconditions**, **Test Procedure**, **Expected
Result**, **Failure Criteria**, **Evidence Required**, **Pass/Fail
Status**. Every claim (route path, field name, timing constant, error
code) is taken directly from the actual source it governs — cross-checked
against the real file during this plan's own authoring, not recalled from
memory — and cited so a validator can jump straight to the ground truth.

---

## 1. Firmware

**Preconditions**
- ESP32 Dev Module board, USB cable, SD card (FAT32) wired to `PIN_SD_CS`
  (GPIO4), pulse source on `PIN_PULSE` (GPIO27) — or `SIM_PULSES=1` +
  jumper GPIO25→GPIO27 for a meter-free bench run (`config.h`).
- `SETUP.md` §3 toolchain installed (PlatformIO Core `6.1.15`, platform
  pin `espressif32@6.5.0`).
- Partition scheme is OTA-capable (`min_spiffs.csv`, `platformio.ini`'s
  `[env]` section) — required for §12 (OTA) later in this plan.

**Test Procedure**
1. `pio run -e esp32dev` — compile-verify the bench/dev build.
2. `pio run -e release` — compile-verify the production build
   (`-DRELEASE_BUILD=1`). Additionally: temporarily leave
   `DEFAULT_API_KEY` unchanged and confirm the build refuses to boot (not
   just refuses to compile) with it still active — `ADR-005`'s Definition
   of Done ("no device leaves the factory floor with the default API key
   active"), enforced in `covio_firmware.ino`'s `setup()`.
3. `pio run -e factory` — compile-verify the factory-test build
   (`-DFACTORY_TEST_BUILD=1`).
4. `pio run -e esp32dev -t upload`, then `pio device monitor -b 115200`.
5. Observe the boot sequence: NVS seed/read (`store.h`'s `begin()`), SD
   mount, PCNT init, `[LOCALAPI] HTTP server started on :80`, and (once
   WiFi connects) `[LOCALAPI] mDNS started: covio-<hex>.local`.
6. Serial console: run `show` and confirm `device_id`, `fw`, `boot_id`,
   `server_url`, `wifi_ssid` print correctly (`provision.h`).

**Expected Result**
All three environments compile without error — this is this project's
**first-ever real compile** (`VERSIONS.md`, `Docs/FIRMWARE_BUILD.md`), so
"compiles cleanly" is itself a meaningful, non-trivial result, not a
formality. `esp32dev` boots to a stable idle loop with the local API
reachable at `http://<device-ip>/`.

**Failure Criteria**
- Any compile error in any of the three environments.
- `release` build boots with the default API key still active (a Critical
  security-gate failure, `ADR-005`).
- Boot loop, panic, or watchdog reset before reaching a stable idle state.
- SD card not detected causing `queue.h` initialization failure with no
  diagnostic message.

**Evidence Required**
PlatformIO build logs (all 3 environments, full output), Serial Monitor
capture of the full boot sequence, `show` command output, `pio run` exit
codes.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE.** No ESP32 board,
compiler, or PlatformIO installation exists in this environment.

---

## 2. Backend

**Preconditions**
- `SETUP.md` §4 (Python 3.11, `pip install flask`), a clean/fresh
  `server/covio.db` (or none — `init_db()` creates it), repository root as
  working directory for `test/native/`.

**Test Procedure**
1. `python -m unittest discover -s test/native -v` — the full native test
   suite (`test_dm_phase_0b_auth.py`, `test_adr001_schema.py`,
   `test_dm_phase_4_registry.py`, `test_dm_phase_6_logical_id.py`).
2. `cd server && python server.py` — confirm it starts on
   `http://0.0.0.0:8000` (`server.py`'s own `app.run()` call).
3. `POST /api/iot/flow/push` with header `X-Api-Key: dev-key-change-me`
   (`BOOTSTRAP_DEFAULT_API_KEY`) and a body containing `records` with
   valid `schema_version`/`record_type`/`boot_id`/`seq`/`ts`/`totalizer` —
   confirm `{"ack_seq": ..., "server_time_ms": ...}`.
4. Repeat step 3 **without** the `X-Api-Key` header — confirm
   `401` with `{"error":{"code":"MISSING_API_KEY", ...}}`.
5. `GET /api/iot/flow/config` (with a valid key) — confirm
   `K_factor`/`density`/`T_ref`/`version` fields.
6. `GET /api/iot/flow/ota/manifest` (with a valid key, no manifest file
   present) — confirm `{"version":"","url":""}` (server.py's own
   documented no-op contract).
7. Submit one record with an unsupported `schema_version` and one missing
   a required field (e.g. no `totalizer`) — confirm both land in
   `quarantined_records`, not `records`, and do not abort the rest of the
   batch (per-record isolation, `push()`'s own documented behavior).

**Expected Result**
All native tests pass (44 desktop-app tests are a separate suite — this
count is the backend's own, expected to match whatever `test/native/`
currently contains). `push`/`config`/`ota/manifest` behave exactly as
`config.h`'s `PATH_*` constants and this file's own contract dictate.
Quarantine logic isolates bad records without crashing the request.

**Failure Criteria**
Any test failure; a 500 error on any of the four device-facing endpoints;
an unauthenticated request succeeding; a malformed record crashing the
whole push request instead of being quarantined; `ack_seq` not computed as
the highest **contiguous** seq (`push()`'s own documented cumulative-ack
logic).

**Evidence Required**
Full `unittest` output (pass/fail count), raw HTTP request/response
transcripts for steps 3–7, `quarantined_records`/`records` table row
counts before/after step 7.

**Pass/Fail Status:** **NOT EXECUTED — PENDING PYTHON INTERPRETER.** No
Python interpreter has been available in any environment this project has
been developed in (`VERSIONS.md`); `test/native/`'s ~46 tests have never
actually run.

---

## 3. Desktop Device Manager

**Preconditions**
- `SETUP.md` §5–6 (Node.js 24.x, npm), `npm ci` in
  `tools/device-manager/`, Windows 10/11 (the app's only packaged target).

**Test Procedure**
1. `npm test` (44 unit tests, `node --test`) and `npm run lint` (ESLint) —
   already re-verified with real, passing results as part of RE-6/RE-7/
   RE-8's own work (44/44 pass, 0 errors, 3 pre-existing warnings); re-run
   here as this plan's own baseline gate before interactive testing.
2. `npm start` — launch the Electron app. Confirm the window opens with no
   uncaught main-process exception.
3. Open DevTools (if enabled) and confirm: no Content-Security-Policy
   violation messages, no `require`/`process` reference errors in renderer
   code (the CommonJS/ES-module split, `Docs/CODE_QUALITY.md`).
4. Navigate every view: Dashboard, Discovery, Live Monitor, Diagnostics,
   OTA, Settings, Provisioning, Factory Test, Devices, Logs. Confirm each
   renders without a blank/error screen (`notYetAvailable.js` is the
   correct, honest placeholder for anything genuinely unimplemented — not
   a bug if shown where expected).
5. Inspect the DOM/console for the raw API key string at any point during
   §8/§13 below — it must never appear (`apiKeyPill` component,
   `factoryTest.js`'s own documented security note).

**Expected Result**
App launches, all views render, `contextIsolation`/`sandbox` are on and
`nodeIntegration` is off (verified by the CommonJS-main/ES-module-renderer
split holding at runtime, not just in source), no raw secret ever appears
in the UI, DevTools console, or on disk (`deviceStore.json`).

**Failure Criteria**
Crash on launch or on any view navigation; a CSP violation; the raw API
key rendered, logged, or persisted anywhere; a `require`/Node API
reachable from renderer code at runtime.

**Evidence Required**
`npm test`/`npm run lint` output, screenshots of every view, DevTools
console log (clean), a file-system inspection of `deviceStore.json`
confirming no raw key is stored in it.

**Pass/Fail Status:** **NOT EXECUTED (interactive/hardware portion) —**
automated `npm test`/`npm run lint` have real, passing results from RE-6
through RE-8 (44/44, 0 errors) and may be treated as satisfied for step 1
specifically; steps 2–5 require a live device (§7 onward) and have not
been run.

---

## 4. GitHub Actions

**Preconditions**
This repository pushed to a real GitHub remote with Actions enabled — **a
hard precondition not yet met** (`CONTRIBUTING.md`, disclosed since RE-1:
zero commits, no remote).

**Test Procedure**
1. Push a commit to `main` (or open a PR) and confirm `.github/workflows/ci.yml`
   triggers.
2. Confirm all five jobs run and pass: `repo-validation`, `firmware`,
   `desktop` (both `windows-latest` and `ubuntu-latest` legs), `backend`,
   `code-quality`.
3. Inspect the `firmware` job specifically — this is the **first real
   compile** of this codebase in any environment; a failure here is a
   genuine discovery, not CI flakiness (`Docs/FIRMWARE_BUILD.md`'s own
   "if the first real CI run fails" procedure applies).
4. Confirm the `firmware` job uploads a `firmware-vX.Y.Z` artifact
   containing `.bin`, `.elf`, `.sha256` for each (and `.map` if produced).
5. Confirm `code-quality`'s Prettier step reports its known, non-blocking
   `::warning::` (27/27 files, `Docs/CODE_QUALITY.md`) without failing the
   job, while `ruff check`/ESLint remain blocking.

**Expected Result**
All jobs green on a clean push. The `firmware` job's compile succeeding
for the first time is itself the primary deliverable of this test, not
just "CI is green."

**Failure Criteria**
Any job failure. Specifically for `firmware`: a compile error against the
pinned `espressif32@6.5.0`/`platformio==6.1.15` toolchain — per
`Docs/FIRMWARE_BUILD.md`, the fix is a targeted one (a version bump or a
source fix tied to the exact reported error), not a workflow redesign.

**Evidence Required**
Actions run URL, full logs for all five jobs, the `firmware` job's
uploaded artifact contents.

**Pass/Fail Status:** **NOT EXECUTED — BLOCKED ON PRECONDITION.** No
GitHub remote is configured; `ci.yml` has never run on a real runner.

---

## 5. GitHub Releases

**Preconditions**
§4 passing on a real remote; a SemVer tag ready to push
(`Docs/RELEASE_VERSIONING.md`'s `vX.Y.Z` / `vX.Y.Z-rc.N` format).

**Test Procedure**
1. `git tag v1.0.0-rc.1 && git push origin v1.0.0-rc.1` (`SETUP.md` §11) —
   start with a release-candidate tag, not a stable one, for this first
   real exercise of the pipeline.
2. Watch `.github/workflows/release.yml`'s four jobs run in sequence:
   `determine-version` → `verify-and-build-firmware` (reuses `ci.yml`
   wholesale) → `package-desktop` (reuses `desktop-package.yml`) →
   `publish`.
3. Confirm `determine-version` parses `is_prerelease=true` for the `-rc.1`
   tag (`Docs/RELEASE_AUTOMATION.md`'s prerelease-detection logic).
4. Confirm `publish`'s own "Verify release assets" step finds all 8
   required files under their exact `Docs/RELEASE_VERSIONING.md` names
   before calling `gh release create`.
5. Confirm the GitHub Release is created and marked **Pre-release**
   (matching step 3), with `--generate-notes` output populated.
6. Repeat with a stable tag (e.g. `v1.0.0`) once satisfied with the `-rc.1`
   dry run — confirm it publishes as a full Release, not a prerelease.

**Expected Result**
A GitHub Release page with exactly 8 assets: `covio-firmware-vX.Y.Z.bin`
+ `.sha256`, `covio-firmware-vX.Y.Z.elf` + `.sha256`,
`covio-device-manager-setup-vX.Y.Z.exe` + `.sha256`,
`covio-device-manager-vX.Y.Z-portable.exe` + `.sha256` (plus `.map`/
`.map.sha256` if the build produced one — best-effort,
`Docs/FIRMWARE_BUILD.md`).

**Failure Criteria**
Any missing/misnamed asset (should fail loud in `publish`'s own
verification step, per `Docs/RELEASE_AUTOMATION.md` — a silent partial
release would itself be a Critical finding); wrong prerelease flag; the
`publish` job failing on an `actions/download-artifact` permissions error
(the `actions: read` fix from RE-7's own self-audit should prevent this —
confirming it holds on a real run is exactly what this test is for).

**Evidence Required**
Release page screenshot/URL, all four job logs, downloaded assets with
`sha256sum -c` (or `certutil -hashfile ... SHA256`) verified against their
`.sha256` files.

**Pass/Fail Status:** **NOT EXECUTED — BLOCKED ON §4.** No tag has ever
been pushed; `release.yml` has never run.

---

## 6. Windows Installer

**Preconditions**
§5 producing real installer/portable assets (or a manual
`desktop-package.yml` `workflow_dispatch` run); a Windows 10/11 machine —
**not** this development account (`Docs/DESKTOP_PACKAGING.md`'s disclosed
`SeCreateSymbolicLinkPrivilege` restriction) unless Developer Mode is
enabled first (`SETUP.md` §9).

**Test Procedure**
1. Download `covio-device-manager-setup-vX.Y.Z.exe` and its `.sha256`.
2. Verify the checksum locally before running anything.
3. Run the installer. Confirm the NSIS dialog (`oneClick: false`,
   `allowToChangeInstallationDirectory: true`, `package.json`'s
   `build.nsis`) lets you choose an install directory.
4. Confirm the installed executable is named `CovioDeviceManager.exe`
   (`build.win.executableName`) and the app launches from the Start Menu.
5. Separately, download `covio-device-manager-vX.Y.Z-portable.exe`,
   verify its checksum, and confirm it runs standalone with no
   installation step.
6. Uninstall via Windows' own "Apps & Features" and confirm a clean
   removal (no orphaned Start Menu entries or registry-visible leftovers
   beyond what NSIS's own uninstaller is expected to leave).

**Expected Result**
Both checksums match. Installer and portable executable both launch the
same application, unsigned (no code-signing certificate configured yet —
`Docs/DESKTOP_PACKAGING.md`'s disclosed, expected state) — Windows
SmartScreen may warn on first run; this is expected, not a defect, given
the disclosed code-signing status.

**Failure Criteria**
Checksum mismatch (Critical — indicates a corrupted or tampered
artifact); installer crash or failure to complete; wrong executable name;
uninstall leaves the app runnable or leaves user data it shouldn't
(`deviceStore.json` persisting after uninstall is expected/acceptable —
document which files persist).

**Evidence Required**
Checksum verification command output, installation/uninstallation
screenshots, confirmation of the launched app's version (Settings/about)
matching the release tag.

**Pass/Fail Status:** **NOT EXECUTED — BLOCKED ON §5**, and independently
unverifiable end-to-end on this development machine even once assets
exist (disclosed `winCodeSign` restriction, `Docs/DESKTOP_PACKAGING.md`).

---

## 7. Device Discovery

**Preconditions**
A provisioned, WiFi-connected ESP32 (§1 booted, on the same LAN/subnet as
the PC running Device Manager); multicast (mDNS/UDP 5353) not blocked by
the router or Windows Firewall.

**Test Procedure**
1. Launch Device Manager (§3) with the ESP32 already powered on and
   connected to WiFi.
2. Open the Discovery/Dashboard view and observe `discovery.js`'s
   continuous `_covio._tcp.local` PTR query (`SERVICE_TYPE`,
   `discovery.js`).
3. Confirm the device appears with `hardware_id`, `model`, `fw_version`
   (pulled from `GET /api/v1/info`) and the TXT-record-advertised
   `logical_device_id` (empty/null pre-factory-provisioning, per
   `local_api.h`'s `startMdns_()`).
4. Power the device off; confirm the app eventually reflects it as
   unreachable rather than stale-"online" forever.
5. Separately, test the manual-IP fallback path (`discovery.js`'s own
   documented reason for existing: "networks that block mDNS multicast"):
   enter the device's IP directly and confirm the same `/api/v1/info`
   data resolves.

**Expected Result**
Device appears within a few seconds of both the app and device being on
the same network (mDNS starts ~1s after the device's WiFi connects,
`local_api.h`'s `service()` throttle). No duplicate entries for the same
`hardware_id`.

**Failure Criteria**
Device never appears despite being reachable by IP (indicates an mDNS/
multicast-handling regression, not a network problem — cross-check with
the manual-IP path in step 5 to isolate which layer failed); duplicate or
stale entries after the device goes offline.

**Evidence Required**
Discovery view screenshot showing the device, timestamp from device power-on
to first appearance, manual-IP fallback screenshot.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE + LAN.**

---

## 8. Wi-Fi Provisioning

**Preconditions**
An unconfigured or factory-reset device (serial `factory` command, or a
never-before-provisioned unit); a PC WiFi adapter free to join the
device's SoftAP.

**Test Procedure**
1. Power on the unconfigured device. Confirm it attempts station connect,
   times out after `AP_FALLBACK_TIMEOUT_MS` (15s, `config.h`), and starts
   an **open** SoftAP named `Covio-Setup-<last4-of-hardware-id>`
   (`wifi_provision.h`'s `apSsid_()`).
2. In Device Manager's Provisioning wizard, follow all four steps exactly
   as implemented (`provisioning.js`'s own `STEPS` array):
   - **Detect** — confirm `app.covio.wifi.scanNearbySetupNetworks()`
     lists the SSID on Windows (`netsh`-based scan, `wifiScan.js`), or
     confirm the "connect manually" message appears on non-Windows.
   - **Connect** — connect the PC to the SoftAP, confirm reachability at
     `192.168.4.1` (`DEFAULT_AP_IP`) via `GET /api/v1/info`.
   - **Configure** — submit real target WiFi SSID/password, `server_url`,
     and `api_key` via `POST /api/v1/config`.
   - **Verify** — confirm the wizard polls for the device to reappear via
     mDNS within `REAPPEAR_TIMEOUT_MS` (90s).
3. Negative case: submit a deliberately wrong WiFi password. Confirm the
   device reboots, fails to join, re-enters AP-fallback, and the wizard's
   "reappear" timeout expires and offers a retry (`provisioning.js`'s own
   documented risk/handling) — the firmware's JSON path does **not**
   live-test the password (only the HTML captive-portal form does), so
   this is the *expected*, documented behavior, not a bug.
4. Negative case: submit an empty required field via the JSON path and
   confirm `400 {"error":{"code":"CONFIG_MISSING_FIELD", ...}}`; submit a
   `server_url` without `http(s)://` and confirm
   `400 {"error":{"code":"CONFIG_INVALID_URL", ...}}` (`wifi_provision.h`'s
   `validateFields_()`).

**Expected Result**
Happy path: device joins the real network and reappears in Discovery
(§7) within the timeout. Wrong-password path: wizard detects the failure
and offers a graceful retry rather than hanging indefinitely.

**Failure Criteria**
AP SSID never appears; `192.168.4.1` unreachable from the wizard's
"Connect" step; a valid config submission does not result in the device
reappearing on the target network; the wrong-password case hangs past
90s with no retry offered; validation error codes don't match §13 A.3's
frozen contract.

**Evidence Required**
Screenshots of all four wizard steps (happy path) and the retry path
(negative case), Serial Monitor log of AP start → config write → reboot →
station connect, HTTP transcripts of both negative-case requests.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE.**

---

## 9. Telemetry

**Preconditions**
A provisioned device (§8 complete) with a reachable `server_url` (§2
running), `SIM_PULSES=1` or a real meter, SD card present.

**Test Procedure**
1. With the backend running and the device connected, observe
   `TELEMETRY_PERIOD_MS` (1000ms) record building and `PUSH_PERIOD_MS`
   (5000ms) push attempts via Serial logs and `/api/v1/status`'s
   `queue.backlog`/`last_sync_ms_ago`/`last_push_http_code` fields.
2. Confirm `ack_seq` advances and SD queue backlog trends toward 0 during
   sustained connectivity.
3. Confirm `CONFIG_POLL_MS` (60000ms) K-factor polling updates
   `cfg_ver`/`kfactor_version` after an admin K-factor change
   (`POST /admin/kfactor`) — verify the *next* telemetry batch stamps the
   new `kfactor_version`, not the old one.
4. Disconnect the device from the backend (stop `server.py`, or block the
   route) for several minutes. Confirm: no crash; SD queue backlog grows;
   `/api/v1/health` reports `QUEUE_HIGH` (WARNING, backlog > 1000) or
   `QUEUE_CRITICAL` (CRITICAL, backlog > 10000) as appropriate
   (`diagnostics.h`'s `computeHealth_()`); `health_state` becomes
   `"offline"` once `millis() - lastSyncMs() > 300000` (5 minutes, same
   file).
5. Restore connectivity. Confirm zero-loss catch-up: every record queued
   during the outage is eventually pushed and acked, in order, with no
   duplicates (`server.py`'s `UNIQUE(device_id, boot_id, seq)` idempotency
   — verify via the `records` table row count matching the expected count
   exactly, not more).

**Expected Result**
Continuous operation shows a shrinking/near-zero backlog; an outage
queues losslessly and drains completely on reconnect; health-state
transitions match §13 A.4's precedence rule exactly (offline → degraded →
ok).

**Failure Criteria**
Any lost or duplicated record after an outage; `ack_seq` not advancing
correctly (non-contiguous gap logic malfunctioning); alarms not firing at
the documented thresholds; `health_state` stuck on a stale value.

**Evidence Required**
Time-series capture of `/api/v1/status`/`/api/v1/metrics` across the
outage-and-recovery window; server-side `records` table row count before/
during/after; Serial Monitor logs of push attempts and failures.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE + BACKEND.**

---

## 10. Device Twin

**Preconditions**
§2 (backend) and §9 (telemetry) both active — a device actively pushing
and/or config-polling.

**Test Procedure**
1. After a push, query `GET /admin/devices` (or the underlying `devices`
   table) and confirm `last_fw_version`, `last_kfactor_version`,
   `last_seen_ms`, `last_push_totalizer`, `derived_health_state` all
   reflect the just-completed push.
2. Send a request that produces **only** quarantined records (e.g. an
   unsupported `schema_version`) and confirm `last_seen_ms`/`last_fw_version`
   still update (a quarantined-but-parseable request still proves
   liveness, per `push()`'s own documented behavior) but
   `last_push_totalizer` does **not** change (quarantined data must never
   reach the Twin, per the same function's own comment).
3. Stop all traffic from the device and, after >5 minutes, reload
   `/admin/devices` — confirm the displayed health recomputes to
   `"offline"` **live**, at read time (`devices_dashboard()`'s own
   `derive_health_state()` call), not from the stored, potentially-stale
   `derived_health_state` column — this was a specific, already-documented
   audit fix (CHANGELOG's DM-Phase 4 entry) and this test re-confirms it
   holds.
4. Send a `config`-poll-only request (no push) and confirm it also
   updates `last_seen_ms` (`config()`'s own `touch_last_seen()` call).

**Expected Result**
The Twin's "current" row is always an accurate, live-computed reflection
of the device's most recent legitimate activity — never a value that
requires a subsequent push to "notice" the device went offline.

**Failure Criteria**
Stale/cached health display; a quarantined record incorrectly updating
`last_push_totalizer`; `last_seen_ms` not updating on a config-only poll.

**Evidence Required**
`/admin/devices` dashboard screenshots/DB query output at each step,
before/after values for all five Twin fields.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE + BACKEND.**

---

## 11. Registry

**Preconditions**
§2 (backend) running; a device identity (real or synthetic `device_id`)
to provision against.

**Test Procedure**
1. `POST /admin/devices/provision` with `{"device_id": "...",
   "asset_label": "..."}`. Confirm a fresh `api_key` is returned **once**
   in this response and never again by any other endpoint; confirm a
   `logical_device_id` is allocated.
2. Repeat the exact same call for the same `device_id`. Confirm the
   `logical_device_id` returned is **identical** to step 1 (allocated
   exactly once per unit, `provision_device()`'s own idempotency logic) —
   not a second, wasted counter value.
3. `POST /admin/devices/<id>/revoke-key`. Confirm a subsequent device
   request using the old key returns `401`.
4. `POST /admin/devices/<id>/rotate-key`. Confirm a **new** key is
   returned, the old key (if not already revoked) stops working, and the
   new key works immediately.
5. `GET /admin/events` and `GET /admin/devices/<id>/events`. Confirm
   `DEVICE_PROVISIONED`, `KEY_REVOKED`, `KEY_ROTATED` events are all
   present with correct `detail_json`.
6. Confirm the raw `api_key` value never appears in `/admin/events`'
   output or in any server log (`hash_api_key()`'s "never store plaintext"
   contract — verify it also means "never *log* plaintext").

**Expected Result**
Key issuance/revocation/rotation all behave exactly as documented; the
Logical Device ID is a stable, permanent-per-unit identifier across
repeated provisioning calls; a full audit trail exists in
`device_events`.

**Failure Criteria**
A second `logical_device_id` issued for an already-provisioned
`device_id`; a revoked key still accepted by any device-facing endpoint;
the raw API key appearing anywhere outside the single provision/rotate
response that mints it.

**Evidence Required**
HTTP transcripts for every step, `device_events` table dump, confirmation
(via source review + log inspection) that no code path logs a raw key.

**Pass/Fail Status:** **NOT EXECUTED — PENDING PYTHON INTERPRETER.**

---

## 12. OTA

**Preconditions**
A running device on `FW_VERSION 1.0.0`; a second build with a bumped
`FW_VERSION` (e.g. `1.0.1`); `server/firmware/manifest.json` +
the new `.bin` staged (`server.py`'s documented manual-file OTA
mechanism); OTA-capable partition scheme already flashed (§1).

**Test Procedure**
1. Build the new version (`pio run -e release` after bumping
   `FW_VERSION`), place the `.bin` in `server/firmware/`, write
   `manifest.json` (`{"version":"1.0.1","url":"http://<host>:8000/firmware/<file>.bin"}`).
2. Wait for the device's next `OTA_POLL_MS` cycle (5 minutes) or restart
   it to poll immediately. Confirm `Ota::poll()` detects the version
   mismatch and calls `doUpdate_()`.
3. Observe `[OTA] update offered: 1.0.1 (running 1.0.0)` in Serial logs,
   then the `HTTPUpdate` download, then an automatic reboot into the new
   image.
4. On the new boot, confirm `ota.state()` reports `pending_verify`
   (`/api/v1/status`'s `ota.state`) until `confirmHealthyBoot()` runs
   (after WiFi + one successful server contact), then transitions to
   `confirmed`.
5. Confirm `/api/v1/info`'s `fw_version` now reads `1.0.1`.
6. **Rollback test:** repeat with a deliberately broken image (e.g. one
   that panics immediately after boot, never reaching
   `confirmHealthyBoot()`). Confirm the ESP-IDF bootloader automatically
   reverts to the last-known-good partition on the next boot, with **no
   operator intervention** — this is the core safety property OTA exists
   to guarantee.
7. Confirm the `.bin` transfer itself uses `WiFiClientSecure` with the
   pinned CA when `server_url` is `https://` (`ota.h`'s `doUpdate_()`
   scheme dispatch, `ADR-005`) — test with an `https://` bench setup if a
   valid cert is available; document if this specific sub-check is
   deferred to a later phase for lack of one.

**Expected Result**
Version upgrade completes cleanly with a single automatic reboot; a
broken image is automatically rolled back with zero data loss and zero
manual recovery step.

**Failure Criteria**
Update loop (repeatedly re-downloading the same offered version); failure
to roll back a genuinely broken image (a **Critical** finding — this is
the one failure mode that can brick a fielded device); `fw_version` not
reflecting the new version after a successful update; the manifest
version-comparison logic offering an update when `ver == FW_VERSION`.

**Evidence Required**
Full Serial Monitor capture of both the successful-upgrade and the
rollback scenario, before/after `/api/v1/info`/`/api/v1/status` snapshots.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE + BACKEND.**

**Disclosed, out-of-scope gap (do not silently validate around this):**
Secure Boot V2 + Flash Encryption (the second of `ota.h`'s own two stated
field-deployment requirements) remain **not implemented** — a one-way
eFuse burn requiring real hardware, explicitly deferred since DM-Phase 5
(`Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md`). This plan
validates the HTTPS/CA-pinning half of OTA security only; a tampered
binary at a compromised URL is not yet rejected at the bootloader level.
This is a known, disclosed limitation, not something RE-9 introduces or
is scoped to fix (RE-9's own "no new features/refactors" constraint).

---

## 13. Factory Provisioning

**Preconditions**
A unit flashed with the `factory` environment
(`pio run -e factory`, `-DFACTORY_TEST_BUILD=1`), joined to a factory-floor
WiFi network in normal **station** mode (not AP mode); Device Manager's
Factory Test view; a backend reachable to call
`POST /admin/devices/provision` first (to obtain the `api_key` +
`logical_device_id` this workflow writes to the device).

**Test Procedure**
1. Confirm (via Serial Monitor only — explicitly out of the desktop app's
   own visibility, per `factoryTest.js`'s own documented scope note) the
   firmware's Serial-only self-test sequence (SD write-read-CRC, WiFi
   association, PCNT pulse-simulator check, `ADR-008`) reports PASS.
2. In the Factory Test view, run the network-visible checks — confirm
   `hardware_id`, model/firmware, WiFi, SD, health, OTA state, and API key
   status all display correctly, sourced from `GET /api/v1/info` +
   `/status` + `/health`.
3. Call the backend's `POST /admin/devices/provision` to obtain a real
   `api_key` + `logical_device_id`.
4. From the Factory Test view, `POST /api/v1/factory/provision` with that
   `logical_device_id` and `api_key`. Confirm `200` with both values
   echoed back and `api_key_status: "configured"`.
5. Re-submit the **same** `logical_device_id` again. Confirm it succeeds
   idempotently (`200`, unchanged) — not an error.
6. Submit a **different** `logical_device_id` for the same device.
   Confirm `409 {"error":{"code":"LOGICAL_ID_ALREADY_ASSIGNED", ...}}`,
   and specifically confirm a **simultaneously submitted, valid `api_key`
   in that same request still gets written** despite the ID conflict
   (the documented audit fix in `local_api.h`'s `handleFactoryProvision_()`
   — re-verify it holds, not just that the code comment claims it).
7. **Negative/security test (Critical):** on a normal `esp32dev` or
   `release` build (not `factory`), attempt
   `POST /api/v1/factory/provision`. Confirm `404` — the route must not
   exist at all in a non-factory image (compile-time absence,
   `FACTORY_TEST_BUILD` gate in `local_api.h`/`config.h`). Optionally
   confirm via binary inspection (`strings firmware.bin | grep -i
   "factory/provision"` on the `release` build returns nothing).

**Expected Result**
A factory-floor unit is fully commissioned (Logical Device ID + unique API
key) via the desktop app with no serial cable involved for this half of
the flow; the write-once/independent-field semantics hold exactly as
documented; the route is provably absent from any customer-shippable
build.

**Failure Criteria**
The factory route reachable on a `release`-build device (**Critical** —
direct violation of `ADR-008`'s frozen "must never ship to a customer"
requirement); a Logical Device ID silently overwritten; an ID conflict
incorrectly blocking an otherwise-valid `api_key` write.

**Evidence Required**
Serial self-test log, Factory Test view screenshots for each step, HTTP
transcripts for steps 3–7, the `404` confirmation on a non-factory build.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE.**

---

## 14. QR Workflow

**Preconditions**
§13 completed for a unit (a real `logical_device_id` assigned); a
smartphone or QR-scanning app to independently verify decoded content.

**Test Procedure**
1. Immediately after successful provisioning in the Factory Test view,
   trigger QR label generation (`qrGenerator.js`).
2. Confirm the QR image renders on screen — specifically confirm this
   does **not** trip any CSP restriction (`img-src` must permit `data:`,
   the DM-Phase 6 audit fix documented in `CHANGELOG.md`).
3. Scan the rendered QR with an independent device (phone). Confirm the
   decoded text exactly matches the displayed Logical Device ID
   (`COV-NNNNNN` format, `ADR-018`).
4. Generate labels for two different devices and confirm the two QR
   images are visually distinct and each decodes to its own correct ID.
5. Use the view's print/export action and confirm a print dialog/output
   is produced correctly (label legible at expected print size).

**Expected Result**
Every generated QR code decodes back to exactly the Logical Device ID
shown in the UI, with no CSP-driven rendering failure.

**Failure Criteria**
QR fails to render (CSP block); decoded value doesn't match the displayed
ID; two different devices produce identical QR output; print output is
illegible or fails silently.

**Evidence Required**
Screenshot of the rendered QR, phone-scan decoded-value screenshot/log,
print preview or printed label photo.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE (depends on §13).**

---

## 15. Release Upgrade

**Preconditions**
A previously installed Device Manager build (§6) with at least one
discovered/known device in `deviceStore.json`; a newer release published
(§5) with a higher version number.

**Test Procedure**
1. Note the current `deviceStore.json` contents (known devices, any saved
   settings) before upgrading.
2. Download and run the new `covio-device-manager-setup-vX.Y.Z.exe` over
   the existing installation (do not uninstall first).
3. Launch the app and confirm the version displayed matches the new
   release tag exactly (validates RE-7's `npm version
   ${release_version} --no-git-tag-version` stamping step actually
   produced a correctly-versioned artifact, not just a correctly-named
   file).
4. Confirm `deviceStore.json`'s previously known devices/settings are
   still present and correct after the upgrade.
5. Separately, exercise the **firmware** half of an upgrade: a fielded
   device running the previous release's `FW_VERSION`, pointed at a
   backend serving the new release's manifest — confirm it OTA-upgrades
   cleanly (this is §12's own procedure, re-run here specifically using
   the actual published release artifact rather than an ad hoc local
   build, to validate the full "tag push → GitHub Release → field device"
   path end-to-end).

**Expected Result**
The desktop app upgrades in place with zero data loss and a correctly
reported version; a fielded device upgrades via OTA using the exact
artifact GitHub published, not a hand-built substitute.

**Failure Criteria**
`deviceStore.json` reset or corrupted by the upgrade; installer refuses to
overwrite the existing install; displayed version does not match the
release tag (a version-stamping regression); OTA using the published
artifact fails where an ad hoc local build (§12) succeeded, which would
indicate a release-packaging-specific defect rather than a firmware logic
one.

**Evidence Required**
Before/after `deviceStore.json` diffs, installed-version screenshot,
Serial Monitor OTA log using the actual downloaded release `.bin`.

**Pass/Fail Status:** **NOT EXECUTED — BLOCKED ON §5/§6/§12.**

**Disclosed, out-of-scope gap:** the desktop app has **no auto-update
mechanism** (`electron-updater` is not a dependency — confirmed absent
from `tools/device-manager/package.json`). Every upgrade today is a
manual re-download-and-reinstall. This is an existing scope boundary, not
a defect RE-9 is positioned to fix (RE-9's own "no new features"
constraint) — recorded here so it is not mistaken for an untested feature.

---

## 16. Failure Recovery

**Preconditions**
A provisioned, actively-telemetry-pushing device (§9 baseline established)
and the ability to physically interrupt power/SD/WiFi.

**Test Procedure — six induced-failure scenarios**

a. **Power-cut during totalizer write.** Interrupt power repeatedly at
   random points, including mid-write. On each reboot, confirm the
   dual-slot CRC32 checkpoint (`totalizer.h`) recovers the last valid
   total with no loss and no double-count (compare `tot.total()` before
   the cut against after recovery, accounting only for pulses that
   genuinely occurred).

b. **Server-down resilience.** Stop `server.py` while the device is
   running. Confirm no crash; SD queue backlog grows; `QUEUE_HIGH` (>1000)
   then `QUEUE_CRITICAL` (>10000) alarms fire at the documented
   thresholds (`diagnostics.h`); `health_state` becomes `"offline"` after
   5 minutes (`300000ms`) with no contact. Restart the server and confirm
   full, in-order, zero-duplicate catch-up (cross-reference §9 step 5).

c. **SD card removed mid-operation.** Physically remove the SD card while
   the device is running. Confirm `SD_REMOVED` (CRITICAL) fires via
   `/api/v1/health`; confirm no crash or hang. Note: full graceful
   degradation beyond detection is explicitly deferred to `ADR-004` (not
   yet implemented) — this test validates detection/reporting only, and
   that absence of full degradation handling is a disclosed gap, not a
   surprise finding.

d. **WiFi drop/reconnect.** Disable the device's WiFi (e.g. power-cycle
   the access point) mid-session. Confirm `sync.h`'s reconnect/backoff
   (`WIFI_RETRY_MS`, 10s base) recovers the connection automatically with
   no operator action once the AP returns.

e. **Bad OTA image rollback.** Cross-referenced to §12's own rollback
   procedure — re-run here as part of the same failure-recovery sweep
   rather than duplicated.

f. **Factory-reset recovery.** Issue the serial `factory` command
   (`provision.h`). Confirm NVS is wiped, the device reboots, re-seeds
   from `config.h`'s defaults (including the placeholder
   `DEFAULT_WIFI_SSID`/`DEFAULT_SERVER_URL`), and — since those defaults
   are not real credentials — correctly falls through to AP-fallback
   provisioning mode (§8) on the next boot.

**Expected Result**
Every scenario above self-heals without physical intervention beyond what
the scenario itself required (e.g. physically reinserting a removed SD
card) — matching the "reliability contract" already documented in
`README.md` (ACK-gated prune, idempotent inserts, cumulative ack, torn-
write-safe checkpoints, globally monotonic `seq`).

**Failure Criteria**
Any scenario resulting in data loss, data duplication, a crash loop, or a
state requiring manual NVS/SD intervention to recover from (beyond
scenario (f)'s own intentional factory reset).

**Evidence Required**
Serial Monitor logs spanning each induced failure and recovery, `/api/v1/status`
and `/api/v1/metrics` snapshots immediately before and after each event,
server-side `records` row counts confirming exact expected counts (no
gaps, no duplicates) after each network-related scenario.

**Pass/Fail Status:** **NOT EXECUTED — PENDING HARDWARE.**

---

# Master Validation Checklist (execution order)

Execute top to bottom. Each step names its governing subsystem section
above. A step should not begin until every step above it has a recorded
**PASS** — this mirrors the project's own established "one phase at a
time, audited before proceeding" discipline (`CONTRIBUTING.md`'s DM/RE
governance model), applied here to hardware/production validation instead
of implementation phases.

```
Repository
  ↓
GitHub Actions
  ↓
GitHub Release
  ↓
Download Installer
  ↓
Install Device Manager
  ↓
Power ON ESP32
  ↓
Automatic Discovery
  ↓
Connect Device
  ↓
Wi-Fi Configuration
  ↓
Cloud Registration
  ↓
Telemetry
  ↓
OTA
  ↓
Recovery Tests
```

| # | Checklist item | Subsystem section | Pass/Fail |
|---|---|---|---|
| 1 | Repository — clean clone builds per `SETUP.md`; `.pio`/`node_modules`/`__pycache__` etc. absent from a fresh checkout (`.gitignore`) | §1, §2, §3 (build-only) | NOT EXECUTED |
| 2 | GitHub Actions — push/PR triggers `ci.yml`; all 5 jobs pass, including the first-ever real firmware compile | §4 | NOT EXECUTED — blocked, no remote |
| 3 | GitHub Release — tag push triggers `release.yml`; all 4 jobs pass; Release page shows all 8 correctly-named assets | §5 | NOT EXECUTED — blocked on #2 |
| 4 | Download Installer — download NSIS installer + portable exe from the Release; verify both `.sha256` checksums | §6 | NOT EXECUTED — blocked on #3 |
| 5 | Install Device Manager — run the installer; app launches; version matches the release tag | §6, §3 | NOT EXECUTED — blocked on #4 |
| 6 | Power ON ESP32 — flash `esp32dev` build (or use a pre-flashed unit); device boots to a stable idle state | §1 | NOT EXECUTED — pending hardware |
| 7 | Automatic Discovery — device appears in Device Manager's Discovery view via `_covio._tcp.local` mDNS within a few seconds | §7 | NOT EXECUTED — blocked on #5, #6 |
| 8 | Connect Device — app reads `GET /api/v1/info`/`/status` from the discovered device without error | §7, §3 | NOT EXECUTED — blocked on #7 |
| 9 | Wi-Fi Configuration — Provisioning wizard's 4 steps (detect/connect/configure/verify) complete against an unconfigured unit; wrong-password retry path also exercised | §8 | NOT EXECUTED — blocked on #5, #6 |
| 10 | Cloud Registration — backend `POST /admin/devices/provision` issues a unique key + Logical Device ID; device successfully authenticates its first push with it | §2, §11 | NOT EXECUTED — blocked on #2 (backend), #9 |
| 11 | Telemetry — sustained push/ack cycle observed; outage-and-recovery zero-loss catch-up confirmed; Device Twin (`/admin/devices`) reflects live state | §9, §10 | NOT EXECUTED — blocked on #10 |
| 12 | OTA — device upgrades from the actual published release `.bin`; rollback-on-bad-image confirmed separately | §12, §15 (step 5) | NOT EXECUTED — blocked on #3, #11 |
| 13 | Recovery Tests — all six induced-failure scenarios (power-cut, server-down, SD removal, WiFi drop, bad-OTA rollback, factory-reset) pass | §16 | NOT EXECUTED — blocked on #11, #12 |

## Parallel/prerequisite tracks not on the customer-facing critical path above

The 16 required subsystems include three that happen **before** a unit
ever reaches the "Power ON ESP32" step in a customer's or field
technician's hands, or as a distinct pass after the main flow completes
once. They are listed here rather than forced into the linear diagram
above, since doing so would misrepresent when they actually occur:

| Track | When it runs | Subsystem section |
|---|---|---|
| Manufacturing / Factory Provisioning | Before a unit ships — on the factory floor, using the `factory` build, immediately after backend registration (§11) is available | §13 Factory Provisioning |
| QR Label Generation | Immediately after Factory Provisioning, same session | §14 QR Workflow |
| Release Upgrade (second pass) | After the full checklist above has passed once for release N, repeated for release N+1 against an already-installed/fielded system | §15 Release Upgrade |

---

## Overall Pass/Fail Summary

| Subsystem | Status |
|---|---|
| 1. Firmware | NOT EXECUTED |
| 2. Backend | NOT EXECUTED |
| 3. Desktop Device Manager | NOT EXECUTED (automated unit/lint tests already pass; interactive/hardware portion not executed) |
| 4. GitHub Actions | NOT EXECUTED |
| 5. GitHub Releases | NOT EXECUTED |
| 6. Windows Installer | NOT EXECUTED |
| 7. Device Discovery | NOT EXECUTED |
| 8. Wi-Fi Provisioning | NOT EXECUTED |
| 9. Telemetry | NOT EXECUTED |
| 10. Device Twin | NOT EXECUTED |
| 11. Registry | NOT EXECUTED |
| 12. OTA | NOT EXECUTED |
| 13. Factory Provisioning | NOT EXECUTED |
| 14. QR Workflow | NOT EXECUTED |
| 15. Release Upgrade | NOT EXECUTED |
| 16. Failure Recovery | NOT EXECUTED |

**Overall RE-9 status: PLAN COMPLETE, ZERO SECTIONS EXECUTED.** Execution
requires, at minimum: real ESP32 hardware + SD card + pulse source, a
configured GitHub remote with Actions enabled, and a Windows machine
capable of local packaging or reliance on a GitHub-hosted `windows-latest`
runner. None of these exist in the environment this plan was authored in.

## Cross-reference index

| Topic | Authoritative document |
|---|---|
| Toolchain setup to execute any section above | `SETUP.md` |
| Exact pinned/verified versions | `VERSIONS.md` |
| CI job architecture | `Docs/CI_WORKFLOW.md` |
| Firmware build environments | `Docs/FIRMWARE_BUILD.md` |
| Desktop packaging | `Docs/DESKTOP_PACKAGING.md` |
| Release pipeline architecture | `Docs/RELEASE_AUTOMATION.md` |
| Artifact naming | `Docs/RELEASE_VERSIONING.md` |
| Local API contract (frozen) | `Docs/Covio_Device_Manager_Live_Readiness_Plan.md` §13 Appendix A |
| Architecture decisions (ADR-005 security, ADR-008 factory self-test, ADR-017/018 provisioning + Logical Device ID) | `Docs/Firmware Detailed Architecture Decision Record (ADR).md` |
| Secure Boot / Flash Encryption (deferred, hardware-dependent) | `Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md` |
| Bench bring-up / field-test procedure | `IMPLEMENTATION_AND_TESTING.md` |

RE-9 validation plan complete. Awaiting hardware execution.
