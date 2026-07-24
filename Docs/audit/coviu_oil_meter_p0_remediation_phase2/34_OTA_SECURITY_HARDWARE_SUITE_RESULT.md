# 34 — OTA Security & Failure-Recovery Validation (Parts 8-13)

Executes Parts 8-13 against the hardened baseline (`8a9c9a9033f866b421722867f6540c317a35ac89`,
doc 33). **Stopping after Part 13 as instructed — no sensor/production
certification attempted.** No production firmware was modified during
this validation.

## Verdict: OTA SECURITY VALIDATION FAILED

Not because a security check let something bad through — every gate that
COULD be reached behaved correctly and fail-closed. It failed because a
genuine, precisely-diagnosed firmware defect (found by this very
validation, exactly what hardware testing exists to catch) makes the
manifest-authenticity gate permanently unreachable-as-accepting on real
hardware, which transitively blocks Tests 1, 3, 4, 5, and 6. Full
reasoning below.

## 1. OTA environment

```
Device:      esp32-F4E5B2858428, COM6, ESP32-S3
Running:     fw_version 1.0.0, build_commit 8a9c9a9033f866b421722867f6540c317a35ac89,
             build_dirty false, boot_id 20 (unchanged throughout this suite)
Bench server: http://192.168.1.3:8000, PID unchanged since doc 29/33
accepted_security_floor: 0 (never raised -- see Test 1 below)
Signing key: server/tools/.test_signing_key.pem (gitignored, unchanged),
             public key matches ota_keys.h's COVIO_OTA_PUBLIC_KEY_PEM
             (independently verified against every manifest below)
```

## 2. Test matrix (planned vs. actually executed)

| Test | Planned | Actually executed | Outcome |
|---|---|---|---|
| 1. Normal OTA | Full install + reboot + confirm | Manifest served, polled, **rejected before download** | BLOCKED (root cause below) |
| 2. Anti-downgrade | Reject security_version below floor | **Substituted**: hw_compat mismatch (same gate function, same code path, reachable) | **PASSED** — genuine hardware evidence |
| 2b. True downgrade | security_version < floor | Not testable — floor is still 0 (Test 1 never raised it) | UNTESTABLE this session (see below) |
| 3. Invalid signature | Reject on bad ECDSA sig | Not attempted | BLOCKED (same root cause — see below) |
| 4. Hash mismatch | Reject after download | Not attempted | BLOCKED (same root cause) |
| 5. Interrupted download | Recover from a cut transfer | Not attempted | BLOCKED (same root cause) |
| 6. Rollback | Bootloader auto-rollback | Not attempted | BLOCKED (same root cause) |

## 3. Test 1 — what actually happened, in full

Built a fresh candidate from the exact committed hardened baseline
(throwaway `FW_VERSION` bump to `1.0.1-hardened-candidate`, reverted
immediately after the build — same discipline this project has used for
every prior test candidate). Independently verified before serving:

```
Candidate SHA-256:  b3e45eb7e84cd69bec101b5eb9478f00c5e78ef2a743a5408768ce62a04f7c85
Manifest SHA-256:   (recorded in evidence/ota_hardware_suite/signed_manifest_test1.json)
Signature:          VALID (independently verified against ota_keys.h's public key)
security_version:   1 (same-or-higher than floor 0 -- not a downgrade)
hw_compat:          covio-oilflow-v1 (matches)
schema_version:     1 (matches)
```

Served via the real path (`server/firmware/covio-1.0.1-hardened-candidate.bin`
+ `server/firmware/manifest.json`). Polled `/api/v1/status` for ~340s.
Result:
```
ota.last_auth_reject_reason: "no_time_source"
ota.running_version: "1.0.0"  (unchanged -- no download was attempted)
```
Confirmed via direct serial observation (200s bounded capture) that this
was **reproducible across at least two separate `OTA_POLL_MS` ticks**
(~5 minutes apart) with the identical rejection reason both times, and
that `uptime_ms` climbed continuously throughout with **no reboot** — this
is not a boot loop or crash, the device is healthy and simply never
accepts the manifest's authenticity check.

## Root cause — found and precisely diagnosed, not worked around

`ota.h::verifyManifestAuthenticity_()` fails closed with
`OTA_AUTH_NO_TIME_SOURCE` whenever `sync.haveServerTime()` is false.
`sync.h::haveServerTime_` is set true only inside `pushOnce()`, after
parsing `server_time_ms` from the server's push response via
`sync.h`'s own `extractLong_()`:

