# 08 — Firmware Updates (Part 9)

Every claim below is real-hardware-proven **within this same overall
audit engagement** (`Docs/audit/coviu_oil_meter_p0_remediation_phase2/`
docs 33-37), independently re-confirmed live this session where marked.
Not accepted from stale/older documentation predating this session's own
remediation work.

| # | Question | Answer | Evidence |
|---|---|---|---|
| 1 | OTA supported? | Yes | Real installs proven repeatedly |
| 2 | Manifest authenticated? | Yes — ECDSA-P256 over a canonical field string | Independently re-verified with Python `cryptography` against the compiled-in public key, multiple manifests, this whole chain |
| 3 | Firmware hash verified? | Yes — streamed SHA-256, compared before `Update.end()` | Real hardware: hash-mismatch test correctly aborted before commit |
| 4 | Hardware compatibility checked? | Yes | Real hardware: `hw_compat` mismatch correctly rejected (doc 34) |
| 5 | Timestamp/replay protection active? | Yes, **after** the `server_time_ms` overflow fix (doc 35) | Real hardware: pre-fix, ALL real manifests were rejected `no_time_source`; post-fix, genuine installs succeed |
| 6 | Security version enforced? | Yes | Real hardware: genuine anti-downgrade rejection proven (doc 36) against an established non-zero floor |
| 7 | Accepted floor persisted? | Yes | Real hardware: floor survived a controlled reboot unchanged (doc 37) |
| 8 | Invalid signature rejected? | Yes | Real hardware, doc 36 |
| 9 | Hash mismatch rejected? | Yes | Real hardware, doc 36 |
| 10 | Interrupted download safe? | Yes | Real hardware, deterministic truncation method, doc 37 |
| 11 | Partial image prevented from activation? | Yes | Same test — `boot_id`/build identity unchanged after 2 truncated attempts |
| 12 | OTA scheduling supported? | **No** — polls on a fixed `OTA_POLL_MS` timer only, no maintenance-window concept in firmware | Code-confirmed |
| 13 | Maintenance-window enforcement exists? | **No firmware-level enforcement** — procedural only (doc 01 of the plant-readiness series, Part 7 OTA policy) | Code-confirmed absence |
| 14 | Operator authorization exists (in-firmware)? | **No** — any correctly-signed manifest the device can fetch will be evaluated; there is no additional "operator approved this specific rollout" gate in firmware | Code-confirmed absence |
| 15 | OTA audit trail exists? | **Partial** — `device_events`/`OTA_FAILED` alarms exist server/device-side (proven live, doc 36's `health_state:"degraded"` + alarm test); no separate durable "who approved this OTA and when" record exists | Code + live evidence |
| 16 | Current signing key classification | **TEST** (`covio-test-key-2026-07`) | Confirmed, this session's own security scan (doc 10) |
| 17 | TLS certificate production-ready? | **No** — bench endpoint is `http://`, no TLS in use at all currently | Confirmed live, this session (`server_url` still `http://192.168.1.3:8000`) |
| 18 | Automatic rollback exists? | **NO — proven absent, with binary-level evidence, not merely "not observed"** | See below |

## Automatic bootloader rollback — the proven limitation, restated precisely

**Bootloader-level automatic rollback does NOT exist on this hardware.**
This was proven, not assumed, in the prior remediation phase (doc 37 of
the phase-2 series): the flashed bootloader is one of exactly 4 fixed,
prebuilt ELF binaries shipped with the `arduino-esp32` framework release
(no bootloader source exists anywhere in the framework package at all —
confirmed again this session by the same `find`), never compiled from
this project's own `sdkconfig.h`. Direct byte-level inspection of the
actual flashed binary (`bootloader_dio_80m.elf`) found **zero references
to the application-image pending-verify/rollback state machine** — only
an unrelated secure-boot anti-rollback string. Across **four independent
real transitions this whole engagement** (2 fresh USB baselines, 2
genuine OTA installs), the device's raw `esp_ota_get_state_partition()`
reading was always `VALID`/`UNDEFINED`, never `NEW`/`PENDING_VERIFY` —
consistent, not flaky.

**Application-level confirmation is real and does work** — `ota.h`'s
`confirmHealthyBoot()` (fixed doc 36) genuinely advances the security
floor once the device proves it can reach its server, independent of the
absent bootloader mechanism, and this is honestly disclosed via the
device's own `bootloader_rollback_engaged` field (has read `false` on
every single confirmation this entire engagement — never claims
protection that isn't there). **Application confirmation is explicitly
NOT the same thing as automatic bootloader rollback**, and this report
does not conflate them.

## Does this limitation block deployment levels?

- **Controlled pilot**: does not block, PROVIDED a human with USB
  recovery equipment is present during any OTA window (procedural
  mitigation, doc 01 of the plant-readiness series).
- **Normal production**: does not block by itself, same mitigation, but
  raises the operational cost of every OTA (mandatory supervised
  window).
- **Remote unattended OTA**: **blocks it outright.** An authentic-but-
  unhealthy candidate has no automatic recovery path on this hardware —
  confirmed structurally, not merely "not yet tested."

## Bench-infrastructure check for the current candidate

Confirmed live, this session: `server_url` on the device reads
`http://192.168.1.3:8000` — **the bench address, unchanged**. This is
the single most important finding of this entire re-audit and is
carried through to `19_PLANT_GO_NO_GO.md` as a mandatory-NO-GO trigger
per the mandate's own rule.
