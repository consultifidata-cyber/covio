# 11 — Explicit Deferred Architecture Items (Part 12)

None of the following were attempted this phase, per explicit
instruction. Each is a separate future work item.

| # | Item | Business risk if deferred | Technical scope | Migration/reprovisioning risk | Est. testing required | Can existing devices be upgraded safely? |
|---|---|---|---|---|---|---|
| 1 | Secure boot enablement | Physical device theft/tampering could extract or replace firmware | eFuse burn (one-way), requires a real production signing key first | **High** — irreversible per device; must be done at first-flash/factory time, not in the field | Full re-verification of every OTA/boot path against a secure-boot image | No — must be done before first deployment, not retrofittable without physical re-flash + eFuse burn |
| 2 | Flash encryption enablement | Physical extraction of NVS credentials | eFuse-based, one-way | **High**, same as above | Full re-verification | No, same constraint |
| 3 | Custom rollback-capable bootloader | An authentic-but-unhealthy OTA has no automatic recovery | Requires building the bootloader from ESP-IDF source directly (bypassing PlatformIO's Arduino-framework prebuilt bootloader) — a real toolchain change | Medium-high — a new bootloader must be flashed once, then the existing app-level OTA logic should work unchanged | A full repeat of the OTA hardware suite (docs 33-37 of the phase-2 remediation) against the new bootloader | Possible via a supervised USB reflash, not remotely |
| 4 | PlatformIO-to-ESP-IDF migration | N/A directly, but enables #3 and finer-grained bootloader control | Large — different build system, different project layout | High — touches everything | Full regression across this entire audit trail's worth of tests | N/A until the migration itself is proven |
| 5 | Partition expansion (larger queue partition) | Longer offline buffering | Requires a different partition table, likely trading OTA dual-app-slot space or using larger flash | Medium — existing devices would need reprovisioning with a new partition table (data-preserving migration not guaranteed) | Full capacity re-calculation + real fill-to-capacity test | Needs careful design — not a drop-in |
| 6 | Multi-day offline-storage redesign (beyond partition size — e.g. external storage) | Same as #5, more comprehensive | Hardware change (external SD/flash) | High — physical hardware change | Full redesign + test | No — hardware modification |
| 7 | Production PKI rollout (real OTA signing key + rotation/revocation) | Single test-key compromise currently affects the whole fleet (RISK-19) | Key generation/management process, multi-key trust support in `ota_keys.h` | Low for individual devices (just a new compiled-in public key + reflash) | Re-run the full OTA authenticity suite against the new key | Yes, via a supervised reflash |
| 8 | Per-device hardware-backed credential storage (e.g. secure element) | Reduces blast radius of a single device's physical compromise | New hardware component | High — physical BOM change | Full redesign | No — hardware change |
| 9 | Thirty-day soak certification | Confidence for unattended/normal production | No code change — a test campaign | None (test-only) | 30 continuous days, per the enterprise re-audit's own soak plan (doc 15) | N/A |

## Priority note

Items #7 (production PKI) and #9 (30-day soak) are the lowest-risk,
highest-value next steps — neither requires a hardware/bootloader
change, unlike #1-6 and #8. #3 (rollback-capable bootloader) is the
single item that would close the structural rollback gap, and is
explicitly the most consequential to get right before any expansion
beyond a supervised pilot.
