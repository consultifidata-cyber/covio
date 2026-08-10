# Phase 2 — Miki Wire Validation Matrix

**Candidate under validation (frozen):** branch `miki-wire-enterprise-hardening`,
commit `ef6bf16` (firmware source identical since `2559e2e`; later commits are
CI/docs only), working tree clean. Remote backup: pushed to
`consultifidata-cyber/covio` (both this branch and the production snapshot
`miki-wire-production-snapshot-20260810` @ `73d82ff`), no force-push.
**Validation session:** 2026-08-10, Windows bench host, PlatformIO 6.1.15,
host C++ via `python -m ziglang c++` (no g++/MSVC on this machine).

**Hardware availability this session: NONE.** Zero serial/COM devices
enumerated (`pio device list` empty; `SerialPort::GetPortNames()` returned
only LPT1). Every test requiring the physical ESP32-S3-POE-ETH-8DI-8DO +
LJ12A3-4-Z/BX is therefore **BLOCKED**, with its full bench procedure written
below so a technician can execute it verbatim. BLOCKED is never reported as
PASS anywhere in this document.

Status legend: **PASS** (executed, evidence held) · **FAIL** (executed,
failed) · **BLOCKED** (cannot be executed this session — reason given) ·
**IVI** (INSUFFICIENT VERIFIED INFORMATION — a needed parameter/fact is
unverified; no guessing).

Reference build artifacts (SHA-256, built at source state `2559e2e`):

| Env | firmware.bin SHA-256 |
|---|---|
| `esp32dev` (Balaji flag-less) | `72e0f52231e0027c80838aa8580a78171e54e4cee0519294bc68eceeddca57e9` |
| `esp32dev-8di8do-npn` (MW-001 deployed config) | `0ac24c075b0518c749daf83ce6a7d27a76f486b7dc3de1218a90c2020f14ed9a` |
| `esp32dev-mikiwire` (candidate) | `be3a0c32998e06bff510456c4f37475b2f6b0ec5ed7602dfd22f5949b9628655` |
| `release-mikiwire` | `66798bdb3cbdae391fcdd9f5fdfaab79713550e067329ac5ca8798d99d20a11f` |

---

## A. Sensor (LJ12A3-4-Z/BX NPN on DI1/GPIO4)

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| A1 | Hardware identification — confirm board is ESP32-S3-POE-ETH-8DI-8DO, sensor is LJ12A3-4-Z/BX NPN NO, wired sensor-out→DI1, sensor-0V→DI COM, field supply on 7-36V terminal | Physical inspection vs `PRODUCTION_RELEASE_NOTE_v1.0.0.md` §Supported Hardware/Sensor | Matches record | Documentary only: release note + config.h pin table + `test_board_config` freeze. Physical unit not present | BLOCKED (no hardware) | Firmware counts nothing / wrong input |
| A2 | Electrical validation, sensor inactive — measure sensor output, DI1 terminal, GPIO4 voltage | Multimeter/scope at each point, no target at sensor face | Consistent with NPN NO + inverting opto: GPIO4 HIGH (INPUT_PULLUP idle) when sensor inactive | Not measured. NOTE: sensor supply voltage at Miki site is **IVI** (LJ12A3 spec 6-36 VDC; actual wiring unknown) | BLOCKED + IVI | Miscounting/no counting; PCNT edge polarity wrong |
| A3 | Electrical validation, sensor active — same points, ferrous target at ≤4 mm | GPIO4 LOW when target present; clean levels, no oscillation | — | Not measured | BLOCKED | As A2 |
| A4 | Functional sweep: no target / present / slow / normal / rapid / repeated / long-ON / long-OFF; record counts via serial `show` + `/api/v1/status` | Each condition ≥60 s; compare observed pulses vs physical events | 1 count per physical approach event; no counts when static | Not executed | BLOCKED | False counts (production overstated) or missed counts |
| A5 | Stuck/disconnect presentation — disconnect sensor wire; separately hold target permanently | Both present as "no edges"; sensor_health goes IDLE→SUSPECT per configured threshold; **counting continues**; no crash | Not executed | BLOCKED | Silent dead sensor read as idle forever |

