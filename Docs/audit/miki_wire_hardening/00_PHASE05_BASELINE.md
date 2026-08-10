# Miki Wire Hardening — Phase 0.5 Production Baseline

**Date:** 2026-08-10
**Snapshot branch:** `miki-wire-production-snapshot-20260810`
**Snapshot commit:** `73d82ff6fdd3f1257ea73b36202b5de0e0c643a6`
**Implementation branch:** `miki-wire-enterprise-hardening` (branched from the snapshot)

---

## 1. What the snapshot is

Commit `73d82ff` preserves, verbatim, the covio-main working tree as it stood on
2026-08-10 — 15 modified tracked files (+680/−68) and 27 untracked files, no
stash, on top of `e5a593b`. This tree is the only source record of the firmware
physically running at Miki Wire Line 1 (`esp32-F84AD1A172E0`), released
2026-08-04 per `PRODUCTION_RELEASE_CERTIFICATE_MW-001.md` (Miki-Wire-Updated
repo), which discloses that the release was built from this uncommitted tree.

Nothing was excluded, cleaned, reset, or untangled. The two pre-existing
workstreams in the tree (Balaji V1 freeze remediation; MW-001 / 8DI-8DO board
retarget) are committed together exactly as found — separating them
retroactively would have manufactured a state that never existed on disk.

**Secrets:** `server/tools/.production_signing_key.pem` and
`.test_signing_key.pem` exist on disk, are covered by the `*.pem` gitignore
rule (verified with `git check-ignore` before staging), and are **not** in the
snapshot. The staged file list was explicitly scanned for `*.pem` before
committing. Only the OTA *public* key (`ota_keys.h`) is committed.

**Evidence copies** (outside the repo, session scratchpad):
`covio-dirty-tree-evidence-20260810.diff` (1,241 lines, full `git diff`) and
`covio-dirty-tree-status-20260810.txt` (full status listing).

## 2. Deployed-state ground truth

| Plant | Device | Build source | Env / flags | Pulse input |
|---|---|---|---|---|
| Balaji (`data.funtastik.co.in`) | `esp32-F4E5B2858428`, boot_id 54 | clean `e5a593b` (verified live: `build_commit=e5a593b build_dirty=0`, commissioning docs 10/11) | `esp32dev`, flag-less | GPIO1, Relay-1CH |
| Miki Wire (`compliance.mikigroup.co.in`) | `esp32-F84AD1A172E0` | this snapshot's tree (uncommitted at release time) | `esp32dev-8di8do-npn`, `-DBOARD_MODE=1` | GPIO4/DI1, 8DI-8DO, LJ12A3-4-Z/BX NPN |

## 3. Baseline builds (from snapshot commit `73d82ff`, clean tree)

Build identity embeds `BUILD_COMMIT`/`BUILD_TIME_UTC`, so binaries are not
byte-reproducible across rebuilds; these hashes identify **these** reference
artifacts, built 2026-08-10 with PlatformIO 6.1.15 / espressif32@6.5.0.

| Env | Result | firmware.bin SHA-256 | Size |
|---|---|---|---|
| `esp32dev` (Balaji profile) | SUCCESS (201.7 s) | `d643df577a638b3c9c82fe19b05ad5d7aeac793bbb92f810ddb6d27dfdf7f9de` | 1,032,160 B |
| `esp32dev-8di8do-npn` (Miki profile) | SUCCESS (161.1 s) | `5f83313130bd2c4d0f9e5aaea0172b196f6c56f51b90e087b3fb21d6d3f4f8ff` | 1,032,256 B |
| `release` | SUCCESS (187.9 s) — **first successful release build ever**; previously impossible because the dirty tree tripped the `BUILD_DIRTY` gate | `7b654e0ca993605cd558100edf8ce65d3e460dee8f0445fb0cb9d716ce941923` | — |
| `esp32dev-8di8do-ct` | SUCCESS (174.6 s) | `f27380aeb749af20e7264e9df57ed08e7b657904655966dab1a02105ea3ec70c` | — |
| `factory` | NOT BUILT (deliberate — factory image must never be a shippable artifact; unchanged posture) | — | — |

RAM 15.0% / Flash 15.7% on both plant profiles.

## 4. Baseline tests (all run on snapshot code, unmodified)

| Suite | Result |
|---|---|
| Python `test/native` (117 tests) | **OK** — 116 pass, 1 skipped (`test_gen_production_key_writes_owner_only_permissions`: POSIX file modes not meaningful on Windows) |
| `test_queue_fault_injection` (RISK-04, 15 cases) | **PASS** |
| `test_ota_version_policy` (RISK-15) | **PASS** |
| `test_ota_manifest_auth` (RISK-16) | **PASS** |
| `test_sensor_stuck` | **PASS** |
| `test_sensor_ct` | **PASS** |
| `test_board_config` — default (Balaji pins) | **PASS** |
| `test_board_config` — variant (`-DBOARD_MODE=1 -DSENSOR_MODE=1`) | **PASS** |
| `test_credential_display` (not in CI) | **PASS** |
| `test_timestamp_parse` (not in CI) | **PASS** |
| Static analysis (ruff, server/ + test/native) | NOT RUN locally this phase (CI job exists) |

Host C++ toolchain note: this Windows machine has no g++/MSVC/WSL. The native
C++ suites were compiled and run with `python -m ziglang c++` (zig 0.16.0,
pip-installed), using CI's exact flags (`-std=c++14 -Wall [-DNATIVE_TEST]`).

## 5. Phase 1 targets (from the Phase 0 audit)

P0: queue segment rollover (QUEUE_SEGMENT_ROWS is currently dead — partition
fills in ~27 h of uptime), truncate/file-safety guard, ack_seq bounding.
P1: task watchdog, sensor-path isolation from blocking OTA/AP, push backoff.
P2: Miki-only proximity plausibility + sensor-health (compile-time absent from
the flag-less Balaji build; thresholds ship inert — INSUFFICIENT VERIFIED
INFORMATION for real line parameters).

Miki Wire deployment method for any of the above: **USB/site reflash** — the
Miki backend deliberately serves no OTA manifest endpoint.
