# 35 — OTA Time-Source Overflow Remediation and Hardware Re-Certification

Executes the full remediation-and-recertification mandate against doc 34's
failed verdict. **The original defect is fixed and proven on real
hardware. A second, separate, previously-undiscovered defect was found
during re-certification, and is why full certification still cannot be
granted.**

## 1. Previous failed verdict (accepted, not softened)

`OTA SECURITY VALIDATION FAILED` (doc 34) — `sync.h`'s 32-bit `long`
accumulator overflowed on any real Unix millisecond timestamp, permanently
blocking the OTA authenticity gate with `no_time_source`.

## 2. Root-cause reconfirmation (fresh, not relying only on doc 34)

```
File:     sync.h
Function: extractLong_() (private static), called from pushOnce()
Accumulator: `long v = 0;` then `v = v*10 + digit` per character, no bound check
Gate:     `if (serverTimeMs >= 0) { ...; haveServerTime_ = true; }`
Call path: pushOnce() [every PUSH_PERIOD_MS] -> haveServerTime_
           -> ota.h::verifyManifestAuthenticity_() -> `if (!sync.haveServerTime())
              return OTA_AUTH_NO_TIME_SOURCE;` -> surfaced via
              /api/v1/status's last_auth_reject_reason
```
Fresh reproduction this session, current real timestamp:
```
value: 1784816796695 (13 digits)
32-bit signed wraparound: -1889598441
fails `>= 0`: True
```
`long` is 32 bits on arduino-esp32/Xtensa; any epoch-ms value beyond
`2^31 ≈ 24.86 days` after 1970-01-01 overflows identically — not a
today-only fluke.

## 3. Exact remediation

New file **`timestamp_parse.h`** (dependency-free — no Arduino.h/WiFi.h,
matching this project's own `ota_version_policy.h`/`ota_manifest_auth.h`
host-testability pattern): `parseNonNegativeInt64Checked(const char*,
int start, int end, int64_t* out)`.

**`sync.h`** changes:
- New private `extractInt64_()`: same field-location logic as
  `extractLong_()` (first-match-wins on a duplicate key — matches the
  existing, unchanged parser policy), hands the located digit range to
  the checked parser above.
- `pushOnce()`'s `server_time_ms` handling now uses `extractInt64_()`
  into an `int64_t`, only setting `haveServerTime_` if it parses AND is
  `>= 0` — identical fail-closed contract as before, now actually
  reachable for real values. `serverUnixS_` (epoch **seconds**, `long`)
  is unchanged in type — 32-bit signed seconds don't overflow until 2038,
  outside this remediation's scope; only the millisecond *parsing* step
  needed widening.
- `ack_seq`/config-`version` parsing (small values, `extractLong_()`,
  unchanged) is untouched — this fix touches exactly one call site.

**`diagnostics.h`**: added `case ESP_RST_EXT: return "external_pin";` to
`resetReasonStr_()`'s switch.

## 4. Numeric overflow-hardening design

```cpp
inline bool parseNonNegativeInt64Checked(const char* s, int start, int end, int64_t* out) {
  if (s == nullptr || start >= end) return false;
  int64_t v = 0;
  for (int i = start; i < end; i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;
    int digit = c - '0';
    if (v > (INT64_MAX - digit) / 10) return false;   // checked BEFORE the multiply-add
    v = v * 10 + digit;
  }
  *out = v;
  return true;
}
```
Checked *before* every accumulation, never relies on wraparound/UB. Fails
closed (returns `false`, leaves `*out` untouched) for: empty range,
non-digit content anywhere (including a leading `-`, `.`, or letters),
and any value that would exceed `INT64_MAX`. No silent wrap, ever.

## 5. Test changes

`test/native_cpp/test_timestamp_parse.cpp` — 12 cases against the real
pure parser: current valid 13-digit value, zero, small value, exactly
`INT64_MAX`, one greater than `INT64_MAX` (rejected, sentinel untouched),
a 50-digit field (rejected), negative sign, decimal point, alphabetic
content, empty value, the exact value that broke the original hardware
test, and a round-trip check across 6 representative magnitudes.

