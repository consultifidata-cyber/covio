# Changelog

All notable changes to the Covio Device Manager project (firmware, bench
server, and Windows desktop app) are documented here.

Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning scheme (SemVer adoption, tag format, and whether firmware/desktop
app/backend version independently or in lockstep) is decided in **RE-2 —
Versioning Strategy & Artifact Naming**, not yet performed as of this file's
creation -- so every entry below is grouped by its **DM-Phase** (Device
Manager implementation phase) or **RE-Phase** (Release Engineering phase)
rather than a version number. Once RE-2 lands, new entries move to
conventional `## [X.Y.Z] - YYYY-MM-DD` sections; this file's existing
DM-Phase groupings are not retroactively renumbered.

See `Docs/Covio_Device_Manager_Live_Readiness_Plan.md` for the authoritative
phase definitions and acceptance criteria referenced below.

## [Unreleased]

### v1.3.0 — the meter says WHY it rebooted

- Added: `reset_reason.h` — the single mapping from `esp_reset_reason()` to a
  cause name, plus `resetReasonIsPowerLoss()`. Previously a private static
  inside `diagnostics.h`; promoted because `telemetry.h` now needs the same
  answer, and two switches over one enum is how the LAN endpoint and the
  cloud would eventually disagree about why the same meter rebooted.
  `diagnostics.h` delegates to it and its `/api/v1/metrics` output — including
  the `unknown(<n>)` form — is byte-for-byte unchanged.
- Added: a `boot` object in the push envelope (`telemetry.h::toJson`) carrying
  `boot_id`, `reset_reason`, `reset_reason_raw`, and the three NVS-backed
  lifetime counters `restart_count` / `watchdog_reset_count` /
  `brownout_reset_count`.

  **Why.** A gap in the readings had two opposite meanings that the cloud
  could not tell apart. If the meter lost power, the pump on the same supply
  stopped too, so no oil moved and the day's total is exact. If the meter
  reset itself while the plant kept running, oil flowed past an unpowered
  sensor and the total is a floor. Indistinguishable from the server, so every
  gap had to be treated as the bad case — and days that were in fact complete
  were reported to the owner as unreliable. The device always knew; it had no
  way to say so, because the answer was published only on the LAN-only
  `/api/v1/metrics` endpoint nobody outside the plant network can reach.

  `boot_id` is load-bearing, not decoration: a push batch can carry records
  from an EARLIER boot (that is exactly what a backlog flush after a restart
  looks like), so the reset cause must name the boot it belongs to. Without
  it a receiver would pin this boot's reason onto whichever boot the records
  came from, and clear a gap the meter was never off for.

  ADR-001 is unaffected — `schema_version`/`record_type` remain per-record and
  unchanged; this is envelope-level only, and receivers ignore keys they do
  not know (the bench receiver in `server/server.py` reads the envelope with
  `.get()` and needed no change).
- Added: `test/native_cpp/test_reset_reason.cpp`, wired into the
  `firmware-native-fault-injection` CI job. The ERP clears a measurement gap
  on the strength of these strings and that predicate, so a renamed cause
  silently unclears history and a cause wrongly added to the power-loss set
  fabricates a zero — neither surfaces as an error downstream. Raw 0
  (`ESP_RST_UNKNOWN`, what an esptool-triggered reset reads on the deployed
  Balaji hardware, captured 2026-08-11) is pinned as NOT power loss.
- Changed: `FW_VERSION` 1.2.1 → 1.3.0. `FW_SECURITY_VERSION` stays 2 — this
  release changes no security posture, and that floor is a monotonic NVS
  ratchet that must never be lowered.

  ⚠ Not built, not signed, not published. This tree has no hardware here and
  no compiler; the C++ has been reviewed but never compiled. Cutting a release
  and arming an OTA manifest remain deliberate founder-run steps.

