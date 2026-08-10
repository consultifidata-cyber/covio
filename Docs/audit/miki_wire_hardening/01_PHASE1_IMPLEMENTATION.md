# Miki Wire Hardening — Phase 1 Implementation Report

**Date:** 2026-08-10
**Branch:** `miki-wire-enterprise-hardening` (from snapshot `73d82ff`)
**Scope:** P0 (data loss) → P1 (hang/recovery) → P2 (Miki proximity), exactly
per the approved plan. Release/security migration (RELEASE_BUILD adoption,
OTA key rotation) deliberately NOT mixed in — separate plan required.

---

## 1. Changes implemented (by commit)

| Commit | Change | Category | Findings closed |
|---|---|---|---|
| `aa0de5e` | Queue segment rollover activated; truncate guard with valid-row adoption; boot segment reconciliation (regression detect/adopt, exact-orphan-only delete, below-cursor sweep); bounded per-ack deletion; `QUALITY_BACKLOG_HIGH` wired | **C** (shared) | F1, F4, F10 |
| `48c130d` | `ack_seq` bounded to highest transmitted seq (clamp + warning); boot seq resume floor `max(checkpoint, ack)` | **C** (shared) | F8 |
| `270db97` | Task watchdog (60 s, loop-task subscription, armed after serviceable halts); OTA service-callback with progress-based feed + totalizer keepalive | **C** (shared) | F2, F3 (OTA half) |
| `a392715` | Push failure backoff 10 s→2 min; OTA download-failure backoff 10 min→1 h | **C** (shared) | F7 |
| `ee67b49` | AP-provisioning branch keeps totalizer drain/checkpoint alive (30 s cadence) | **C** (shared) | F3 (AP half) |
| `2559e2e` | `MIKI_WIRE_PROFILE` compile-time layer: `pulse_plausibility.h`, `sensor_health.h`, validated NVS tunables, console commands, quality bits, `esp32dev-mikiwire`/`release-mikiwire` envs, board-guard `#error` | **B** (Miki-only) | F5, F6, S1(partial) |
| `6951e28` | CI: new suites + previously-orphaned suites + MW-001/mikiwire env compiles | tests/CI | — |

## 2. Balaji impact review (every shared file)

The deployed Balaji unit (`esp32-F4E5B2858428`, clean `e5a593b`) is untouched
until someone deliberately reflashes it — no OTA path serves it these changes.
The rows below describe the *flag-less build*, i.e. what a future deliberate
Balaji flash would carry.

| File | Shared? | Flag-less-build effect | Verdict |
|---|---|---|---|
| `queue.h` | yes | Rollover + guards active (intended: Balaji has the same ~27 h flash-fill exposure). On-flash formats/magics/paths unchanged; append→checkpoint ordering preserved | POTENTIAL IMPACT — REQUIRES TESTING (host-tested 21/21; needs hardware soak) |
| `sync.h` | yes | Ack clamp (no-op unless server misbehaves), backoff (only under failure), resume floor (no-op on healthy boot) | POTENTIAL IMPACT — REQUIRES TESTING (behavioral only under failure conditions) |
| `ota.h` | yes | Failure backoff (only after failed download); service callback (feed/keepalive during download) | POTENTIAL IMPACT — REQUIRES TESTING |
| `covio_firmware.ino` | yes | WDT arm/feed + one new boot log line `[WDT] ...`; backlog-bit wiring; AP-branch keepalive. Miki blocks compile-time absent | POTENTIAL IMPACT — REQUIRES TESTING |
| `config.h` | yes | New `#ifndef` macros only; all existing values untouched | NO IMPACT VERIFIED (macros inert unless referenced; board-guard compiles) |
| `store.h` | yes | Miki accessors inside `#if MIKI_WIRE_PROFILE` — absent from flag-less build | NO IMPACT VERIFIED (compile-time absence) |
| `provision.h` | yes | Miki commands inside `#if MIKI_WIRE_PROFILE` — absent from flag-less build | NO IMPACT VERIFIED (compile-time absence) |
| `storage_backend.h` | yes | New `readAt()` method; existing methods byte-identical | NO IMPACT VERIFIED (additive interface; host-tested) |
| `platformio.ini` | yes | Two new envs; existing envs untouched | NO IMPACT VERIFIED |
| `telemetry.h`, `totalizer.h`, `store` NVS keys, wire payload, OTA manifest format | — | **Not modified** | — |

Serial-output note (disclosed, not hidden): the flag-less build's boot output
gains exactly one line (`[WDT] task watchdog armed: 60s`) plus
failure-condition-only log lines. It is NOT byte-identical to the snapshot
build — that is inherent to Category C hardening and was accepted in the
approved plan.

## 3. Test matrix (actual results, 2026-08-10)

