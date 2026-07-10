# Desktop Packaging (Windows Installer & Portable Package)

**Status:** Living document (RE-6, Release Engineering — Desktop Packaging
& Windows Installer; extended by RE-7 — Automated GitHub Releases). Covers
`tools/device-manager/package.json`'s `build` section,
`scripts/afterAllArtifactBuild.js`, and
`.github/workflows/desktop-package.yml`, including RE-7's optional
release-version stamping.

## What this phase configures

| Target | electron-builder target | Artifact name (matches `Docs/RELEASE_VERSIONING.md`) |
|---|---|---|
| Windows installer | `nsis` | `covio-device-manager-setup-vX.Y.Z.exe` |
| Windows portable package | `portable` | `covio-device-manager-vX.Y.Z-portable.exe` |

Both are configured via `package.json`'s `build.win.target` (`["nsis",
"portable"]`) with per-target `artifactName` templates
(`build.nsis.artifactName` / `build.portable.artifactName`) using
electron-builder's own `${version}`/`${ext}` substitution — the literal
`v` prefix is written into the template string itself, since
electron-builder does not add it automatically.

Every packaged artifact gets a `.sha256` checksum file generated alongside
it, via electron-builder's `afterAllArtifactBuild` hook
(`scripts/afterAllArtifactBuild.js`) — see **Checksums** below.

Neither target is built by `.github/workflows/ci.yml` — packaging lives in
its own workflow, `.github/workflows/desktop-package.yml`, deliberately
kept out of the fast push/PR loop (this phase's own explicit scope). See
**GitHub Actions integration** below.

## Package metadata

| Field | Value | electron-builder config key |
|---|---|---|
| Product name | `Covio Device Manager` | `build.productName` (unchanged, already set) |
| Company | `Covio` | Inferred from `author` (unchanged, already `"Covio"`) |
| Executable name | `CovioDeviceManager.exe` | `build.win.executableName` (new — avoids the spaces in `productName`, which can cause quoting issues in shortcuts/scripts) |
| Version | `package.json`'s own `"version"` field | Used automatically by electron-builder for `${version}` substitution and the Windows PE version resource |
| Copyright | `Copyright © 2026 Covio` | `build.copyright` (new) |

**Version note (not changed by this phase):** `package.json`'s `"version"`
is still `0.1.0`, not yet aligned with `config.h`'s `FW_VERSION` (`1.0.0`)
— per `Docs/RELEASE_VERSIONING.md`'s own lockstep policy, both are aligned
together only when the first real release is actually cut (RE-7), not
preemptively. This phase's `artifactName` templates are correct regardless
of the current version number — they interpolate whatever
`package.json`'s version is at build time.

## Icon support

**No icon asset was added.** `tools/device-manager/build/README.md`
documents exactly what's needed (`build/icon.ico`, multi-resolution) and
the one-line `package.json` change to activate it once a real icon exists.
Designing an icon is a design deliverable, not an engineering one — this
matches this project's own established discipline of never fabricating a
placeholder that looks like a real decision (`certs.h`'s CA certificate,
`LICENSE`). Confirmed, via a real build attempt (see **Verification**
below), that leaving `icon` unset does not break packaging — electron-builder
logged `default Electron icon is used  reason=application icon is not
set` and continued normally.

## Checksums

`scripts/afterAllArtifactBuild.js` — electron-builder's own
`afterAllArtifactBuild` hook, called once after all configured Windows
targets have produced their artifacts. For each real artifact file (skips
`.blockmap` metadata files, which are never a distributable download), it
writes a `<filename>.sha256` file in the standard `sha256sum`-compatible
format (`<hash>  <filename>`) — verifiable with `certutil -hashfile <file>
SHA256` (Windows) or `sha256sum -c` (any platform with coreutils).

## Code signing (prepared, not active)

`forceCodeSigning: false` is set explicitly in `package.json` — self-
documenting the already-true default (electron-builder skips signing
when no certificate is configured) rather than relying on silent implicit
behavior. `.github/workflows/desktop-package.yml` passes
`CSC_LINK`/`CSC_KEY_PASSWORD` through from GitHub Secrets of the same
name — referencing a secret that doesn't exist yet evaluates to an empty
string at workflow run time, which electron-builder correctly treats as
"no certificate configured," so packaging succeeds unsigned today with
zero special-casing. When a real code-signing certificate exists (a
business decision, same category as `LICENSE`— not made here), the only
change needed is adding those two repository secrets in GitHub's own
settings; no workflow or `package.json` change is required.

## GitHub Actions integration

`.github/workflows/desktop-package.yml` — a workflow **separate** from
`ci.yml` (this phase's own explicit "keep release packaging separate from
CI" requirement), triggered by `workflow_dispatch` for a manual, on-demand
run. Runs on `windows-latest` (NSIS/portable packaging needs to happen on
a real Windows host, not cross-built), re-runs `npm test`/`npm run lint`
before packaging (packaging should never run against code that hasn't
already passed those gates, even though this workflow can be triggered
independently of any specific `ci.yml` run), then `npm run dist`, then
uploads the installer and portable package (each with its `.sha256`) as
two separate workflow artifacts. Does **not** publish a GitHub Release
itself.

**RE-7 addition — `workflow_call` reuse.** This workflow also gained a
second, additive trigger:

```yaml
on:
  workflow_dispatch: {}
  workflow_call:
    inputs:
      release_version:
        required: false
        type: string
