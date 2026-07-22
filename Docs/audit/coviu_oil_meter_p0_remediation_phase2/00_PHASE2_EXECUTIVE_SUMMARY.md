# 00 — Phase 2 Executive Summary

## Verdict: AUTOMATED REMEDIATION COMPLETE — HARDWARE AUTHORIZATION REQUIRED

(See `15_FINAL_PLANT_PILOT_CERTIFICATE.md` for the full certificate and
gate-by-gate detail.)

## What this phase did

- **RISK-04 (flash-failure testability):** built a real fault-injection
  harness (`storage_backend.h` abstraction + `test/native_cpp/`) that
  exercises the ACTUAL production `EventQueue::append()`/`FailureState`
  logic, not a reimplementation — 14 tests covering segment-open/truncate/
  write failures, counter monotonicity, timestamp behavior, dual-slot
  corruption recovery, and capacity-threshold boundaries.
- **RISK-15 (OTA anti-downgrade):** implemented a real, durable
  monotonic security-version floor (`store.h`, its own NVS namespace so
  factory reset can't wipe it) and a pure decision function
  (`ota_version_policy.h`) wired into `ota.h::poll()` — 16 tests covering
  upgrade/downgrade/replay/malformed-input/hardware-mismatch/schema-
  mismatch scenarios.
- **Firmware artifacts:** built and hashed a real known-good and a real
  version-bumped candidate binary (doc 07), via a temporary, reverted
  `config.h` edit — nothing committed.
- **Documentation:** OTA static certification updated with the
  anti-downgrade gate traced end-to-end; a full physical OTA test plan,
  USB recovery runbook, credential-rotation checklist, and formal
  authorization request prepared.

## The one fact that matters most in this summary

**No host C++ compiler (g++/clang/MSVC) exists on this development
machine.** Both new test suites (30 tests total) were written, manually
traced against the real code for correctness, and fixed for two real
compile-correctness issues caught on review — but **could not be compiled
or run in this session.** A new CI job
(`.github/workflows/ci.yml::firmware-native-fault-injection`) will
actually execute them on `ubuntu-latest` (which has `g++` preinstalled)
the next time this branch's CI runs. Until that has happened and been
reviewed, **RISK-04 and RISK-15 are not closed** — they are "written,
reviewed, execution pending," a real and meaningfully different status
from "closed," stated as such throughout this phase's documents rather
than glossed over.

## Hardware boundary

COM6 was never opened. No `esptool`, serial monitor, or `-t upload`
command was ever run. All firmware builds this phase were compile-only.
Physical OTA/rollback/downgrade/interrupted-download testing remains
fully unexecuted, with a complete plan and authorization request prepared
(docs 08, 09, 11) and awaiting explicit authorization.

## Where to look for detail

Baseline: doc 01. RISK-04 design/results: docs 02-03. RISK-15 design/
results: docs 04-05. Updated OTA static cert: doc 06. Firmware artifacts:
doc 07. Physical test plan + recovery runbook: docs 08-09. Credential
checklist: doc 10. Authorization request: doc 11. Reconciliation: doc 13.
Updated risk register: doc 14. Final certificate: doc 15.

---

**PHYSICAL HARDWARE AUTHORIZATION REQUIRED:** The code, automated tests,
firmware artifacts, and recovery plan are ready. No serial, flashing,
reset, HTTP device access, or OTA action has been performed. Explicit
authorization is required before executing the documented isolated
hardware tests.