```cpp
static long extractLong_(const String& s, const char* key) {
    ...
    long v = 0; bool any = false;
    while (i < (int)s.length() && (isdigit(s[i]))) { v = v*10 + (s[i]-'0'); i++; any = true; }
    return any ? v : -1;
}
```

`long` on `arduino-esp32`/Xtensa is **32 bits**, not 64. `server_time_ms`
is a real Unix millisecond timestamp (`server.py`:
`int(time.time() * 1000)`) — a 13-digit number today. Simulating the
exact same digit-by-digit accumulation with 32-bit signed wraparound:

```
digits parsed: 1784813824816 (13 digits)
resulting 32-bit signed value: -1892570320
fails the `if (serverTimeMs >= 0)` check: True
```

**This overflow is not specific to today's date** — any millisecond-
epoch value beyond `2^31 ≈ 24.86 days` after 1970-01-01 overflows a
32-bit signed `long` the same way. This means `haveServerTime_` can
**never** become true against any real-world clock, on any device,
ever — a systemic, 100%-reproducible defect, not an environmental fluke.
Every successful push still updates `lastSyncMs_`/`haveSync_`/`ack_seq`
correctly (a completely separate code path a few lines earlier in the
same function) — which is exactly why `last_push_http_code:200` and
fresh `last_sync_ms_ago` values were visible the entire time even though
OTA time-sourcing was silently broken underneath.

**Why this was never caught by the existing native test suite**:
`ota_manifest_auth.h`'s own tests (`test_ota_manifest_auth.cpp`, "13/13
passing" per doc 20) operate on already-parsed native `long`/`string`
fields passed in directly — they never exercise `sync.h`'s raw-JSON
`extractLong_()` against a real 13-digit value. This is precisely the
"ESP32-coupled integration wiring is compile-verified only, not executed"
gap doc 26 already disclosed for RISK-16 — hardware execution (this
validation) is exactly what was needed to surface it, and did.

