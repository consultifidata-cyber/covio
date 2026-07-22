# 15 — Final Plant Pilot Certificate (Phase 2)

## Certification decision: AUTOMATED REMEDIATION COMPLETE — HARDWARE AUTHORIZATION REQUIRED

All code, automated-test authoring, firmware artifacts, and the physical
test plan required before a supervised pilot decision are now in place.
**No physical hardware step has been executed.** This certificate does not
authorize one — see `11_PHYSICAL_TEST_AUTHORIZATION_REQUEST.md`.

## Gate-by-gate status against the mandate's Final Plant-Pilot Gate

- **RISK-01:** Closed, all regression tests pass. **Gate met.**
- **RISK-02:** Source clean; real credential rotation NOT yet confirmed.
  **Gate NOT met** — human action outstanding (doc 10).
- **RISK-03:** Every admin route authenticated/authorized, production
  fails closed, no bypass found. **Gate met.**
- **RISK-04:** Real fault-injection harness written and exercises the
  actual production code; NOT yet proven to pass (no local toolchain to
  run it; wired into CI for the next run). **Gate NOT met** — pending
  actual CI execution and a look at real results.
- **RISK-05:** No OTA or rollback has been demonstrated on hardware.
  **Gate NOT met.**
- **RISK-15:** Downgrade rejection logic implemented and (self-)reviewed;
  floor survives reboot/rollback/factory-reset by design; no break-glass
  process exists (by design). Automated proof pending CI, same as
  RISK-04. **Gate NOT met** in the strict sense (no observed passing
  test run yet), though the design itself satisfies every sub-requirement
  the mandate listed.

## Operational gate

- Device must not be an authoritative billing/inventory/compliance source
  — **unchanged, still true**: RISK-01 and RISK-07 (K-factor retroactivity,
  untouched this phase) still condition this.
- Maximum offline SLA must be based on tested safe capacity — **still not
  independently confirmed on real hardware**; the authoritative PARTITION
  size is confirmed (doc 01), but real on-flash overhead is still an
  estimate (unchanged from phase 1).
- Monitoring/replacement procedures — doc 09 (USB recovery runbook) exists
  now; a spare pre-provisioned unit does NOT exist in evidence (flagged as
  RISK gap, doc 14 wasn't the right place for it but doc 09 states it).

## Verdict

**None of the five P0/P2-escalated risks this mandate named
(RISK-01/02/03/04/05, plus RISK-15) are ALL simultaneously at a status
that clears the full gate.** RISK-01 and RISK-03 are genuinely closed.
RISK-02 needs one human action. RISK-04 and RISK-15 need their new test
suites to actually run (CI) and be reviewed before either can be called
CLOSED rather than "written." RISK-05 needs the authorized physical test
in doc 08.

**A supervised pilot is not yet certifiable from this phase's work alone.**
The concrete, ordered path forward:
1. Push this branch (or otherwise trigger CI) and read the ACTUAL results
   of `firmware-native-fault-injection` — do not assume they pass.
2. Rotate the real WiFi credential (doc 10) and record the confirmation.
3. Review CI results; fix anything the tests reveal.
4. Only then request/grant physical hardware authorization for doc 08's
   test plan.
5. Only after doc 08's tests actually pass on hardware does RISK-05 close,
   and only then is a supervised pilot recommendation fully supportable.

## Signature block

This certificate reflects an AI-assisted code/test-authoring remediation
pass, performed entirely without physical-device interaction, against
branch `fix/coviu-oil-meter-p0-enterprise-readiness` through commit
`ac9bc19` plus this documentation commit, on 2026-07-23. It explicitly
does NOT certify RISK-04/RISK-05/RISK-15 as closed, because the evidence
required to do so (actual passing test runs, actual hardware tests) does
not yet exist. Not a substitute for a qualified engineer's sign-off.
