# 26 — Pre-Hardware Certificate

## Verdict: ALL NON-HARDWARE GATES PASSED — PHYSICAL AUTHORIZATION READY

(With the explicit caveats below — this is "ready to request hardware
authorization," not "pilot-certified.")

## What genuinely passed, with real evidence

- **RISK-01, RISK-03:** closed, re-confirmed via 80/80 regression.
- **RISK-04:** 15/15 tests genuinely executed and passing (2 real bugs
  found and fixed by actually running them — not hidden, doc 17).
- **RISK-15:** 14/14 tests genuinely executed and passing.
- **RISK-16:** built for real (ECDSA-P256 signing tool + on-device
  verification + streaming hash-before-commit download), 13/13 pure-logic
  tests genuinely executed and passing, cross-validated against a REAL
  signed manifest for a REAL firmware binary.
- Firmware compiles clean on `esp32dev`/`factory`; `release` fails closed
  on BOTH the pre-existing CA-cert guard and the new OTA-key guard.
- Zero private key material anywhere in tracked files or compiled
  binaries — verified directly, not assumed.

## What did NOT pass / remains genuinely open

- **RISK-02:** real WiFi credential rotation — human action, still not
  performed.
- **RISK-05:** physical OTA + rollback proof — no hardware touched this
  session either.
- **RISK-04's residual gap:** `pending()`/`ackThrough()` segment-walk
  logic remains compile-verified only.
- **RISK-16's residual gap:** the ESP32-coupled verification/download
  wiring (as opposed to the pure crypto/canonicalization logic) is
  compile-verified only, not executed.
- **No GitHub Actions CI run exists or can exist** without a git remote —
  "CI" in this document means real local execution via a locally-installed
  compiler, stated as such throughout, not a GitHub Actions run.

## Why "ready" and not "complete"

Every gate this mandate asked to be resolved WITHOUT touching hardware
has been resolved as far as is possible without touching hardware. The
remaining gaps (RISK-02, RISK-05, and the two residual test gaps above)
are EITHER a human action this session cannot perform, OR require the
physical device this session's authority boundary prohibits touching. That
is precisely the state "ready for hardware authorization" describes.

## Path to full pilot certification

1. Rotate the real WiFi credential (doc 10, unchanged).
2. Obtain physical hardware authorization (doc 24) and execute doc 23's
   six tests.
3. Review the actual results — do not assume they pass.
4. Only then does a supervised-pilot recommendation become fully
   supportable.

## Signature block

Reflects an AI-assisted code/test-execution remediation pass, performed
entirely without physical-device interaction, against branch
`fix/coviu-oil-meter-p0-enterprise-readiness` through commit `d24051f`
plus this documentation commit, on 2026-07-23. Real, locally-executed test
results are reported as such; no GitHub Actions CI run exists (no remote
repository configured) and none is claimed. Not a substitute for a
qualified engineer's sign-off.

---

**PHYSICAL HARDWARE AUTHORIZATION REQUIRED:** All listed non-hardware
gates have passed only if supported by executed CI results above (see doc
16/17/18/20 for the real, local execution logs this claim rests on). No
serial port, device HTTP API, reset, USB flash, or OTA action was
performed. Authorization must identify the device, serial port, isolated
endpoint, approved test cases, and permitted recovery actions.
