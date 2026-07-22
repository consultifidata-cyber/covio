# 14 — Updated Risk Register (Phase 2)

Historical findings from `Docs/audit/coviu_oil_meter_p0_remediation/11_UPDATED_RISK_REGISTER.md`
are preserved below with status updated where this phase changed anything;
nothing is deleted or silently rewritten.

| Risk ID | Severity | Status after phase 1 | Status after phase 2 |
|---|---|---|---|
| RISK-01 | P0 | CLOSED | **CLOSED, unchanged** — regression-confirmed this phase (75/75 tests) |
| RISK-02 | P0 | CODE-CLOSED / HUMAN ACTION PENDING | **Unchanged** — see doc 10, still pending real-world rotation |
| RISK-03 | P0 | CLOSED | **CLOSED, unchanged** — regression-confirmed this phase |
| RISK-04 | P0 | CODE-CLOSED / TEST GAP | **CODE-CLOSED / HARNESS WRITTEN, EXECUTION PENDING (CI)** — a real fault-injection harness now exists and exercises the actual `EventQueue` class (docs 02-03), but could not be executed locally (no host C++ toolchain on this machine); wired into CI for real execution on next run. This is progress, not closure — do not treat as CLOSED until CI results are actually observed |
| RISK-05 | P0 | OPEN — HARDWARE-PROOF-PENDING | **Unchanged — still OPEN.** Nothing in this phase touched physical hardware. Static confidence unchanged from phase 1 (bootloader rollback support confirmed compiled-in) |
| RISK-15 | P2 (new in phase 1) | OPEN | **CODE-CLOSED / DECISION-LOGIC TESTS PENDING CI / OPERATOR MANIFEST UPDATE REQUIRED** — real anti-downgrade gate implemented (`ota_version_policy.h`, `store.h`'s security-version floor), 16 tests written but not locally executed (same CI-pending status as RISK-04). Manifest signing/hashing remain absent (unchanged, separate gap) |
| RISK-06 through RISK-14 | P1/P2/P3 | (see phase 1 register) | **Unchanged this phase** — out of scope, not touched |

## New findings this phase

| Risk ID | Severity | Finding |
|---|---|---|
| **RISK-16** | P2 | **No host C++ test toolchain exists in this development environment at all.** This blocked local execution of BOTH this phase's new test suites. Now mitigated for FUTURE runs via the new CI job, but any local development on a similarly bare Windows machine will hit the same wall. Recommend documenting a required local dev-environment setup step (install MSYS2/MinGW or WSL+gcc) in `Docs/FIRMWARE_BUILD.md` or `SETUP.md`. |
| **RISK-17** | P1 | **The RISK-15 fix is a breaking change for the OTA manifest contract** — any manifest lacking `security_version` is now rejected outright (`OTA_REJECT_MISSING_METADATA`). An operator who forgets to update `server/firmware/manifest.json` after deploying this fix will find NO future OTA update is ever accepted, with only a serial-log/`/api/v1/status` trace to explain why. Documented in doc 05; flagged here as a genuine deployment-sequencing risk, not just a design note. |
| **RISK-18** | P2 | **No server-side audit trail exists for OTA attempts/results** (confirmed again this phase, doc 06) — an operator checking `/admin/events` sees nothing about OTA activity at all, only the device's own local `/api/v1/status`. |

## Gate re-evaluation

| Gate | After phase 1 | After phase 2 |
|---|---|---|
| B — Durable Persistence | PARTIAL (RISK-04 code-closed, test gap) | **PARTIAL, upgraded** — real harness exists, execution pending CI, not yet PASS |
| F — Safe OTA | FAIL (RISK-05 open) | **FAIL, unchanged** — RISK-05 still open; RISK-15 (a related but distinct gate item) improved from OPEN to code-closed |
| G — Security | PARTIAL (RISK-02 human-action-pending) | **PARTIAL, unchanged** — RISK-02 still pending rotation |