## B. Pulse counting (PCNT path)

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| B1 | Known-pulse fidelity 10/50/100/1000 — physical == PCNT == firmware totalizer == server total | Signal generator or scripted target passes; compare all four counts | Exact equality at every scale | Not executed | BLOCKED | The core product number is wrong |
| B2 | Pulse fidelity during WiFi loss, server down, AP mode, OTA download, serial logging load | Repeat B1 under each condition (procedures F1/F2/H1/G-series) | Zero loss in every condition (PCNT is hardware; F3 fix keeps drain alive in AP/OTA) | Not executed. Design-level: PCNT counts in hardware regardless of CPU; AP/OTA drain keepalive host-verified in code review; **hardware proof outstanding** | BLOCKED | "Never lose a pulse" claim unproven |
| B3 | Plausibility monitor logic (F5) — spike immunity, 3-cycle latch, 5-cycle clear, boundary, blocking-gap rates, totalizer regression, millis wrap, live reconfigure, disabled-mode peak tracking | `test_pulse_plausibility.cpp` (host, real class) | All pass | **10/10 PASS** this session | PASS | EMI counted as production silently |
| B4 | PCNT drain race / wrap margin — service cadence guarantees no 16-bit wrap at max plausible rate | Analysis: drain at ≥30000; 30 s keepalive cadence; LJ12A3 ≤~500 Hz ⇒ ≥65 s to wrap from 0 | Margin ≥2× everywhere | Verified by analysis vs code; sensor max rate itself is site-**IVI** | PASS (analysis) / IVI (site rate) | Silent counter wrap during long blocks |

## C. Queue

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| C1 | Segment rollover at cap — next append starts seg N+1; checkpoint moves only after durable write | `test_queue_fault_injection.cpp` #15 (host, real `EventQueue::append`) | Rollover exactly at `QUEUE_SEGMENT_ROWS` | **PASS** (21/21 suite) | PASS | 27 h flash-fill defect returns |
| C2 | Interrupted rollover — write into new segment fails; checkpoint unmoved; retry cleans partial bytes | Suite #16 | No checkpoint advance; clean retry | **PASS** | PASS | Lost/duplicated rows at boundary |
| C3 | Accelerated fill→rollover→ack→reclaim on real flash: drive ≥2 segment boundaries (SIM/bench pulse source or shortened `QUEUE_SEGMENT_ROWS` test build), ack via live server, verify old segment files deleted, storage % stable | Bench, serial `show` + `/api/v1/status` capacity fields | Segments roll and acked segments are physically deleted; no deadlock | Not executed on flash | BLOCKED | Reclaim works on host fake but not LittleFS |
| C4 | Real endurance ≥27 h at 1 row/s — storage %, rows, rollover count, failed writes, heap, reboots | Bench soak with logging | Storage plateaus (bounded); zero failed writes; zero reboots | Not executed | BLOCKED | Original production defect not provably fixed |
| C5 | Full-queue behavior — storage at 100 %: appends fail loudly (FailureState), alarms raised, recovery via ack drain | Host suite covers failure recording; capacity alarm thresholds `capacityAlarmLevel` boundary-tested | Loud, durable failure trace; no crash | **PASS** (host) / flash-full on-device BLOCKED | PARTIAL: PASS(host)+BLOCKED(device) | Silent data loss at full |
| C6 | Ordering invariants preserved — row-durable-before-checkpoint; cursor-persist-before-delete | Code review of final diff (both orderings unchanged) + suite #12/#13 | Invariants intact | **PASS** | PASS | Power-loss correctness broken |

