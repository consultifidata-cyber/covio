# 06 — RISK-05: OTA Static Certification (Code/Build Evidence Only)

No physical-device OTA/rollback test was performed in this session — that
is explicitly prohibited by this mandate's authority boundary without
separate authorization (see `07_OTA_PHYSICAL_TEST_PLAN.md`). This document
covers what CAN be established without touching hardware.

## Checklist against `ota.h` (file read in full this session)

| Item | Status | Evidence |
|---|---|---|
| A/B OTA partition table | **PASS** | `platformio.ini`: `board_build.partitions=default_16MB.csv`, confirmed to define `app0`/`app1` (two OTA app slots) by direct read of the partition CSV |
| Signed/authenticated firmware | **ABSENT** | No image signing exists in `ota.h`; integrity relies entirely on TLS transport (see below), not on a signature verified independent of the download channel |
| Integrity/hash verification (post-download) | **ABSENT** as a separate step | `HTTPUpdate`'s internal flash-write path performs its own internal size/CRC bookkeeping (ESP-IDF `esp_ota_write` internals), but there is no application-level manifest-declared hash the device checks against |
| HTTPS download (TLS + CA pinning) | **PASS (code)** | `doUpdate_()` dispatches on `covioIsHttpsUrl()`; when true, uses `WiFiClientSecure` with `setCACert(COVIO_PINNED_CA_CERT)` — never `setInsecure()` (ADR-005) |
| Hardware compatibility check | **ABSENT** | No hardware-revision/model field is checked before applying an update — `poll()` only compares the manifest's `version` string against `FW_VERSION` |
| Firmware version policy | **PARTIAL** | Update triggers whenever `manifest.version != FW_VERSION` — any difference, not just "newer" |
| Anti-downgrade policy | **ABSENT** | Direct consequence of the above: a manifest offering an OLDER version string would be applied identically to a newer one. **This is a real, code-provable gap**, distinct from RISK-05's stated problem (rollback proof) — noted here as a finding, deliberately NOT fixed in this pass per the mandate's "do not expand scope into P1/P2/P3 unless required for a P0 fix to be safe" — fixing it is not required for the ROLLBACK mechanism itself to be safe to test, and is tracked as a new risk item in the updated risk register |
| Image-size checks | Delegated to `HTTPUpdate`/`Update` library internals (ESP-IDF's OTA write path checks the target partition's size); not independently re-verified this session |
| Queue/config partition isolation | **PASS** | Queue (`spiffs`/LittleFS) and app partitions (`app0`/`app1`) are physically separate regions of the partition table; an OTA write targets only the inactive `app` partition and cannot touch `spiffs` |
| NVS/LittleFS migration compatibility | **Unchanged, out of OTA's own scope** | Config lives in NVS (`store.h`), queue in LittleFS — neither is touched by the OTA write path itself; `totalizer.h`/`queue.h` already have their own forward-compatible checkpoint-migration logic (`loadLegacy_()`), independent of firmware version |
| Boot validation timeout / rollback trigger | **PASS (mechanism confirmed present)** | `noteBoot()` reads `esp_ota_get_state_partition()`; if `ESP_OTA_IMG_PENDING_VERIFY`, the bootloader-level auto-rollback safety net applies. **New this session:** directly confirmed `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` is compiled into this exact toolchain's SDK (`~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/dio_qspi/include/sdkconfig.h`) — the underlying mechanism genuinely exists in this build, not merely assumed from documentation |
| `mark app valid` timing | **PASS (code)** | `confirmHealthyBoot()` is only called after WiFi connects AND at least one successful server ack/config-poll (`covio_firmware.ino`'s `healthySignalled` gating) — a genuine health proof, not merely "the app didn't immediately crash" |
| Reporting to Device Manager | **PARTIAL** | `ota.state()` is exposed via `/api/v1/status`'s `ota` object (`state`, `running_version`); no explicit prior-version-vs-current-version pair, no OTA event history beyond the current instant |
| Recovery after network interruption during download | Delegated to `HTTPUpdate` internals; not independently re-verified this session |
| Recovery after power interruption during a flash write | Relies on the same bootloader rollback mechanism above (an incomplete OTA write should leave `otadata` still pointing at the previous good partition) — code-plausible, **not hardware-proven** |
| Preservation of unsynced records across an update | **PASS (architectural)** | The queue lives in a completely separate LittleFS partition untouched by any OTA write; nothing in `ota.h` ever calls `LittleFS.format()` or touches `/queue/*` |

## OTA state machine — actual implementation, mapped to the mandate's suggested states

Actual code state (`OtaState` enum in `ota.h`): `OTA_STATE_NONE` →
(update offered) → `OTA_STATE_PENDING_VERIFY` (post-reboot, ESP-IDF's own
`ESP_OTA_IMG_PENDING_VERIFY`) → `OTA_STATE_CONFIRMED` (after
`confirmHealthyBoot()`), or `OTA_STATE_FAILED` (most recent `doUpdate_()`
attempt failed, e.g. `HTTP_UPDATE_FAILED`).

Mapped to the mandate's illustrative state names:

```
IDLE                 = OTA_STATE_NONE
MANIFEST_RECEIVED     = (transient, inside poll() -- not a durable state)
VALIDATED             = (transient -- version-difference check passes)
DOWNLOADING           = (transient, inside doUpdate_()/HTTPUpdate)
IMAGE_VERIFIED        = (delegated to HTTPUpdate/esp_ota_ops internals)
PENDING_REBOOT        = (implicit -- HTTPUpdate reboots immediately on success)
BOOTING_CANDIDATE     = OTA_STATE_PENDING_VERIFY
VALIDATED_RUNNING     = OTA_STATE_CONFIRMED
COMMITTED             = OTA_STATE_CONFIRMED (no separate "committed" step
                         beyond esp_ota_mark_app_valid_cancel_rollback())

DOWNLOAD_FAILED / SIGNATURE_FAILED / INCOMPATIBLE / BOOT_FAILED / ROLLED_BACK
                      = collapsed into a single OTA_STATE_FAILED (does not
                        distinguish WHICH failure mode occurred)
RECOVERY_REQUIRED     = not a distinct state; the bootloader's own automatic
                        rollback handles this transparently, with no
                        device-side flag marking "a rollback just happened"
```

**Finding:** the mandate's more granular state machine is not implemented —
this codebase's actual 4-state enum collapses several mandate-distinct
states together (most notably, it cannot currently tell you WHETHER a boot
that returned to `OTA_STATE_NONE`/an old version did so because of a
rollback vs. simply never having attempted an update). This is a real gap
for Device Manager observability (worsens diagnosability of a rollback
event specifically) but is not, by itself, evidence that the rollback
mechanism doesn't work — it is evidence that its outcome isn't automatically
distinguishable from other states. Tracked in the updated risk register as
a P1/P2 diagnosability gap distinct from RISK-05 itself.

## Bottom line

RISK-05 remains **NOT PROVEN**. Static/build confidence increased this
session (the compiled-in bootloader rollback support is now independently
confirmed, not merely assumed); one new code-level gap was found
(anti-downgrade) and is tracked separately, not silently folded into "OTA
is fine." A real OTA + forced-bad-image rollback test on the physical,
already-connected bench unit remains the only way to close this P0 — see
the next document for the exact, pre-authorized test plan.
