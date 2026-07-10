# Developer Setup

**Status:** Living document (RE-8, Release Engineering — Developer
Onboarding & Reproducible Environment). This is the "how do I get a clean
clone building on my own machine" document `CONTRIBUTING.md` has pointed
to as pending since RE-1 ("that is Release Engineering phase RE-8's job").
It does not replace `README.md` (what the firmware does, hardware bring-up),
`IMPLEMENTATION_AND_TESTING.md` (bench rig bring-up and field-test
procedure), or `CONTRIBUTING.md` (branch/PR/governance workflow) — it
covers only *installing the toolchains and running the commands* those
documents assume you already have working.

**Honesty note, matching this project's own established discipline
throughout every prior phase:** this document was cross-checked line by
line against every command it references (`package.json` scripts,
`platformio.ini`, `pyproject.toml`, `requirements-dev.txt`,
`.github/workflows/*.yml`, and the test files' own docstrings) rather than
recalled from memory. Two toolchains referenced here — a C++/PlatformIO
compiler and a Python interpreter — have **never been available in the
environment this repository itself has been developed in** (disclosed
consistently since DM-Phase 0B, and again in `VERSIONS.md`,
`Docs/CODE_QUALITY.md`, and `Docs/FIRMWARE_BUILD.md`). The commands for
those two toolchains below are verified *by reading the exact config each
one consumes*, not by executing them in this environment — this is stated
explicitly per-section below, not glossed over.

## Repository layout, in one paragraph

Three components developed together in one repository: the ESP32 firmware
at the repository root (`covio_firmware.ino` + `.h` files — see
`README.md`'s own file map, `ADR-013`'s "no `src/` folder" decision), the
bench/reference backend (`server/server.py`), and the Windows desktop app
(`tools/device-manager/`). You do not need all three toolchains to work on
one component — see each section below for what's actually required.

## 1. Windows development setup

This project's own verified development environment is Windows (see
`VERSIONS.md`) and the Windows desktop app (`tools/device-manager/`) only
packages for Windows (`Docs/DESKTOP_PACKAGING.md`). Firmware and backend
work is not Windows-specific — PlatformIO and Python both run identically
on macOS/Linux — but this document's own concrete steps (paths, shell
examples) assume Windows with PowerShell, matching this repository's own
CI (`windows-latest` legs) and this document's own author environment.

Baseline, before any of the tool-specific sections below:

