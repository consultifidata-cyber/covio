# 17 — Flash Fault-Injection Harness: Real Execution Results (RISK-04)

## Result: 15/15 PASSED (real, executed this session)

Two real bugs were found and fixed by actually running this suite for the
first time — reported here in full, per the mandate's explicit instruction
not to hide this.

## Bug 1 — genuine compile error (production code)

`queue.h::append()` contained `if (!tot_) { appendLegacy_(rowIn); return; }`
unconditionally. `appendLegacy_()`'s definition is (deliberately, see doc
02) excluded from `NATIVE_TEST` builds. Native tests always supply a real
checkpoint object, so this branch is never TAKEN at runtime — but the
*call* still needs to resolve at compile time, and it didn't, because the
function it calls doesn't exist in that build configuration. This is a
genuine compile-time defect in the phase-1 refactor, invisible to
`pio run` (which never defines `NATIVE_TEST`) and invisible to code review
alone (the logic reads correctly; the preprocessor interaction is what's
wrong). **Fixed** by wrapping the call itself in `#ifndef NATIVE_TEST`.
Classification: production logic (a preprocessor-guard omission), not a
test-harness or platform issue.

## Bug 2 — genuine test-authoring error (test code, not production)

`test_valid_failure_slot_survives_corruption_of_alternate_slot` assumed
`persistFail_()`'s slot-selection assigned writes-count 1 to slot B and
writes-count 2 to slot A. The actual code
(`const char* path = (fail_.writes & 1) ? FAIL_PATH_A : FAIL_PATH_B;`) does
the reverse: odd → A, even → B. This was caught because the test's own
assertion failed (`q2.failedWriteCount() == 1`, actual value 2) — not
because anyone reviewed the comment and noticed the error; the test
**correctly disproved its own incorrect assumption.** Re-derived the
correct mapping directly from the code, rewrote the test with a
three-failure scenario that unambiguously corrupts the newest slot and
asserts recovery falls back to the correct older value.
Classification: test-authoring error, NOT a production defect —
`persistFail_()`/`begin()`'s dual-slot recovery logic itself is correct
(the same pattern already proven for `AckRec`/`Checkpoint` elsewhere in
this codebase) and required no change.

**Neither fix weakened any assertion.** Bug 1's fix changes zero runtime
behavior (only what compiles under a test-only build flag). Bug 2's fix
made the test assert the CORRECT value, derived independently from the
code, not the value needed to make it pass.

## Coverage confirmation (real, not assumed) against the mandate's list

- Every append failure branch executed: confirmed — `test_segment_open_failure...`, `test_truncate_failure...`, `test_short_write_rejected...`, `test_write_open_failure_rejected...` all PASS, and each specifically asserts `tot.setCount == 0` (checkpoint never advances).
- Short write detected: `test_short_write_rejected_checkpoint_not_advanced` PASS.
- Flush failure detected: **not separately modeled** — see doc 03's stated simplification (bundled into `appendWrite()` at this abstraction level).
- Segment-open failure detected: `test_segment_open_failure_records_failure_does_not_advance_checkpoint` PASS.
- Persistent failure counter monotonic: `test_failure_counter_monotonic_across_multiple_failures` PASS (3 failures, then 1 success, counter stays at 3).
- First-failure timestamp stable / last-failure timestamp updates: `test_first_failure_timestamp_stable_last_updates` PASS.
- One-slot corruption recovery works: `test_valid_failure_slot_survives_corruption_of_alternate_slot` PASS (after the bug-2 fix above).
- Both-slot corruption fails loudly and safely: `test_both_failure_slots_corrupted_falls_back_to_fresh_state_no_crash` PASS.
- Capacity boundaries at 79/80/89/90/94/95/99/100%: `test_capacity_alarm_thresholds_exact_boundaries` PASS, all 9 boundary values individually asserted.
- Reboot persistence: `test_failed_write_state_survives_reboot` PASS.
- No existing unacknowledged row overwritten: `test_existing_confirmed_records_preserved_across_a_later_failure` PASS.
- No failed append reported as successfully captured: every failure test's `tot.setCount == 0` assertion IS this proof (the checkpoint is what makes a row "captured" from the rest of the system's view).

## Does the untested `pending()`/`ackThrough()` gap block RISK-04 closure?

**No, for the specific failure this risk names** (silent loss on a write
failure) — that failure mode lives entirely in `append()`/`FailureState`,
now genuinely tested. **Yes, in a narrower sense** — the mandate's fault
list also asked about "ack cursor succeeds but segment deletion fails"
(item 12) and "segment deletion succeeds but subsequent metadata update
fails" (item 13), which live in `ackThrough()`, explicitly out of scope
(doc 02). These remain compile-verified only. Given the core RISK-04
invariant (no silent write-failure) IS now proven, and the untested
remainder concerns a DIFFERENT risk surface (pruning correctness, closer
to RISK-01's territory than RISK-04's), this is reported as:

## RISK-04 status: **CODE-CLOSED / CI-PROVEN (LOCAL) / RESIDUAL TEST GAP**

The residual gap (`pending()`/`ackThrough()` segment-walk logic) does not,
in this session's judgment, block a supervised pilot recommendation on its
own — it is a pre-existing, previously-reviewed code path unchanged by
this remediation, not a new risk introduced by it. It should still be
closed eventually (tracked in the updated risk register, doc 25) for full
confidence in multi-year unattended operation.
