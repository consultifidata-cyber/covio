# 09 — Automated Test Evidence

## Test suites and exact results (real runs, this session, this machine)

| Suite | Command | Result |
|---|---|---|
| Full native suite | `python -m unittest discover -s test/native -v` | **75/75 passed**, 0 failures, 0 errors, 17.6s |
| — of which pre-existing (regression) | `test_adr001_schema.py` (6), `test_dm_phase_0b_auth.py` (6), `test_dm_phase_4_registry.py` (21), `test_dm_phase_6_logical_id.py` (13) | **46/46 passed** (1 assertion in `test_adr001_schema.py` was corrected to match the fixed, correct ack behavior — see doc 02; this is a deliberate test update, not a weakened test) |
| — of which new this session | `test_p0_1_ack_gap_remediation.py` (14), `test_p0_3_admin_auth.py` (18)* | **29/29 passed** |
| Firmware compile: `env:esp32dev` | `pio run -e esp32dev` | **SUCCESS**, 35.6s, RAM 15.0%, Flash 15.6% |
| Firmware compile: `env:release` | `pio run -e release` | **FAILED by design** — placeholder-CA-cert compile guard (`certs.h:50`), identical to pre-fix behavior, confirms no regression |
| Firmware compile: `env:factory` | `pio run -e factory` | **SUCCESS**, 25.3s, RAM 15.1%, Flash 15.8% |

*(`test_p0_3_admin_auth.py` file contains 18 test methods; `subTest` blocks
inside two of them iterate over 4-8 routes each, so the actual assertion
count exceeds 18, but `unittest`'s reported test count is per-method.)*

Raw logs captured in `evidence/test_suite_run.log`,
`evidence/compile_esp32dev.log`, `evidence/compile_release.log`,
`evidence/compile_factory.log`.

## Test layers actually run vs. the mandate's 11-layer list

| # | Layer | Status |
|---|---|---|
| 1 | Firmware unit tests | **NOT RUN** — no host-compilable C++ harness exists in this repo (see doc 05's honest-gap section) |
| 2 | Queue/ack state-machine tests | **RUN**, server-side (the state machine that actually changed — `push()`'s ack computation) — 14 tests |
| 3 | Filesystem fault-injection tests | **NOT RUN** — same C++ harness gap; verified by compile + code review only |
| 4 | Server API tests | **RUN** — 75 tests total exercise every route via Flask's test client |
| 5 | Database transaction/concurrency tests | **RUN** — `test_concurrent_duplicate_pushes_produce_exactly_one_stored_row` (8 real threads) |
| 6 | Authentication/authorization tests | **RUN** — 18 tests in `test_p0_3_admin_auth.py` |
| 7 | Secret scans | **RUN, manually, narrowly** — targeted grep for the two known-exposed literal strings across the entire tree, zero other matches; **not** an automated general-purpose scanner integrated into CI (documented gap, doc 03) |
| 8 | Firmware compilation, all environments | **RUN** — all three (`esp32dev`/`release`/`factory`), real `pio run`, no upload |
| 9 | Integration tests against an isolated local server | **RUN** — every server test IS against a real Flask app instance + a throwaway SQLite file, which is what "isolated local server" means for this architecture (no separate network-level integration harness exists or was built) |
| 10 | End-to-end replay/reconciliation tests | **RUN**, synthetic-data-only — see doc 08; **NOT RUN** against the physically connected device |
| 11 | Existing full regression suite | **RUN** — all 46 pre-existing tests, one assertion corrected (justified in doc 02), zero others touched |

## Honest statement on coverage

This session's test evidence is strong and real for everything that lives
in `server/server.py` (P0-1 and P0-3, entirely server-side fixes). It is
**compile-verified but not unit-tested** for the P0-4 firmware change
(`queue.h`/`diagnostics.h`), because no test infrastructure for that layer
exists in this repository at all — not a regression introduced by this
session, but a pre-existing gap this session's P0-4 fix is subject to.
Building a LittleFS/Arduino host-mock test harness is recommended as a
follow-up (tracked in the updated risk register) but was not attempted
here, to avoid claiming test coverage that would not actually be
trustworthy if rushed.
