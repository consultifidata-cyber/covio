# CI Workflow (GitHub Actions)

**Status:** Living document (RE-4, Release Engineering — Continuous
Integration; extended by RE-5 — Firmware Build Automation; extended by
RE-7 — Automated GitHub Releases). Describes `.github/workflows/ci.yml`.
This workflow builds firmware (RE-5) but still does **not** build the
Windows installer itself and does **not** publish releases itself — those
happen in `.github/workflows/desktop-package.yml` (RE-6) and
`.github/workflows/release.yml` (RE-7), both of which **reuse** this
workflow's jobs via `workflow_call` rather than duplicating them (see
`Docs/RELEASE_AUTOMATION.md`) — this workflow's own `push`/`pull_request`/
`workflow_dispatch`-triggered behavior is completely unchanged by that
reuse.

**Honesty note:** this workflow has been validated by parsing it with a
real YAML parser (`js-yaml`, via a one-off `npx` invocation — no dependency
added to the project for this) and by manually verifying every referenced
command and file path exists. It has **not** yet executed on a real GitHub
Actions runner — this repository has no remote configured yet (`RE-1`'s own
audit already disclosed this: zero commits, zero remote). Its first real
run happens the first time this repository is pushed to GitHub. The
`firmware` job in particular represents this project's **first-ever real
compile** — see `Docs/FIRMWARE_BUILD.md` for the full honesty note on that.

## Workflow architecture

Five jobs, each independently scoped to one concern (`repo-validation`,
`firmware`, `desktop`, `backend`, `code-quality`) — matching the same
one-concern-per-unit discipline this project already applies to DM-Phases
and RE-Phases themselves.

```
                    ┌──────────────────┐
                    │  repo-validation │  (ubuntu-latest, fastest, runs first)
                    └─────────┬────────┘
              ┌────────────────┼────────────────┬────────────────┐
              ▼                ▼                ▼                ▼
       ┌─────────────┐  ┌─────────────┐  ┌───────────┐  ┌────────────────┐
       │  firmware   │  │   desktop   │  │  backend  │  │  code-quality   │
       │ (ubuntu-    │  │ (matrix:    │  │ (ubuntu-  │  │  (ubuntu-latest)│
       │  latest)    │  │  windows +  │  │  latest)  │  │                 │
       │             │  │  ubuntu)    │  │           │  │                 │
       └─────────────┘  └─────────────┘  └───────────┘  └─────────────────┘
```

- **`repo-validation`** (`ubuntu-latest`): checks out the repo, verifies a
  fixed list of required files/directories exist (firmware, backend,
  desktop app, RE-1/RE-3 hygiene/quality files), then separately verifies a
  fixed list of required documentation files exist. Two separate steps
  (structure vs. documentation) so a failure clearly identifies which
  category broke, with an `::error::` annotation naming the *exact* missing
  path — not a generic "something's missing."
