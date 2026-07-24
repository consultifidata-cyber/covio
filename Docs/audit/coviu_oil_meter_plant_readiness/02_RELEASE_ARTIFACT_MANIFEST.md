# 02 — Release Artifact Manifest

The exact firmware proposed for tomorrow's controlled plant pilot.

## Identity

```
Git commit:            856972ef6106d662c5f8a7f5b71c9a60ce40edc1
Working tree at build:  clean (git status --short showed zero tracked
                         modifications; only this session's own untracked
                         audit docs, which do not affect the build)
Build environment:      esp32dev  (PlatformIO espressif32@6.5.0, Arduino
                         framework, board override esp32-s3-devkitc-1,
                         board_build.flash_mode=dio, board_upload.flash_size=16MB,
                         board_build.partitions=default_16MB.csv)
FW_VERSION:             1.0.0   (as committed -- NOT bumped for this freeze)
FW_SECURITY_VERSION:    1       (as committed)
DEVICE_MODEL:           covio-oilflow-v1
build_commit (embedded): 856972ef6106d662c5f8a7f5b71c9a60ce40edc1
build_dirty (embedded):  false
build_time_utc (embedded): 2026-07-23T18:31:53Z
```

**IMPORTANT — build reproducibility caveat, discovered while freezing this
candidate**: `BUILD_TIME_UTC` is embedded as a compile-time string
constant (`scripts/generate_build_identity_extra.py`), so re-running `pio
run` on byte-identical source produces a **different** `firmware.bin`
hash each time (the timestamp differs). The hash below is not "the hash
of this source tree in general" — it is the hash of **the one specific
build that was actually flashed to the device**, independently confirmed
by grepping the built binary for its own embedded `build_time_utc` and
`build_commit` strings before packaging it here. Any future rebuild
intended to reproduce this exact release must be flashed and reverified
the same way — do not assume a fresh `pio run` byte-matches this file.

## Artifact hashes

```
covio-oilflow-v1-1.0.0-856972e.bin
  SHA-256: 136ef36ccf3d1885bb78a17473ee68d47f65e83318e6aff7e5fc9953f0fb2031
  Size:    1,025,680 bytes

bootloader.bin (prebuilt, framework-shipped -- see doc 37 §4-8; not
  project-specific, included here only for completeness of what gets
  written during flash)
  SHA-256: 08ce4b821b21b90cf529b6635afe4d346fd74139c75b55b537d36247d143150c
  Size:    14,032 bytes

partitions.bin (generated from platformio.ini's board_build.partitions=default_16MB.csv)
  SHA-256: bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce
  Size:    3,072 bytes

OTA public verification key fingerprint (ota_keys.h's
  COVIO_OTA_PUBLIC_KEY_PEM, SHA-256 of the DER-encoded
  SubjectPublicKeyInfo):
  56dce88c44d43cb8ffac47a27797889f4b77483b507b08c326ac8081937aeb3e
  key_id: covio-test-key-2026-07  (TEST key -- see 09_KNOWN_LIMITATIONS.md)
```

## What this build is, and is not

- This is the `esp32dev` (bench/dev) environment. `RELEASE_BUILD=1` (the
  `release` environment) still correctly fails closed at compile time on
  two pre-existing guards (`certs.h`'s placeholder CA cert,
  `ota_keys.h`'s placeholder OTA signing key) — real production
  certificates and a real production signing key have not been
  provisioned. **The device deployed tomorrow will therefore run a
  dev-classification build, using the same TEST signing key this entire
  remediation chain has used, not a hardened production release image.**
  This is not a defect in tomorrow's specific candidate — it is an
  honest statement of what "release" actually means today, and is
  reflected in the Go/No-Go decision (doc 01) and known-limitations
  statement (doc 09).
- `api_key_status` on the device reads `"default"` (the shared bootstrap
  key `dev-key-change-me`) — RISK-02's own already-tracked, still-open
  human-action item (Wi-Fi/API-key rotation) is unchanged by this
  release freeze.

## Release artifact directory contents

```
Docs/audit/coviu_oil_meter_plant_readiness/release_artifact/
  covio-oilflow-v1-1.0.0-856972e.bin        <- the approved firmware binary
  covio-oilflow-v1-1.0.0-856972e.bin.sha256 <- sha256sum-format hash file
```
**No private signing key is included** (`server/tools/.test_signing_key.pem`
is gitignored, was never copied here, and this directory was reviewed
before writing this manifest to confirm that). Flashing command,
recovery instructions, and the configuration checklist are documented
separately (docs 03/05/09 in this same folder) rather than duplicated
into the artifact directory itself, to keep that directory limited to
exactly the binary + hash the mandate asked for.

## Exact flash command (established, hash-verified process — proven
repeatedly this remediation chain, docs 33/35/36/37)

```
pio run -e esp32dev -t upload --upload-port COM6
```
Writes bootloader (`0x0`), partition table (`0x8000`), otadata (`0xe000`),
and application (`0x10000`) — never NVS, never LittleFS/queue. Requires
`esptool`'s `Hash of data verified.` on all four regions to consider the
flash successful.