**Honest disclosure, repeated from doc 34 and still true**: this machine
has no host C++ compiler (`g++`/`gcc`/`clang++`/`cl` all absent, MinGW64
present but compiler-less). This test file was written and statically
reviewed, **not locally compiled or executed**. It IS compile-verified
indirectly — `sync.h` includes `timestamp_parse.h`, and the full firmware
(which includes `sync.h`) compiles cleanly via PlatformIO's xtensa
cross-toolchain (§6 below). Cases requiring `sync.h`'s Arduino-`String`
wrapper (missing field, truncated JSON) or full `Sync`/`Ota` class
behavior (`haveServerTime()`, end-to-end `no_time_source` absence) are
**not** exercised by this file — those are proven instead by the actual
hardware re-run in §10, which is stronger evidence than a unit test could
provide for exactly those cases.

**Existing suites, actually re-executed:**
```
python -m unittest discover -s test/native -v  -> 96/96 passed (2nd run, post-fix)
```

## 6. Compile/build results

```
pio run -e esp32dev  -> SUCCESS (both pre- and post-commit)
pio run -e factory   -> SUCCESS
pio run -e release   -> pre-commit: FAILED on the build-identity dirty-tree
                         gate (real dirty tree at that moment, expected)
                      -> post-commit (clean tree): FAILED on the SAME TWO
                         pre-existing guards as every prior session
                         (certs.h CA-cert placeholder, ota_keys.h OTA-key
                         placeholder) -- proves this remediation neither
                         masks nor bypasses either guard.
```

## 7. Security control diff review

```
git diff --stat -- ota.h ota_manifest_auth.h ota_version_policy.h ota_keys.h certs.h store.h queue.h
-> (empty) -- ZERO changes to any security-critical file.
Only diagnostics.h and sync.h modified (tracked); timestamp_parse.h and
test/native_cpp/test_timestamp_parse.cpp added.
```
Manifest signature enforcement, SHA-256 verification, `hw_compat`
validation, security-version policy, accepted-floor persistence,
replay/freshness (`checkManifestTimeValidity`) — all unchanged, all still
compiled in (confirmed live on hardware in §10). No private signing key
in either rebuilt binary (`grep -a -c "PRIVATE KEY"` on both `.elf`s finds
only mbedtls's own generic PEM-label table, same benign match as every
prior session — no real secret). No debug bypass, no unconditional
accept path introduced.

## 8. New commit and artifact hashes

```
Old commit:  8a9c9a9033f866b421722867f6540c317a35ac89
New commit:  ffef43890d7b6a4da341f8094a71a56b2e8d259d
Changed:     diagnostics.h (+1 line), sync.h (+41/-3 lines)
Added:       timestamp_parse.h, test/native_cpp/test_timestamp_parse.cpp
Working tree after commit: clean of tracked changes
esp32dev artifact: 1,021,632 bytes,
  SHA-256 99f4ebb07db60a687853f2104fb9a1a6381a16ab8c1b2a8b91291bb190d37fcf
Embedded build_commit (confirmed via direct ELF grep): ffef43890d7b6a4da341f8094a71a56b2e8d259d
```

## 9. Pre-flash baseline

```
timestamp: 1784817331
device_id: esp32-F4E5B2858428, fw_version 1.0.0, boot_id 20
build_commit: 8a9c9a9033f866b421722867f6540c317a35ac89 (old, pre-remediation)
accepted_security_floor: 0, security_version: 1
queue: backlog 1, acked_seq 46562, last_seq 46563, capacity 48.3%, failed_write_count 0
totalizer_raw_pulses: 401
wifi: connected (ssid redacted), rssi -57
reset_reason: "unknown" (pre-fix)
health_state: ok, alarms: []
Server DB: records 1..46562 contiguous, 0 quarantined
```

## 10. Flash evidence

```
pio run -e esp32dev -t upload --upload-port COM6 -v
Serial port COM6, Chip is ESP32-S3 (revision v0.2)
4 regions erased+written (bootloader 0x0, partition table 0x8000,
otadata 0xe000, application 0x10000) -- each independently reported
"Hash of data verified." by esptool. No erase_flash, no full-chip erase.
NVS and the LittleFS/queue partition were never touched (different flash
offsets, outside every erase/write range above).
Leaving... Hard resetting via RTS pin... [SUCCESS] Took 68.27s
```

## 11. Post-flash identity evidence

