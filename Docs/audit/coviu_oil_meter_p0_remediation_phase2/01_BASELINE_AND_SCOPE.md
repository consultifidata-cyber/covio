# 01 — Baseline and Change-Scope Verification

## Recorded baseline (start of this phase)

- Repository path: `C:\Users\Dell\Documents\ConsultiFi_Data\covio-main`
- Branch: `fix/coviu-oil-meter-p0-enterprise-readiness` (continued, not a new branch — the mandate named this exact branch as "current")
- Commit at start of this phase: `19f5f49` ("docs: P0 remediation deliverables and updated risk register")
- Working tree: clean at start
- Prior commits on this branch: `c83fcd7` (baseline), `9c76edb` (RISK-01/RISK-03), `ae4e037` (RISK-04 v1), `19f5f49` (P0 remediation docs)
- Firmware version: `FW_VERSION "1.0.0"` (config.h, unchanged at baseline)
- PlatformIO environments: `esp32dev` (bench/dev), `release` (RELEASE_BUILD=1), `factory` (FACTORY_TEST_BUILD=1)
- Partition table: `default_16MB.csv`, sha256 `4a6aaf11525dab5d49a336aa52ef7e65f8e83d824c9a7a931c72943cbd40d63c` (re-confirmed this phase, unchanged from the previous phase's finding)
- Server/API test baseline: `python -m unittest discover -s test/native -v` → 75/75 passing
- Existing audit documents: `Docs/audit/coviu_oil_meter_enterprise_readiness/` (original audit) and `Docs/audit/coviu_oil_meter_p0_remediation/` (phase 1) — both preserved unchanged by this phase; this phase's output lives in `Docs/audit/coviu_oil_meter_p0_remediation_phase2/`.

## Regression baseline confirmed before any change

- `python -m unittest discover -s test/native -v`: **75/75 passed**, 0 failures — re-run at the start of this phase, matching the prior phase's final state exactly.
- All `/admin/*` routes confirmed still gated by `require_admin()` (grep-confirmed against `server.py`, unchanged since the prior phase).
- Repo-wide grep for the previously-exposed literal WiFi SSID/password strings: zero matches (still clean).
- No physical-device interaction of any kind performed to establish this baseline (no COM6 access).

## Commit history added THIS phase

- `ac9bc19` — RISK-04 fault-injection harness (StorageBackend abstraction + native test harness) and RISK-15 OTA anti-downgrade (ota_version_policy.h, Store security-version floor, ota.h wiring).
- (further commits below for phase-2 documentation)

## Scope note carried forward from phase 1

Per the mandate's own instruction ("do not reopen already-certified work unless regression evidence requires it"), RISK-01 and RISK-03 were NOT touched in this phase beyond re-confirming their regression tests still pass. No code in `server.py`'s `push()` ack logic or admin-auth decorators was modified this phase.
