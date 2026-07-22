# Contributing to the Covio Device Manager Project

This repository spans three artifacts developed together: the ESP32
firmware (repo root), the bench/reference backend (`server/`), and the
Windows desktop app (`tools/device-manager/`). This document covers how
work moves through the repository. It does **not** cover local environment
setup (toolchain installation, build/test/release commands) — see
`SETUP.md` (RE-8, Release Engineering — Developer Onboarding &
Reproducible Environment) for that.

## Branch strategy

**Current actual state, stated honestly (audit fix, RE-1):** as of this
document's own creation, this repository has zero commits and no configured
remote — every DM-Phase 0B through DM-Phase 6, and RE-1 itself, exist only
as an uncommitted local working tree, with `HEAD` pointing at an unborn
`refs/heads/master`. Nothing below is already-active practice; it is the
intended model for once this repository is first pushed to a remote (most
likely GitHub) and its history actually begins. Do not assume `main`
already exists as a populated branch anywhere until whoever performs that
first push confirms it.

- **`main`** is the intended trunk branch once the repository has a remote.
  It is meant to be the PR target for all changes and to always be in a
  working, reviewable state. Whether the very first push creates `main`
  directly, or renames an initial `master` to `main`, is a one-time
  repository-setup decision for whoever performs it — not something this
  document resolves in advance.
- Work happens on short-lived topic branches, one branch per DM-Phase, RE-
  Phase, or standalone fix — never a long-lived branch accumulating
  unrelated work. Suggested prefixes, matching this project's own existing
  phase vocabulary:
  - `dm/<n>-<short-description>` — a Device Manager implementation phase
    (e.g. `dm/7-...` for whatever follows DM-Phase 6).
  - `re/<n>-<short-description>` — a Release Engineering phase (e.g.
    `re/1-repo-hygiene`).
  - `fix/<short-description>` — a scoped bugfix outside any phase.
  - `chore/<short-description>` — non-functional maintenance (docs,
    formatting, etc.).
- **Release branches, hotfix branches, and tagging conventions** — decided
  in `Docs/RELEASE_VERSIONING.md` (RE-2) and implemented by
  `.github/workflows/release.yml` (RE-7, see `Docs/RELEASE_AUTOMATION.md`):
  there are no separate release branches — `main` plus SemVer tags
  (`vMAJOR.MINOR.PATCH[-rc.N]`) is the model, and pushing a tag matching
  that pattern is what triggers a release (`SETUP.md` §11). This repository
  has not cut a real release yet (no remote configured — see above), so
  this remains unexercised in practice, not undecided in policy.

## Development workflow

Applies from the point this repository has a remote and a real `main`
branch (see **Branch strategy** above for the current, pre-remote state):

1. Branch from `main` using the naming convention above.
2. Make the change. If it touches firmware, the backend, or the desktop
   app, run the relevant test suite locally before opening a PR (see
   **Coding standards** below for what "relevant" means per area, and
   `SETUP.md` for the exact commands and toolchain setup).
3. Open a PR against `main` (see **Pull request expectations**).
4. Address review feedback. Once approved, merge — no direct pushes to
   `main` for anything beyond the repository-hygiene files this document
   itself is part of.

## Pull request expectations

Every PR description should state, explicitly:

- **Which unit of work this is** — a DM-Phase, an RE-Phase, or a standalone
  fix — and a one-line summary of its objective. This project's whole
  history (DM-Phase 0B through DM-Phase 6, and the Release Engineering
  phases that follow) has been implemented and reviewed one phase at a
  time, each with its own explicit scope; PRs should preserve that
  discipline rather than bundling unrelated phases or fixes together.
- **What existing interfaces/contracts must remain unchanged**, and
  confirmation they were not touched. This project treats several things as
  frozen unless a phase explicitly authorizes a change: the local API
  contract (`Docs/Covio_Device_Manager_Live_Readiness_Plan.md` §13 Appendix
  A), the backend's device-facing wire contract (`push`/`config`/
  `ota/manifest`), and any `APPROVED` ADR in
  `Docs/Firmware Detailed Architecture Decision Record (ADR).md`.