## D. Power / reset

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| D1 | Power cut in normal operation ×10 — recovery, totalizer continuity (≤1 s loss), no boot loop | Bench: cut 24 V/USB at random times | Clean recovery every time | Not executed | BLOCKED | Field power events corrupt state |
| D2 | Power cut during queue write / rollover boundary / checkpoint write / push / ack processing | Bench: scripted cuts targeting each window (repeat ≥5× each; rollover window via C3 setup) | Torn tail ≤1 row truncated; no mass deletion; no seq regression; no duplicate beyond server-side idempotent absorb | Not executed. Host analogs: torn-tail truncation, checkpoint-regression adoption, interrupted rollover all **PASS** in suite | BLOCKED (device) / PASS (host analogs) | The exact failure family the dual-slot design exists for |
| D3 | Brownout classification — brownout resets increment `bod_cnt`, crash streak, REBOOT_LOOP alarm at 3 | Bench variable supply | Counters correct; recovery normal | Not executed | BLOCKED | Undiagnosed field resets |

## E. Watchdog

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| E1 | Design review: can a blocked loop evade detection? | Review every blocking path vs 60 s: push 8 s(+TLS), config 6 s, portal cred-test 15 s, boot STA wait 15 s (pre-arm), OTA download (fed on progress only; 15 s stall abort), WebServer silent-client ≤5 s framework cap. Feed sites: loop top + OTA progress only — no blind feeding anywhere | No legitimate path exceeds 60 s; every true hang starves the feed | Review complete. One caveat recorded: `esp_task_wdt_init` retunes the GLOBAL TWDT period, so the framework idle-task watchdog also moves to 60 s — acceptable, documented. `delay()` never feeds our subscription (correct) | **PASS** (review) | False resets in production, or hangs undetected |
| E2 | Hang injection on hardware — `test_hang` console command (WDT_TEST_BUILD-gated, this phase) | Flash `esp32dev-mikiwire` + `-DWDT_TEST_BUILD=1`; type `test_hang`; observe | Reset within 60 s; boot log shows watchdog reset classified, `wdt_cnt`+1, crash_streak+1; config/queue/totalizer survive; normal operation resumes; streak clears after 5 min healthy | Command implemented + compile-verified both ways (flag absent from all shipping envs). Not executed on device | BLOCKED (device) | Watchdog exists but doesn't actually protect |
| E3 | False-positive soak — 24 h normal operation incl. OTA poll cycles, portal entry/exit: zero WDT resets | Part of C4 soak | `wdt_cnt` unchanged | Not executed | BLOCKED | Spurious production resets |

## F. Network

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| F1 | WiFi loss mid-production → pulses continue → restore → full sync | Bench: kill AP 30+ min under pulse load, restore | No pulse loss; queue drains; cumulative ack advances gap-free; no WDT reset; backoff caps at 2 min WiFi/2 min push | Not executed | BLOCKED | Field outages lose data |
| F2 | Server down (WiFi up) → 5xx/timeout → backoff engages → recovery | Bench + controllable server stub (`server/server.py`) | Push attempts space out 10 s→2 min; reset on first ack; queue intact | Logic host-reviewed; timers use wrap-safe idiom; **on-device timing unverified** | BLOCKED (device) | Server outages hammer API / starve loop |
| F3 | Ack semantics under duplicate/lost-response replay | Server stub: drop responses, replay acks | Idempotent absorb server-side; device re-sends, never double-prunes (monotonic guard) | Covered by design + server suite (`test_p0_1_ack_gap_remediation.py` PASS in 117-suite) + `test_ack_validation` **10/10** | PASS (software) | Silent loss or stuck queue |
| F4 | Ack bounds on device: ack < oldest (ignored), = oldest, = newest sent, > newest (clamped+warned), far beyond (clamped), duplicate (no-op) | `boundAckSeq` host suite + `ackThrough` monotonic guard (code review; ESP-gated path) | Per ack_validation.h contract | **PASS 10/10** (bounds logic); `ackThrough` walk itself is compile-verified only (host harness excludes it by prior design decision) | PASS (logic) / device replay BLOCKED | Bogus ack deletes backlog |

