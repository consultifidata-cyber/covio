# 00 — Remediation Executive Summary

## Verdict: CODE REMEDIATION COMPLETE — PHYSICAL OTA/FAULT CERTIFICATION STILL REQUIRED

(See `12_PLANT_PILOT_READINESS_CERTIFICATE.md` for the full certificate.)

## What changed

| Risk | Fix | Files | Tests |
|---|---|---|---|
| RISK-01 | Ack watermark now advances past permanently-quarantined seqs, not just accepted ones | `server/server.py` | 14 new, all passing |
| RISK-02 | Real WiFi credential replaced with placeholder before first git commit (never entered history) | `config.h` | Repo-wide grep confirms zero other occurrences |
| RISK-03 | HTTP Basic Auth (admin/viewer roles), rate limiting, audit logging, fail-closed production startup added to every `/admin/*` route | `server/server.py` | 18 new, all passing |
| RISK-04 | Durable, dual-slot-CRC failed-write counter + CRITICAL alarm + real filesystem capacity percentage (replacing a code-comment estimate) | `queue.h`, `diagnostics.h`, `config.h` | Compile-verified (3 environments); no unit tests (honest gap — no C++ test harness exists in this repo) |
| RISK-05 | Static/build review only; confirmed bootloader rollback support is genuinely compiled into this toolchain; one new gap found (no anti-downgrade check) | `ota.h` (read, not modified) | Physical test plan prepared, not executed |

**Total: 75/75 automated tests passing** (46 pre-existing regression tests +
29 new), across a real `python -m unittest discover` run this session, plus
three real `pio run` firmware compiles (build-only, no upload).

## Process note (read this before trusting the rest)

Per this mandate's own strict authority boundary — and because this session
has no mechanism to strip serial/hardware tool access from a delegated
sub-agent — **all of this remediation work was performed directly, with no
delegation to any sub-agent.** COM6 was never opened. No `esptool`, serial
monitor, or `-t upload` command was ever run. `git init` was performed
(no `.git` existed at session start, despite CI workflow files already
present in the tree) with the real WiFi credential redacted before the
first-ever commit, so it never entered git history at all.

## What was NOT done, stated plainly

- The real WiFi network's password was **not** rotated — that requires an
  authorized network administrator, not this session.
- No OTA update or forced-rollback test was run on the physically connected
  device — prohibited without separate authorization; a complete test plan
  with an explicit human-approval line is prepared (doc 07).
- No C++ unit tests were added for the P0-4 firmware fix, because no
  host-compilable test harness for firmware logic exists anywhere in this
  repository (confirmed by reading every existing test file — all of them
  test `server.py`, none test `queue.h`/`totalizer.h`/`diagnostics.h`
  directly). The fix is real, compiles cleanly, and was verified by code
  review; it is not unit-tested, and this document says so rather than
  claiming otherwise.
- No automated general-purpose secret scanner was added to CI — the
  credential containment check this session performed was a manual,
  targeted grep, not a standing safeguard against a future secret being
  committed.

## Where to look for detail

Per-risk remediation rationale: docs 02-06. OTA test plan: doc 07. DB/
reconciliation proof: doc 08. Full test evidence: doc 09. Compatibility:
doc 10. Updated risk register (preserves prior findings, adds status +
one new finding): doc 11. Final certificate: doc 12.
