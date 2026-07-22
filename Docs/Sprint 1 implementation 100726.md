# Covio Platform — Pilot Execution Backlog

**Status:** Living execution document. Created 2026-07-10.
**Purpose:** Day-by-day developer backlog converting the approved, frozen Pilot roadmap into executable tasks. This is the single source of truth for "what to do next" — any contributor or agent picking up this project should start here, find the first unchecked task in the Master Development Checklist, confirm its prerequisites are ticked, and execute it. Do not skip ahead and do not redo a ticked task.

**Scope boundary (do not expand):** This backlog delivers Pilot only (10–100 devices). It deliberately excludes work already classified as deferred at architecture freeze: admin authentication, staged/canary OTA rollout, multi-tenant architecture, device lifecycle state model, device replacement (RMA) workflow, and a polished web dashboard. Do not add tasks for these here — they belong to a future Production-scope backlog.

**How to use this document:**
1. Find the first unchecked box in the **Master Development Checklist**.
2. Confirm every task listed under its "Prerequisites" is already checked.
3. Execute the task exactly as scoped, using the linked source doc where given (`SETUP.md`, `IMPLEMENTATION_AND_TESTING.md`, `ARCHITECTURE.md`, `README.md`) for exact commands.
4. Confirm the "Completion Criteria" before checking the box.
5. If a task is blocked (prerequisite hardware/access not available), skip to a task on a different track (see Parallel Tasks) rather than idling.

**Track legend:** 🔴 Critical Path (gates Pilot release — do not delay) · 🟢 Parallel (can run anytime its own prerequisites are met, does not block the critical path)

**Resource legend:** 💻 Software-only (your current machine, no special hardware) · 🌐 Internet · 🔩 ESP32 hardware · 🖥️ Backend (running `server.py`) · 📱 Device Manager (built Electron app) · ☁️ VPS

---

# Sprint 1 — Foundation (Repository, Toolchains, Backend Bring-Up)

**Everything in this sprint is doable today with your current hardware.** No ESP32 board is required for any Sprint 1 task.

## Day 1 — Repository & Git Foundation

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S1.D1.T1 | Install/confirm Git for Windows | None | `git --version` succeeds | Version string returned | 30 min | 🟢 | 💻 |
| S1.D1.T2 | Create the GitHub remote repository | S1.D1.T1 | Empty/initial repo exists on GitHub | Repo URL captured | 30 min | 🔴 | 🌐 |
| S1.D1.T3 | Push existing local working tree to `main` | S1.D1.T2 | All current files committed and pushed | `git log` on GitHub shows the commit(s); local `git status` clean | 30 min | 🔴 | 🌐 |
| S1.D1.T4 | Enable branch protection on `main` (PR-only, per `CONTRIBUTING.md`) | S1.D1.T3 | Protection rule active | A direct push to `main` is rejected in a test attempt | 30 min | 🔴 | 🌐 |

## Day 2 — Firmware Toolchain & First Compile

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S1.D2.T1 | Install Python 3.11 | None | `python --version` succeeds | Reports 3.11.x | 30 min | 🔴 | 💻 |
| S1.D2.T2 | Install PlatformIO Core 6.1.15 (`pip install platformio==6.1.15`, per `SETUP.md` §3) | S1.D2.T1 | `pio --version` succeeds | Version output returned | 30 min | 🔴 | 💻🌐 |
| S1.D2.T3 | First compile: `pio run -e esp32dev` | S1.D2.T2 | Build log | `.pio/build/esp32dev/firmware.bin` produced, or error log captured | 1 hr | 🔴 | 💻 |
| S1.D2.T4 | First compile: `pio run -e release` and `pio run -e factory` | S1.D2.T3 | Two more build logs | Both environments compile, or errors captured | 30 min | 🔴 | 💻 |
| S1.D2.T5 | Diagnose/fix any compile errors against the `espressif32@6.5.0` pin (`Docs/FIRMWARE_BUILD.md` procedure) | S1.D2.T3, T4 (only if either failed) | Clean compile, all 3 environments | All three `pio run -e ...` exit 0 | 2 hr (contingency; 0 if clean) | 🔴 | 💻 |

