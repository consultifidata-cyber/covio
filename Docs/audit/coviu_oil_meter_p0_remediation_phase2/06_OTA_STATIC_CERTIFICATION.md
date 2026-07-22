# 06 — Complete OTA Static Certification (Updated)

Supersedes (does not delete) `Docs/audit/coviu_oil_meter_p0_remediation/06_OTA_STATIC_CERTIFICATION.md`.
This version adds the anti-downgrade gate to the trace and re-verifies
every item against the CURRENT code (post RISK-15 fix).

## End-to-end trace

```
Device Manager command       -- N/A: OTA is device-POLLED (pull), not
                                 Device-Manager-pushed. There is no
                                 "command" in this architecture's OTA path.
→ authentication/authorization -- ota.h::poll() sends X-Api-Key
                                 (require_api_key(), server.py) -- reused,
                                 unchanged, real device-facing auth
→ OTA manifest                -- GET PATH_OTA_MANIFEST, server.py's
                                 ota_manifest() (static file passthrough)
→ device polling               -- ota.h::poll(), every OTA_POLL_MS (5 min)
→ manifest validation          -- NEW (RISK-15): evaluateOtaCandidate()
                                 (ota_version_policy.h) -- security_version/
                                 hw_compat/schema_version gate, BEFORE any
                                 download attempt
→ image download               -- ota.h::doUpdate_(), HTTPUpdate library
→ TLS validation                -- WiFiClientSecure + setCACert()
                                 (COVIO_PINNED_CA_CERT), when https://
                                 (ADR-005, unchanged)
→ hash/signature verification   -- ABSENT (see below, unchanged from prior cert)
→ inactive partition write      -- HTTPUpdate/esp_ota_ops internals
→ boot partition selection      -- esp_ota_set_boot_partition (internal to
                                 HTTPUpdate's rebootOnUpdate(true) path)
→ reboot                        -- automatic on successful HTTPUpdate
→ candidate health validation   -- covio_firmware.ino: healthySignalled,
                                 gated on WiFi connect + 1 successful
                                 push-ack/config-poll
→ mark-valid or rollback        -- ota.h::confirmHealthyBoot() (mark valid)
                                 OR bootloader auto-rollback if the
                                 candidate never reaches that call
                                 (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1,
                                 confirmed compiled into this exact
                                 toolchain -- prior session's finding,
                                 unchanged, re-verifiable via
                                 ~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/dio_qspi/include/sdkconfig.h)
→ result reporting               -- /api/v1/status's "ota" object, NOW also
                                 including security_version,
                                 accepted_security_floor, and
                                 last_reject_reason (RISK-15 addition)
```

## Mandatory static checks (re-verified this session)

| Check | Status |
|---|---|
| A/B OTA partition layout, from the actual compiled partition table | **PASS** — `default_16MB.csv` defines `app0`/`app1` (re-confirmed, same hash as phase 1: `4a6aaf1...`) |
| Rollback configuration in the actual build environment | **PASS** — `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` confirmed compiled into the `dio_qspi` SDK variant this build uses |
| Candidate app does not mark itself valid before filesystem mount / config load / queue recovery / network init / core task health / watchdog init | **PARTIAL** — confirmed the ORDER is correct (`covio_firmware.ino`'s `setup()`: LittleFS mount → totalizer/queue recovery → WiFi connect → first successful sync → THEN `confirmHealthyBoot()`), but there is still no explicit task-watchdog configuration anywhere in this codebase (RISK-12, unchanged, pre-existing P2 gap) — "watchdog initialization" as a precondition does not apply because no watchdog is configured to precondition on |
| Health timeout is bounded | **PASS** — `AP_FALLBACK_TIMEOUT_MS`/`CONFIG_POLL_MS`/network timeouts are all bounded constants; there is no unbounded wait in the health-confirmation path |
| Boot loops trigger rollback | **PARTIAL** — relies entirely on the bootloader's own `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` mechanism (confirmed present), not on any application-level crash-loop counter (RISK-12, unchanged gap) |
| Unsynced queue partition is not erased by OTA | **PASS** — the queue lives in `spiffs`/LittleFS, a separate partition from `app0`/`app1`; nothing in `ota.h` calls `LittleFS.format()` |
| NVS/config survives OTA | **PASS**, and now MORE IMPORTANT than before: `NVS_NS_SECURITY` (the new anti-downgrade floor) is in the SAME NVS partition as everything else, untouched by any OTA write path |
| LittleFS schema remains compatible | **PASS**, unchanged — `totalizer.h`'s `loadLegacy_()` already handles forward migration independent of firmware version |
| Manifest and binary use HTTPS with certificate verification | **PASS (code)**, unchanged from prior cert |
| Production builds cannot disable certificate verification | **PASS**, unchanged — `certs.h`'s placeholder-cert compile guard, re-verified this session (`env:release` still fails exactly as before) |
| OTA command is authenticated and authorized | **PASS** for the polling requests (`X-Api-Key`); **N/A** for a "command" since none is pushed in this architecture |
| OTA progress and final result are audited | **PARTIAL** — `/api/v1/status` now reports more (RISK-15 additions), but there is still no SERVER-side `device_events` entry for an OTA attempt/success/failure/rollback (the device-facing OTA routes don't call `record_event()`) — a real, still-open gap, distinct from RISK-15 |
| Device Manager can distinguish download-failed / verification-failed / candidate-boot-failed / rolled-back / committed | **STILL PARTIAL, unchanged from prior cert** — the 4-state `OtaState` enum collapses several of these into `OTA_STATE_FAILED`; RISK-15 added `last_reject_reason` for the NEW pre-download rejection case specifically, but did not expand the post-download state granularity |

## What remains absent (unchanged from the prior static certification)

- Manifest/image signing.
- Image hash verification.
- A more granular OTA state machine distinguishing every mandate-listed
  failure mode.
- Server-side OTA event audit trail.

## Bottom line

RISK-15 (anti-downgrade specifically) is code-closed pending CI test
execution (doc 05). RISK-05 (hardware-proven OTA+rollback) remains
**NOT PROVEN** — nothing in this phase touched physical hardware. The
compiled-in bootloader rollback support remains the strongest piece of
static evidence available without crossing that boundary.
