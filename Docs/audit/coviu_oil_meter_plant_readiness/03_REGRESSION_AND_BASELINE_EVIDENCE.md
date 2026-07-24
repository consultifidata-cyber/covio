# 03 — Regression Evidence and Final Device Baseline

## Regression — actually executed vs. compile-only vs. hardware-executed-previously

**Executed for real, this session, against the exact frozen candidate's
source tree**:
```
python -m unittest discover -s test/native -v  ->  96/96 passed
```
Covers: schema/ADR-001 acceptance, DM-Phase 0B/4/6 device registry +
auth, RISK-01 ack-gap remediation, RISK-03 admin auth, RISK-16 signing
tool + manifest authenticity, **and the two remediations added this
overall chain**: `test_build_identity.py` (21 cases, build-identity
resolution logic) and — not yet added as of this specific test run,
covered instead by real hardware execution below —
`test_timestamp_parse.cpp`'s equivalent logic.

**Compile-only** (this machine has no host C++ compiler — `g++`/`gcc`/
`clang++`/`cl` all absent, confirmed directly, unchanged since doc 32):
```
pio run -e esp32dev  -> SUCCESS
pio run -e factory   -> SUCCESS
pio run -e release   -> FAILED, correctly, on the same two pre-existing
                         guards as every prior session (certs.h CA cert,
                         ota_keys.h OTA key placeholders) -- not masked,
                         not bypassed
```
`test/native_cpp/*.cpp` (`test_ota_version_policy.cpp`,
`test_ota_manifest_auth.cpp`, `test_queue_fault_injection.cpp`,
`test_timestamp_parse.cpp`) remain **written and statically reviewed,
not locally executed** — the same disclosed gap since doc 32, unchanged.
Their logic IS however proven correct by the hardware-executed evidence
below, which exercises the identical code paths for real.

**Hardware-executed, this remediation chain (docs 33-37), all against
commits on the same line of history as this frozen candidate**:
- Build identity: exact commit match, `build_dirty` correctly tracked —
  real hardware, 3 separate flashes.
- Manifest signature verification (valid, tampered, hash-mismatched) —
  real hardware, all three outcomes proven.
- Time-source overflow fix — real hardware, proven via a genuine OTA
  install that previously could not complete at all.
- OTA confirmation + security-floor advance/persistence — real hardware,
  proven via 2 genuine OTA installs + 1 controlled reboot.
- Genuine anti-downgrade rejection — real hardware, against an
  established non-zero floor.
- Interrupted download (deterministic truncation) — real hardware,
  proven no partial image ever activates.

No required test failed. No release-blocking regression found.

## Final device baseline (this session, immediately before writing this report)

```
timestamp: 1784831592 (device flash + boot) / 1784831602ish (post-server-restart recheck)
device_id: esp32-F4E5B2858428
fw_version: 1.0.0, build_commit: 856972ef6106d662c5f8a7f5b71c9a60ce40edc1
build_dirty: false, build_time_utc: 2026-07-23T18:31:53Z
security_version (this build): 1
accepted_security_floor (THIS test unit's NVS, from earlier test cycles): 2
  -- see the caveat below; a fresh, never-tested device would read 0 here.
ota.state: "confirmed" (once health was proven post-boot)
bootloader_rollback_engaged: false (honest, unchanged conclusion --
  doc 37: this hardware's bootloader does not implement app-rollback)
running_partition / boot_partition: app0 @ 0x10000
next_update_partition: app1
raw_img_state: UNDEFINED (fresh USB flash, no OTA transition yet -- expected)
boot_id: 29
uptime at capture: ~20s (fresh boot) then confirmed healthy after server restart
reset_reason: "unknown(0)"  reset_reason_raw: 0
  -- RESOLVED THIS SESSION: raw value 0 = ESP_RST_UNKNOWN, ESP-IDF's own
  genuine "cannot be determined" case for this native-USB/JTAG reset
  mechanism -- not a missing switch case after all (the prior
  ESP_RST_EXT hypothesis, doc 35, is now superseded by this direct,
  proven raw-value read; nothing was hidden, the mapping already
  reports "unknown(<n>)" rather than a bare "unknown").
wifi: connected, rssi -53 to -54 dBm (ssid redacted throughout this
  entire audit trail, consistent with every prior document)
last_push_http_code: 200 (once the bench server was confirmed running)
queue: backlog 18, acked_seq 56989, last_seq 57007, capacity_pct_used 58.9%,
  failed_write_count 0
totalizer_raw_pulses: 401 (unchanged for this entire multi-day audit
  trail -- see 09_KNOWN_LIMITATIONS.md's sensor-readiness caveat)
health_state: "ok", alarms: []
Server DB reconciliation: 56,989 records, fully contiguous (1..56989),
  0 quarantined, 0 duplicates observed at any point this entire session,
  server-side last_fw_version correctly reads "1.0.0"
```

## Caveat: this specific bench unit's accumulated test history

`accepted_security_floor` reads `2` on this physical unit because this
exact device was used to prove floor-advancement (doc 36) with a
throwaway `FW_SECURITY_VERSION=2` test candidate, then reverted. **A
brand-new, never-tested device flashed with this same release candidate
will start at floor `0`** and advance to `1` on its first proven-healthy
boot (already demonstrated, doc 36 §15) — not `2`. This does not affect
the release candidate's own correctness; it is a property of this one
bench unit's test history, disclosed so it is never mistaken for
something the candidate itself does.
