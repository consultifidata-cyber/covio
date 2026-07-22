# 12 — Plant Pilot Readiness Certificate

## Certification decision: **CODE REMEDIATION COMPLETE — PHYSICAL OTA/FAULT CERTIFICATION STILL REQUIRED**

Three of five P0 blockers are genuinely closed with real, passing automated
tests (RISK-01, RISK-03). One is code-closed but carries an outstanding
**human action** that this session cannot perform (RISK-02 — real
credential rotation) and is therefore not fully closed regardless of code
quality. One is code-closed but with an honestly-disclosed unit-test gap
(RISK-04 — no C++ host test harness exists in this repo). One remains
genuinely open pending hardware proof this session's authority boundary
does not permit (RISK-05 — OTA/rollback).

## What is safe to say now

- The device is **not** blocked from a supervised pilot by RISK-01 or
  RISK-03 any longer — both have real fixes and real regression tests.
- The device **cannot** be certified for even a supervised pilot until:
  1. The real WiFi credential is rotated by an authorized network
     administrator (RISK-02 — a human action, not a code change).
  2. RISK-05 (OTA + rollback) is exercised on the physical hardware per the
     plan in doc 07, under explicit human authorization — unless the pilot
     plan commits to **never** using OTA on the piloted unit (in which case
     RISK-05 is a deferred, not a blocking, condition — this is a business
     decision, not a technical one this report can make unilaterally).
  3. RISK-04's unit-test gap is either closed with a real host-test harness,
     or accepted as a documented residual risk for the specific
     supervised-pilot duration (compile-verification + code review is real
     evidence, just not as strong as a passing fault-injection test suite).

## What is NOT claimed

This certificate does **not** claim "zero data loss," "exactly-once
delivery," or "enterprise ready" — none of those claims are demonstrated by
hardware fault-injection in this session (none was performed; none was
authorized). What IS demonstrated: a real, passing, at-least-once delivery
mechanism with a database-enforced idempotency guarantee (RISK-01's fix +
the pre-existing `PRIMARY KEY(device_id,seq)` constraint), which is an
equivalent, sufficient business guarantee for this use case's stated
requirements — not "exactly-once" in the strict distributed-systems sense,
but no double-counted business reading is possible given the DB constraint,
confirmed by concurrency testing this session.

## Path to full closure

1. Rotate the real WiFi credential (human action, outside this session).
2. Execute doc 07's OTA physical test plan on the connected bench unit,
   under explicit authorization.
3. Decide (business decision) whether RISK-04's unit-test gap must be
   closed before pilot, or is an acceptable residual risk given the real
   compile-verification + code-review evidence already in hand.
4. Re-run this session's full test suite (`python -m unittest discover -s
   test/native -v`, currently 75/75 passing) as a fast regression check
   after any further change.

## Signature block

This certificate reflects an AI-assisted code remediation pass, performed
entirely without physical-device interaction (COM6 was never opened, no
`esptool`/serial-monitor/upload command was ever run), against the codebase
as it existed on branch `fix/coviu-oil-meter-p0-enterprise-readiness`,
commits `c83fcd7`..`ae4e037` and this documentation commit, on 2026-07-22.
It is not a substitute for a qualified engineer's sign-off and should not be
the sole basis for a commercial deployment decision.
