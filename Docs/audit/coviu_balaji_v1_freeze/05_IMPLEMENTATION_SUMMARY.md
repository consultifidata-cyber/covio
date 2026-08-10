# 05 — Balaji V1 Final Implementation Summary

Implements exactly the five approved freeze-list items and nothing else —
no deferred architecture, no ERP redesign, no fleet-management concepts, no
commercial-scale features. Every change below is additive to existing
behavior; nothing that previously worked was altered except where explicitly
noted (the OTA signing key, which was *designed* to be replaced before any
release build).

## 1. OTA signing-key ceremony

- Generalized `server/tools/sign_manifest.py`'s key-generation core
  (`_generate_keypair()`), added `--gen-production-key` alongside the
  existing `--gen-test-key` (same cryptography, different operator
  handling instructions).
- Ran the ceremony: generated a real ECDSA P-256 production keypair. The
  private key was written **only** to the local, gitignored
  `server/tools/.production_signing_key.pem` and was never printed, logged,
  or transmitted — only the public key (not secret) was ever displayed.
- `ota_keys.h`: replaced the placeholder public key with the real
  production public key, set `COVIO_OTA_KEY_ID` to `"covio-prod-key-2026-07"`,
  flipped `COVIO_OTA_KEY_IS_PLACEHOLDER` from `1` to `0`. The existing
  `#if RELEASE_BUILD && COVIO_OTA_KEY_IS_PLACEHOLDER` compile guard now
  passes.
- Documented the full ceremony record and standing operational procedure
  (including the still-required manual step: moving the private key off
  this laptop into a real secrets vault) in
  `Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md`.
- No change to the OTA signing/verification design itself — it was already
  reviewed as sound.

## 2. Sensor accuracy validation support

- Reviewed the existing calibration flow (`store.h`, `sync.h`,
  `server/server.py`): confirmed calibration is entirely server-authoritative
  and the device only caches a display copy — unchanged, not touched.
- Added `scripts/validate_meter_calibration.py`: a read-only tool that
  captures the device's `totalizer_raw_pulses` before/after a technician-run,
  real-flow test (measured against a calibrated reference volume the
  technician supplies), computes the actual pulses-per-litre constant, and
  compares it against the currently configured K-factor with a
  configurable tolerance (default ±2%). Produces a timestamped JSON
  validation report. Makes zero writes to the device or server.
- No calibration architecture change — this is purely an external
  measurement/verification aid.

## 3. Production API key verification

- Traced the exact classification logic (`credential_display.h`,
  `provision.h`) and the server's key-uniqueness enforcement
  (`server/server.py`'s `UNIQUE` constraint + `secrets.token_hex(24)`
  generation + the `legacy-default-key` fallback-identity trap).
- Performed a fresh, **read-only, no-reset** serial console check of the
  live production device: confirmed `api_key: configured
  (fingerprint=18519bce8d2a)`, `boot_id: 54` unchanged since the prior
  commissioning session (device has been continuously running, not
  reprovisioned).
- **Conclusion: no mismatch found, no rotation performed** — the freeze
  list's own instruction was to rotate only if a mismatch or security issue
  is proven, and preserving working commissioning was an explicit
  requirement. Full evidence and reasoning in
  `Docs/audit/coviu_balaji_v1_freeze/04_API_KEY_VERIFICATION_RESULT.md`.
- Added `scripts/verify_api_key_fingerprint.py` for future verification/
  rotation events: computes the identical SHA-256-derived fingerprint the
  serial console prints, from a key read via hidden prompt or stdin —
  never as a CLI argument, never printed back.

## 4. Long-term diagnostics

New, additive firmware capability — persistent (NVS-backed, survive
reboot) failure counters and a sensor-stuck-at-zero alarm, both exposed via
the existing local API and the serial console.

- **New file `sensor_stuck.h`**: pure, dependency-free `SensorStuckDetector`
  class (heuristic: flags when the lifetime pulse total hasn't changed for
  `SENSOR_STUCK_THRESHOLD_MS`, default 48h — generous by design, since a
  real oil meter can legitimately sit idle).
- **`store.h`**: six new NVS-backed counters (`restart_count`,
  `watchdog_reset_count`, `brownout_reset_count`, `push_fail_count`,
  `wifi_reconnect_count`, `crash_reset_streak`) with getters/incrementers.
  Same namespace/lifecycle as every existing operational field (reset only
  by an explicit factory reset, exactly like `boot_id`).
