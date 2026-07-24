# 31 — Hardened USB Baseline Installation (ABORTED BEFORE BUILD/FLASH)

Executes the USB baseline installation mandate. **Stopped before Phase 2
(build), before any firmware was compiled or flashed.** COM6 was not
opened for programming; no bytes were written to the device.

## Verdict: FLASH ABORTED BEFORE WRITE

## Source identity — confirmed clean (per Phase 1 pre-checks)

```
git branch --show-current  -> fix/coviu-oil-meter-p0-enterprise-readiness
git rev-parse HEAD          -> af11924200f4a8621f9d7b3186bf53e5ded64dfb
git status --short          -> 4 untracked files, zero modified tracked files:
  ?? Docs/.../27_READ_ONLY_PREFLIGHT_RESULT.md
  ?? Docs/.../28_READ_ONLY_PREFLIGHT_RETRY_RESULT.md
  ?? Docs/.../29_HTTP_PREFLIGHT_RESULT.md
  ?? Docs/.../30_OTA_SUCCESS_PHYSICAL_TEST_RESULT.md
```
Branch and commit match the authorization exactly. The four untracked
files are this session's own prior audit documents (Markdown only, inside
`Docs/`, explicitly excluded from the firmware build by
`platformio.ini`'s `build_src_filter`) — zero tracked source files are
modified, and nothing untracked is C/C++/build-relevant. Treated as
"clean" for build-determinism purposes; disclosed rather than silently
waved through.

## Build environment — confirmed matching hardware

`platformio.ini`'s `[env:esp32dev]` (inheriting `[env]`) already targets
`board = esp32-s3-devkitc-1`, `board_upload.flash_size = 16MB`,
`board_build.flash_mode = dio`, `upload_port = COM6` — this is a
documented, deliberate override for this exact bench unit (the file's own
comment: "esptool identified the connected chip as a genuine ESP32-S3").
Consistent with the earlier-confirmed USB identity (`VID_303A&PID_1001`).

## Confirmed present in committed source (would satisfy Phase 2's checklist)

- `security_version` / `accepted_security_floor` / `last_reject_reason` /
  `last_auth_reject_reason` — all present, unconditionally emitted, in
  `diagnostics.h::buildStatusJson()`.
- Manifest ECDSA-P256 verification (`ota_manifest_auth.h`,
  `mbedtls_pk_verify()` call site in `ota.h`) and streaming SHA-256 image
  verification — present, previously confirmed independently (doc 30).
- `ota_keys.h`'s compiled-in public key, non-placeholder key ID
  (`covio-test-key-2026-07`) for this dev/bench build; `RELEASE_BUILD`
  guard unaffected (still 0 for `esp32dev`).

## Why this stopped anyway — the blocking finding

The mandate's own "Firmware Identity Requirement" section states:
> "If the current committed code does not expose build identity, stop
> before flashing and report the missing observability blocker. Do not
> add code under this authorization."

A full read of `diagnostics.h::buildInfoJson()` (the entire `/api/v1/info`
handler) shows its complete field set: `hardware_id`, `logical_device_id`,
`asset_id`, `fw_version`, `model`, `boot_id`, `schema_version_current`.
**No build-hash or commit-identity field exists.** A repository-wide
search for any mechanism that could have injected one at compile time —
`GIT_COMMIT`, `BUILD_HASH`, `BUILD_COMMIT`, `GIT_SHA`, `BUILD_SHA`,
`commit_hash`, `__DATE__` — returned **zero matches** anywhere in the
tree. This is not a partial gap; there is no build-identity observability
mechanism in this firmware at all, committed or otherwise.

Per the mandate's own explicit instruction, adding one now would be
"adding code under this authorization," which is forbidden. So this is
reported as a blocker, not worked around.

## Practical consequence

