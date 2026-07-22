# Toolchain Versions

**This is the file `ADR-013` ("Single Source of Truth, Native Test
Harness, Pinned Toolchain") already requires**: *"The exact Arduino-ESP32
core (and, if applicable, PlatformIO platform/framework) version used for
the current release build is recorded in a new file at the repository
root, updated whenever a release changes or verifies compatibility with a
new core version."* (`Docs/Firmware Detailed Architecture Decision Record
(ADR).md`). Created here as part of **RE-2 — Versioning Strategy &
Artifact Naming**.

**Honesty note, stated up front:** the firmware toolchain rows below are
now **pinned** (RE-5 — Firmware Build Automation) but **not yet
compiler-verified** — this is a meaningfully different status than "NOT YET
PINNED," but still short of "confirmed working." `ADR-013`'s own Definition
of Done requires this file to "name the exact version the current release
was built and tested against" — since no compiler/toolchain has ever been
available in any environment this project was developed in, these pins are
a **reasoned, deliberate choice**, made explicitly so RE-5's own CI job
(`.github/workflows/ci.yml`, `firmware` job) has something concrete to
compile against for the first time — not a claim that a real compile has
already succeeded. See `Docs/FIRMWARE_BUILD.md` for the full reasoning and
the update procedure if this first real compile finds a problem with the
pin.

## Firmware toolchain

| Component | Version | Status |
|---|---|---|
| PlatformIO platform (`espressif32`) | `6.5.0` | **Pinned (RE-5), not yet compiler-verified.** `platformio.ini`'s `[env]` base section (inherited by `esp32dev`/`release`/`factory` alike, so all three environments share one pin) now specifies `platform = espressif32@6.5.0` — chosen for API compatibility with what this firmware actually uses (see `Docs/FIRMWARE_BUILD.md`), not because it has been confirmed to compile this codebase yet. RE-5's own CI job performs that first real compile. |
| Arduino-ESP32 core | `~2.0.14` (bundled transitively by `espressif32@6.5.0`) | **Pinned indirectly (RE-5), not yet compiler-verified.** Not a separately-specifiable PlatformIO setting — comes bundled with the platform version above. `ADR-013`'s own Reasoning section notes this project has *already* documented experiencing OTA-rollback behavior varying by core version, which is exactly why this row matters; if RE-5's first compile reveals a different bundled core version than expected, this row should be corrected to match reality, not left as an assumption. |
| PlatformIO Core (the `pio` CLI itself) | `6.1.15` | **Pinned (RE-5).** `.github/workflows/ci.yml`'s `firmware` job installs this exact version via `pip install platformio==6.1.15`, for the same reproducibility reason as the platform pin above — an unpinned `pio` CLI version could itself behave differently across runs even with the platform version fixed. |
| Board | `esp32dev` (generic ESP32 Dev Module) | Fixed — `platformio.ini`'s `[env]` section, `board = esp32dev`. Unchanged by RE-5. |
| Partition scheme | `min_spiffs.csv` (OTA-capable, "Minimal SPIFFS (1.9MB APP with OTA)") | Fixed — `platformio.ini`'s `[env]` section, `board_build.partitions`. Required for OTA to function at all (two app partition slots). Bundled with the platform itself, not a project-local file. Unchanged by RE-5. |

## Desktop application (Windows, `tools/device-manager/`) toolchain

Verified directly against this repository's own installed
`node_modules`/environment as of this file's creation — real, checked
values, not version-range specifiers copied from `package.json`.

| Component | Version | Source of truth |
|---|---|---|
| Node.js | `v24.18.0` | `node --version`, this development environment |
| npm | `11.16.0` | `npm --version`, this development environment |
| Electron | `31.7.7` | `tools/device-manager/node_modules/electron/package.json` (resolved from `package.json`'s `"electron": "^31.0.0"`) |
| electron-builder | `24.13.3` | `tools/device-manager/node_modules/electron-builder/package.json` (resolved from `package.json`'s `"electron-builder": "^24.13.3"`) |
| `multicast-dns` (runtime dependency) | `7.2.5` | `tools/device-manager/node_modules/multicast-dns/package.json` |
| `qrcode` (runtime dependency) | `1.5.4` | `tools/device-manager/node_modules/qrcode/package.json` |
| `package-lock.json` lockfile version | `3` | `tools/device-manager/package-lock.json` |

`package.json` pins these with `^` (caret) ranges, not exact versions —
the table above is what those ranges *actually resolved to* in this
environment, which is the number that should be pinned exactly
(`electron-builder` already happens to be pinned exact via its caret range
resolving to no higher patch; `electron`'s `^31.0.0` resolved to `31.7.7`,
a real drift from the literal `31.0.0` written in `package.json` worth
being aware of). Whether to move `package.json` itself to exact-pin
(`"31.7.7"` instead of `"^31.0.0"`) is a build-configuration change,
correctly out of RE-2's documentation-only scope — flagged here for RE-3/
RE-6 to decide.

## Backend (`server/server.py`) toolchain

| Component | Version | Status |
|---|---|---|
| Python | *(none pinned to an exact version)* | **Not exactly pinned; target and CI-actual versions now both recorded (RE-3/RE-4/RE-8).** `pyproject.toml`'s `[tool.ruff] target-version = "py39"` records the *minimum* the code is written to support (RE-3) — still no `requirements.txt`/`.python-version` pinning an exact version. `.github/workflows/ci.yml`'s `backend` and `firmware` jobs both actually install **Python 3.11** (`actions/setup-python@v5`, `python-version: '3.11'`, RE-4/RE-5) — a specific version ≥ 3.9, not a contradiction of the row above, just more precise. `SETUP.md` (RE-8) recommends installing 3.11 specifically for local/CI parity. |
| Flask | *(none pinned)* | **NOT YET PINNED.** `README.md`'s own instructions just say `pip install flask` with no version constraint. To be pinned alongside Python itself once a `requirements.txt` is introduced (RE-3/RE-4). |

## Update discipline

Per `ADR-013`'s own words, this file is "updated whenever a release
changes or verifies compatibility with a new core version" — i.e. this is
a living document, not a one-time snapshot. Whoever bumps a pinned version
here should also confirm (via the RE-4/RE-5 CI jobs, both now live, or a
real local build) that the new version actually still builds and passes
the existing test suites, not just update the number.

**Still open after RE-5**: the firmware toolchain pins above remain
"pinned, not yet compiler-verified" until `.github/workflows/ci.yml`'s
`firmware` job actually runs for the first time on a real GitHub Actions
runner (this repository still has no remote configured — `RE-1`'s own
disclosed state). Once it does, this file should be updated a second time
to change that status to "verified" (or corrected, if the first real
compile finds a problem) — a follow-up action this environment cannot
itself perform.
