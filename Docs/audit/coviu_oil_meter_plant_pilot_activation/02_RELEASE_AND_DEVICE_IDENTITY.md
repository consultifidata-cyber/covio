# 02 — Release and Device Identity (Part 1 Baseline + Part 3 Build/Commit)

## Pre-change baseline (captured before any edit this phase)

```
Git commit:        856972ef6106d662c5f8a7f5b71c9a60ce40edc1
Device build_commit: 856972ef6106d662c5f8a7f5b71c9a60ce40edc1  (match)
Security version:  1
Accepted floor:    2 (this bench unit's test history, see prior audit)
Boot ID:           29
Uptime:            2,201,982 ms
Server URL:        http://192.168.1.3:8000 (bench address — the confirmed blocker)
Device ID:         esp32-F4E5B2858428
Plant ID:          not applicable (field does not exist)
Wi-Fi fingerprint: SSID "Airtel_amar_3999" (redacted beyond SSID — password never
                   read or displayed by this or any prior phase)
API credential:    api_key_status "default" (redacted — raw value never displayed)
Queue:             backlog 51, acked_seq 58384, last_seq 58435
Totalizer:         401
Filesystem usage:  60.3%
Alarms:            none
Health state:      ok
Server DB:         contiguous through the device's own last_seq, 0 quarantined
```

## Change made this phase

Serial-console credential-exposure fix (doc 03) — the only code change
this phase.

## Build and commit evidence

```
Old commit:  856972ef6106d662c5f8a7f5b71c9a60ce40edc1
New commit:  443dc448423b9fec2a48fb4bf7ac02740e6c2341
Changed:     provision.h (+29/-1)
Added:       credential_display.h, test/native_cpp/test_credential_display.cpp
Working tree after commit: clean of tracked changes

pio run -e esp32dev  -> SUCCESS (both pre- and post-commit)
pio run -e factory   -> SUCCESS
pio run -e release   -> pre-commit: FAILED, build-identity dirty-tree gate (expected)
                      -> post-commit (clean): FAILED on the SAME pre-existing
                         ota_keys.h placeholder-key guard as every prior
                         session -- not masked, not bypassed

python -m unittest discover -s test/native -v  -> 96/96 passed (unchanged suite,
  zero regressions from this phase's change)

Firmware artifact (the one actually flashed):
  covio_firmware, esp32dev environment
  SHA-256: e0cfb551adaf3422f234f831317dd62efccd518742c560d063e3307151d04ba6
  Size:    1,026,160 bytes
  Embedded build_commit (confirmed via direct ELF grep): 443dc448423b9fec2a48fb4bf7ac02740e6c2341
```

## Flash evidence

```
pio run -e esp32dev -t upload --upload-port COM6 -v
4 regions erased+written (bootloader/partition-table/otadata/app), each
independently "Hash of data verified." by esptool. No erase_flash, no
NVS/LittleFS in the write set. [SUCCESS] Took 66.71s
```

## Post-flash identity (live, this session)

```
build_commit: 443dc448423b9fec2a48fb4bf7ac02740e6c2341  MATCH
build_dirty:  false
boot_id:      30 (was 29, exactly +1, no boot loop)
accepted_security_floor: 2 (UNCHANGED, preserved)
totalizer_raw_pulses:    401 (UNCHANGED, preserved)
Server DB: fully contiguous both before and after the flash, 0 quarantined
health_state: ok (once server contact resumed)
```

**Build identity cross-check: source commit, compiled binary, flashed
binary, and live `/api/v1/info` all agree exactly — no
`BUILD IDENTITY MISMATCH`.**
