# Release Versioning Strategy

**Status:** Living document (RE-2, Release Engineering). Strategy only —
adopting it in practice (actually bumping version numbers, cutting the
first tag) happens starting at **RE-7 — GitHub Releases Automation**, not
here. This document does not itself change any version number in the
codebase.

## Why one strategy document for three artifacts

This repository ships three things that are developed together but have
different release semantics:

| Artifact | Where its version lives today | Is it an independently released artifact? |
|---|---|---|
| ESP32 firmware | `FW_VERSION` in `config.h` | Yes — becomes a GitHub Release asset (a `.bin`) |
| Windows desktop app | `"version"` in `tools/device-manager/package.json` | Yes — becomes a GitHub Release asset (an installer/portable `.exe`) |
| Bench/reference backend (`server/server.py`) | No version marker exists | **No** — see below |

**Not to be confused with:** `ADR-001`'s `schema_version`/`SCHEMA_VERSION_CURRENT`
(the wire/SD-row telemetry format discriminator, `SCHEMA_REGISTRY.md`) and
`ADR-007`'s `config_schema_version` (NVS layout versioning, not yet
implemented — see `VERSIONS.md`). Both are orthogonal, independent
versioning concepts already owned by their respective ADRs — this document
governs release/artifact versioning only, and does not change, reference,
or renumber either of those.

## Backend versioning (server.py)

`ADR-016` (frozen) already declares this receiver **bench/development-only
forever**; a real production receiver is a separate, out-of-scope system.
Consequently `server.py` is **not** an independently versioned or released
artifact in the GitHub Releases sense — it does not get its own tag, its
own SemVer number, or its own changelog entries distinct from the repo's.
It is tracked implicitly by whatever commit/tag of the overall repository
it ships alongside, purely for traceability ("which commit was the bench
server at when this firmware/app release was cut"), never as a claim that
it has its own release lifecycle. Do not add a version marker to
`server.py` that would imply otherwise.

## Firmware versioning

`FW_VERSION` (`config.h`) is already load-bearing at runtime — `ota.h`
compares the OTA manifest's offered version against it to decide whether an
update is needed, and it's reported via `/api/v1/info`'s `fw_version`
field (§13 A.3, frozen contract). The versioning strategy does not change
this mechanism, only the *policy* for what value goes into it:

- Follows Semantic Versioning (see **SemVer policy** below).
- Bumped by hand in `config.h` today (no automation exists until RE-5/RE-7
  land); each bump is a single-line, non-functional change.
- For v1 (see **Lockstep policy** below), kept equal to the same version
  the desktop app ships under for the same release.

## Desktop application versioning

`tools/device-manager/package.json`'s `"version"` field is already the
convention Node/npm/electron-builder expect, and already drives the
installer filename via electron-builder's default naming (see **Artifact
naming** below). The strategy:

- Follows Semantic Versioning.
- For v1, kept equal to the firmware's `FW_VERSION` for the same release
  (see **Lockstep policy**).

## Lockstep policy (v1)

