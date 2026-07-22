# 16 — CI Execution Report

## Honest correction to this phase's mandate

The mandate asked to "run or dispatch CI" and record "workflow name, run
identifier, trigger type, runner OS." **This repository has no git remote
configured** — confirmed via `git remote -v` (empty) at the start of this
phase. There is no GitHub repository this branch is connected to, so no
GitHub Actions workflow run, run ID, or runner log can exist. This is not
a policy choice; it is a hard environmental fact re-verified before doing
anything else this phase.

Given this, the user was asked directly how to proceed (a genuine decision
point, not resolvable from the mandate or the code) and chose: **install a
local compiler and get real local execution evidence instead.**

## What was actually done instead

- Chocolatey (`choco install mingw`) failed — this session's shell lacks
  the administrator rights Chocolatey's package installation requires
  (`Access to the path 'C:\ProgramData\chocolatey\lib-bad' is denied`).
- `winget` requires interactive terms-of-service acceptance, incompatible
  with this session's non-interactive shell.
- **`pip install ziglang`** succeeded (98.7 MB, no admin rights needed) —
  Zig's bundled Clang (`clang version 21.1.0`, target
  `x86_64-unknown-windows-gnu`) is a real, working, portable C++17-capable
  compiler. This is what actually compiled and ran every test in this
  phase.

## Exact commands used, this session, this machine

```
pip install ziglang
python -m ziglang c++ -std=c++14 -Wall -I../.. test_ota_version_policy.cpp -o test_ota_version_policy.exe
python -m ziglang c++ -std=c++14 -Wall -DNATIVE_TEST -I. -I../.. test_queue_fault_injection.cpp arduino_shim.cpp -o test_queue_fault_injection.exe
python -m ziglang c++ -std=c++14 -Wall -I../.. test_ota_manifest_auth.cpp -o test_ota_manifest_auth.exe
python -m unittest discover -s test/native -v
```

## Results (real, executed, this session)

| Suite | Result |
|---|---|
| `test_ota_version_policy` (RISK-15) | **14/14 PASSED** |
| `test_queue_fault_injection` (RISK-04) | **15/15 PASSED** (after fixing 2 real bugs — see doc 17) |
| `test_ota_manifest_auth` (RISK-16) | **8/8 PASSED** |
| `python -m unittest discover -s test/native` | **80/80 PASSED** (75 pre-existing + 5 new `test_sign_manifest_tool.py`) |
| `pio run -e esp32dev` | SUCCESS |
| `pio run -e factory` | SUCCESS |
| `pio run -e release` | FAILED by design (both the pre-existing CA-cert guard AND the new `ota_keys.h` guard fired) |

Full raw logs: `evidence/python_test_run.log`,
`evidence/test_ota_version_policy_run.log`,
`evidence/test_queue_fault_injection_run.log`,
`evidence/test_ota_manifest_auth_run.log`.

## What this is, and is not

This **is** real, observed, first-time execution of code that had
previously only been written and statically reviewed — the mandate's own
core demand ("may not call the remediation 'automatically tested' until
the new suites genuinely execute") is satisfied for LOCAL execution.

This is **not** a GitHub Actions CI run. `.github/workflows/ci.yml` (edited
further this phase — see doc 17/18/20) will run these same suites for real
the moment this branch reaches a GitHub repository with Actions enabled;
until then, "CI" in the GitHub-Actions sense remains theoretical, and this
document does not claim otherwise.

## Secret scanning (mandate's Phase 1, item 5)

Same manual, targeted approach as the prior two phases — no automated
general-purpose scanner (gitleaks/trufflehog) was added this phase either
(still tracked as an open P3 item). This phase's specific scan: confirmed
the new ECDSA test private key
(`server/tools/.test_signing_key.pem`) is `.gitignore`d and was never
staged; confirmed the string `PRIVATE KEY` appearing inside the compiled
firmware `.bin` evidence artifacts is mbedTLS's own internal PEM-header
label table (verified by extracting the exact byte context — it lists
`BEGIN RSA PRIVATE KEY`, `BEGIN EC PRIVATE KEY`, `BEGIN PRIVATE KEY`,
`BEGIN ENCRYPTED PRIVATE KEY` as consecutive parser-recognized label
strings, not an embedded key).