### RE-10 — Final Production Readiness Audit & Release Sign-off
- Added: `Docs/RE10_FINAL_PRODUCTION_READINESS_REPORT.md` — the final
  Release Engineering deliverable: independent Release Manager review
  treating RE-9's validation plan as source of truth, an execution log
  against both RE-9's 13-item Master Checklist and its 16 subsystems (all
  BLOCKED except the desktop app's automated portion, which PASSED with
  fresh, real evidence), a defect register (zero new Critical/High found;
  every prior phase's own fix re-verified still present, no regressions),
  a 6-item risk assessment, a 10-item evidence-gap list identifying
  exactly what real execution is still required, and a **NO-GO** release
  recommendation with an explicit path to GO.
- Verification performed fresh for this audit (not reused from earlier
  turns without re-checking): `npm test` (44/44 pass) and `npm run lint`
  (0 errors, 3 pre-existing warnings) in `tools/device-manager/`; all
  three `.github/workflows/*.yml` files re-parsed as valid YAML; a 39-file
  documentation/structure completeness sweep (all present); a recursive
  scan for accidentally committed secret-like files (none found); RE-7's
  `actions: read` permission fix and RE-8's `.gitignore` `.vscode/*`
  negation-scoping fix both re-confirmed present with no regression;
  `release.yml`'s 8 required release-asset checks re-confirmed to match
  `Docs/RELEASE_VERSIONING.md` exactly.
- No Critical or High defect was found — no code was fixed, refactored,
  or otherwise modified in this phase, per its own explicit scope
  ("no new features, no refactors unless a Critical defect is found").
- Did not claim any hardware, GitHub Actions, GitHub Releases, or live
  device test passed — all 15 of RE-9's remaining subsystems (Firmware,
  Backend, GitHub Actions, GitHub Releases, Windows Installer, Device
  Discovery, Wi-Fi Provisioning, Device Twin, Telemetry, OTA, Registry,
  Factory Provisioning, QR Workflow, Recovery scenarios, Release Upgrade)
  are recorded as BLOCKED, with the exact missing evidence enumerated,
  not glossed over.

### RE-9 — End-to-End Production Validation
- Added: `Docs/RE9_END_TO_END_VALIDATION_PLAN.md` — a complete,
  hardware-execution-ready validation plan covering all 16 required
  subsystems (Firmware, Backend, Desktop Device Manager, GitHub Actions,
  GitHub Releases, Windows Installer, Device Discovery, Wi-Fi
  Provisioning, Telemetry, Device Twin, Registry, OTA, Factory
  Provisioning, QR Workflow, Release Upgrade, Failure Recovery), each with
  Preconditions/Test Procedure/Expected Result/Failure Criteria/Evidence
  Required/Pass-Fail Status, plus a Master Validation Checklist in the
  required execution order (Repository → GitHub Actions → GitHub Release →
  Download Installer → Install Device Manager → Power ON ESP32 →
  Automatic Discovery → Connect Device → Wi-Fi Configuration → Cloud
  Registration → Telemetry → OTA → Recovery Tests), with Factory
  Provisioning/QR Workflow/Release Upgrade documented as separate
  parallel/prerequisite tracks rather than forced onto that customer-
  facing linear flow.
- Every procedure step, route path, field name, error code, and timing
  constant in the plan was cross-checked against the actual governing
  source file during authoring (`config.h`, `diagnostics.h`,
  `local_api.h`, `wifi_provision.h`, `ota.h`, `store.h`, `provision.h`,
  `server/server.py`, and the desktop app's `discovery.js`/
  `provisioning.js`/`factoryTest.js`) rather than written from general
  QA-plan conventions.
- Explicitly disclosed within the plan (not silently validated around):
  Secure Boot V2 + Flash Encryption remain unimplemented (§12's own
  disclosed gap, unchanged since DM-Phase 5); the desktop app has no
  auto-update mechanism (§15) — both are existing, already-documented
  scope boundaries, not defects this phase found or is positioned to fix.
- **No test was executed.** Per this phase's own explicit instruction,
  every subsystem's Pass/Fail field is recorded as NOT EXECUTED — this
  repository has no ESP32 hardware, no configured GitHub remote, and (on
  this development machine) no unblocked local Windows packaging path,
  all disclosed as hard preconditions the plan itself states up front.
- Did not modify: firmware logic, backend logic, desktop application
  functionality, or any release/CI/build configuration file — no Critical
  production defect was discovered during this inspection-and-planning
  pass that would have justified an exception to this phase's own
  "no new features, no refactors" constraint.

### RE-8 — Developer Onboarding & Reproducible Environment
- Added: `SETUP.md` — full new-developer onboarding document: Windows dev
  setup, Git, PlatformIO, Python, Node.js, npm, VS Code extensions,
  environment variables, and complete build/test/release command tables
  for all three components, cross-linked against every other living
  document rather than duplicating their content. Includes an honest
  per-section verification record: real, executed commands for the
  desktop app (Node.js/npm available in this environment); commands for
  firmware/PlatformIO and backend/Python cross-checked against their exact
  governing config files (`platformio.ini`, `pyproject.toml`,
  `requirements-dev.txt`, `ci.yml`) but not executed — no compiler or
  Python interpreter has been available in any environment this project
  has been developed in (unchanged, long-disclosed gap).