| Suite | Result |
|---|---|
| Queue fault injection (21: append faults, rollover at cap, interrupted rollover, checkpoint-regression adoption, tail-only truncation, single-row tail, scan-failure floor, dual-slot recovery, capacity) | **PASS 21/21** |
| Ack validation (10: bounds, clamp, extremes, resume floor incl. live-watermark case) | **PASS 10/10** |
| Pulse plausibility (10: disabled-mode, normal, single-spike immunity, 3-cycle latch, 5-cycle clear, boundary, reconfigure, totalizer regression, blocking-gap rate, millis wrap) | **PASS 10/10** |
| Sensor health (11: startup off/running, active/idle/suspect transitions, instant recovery, disabled mode, arming-mask, millis wrap) | **PASS 11/11** |
| OTA version policy / manifest auth / sensor stuck / sensor CT / credential display / timestamp parse | **PASS** (all, unchanged code) |
| Board-config guard ×3 (flag-less, 8di8do-ct, mikiwire) | **PASS** (compile-time) |
| Python `test/native` (117) | **PASS** (116 + 1 Windows skip) |
| Build matrix: `esp32dev`, `esp32dev-8di8do-npn`, `esp32dev-mikiwire`, `esp32dev-8di8do-ct`, `release`, `release-mikiwire` | **ALL SUCCESS** at the final firmware-source state (`2559e2e`; later commits touch only CI/docs). Artifact SHA-256: esp32dev `72e0f522…57e9`, 8di8do-npn `0ac24c07…ed9a`, mikiwire `be3a0c32…8655`, release-mikiwire `66798bdb…a11f` |
| Watchdog hang-injection on hardware | **BLOCKED** (no device on bench this session) |
| Queue rollover ≥27 h soak on hardware | **BLOCKED** (site/bench hardware required) |
| Power-cycle / brownout recovery on hardware | **BLOCKED** (hardware required) |
| Live sensor OFF→ON/stuck tests with LJ12A3 | **BLOCKED** (site acceptance scope) |

BLOCKED ≠ PASS. Hardware validation is a precondition for deployment (see §5).

## 4. Fault matrix (as now implemented)

| Fault | Detection | Recovery | Counting continues? | Reported |
|---|---|---|---|---|
| Flash fills from unrotated queue | prevented: rollover + ack reclamation | automatic | ✓ | capacity alarms + `QUALITY_BACKLOG_HIGH` |
| Checkpoint regression (both slots) | tail > 1 row / segments > active+1 | adopt valid rows; never blind-delete; seq floor prevents ack deadlock | ✓ | CRITICAL log |
| Oversized/bogus server ack | clamp vs max sent seq | bounded damage | ✓ | WARNING log |
| Firmware hang | 60 s task WDT | panic reset; classified by existing reset-reason counters | ✓ (PCNT + checkpoint recovery) | `wdt_cnt`, REBOOT_LOOP alarm |
| Long OTA download / AP mode parked | — | totalizer drain+checkpoint every 30 s | ✓ | — |
| Server down/erroring | HTTP result | push backoff 10 s→2 min; queue buffers (now genuinely bounded-safe) | ✓ | `pushfail_cnt` |
| Repeated OTA download failure | failed_ state | backoff 10 min→1 h | ✓ | log |
| Implausible pulse rate (Miki) | rate > site ceiling, 3 cycles | advisory only; counts preserved | ✓ | `QUALITY_SUSPECT_RATE` |
| Sensor silent beyond plausible idle (Miki) | gap > site threshold | advisory only | ✓ | `QUALITY_SENSOR_SUSPECT` |
| Invalid Miki tunable | bounds check in store.h | value refused; inert default | ✓ | console message |

## 5. Remaining blockers (unchanged status = must be resolved before deployment)

1. **Miki line parameters** — max plausible pulse Hz and longest plausible
   idle gap: INSUFFICIENT VERIFIED INFORMATION. Monitors ship inert;
   `peakHzObserved` gathers evidence for choosing the ceiling on-site.
2. **Hardware validation** — watchdog hang test, ≥27 h rollover soak,
   power-cycle recovery, live sensor tests (§3 BLOCKED rows).
3. **Deployment path** — MW-001 is reachable only by USB/site reflash (the
   Miki backend deliberately serves no OTA manifests). Flash
   `esp32dev-mikiwire` (or `release-mikiwire` once release provisioning is
   satisfied).
4. **Release/security migration** — both plants still run RELEASE_BUILD=0
   images; Balaji still trusts the placeholder OTA key. Needs its own plan
   (deliberately out of Phase-1 scope).
5. **Balaji reflash decision** — the shared hardening reaches Balaji only
   via a deliberate reflash, which should follow the same bench validation
   plus the freeze-doc process.
