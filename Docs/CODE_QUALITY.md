# Code Quality Policy

**Status:** Living document (RE-3, Release Engineering — Code Quality
Gates & Static Analysis). Configuration and policy only: this phase adds
tooling and documents how it will be used; it does **not** reformat any
existing file. See `CONTRIBUTING.md`'s own **Coding standards** section for
the informal conventions this tooling now formalizes.

## Tooling by component

| Component | Formatter | Linter | Config file(s) |
|---|---|---|---|
| Desktop app (`tools/device-manager/`) | Prettier | ESLint | `.prettierrc.json`, `.prettierignore`, `eslint.config.js` |
| Backend (`server/`, `test/native/`) | ruff format | ruff check | `pyproject.toml`, `requirements-dev.txt` |
| Firmware (root `.h`/`.ino`) | clang-format | *(not yet added — see Exceptions)* | `.clang-format` |
| All components | — | — | `.editorconfig` (indent width/style, line endings, charset) |

## Formatting policy

- **Desktop app**: `npm run format:check` (Prettier, non-mutating) is the
  canonical check; `npm run format` (Prettier `--write`) is the canonical
  fix. Style: single quotes, semicolons, 2-space indent, 100-column width,
  trailing commas — chosen to match this codebase's own already-consistent
  hand-written style as closely as possible (see `.prettierrc.json`).
- **Backend**: `ruff format --check .` / `ruff format .` (once installed —
  see **Toolchain** below). Style: double quotes, 4-space indent,
  100-column width — matches Python/PEP 8 convention and this codebase's
  own existing style.
- **Firmware**: `clang-format --dry-run` / `clang-format -i` against the
  root `.h`/`.ino` files (once a C++ toolchain exists to run it — see
  **Toolchain** below). `.clang-format` is tuned to match this codebase's
  existing 2-space, attached-brace, left-aligned-pointer style, and
  explicitly disables comment reflowing (`ReflowComments: false`) so it
  doesn't fight this project's own deliberate long-form prose comment
  convention (`CONTRIBUTING.md`'s "explain the why" standard).

## Lint policy

- **Desktop app**: `npm run lint` (ESLint, flat config, ESLint 9+). Two
  rule sets — CommonJS main-process code (Node globals) and ES-module
  renderer code (browser globals only, no Node access) — matching this
  app's own two-process security architecture (`main.js`'s own header
  comment). `no-restricted-globals` explicitly forbids `require`/`process`
  in renderer code as a linted, not just documented, security boundary.
- **Backend**: `ruff check .` (pyflakes-equivalent real-bug rules — unused
  imports, undefined names — plus pycodestyle style rules and import
  sorting).
- **Firmware**: no static-analysis linter (`clang-tidy`/`cppcheck`) is
  added by this phase — see **Exceptions** below for why.

## When CI should fail (for RE-4 to implement)

This phase does not itself add CI — these are the gating rules RE-4's
workflow should implement, decided now so RE-4 doesn't have to invent
policy while wiring automation:

| Check | Blocks CI? | Why |
|---|---|---|
| ESLint errors | **Yes, immediately** | Zero errors exist today (verified — see **Verification results** below); any new one is a real regression. |
| ESLint warnings | **Not yet** | 3 genuine pre-existing warnings exist today; blocking on them now would fail CI before any new code is even involved. Revisit once those 3 are cleaned up in their own dedicated change. |
| `ruff check` errors | **Yes, once RE-4's runner can execute it** | Same reasoning as ESLint errors — real-bug-catching rules should gate immediately once they can run at all. |
| `prettier --check` | **Not yet** | All 27 existing tracked source files currently differ from Prettier's output (verified below) — the codebase has never been run through a formatter. Blocking on this today would fail CI on 100% of the existing codebase before a single new line is written. Becomes a blocking gate only after a dedicated, isolated formatting-normalization change (its own reviewed diff, per `CONTRIBUTING.md`'s "never bundle unrelated work" standard) brings the tree into compliance. |
| `ruff format --check` | **Not yet, same reasoning** | Not yet run (no Python interpreter available to verify current compliance — see **Verification results**). |
| `clang-format --dry-run` | **Not yet** | No C++ compiler/toolchain has ever been available to run this or establish a compliance baseline (see `VERSIONS.md`). |

