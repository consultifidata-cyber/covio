# Project Milestones

Living log of implementation checkpoints. New file, kept separate from the
Master Plan's own `PROJECT STATUS` block (not edited here, per
`MASTER_GOVERNANCE.md` — that block is that document's own living record;
this file is a lighter-weight, session-level checkpoint log alongside it).

---

## Milestone: `Phase2_Runtime_Integrated`

**Date:** 2026-07-09
**Status:** READY FOR BENCH VALIDATION (not yet certified — see Blocking item below)
**Implemented:** P2-T1 → P2-T8 (ADR-003, Phase 2 — schema/segment write-offset
checkpoint extension, AckRec cursor extension, segment naming + orphan
cleanup, truncate-before-append + active-segment write path, cursor-based
segment-aware read path, per-segment acknowledgement + deletion, runtime
wiring in `covio_firmware.ino`).
**Remaining:** P2-T9 (adaptive push) and later Phase 2 tasks — **not authorized, not started.**
**Blocking item:** Bench validation only. No known correctness defect in the
reviewed logic (see `Docs/PHASE2_ARCHITECTURE_READINESS_REVIEW.md` and the
P2-T1–T8 implementation/integration reports for the full history). This is
the first point where the new segment-based mechanism runs live (previously
guarded inert behind `tot_ == nullptr`), and none of it has been exercised
on real hardware yet.

**Open items carried forward, non-blocking for bench validation itself:**
- ACR-002 (ADR-003 corrupted-write-offset-checkpoint recovery) remains OPEN — risk knowingly accepted to reach this point.
- A cross-checkpoint (Totalizer vs. AckRec) non-atomicity was identified during integration review — dormant until segment rollover exists, must inform that task's design.
- Now-live write amplification (checkpoint persisted twice per telemetry cycle) — assessed as likely acceptable, pending explicit Architecture Owner sign-off.
- `appendLegacy_()` / `pendingLegacy_()` / `ackThroughLegacy_()` are now dead code in this `.ino`'s configuration — scheduled for a future cleanup task, not yet removed.

**Next action:** execute the bench validation checklist (see the P2-T1–T8
integration report's §7) on physical hardware. Outcome A (bench passes) →
authorize P2-T9. Outcome B (bench finds issues) → fix only what's found,
rerun the affected tests, do not resume forward implementation until clear.
