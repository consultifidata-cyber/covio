# Release Automation (Automated GitHub Releases)

**Status:** Living document (RE-7, Release Engineering — Automated GitHub
Releases). Covers `.github/workflows/release.yml` and the small, additive
`workflow_call` extensions RE-7 made to `.github/workflows/ci.yml` (RE-5)
and `.github/workflows/desktop-package.yml` (RE-6).

**Honesty note, matching this project's own established discipline:** this
workflow has been validated by parsing it with a real YAML parser
(`js-yaml`, via a one-off `npx` invocation) and by manually verifying every
referenced job, path, and secret. It has **not** yet executed on a real
GitHub Actions runner — this repository has no remote configured yet
(`RE-1`'s own audit already disclosed this: zero commits, zero remote). Its
first real run happens the first time a `vX.Y.Z`-shaped tag is pushed to
this repository on GitHub. Because `release.yml` reuses `ci.yml` wholesale,
its first real run is also this project's first real firmware compile in
a *release* configuration — see `Docs/FIRMWARE_BUILD.md`'s own honesty note
for the underlying, still-open compiler-verification gap.

## Why a separate workflow, reusing the other two

RE-7's own explicit scope required two things that are in tension unless
resolved deliberately:

1. **"Keep release automation separate from CI"** — `release.yml` must not
   be folded into `ci.yml`'s own push/PR-triggered job graph.
2. **"Reuse existing workflows where appropriate instead of duplicating
   build logic"** — `release.yml` must not re-implement `ci.yml`'s firmware
   build steps or `desktop-package.yml`'s packaging steps a second time.

GitHub Actions' reusable-workflow mechanism (`workflow_call`) resolves
both: `ci.yml` and `desktop-package.yml` each gained one new, *optional*
trigger (`workflow_call`, with one optional `release_version` string
input) alongside their existing triggers. Neither file's existing
`push`/`pull_request`/`workflow_dispatch`-triggered behavior changed in any
way — a normal commit or PR still runs exactly the same jobs, exactly the
same way, with `release_version` simply absent (`''`). `release.yml` then
`uses: ./.github/workflows/ci.yml` and
`uses: ./.github/workflows/desktop-package.yml` directly, running each
file's **entire** job graph as-is rather than duplicating any step.

Calling `ci.yml`'s full suite (`repo-validation`, `firmware`, `desktop`,
`backend`, `code-quality`) as a release prerequisite — not a narrower,
release-only subset — is deliberate: a release should never publish
without the exact same full verification a normal push/PR gets.

## The version-stamping problem

Simply renaming a build output file to match a release tag (e.g.
`firmware.bin` → `covio-firmware-v1.2.3.bin`) would **not** make the
firmware's own embedded `FW_VERSION` (read by `ota.h`'s
`ver == FW_VERSION` OTA-update comparison and served in
`/api/v1/info`'s `fw_version` field) actually equal `1.2.3` — nor would it
make `package.json`'s own `"version"` field match. That would be a real
correctness bug disguised as a naming detail: a device could report itself
as running an old version while actually running the just-released one, or
OTA logic could compare against a stale value.

RE-7 resolves this with two small, conditional, ephemeral steps:

- `ci.yml`'s `firmware` job gains a **"Stamp release version into
  config.h"** step, `if: ${{ inputs.release_version != '' }}` — rewrites
  `config.h`'s `#define FW_VERSION "..."` line via `sed -i -E` before the
  `release` environment is compiled, so the compiled binary's own embedded
  version string is the released version, not just the output filename.
- `desktop-package.yml`'s `package` job gains a **"Stamp release version
  into package.json"** step, same condition — runs
  `npm version ${{ inputs.release_version }} --no-git-tag-version
  --allow-same-version` before `npm ci`/`npm run dist`, so
  electron-builder's own `${version}` substitution (used by both the
  `artifactName` templates and the Windows PE version resource) reflects
  the released version.

Both edits happen only inside the ephemeral CI checkout for that one run —
**neither is ever committed back to the repository.** A normal
`workflow_dispatch`/push/PR run of either workflow never sets
`release_version`, so `config.h` and `package.json` are left completely
untouched in every case except an actual release build triggered through
`release.yml`.

## Workflow architecture