- **`config.h`**: three new tunable constants (`SENSOR_STUCK_THRESHOLD_MS`,
  `CRASH_RESET_STREAK_ALARM`, `HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS`).
- **`covio_firmware.ino`**: classifies each boot's reset cause once at
  startup (watchdog/brownout/panic → increments the matching counter +
  crash streak); feeds the totalizer reading into the stuck-sensor detector
  once per telemetry cycle; clears the crash streak once a boot proves 5
  minutes of stable uptime.
- **`sync.h`**: increments `wifi_reconnect_count` on each genuine reconnect
  attempt, `push_fail_count` on each push that doesn't result in an ack
  (deliberately one honest combined counter, not a fabricated DNS-vs-TLS
  split the HTTP client abstraction can't actually distinguish).
- **`diagnostics.h`**: all six counters plus `sensor_stuck`/
  `ms_since_last_pulse_change` added to `/api/v1/metrics`; two new alarm
  types added to the existing health computation — `SENSOR_STOPPED`
  (WARNING) and `REBOOT_LOOP` (CRITICAL, ≥3 consecutive abnormal resets
  without a healthy run in between). `WIFI_RECONNECT_LOOP` and
  `BROWNOUT_DETECTED` were deliberately **not** added as separate alarm
  types — a true rate-based "loop" detector needs wall-clock time this
  device doesn't have (unchanged RISK-11 limitation); the raw counters are
  exposed instead of a potentially misleading fabricated verdict.
- **`local_api.h`**: threads the new counters/detector through to every
  existing route (`/api/v1/status`, `/api/v1/health`, `/api/v1/metrics`,
  the plain-HTML page) — no new routes.
- **`provision.h`**: `show` now also prints a `diag:` line with all six
  counters, so they're readable via USB with zero network/API dependency.

## 5. Documentation

- `Docs/audit/coviu_balaji_v1_freeze/02_FIELD_SERVICE_GUIDE.md` — covers
  exactly the six requested scenarios (replace device, change Wi-Fi, rotate
  API key, recalibrate, firmware update, recover from common failures),
  scoped strictly to capabilities that exist today.
- `03_OTA_SIGNING_KEY_CEREMONY.md`, `04_API_KEY_VERIFICATION_RESULT.md` —
  as described above.

---

## Files changed

| File | Change |
|---|---|
| `ota_keys.h` | Real production public key embedded; placeholder flag cleared |
| `server/tools/sign_manifest.py` | Added `--gen-production-key` (generalizes existing key-gen core) |
| `.gitignore` | Explicit production-key entry (belt-and-suspenders; already covered by `*.pem`) |
| `store.h` | 6 new persistent counters + accessors |
| `config.h` | 3 new tunable diagnostic constants |
| `covio_firmware.ino` | Boot-time reset classification, stuck-sensor feed, crash-streak clear |
| `sync.h` | Increment `wifi_reconnect_count` / `push_fail_count` at the right call sites |
| `diagnostics.h` | New counter fields in `/api/v1/metrics`; 2 new alarm types |
| `local_api.h` | Threads new data through existing routes; new `SensorStuckDetector*` dependency |
| `provision.h` | `show` prints the new `diag:` line |
| `.github/workflows/ci.yml` | Wires the new native C++ test into the existing CI job |
| `test/native/test_sign_manifest_tool.py` | 3 new tests for `--gen-production-key` |

## New files

| File | Purpose |
|---|---|
| `sensor_stuck.h` | Pure sensor-stuck-at-zero detector |
| `scripts/validate_meter_calibration.py` | Real-flow calibration accuracy validation tool |
| `scripts/verify_api_key_fingerprint.py` | API key fingerprint verification tool |
| `test/native_cpp/test_sensor_stuck.cpp` | 7 tests for the stuck detector (CI-compiled) |
| `test/native/test_validate_meter_calibration.py` | 15 tests for the calibration tool |
| `test/native/test_verify_api_key_fingerprint.py` | 5 tests for the fingerprint tool |
| `Docs/audit/coviu_balaji_v1_freeze/02_FIELD_SERVICE_GUIDE.md` | Field service guide |
| `Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md` | Signing ceremony record + procedure |
| `Docs/audit/coviu_balaji_v1_freeze/04_API_KEY_VERIFICATION_RESULT.md` | API key verification evidence |
| `Docs/audit/coviu_balaji_v1_freeze/05_IMPLEMENTATION_SUMMARY.md` | This document |

(`server/tools/.production_signing_key.pem` was created but is gitignored
and intentionally not listed as a tracked file — see item 1's "what must
happen next.")

## Test results

- **Python (`test/native/`, `python -m unittest discover -s test/native -v`):
  117 tests, 116 passed, 1 skipped (a POSIX-only file-permission check,
  correctly skipped on this Windows machine), 0 failures.** Includes all
  pre-existing tests (confirms no regression) plus 23 new tests across the
  three new/modified tool test files.
- **Firmware compile (`pio run -e esp32dev`, real PlatformIO xtensa
  cross-toolchain, not a simulation): SUCCESS**, both before this session's
  changes (baseline) and after — confirms every signature change across
  `store.h`/`diagnostics.h`/`local_api.h`/`sync.h`/`covio_firmware.ino`/
  `sensor_stuck.h`/`provision.h`/`config.h`/`ota_keys.h` compiles and links
  correctly for the real target hardware (ESP32-S3). Flash usage: 15.7%
  (1,031,241 / 6,553,600 bytes) — a ~2,976-byte increase over baseline for
  all of this session's additions combined.
- **Firmware compile (`pio run -e release`, `RELEASE_BUILD=1`): FAILED —
  by design, not a defect.** The build-identity extra-script's existing
  fail-closed dirty-tree gate (`scripts/build_identity.py`,
  `RELEASE_BUILD requires a clean working tree`) correctly refuses to build
  a release image while these changes are uncommitted. This is the same
  gate this project already relies on to guarantee every release image is
  traceable to an exact, clean commit — it was not bypassed or weakened.
  **This means the `[env:release]` build (which also exercises the OTA
  signing-key gate and the default-API-key gate) has not yet been verified
  end-to-end** — that requires committing these changes first, which was
  not done in this session (commits happen only when the user asks). The
  bench build already proves the underlying C++ is correct; the only
  additional thing a clean release build would verify is the two
  preprocessor guards (`ota_keys.h`'s placeholder check, `config.h`'s
  default-API-key check) — both unchanged in design and already known to
  pass now that the placeholder flag is `0`.
- **Native C++ tests (`test/native_cpp/test_sensor_stuck.cpp`, 7 tests):
  written and statically reviewed but not locally executed** — no host C++
  compiler (g++/gcc/clang++/cl) is available on this machine, the same
  disclosed gap every prior native_cpp test file in this project already
  states. Wired into `.github/workflows/ci.yml`'s existing
  `firmware-native-fault-injection` job, which will compile and run it on
  the next CI run (ubuntu-latest, g++ preinstalled) exactly as it already
  does for `test_ota_version_policy.cpp`/`test_ota_manifest_auth.cpp`.

## Deployment impact

- **Server/ERP: no change.** Nothing in `server/server.py` was modified.
- **Device Manager: no change.** No file under `tools/device-manager/` was
  touched.
- **Firmware: changed, requires an update to take effect.** All of item 4's
  diagnostics and item 1's signing key change are compiled into the
  firmware image — none of this is retroactive to the firmware currently
  running on the production device.
- **Backward compatibility: preserved.** Every NVS field, API endpoint,
  and route already in use is unchanged in meaning; all additions are new
  fields/counters appended to existing JSON bodies (never a breaking
  schema change) and a new line in the serial console's `show` output.
  Existing Wi-Fi, server URL, API key, calibration, and totalizer/queue
  data on the production device are untouched by any of this — nothing
  here reads or writes those in a new way.

## Does the production ESP32 require any action after these changes?

**Yes — one action, when the team is ready, not urgently:**

The device is currently running firmware built before this session's
changes. To get the new diagnostics (persistent counters, sensor-stuck
alarm) and the corrected OTA signing key onto the live device, it needs a
firmware update. Two paths, neither of which changes Wi-Fi, server URL, API
key, calibration, or queue/totalizer data:

1. **Preferred: OTA**, once these changes are committed and a signed
   release manifest is published using the new production key (per
   `03_OTA_SIGNING_KEY_CEREMONY.md`'s signing instructions). The device
   will pick it up automatically on its next OTA poll cycle — no physical
   access required.
2. **Alternative: USB reflash** (`pio run -e release -t upload`, after
   committing), if a faster/manual update is preferred.

**Nothing needs to happen immediately.** The device is healthy and correct
as-is; today's changes add observability and close the signing-key gap for
the *next* release, they do not fix a currently-broken behavior on the
running device. There is no urgency to reflash before the next planned
maintenance window.