- Added: `.vscode/extensions.json` — 8 recommended extensions (PlatformIO
  IDE, C/C++, Python, Ruff, ESLint, Prettier, EditorConfig, YAML), each ID
  matched to its real, well-known Marketplace publisher/extension name,
  not guessed.
- Changed: root `.gitignore` — un-ignored `.vscode/extensions.json`
  specifically, so this one shared recommendations file is actually
  tracked while every other `.vscode/` file stays personal/local. Self-
  caught during implementation (not a separate audit pass): the first
  attempt used a bare `.vscode/` directory-exclusion pattern with a
  `!.vscode/extensions.json` negation after it — a real, easy-to-miss
  `.gitignore` semantics bug, since a negation can never re-include a file
  whose *parent directory* was excluded via a trailing-slash pattern (Git
  prunes the directory before ever evaluating the negation). Fixed to
  `.vscode/*` (excludes the directory's *contents*, not the directory
  itself) before the negation. Verified for real — not just reasoned about
  — using the `ignore` npm package (the same matching semantics Git's own
  `.gitignore` engine uses) against a set of representative paths: confirms
  `.vscode/extensions.json` is tracked, `.vscode/settings.json` and
  `.vscode/launch.json` remain ignored, and the pre-existing
  `.env`/`.env.example` exception is unaffected by the edit.
- Updated: `CONTRIBUTING.md` — the two "full setup instructions land in
  RE-8" forward references now point at the real `SETUP.md`; the
  "No automated linter/formatter exists yet (that's RE-3)" line (stale
  since RE-3 actually landed, never updated) corrected to describe the
  real, existing tooling; the "once RE-5 exists, CI does this
  automatically" firmware-compile-disclosure line and the "decided in RE-2
  ... not yet performed" release/tagging-convention line (both stale since
  RE-2/RE-5/RE-7 all landed since RE-1 originally wrote those lines)
  corrected to reference the real, implemented documents
  (`Docs/RELEASE_VERSIONING.md`, `Docs/RELEASE_AUTOMATION.md`, `SETUP.md`).
- Updated: `VERSIONS.md` — backend Python row expanded to record both
  `pyproject.toml`'s `target-version = "py39"` (minimum-supported target)
  and `ci.yml`'s actual installed `3.11` (what CI really runs against) side
  by side, rather than only the vaguer "3.9+" recommendation — cross-
  checked and found under-specified relative to what `ci.yml` (RE-4/RE-5)
  actually already does, not contradictory.
- Updated: `Docs/FIRMWARE_BUILD.md` — "Building locally" section now
  cross-links `SETUP.md` for the full toolchain-installation walkthrough
  instead of only giving the bare command list.
- Did not modify: firmware logic, backend logic, desktop application
  functionality, or any release/CI workflow file — per this phase's own
  explicit scope. `Docs/DESKTOP_PACKAGING.md`'s already-disclosed
  `winCodeSign`/symbolic-link packaging failure is repeated in `SETUP.md`
  as an actionable local-dev workaround (Developer Mode / elevated shell /
  rely on CI) — a new piece of onboarding guidance, not a functional
  change to the packaging workflow itself.