```
Serial boot log: build_commit=ffef43890d7b6a4da341f8094a71a56b2e8d259d
                 build_dirty=0  boot_id=21 (was 20, exactly +1, no loop)
GET /api/v1/info:   build_commit ffef438... build_dirty:false -- MATCH
GET /api/v1/status: accepted_security_floor 0 (unchanged, correctly
                     preserved -- no OTA had run yet), health ok
```
**`reset_reason` correction — NOT confirmed working, disclosed honestly:**
`/api/v1/metrics` after this flash still reported `reset_reason:"unknown"`,
**not** `"external_pin"` as the doc 34 hypothesis predicted. The
`ESP_RST_EXT` case was added exactly as proposed and compiles cleanly, but
the live hardware value is evidently NOT `ESP_RST_EXT` for this specific
reset mechanism after all — my prior hypothesis (esptool's RTS-pin reset
= external-pin reset) is **not confirmed by hardware evidence** and may
be wrong or incomplete. Reading the actual raw numeric reset-reason value
would require additional firmware instrumentation, which is out of this
pass's authorized scope (fix + apply only what was specified). Reported
as an open, unresolved, still-cosmetic item — not claimed fixed.

## 12. Full OTA test matrix — actually executed results

| Test | Result | Evidence |
|---|---|---|
| **1. Valid signed OTA** | **PARTIAL** | Manifest accepted (no `no_time_source` — the fix works), downloaded, hash verified, installed, rebooted once cleanly (`boot_id` 21→22). **But**: `ota.state` stayed `"none"` (never `pending_verify`/`confirmed`) and `accepted_security_floor` never advanced from 0 — see §13, a newly-discovered, separate defect. |
| **2. Anti-downgrade** | **Substituted, unaffected, still valid** | True downgrade needs floor > 0, unreachable (floor never advances — same new defect). The hw_compat-mismatch substitute from doc 34 remains valid evidence for the pre-authenticity version-policy gate; not re-run (unaffected by anything in this pass). |
| **3. Invalid signature** | **PASSED** | Manifest signed correctly, then `security_version` tampered post-signature. Independently confirmed the tampering breaks verification BEFORE serving. Device: `last_auth_reject_reason:"signature_verify_failed"`, firmware/boot_id unchanged. |
| **4. Hash mismatch** | **PASSED** | Valid, untampered signature; served bytes deliberately differ (same size, flipped bytes) from the signed hash. Device downloaded (auth passed), detected the mismatch, `Update.abort()`'d: `ota.state:"failed"`, **bonus evidence**: `health_state:"degraded"`, `alarms:[{"type":"OTA_FAILED",...}]` — the health/alarm system correctly surfaced the failure too. `boot_id` unchanged, firmware unchanged, floor unchanged. Server DB unaffected (fully contiguous, 0 quarantined). |
| **5. Interrupted download** | **INCONCLUSIVE — my interruption attempt failed** | Killed the bench server process ~15s after serving the manifest, held it down ~8s, restarted it. Device rebooted anyway (`boot_id` 22→23) with `reset_reason:"software"` (a genuine, deliberate `ESP.restart()`, not a crash) — but `fw_version` stayed `"1.0.1-fixed-candidate"`. Reconciling this: Test 5's served binary was a byte-identical copy of the already-running candidate (same compiled `FW_VERSION` string) under a different manifest version/filename — the download most likely **completed successfully** despite my brief server outage (my timing did not actually land inside the transfer window), producing a second genuine successful install rather than a genuine interruption. **Not claimed as a pass or fail for interruption specifically** — the attempt did not exercise what it was meant to. |
| **6. Rollback/recovery** | **NOT ATTEMPTED — judged unsafe given §13's finding** | Two independent real OTA installs (Test 1 and the accidental Test-5 install) both show `esp_ota_get_state_partition()`/`confirmHealthyBoot()` never engaging the pending-verify/confirm bookkeeping. Constructing a deliberately-unhealthy candidate under that condition risks the device booting into it with **no confirmed automatic-rollback safety net**, needing manual USB recovery to get back — not attempted rather than risk stranding the device on an unverified assumption. |

## 13. New defect discovered: confirmation/floor-advancement bookkeeping never engages

