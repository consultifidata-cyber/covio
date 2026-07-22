# 08 — OTA and Rollback Certification

**Certification status: NOT CERTIFIED.** Full findings in `03_ENTERPRISE_CHECKLIST_15_SECTIONS.md` §9. Summary:

## What was directly demonstrated this session
- `pio run -e esp32dev` and `pio run -e factory` both compile successfully with the OTA code (`ota.h`) included and linked (confirmed: `esp_ota_ops.h` usage, `HTTPUpdate` dependency resolved in the real build's dependency graph).
- `pio run -e release` correctly **refused** to compile with a placeholder CA cert, a real, deliberately-triggered proof that at least one OTA-adjacent security gate (transport CA pinning) cannot be silently skipped for a release image.
- The live connected device reports real, current OTA state via its local API: `{"state":"none","running_version":"1.0.0"}` — confirming it has never undergone an OTA update in its history.

## What was NOT demonstrated (and is not claimed)
- No real OTA upgrade was performed. No `server/firmware/manifest.json` was staged, no second firmware build was pushed, no device ever downloaded or booted a new image during this audit.
- No forced-bad-image rollback test was performed. The `esp_ota_mark_app_valid_cancel_rollback()` gate (`ota.h:83-88`, wired correctly to fire only after a real WiFi connection + a real successful server contact, `covio_firmware.ino:203-218`) is sound **by code inspection** — using the correct, standard ESP-IDF primitive at the correct point in the boot sequence — but this is the exact test RE-10's own prior audit called "the single highest-impact unexecuted test in this entire plan," and it remains unexecuted here too.
- No compatibility/hardware-revision check exists in the manifest or update logic — confirmed **absent**, not merely untested.
- No signed-manifest or artifact-level firmware signature verification exists — confirmed **absent**. The only integrity control in the OTA path is transport-level (HTTPS + CA pinning, when the URL is `https://`; the live device currently uses `http://` and therefore has **no** transport integrity on OTA either, today).
- No staged/canary rollout, no pause/cancel, no fleet-jitter — confirmed **absent** by reading `server.py`'s single-static-manifest-file mechanism and `ota.h`'s fixed, unjittered `OTA_POLL_MS`.

## Required before certification
1. A real successful OTA upgrade, on the physical connected unit or an equivalent bench unit, from a genuinely different `FW_VERSION`, with before/after `/api/v1/info.fw_version` confirmation and Serial Monitor capture of the full download → reboot → `confirmHealthyBoot()` sequence.
2. A real forced-bad-image rollback test: stage a deliberately broken image (e.g., one that panics before reaching `confirmHealthyBoot()`'s prerequisites) and confirm the ESP-IDF bootloader automatically reverts with zero operator intervention.
3. A decision, documented, on whether artifact-level signing/Secure Boot is required before this device is trusted with pull-based OTA from a URL the device does not independently verify beyond transport encryption.

**Blocks plant deployment: Yes**, until at minimum item 1 and item 2 above have real evidence attached.