## Exceptions

- **`catch (_e)` unused-error convention**: this codebase already uses a
  leading-underscore name for an intentionally-unused caught error
  throughout `deviceClient.js`/`backendClient.js`/`deviceStore.js`/
  `discovery.js`. ESLint's `caughtErrorsIgnorePattern: '^_'` recognizes
  this explicitly — it is a convention, not a lint violation to silence
  individually.
- **Firmware numeric literals** (GPIO pin numbers, timing constants):
  intentionally not treated as "magic numbers" needing extraction into
  named constants case-by-case — most already are named via `#define` in
  `config.h`; the remainder are inherent, self-evident hardware constants
  a `readability-magic-numbers`-style rule would flag pedantically without
  adding real value. This is why no `clang-tidy` config is added by this
  phase (see below).
- **No C++ static-analysis linter (`clang-tidy`/`cppcheck`) added yet**:
  RE-3's own explicit objective list names `.clang-format` (formatting)
  for firmware but not a separate static-analysis tool. Rather than
  silently expand scope, this is recorded as a deliberate, disclosed gap
  for a future phase to pick up if wanted — not an oversight.
- **Backend dense one-liners** (`server.py`'s multi-import lines, inline
  SQL parameter tuples): an existing, reviewed style choice, not something
  a line-length/style warning should be reflexively "fixed" if it recurs
  once ruff actually runs.
- **`package-lock.json`**: excluded from Prettier (`.prettierignore`) —
  machine-generated, npm's own tool is the only thing that should ever
  rewrite it.

## Generated files excluded

Matches `RE-1`'s `.gitignore`/`tools/device-manager/.gitignore` exactly —
nothing generated is ever a formatting/lint target: `node_modules/`,
`dist/`, `out/`, `.pio/`, `__pycache__/`, `.venv/`/`venv/`,
`package-lock.json` (Prettier only — npm itself still owns it).

## Toolchain

- ESLint `^9.9.0`, `@eslint/js` `^9.9.0`, Prettier `^3.3.0` — added as real
  `devDependencies` in `tools/device-manager/package.json`, installed and
  run for real as part of this phase (see **Verification results**).
- `ruff>=0.6.0` — `requirements-dev.txt` (backend). **Not installed or run
  in this environment** — no Python interpreter has been available in any
  environment this project has been developed in so far (see
  `VERSIONS.md`). To be installed and run for the first time by RE-4's CI
  runner, which does have Python preinstalled.
- `clang-format` — no version recorded; **not installed or run in this
  environment** — no C++ compiler/toolchain has ever been available (see
  `VERSIONS.md`). To be run for the first time once RE-5 (Firmware Build
  Automation) establishes a working compiler toolchain in CI.

## Verification results (this phase, real runs)

Real commands actually executed against the real, current codebase in this
environment (Node.js/npm are available here — see `VERSIONS.md`; Python
and a C++ toolchain are not, so those two rows are honestly marked
unexecuted rather than guessed):

| Command | Result |
|---|---|
| `npx eslint .` | **0 errors, 3 warnings** (all pre-existing, on unmodified application code — see `RE-3`'s own report for exact locations). Two real bugs found *in this phase's own new `eslint.config.js`* during this same run were fixed before this final result (see the phase report). |
| `npx prettier --check .` | **27 of 27 tracked source files differ from Prettier's output.** Expected and disclosed, not a regression — this codebase has never been run through a formatter; see **When CI should fail** above for the rollout plan. |
| `ruff check .` / `ruff format --check .` | **Not executed** — no Python interpreter available in this environment. |
| `clang-format --dry-run` | **Not executed** — no C++ toolchain available in this environment. |
