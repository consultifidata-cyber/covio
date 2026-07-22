# 14 — Final Production Certificate

## Certification decision: NOT CERTIFIED FOR UNSUPERVISED PLANT DEPLOYMENT

This device/system is **not certified** for unsupervised installation in a customer's live plant at this time, on two independent grounds:

1. **Real, code-proven defects were found** (RISK-01 through RISK-05 in `11_RISK_REGISTER.md`), independent of any hardware-testing limitation — these would need to be fixed regardless of what further hardware testing later shows.
2. **Required hardware-dependent tests could not be safely executed** in this environment (no relay for power-cut injection, no pulse generator, no OTA attempt performed) — several of the mandate's explicitly-required tests (randomized power-interruption cycles, a real OTA + forced-rollback test) remain genuinely unexecuted, not merely assumed safe.

## What this audit DID establish, with real evidence, that a future re-certification pass can build on rather than repeat
- The firmware genuinely compiles (all three build environments attempted for real, in this session, on this machine) — a first for this project's recorded history.
- 90 real automated tests (46 native + 44 device-manager) pass.
- The connected physical device is confirmably a bench/dev unit, not staging/production, and is running its intended firmware correctly, with a real (if narrow) demonstration of checkpoint/queue recovery across one genuine reset.
- The idempotency/delivery design is DB-enforced and sound, modulo RISK-01.
- The TLS/CA-pinning and release-build security gates are real and were directly, deliberately triggered to confirm they work.

## Path to a future GO decision
Complete the P0 remediation items in `12_REMEDIATION_PLAN.md`, then execute the specific hardware tests this audit could not perform (a real power-cut/brownout campaign per `13_PLANT_INSTALLATION_RUNBOOK.md`'s standards, and a real OTA + forced-rollback test), with real evidence captured at each step — not a repeat of static code analysis alone. Re-run this audit's evidence-gathering steps (the compiles, the test suites, the live-device queries) as a fast regression check once fixes land, since they are now proven-fast and repeatable in this environment.

## Signature block
This certificate reflects an AI-assisted static/code audit plus limited, real, non-destructive local verification, performed on 2026-07-22 in a single session, against the codebase and physical device state as they existed at that time. It is not a substitute for a qualified engineer's sign-off, and should not be the sole basis for a commercial deployment decision.