**Minimal corrective patch proposed, NOT applied** (per "do not modify
production firmware unless explicitly authorized"): widen the
accumulator `sync.h::extractLong_()` uses for `server_time_ms`
specifically to a 64-bit type (`long long`/`int64_t`), and store/derive
`serverUnixS_` from that — the existing `serverUnixS_` field itself
(epoch **seconds**, not ms) fits comfortably in 32 bits until 2038, so
only the millisecond-value *parsing* step needs widening, not every
downstream field.

## 4. Test 2 (substituted) — hardware-compatibility gate, real evidence

Since `accepted_security_floor` is still 0 (Test 1 never completed to
raise it), a genuine "security_version below an already-proven floor"
scenario has no floor to be below. `ota_version_policy.h::evaluateOtaCandidate()`
runs its `hw_compat` check in the **same function, same gate, before**
the (currently-blocked) authenticity check — a legitimate, real
substitute proving the pre-authenticity version-policy gate genuinely
functions on hardware.

Signed and served a manifest with `hw_compat:
"covio-oilflow-v2-WRONG-MODEL"` (`security_version:0`, not a downgrade;
`schema_version:1`, matches — isolating the hw_compat check specifically).
Result, observed live via `/api/v1/status`:
```
ota.last_reject_reason: "hardware_mismatch"
ota.running_version: "1.0.0"  (unchanged)
```
**PASSED** — real, hardware-executed proof that `OTA_REJECT_HW_MISMATCH`
fires correctly, before any authenticity/download attempt, exactly as
`ota_version_policy.h` specifies.

## 5. Tests 3-6 — not attempted, precisely because of the diagnosed root cause

Each requires passing the SAME authenticity gate Test 1 could not pass:
- **Test 3** (invalid signature) is evaluated *inside*
  `verifyManifestAuthenticity_()`, strictly *after* the `no_time_source`
  check — unreachable right now.
- **Test 4** (hash mismatch) requires a successful authenticity pass to
  ever reach `doVerifiedUpdate_()` — unreachable.
- **Test 5** (interrupted download) — same reason.
- **Test 6** (rollback) requires a successful-looking install to first
  occur — unreachable.

Attempting any of these now would only re-observe the identical
`no_time_source` rejection already conclusively diagnosed above — not
genuine additional evidence, so not repeated four more times at ~5
minutes of real wait each.

## 6. Regression checks (after every scenario)

```
/api/v1/info    -> device_id/build_commit/build_dirty/fw_version all unchanged
/api/v1/health  -> {"health_state":"ok","alarms":[]}
/api/v1/status  -> health ok, queue draining normally throughout
boot_id         -> 20, unchanged across the entire suite (zero reboots)
Wi-Fi           -> connected throughout
Server DB       -> records fully contiguous 1..43639, 0 quarantined,
                   before/during/after this entire test session
```
No crash, no watchdog reset, no boot loop, no data loss, no duplicate or
missing sequence at any point in this validation.

## 7. Investigation: `reset_reason = "unknown"` (carried over from doc 33)

Read `diagnostics.h::resetReasonStr_()`'s complete switch and the
installed ESP-IDF's actual `esp_reset_reason_t` enum
(`framework-arduinoespressif32/tools/sdk/esp32s3/include/esp_system/include/esp_system.h`):

```
Enum values that exist: ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT,
ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO
Switch cases handled:  POWERON, SW, PANIC, INT_WDT/TASK_WDT/WDT,
                        BROWNOUT, DEEPSLEEP
Missing:                ESP_RST_EXT, ESP_RST_SDIO, ESP_RST_UNKNOWN (harmless,
                        already synonymous with the default branch)
```

**Determination: this is purely a missing switch case, not a hidden
fault.** `esptool`'s `Hard resetting via RTS pin...` (used by every USB
upload, doc 33's flash included) works by toggling the RTS/DTR-driven
auto-reset circuit into the chip's physical `EN`/`CHIP_PU` pin — a
textbook **external pin reset**, which ESP-IDF's own header comment
literally labels `ESP_RST_EXT`. The switch's existing `ESP_RST_PANIC`
case would have fired and reported `"panic"` had a genuine crash
occurred; it did not fire, and every independent signal this session
(clean `boot_id` +1 after the flash, zero alarms, continuous healthy
uptime through this entire OTA suite) confirms no crash condition is
being hidden.

**Proposed minimal patch, NOT applied:**
```cpp
case ESP_RST_EXT:  return "external_pin";
```
added to `resetReasonStr_()`'s switch in `diagnostics.h`, immediately
alongside the existing cases.

## 8. Defects discovered

1. **(Blocking, this validation's main finding)** `sync.h::extractLong_()`'s
   32-bit `long` accumulator overflows on any real millisecond-epoch
   `server_time_ms` value, permanently preventing `haveServerTime()` from
   becoming true and therefore permanently blocking the OTA manifest
   authenticity gate from ever accepting a real manifest. Fails
   **closed** (rejects everything) rather than open — the least-bad
   direction for a bug to fail in, but it fully blocks RISK-16's
   "authenticated OTA" capability from ever functioning in real
   operation as currently implemented.
2. **(Cosmetic, non-blocking)** `diagnostics.h::resetReasonStr_()` has no
   case for `ESP_RST_EXT`, so a completely normal external/RTS-pin reset
   reports `"unknown"` instead of a specific reason.

Neither defect was fixed in this pass, per "do not modify production
firmware during this validation unless explicitly authorized."

## 9. Honest limitations

- Tests 3-6 have **zero** direct hardware evidence this session — their
  "blocked" status is a logical consequence of the diagnosed root cause,
  not independently re-confirmed by observation, in the interest of not
  spending ~20 more minutes of real wall-clock time re-observing the
  identical already-diagnosed rejection four more times.
- The true "security_version below a proven floor" downgrade scenario
  remains completely untested — it requires Test 1 to first succeed
  (raising the floor above 0), which requires the defect above to be
  fixed first.
- This defect has, in all likelihood, been present since RISK-16's
  original commit (`d24051f`) and therefore in every "OTA CI-proven
  (local)" claim in this project's prior documents — those claims are
  about the pure-logic/native-test layer, which never exercised this
  specific raw-JSON-parsing code path; they are not being retracted, but
  this finding shows the boundary of what they actually proved was
  narrower than "OTA works end-to-end," exactly as those documents
  themselves already disclosed.

---

**STOPPING AFTER PART 13, as instructed.** Part 14 (sensor/production
certification) was not attempted. Recommend the `sync.h` fix above be
separately authorized, applied, and this entire Test 1/3/4/5/6 sequence
re-attempted before any further OTA certification claim is made.
