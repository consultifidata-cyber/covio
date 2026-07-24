# 09 — Known Limitations Statement

## 1. Bootloader automatic rollback is not available

**Proven, not suspected** (doc 37): the flashed bootloader is one of 4
fixed, prebuilt binaries shipped with the arduino-esp32 framework
release, never compiled from this project's own configuration. Direct
binary inspection found no app-rollback (`PENDING_VERIFY`) support
compiled into it. **Consequence**: a validly signed, authentic, but
functionally unhealthy OTA candidate will install and reboot, and will
**not** be automatically reverted by the bootloader. Recovery in that
scenario requires physical USB access (doc 04). Application-level health
confirmation and security-floor advancement (doc 36) work correctly and
independently of this gap, but they are not a substitute for bootloader-
level automatic rollback. **The application must not, and does not,
claim bootloader rollback protection exists** — `bootloader_rollback_engaged`
is explicitly exposed and has read `false` on every confirmation this
entire remediation chain.

## 2. Real sensor / oil-flow accuracy has not been validated

`totalizer_raw_pulses` has read a static, unchanging value (`401`) across
this entire multi-day, multi-session audit trail — no controlled real
flow has been observed. Metrological accuracy against a known reference
quantity has **not** been tested. This requires a physically present
person with a calibrated reference and cannot be performed remotely.
**Classified as: FUNCTIONALLY VERIFIED (queue/sync/telemetry pipeline) —
METROLOGICAL ACCURACY NOT YET CERTIFIED.**

## 3. The deployment candidate is a dev-classification build

`RELEASE_BUILD=1` still correctly fails closed (real production CA
certificate and OTA signing key are not provisioned). Tomorrow's
candidate uses the same TEST signing key (`covio-test-key-2026-07`) this
entire remediation chain has used, and the shared bootstrap API key
(`dev-key-change-me`) is still active on this device (`api_key_status:"default"`).
This is acceptable **only** under the restricted, supervised conditions
of a Controlled Plant Pilot (Level 1) — not for Level 2/3.

## 4. Wi-Fi/credential rotation (RISK-02) remains open

Already tracked since early in this audit trail (docs 00-14 of the
`coviu_oil_meter_p0_remediation`/`_phase2` series) — code-side support
for rotation exists (AP-mode provisioning), but the actual real-network
credential rotation is a human action, still pending, unchanged by this
session.

## 5. Reset-reason evidence — now resolved

Previously open (docs 35-37): whether `"unknown"` reset reports hid a
genuine unmapped case. **Resolved this session**: the specific
esptool-flash reset reads raw value `0` (`ESP_RST_UNKNOWN`) — a genuine
"ESP-IDF itself cannot determine this" result, not a missing switch
case. No further action needed; the `unknown(<n>)` format (doc 36)
already ensures no future unmapped value is hidden.

## 6. Device registry / logical asset identity not provisioned

`logical_device_id` has read `null` throughout this entire audit trail —
this specific unit has never been through the factory/asset-provisioning
workflow that would assign it a permanent, plant-facing logical ID
(`COV-000123`-style). Plant-side asset mapping must be established
separately before or during installation.

## 7. Server/database is the bench stub, not production infrastructure

`server/server.py` remains explicitly a bench/dev stub (its own module
docstring says so) — SQLite, plain HTTP, single-process. Whether the
plant deployment target is this same stub (acceptable only for a
Level-1 supervised pilot, with the endpoint pointed at a plant-local,
still-isolated instance) or a real backend (LCS/cloud) implementing the
same documented contract is a decision this report does not make —
flagged explicitly rather than assumed.