```
   tag pushed: v1.2.3 or v1.2.3-rc.1
              │
              ▼
   ┌─────────────────────┐
   │  determine-version   │  parses the tag -> bare version + prerelease flag
   └──────────┬───────────┘
              │
              ▼
   ┌───────────────────────────────┐
   │  verify-and-build-firmware     │  uses: ./.github/workflows/ci.yml
   │  (reuses ALL of ci.yml:        │  with: release_version
   │  repo-validation, firmware,    │
   │  desktop, backend,             │
   │  code-quality)                 │
   └──────────┬─────────────────────┘
              │
              ▼
   ┌───────────────────────────────┐
   │  package-desktop                │  uses: ./.github/workflows/desktop-package.yml
   │  (reuses desktop-package.yml's  │  with: release_version
   │  package job)                   │
   └──────────┬─────────────────────┘
              │
              ▼
   ┌───────────────────────────────┐
   │  publish                        │  contents: write (only elevated job
   │  downloads all artifacts,       │  in this entire project)
   │  verifies exact names, then     │
   │  `gh release create`            │
   └─────────────────────────────────┘
```

Jobs run in a strict, linear `needs:` chain (not the parallel fan-out
`ci.yml`'s own internal jobs use) — a release is a single, ordered pipeline
where each stage's success is a precondition for the next, not a set of
independent concerns to run concurrently.

## Trigger rules

```yaml
on:
  push:
    tags:
      - 'v[0-9]+.[0-9]+.[0-9]+'
      - 'v[0-9]+.[0-9]+.[0-9]+-rc.[0-9]+'
```

Two specific patterns, not one loose wildcard — matches `vX.Y.Z` (stable)
or `vX.Y.Z-rc.N` (pre-release, dot-separated numeric identifier, per
`Docs/RELEASE_VERSIONING.md`'s own SemVer precedence note on why the dot
between `rc` and the number is mandatory) and nothing else. GitHub's own
tag-filter glob syntax is not a full regex, so `determine-version`'s own
first step re-validates the pushed tag against a real bash regex
(`^v[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$`) and fails loudly, with an
`::error::` annotation, on anything that doesn't match exactly — defense in
depth rather than trusting the trigger pattern alone.

## Prerelease detection

`determine-version`'s own parse step sets `is_prerelease=true` when the tag
contains `-rc.`, `false` otherwise. `publish`'s "Create GitHub Release"
step conditionally adds `gh release create`'s own `--prerelease` flag based
on that output — a stable `vX.Y.Z` tag is published as a full Release; a
`vX.Y.Z-rc.N` tag is published as a GitHub prerelease. This is the only
place prerelease status is decided; nothing about `ci.yml` or
`desktop-package.yml`'s own reused jobs treats a release-candidate build
any differently from a stable one (the same verification bar applies to
both).

## Permissions model

`release.yml` itself declares `permissions: contents: read` at the
workflow level, matching `ci.yml`'s and `desktop-package.yml`'s own
established least-privilege baseline. `determine-version`,
`verify-and-build-firmware`, and `package-desktop` all stay at
`contents: read` — the two `uses:` jobs pass `permissions: contents: read`
explicitly to the called workflow rather than relying on inheritance,
so a future edit to `ci.yml`'s or `desktop-package.yml`'s own default
permissions can never silently widen what a release run is granted. Only
`publish` — the single job that actually calls `gh release create` — is
elevated to `contents: write`. This is the **only** job, across every
workflow in this entire project (`ci.yml`, `desktop-package.yml`,
`release.yml`), ever granted write access. It uses `GITHUB_TOKEN`,
automatically provided by GitHub Actions for every workflow run — no
repository secret needs to be configured for this.

`publish` also explicitly declares `actions: read` alongside
`contents: write` — added during this phase's own self-audit. Once a
workflow declares any `permissions:` block, every scope not listed
defaults to `none`; `publish`'s first three steps
(`actions/download-artifact@v4`, pulling artifacts uploaded by the
upstream `verify-and-build-firmware`/`package-desktop` jobs in this same
run) should not silently rely on an ambient default that could change.
`actions: read` is itself a read-only scope, so this does not widen the
least-privilege posture — it only removes ambiguity about what `publish`
is actually granted.

## Checksum generation

Every released artifact has a `.sha256` file generated **at build time**,
close to the artifact itself, using two established, independent
mechanisms rather than one central pass in `release.yml`:

- **Firmware** (`.bin`, `.elf`, `.map` if produced): `ci.yml`'s `firmware`
  job (RE-5, extended by RE-7) — a bash loop,
  `sha256sum "$f" | sed "s|artifacts/||" > "$f.sha256"`, run once per file
  in the `artifacts/` directory.
- **Desktop** (installer, portable): `scripts/afterAllArtifactBuild.js`
  (RE-6) — electron-builder's own `afterAllArtifactBuild` hook.

Both produce the same `<hash>  <filename>` format (standard
`sha256sum -c`-compatible). `release.yml` never computes a checksum
itself — it only downloads what the two reused workflows already produced
and verifies (see below) that the expected files, checksums included, are
present before publishing.

## Artifact verification before publish

`publish`'s "Verify release assets match Docs/RELEASE_VERSIONING.md" step
does not trust that `actions/download-artifact` succeeding means the right
files exist under the right names — it explicitly checks for each of the
eight required files (firmware `.bin`/`.elf` + their `.sha256`s, installer
`.exe` + its `.sha256`, portable `.exe` + its `.sha256`) by exact,
version-interpolated path, failing the job with an `::error::` per missing
file if any are absent. `firmware.map` is checked separately and is
**never** required — its absence produces only a `::warning::`, matching
`Docs/FIRMWARE_BUILD.md`'s own "best-effort, not every toolchain/framework
combination emits one" disclosure. A release is never published
incomplete or misnamed.

## What gets published

`gh release create "$TAG" --title "Covio Device Manager $TAG"
--generate-notes [--prerelease] <firmware assets> <desktop assets>` —
GitHub's own `--generate-notes` (auto-generated release notes from merged
PRs/commits since the previous tag) is used rather than hand-authored
notes, since this repository has no PR history to draw from yet and no
mechanism in this phase maintains a hand-written notes file per release.
`CHANGELOG.md`'s existing per-phase entries remain the authoritative,
human-readable history; nothing in this phase requires them to be kept in
perfect lockstep with GitHub's auto-generated notes.