**Decision:** For v1 (the scope covering DM-Phase 0B through DM-Phase 6 —
see **Relationship to the Live Readiness Plan's own roadmap** below),
firmware (`FW_VERSION`) and the desktop app (`package.json`'s `version`)
are released **in lockstep** — the same release tag bumps both to the same
number, even in a release where only one of the two artifacts actually
changed. This is a deliberate simplification, not an oversight:

- Every DM-Phase so far has touched firmware and/or the desktop app as one
  coordinated unit of work (e.g. DM-Phase 3's provisioning wizard depends
  on DM-Phase 2's firmware endpoint; DM-Phase 6 touched both together).
- Tracking independent version skew between two artifacts that have never
  yet shipped independently would be process overhead with no current
  payoff.
- This is explicitly revisitable: the Live Readiness Plan's own §11.9
  roadmap names "V2: Cloud Fleet Management" as a later scope where release
  cadences could reasonably diverge — if/when that happens, this document
  should be updated to describe independent versioning at that point, not
  before.

**Current known inconsistency, disclosed rather than silently fixed:**
`config.h`'s `FW_VERSION` is currently `"1.0.0"` and
`tools/device-manager/package.json`'s `"version"` is currently `"0.1.0"` —
these do **not** match today. This is expected and untouched by this
document: no version number is bumped as part of RE-2 (documentation-only,
per RE-2's own scope). Aligning both to the same number happens at the
point the first real release is actually cut (RE-7), once RE-9's hardware
acceptance testing and RE-10's end-to-end validation have passed — not
before, since bumping either number now would misleadingly suggest a
release exists when nothing has been hardware-validated yet.

## Semantic versioning policy

Standard [SemVer 2.0.0](https://semver.org/) — `MAJOR.MINOR.PATCH`, applied
to the lockstep firmware+desktop-app version:

- **MAJOR** — a breaking change to a frozen contract: §13's local API
  contract, the cloud wire contract (`push`/`config`/`ota/manifest`), or a
  backward-incompatible ADR supersession. Also any firmware change that
  cannot be delivered via a normal OTA update (e.g. a change requiring
  re-flashing over USB).
- **MINOR** — new backward-compatible functionality. In practice, this
  project's unit of "new functionality" has been the DM-Phase — each of
  DM-Phase 4/5/6 would reasonably have been a MINOR bump had tags existed
  at the time.
- **PATCH** — backward-compatible bug fixes only (the audit-fix pattern
  used throughout this project's own DM-Phase and RE-Phase history).
- **Pre-release identifiers** (`-rc.1`, `-rc.2`, ...) — used for release-
  candidate dry runs through the CI/Release pipeline (RE-7) before a real
  tag is cut. E.g. `v1.0.0-rc.1` exercises the full pipeline without being
  the actual first release.
  **Audit fix (production-quality audit): the dot between `rc` and the
  number is required, not stylistic.** SemVer 2.0.0 §11 compares dot-
  separated pre-release identifiers individually: an identifier consisting
  *only* of digits is compared numerically, but a mixed letters+digits
  identifier (no dot) is compared lexically (ASCII order). `-rc1`/`-rc2`/
  `-rc10` with no dot would be ONE identifier each, compared as strings —
  under that comparison, `1.0.0-rc10` sorts *before* `1.0.0-rc2` (`'1' <
  '2'` at the first differing character), silently breaking any future
  semver-aware tooling (e.g. "get the latest rc") the moment a 10th
  release candidate exists. `-rc.1`/`-rc.2`/`-rc.10` splits `rc` and the
  number into two separate dot-delimited identifiers — the numeric one is
  then compared numerically per spec, giving the correct order.
- **`v` prefix on tags** — release tags are `vMAJOR.MINOR.PATCH[-rc.N]`
  (e.g. `v1.0.0`, `v1.0.0-rc.1`, `v1.1.0`); the version numbers stored in
  `config.h`/`package.json` themselves do **not** carry the `v` prefix
  (matches SemVer's own convention and npm's existing expectation for
  `package.json`'s `version` field).

## Relationship to the Live Readiness Plan's own roadmap

The Live Readiness Plan's §11.9 table uses **"V1," "V2," "V3"...** as
**product-roadmap scope milestones** (e.g. "V1: Provisioning & Monitoring,"
built by "This plan, DM-Phase 0–6"), not as SemVer release numbers. Do not
conflate the two:

- The plan's "V1" scope is expected to correspond to `1.x.y` — the first
  real GitHub Release (once RE-9/RE-10 pass) is expected to be tagged
  `v1.0.0`, and any subsequent MINOR/PATCH releases that stay within "V1"
  scope (e.g. a bugfix, or a small addition that doesn't reach "V2: Cloud
  Fleet Management" territory) stay within the `1.x.y` line.
- A future jump to the plan's "V2" scope is *expected*, as a strong
  convention rather than a SemVer-mandated rule, to coincide with a MAJOR
  version bump to `2.0.0` — since V2 explicitly depends on `ADR-016`'s real
  production receiver replacing the bench-only `server.py`, which is
  exactly the kind of foundational change this policy's MAJOR criterion
  above describes.
- This alignment is a convention this project is choosing to keep, not an
  automatic consequence of either numbering scheme — restate it explicitly
  here rather than leave it as an assumption.

## Release tag strategy

- **One tag per release, covering firmware + desktop app together**
  (matches the lockstep policy above): `vMAJOR.MINOR.PATCH[-rc.N]`.
- Tags are the trigger for RE-7's release automation (tag push → build →
  publish to GitHub Releases). No tag is pushed manually outside that
  pipeline once RE-7 exists.
- `server.py` (the backend) is never tagged independently — see **Backend
  versioning** above.
- Release-candidate tags (`-rc.1`, `-rc.2`, ...) are dry runs: they exercise
  the full RE-7 pipeline and produce real downloadable artifacts, but are
  marked "Pre-release" in GitHub Releases and are not the artifact a
  technician in RE-10's end-to-end scenario would be pointed at. A
  workflow determines the "Pre-release" flag by checking whether the tag
  contains `-rc.` — GitHub does not infer this automatically from the tag
  name itself, so RE-7's workflow must compute it explicitly (ordinary
  workflow logic, not a change to this versioning policy).

## Artifact naming

All names below use the release tag's version number **without** the `v`
prefix inside the filename body but **with** it in the tag itself — e.g.
tag `v1.0.0` produces files named with `v1.0.0` in them, matching how this
project already writes version strings elsewhere (`FW_VERSION`).

**Audit fix (production-quality audit — artifact naming consistency):**
all four names below use one convention throughout — lowercase,
hyphen-separated ("kebab-case") — with no Title-Case/spaces anywhere. An
earlier draft of this table used `Covio-Device-Manager-Setup-...` (Title
Case) for the installer while the firmware/archive names were already
lowercase, an unexplained inconsistency across a set of names that should
read as one family. Lowercase-kebab was chosen project-wide because it
avoids case-sensitivity ambiguity across the OSes involved (the firmware
`.bin` is produced on whatever CI runner, the installer on Windows) and
avoids any shell-quoting/escaping concern in CI scripts — a concrete,
practical reason, not an arbitrary pick.

| Artifact | Filename pattern | Notes |
|---|---|---|
| Firmware binary (release build) | `covio-firmware-vX.Y.Z.bin` | Built from `[env:release]` (`-DRELEASE_BUILD=1`, RE-5). Board-qualified naming (`covio-firmware-esp32dev-vX.Y.Z.bin`) is the pattern to switch to if/when a second board target is ever added — not needed today (`esp32dev` is the only target). **Implemented and published by RE-7.** |
| Firmware debug symbols (ELF) | `covio-firmware-vX.Y.Z.elf` | The unstripped linker output — required for post-mortem crash/backtrace analysis against a `.bin` running in the field. Always produced by a successful build (standard GCC/PlatformIO toolchain behavior) — **required**, same as `.bin`. Added by RE-7. |
| Firmware linker map | `covio-firmware-vX.Y.Z.map` | Best-effort only — not every toolchain/framework combination emits a `.map` file without an explicit linker flag this project has not added (`Docs/FIRMWARE_BUILD.md`). Included in the release when produced; its absence is a warning, never a failure, and never blocks a release. Added by RE-7. |
| Firmware binary (factory-test build) | **Never published as a GitHub Release asset.** | `[env:factory]` (`-DFACTORY_TEST_BUILD=1`, RE-5) images are manufacturing-line-only, per ADR-008's own frozen requirement that "the factory-test image must never be the image that ships to a customer" — this extends to never appearing in a public/customer-facing GitHub Release either. Distributed through a separate, restricted manufacturing channel, out of RE-7's scope. |
| Windows installer (NSIS) | `covio-device-manager-setup-vX.Y.Z.exe` | electron-builder's own *default* NSIS naming is `${productName} Setup ${version}.${ext}` (spaces, no `v` prefix, Title Case) — overridden via `package.json`'s `build.nsis.artifactName` (RE-6). **Implemented and published by RE-7.** |
| Windows portable package | `covio-device-manager-vX.Y.Z-portable.exe` | electron-builder's `portable` target (RE-6, `package.json`'s `build.win.target`), `artifactName` overridden the same way. **Implemented and published by RE-7.** |
| Checksum (every artifact above) | `<artifact-filename>.sha256` | One per artifact, `<hash>  <filename>` format (standard `sha256sum`-compatible), generated at build time — firmware artifacts by `ci.yml`'s own `firmware` job (RE-5/RE-7), desktop artifacts by `scripts/afterAllArtifactBuild.js` (RE-6). All uploaded to the Release alongside the artifact they check. |
| Release archive (convenience bundle) | `covio-device-manager-vX.Y.Z-release.zip` | **Not implemented.** Originally sketched in RE-2 as a possible future "everything in one download" bundle; RE-7's actual, explicit scope asked for the individual artifacts above to be uploaded to the Release directly, not a separate curated archive on top of them — this row is left here as a disclosed, still-open *possible* future enhancement, not a gap in what RE-7 was asked to deliver. |

## Compatibility with GitHub Actions / GitHub Releases / future CI

- Tag format (`vMAJOR.MINOR.PATCH[-rc.N]`) matches the trigger pattern
  `.github/workflows/release.yml` (RE-7) actually uses:
  ```yaml
  on:
    push:
      tags:
        - 'v[0-9]+.[0-9]+.[0-9]+'
        - 'v[0-9]+.[0-9]+.[0-9]+-rc.[0-9]+'
  ```
  Two specific patterns, not a bare `'v*'` glob — a bare `'v*'` would also
  match any non-release tag that happens to start with "v" (e.g. an
  unrelated `vscode-config` tag), which these precise patterns avoid.
  `release.yml`'s own `determine-version` job additionally re-validates the
  pushed tag with a real bash regex
  (`^v[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$`) before doing anything else,
  since GitHub's own tag-filter glob syntax is not a full regex and
  shouldn't be fully trusted alone. Prerelease detection (`-rc.`
  substring) is ordinary workflow logic in `determine-version`, not
  something this versioning policy needs to encode further — see
  `Docs/RELEASE_AUTOMATION.md` for the full mechanism.
- Artifact names above contain no spaces or characters requiring escaping
  in a GitHub Actions upload step or a GitHub Releases asset URL.
- The lockstep single-tag-per-release model means `release.yml`'s pipeline
  has exactly one version number (`determine-version`'s own output) to
  thread through both the firmware build job and the desktop build job —
  no cross-referencing between two independent tag histories is ever
  needed for v1.

## Toolchain versions

See `VERSIONS.md` (repository root) — kept as a separate file per
`ADR-013`'s own explicit requirement ("the exact ... version ... is
recorded in a new file at the repository root"), not merged into this
document.