## G. OTA (lab reliability feature — NOT the Miki deployment path)

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| G1 | Signature/downgrade/hash/expiry gates | `test_ota_version_policy` (14) + `test_ota_manifest_auth` (8) — host, real logic | All reject/accept verdicts correct | **PASS** this session | PASS | Malicious/broken image installed |
| G2 | Normal OTA end-to-end on bench (signed manifest via `server/tools/sign_manifest.py` test key) | Bench + server stub | Download, verify, reboot, confirm-healthy, floor advance | Not executed | BLOCKED | Update path unusable when needed |
| G3 | Interrupted download / stall / power cut mid-download / wrong hash | Bench: kill transfer, drip-feed, cut power, serve corrupted bin | Current firmware unaffected every time; backoff 10 min→1 h engages; no boot into unverified slot | Failure *paths* host-verified (abort-before-commit order review + G1 suites); physical interruption not executed | BLOCKED (device) | Bricked device |
| G4 | Known gap (Phase-0 S2/S7): bootloader rollback does not engage on this hardware; low confirm bar; single-key trust | Documented, unchanged — deliberately out of Phase-2 functional scope, owned by the §22 security plan | — | Recorded | IVI / deferred | Bad-but-boots image persists |

## H. AP / provisioning

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| H1 | AP mode keeps counting: enter `provision` mode under pulse load ≥10 min | Bench; compare totalizer before/after vs physical pulses | Zero loss (30 s drain keepalive, F3 fix); checkpoint fresh on power cut in AP mode | Code host-reviewed; not executed on device | BLOCKED | Provisioning sessions lose production counts |
| H2 | Portal credential test (15 s block) does not WDT-reset; telemetry resumes after exit | Bench | No reset; clean resume | Not executed | BLOCKED | Provisioning bricks session |

## I. Storage

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| I1 | Checkpoint corruption family: both-slots-corrupt (adopt rows, never blind-truncate), garbage tail (truncate only tail), single-row tail (normal path), scan-read failure (fail-safe floor), old-checkpoint/newer-ack (seq resume floor) | Queue suite #17–#20 + ack suite resume-floor cases — host, real classes | Never destroy valid telemetry because metadata is corrupt | **PASS** (all cases) | PASS | F4 cascade returns |
| I2 | Boot reconciliation on real flash: stranded below-cursor segment swept; exactly-one orphan deleted; regression (segments > active+1) adopts, deletes nothing | ESP-side code (`cleanupOrphanSegments_`) — host harness excludes directory iteration by design; needs bench with crafted flash states | Per design comments | Compile-verified + code review only | BLOCKED (device) | Wrong file deleted at boot |
| I3 | Flash wear budget at 1 Hz telemetry (2 checkpoint writes/s + row append) over deployment life | Analysis vs `06_FLASH_LIFETIME_ANALYSIS.md` assumptions, now with bounded partition occupancy (rollover) restoring LittleFS wear-leveling headroom | Years, not months | Pre-existing analysis holds better post-rollover; **not re-derived quantitatively this session** | IVI (quantitative) | Premature flash death |

## J. Memory / resource stability

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| J1 | Heap trend over ≥24 h (free, low-water via `/api/v1/metrics`), String/JSON churn under push cycles | C4 soak, sample metrics every 5 min | Flat trend after warm-up; low-water stable | Not executed | BLOCKED | Weeks-scale OOM death |
| J2 | Static footprint | Build reports: RAM 15.0 %, Flash 15.7 % (all envs ±0.1 %) | Ample headroom | **PASS** (recorded) | PASS | — |

## K. Balaji regression

