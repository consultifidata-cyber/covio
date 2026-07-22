# Firmware Build

**Status:** Living document (RE-5, Release Engineering — Firmware Build
Automation; extended by RE-7 — Automated GitHub Releases). Covers
`platformio.ini`'s three build environments and how
`.github/workflows/ci.yml`'s `firmware` job builds them, including RE-7's
optional release-version stamping and the `.elf`/`.map`/`.sha256`
artifacts it now produces alongside `.bin`.

**Honesty note, stated up front, matching this project's own established
discipline throughout every prior phase:** this document describes the
**first real compile this project's firmware has ever undergone.** No
compiler or PlatformIO toolchain has been available in any environment
this project (DM-Phase 0B through DM-Phase 6, and RE-1 through RE-4) was
developed and reviewed in — every firmware change across that entire
history was verified by careful manual code review only, never by a
compiler. The toolchain pin below is a **reasoned, deliberate choice**, not
a **confirmed-working** claim. RE-5's own CI job is the first thing that
will actually tell us whether it's right.

## The three build environments

| Environment | Flags | Purpose | Built by CI? | Uploaded? |
|---|---|---|---|---|
| `esp32dev` | *(none — config.h's own defaults)* | Bench/dev build — the environment every DM-Phase in this project was actually implemented and reviewed against. | Yes | No (verification only) |
| `release` | `-DRELEASE_BUILD=1` | "The production firmware" (DM-Phase 5/ADR-005) — refuses to boot with the default API key active; `certs.h` refuses to compile with its placeholder CA cert in place. | Yes | **Yes** — `covio-firmware-vX.Y.Z.bin` (required), `.elf` (required, RE-7), `.map` (best-effort, RE-7), plus a `.sha256` for each |
| `factory` | `-DFACTORY_TEST_BUILD=1` | Factory-only build (DM-Phase 6/ADR-008) — compiles in `POST /api/v1/factory/provision`. | **No** (defined, not yet built by CI — see below) | Never (ADR-008: "must never be the image that ships to a customer") |

All three inherit a single, shared `[env]` base section in `platformio.ini`
(board, framework, partition scheme, source filter, and — critically — the
platform version pin) so they can never silently drift apart from each
other. This is `ADR-013`'s own "pinned toolchain" requirement, applied
consistently across all three variants rather than to just one.

### Why `factory` is defined but not built by CI yet

RE-5's own explicit scope is to *prepare*, not yet *implement*, support for
`FACTORY_TEST_BUILD`. The environment exists in `platformio.ini` — a future
phase can build it with zero additional configuration — but no CI job
compiles or uploads it yet. This is a deliberate scope boundary, not an
oversight: `ADR-008` requires this image never leave the factory floor as
a customer-facing artifact, which means it needs different handling than
routine per-push CI verification (e.g. restricted distribution, not a
public build artifact) — a decision for a later phase, not this one.

## Toolchain pin

See `VERSIONS.md` for the authoritative record. Summary:

- **PlatformIO Core**: `platformio==6.1.15` (exact pin, installed via pip
  in CI).
- **PlatformIO platform** (`espressif32`): `@6.5.0` (exact pin, in
  `platformio.ini`'s `[env]` section) — bundles Arduino-ESP32 core
  approximately `2.0.14`, a mature, long-stable 2.x-generation core.
- **Board**: `esp32dev` (generic ESP32 Dev Module) — unchanged.
- **Partition scheme**: `min_spiffs.csv` (bundled with the platform itself,
  not a project-local file) — unchanged, required for OTA's two-app-slot
  layout.

### Why this specific platform version

Chosen for API compatibility with what this firmware actually uses —
`WiFiClientSecure` (DM-Phase 5), `ESPmDNS` (DM-Phase 1), `Preferences`
(store.h), `esp_ota_ops.h` (ota.h's rollback mechanism), `WebServer`/
`DNSServer` (DM-Phase 1/2's local API and captive portal), `HTTPUpdate`
(OTA) — all long-stable APIs present across the 2.x Arduino-ESP32 core
line, not requiring anything from the newer, more disruptive 3.x core
generation. A mature, conservative pin was chosen deliberately over
"whatever's newest" specifically *because* this is a first, unverified
compile — a narrower gap between "what the code was written against" and
"what actually gets compiled" reduces the number of plausible failure
causes if something doesn't build cleanly on the first attempt.

### If the first real CI run fails

Treat it as a genuine discovery exercise (already flagged as the expected
posture for this exact moment, back in the original Release Engineering
roadmap), not a workflow bug:

1. Read the actual compiler error from the CI log — it will name the exact
   file/line/API involved.
2. Fix *only* what the error identifies, re-entering the relevant DM-Phase's
   own governance for that file (explain why, cite which phase it belongs
   to) — never a speculative, broader rewrite.
3. If the error is version-specific (an API that changed between core
   versions), the fix may be a one-line version bump in `platformio.ini`'s
   `[env]` section, which every environment inherits automatically — update
   `VERSIONS.md`'s pin and the reasoning above to match.

## How CI builds this

`.github/workflows/ci.yml`'s `firmware` job (`needs: repo-validation`,
`ubuntu-latest`):

1. Installs PlatformIO Core (pinned, above).
2. Caches `~/.platformio` (PlatformIO's downloaded platform/toolchain
   packages — large, and unchanged unless the pinned platform version
   changes, which the cache key already accounts for via
   `hashFiles('platformio.ini')`).
3. **(RE-7, release builds only)** If this job was invoked via
   `workflow_call` with a non-empty `release_version` input (i.e. it's
   running as part of `.github/workflows/release.yml`, triggered by a
   version tag push — see `Docs/RELEASE_AUTOMATION.md`), rewrites
   `config.h`'s `#define FW_VERSION "..."` line to that exact version
   *before* compiling, so the compiled binary's own embedded version
   string — not just the output filename — matches the released tag. A
   normal push/PR/manual `workflow_dispatch` run never sets this input, so
   `config.h` is left untouched in every other case. This edit happens
   only in the ephemeral CI checkout and is never committed back to the
   repository.
4. `pio run -e esp32dev` — compile-verify the bench/dev build.
5. `pio run -e release` — compile-verify the production build.
6. **"Prepare versioned artifacts"** — extracts `FW_VERSION` from
   `config.h` (picking up the RE-7 stamped value automatically if step 3
   ran) and, into an `artifacts/` directory:
   - Copies `.pio/build/release/firmware.bin` →
     `covio-firmware-vX.Y.Z.bin` — **required**; the job fails loudly if
     this file doesn't exist where PlatformIO's own standard convention
     says it should.
   - Copies `.pio/build/release/firmware.elf` →
     `covio-firmware-vX.Y.Z.elf` — **required** (RE-7). This is
     PlatformIO's/GCC's own standard linker output for any successful
     build (the unstripped binary with debug symbols, needed for
     post-mortem crash/backtrace analysis against a `.bin` running in the
     field) — same required treatment as `.bin`.
   - Copies `.pio/build/release/firmware.map` →
     `covio-firmware-vX.Y.Z.map` if it exists — **best-effort** (RE-7).
     Not every toolchain/framework combination emits a linker map without
     an explicit flag this project's `platformio.ini` has not added; its
     absence produces an `::warning::`, never a build failure, and `.bin`/
     `.elf` are still uploaded regardless.
   - Generates a `.sha256` checksum file for every real file in
     `artifacts/`, via `sha256sum "$f" | sed "s|artifacts/||" > "$f.sha256"`
     (RE-7) — same `<hash>  <filename>` format as RE-6's
     `afterAllArtifactBuild.js` hook for desktop artifacts.
7. Uploads the entire `artifacts/` directory as one workflow artifact
   (`actions/upload-artifact`, named `firmware-vX.Y.Z`, 14-day retention) —
   a downloadable build output for review, **not** a GitHub Release
   publish. Publishing to a GitHub Release (with these exact files) is
   `.github/workflows/release.yml`'s own job, which reuses this `firmware`
   job via `workflow_call` rather than duplicating it — see
   `Docs/RELEASE_AUTOMATION.md`.

## Building locally

See `SETUP.md` (RE-8) for the full toolchain-installation walkthrough
(Python, PlatformIO, PATH setup, VS Code extensions). Once a real Python +
PlatformIO environment is available (this repository's own development
history has never had one — see the honesty note above):

```
pip install platformio==6.1.15
pio run -e esp32dev   # bench/dev build
pio run -e release    # production build
pio run -e factory    # factory-test build (manual only -- see above)
```

Flashing instructions (USB, board settings, partition scheme) are
unchanged from `README.md`'s existing "Build & flash" section — this
document covers the *environments and toolchain pin* RE-5 adds, not a
duplicate of that existing procedure.