## Compatibility notes

- **With RE-5 (`ci.yml`'s `firmware` job)**: unchanged for its primary
  push/PR/workflow_dispatch use case; the new `workflow_call` trigger and
  the new conditional stamping step are strictly additive.
- **With RE-6 (`desktop-package.yml`)**: unchanged for its primary
  `workflow_dispatch` use case; same additive pattern.
- **With `Docs/RELEASE_VERSIONING.md`**: every filename `release.yml`
  checks for is generated by the naming table it defines
  (`covio-firmware-vX.Y.Z.bin`/`.elf`/`.map`,
  `covio-device-manager-setup-vX.Y.Z.exe`,
  `covio-device-manager-vX.Y.Z-portable.exe`, plus `.sha256` for each). The
  `-rc.N` dot-format tag pattern matches that document's own SemVer
  precedence note exactly.

## Not implemented in RE-7 (disclosed scope boundaries)

- **The `[env:factory]` (`FACTORY_TEST_BUILD=1`) image is never built,
  uploaded, or published by `release.yml`** — `ci.yml`'s `firmware` job
  still only builds `esp32dev` and `release`, per ADR-008's own frozen
  requirement that the factory-test image must never ship to a customer.
  This is unchanged from RE-5's own scope boundary, not a new gap.
- **The `covio-device-manager-vX.Y.Z-release.zip` "convenience bundle"**
  sketched in RE-2's own artifact-naming table is **not** produced by this
  phase. RE-7's actual, explicit objective list asked for the individual
  artifacts (firmware `.bin`/`.elf`/`.map`, installer, portable, their
  checksums) to be uploaded to the Release directly — not a separate
  curated archive bundling them again. `Docs/RELEASE_VERSIONING.md`'s own
  naming table discloses this row as a still-open, optional future
  enhancement rather than silently dropping it.
- **Code signing** remains exactly as RE-6 left it — prepared
  (`CSC_LINK`/`CSC_KEY_PASSWORD` passed through if the secrets exist) but
  inactive (no certificate configured yet). `release.yml` does not change
  this.
- **End-to-end execution on a real GitHub Actions runner** has not
  happened — this repository has no remote yet (see the honesty note
  above). The first real tag push is this project's first real test of
  the entire pipeline, including its first-ever release-configuration
  firmware compile.