1. **PowerShell** — already present on Windows; every command below is
   given in PowerShell form. (A Unix-like shell — Git Bash, WSL — works
   too for anything that isn't PowerShell-specific, but is not required.)
2. **A code editor** — VS Code is what this document's **VS Code
   extensions** section (below) assumes; any editor works for the
   toolchains themselves.
3. Enough free disk space for PlatformIO's downloaded toolchain/platform
   packages (the Xtensa/ESP32 toolchain is a real download, cached under
   `~/.platformio` — see `Docs/FIRMWARE_BUILD.md`) and `node_modules/`
   (desktop app).

## 2. Git

No exact Git version is pinned — this project treats Git as an ordinary
baseline dev tool, not a pinned toolchain component like PlatformIO/Node.

- Install **Git for Windows** (also provides Git Bash, not required but
  useful if you ever want to run a workflow's own `shell: bash` step
  locally to debug it — every step in `.github/workflows/*.yml` that
  matters for local reproduction is plain `bash`).
- **Current, disclosed repository state** (see `CONTRIBUTING.md`'s own
  "Branch strategy" section): as of this document's own creation, this
  repository has no configured remote. There is no `git clone <url>`
  command to give you yet — once a remote exists, clone it the ordinary
  way (`git clone <repository-url>`) and everything below applies to that
  clone unchanged.
- Once a remote exists, follow `CONTRIBUTING.md` for branch naming
  (`dm/`, `re/`, `fix/`, `chore/`) and PR expectations — not repeated here.

## 3. PlatformIO (firmware toolchain)

**Not executed in this document's own verification pass** — no compiler or
PlatformIO installation has been available in any environment this
repository has been developed in (see `VERSIONS.md`, `Docs/FIRMWARE_BUILD.md`).
The commands below are verified against `platformio.ini`'s actual `[env]`
definitions and `.github/workflows/ci.yml`'s own `firmware` job, which
runs these same commands for real on every push — that CI run is this
project's actual first-ever real compile, not this document.

1. Install **Python 3.11** first (see **§4 Python**, below) — PlatformIO
   Core itself is a Python package.
2. Install the exact pinned PlatformIO Core version, matching
   `VERSIONS.md`/`ci.yml` exactly (not "whatever's latest" — reproducibility
   requires the same version CI uses):
   ```powershell
   pip install platformio==6.1.15
   ```
3. Confirm the install:
   ```powershell
   pio --version
   ```
4. Build. PlatformIO reads `platformio.ini` at the repository root — run
   these from the repository root, not from inside any subdirectory
   (`src_dir = .` in `platformio.ini` — `ADR-013`'s "no `src/` folder"
   decision):
   ```powershell
   pio run -e esp32dev   # bench/dev build (compile-verify only)
   pio run -e release    # production build (-DRELEASE_BUILD=1)
   pio run -e factory    # factory-test build (-DFACTORY_TEST_BUILD=1) -- manual only, see Docs/FIRMWARE_BUILD.md
   ```
   You do **not** need to pass the platform version (`espressif32@6.5.0`)
   yourself — it's pinned once in `platformio.ini`'s shared `[env]` section
   and PlatformIO downloads/caches it automatically on first build.
5. Flash + monitor over USB (once a build succeeds and a board is
   connected):
   ```powershell
   pio run -e esp32dev -t upload
   pio device monitor -b 115200
   ```
   `-b 115200` matches `platformio.ini`'s own `monitor_speed`. For the full
   bench bring-up procedure (wiring, board settings, first-boot checklist),
   see `IMPLEMENTATION_AND_TESTING.md` — this document covers only the
   toolchain and the build/flash commands themselves.

**Naming note (removes a real, easy-to-hit ambiguity):** PlatformIO calls
a build target an "**environment**" (`[env:esp32dev]`, `pio run -e ...`).
That is a completely different meaning from "environment variables" (§8,
below) or "development environment" (this whole document). When this
project's own docs say "environment," check which of the three meanings
applies from context — PlatformIO's own `-e <name>` flag is always the
build-target meaning.

**If the first real build fails:** see `Docs/FIRMWARE_BUILD.md`'s own "If
the first real CI run fails" procedure — the same guidance applies to a
local build failure.

## 4. Python (backend toolchain)

**Not executed in this document's own verification pass** — no Python
interpreter has been available in any environment this repository has
been developed in (see `VERSIONS.md`). Only needed for `server/server.py`
and `test/native/*.py` — not needed at all if you're only working on
firmware or the desktop app (PlatformIO installs its own bundled Python
internally if none is present, but installing one yourself, as below, is
what lets you also run the backend and its tests).

1. Install **Python 3.11** — `.github/workflows/ci.yml`'s `backend` and
   `firmware` jobs both install this exact version
   (`actions/setup-python@v5`, `python-version: '3.11'`). `VERSIONS.md`'s
   own recommendation ("3.9+") and `pyproject.toml`'s `target-version =
   "py39"` describe the *minimum* the code is written to support; install
   **3.11 specifically** for full parity with what CI actually runs your
   code against, not just any version ≥ 3.9.
2. Confirm the install:
   ```powershell
   python --version
   ```
3. Runtime dependency (to actually run `server.py`) — no version pinned,
   matching `README.md`'s own existing instruction and `VERSIONS.md`'s
   disclosed not-yet-pinned status:
   ```powershell
   pip install flask
   ```
4. Dev-only dependency (lint/format tooling, never needed to run
   `server.py` itself):
   ```powershell
   pip install -r requirements-dev.txt
   ```

## 5. Node.js

Required for the Windows desktop app (`tools/device-manager/`) only.

1. Install **Node.js 24.x** — `VERSIONS.md` records `v24.18.0` as the
   real, verified version this repository's own desktop-app work has been
   developed against; `.github/workflows/ci.yml` and
   `.github/workflows/desktop-package.yml` both install major version `24`
   (`actions/setup-node@v4`, `node-version: '24'`). Installing `24.18.0`
   exactly gives you the closest parity with the verified environment; any
   current `24.x` release is expected to work the same way CI's own
   (potentially newer patch) `24.x` does.
2. Confirm the install:
   ```powershell
   node --version
   ```

## 6. npm

Installed automatically alongside Node.js — no separate install step.
`VERSIONS.md` records `11.16.0` as the real, verified version. Confirm:
```powershell
npm --version
```
Always use `npm ci` (not `npm install`) when setting up a clean clone —
matches the lockfile (`tools/device-manager/package-lock.json`) exactly,
the same convention `ci.yml`/`desktop-package.yml` both already use, rather
than potentially resolving different transitive dependency versions.

## 7. VS Code extensions

`.vscode/extensions.json` (added by this phase) recommends these when you
open the repository root in VS Code — VS Code itself will prompt you to
install any that are missing:

| Extension | ID | Why |
|---|---|---|
| PlatformIO IDE | `platformio.platformio-ide` | Firmware build/upload/monitor from inside the editor; also provides IntelliSense for `platformio.ini`'s environments. |
| C/C++ (Microsoft) | `ms-vscode.cpptools` | C++ IntelliSense for the root `.h`/`.ino` files (PlatformIO IDE depends on this). |
| Python (Microsoft) | `ms-python.python` | Backend (`server/server.py`, `test/native/`) editing/debugging. |
| Ruff | `charliermarsh.ruff` | Real-time lint/format matching `pyproject.toml` exactly (same tool `ci.yml`'s `code-quality` job runs). |
| ESLint | `dbaeumer.vscode-eslint` | Real-time lint matching `tools/device-manager/eslint.config.js` exactly. |
| Prettier | `esbenp.prettier-vscode` | Real-time format-on-save matching `tools/device-manager/.prettierrc.json`. |
| EditorConfig for VS Code | `editorconfig.editorconfig` | Applies root `.editorconfig` (indent width/style, line endings) automatically across all three components. |
| YAML (Red Hat) | `redhat.vscode-yaml` | Schema-aware editing for `.github/workflows/*.yml` — optional, only useful if you touch CI/release workflows. |

`tools/device-manager/.gitignore`'s (and the root `.gitignore`'s) existing
`.vscode/` ignore pattern would normally exclude *any* `.vscode/` file from
being committed at all — this phase adds one narrow, explicit exception
(`!.vscode/extensions.json`, matching the same pattern already used for
`.env.example`) so this one recommendations file is actually shared with
every developer who clones the repository, rather than being a
private, per-machine file. No other `.vscode/` file (settings, launch
configs) is un-ignored — those remain personal/local by design.

## 8. Environment variables

**This project needs almost none for local development** — stated
explicitly because "environment" is an overloaded word here (see §3's
PlatformIO naming note above), and because a new developer coming from a
more configuration-heavy project might reasonably expect a `.env` file to
fill in. There isn't one to fill in:

- **Firmware**: `config.h`'s `#define DEFAULT_WIFI_SSID` /
  `DEFAULT_SERVER_URL` / etc. (edited directly in `config.h`, per
  `README.md`'s own "the only file you edit per deployment") and
  `platformio.ini`'s `build_flags` (`-DRELEASE_BUILD=1`,
  `-DFACTORY_TEST_BUILD=1`) are C/C++ **preprocessor macros**, not OS
  environment variables — nothing in your shell's environment affects a
  firmware build.
- **Backend**: `server.py` hardcodes `host="0.0.0.0", port=8000` — no
  environment variable configures it. Nothing to set.
- **Desktop app**: no code in `tools/device-manager/src/` reads
  `process.env` for anything (confirmed by inspection) — nothing to set
  for `npm start`/`npm test`/`npm run lint`.
- **`PATH`**: the only thing that actually needs to be "configured" —
  make sure `pio`, `python`/`pip`, and `node`/`npm` are all on your
  shell's `PATH` after installing them (§§3–6 above); each installer
  offers to do this automatically on Windows.
- **Optional, packaging-only**: `CSC_LINK` / `CSC_KEY_PASSWORD`
  (code-signing certificate + password) — only consumed by
  `desktop-package.yml`, only via GitHub Secrets, never needed for local
  `npm run dist` (electron-builder produces an unsigned build without
  them — see `Docs/DESKTOP_PACKAGING.md`). Do not set these locally unless
  you specifically need to test signing.
- **Automatic, CI-only**: `GITHUB_TOKEN` — provided automatically by
  GitHub Actions for `release.yml`'s `publish` job; never configured by a
  developer, locally or otherwise (see `Docs/RELEASE_AUTOMATION.md`).

## 9. Build commands

| Component | Command | From |
|---|---|---|
| Firmware (bench/dev) | `pio run -e esp32dev` | repository root |
| Firmware (production) | `pio run -e release` | repository root |
| Firmware (factory-test) | `pio run -e factory` | repository root (manual only — never built by CI, see `Docs/FIRMWARE_BUILD.md`) |
| Desktop app (run unpackaged) | `npm start` | `tools/device-manager/` |
| Desktop app (package installer + portable) | `npm run dist` | `tools/device-manager/` |
| Backend | *(none — interpreted, not compiled)* `python server.py` runs it directly | `server/` |

**Desktop packaging, known local-environment caveat** (disclosed in
`Docs/DESKTOP_PACKAGING.md`, repeated here since a new developer will hit
it immediately on `npm run dist`): electron-builder's Windows packaging
extracts a bundled `winCodeSign` archive using a symbolic link, which
fails with `Cannot create symbolic link: A required privilege is not held
by the client` on a Windows account without `SeCreateSymbolicLinkPrivilege`
(a standard, non-administrator account). If you hit this:
- Enable **Windows Developer Mode** (Settings → Privacy & security → For
  developers) — grants this privilege without needing Administrator, **or**
- Run your shell **as Administrator**, **or**
- Skip local packaging entirely and rely on
  `.github/workflows/desktop-package.yml` — GitHub's own `windows-latest`
  runners do not have this restriction.

## 10. Test commands

| Component | Command | From |
|---|---|---|
| Backend (all native tests) | `python -m unittest discover -s test/native -v` | repository root |
| Backend (single file) | `python test/native/test_dm_phase_0b_auth.py` | repository root (works because each test file inserts `server/` onto `sys.path` itself) |
| Desktop app (unit tests) | `npm test` | `tools/device-manager/` |
| Desktop app (lint) | `npm run lint` | `tools/device-manager/` |
| Desktop app (format check) | `npm run format:check` | `tools/device-manager/` (currently non-blocking in CI — see `Docs/CODE_QUALITY.md`) |
| Backend (lint) | `ruff check server/ test/native/` | repository root |
| Backend (format check) | `ruff format --check server/ test/native/` | repository root |
| Firmware | *(no native unit-test harness for firmware logic itself — `ADR-013`'s "Native Test Harness" refers to `test/native/`'s backend-logic tests above, which exercise the same wire contract the firmware speaks, without needing ESP32 hardware)* | — |

Matches `.github/workflows/ci.yml`'s own `backend`/`desktop`/`code-quality`
jobs exactly — running these locally before opening a PR is what
`CONTRIBUTING.md`'s "run the relevant test suite locally" line (§ Pull
request expectations) means in practice.

## 11. Release commands

Ordinary local development never runs these — they trigger
`.github/workflows/release.yml` (RE-7), which is deliberately the *only*
path that publishes a GitHub Release (`Docs/RELEASE_AUTOMATION.md`). Given
here for completeness/onboarding, and **disclosed as unexercised**: this
repository has no remote configured yet (§2 above), so no tag has ever
actually been pushed and this sequence has never run for real.

```powershell
git tag v1.2.3            # stable release -- or v1.2.3-rc.1 for a pre-release dry run
git push origin v1.2.3
```

Pushing a tag matching `Docs/RELEASE_VERSIONING.md`'s SemVer policy is the
**entire** release trigger — no separate CLI command builds or uploads
anything; `release.yml`'s own `determine-version` → `verify-and-build-firmware`
→ `package-desktop` → `publish` pipeline (`Docs/RELEASE_AUTOMATION.md`)
does the rest. A developer should never manually run `pio run -e release`
or `npm run dist` and manually upload the result to a GitHub Release — the
tag push is the only supported path, so version-stamping
(`Docs/RELEASE_AUTOMATION.md`'s own "version-stamping problem" section)
happens correctly every time.

`tools/device-manager/dist/` output from a **local** `npm run dist` (§9
above) is for local testing/inspection only — never a release artifact by
itself.

## Verification performed (and what could not be verified)

Real commands were actually run in this environment (Node.js/npm are
available — see `VERSIONS.md`):

- `npm ci`, `npm test` (44/44 pass), `npm run lint` (0 errors, 3
  pre-existing warnings) — all executed for real in
  `tools/device-manager/`, matching §§9–10 exactly.
- Every `package.json` script referenced above (`start`, `test`, `dist`,
  `lint`, `format`, `format:check`) was cross-checked against
  `tools/device-manager/package.json`'s actual `scripts` block — none
  invented.
- Every PlatformIO command (§3, §9) was cross-checked against
  `platformio.ini`'s actual `[env:esp32dev]`/`[env:release]`/
  `[env:factory]` section names and `.github/workflows/ci.yml`'s
  `firmware` job — **not executed**, no PlatformIO/compiler available in
  this environment (disclosed at the top of this document).
- Every Python/`ruff` command (§4, §10) was cross-checked against
  `pyproject.toml`'s actual `[tool.ruff]` configuration,
  `requirements-dev.txt`, and `test/native/test_dm_phase_0b_auth.py`'s own
  docstring (which documents its own two run forms verbatim) — **not
  executed**, no Python interpreter available in this environment
  (disclosed at the top of this document).
- `.vscode/extensions.json`'s 8 extension IDs were not installed/tested in
  this environment (no interactive VS Code session available here) — each
  ID was checked against its publisher's own well-known, stable Visual
  Studio Marketplace identifier rather than guessed.

**What "a clean clone can be built by following only this documentation"
actually means today, stated honestly:** fully true and verified for the
desktop app (§§5–10, real commands, real passing results, in this exact
environment). For firmware and backend, this document is verified to be
**internally consistent with every config file that governs those
commands** (`platformio.ini`, `pyproject.toml`, `requirements-dev.txt`,
`ci.yml`) — but, matching this project's own long-disclosed gap, has not
been proven end-to-end by actually running `pio run -e esp32dev` or
`python -m unittest discover -s test/native -v` in any environment yet.
`.github/workflows/ci.yml` running for the first time on a real GitHub
Actions runner remains this project's first real end-to-end proof of both.

## Cross-reference index

| Topic | Authoritative document |
|---|---|
| What the firmware does, hardware wiring | `README.md` |
| Bench bring-up, field-test procedure, remote deployment | `IMPLEMENTATION_AND_TESTING.md` |
| Full architecture / API contract | `ARCHITECTURE.md` |
| Branch strategy, PR expectations, DM/RE governance | `CONTRIBUTING.md` |
| Exact pinned/verified toolchain versions | `VERSIONS.md` |
| SemVer policy, artifact naming, release tags | `Docs/RELEASE_VERSIONING.md` |
| Formatting/lint policy, when CI fails on style | `Docs/CODE_QUALITY.md` |
| CI job architecture, required secrets, failure policy | `Docs/CI_WORKFLOW.md` |
| Firmware build environments, toolchain-pin reasoning | `Docs/FIRMWARE_BUILD.md` |
| Desktop packaging, checksums, code signing | `Docs/DESKTOP_PACKAGING.md` |
| Release pipeline architecture, permissions model | `Docs/RELEASE_AUTOMATION.md` |
| Firmware architecture decisions (ADRs), Absolute Rules | `Docs/MASTER_GOVERNANCE.md`, `Docs/Firmware Detailed Architecture Decision Record (ADR).md` |