- **`firmware`** (RE-5, `ubuntu-latest`): installs the pinned PlatformIO
  Core (`VERSIONS.md`), builds the `esp32dev` (bench/dev) and `release`
  environments, extracts `FW_VERSION` from `config.h`, renames the
  `release` build's output to `covio-firmware-vX.Y.Z.bin`
  (`Docs/RELEASE_VERSIONING.md`'s naming table), and uploads it as a
  workflow artifact. Does **not** build or upload the `factory` environment
  (defined in `platformio.ini`, but RE-5's own explicit scope is to
  prepare, not yet implement, factory-build CI support — see
  `Docs/FIRMWARE_BUILD.md`). See **Failure policy** below for why any
  compile failure here is treated as a hard, blocking, high-priority signal.
- **`desktop`**: `npm ci` → `npm run lint` (ESLint) → `npm test`. Matrix
  across `windows-latest` and `ubuntu-latest` — Windows because it's the
  app's actual shipped target; Ubuntu because the underlying Node logic is
  genuinely cross-platform (verified by reading `wifiScan.js`'s own
  `process.platform !== 'win32'` branch, which degrades to
  `{supported: false}` rather than throwing — this isn't an assumption).
- **`backend`**: installs Python 3.11, installs Flask (server.py's only
  runtime dependency, unpinned — matches `README.md`'s own existing
  instruction and `VERSIONS.md`'s disclosed not-yet-pinned status), then
  runs `python -m unittest discover -s test/native -v`. **Fails the job on
  any test failure** (unittest's own exit-code behavior — no special
  handling needed). This is the first environment in this project's
  history where these ~46 tests will actually execute (no Python
  interpreter has been available anywhere else this project was
  developed — every backend DM-Phase's own audit disclosed this).
- **`code-quality`**: runs Prettier's format check (Node) and ruff's lint
  check (Python) in one job, since neither needs the other's build output —
  purely a scheduling convenience, not a dependency. See **Failure policy**
  below for why these two checks behave differently on failure.

## Job dependencies

All four heavier jobs (`firmware`, `desktop`, `backend`, `code-quality`)
declare `needs: repo-validation` — none of them runs if the repository's
basic structure is already broken. This is a deliberate fail-fast choice:
it would waste CI minutes (and, on `windows-latest` runners, real cost) to
run a full firmware/Node/Python matrix against a checkout missing
`platformio.ini` or `package.json` or `test/native/`. `firmware`,
`desktop`, `backend`, and `code-quality` do **not** depend on each other
and run fully in parallel once `repo-validation` passes.

## Required GitHub secrets

**None, for `ci.yml` itself.** Every job in this workflow operates entirely
on the checked-out repository's own content plus publicly-available
package registries (npm, PyPI) — no API keys, tokens, or credentials of
any kind are read, including its `firmware` job (RE-5/RE-7) — confirmed it
never reads a secret, even when called via `workflow_call` with
`release_version` set.

**`desktop-package.yml` (RE-6) references two *optional* secrets**:
`CSC_LINK`/`CSC_KEY_PASSWORD` (code-signing certificate + password,
electron-builder's own convention). Neither is configured in this
repository yet — referencing an unset GitHub secret evaluates to an empty
string at workflow run time, which electron-builder correctly treats as
"no certificate," so packaging succeeds unsigned with zero special-casing
today. See `Docs/DESKTOP_PACKAGING.md`.

**`release.yml`'s `publish` job (RE-7) is the first, and only, job in this
entire project that reads and requires a secret that's actually *needed*
rather than optional**: `GITHUB_TOKEN`, used as `gh release create`'s own
`GH_TOKEN` environment variable. It is automatically provided by GitHub
Actions for every workflow run — **not** a repository secret that needs to
be manually configured. `publish` is also the only job, across all three
workflows in this project, ever granted `permissions: contents: write`;
every other job in every workflow remains `contents: read`. See
`Docs/RELEASE_AUTOMATION.md` for the full permissions model.

## Failure policy

| Check | Blocking? | Rationale |
|---|---|---|
| Repository structure / documentation validation | **Yes** | A missing required file is always a real regression — no legitimate reason for it to be absent. |
| Firmware compile (`esp32dev`, `release`) | **Yes** | This is this project's first-ever real compile (`VERSIONS.md`/`Docs/FIRMWARE_BUILD.md`) — a failure is exactly the highest-value thing this job exists to catch, not noise to tolerate. |
| ESLint (`npm run lint`) | **Yes** | 0 errors exist today (RE-3's own verified baseline) — any new one is a real regression, not pre-existing noise. |
| Desktop app tests (`npm test`) | **Yes** | 44/44 pass today (verified repeatedly throughout this project) — a failure here is never expected. |
| Python native test suite | **Yes** | Explicit RE-4 requirement ("fail on Python test failures"); also this project's first-ever real execution of these tests, so a failure is exactly the kind of thing CI exists to catch. |
| `ruff check` | **Yes** | A real-bug-catching tool (unused imports, undefined names), same reasoning as ESLint. |
| `prettier --check` | **No (`continue-on-error: true`)** | `Docs/CODE_QUALITY.md`'s own already-documented policy: all 27 tracked desktop-app source files currently differ from Prettier's output (verified in RE-3), since the codebase has never been run through a formatter. Blocking on this today would fail every single CI run before a single new line of code is ever involved. Reported as a `::warning::` annotation instead, so it's visible without being a hard gate — becomes blocking only after a dedicated, isolated formatting-normalization change (its own reviewed diff, per `CONTRIBUTING.md`) brings the tree into compliance. |

**Distinguishing formatting from lint failures**, per RE-4's own explicit
requirement: `code-quality`'s Prettier step and its "Report Prettier
result" step are separate from the `ruff check` step — a formatting-only
issue shows as a `::warning::` on a job that otherwise passes; a lint
issue (ESLint or ruff) fails the job outright. These are never merged into
one pass/fail signal.

## Branch protection recommendations

Once this repository has a real remote and `main` branch (see
`CONTRIBUTING.md`'s own disclosed pre-remote state), the repository owner
should configure branch protection on `main` requiring, at minimum:
- `Repository Validation` — required status check.
- `Firmware` — required status check.
- `Desktop App (windows-latest)` and `Desktop App (ubuntu-latest)` —
  required status checks (both matrix legs, not just one).
- `Backend (Python)` — required status check.
- `Code Quality` — required status check (note: this job can still
  *pass* with a Prettier warning present, per the failure policy above —
  requiring it as a status check does not accidentally make formatting
  blocking).
- At least one PR review approval before merge (`CONTRIBUTING.md`'s own
  "no direct pushes to main" policy, now backed by an enforceable rule
  rather than only a documented convention).
- "Require branches to be up to date before merging" — avoids merging a PR
  whose CI run predates a since-landed breaking change on `main`.

This is a recommendation for the repository owner to configure via GitHub's
own branch protection settings — not something this workflow file can
configure itself.

## Extension history (RE-5, RE-6, RE-7 — all implemented)

Previously this section documented forward-looking extension points for
RE-5/RE-6/RE-7 so each phase would extend the existing workflow rather
than restructure it. All three are now implemented, and each entry has
been removed from here once landed, matching this project's own
established documentation discipline (an implemented extension point is
described in its own living document, not left as a stale forward-looking
note):

- **RE-5 (Firmware Build Automation)** — the `firmware` job, above. See
  `Docs/FIRMWARE_BUILD.md`.
- **RE-6 (Desktop Packaging & Windows Installer)** — a **separate**
  workflow file, `.github/workflows/desktop-package.yml` (RE-6's own
  explicit "keep release packaging separate from CI" instruction, not a
  job appended to `ci.yml`). See `Docs/DESKTOP_PACKAGING.md`.
- **RE-7 (Automated GitHub Releases)** — a **separate**,
  tag-triggered workflow, `.github/workflows/release.yml`, which *reuses*
  both `ci.yml` (this file) and `desktop-package.yml` via GitHub Actions'
  `workflow_call` mechanism rather than duplicating their build/packaging
  steps — see the `workflow_call` trigger and its `release_version` input
  in the `on:` block above, and the `firmware` job's own conditional
  "Stamp release version into config.h" step. Full architecture,
  permissions model, and trigger rules: `Docs/RELEASE_AUTOMATION.md`.

No further extension points are currently planned beyond RE-7 — a future
RE-8+ phase, if any, would document its own needs here when defined.