Phase 5 of this mandate would have required confirming, post-flash:
```
build identity = af11924200f4a8621f9d7b3186bf53e5ded64dfb
```
live from the device. That specific confirmation is **structurally
impossible** with the code as currently committed — no live read of this
device, before or after any flash, can ever produce a build-commit value,
because nothing on the device or in its HTTP responses carries one. The
only identity signal available post-flash would be the fw_version string
(still `"1.0.0"`, unchanged, since no source edit was made — the mandate
also forbids source modification during this session) plus the presence
of the security/auth diagnostic fields — which is exactly the situation
Phase 5 itself warned against relying on ("do not rely solely on the
firmware version string") while offering no alternative that doesn't
require the missing field.

## Fresh read-only baseline (captured before halting, for continuity)

```
timestamp: 1784809530
GET /api/v1/info   -> {"hardware_id":"esp32-F4E5B2858428","fw_version":"1.0.0",
                        "model":"covio-oilflow-v1","boot_id":19,"schema_version_current":1}
GET /api/v1/status -> uptime_ms 5617249, wifi connected (ssid redacted),
                        server_url http://192.168.1.3:8000, api_key_status "default",
                        sd_status "ok", queue{backlog:2, acked_seq:39276, last_seq:39278},
                        totalizer_raw_pulses 280, ota{state:"none", running_version:"1.0.0"},
                        health_state "ok"
DB: records count=39276 min=1 max=39276 (contiguous), quarantined=0, device rows=2
```

## What was NOT done

- No firmware was built (`pio run` was never invoked).
- COM6 was never opened for programming (no `-t upload`, no `esptool`).
- No bytes were written to any partition.
- No reboot, reset, or recovery action occurred.
- The bench server remains running, unmodified, isolated.

---

# Required Final Response

## 1. Baseline Installation Verdict
**FLASH ABORTED BEFORE WRITE**

## 2. Device and Source Identity
- Device ID: `esp32-F4E5B2858428` (confirmed via live HTTP, matches authorization)
- COM port: COM6 (not opened for programming this session)
- USB identity: `VID_303A&PID_1001`
- Branch: `fix/coviu-oil-meter-p0-enterprise-readiness`
- Commit: `af11924200f4a8621f9d7b3186bf53e5ded64dfb`
- Build environment: `esp32dev` (`board=esp32-s3-devkitc-1`, confirmed matching this bench unit)
- Firmware artifact SHA-256: n/a — nothing was built

## 3. Flash Details
Not applicable — no build, no flash command, no partitions written, no reboot.

## 4. Hardened Firmware Proof
Not obtainable this session — blocked before it could be attempted (see below).

## 5. State Preservation
Not applicable — device untouched; current live state recorded above for continuity only.

## 6. Reconciliation
Not applicable — no flash interval occurred.

## 7. Recovery
Not needed — nothing was attempted, nothing failed.

## 8. Risk Status
- RISK-15 remains hardware-pending until a real OTA downgrade-rejection test.
- RISK-16 remains hardware-pending until a real signed-OTA and rejection test.
- RISK-05 remains open.
- RISK-02 remains human-action pending.
- **New, this step:** there is no way to cryptographically/observably
  distinguish "the hardened commit is physically running" from "the old
  pre-hardening build is still running" via any live read this project's
  firmware currently supports, other than the presence/absence of the
  security/auth diagnostic *fields themselves* (which is meaningful, but
  is not the same as confirming the exact commit).

## 9. Next Recommendation
**BASELINE REMEDIATION REQUIRED BEFORE OTA**

Two independent paths resolve this, either is legitimate — this session
took neither, since both require either a source change (out of this
session's scope) or a policy decision (yours to make):
1. Accept the diagnostic-fields-present/absent signal alone (security_version,
   accepted_security_floor, last_reject_reason, last_auth_reject_reason
   all appearing post-flash) as sufficient proof of the hardened baseline,
   explicitly waiving the "build identity" requirement for this round —
   your call, not mine to assume.
2. Authorize a minimal, explicit source addition (e.g. a `BUILD_COMMIT`
   macro surfaced in `/api/v1/info`) in a **separate** authorization that
   permits a source change, then rebuild/flash under that.

---

**USB BASELINE STEP COMPLETE:** This operation established or attempted
to establish the hardened physical baseline only. It does not constitute
successful OTA, rollback, anti-downgrade, signature-rejection,
hash-rejection, or interrupted-download proof. Any OTA test requires a
new explicit authorization.