Across **two independent, genuinely successful** OTA installs this
session (Test 1's `1.0.1-fixed-candidate`, and the accidental Test-5
completion), `ota.state()` never progressed past `"none"` to
`"pending_verify"` or `"confirmed"`, and `accepted_security_floor` never
advanced from `0` despite `FW_SECURITY_VERSION=1` on both installed
images. Per `ota.h`, this happens because `Ota::confirmHealthyBoot()`
early-returns whenever `pendingVerify_` is false
(`if (!pendingVerify_ || confirmed_) return;`), and `pendingVerify_` is
only set true by `noteBoot()` finding
`esp_ota_get_state_partition() == ESP_OK` **and**
`state == ESP_OTA_IMG_PENDING_VERIFY`. Confirmed `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`
IS compiled in for this exact board/flash-mode combination (checked the
actual bundled `sdkconfig`/`sdkconfig.h` for `esp32s3`/`dio_qspi`, matching
this board's `board_build.flash_mode=dio`) — so the config-level
prerequisite for rollback is genuinely present; something about how this
device's specific OTA transition (or its otadata partition's prior state,
originally initialized by a USB flash rather than a factory-OTA
provisioning flow) reaches `noteBoot()` is preventing the expected
`PENDING_VERIFY` state from ever being observed. **Not fixed in this
pass** — outside the explicitly authorized scope (timestamp overflow +
`ESP_RST_EXT` mapping only) — and investigating further would require
either new firmware instrumentation (not authorized) or a serial console
command (this validation's discipline has been strictly passive/read-only
serial observation throughout; sending commands was never authorized).

## 14. Queue/NVS/database preservation evidence (every scenario)

Across the entire suite (2 real reboots, 1 real download failure, 1
rejected-before-download signature failure): `totalizer_raw_pulses`
stayed `401` throughout (never reset), server DB records stayed fully
contiguous growing from `46562` to `48194` with **zero** quarantined,
duplicate, or missing-sequence rows at any checkpoint, `sd_status:"ok"`
throughout, `failed_write_count:0` throughout, device_id/server_url/
calibration untouched.

## 15. Reset-reason correction evidence

See §11 — **not confirmed working**. Applied exactly as proposed
(`ESP_RST_EXT` case added), compiles cleanly, but live hardware still
reports `"unknown"` after a real esptool-triggered flash reset. The
original hypothesis is not validated by evidence; flagged as still open.

## 16. Defects / observations summary

1. **(Original defect — FIXED, hardware-proven)** `sync.h`'s 32-bit
   `server_time_ms` overflow. Tests 1/3/4 all reaching real
   post-time-check pipeline stages is direct proof.
2. **(New defect — NOT fixed, outside this pass's scope)** OTA
   confirm/floor-advancement bookkeeping never engages on this device,
   across two independent successful installs. Blocks true anti-downgrade
   testing and makes rollback testing unsafe to attempt until resolved.
3. **(Still open, hypothesis disproven)** `reset_reason:"unknown"` for
   the esptool RTS-pin reset — the proposed `ESP_RST_EXT` fix did not
   resolve it; root cause remains genuinely unknown.

## 17. Remaining limitations

- True anti-downgrade rejection (security_version below a **proven**
  floor) remains completely untested — needs defect #2 fixed first.
- Rollback/recovery (Test 6) not attempted — same reason, plus safety
  judgment.
- Test 5 did not genuinely exercise an interrupted transfer.
- `test_timestamp_parse.cpp` remains compile-verified only, not locally
  g++-executed (no host compiler in this environment).
- Real oil-flow sensor validation remains **completely unperformed** and
  explicitly out of scope for this report, per the mandate.

## 18. Final verdict

**OTA SECURITY VALIDATION FAILED**

Significant, real, hardware-proven progress: the specific defect this
mandate authorized fixing is genuinely fixed — three of six tests
(1's mechanics, 3, 4) now execute past the point that blocked all six
previously. But a second, independently-discovered defect (confirmation/
floor bookkeeping) blocks the two most safety-critical remaining tests
(true downgrade, rollback), so full certification is not warranted yet.
Not `OTA SECURITY CERTIFIED WITH OBSERVATIONS` — the un-exercised tests
are not merely "observations," they are currently un-provable on this
hardware for a specific, unresolved reason.

Real oil-flow sensor validation remains a separate, unperformed,
human-assisted phase, exactly as it has been at every prior stage of this
audit trail.
