# 25 — Updated Risk Register (Pre-Hardware)

Preserves all findings from
`Docs/audit/coviu_oil_meter_p0_remediation_phase2/14_UPDATED_RISK_REGISTER.md`
(the prior sub-phase's register); nothing deleted.

| Risk ID | Severity | Status before this sub-phase | Status now |
|---|---|---|---|
| RISK-01 | P0 | CLOSED | **CLOSED, unchanged** — 80/80 regression re-confirmed |
| RISK-02 | P0 | CODE-CLOSED / HUMAN ACTION PENDING | **Unchanged** |
| RISK-03 | P0 | CLOSED | **CLOSED, unchanged** |
| RISK-04 | P0 | CODE-CLOSED / HARNESS WRITTEN, EXECUTION PENDING (CI) | **CODE-CLOSED / CI-PROVEN (LOCAL) / RESIDUAL TEST GAP** — 15/15 tests genuinely executed and passing (2 real bugs found and fixed in the process, see doc 17); `pending()`/`ackThrough()` segment-walk logic remains an explicitly out-of-scope residual gap, judged not pilot-blocking on its own |
| RISK-05 | P0 | OPEN — HARDWARE-PROOF-PENDING | **Unchanged — still OPEN.** No hardware touched this sub-phase either |
| RISK-15 | P2 | CODE-CLOSED / DECISION-LOGIC TESTS PENDING CI | **CODE-CLOSED / CI-PROVEN (LOCAL) — HARDWARE-PENDING** — 14/14 tests genuinely executed and passing, zero bugs found |
| **RISK-16** (new this sub-phase) | **P0** | N/A | **CODE-CLOSED / CI-PROVEN (LOCAL, PARTIAL) — HARDWARE-PENDING** — real ECDSA-P256 manifest signing + SHA-256 image-hash verification implemented and wired into `ota.h`'s actual download path (replacing the HTTPUpdate library with a manual streaming implementation specifically so hash verification can happen BEFORE the boot partition commits); 13/13 pure-logic tests genuinely executed and passing; the ESP32-coupled integration (actual `mbedtls_pk_verify()` call, actual streaming download) is compile-verified only |
| RISK-06 through RISK-15 (P1-P3, unrelated) | — | (see prior registers) | **Unchanged this sub-phase** |

## New findings this sub-phase

| Risk ID | Severity | Finding |
|---|---|---|
| **RISK-19** | P1 | **No signing-key rotation, multi-key trust, or revocation mechanism exists** (doc 21). If the single compiled-in test/production key is ever compromised, recovery requires shipping a new firmware update signed with the OLD (compromised) key — a real, unresolved single-point-of-failure. |
| **RISK-20** | P2 | **`server/tools/sign_manifest.py` has no access control of its own** — "only authorized release operators may sign releases" is enforced entirely by filesystem/machine access to the private key, not by any code. No release-approval workflow exists. |
| **RISK-21** | P3 | **No git remote exists for this repository at all** (re-confirmed this sub-phase). This blocks genuine CI execution (GitHub Actions) indefinitely until resolved — a process/infrastructure gap, not a code defect, but material to any future "CI passed" claim about this project. |
| **RISK-22** | P2 | **`tools/device-manager` (the actual Device Manager UI) does not display any of the new RISK-15/RISK-16 fields** (security_version, accepted_security_floor, last_reject_reason, last_auth_reject_reason) — these are only visible via raw `/api/v1/status` JSON, not a human-readable UI, unchanged this session. |

## Gate re-evaluation

| Gate | Before this sub-phase | Now |
|---|---|---|
| B — Durable Persistence (RISK-04) | PARTIAL, harness written | **PARTIAL, upgraded** — harness EXECUTED and passing; still not full PASS pending the residual `pending()`/`ackThrough()` gap |
| F — Safe OTA (RISK-05, RISK-15, RISK-16) | FAIL | **FAIL, unchanged for RISK-05** (hardware proof still absent); RISK-15/RISK-16 both upgraded to CI-proven-local, still hardware-pending |
| G — Security (RISK-02, RISK-16) | PARTIAL | **PARTIAL, unchanged for RISK-02**; RISK-16 (a new, real security gap the prior phase disclosed) is now code-closed pending hardware proof, a genuine improvement |