| ID | Test / purpose | Procedure | Expected | Actual / evidence | Status | Risk if failed |
|---|---|---|---|---|---|---|
| K1 | Compile-time isolation: flag-less build contains zero Miki code | `test_board_config` default compile `#error` guard + mikiwire/CT variant compiles | Guard trips on leak | **PASS ×3 variants** this session | PASS | Balaji gets Miki behavior |
| K2 | Full existing test suites at candidate HEAD | 117 Python + all 9 pre-existing native suites | All pass | **PASS** (117 OK +1 skip; native all PASS) | PASS | Regression shipped |
| K3 | Flag-less build success after every commit | Executed per-commit during Phase 1 (esp32dev SUCCESS ×6) | — | **PASS** | PASS | — |
| K4 | Deployed Balaji unit unaffected | No OTA path serves it these changes; binary untouched; changes reach it only via deliberate future reflash | — | **NO IMPACT VERIFIED** (for the deployed unit). For a *future reflash*: shared C changes are **POTENTIAL IMPACT — HARDWARE VALIDATION REQUIRED** (same bench matrix, Relay-1CH build) | PASS / flagged | Breaking the working plant |
| K5 | Serial-output delta of flag-less build | Diff review: one new boot line (`[WDT] armed`), failure-only log lines, no removed lines | Disclosed, additive only | **PASS** (disclosed in Phase-1 report §2) | PASS | Operator tooling parses old format |

## L. Security (assessment only — remediation is the §22 separate plan)

| ID | Item | Finding | Status |
|---|---|---|---|
| L1 | `factory` wipe: exact command `factory`, serial-USB physical access required, no confirmation, not remotely triggerable (serial only; local_api config write is 403; portal writes creds only). Erases NVS namespace `covio` (creds/counters/calibration cache) — **not** queue/totalizer/security-floor | Accidental trigger plausible (one word, no confirm). Proposal drafted (below), no silent change made this phase | Documented; decision required |
| L2 | RELEASE_BUILD diff | See `RELEASE_BUILD_DIFF.md` (this phase) | Done |
| L3 | Both plants on RELEASE_BUILD=0; Balaji trusts placeholder OTA key; NVS plaintext (no flash encryption); TLS = LE-root pinning w/o time validation; open AP portal window | Unchanged Phase-0 findings — security migration project, deliberately not mixed into functional validation | Deferred to §22 plan |

**L1 proposal (not implemented — needs owner decision + service-workflow review):**
require `factory confirm-<last4-of-device-id>` as the exact wipe syntax; bare
`factory` prints the required confirmation string instead of wiping. Keeps
one-command service capability, removes single-word accident. Rejected
alternatives: removing the command (breaks documented service runbooks);
timed double-entry (state across console lines is fragile over flaky serial).

## M. Deployment readiness gate (§26 checklist state)

| Item | State |
|---|---|
| Production snapshot preserved | ✅ `73d82ff`, pushed to origin |
| Candidate branch backed up | ✅ pushed to origin, no force |
| Queue rollover tested | ✅ host / ❌ device (C3, C4) |
| Queue recovery tested | ✅ host (I1) / ❌ device (I2) |
| Power-cycle tested | ❌ (D1–D3) |
| Watchdog tested | ✅ review + injection path built (E1) / ❌ device (E2, E3) |
| Pulse counting tested | ❌ (B1, B2) |
| Network failure tested | ✅ software (F3, F4) / ❌ device (F1, F2) |
| Sensor electrical verified | ❌ + supply-voltage IVI (A2) |
| Sensor health behavior verified | ✅ host (11/11) / ❌ real machine (§19) |
| Plausibility thresholds evidence-based | ❌ — deliberately unset; characterization doc ready |
| Balaji regression completed | ✅ software-complete (K1–K5); reflash-gate flagged |
| Miki firmware build reproducible | ✅ env pinned, hashes recorded (BUILD_TIME makes bins non-byte-reproducible — hash the artifact you flash) |
| Firmware hash recorded | ✅ (table above + deploy package) |
| USB recovery procedure tested | ❌ (procedure written, untested) |
| Rollback firmware available | ✅ `esp32dev-8di8do-npn` @ snapshot state, in deploy package |
| Site deployment checklist prepared | ✅ deploy package |

**GATE VERDICT: DO NOT DEPLOY. 9 of 17 items incomplete — every one requires
the physical bench/site work this session cannot perform.**