## Day 3 — Backend First Run

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S1.D3.T1 | `pip install flask` | S1.D2.T1 | Flask importable | `python -c "import flask"` succeeds | 30 min | 🔴 | 💻 |
| S1.D3.T2 | Run `python server.py` from `server/` for the first time | S1.D3.T1 | Terminal prints "Covio bench server on http://0.0.0.0:8000"; `server/covio.db` created | `http://localhost:8000/` loads in a browser | 30 min | 🔴 | 🖥️ |
| S1.D3.T3 | Confirm `/admin/devices` renders on the fresh DB | S1.D3.T2 | Empty registry table renders | Page loads with no error, empty table | 30 min | 🟢 | 🖥️ |
| S1.D3.T4 | Run native test suite: `python -m unittest discover -s test/native -v` | S1.D3.T1 | Full test run log | All tests reported pass/fail | 1 hr | 🔴 | 💻 |
| S1.D3.T5 | Triage and fix any failing native tests | S1.D3.T4 | Green test run | 100% of `test/native/*.py` passing | 2 hr (contingency) | 🔴 | 💻 |

## Day 4 — CI Validation, Device Manager Local Verification, Sprint Close

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S1.D4.T1 | Push a trivial commit; observe `ci.yml` run all 4 jobs | S1.D1.T4, S1.D2.T5, S1.D3.T5 | GitHub Actions run log | firmware/backend/desktop/code-quality jobs all report a result | 30 min (+ passive CI wait) | 🔴 | 🌐 |
| S1.D4.T2 | Fix any CI-only failures not caught locally | S1.D4.T1 | Green CI run | All 4 jobs pass on `main` | 1 hr (contingency) | 🔴 | 🌐 |
| S1.D4.T3 | Install Node.js 24.x + npm | None | `node --version` / `npm --version` succeed | Both return version strings | 30 min | 🟢 | 💻 |
| S1.D4.T4 | `npm ci` and `npm test` in `tools/device-manager/` | S1.D4.T3 | 44/44 tests passing | Test run confirms pass count matches `SETUP.md`'s recorded baseline | 30 min | 🟢 | 💻 |
| S1.D4.T5 | Dry-run a pre-release tag (`git tag v0.1.0-rc.1 && git push origin v0.1.0-rc.1`) to validate `desktop-package.yml`/`release.yml` | S1.D1.T4, S1.D4.T4 | Release pipeline run completes | `determine-version → verify-and-build-firmware → package-desktop → publish` all succeed | 30 min | 🟢 | 🌐 |
| S1.D4.T6 | Sprint 1 review against Sprint 1 Definition of Done | All above | — | See **Definition of Done — Sprint 1** below | 30 min | 🔴 | — |

---

# Sprint 2 — Hardware Bring-Up

**Requires a physical ESP32 dev board + the wiring described in `IMPLEMENTATION_AND_TESTING.md` Part 0.** Nothing in this sprint can start until you have the board in hand.