- Verification performed: `npm ci`/`npm test`/`npm run lint` re-run for
  real in `tools/device-manager/` (44/44 tests, 0 ESLint errors, unchanged
  from RE-7's baseline). The `config.h` `FW_VERSION` line and the `.gitignore`
  fix were both verified with real tooling (a Node.js regex simulation and
  the `ignore` npm package, respectively) rather than assumed correct by
  inspection alone. PlatformIO/Python commands remain unexecuted in this
  environment (disclosed, unchanged gap) — verified instead by exact
  cross-reference against `platformio.ini`/`pyproject.toml`/
  `requirements-dev.txt`/`ci.yml`.

### RE-7 — Automated GitHub Releases
- Added: `.github/workflows/release.yml` — a **new**, `push: tags:`-triggered
  workflow (`v[0-9]+.[0-9]+.[0-9]+` and `v[0-9]+.[0-9]+.[0-9]+-rc.[0-9]+`,
  re-validated with a real bash regex in its own `determine-version` job
  rather than trusting GitHub's tag-filter glob alone). Four jobs in a
  strict `needs:` chain: `determine-version` (parses the tag into a bare
  version + prerelease flag) → `verify-and-build-firmware` (reuses
  `ci.yml`'s **entire** job graph via `workflow_call` — repo-validation,
  firmware, desktop, backend, code-quality — a release never publishes
  without the exact same full verification a normal push/PR gets) →
  `package-desktop` (reuses `desktop-package.yml` the same way) → `publish`
  (downloads every artifact, verifies each expected file exists under
  exactly the name `Docs/RELEASE_VERSIONING.md` defines, then
  `gh release create` — marking `-rc.N` tags as GitHub prereleases and
  plain `vX.Y.Z` tags as full releases). `publish` is the only job, across
  every workflow in this project, ever granted `permissions: contents:
  write`; it uses the automatically-provided `GITHUB_TOKEN`, no new
  repository secret required. No firmware, backend, or desktop application
  functionality changed — release automation only.
- Changed: `.github/workflows/ci.yml` — added an additive, optional
  `workflow_call` trigger (`release_version` string input) alongside its
  existing `push`/`pull_request`/`workflow_dispatch` triggers, completely
  unchanged for its primary use case. The `firmware` job gained a
  conditional "Stamp release version into config.h" step (only runs when
  called with `release_version` set) so the compiled binary's own embedded
  `FW_VERSION` — not just the output filename — matches the released tag.
  Its artifact-preparation step was extended to also require
  `firmware.elf` (same required treatment as `.bin`), copy `firmware.map`
  best-effort if produced, and generate a `.sha256` checksum for every
  real artifact; the whole `artifacts/` directory is now uploaded (was a
  single renamed `.bin`). `repo-validation`'s structure/documentation
  checklists extended to cover `release.yml`/`Docs/RELEASE_AUTOMATION.md`.
- Changed: `.github/workflows/desktop-package.yml` — added the same
  additive, optional `workflow_call` trigger/`release_version` input,
  unchanged for its primary `workflow_dispatch` use case. Gained a
  conditional "Stamp release version into package.json" step (`npm version
  ... --no-git-tag-version --allow-same-version`) so electron-builder's
  `${version}` substitution matches the released tag.
- Changed: `Docs/RELEASE_VERSIONING.md` — artifact naming table extended
  with `.elf` (required), `.map` (best-effort), and a checksum
  (`<artifact-filename>.sha256`) row covering every artifact; marked
  firmware/installer/portable rows as implemented and published by RE-7.
  The previously-sketched `covio-device-manager-vX.Y.Z-release.zip`
  convenience-bundle row is explicitly disclosed as **not** implemented by
  this phase (RE-7's actual scope asked for the individual artifacts to be
  uploaded directly, not a separate curated archive on top of them) rather
  than silently dropped.
- Added: `Docs/RELEASE_AUTOMATION.md` — full architecture (job graph,
  ASCII diagram), the version-stamping design problem and its resolution,
  trigger-rule precision, prerelease detection, permissions model,
  checksum-generation locations, pre-publish artifact verification, and
  compatibility notes against RE-5/RE-6/RELEASE_VERSIONING.md.
- Updated: `Docs/CI_WORKFLOW.md` — header updated; "Required GitHub
  secrets" section now documents `release.yml`'s `publish` job as the
  first, and only, job in the project needing `GITHUB_TOKEN` (automatic,
  no new secret) and `contents: write`; "Future extension points" section
  replaced with an "Extension history" section noting RE-5/RE-6/RE-7 are
  all now implemented, pointing at their own living documents.
- Updated: `Docs/FIRMWARE_BUILD.md` — documents the new conditional
  version-stamping step, the now-required `.elf` artifact, the
  best-effort `.map` artifact, and per-artifact `.sha256` generation.
- Updated: `Docs/DESKTOP_PACKAGING.md` — documents the new
  `workflow_call`/`release_version` trigger and its version-stamping step.
- Verification performed: all three workflow files were parsed with a real
  YAML parser (`js-yaml`) and confirmed structurally correct (trigger
  patterns, job names, `uses`/`with`/`permissions` fields). The
  version-stamping `sed`/extraction logic in `ci.yml`'s `firmware` job was
  verified for real by simulating it (Node.js regex mirroring the exact
  `sed -E` pattern) against `config.h`'s actual `FW_VERSION` line,
  confirming both a stable and an `-rc.N` version stamp correctly. The
  `npm version ... --no-git-tag-version --allow-same-version` stamping
  step in `desktop-package.yml` was verified for real in an isolated
  scratch directory (this environment has no `git` CLI at all — confirmed
  the command still works with zero git interaction, and that re-running
  against an already-matching version does not error). No application
  source file was touched, so `npm test`/`npm run lint` were re-run in
  `tools/device-manager/` to confirm zero regression (44/44 tests pass, 0
  ESLint errors, same 3 pre-existing warnings as RE-6's baseline).
- Fixed (self-audit, High): `release.yml`'s `publish` job declared only
  `permissions: contents: write`. Once a workflow declares any
  `permissions:` block, every unlisted scope defaults to `none` — and this
  job's first three steps are `actions/download-artifact@v4` calls, pulling
  artifacts uploaded by the upstream `verify-and-build-firmware`/
  `package-desktop` jobs in the same run. Rather than rely on an
  unconfirmed ambient default, `actions: read` was added explicitly
  alongside `contents: write` — itself a read-only scope, so this does not
  widen the least-privilege posture, only removes the ambiguity.
- Fixed (self-audit, Medium): `Docs/RELEASE_VERSIONING.md`'s
  "Compatibility with GitHub Actions" section still described RE-7's tag
  trigger as a forward-looking, undecided detail (`'v*'` / a single
  `'v[0-9]+.[0-9]+.[0-9]+*'` placeholder) — stale now that `release.yml`
  actually implements two precise patterns plus its own bash-regex
  re-validation. Updated to describe the real, implemented trigger exactly.
- **Not implemented in this phase (disclosed):** the `covio-device-manager-
  vX.Y.Z-release.zip` convenience bundle (see above); building/publishing
  the `[env:factory]` image (unchanged from RE-5's own scope boundary —
  ADR-008 requires it never ship to a customer); end-to-end execution on a
  real GitHub Actions runner (no remote configured yet — this repository's
  first real tag push will be the first real test of this entire
  pipeline, including its first release-configuration firmware compile).

### RE-6 — Desktop Packaging & Windows Installer
- Changed: `tools/device-manager/package.json`'s `build` section —
  `win.target` now `["nsis", "portable"]` (was `"nsis"` only);
  per-target `artifactName` matching `Docs/RELEASE_VERSIONING.md` exactly
  (`covio-device-manager-setup-vX.Y.Z.exe`,
  `covio-device-manager-vX.Y.Z-portable.exe`); added `copyright`
  ("Copyright © 2026 Covio"), `win.executableName`
  (`CovioDeviceManager`), `forceCodeSigning: false` (explicit, matches
  already-true default), `afterAllArtifactBuild` hook wiring. No desktop
  application functionality, API, or architecture changed — packaging
  configuration only.
- Added: `tools/device-manager/scripts/afterAllArtifactBuild.js` —
  generates a `.sha256` checksum file next to every packaged artifact.
  Verified for real (invoked directly, independent of electron-builder's
  own environment issue — see below): correctly computes and writes a
  matching SHA256, correctly skips `.blockmap` files.
- Added: `tools/device-manager/build/README.md` — documents exactly what
  icon asset is needed and how to activate it; no placeholder icon image
  was fabricated (matches this project's established discipline).
- Added: `.github/workflows/desktop-package.yml` — a workflow **separate**
  from `ci.yml` (this phase's own explicit scope), `workflow_dispatch`
  only, `windows-latest`, re-runs tests/lint then `npm run dist`, uploads
  the installer and portable package (each with its `.sha256`) as
  workflow artifacts. Does not publish a GitHub Release (RE-7).
- Added: `Docs/DESKTOP_PACKAGING.md` — full configuration reference,
  reproducibility notes, and an honest verification record.
- Fixed (self-caught during verification): `eslint.config.js`'s CommonJS
  glob didn't cover the new `scripts/` directory, so `npx eslint .`
  regressed from RE-3's established "0 errors" baseline to 4 errors
  (`afterAllArtifactBuild.js`'s own `require`/`module` flagged as
  undefined) the moment the new file existed — caught by actually running
  the linter after adding it, not assumed safe. `scripts/**/*.js` added to
  the same glob that already covers `src/main/**` and `eslint.config.js`
  itself. Back to 0 errors, 3 pre-existing warnings.
- Updated: `Docs/CI_WORKFLOW.md` — RE-6's own "Future extension points"
  entry corrected to match what was actually built (a separate workflow,
  not a job inside `ci.yml`) and removed once implemented; RE-7's entry
  updated to reference the real `desktop-package.yml` outputs; "Required
  GitHub secrets" section updated to disclose the two optional,
  currently-unset code-signing secrets `desktop-package.yml` references.
- Updated: `.github/workflows/ci.yml` — `repo-validation`'s structure/
  documentation checklists extended to cover RE-6's new files; header
  comment corrected.
- **Verification, stated honestly**: a real `npx electron-builder --win`
  and `npx electron-builder --dir --win` were both attempted in this
  environment and **both failed identically** — `Cannot create symbolic
  link: A required privilege is not held by the client`, while
  electron-builder tries to extract its bundled `winCodeSign` archive
  (apparently required unconditionally by this electron-builder version's
  Windows build pipeline, regardless of code-signing settings). This is a
  genuine Windows account permission restriction in this environment
  (`SeCreateSymbolicLinkPrivilege`), not a configuration defect — confirmed
  by the identical failure recurring across two independent build modes.
  The full NSIS/portable pipeline remains unverified end-to-end pending a
  Windows environment without this restriction (a real GitHub Actions
  `windows-latest` runner does not have it).

### RE-5 — Firmware Build Automation
- Changed: `platformio.ini` — restructured onto a shared `[env]` base
  section (platform/board/framework/partition/source-filter, inherited by
  all environments so they can never drift apart) with an exact, pinned
  `platform = espressif32@6.5.0`; added `[env:release]`
  (`-DRELEASE_BUILD=1`, DM-Phase 5) and `[env:factory]`
  (`-DFACTORY_TEST_BUILD=1`, DM-Phase 6) alongside the existing
  `[env:esp32dev]`. No firmware *behavior* changed — build configuration
  only.
- Added: `firmware` job to `.github/workflows/ci.yml` — installs pinned
  PlatformIO Core (`platformio==6.1.15`), builds `esp32dev` and `release`
  (compile-verification for both; `factory` intentionally not built yet,
  per this phase's own "prepare, don't implement" scope), extracts
  `FW_VERSION` from `config.h`, renames the `release` output to
  `covio-firmware-vX.Y.Z.bin` (`Docs/RELEASE_VERSIONING.md`'s naming
  table), uploads it as a workflow artifact. **This is the first real
  compile this project's firmware has ever undergone** — not yet executed
  on a real runner (no remote configured).
- Added: `Docs/FIRMWARE_BUILD.md` — the three environments, the toolchain
  pin and its reasoning, what to do if the first real compile fails, local
  build instructions.
- Updated: `VERSIONS.md` — firmware toolchain rows moved from "NOT YET
  PINNED" to "pinned (RE-5), not yet compiler-verified" — a real, honest
  status change, not a claim of confirmed success.
- Updated: `Docs/CI_WORKFLOW.md` — architecture diagram, job dependencies,
  failure policy, and branch-protection recommendations extended to cover
  the new `firmware` job; RE-5's own "Future extension points" entry
  removed (now implemented) rather than left stale.
- Did not touch: firmware application logic/behavior, backend, desktop
  application, Windows installer, or release publishing.

### RE-4 — Continuous Integration (GitHub Actions)
- Added: `.github/workflows/ci.yml` — four jobs (`repo-validation`,
  `desktop` matrix on windows-latest/ubuntu-latest, `backend`,
  `code-quality`), all gated behind `repo-validation` passing first.
  Desktop: `npm ci` → `npm run lint` → `npm test`. Backend: installs
  Python 3.11 + Flask, runs `python -m unittest discover -s test/native -v`
  for the first time in this project's history (no interpreter available
  anywhere this project was developed). Code quality: `prettier --check`
  (non-blocking, per `Docs/CODE_QUALITY.md`'s own already-documented
  policy) and `ruff check` (blocking), reported through separate steps.
  Does not build firmware, does not build installers, does not publish
  releases (RE-5/RE-6/RE-7).
- Added: `Docs/CI_WORKFLOW.md` — workflow architecture, job dependency
  graph, required secrets (none today), failure policy, branch-protection
  recommendations, and documented extension points for RE-5/RE-6/RE-7.
- Validated: workflow YAML parsed successfully with a real parser
  (`js-yaml` via one-off `npx`, no dependency added); every referenced
  command and file path (26 total) verified to actually exist. Not yet
  executed on a real GitHub Actions runner — this repository has no
  remote configured yet (see `RE-1`'s own disclosed zero-commits state).
- Fixed (production-quality audit): added an explicit top-level
  `permissions: contents: read` (the workflow previously had no
  `permissions:` block at all, inheriting whatever the repository/org
  default `GITHUB_TOKEN` scope was — a real least-privilege gap, since no
  job ever writes anything); added `.gitattributes` (`* text=auto eol=lf`)
  to prevent the Windows leg of the `desktop` matrix from checking out
  different line endings than the Ubuntu leg/other jobs; added
  `timeout-minutes` to all four jobs (previously defaulted to GitHub's
  360-minute cap); corrected `Docs/CI_WORKFLOW.md`'s RE-7 extension-point
  description, which had incorrectly proposed appending a tag-triggered
  release job onto `ci.yml`'s own push/PR-triggered graph — now correctly
  describes a separate `release.yml`, matching the original Release
  Engineering roadmap.

### RE-3 — Code Quality Gates & Static Analysis
- Added: `tools/device-manager/eslint.config.js` (ESLint 9 flat config,
  separate CommonJS-main/ES-module-renderer rule sets matching this app's
  own security architecture), `.prettierrc.json`, `.prettierignore`.
- Added: `npm run lint` / `format` / `format:check` scripts;
  `eslint`/`@eslint/js`/`prettier` added as real, installed
  `devDependencies`.
- Added: `pyproject.toml` (ruff lint+format config) and
  `requirements-dev.txt` for the backend.
- Added: `.clang-format` (firmware, tuned to match this codebase's
  existing style; `ReflowComments: false` to protect its long-form prose
  comment convention).
- Added: root `.editorconfig` covering all three components.
- Added: `Docs/CODE_QUALITY.md` — formatting/lint policy, CI-gating rules
  for RE-4 to implement, disclosed exceptions, generated-file exclusions.
- Ran ESLint and Prettier for real against the existing desktop-app
  codebase (Node.js/npm available in this environment): found and fixed
  two real bugs in the new `eslint.config.js` itself (its own file wasn't
  covered by either glob; `caughtErrorsIgnorePattern` was needed, not
  `argsIgnorePattern`, to recognize the existing `catch (_e)` convention).
  Final result: 0 ESLint errors, 3 pre-existing warnings (unmodified);
  Prettier found all 27 existing tracked source files differ from its
  output (expected — never previously run through a formatter; no file
  was reformatted, per this phase's own scope).
- `ruff`/`clang-format` were **not** executed — no Python interpreter or
  C++ toolchain available in this environment (see `VERSIONS.md`).
- No firmware logic, backend logic, desktop functionality, release
  workflow, installer, or build-automation file was modified.

### RE-2 — Versioning Strategy & Artifact Naming
- Added: `Docs/RELEASE_VERSIONING.md` — SemVer policy, firmware/desktop-app
  lockstep versioning decision for v1, explicit non-versioning of
  `server.py` (ADR-016-bounded), release tag strategy (`vX.Y.Z[-rc.N]`,
  single tag covering both artifacts), and official artifact naming for
  the firmware binary, Windows installer, portable package, and release
  archive.
- Fixed (production-quality audit): pre-release tag format corrected from
  `-rc1`/`-rc2` to `-rc.1`/`-rc.2` — the undotted form is a genuine SemVer
  2.0.0 §11 precedence bug (`1.0.0-rc10` would sort lexically *before*
  `1.0.0-rc2`); artifact naming standardized to lowercase-kebab-case
  throughout (the Windows installer name previously used Title-Case,
  inconsistent with the other three artifact names).
- Added: `VERSIONS.md` (repository root) — the toolchain-pin file
  `ADR-013` already requires. Records real, verified Node.js/npm/Electron/
  electron-builder/`multicast-dns`/`qrcode` versions; explicitly marks the
  PlatformIO/Arduino-ESP32-core and Python/Flask rows as **not yet
  pinned/verified** rather than guessing, since no compiler or Python
  interpreter has been available in any environment this project has been
  developed in so far.
- No firmware, backend, desktop application, workflow, or build
  configuration file was modified — RE-2 is documentation-only, per its own
  scope.

### RE-1 — Repository Hygiene & Governance Foundation
- Added: root `.gitignore` (firmware build output, bench-server runtime
  data, Python/Node caches, IDE files).
- Added: this `CHANGELOG.md`.
- Added: `CONTRIBUTING.md` (branch strategy, development workflow, PR
  expectations, coding standards, Release Engineering workflow, DM/RE
  governance model).
- Added: `LICENSE` placeholder (license text pending an explicit owner
  decision -- see that file).

### DM-Phase 6 — Factory Onboarding (Logical Device ID, QR Commissioning)
- Added: Logical Device ID (`COV-NNNNNN`) as a third identity tier per
  ADR-018, extending ADR-009 additively.
- Added (backend): sequential ID allocator (`id_counters` table), migration
  adding `devices.logical_device_id` (unique-when-assigned), `/admin/devices/provision`
  now allocates/returns a Logical Device ID (idempotent on re-provisioning).
- Added (firmware): `store.h` NVS field + accessor; `/api/v1/info`'s
  `logical_device_id` now populated (was always `null`); mDNS TXT record
  broadcasts the real value; new `POST /api/v1/factory/provision` local-API
  route, compiled in only for `FACTORY_TEST_BUILD=1` images (write-once ID,
  freely-rotatable API key).
- Added (desktop app): Factory Test view (replaces the DM-Phase 3 stub) --
  network-visible factory checks, backend provisioning, QR label generation
  (`qrcode` dependency) and printing.
- Fixed: CSP `img-src` gained `data:` (was blocking the QR `<img>` entirely);
  a `logical_device_id` write-once conflict no longer silently discards an
  otherwise-valid `api_key` write in the same request.

### DM-Phase 5 — Security Hardening (Transport, ADR-005)
- Added: HTTPS + pinned-CA `WiFiClientSecure` support on all four cloud
  endpoints (push, config-poll, OTA manifest, OTA binary download),
  dispatched automatically by `server_url`'s own scheme
  (`covioIsHttpsUrl()`, case-insensitive) -- plain `http://` continues to
  work unmodified for the bench stub.
- Added: `RELEASE_BUILD` compile-time gate -- refuses to boot with the
  default API key still active in a release build; `certs.h` refuses to
  compile with its placeholder CA certificate in a release build.
- Documented (not implemented -- requires physical hardware): Secure Boot V2
  + Flash Encryption factory-provisioning procedure
  (`Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md`).

### DM-Phase 4 — Backend Device Registry & Device Twin
- Added: `devices` table extended into a full Device Registry + Device Twin
  "current" half; new unified, append-only `device_events` table (historical
  half) per §11.5.
- Added: `POST /admin/devices/provision`, `POST /admin/devices/<id>/revoke-key`,
  `POST /admin/devices/<id>/rotate-key`, `GET /admin/events`,
  `GET /admin/devices/<id>/events`, `GET /admin/devices` dashboard.
- Fixed (post-audit): devices-table migration made crash-safe/self-healing;
  dashboard health-state now recomputed live instead of cached stale;
  quarantined records no longer poison the Twin's `last_push_totalizer`.

### DM-Phase 3 — Windows Device Manager Desktop App
- Added: full Electron application (`tools/device-manager/`) -- mDNS
  discovery + manual-IP fallback, AP-mode-aware provisioning wizard, Live
  Monitor, Diagnostics, OTA status, Logs/Calibration honest stubs, Settings.
- Security: `contextIsolation`/`sandbox` on, `nodeIntegration` off, strict
  CSP, `contextBridge`-only IPC surface, raw API key never rendered/logged/
  persisted.

### DM-Phase 2 — Provisioning Mode (SoftAP + Captive Portal)
- Added: `wifi_provision.h` (SoftAP + captive portal + JSON `/api/v1/config`
  write path, AP-mode only), boot-time station-timeout → AP fallback,
  `provision` console command, WiFi-authority consolidation between
  `sync.h`/`ota.h`.
- Governance: ADR-017 (LAN-Local Diagnostics & WiFi-Based Provisioning) and
  ADR-018 (Logical Device ID tier) authored and approved (DM-Phase 1.8)
  ahead of this phase.

### DM-Phase 1 — Local Diagnostics API
- Added: `diagnostics.h` + `local_api.h` -- read-only local HTTP API
  (`/api/v1/info|status|health|metrics|logs`) and mDNS advertisement, per
  the Live Readiness Plan's frozen §13 Appendix A contract.

### DM-Phase 0B — Backend Authentication
- Added: `X-Api-Key` enforcement on `push`/`config`/`ota/manifest`; bootstrap
  default-key seeding for already-fielded bench devices.

<!-- No GitHub remote/tag exists yet to link a "Compare" URL against --
     add reference-style [Unreleased]/[X.Y.Z] links here once RE-2/RE-7
     (versioning + release automation) are live. -->