```

This lets `.github/workflows/release.yml` (RE-7) invoke this entire job
graph via `uses: ./.github/workflows/desktop-package.yml` rather than
duplicating its packaging steps — GitHub Actions' reusable-workflow
mechanism runs the whole file, not a single job, when called this way. A
normal manual `workflow_dispatch` run is completely unaffected: it never
sets `release_version`, so the packaging steps below run exactly as they
always have.

When `release_version` **is** set (i.e. this workflow is running as part
of a real release, triggered by a version tag push), a new step —
**"Stamp release version into package.json (release builds only)"**,
placed after "Set up Node.js" and before "Install dependencies" — runs
`npm version ${{ inputs.release_version }} --no-git-tag-version
--allow-same-version` inside the ephemeral CI checkout. This makes
`package.json`'s own `"version"` field (and therefore electron-builder's
`${version}` substitution, used by both the `artifactName` templates and
the Windows PE version resource) actually equal the released version, not
just the eventual output filename. `npm version` (not a raw sed/regex
against JSON) is used specifically to avoid any risk of corrupting
`package.json`'s structure; `--no-git-tag-version` because this is a
never-committed, checkout-local edit, not a real repository change;
`--allow-same-version` so a retried workflow run against an
already-matching version doesn't error. See `Docs/RELEASE_AUTOMATION.md`
for the full release pipeline this feeds into, including why file-renaming
alone would not have been sufficient.

**Reconciling with `Docs/CI_WORKFLOW.md`'s earlier RE-6 note:** that
document's "Future extension points" section previously described RE-6 as
adding a `desktop-build` job *inside* `ci.yml`. This phase's actual,
explicit instructions said the opposite — "keep release packaging separate
from CI" — so the implementation here (a separate workflow file) is
correct per this phase's own authoritative scope; `Docs/CI_WORKFLOW.md`
has been updated to match, the same way RE-5's own entry there was
resolved once implemented. RE-7 later confirmed this was the right shape:
it reuses this file wholesale via `workflow_call` rather than needing it
restructured into `ci.yml`.

## Reproducibility

- Node.js, npm, Electron, and electron-builder versions are all pinned
  and recorded in `VERSIONS.md` (RE-2), used consistently by both
  `ci.yml` and `desktop-package.yml`.
- `npm ci` (not `npm install`) is used everywhere, matching the lockfile
  exactly rather than potentially resolving different transitive versions
  run to run.
- `directories.output` and `files` are both explicit, fixed values (not
  implicit glob-everything) — unchanged from earlier phases.
- **Not addressed by this phase**: byte-for-byte binary reproducibility
  (identical builds producing identical bytes, e.g. via `SOURCE_DATE_EPOCH`-
  style timestamp normalization) is a substantially deeper goal than
  "prepare packaging configuration" and is not claimed here — "reproducible"
  in this phase's scope means deterministic *steps* and *correct, consistent
  naming* given the same pinned inputs, not bit-identical output.

## Verification performed (and what could not be verified)

Real commands were actually run in this environment (Node.js/npm are
available — see `VERSIONS.md`):

- **`afterAllArtifactBuild.js`'s own logic**: invoked directly (not via
  electron-builder) against a real temp file — confirmed it computes a
  correct SHA256 (cross-checked against an independently computed hash),
  writes the checksum file in the documented format, and correctly skips
  `.blockmap` files. **This passed for real.**
- **`npx electron-builder --win`** (both configured targets) and
  **`npx electron-builder --dir --win`** (unpacked-only, no installer):
  both were attempted for real and **both failed** with an identical,
  environment-level error: `Cannot create symbolic link: A required
  privilege is not held by the client`, while electron-builder tries to
  extract its bundled `winCodeSign` archive (used internally by its
  Windows build pipeline for this electron-builder version, apparently
  unconditionally, regardless of `forceCodeSigning`/`CSC_IDENTITY_AUTO_DISCOVERY`
  settings — both were set and neither avoided it). This is a genuine
  Windows account permission restriction (`SeCreateSymbolicLinkPrivilege`,
  normally requiring Administrator elevation or Developer Mode) in *this*
  development environment, not a configuration defect — confirmed by the
  error recurring identically across two independently-attempted build
  modes. Toggling Windows Developer Mode or requesting elevation is a
  system-level change outside this phase's own scope to make unilaterally.
- **Consequence, stated honestly**: `package.json`'s exact metadata
  (`copyright`, `executableName`), the `artifactName` templates' actual
  interpolated output, and the full NSIS/portable packaging pipeline have
  **not** been end-to-end verified by a real successful build in this
  environment — only by careful configuration review and the isolated
  hook test above. The first real verification of the full pipeline will
  happen either on a Windows machine with Developer Mode enabled / an
  elevated account, or on a GitHub Actions `windows-latest` runner (which
  does not have this restriction) the first time
  `.github/workflows/desktop-package.yml` actually runs.