## Day 1 — Bench Wiring & First Boot

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S2.D1.T1 | Wire ESP32 + SD module (CS=GPIO4) + PC817 opto (OUT→GPIO27) + sim jumper (GPIO25→GPIO27) per `IMPLEMENTATION_AND_TESTING.md` Part 0/2.4 | Physical hardware in hand | Bench rig wired | Visual continuity check against the wiring table | 1 hr | 🔴 | 🔩 |
| S2.D1.T2 | Edit `config.h`: bench WiFi SSID/pass, `DEFAULT_SERVER_URL` = your laptop IP:8000, `SIM_PULSES 1` | S1.D3.T2 (know your laptop's IP) | Edited `config.h` | Values match your actual bench network | 30 min | 🔴 | 💻 |
| S2.D1.T3 | Flash `esp32dev` build over USB (buck off VIN) | S1.D2.T5, S2.D1.T1, S2.D1.T2 | Flash succeeds | `pio run -e esp32dev -t upload` exits 0 | 1 hr | 🔴 | 🔩 |
| S2.D1.T4 | First Boot Checklist (Gate F1) — confirm serial output | S2.D1.T3 | Serial log | Boot banner, `device_id`/`boot_id`, SD ready, sim pulses, `[BOOT] entering main loop` all appear | 30 min | 🔴 | 🔩 |

## Day 2 — Bench Gates T1–T3

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S2.D2.T1 | Start `server.py`, confirm device's WiFi can reach it | S1.D3.T2 | Server reachable from bench WiFi | Server terminal shows incoming requests once device boots | 30 min | 🔴 | 🖥️ |
| S2.D2.T2 | Gate T1 — pulse counting | S2.D1.T4, S2.D2.T1 | Dashboard shows climbing raw pulses/litres | ~10 pulses/sec climbing, litres = pulses ÷ 1000 | 30 min | 🔴 | 🔩🖥️ |
| S2.D2.T3 | Gate T2 — K-factor hot swap | S2.D2.T2 | Litres recompute instantly; device logs new calibration within 60s | `show` on console reports new `calib: v2 K=...` | 1 hr | 🔴 | 🔩🖥️ |
| S2.D2.T4 | Gate T3 — offline outage zero-loss catch-up (WiFi off 5–10 min, back on) | S2.D2.T2 | Backlog drains with no gaps on reconnect | Records count catches up to ≈1/sec of runtime, ack advances contiguously | Half day | 🔴 | 🔩🖥️ |

## Day 3 — Bench Gates T4–T6 + Provisioning

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S2.D3.T1 | Gate T4 — power-cut totalizer survival (yank USB, replug) | S2.D2.T2 | `boot_id` increments; totalizer recovers within ~1-2s of pre-cut value | Serial shows `[TOT] recovered total=<N>` close to last known value | 1 hr | 🔴 | 🔩 |
| S2.D3.T2 | Gate T5 — server-down resilience + idempotency (Ctrl+C server, restart after 2–3 min) | S2.D2.T2, S2.D2.T1 | No duplicate records after restart | Records = max seq exactly | 1 hr | 🔴 | 🔩🖥️ |
| S2.D3.T3 | Gate T6 — provisioning console round-trip (`set url` to a bad IP, then back) | S2.D2.T2 | Device re-points with no reflash | Backlog from the "broken" window syncs on recovery | 1 hr | 🔴 | 🔩 |
| S2.D3.T4 | SoftAP captive-portal verification — trigger AP mode (`provision` console command or disconnect WiFi), join `Covio-Setup-<id>`, submit credentials via the browser form | S2.D1.T4 | Device saves config and reboots onto real WiFi | Captive portal's "Saved... Rebooting" page appears, device reconnects | 1 hr | 🔴 | 🔩 |

## Day 4 — OTA Gate + Discovery

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S2.D4.T1 | Bump `FW_VERSION` in `config.h`, export compiled binary (Sketch → Export Compiled Binary or `pio run`) | S1.D2.T5 | New `.bin` file | File exists, version string visibly different | 30 min | 🔴 | 💻 |
| S2.D4.T2 | Host `.bin` + `manifest.json` under `server/firmware/` | S2.D4.T1, S1.D3.T2 | Manifest reachable at `/api/iot/flow/ota/manifest` | GET returns the new version/url JSON | 30 min | 🔴 | 🖥️ |
| S2.D4.T3 | Gate F-OTA — observe update download, reboot, version change, trial/rollback confirmation | S2.D4.T2 | New firmware running, data intact | New version banner on boot, totalizer/queue survived, `[OTA] new image confirmed valid` (or documented as unverifiable per core version) | 1 hr | 🔴 | 🔩🖥️ |
| S2.D4.T4 | mDNS discovery verification — confirm the device resolves as `covio-<hex>.local` on the bench LAN | S2.D4.T3 | Device visible via mDNS query | `ping covio-<hex>.local` or equivalent resolves | 30 min | 🟢 | 🔩 |

## Day 5 — Device Manager Real-Device Integration + Packaging

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S2.D5.T1 | Run Device Manager (`npm start` in `tools/device-manager/`) — Discovery view against the real device | S1.D4.T4, S2.D4.T4 | Device appears in Discovery list | Device row shows hostname/IP/fw version | 1 hr | 🔴 | 📱🔩 |
| S2.D5.T2 | Run the Provisioning wizard end-to-end against the real device | S2.D5.T1, S2.D3.T4 | Device re-provisioned via the app instead of the manual portal | Verify step confirms device reappears via mDNS within 90s | 1 hr | 🔴 | 📱🔩 |
| S2.D5.T3 | Run the Live Monitor view against the real device | S2.D5.T1 | WiFi/queue/OTA/last-sync fields populate | Fields update every 5s, no "Unreachable" state | 30 min | 🟢 | 📱🔩 |
| S2.D5.T4 | Factory-provision flow end-to-end — flash a `factory` build, then Factory Test view → `POST /admin/devices/provision` → `POST /api/v1/factory/provision` | S1.D2.T5 (factory env), S2.D1.T1, S1.D3.T2 | Device gets a real `logical_device_id` + unique API key | `/api/v1/info` on the device shows the assigned `logical_device_id`; server's `/admin/devices` shows the same ID | 1 hr | 🔴 | 📱🔩🖥️ |
| S2.D5.T5 | Package Windows installer (`npm run dist`) | S1.D4.T4 | Installer + portable `.exe` in `tools/device-manager/dist/` | Installer runs on a clean machine | 1 hr | 🟢 | 📱 |
| S2.D5.T6 | Sprint 2 review against Sprint 2 Definition of Done | All above | — | See **Definition of Done — Sprint 2** below | 30 min | 🔴 | — |

---

# Sprint 3 — Field Deployment (VPS / Internet)

**Day 1 of this sprint has no ESP32 dependency and can be started early/in parallel with Sprint 2 if you want to save calendar time.**

## Day 1 — VPS & Hosted Backend

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S3.D1.T1 | Provision a small VPS | None | Reachable VPS with SSH access | `ssh` login succeeds | 1 hr | 🔴 | ☁️🌐 |
| S3.D1.T2 | Install Python + Flask + gunicorn on the VPS, copy `server/` there | S3.D1.T1 | `server.py` present on VPS | `python -c "import flask, gunicorn"` succeeds | 1 hr | 🔴 | ☁️ |
| S3.D1.T3 | Run `gunicorn -b 127.0.0.1:8000 server:app` | S3.D1.T2 | Server process running under gunicorn | `curl 127.0.0.1:8000` responds locally on the VPS | 30 min | 🔴 | ☁️ |
| S3.D1.T4 | Install + configure Caddy (or nginx) reverse proxy with a domain + Let's Encrypt TLS | S3.D1.T3, a domain pointed at the VPS | HTTPS-terminated reverse proxy | `https://yourdomain.com/` loads the K-factor dashboard | Half day | 🔴 | ☁️🌐 |
| S3.D1.T5 | Verify `https://yourdomain.com/api/iot/flow/config` from mobile data (off-WiFi) | S3.D1.T4 | K-factor JSON returned | Response received over a non-LAN network path | 30 min | 🔴 | 🌐 |

## Day 2 — Firmware HTTPS Switch

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S3.D2.T1 | Embed ISRG Root X1 CA cert into `sync.h` and `ota.h` (`PROGMEM` constant, per README's "OTA — read before field deploy") | S3.D1.T4 | `ROOT_CA` constant added | Compiles without error | 1 hr | 🔴 | 💻 |
| S3.D2.T2 | Replace `http.begin(url)` → `http.begin(url, ROOT_CA)` in `sync.h`/`ota.h`; use `WiFiClientSecure` + `setCACert` in `ota.h`'s `doUpdate_` | S3.D2.T1 | HTTPS-only firmware build | `grep -r "setInsecure" .` returns nothing | 1 hr | 🔴 | 💻 |
| S3.D2.T3 | Recompile and reflash with HTTPS enabled | S3.D2.T2, S2.D1.T3 | New build flashed | Flash succeeds, boots normally | 1 hr | 🔴 | 🔩 |
| S3.D2.T4 | Point device at the hosted backend via serial console (`set url https://yourdomain.com` → `reboot`) while still on the bench | S3.D2.T3, S3.D1.T5 | Device targets the VPS | Console `show` confirms new `server_url` | 30 min | 🔴 | 🔩🌐 |
| S3.D2.T5 | Confirm the device appears/syncs on the hosted `/admin/devices` page | S3.D2.T4 | Device row with recent `last_seen` | Health shows `ok`, not `offline`/`unknown` | 30 min | 🔴 | 🔩☁️🌐 |

## Day 3 — First Real Pilot Device

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S3.D3.T1 | Set `SIM_PULSES 0`, final build (last USB flash), reconnect real opto sensor to GPIO27, remove sim jumper | S3.D2.T5 | Real-sensor build flashed | Device reads real meter pulses at rest = 0 | 1 hr | 🔴 | 🔩 |
| S3.D3.T2 | Factory-provision the first real Pilot unit against the hosted backend (repeat S2.D5.T4 pattern) | S3.D3.T1, S3.D1.T4 | Unit has a real `logical_device_id` + API key on the hosted backend | Confirmed in hosted `/admin/devices` | 1 hr | 🔴 | 📱🔩☁️ |
| S3.D3.T3 | Field-install the unit, induce a known flow volume, calibrate real K-factor (`K = pulses_counted / litres_actually_passed`) | S3.D3.T2 | Real K-factor entered on the hosted dashboard | Litres reading matches the known-volume run | Half day | 🔴 | 🔩☁️ |
| S3.D3.T4 | Re-run outage (T3) and power-cut (T4) gates once on the final build in the field | S3.D3.T3 | Zero data loss confirmed on the real deployment | Same pass criteria as S2.D2.T4 / S2.D3.T1, against the hosted backend | Half day | 🔴 | 🔩☁️ |

## Day 4 — Remote Calibration & Remote OTA Demonstration

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S3.D4.T1 | Edit K-factor remotely via the hosted dashboard | S3.D3.T3 | Device picks up new version within 60s | Console/telemetry shows new `kfactor_version` | 1 hr | 🔴 | ☁️🌐 |
| S3.D4.T2 | Push a firmware update remotely (upload `.bin`, bump `manifest.json` on the VPS) | S3.D1.T4, S3.D3.T1 | Manifest updated | GET manifest shows new version | 1 hr | 🔴 | ☁️ |
| S3.D4.T3 | Confirm the field device self-updates within 5 minutes; archive the `.bin` | S3.D4.T2 | Device running new version | Version banner changed, no manual intervention; `.bin` saved to a known-good archive location | 1 hr | 🔴 | 🔩☁️ |

## Day 5 — Cohort Scale-Out & Pilot Sign-Off

| ID | Task | Prerequisites | Expected Output | Completion Criteria | Estimate | Track | Resource |
|---|---|---|---|---|---|---|---|
| S3.D5.T1 | Repeat provisioning + field-install for the remaining Pilot cohort (10–100 devices total, one at a time — no bulk tooling exists or is required at Pilot) | S3.D3.T2 pattern proven once | Full cohort online | Every unit visible and `ok` on the hosted `/admin/devices` page | Full day (repeatable per device) | 🔴 | 🔩📱☁️ |
| S3.D5.T2 | Walk the **Pilot Release Checklist** (below) end to end | All Sprint 1–3 tasks | Checklist fully ticked | Every item confirmed | 1 hr | 🔴 | — |
| S3.D5.T3 | Pilot sign-off review | S3.D5.T2 | Pilot declared released | Definition of Done for Pilot (below) met | 30 min | 🔴 | — |

---

# Tasks You Can Start Right Now (Current Hardware, No ESP32/VPS Needed)

Every task in **Sprint 1 (Days 1–4)** is executable today on your current Windows machine: Git/GitHub setup, PlatformIO install and first compile, Python/Flask backend first run, native test suite, Node/npm and Device Manager local verification, CI validation.

**Also startable early, in parallel, if you want to save calendar time later:** Sprint 3 Day 1 (VPS provisioning + hosted backend) has no ESP32 dependency — it can be done any time after Sprint 1, in parallel with Sprint 2's hardware bring-up.

**Everything else requires the physical ESP32 board** (Sprint 2 in full, Sprint 3 Days 2–5).

---

# Critical Path

S1.D1.T2→T3→T4 → S1.D2.T1→T2→T3→T4→T5 → S1.D3.T1→T2→T4→T5 → S1.D4.T1→T2→T6 → S2.D1.T1→T2→T3→T4 → S2.D2.T1→T2→T3→T4 → S2.D3.T1→T2→T3→T4 → S2.D4.T1→T2→T3 → S2.D5.T1→T2→T4→T6 → S3.D1.T1→T2→T3→T4→T5 → S3.D2.T1→T2→T3→T4→T5 → S3.D3.T1→T2→T3→T4 → S3.D4.T1→T2→T3 → S3.D5.T1→T2→T3

All 🔴-tagged tasks above sit on this path — delaying any one of them delays Pilot release by the same amount. 🟢-tagged tasks (Git baseline install, Node/Device Manager local test pass, release dry-run, mDNS check, Live Monitor check, installer packaging) can absorb schedule slack without affecting the release date.

---

# Master Development Checklist

## Sprint 1
- [ ] S1.D1.T1 — Install/confirm Git
- [ ] S1.D1.T2 — Create GitHub remote
- [ ] S1.D1.T3 — Push working tree to `main`
- [ ] S1.D1.T4 — Enable branch protection
- [ ] S1.D2.T1 — Install Python 3.11
- [ ] S1.D2.T2 — Install PlatformIO 6.1.15
- [ ] S1.D2.T3 — First compile `esp32dev`
- [ ] S1.D2.T4 — First compile `release` + `factory`
- [ ] S1.D2.T5 — Fix compile errors (if any)
- [ ] S1.D3.T1 — `pip install flask`
- [ ] S1.D3.T2 — First run `server.py`
- [ ] S1.D3.T3 — Confirm `/admin/devices` renders
- [ ] S1.D3.T4 — Run native test suite
- [ ] S1.D3.T5 — Fix failing native tests (if any)
- [ ] S1.D4.T1 — First CI run
- [ ] S1.D4.T2 — Fix CI-only failures (if any)
- [ ] S1.D4.T3 — Install Node.js 24.x
- [ ] S1.D4.T4 — `npm ci` + `npm test` in Device Manager
- [ ] S1.D4.T5 — Dry-run release tag
- [ ] S1.D4.T6 — Sprint 1 DoD review

## Sprint 2
- [ ] S2.D1.T1 — Wire bench hardware
- [ ] S2.D1.T2 — Edit `config.h` for bench
- [ ] S2.D1.T3 — Flash `esp32dev`
- [ ] S2.D1.T4 — Gate F1 (first boot)
- [ ] S2.D2.T1 — Start server, confirm reachable
- [ ] S2.D2.T2 — Gate T1 (pulses)
- [ ] S2.D2.T3 — Gate T2 (K-factor hot swap)
- [ ] S2.D2.T4 — Gate T3 (outage catch-up)
- [ ] S2.D3.T1 — Gate T4 (power-cut survival)
- [ ] S2.D3.T2 — Gate T5 (server-down resilience)
- [ ] S2.D3.T3 — Gate T6 (provisioning round-trip)
- [ ] S2.D3.T4 — SoftAP captive-portal verification
- [ ] S2.D4.T1 — Build v-next binary
- [ ] S2.D4.T2 — Host binary + manifest
- [ ] S2.D4.T3 — Gate F-OTA
- [ ] S2.D4.T4 — mDNS discovery verification
- [ ] S2.D5.T1 — Device Manager Discovery view (real device)
- [ ] S2.D5.T2 — Device Manager Provisioning wizard (real device)
- [ ] S2.D5.T3 — Device Manager Live Monitor (real device)
- [ ] S2.D5.T4 — Factory-provision flow end-to-end
- [ ] S2.D5.T5 — Package Windows installer
- [ ] S2.D5.T6 — Sprint 2 DoD review

## Sprint 3
- [ ] S3.D1.T1 — Provision VPS
- [ ] S3.D1.T2 — Install Python/Flask/gunicorn on VPS
- [ ] S3.D1.T3 — Run gunicorn
- [ ] S3.D1.T4 — Configure reverse proxy + TLS
- [ ] S3.D1.T5 — Verify HTTPS from mobile data
- [ ] S3.D2.T1 — Embed CA cert
- [ ] S3.D2.T2 — Switch to HTTPS calls in firmware
- [ ] S3.D2.T3 — Recompile + reflash HTTPS build
- [ ] S3.D2.T4 — Point device at hosted backend
- [ ] S3.D2.T5 — Confirm device syncs to hosted backend
- [ ] S3.D3.T1 — Real-sensor build (SIM_PULSES 0)
- [ ] S3.D3.T2 — Factory-provision first real Pilot unit
- [ ] S3.D3.T3 — Field-install + real K-factor calibration
- [ ] S3.D3.T4 — Re-run outage/power-cut gates in the field
- [ ] S3.D4.T1 — Remote K-factor calibration demo
- [ ] S3.D4.T2 — Remote OTA push
- [ ] S3.D4.T3 — Confirm remote OTA applied + archive `.bin`
- [ ] S3.D5.T1 — Scale to full Pilot cohort
- [ ] S3.D5.T2 — Walk Pilot Release Checklist
- [ ] S3.D5.T3 — Pilot sign-off review

---

# Pilot Completion Checklist

- [ ] Real remote, protected `main`, green CI (all four jobs)
- [ ] Firmware compiles clean on `esp32dev` + `release` + `factory`
- [ ] Bench gates T1–T6 green on real hardware
- [ ] OTA gate (F-OTA) green, rollback behavior confirmed or explicitly noted unverifiable per core version
- [ ] Backend runs cleanly; native test suite green
- [ ] Backend hosted with HTTPS + pinned CA, reachable off-LAN
- [ ] Firmware built with `SIM_PULSES 0`, HTTPS CA pinned, `OTA_POLL_MS` finalized
- [ ] Device Manager packaged and verified against real device + real hosted backend
- [ ] Factory-provision flow demonstrated end-to-end at least once
- [ ] First real device onboarded and confirmed syncing against the hosted backend
- [ ] Remote K-factor calibration demonstrated live
- [ ] Remote OTA push demonstrated live against the hosted backend
- [ ] Known-good firmware `.bin` archived
- [ ] Full 10–100 device cohort onboarded and reporting

---

# Definition of Done — Per Sprint

**Sprint 1 DoD:** Repository is live with a protected `main`; CI is green across all 4 jobs; firmware compiles clean on all 3 PlatformIO environments; backend runs and all native tests pass; Device Manager's own test suite passes locally (44/44); release pipeline validated via a dry-run tag.

**Sprint 2 DoD:** A real ESP32 is flashed and passes Gate F1; bench gates T1–T6 are all green; the OTA gate (F-OTA) is green; SoftAP provisioning is verified on real hardware; Device Manager is verified against the real device including the factory-provision flow; a Windows installer is packaged.

**Sprint 3 DoD:** Backend is hosted on a VPS with HTTPS; firmware is switched to HTTPS and verified against the hosted backend; the first real Pilot device is onboarded end-to-end with a real K-factor calibration; remote calibration and remote OTA are both demonstrated live; the full 10–100 device Pilot cohort is onboarded; the Pilot Completion Checklist is fully ticked.