- **Tests run and their results** — which suites were executed, and for
  anything that couldn't be executed in the author's environment (e.g. no
  local Python interpreter, no ESP32 hardware), say so explicitly rather
  than silently omitting it. This project has a long, explicit history of
  disclosing exactly this kind of environment gap rather than papering over
  it — keep doing that.
- **Any new technical debt or known limitation introduced**, however small.

A PR that touches firmware source should note whether it was compiled
locally (`SETUP.md` §3/§9) in addition to CI's own automatic compile
(**RE-5**'s `firmware` job) — CI compiling it is not a substitute for
disclosing whether you *also* verified it locally, particularly since this
project's own toolchain pin has not yet been compiler-verified in every
environment (see `VERSIONS.md`).

## Coding standards

Automated linting/formatting exists (**RE-3** — ESLint/Prettier for the
desktop app, ruff for the backend, clang-format for firmware; see
`Docs/CODE_QUALITY.md` and `SETUP.md` §§7/9/10 for the exact tools and
commands) and runs in CI (**RE-4**). Beyond what those tools check
automatically, new code should match the conventions already consistently
used throughout this codebase:

- **Explain the *why*, not the *what*.** Comments exist for non-obvious
  reasoning, frozen-contract references, and decisions a future reader
  would otherwise have to reverse-engineer — not for narrating what a line
  of code already makes obvious.
- **Fail loud, never silently.** A rejected/malformed input is quarantined,
  logged, or returned as an explicit error — never dropped without a trace.
- **Honest `null`, never fabricated.** A value that can't yet be computed
  (no data source, unimplemented dependency) is reported as `null`/"not yet
  available," never guessed or defaulted to something that looks like real
  data.
- **No new dependency without justification stated in the PR.** This
  applies most strictly to firmware (no new library without a documented
  reason — see `Docs/MASTER_GOVERNANCE.md` §3) and applies with ordinary
  engineering judgment elsewhere (backend, desktop app).
- **Don't duplicate logic across files.** If two places need the same
  parsing/formatting/validation, share one implementation (this project has
  paid for that mistake once already — see `ota.h`'s `extractStr_` being
  promoted to shared use rather than reimplemented a third time).
- **Preserve write-once/idempotent semantics where they exist** (e.g. the
  Logical Device ID, DM-Phase 6) — don't quietly relax them for convenience.

## Release Engineering workflow

Release Engineering (RE-Phases) follows the same one-phase-at-a-time model
as the DM-Phases before it:

1. A phase's objective and scope are agreed before implementation starts.
2. Only that phase's explicitly listed files/objectives are touched — nothing
   from a later phase, even if it seems convenient to do at the same time.
3. After implementation, a self-audit (or an independent audit) is performed
   against that phase's own acceptance criteria before the next phase
   begins.
4. Any Critical or High finding from that audit is fixed, and the audit is
   repeated, before the phase is considered closed.
5. The next RE-Phase does not begin until the current one is explicitly
   closed.

## DM/RE governance

- **`Docs/MASTER_GOVERNANCE.md`** is the authoritative governance document
  for firmware architecture decisions specifically — its Absolute Rules
  (never redesign a frozen ADR, never skip required tests, never ship a
  schema change without updating `SCHEMA_REGISTRY.md`, etc.), its ADR
  supersession mechanism (a new ADR marks an old one "superseded," never
  deletes it), and its ACR workflow (Architecture Compliance Report, for an
  implementation-discovered impracticality — distinct from a deliberate
  supersession) all still apply and are not restated here.
- **DM-Phases** (Device Manager implementation phases) are the unit of
  product-functionality work; **RE-Phases** (Release Engineering phases,
  this document's own numbering) are the unit of release/process work that
  follows once DM-Phase implementation is complete. Neither begins the next
  phase without the current one's acceptance criteria having been met and
  audited.
- When a phase's implementation surfaces a conflict with a frozen ADR or an
  environment limitation it cannot resolve within its own scope (e.g. no
  compiler/hardware available to verify a claim), the correct response is
  to stop and report it — not to proceed on an unverified assumption or to
  quietly redesign the frozen decision.
