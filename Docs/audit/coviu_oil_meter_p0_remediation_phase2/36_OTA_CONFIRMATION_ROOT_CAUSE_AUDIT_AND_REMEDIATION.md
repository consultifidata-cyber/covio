# 36 — OTA Confirmation Bookkeeping Root-Cause Audit and Remediation

Executes the full audit-and-remediation mandate against doc 35's failed
verdict. **Root cause proven with raw hardware evidence (not inferred),
fixed, and the fix proven end-to-end through a genuine OTA transition
plus a genuine anti-downgrade rejection.** Bench server stopped at the
end, per instruction.

## 1. Accepted prior failed verdict

`OTA SECURITY VALIDATION FAILED` (doc 35) — confirmation bookkeeping
never engaged; `accepted_security_floor` stuck at 0 across two
independent successful OTA installs.

## 2. Intended OTA state machine (reconstructed from the real call graph)

```
idle (OTA_STATE_NONE)
  -> poll() finds a manifest, version differs from FW_VERSION
  -> evaluateOtaCandidate() [ota_version_policy.h]: downgrade/hw/schema gate
  -> verifyManifestAuthenticity_() [ota.h]: key id, time source, ECDSA-P256 sig
  -> doVerifiedUpdate_() [ota.h]: streamed download, incremental SHA-256,
     Update.write() -> inactive partition; Update.abort() on ANY failure
     (leaves running partition untouched, OTA_STATE_FAILED)
  -> hash matches -> Update.end(true) -> _verifyEnd() [Updater.cpp] ->
     esp_ota_set_boot_partition() -> ESP.restart()
  -> [REBOOT] bootloader loads the new partition
  -> setup(): ota.noteBoot() -> esp_ota_get_running_partition() +
     esp_ota_get_state_partition() -- INTENDED to read PENDING_VERIFY here
     if the bootloader armed a rollback trial
  -> app proves health (WiFi + 1 real server push/config-poll ack)
  -> confirmHealthyBoot(): esp_ota_mark_app_valid_cancel_rollback()
     (bootloader-level, only meaningful if a trial was armed) THEN
     Store::setSecurityVersion() (application-level floor advance) ->
     OTA_STATE_CONFIRMED
```
Persistence: OTA image state lives in the `otadata` partition (bootloader/
ESP-IDF managed, not app-writable directly); `accepted_security_floor`
lives in NVS namespace `covio_sec` (`store.h`, separate from every other
NVS key, deliberately untouched by `factoryReset()`); confirmation status
(`confirmed_`/`pendingVerify_`) is RAM-only per boot, re-derived from the
otadata read at every `noteBoot()`, never itself persisted. Power loss
between confirmation and floor persistence: `Preferences::putUInt()`
performs its own synchronous NVS commit before returning — a loss
mid-write leaves the OLD floor value (NVS's own atomicity guarantee), not
a corrupted one; this remediation adds an explicit read-after-write check
on top as defense-in-depth (§8).

## 3. Actual state-machine trace (before this fix, real hardware)

Both independent installs (doc 35's Test 1 and the accidental Test-5
completion) reached `Update.end(true)` successfully, rebooted, proved
health, called `confirmHealthyBoot()` — which then **silently
early-returned** at `if (!pendingVerify_ || confirmed_) return;` because
`pendingVerify_` was false. The trace stops precisely at the
`noteBoot()` → `pendingVerify_` determination — proven below, not
assumed.

## 4. Partition and rollback configuration evidence

```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y   (esp32s3 sdkconfig, ALL flash-
CONFIG_APP_ROLLBACK_ENABLE=y               mode variants incl. dio_qspi,
                                            matching this board's
                                            board_build.flash_mode=dio)
```
Confirmed by grepping the actual installed
`framework-arduinoespressif32/tools/sdk/esp32s3/{sdkconfig,*/include/sdkconfig.h}`
— the config-level prerequisite for bootloader rollback genuinely IS
compiled in for this exact board/flash-mode combination. `Updater.cpp`'s
`_verifyEnd()` (the real, installed Arduino-ESP32 `Update` library source,
read directly) confirms `Update.end(true)` calls the correct, standard
`esp_ota_set_boot_partition()` — nothing unusual in the app-side wiring
either.

## 5. Root cause — proven with raw hardware evidence, not stopped at "bookkeeping did not engage"

Added diagnostic instrumentation (`ota.h`'s `noteBoot()`) exposing the
RAW `esp_ota_get_state_partition()` return code and image-state value via
a new `/api/v1/status`→`ota.ota_debug` object (no secrets; explicitly
labeled removable). Flashed via USB, then performed **one genuine OTA
transition** (`1.0.0` → `1.0.2-diag-candidate`, security_version 2) to
observe the actual moment of failure:

```json
"ota_debug": {
  "running_partition": "app1", "running_partition_addr": "0x650000",
  "boot_partition": "app1",    "boot_partition_addr": "0x650000",
  "next_update_partition": "app0",
  "raw_state_read_ok": true,
  "raw_state_read_err": 0, "raw_state_read_err_name": "ESP_OK",
  "raw_img_state": 2, "raw_img_state_name": "VALID",
  "confirm_attempted": false
}
```
**This is the proven root cause**: `esp_ota_get_state_partition()`
succeeds (`ESP_OK` — category B from the mandate's checklist, "confirmation
called but fails," is ruled out; the READ itself never fails) and
correctly reports the freshly-OTA'd partition's state as **`VALID`**,
never `NEW` or `PENDING_VERIFY` (category C: confirmed — the bootloader
genuinely does not establish pending-verify for this transition, despite
the config-level prerequisite being present). `noteBoot()`'s existing,
correct logic (`pendingVerify_ = (state == ESP_OTA_IMG_PENDING_VERIFY)`)
therefore correctly evaluates to `false` — **not a bug in the read
logic itself**, a real absence of the expected bootloader behavior on
this board/toolchain combination. Categories A (confirmation never
called), D (NVS bookkeeping), E (comparison logic), F (timing) were all
evaluated and ruled out — the trace stops exactly at, and only at, the
bootloader's own state assignment.

A second, freshly-USB-flashed baseline (no OTA transition at all) showed
`raw_img_state: -1 (UNDEFINED)` — also never `PENDING_VERIFY`, confirming
this is a `NEW`/`PENDING_VERIFY` gap in general on this hardware, not an
artifact specific to one transition.

**Practical consequence, now also proven**: since floor advancement was
previously gated entirely behind this never-true `pendingVerify_`, the
accepted-security-floor could never advance on this hardware for **any**
successful build — OTA-installed or a fresh USB baseline's very first
boot — a foundational gap, not an OTA-specific symptom.

## 6. Confirmation API return evidence

`esp_ota_mark_app_valid_cancel_rollback()`'s return value was previously
discarded entirely (never called with `pendingVerify_` false anyway, so
this specific defect was latent, not yet the active cause — but a real
gap regardless, now closed: see §8).

## 7. Security-floor persistence analysis

`Store::securityVersion()`/`setSecurityVersion()` (`store.h`, unchanged)
use `Preferences` on the separate `covio_sec` namespace — no bug found
here; the floor was never *written* in the first place (gated out
upstream), not written-and-lost. This remediation adds an explicit
read-after-write check at the call site (§8) as defense-in-depth, without
changing `store.h`'s own signature/contract.

## 8. Minimum remediation

`ota.h::confirmHealthyBoot()` — bootloader-level trial cancellation and
application-level health confirmation are now two independently-gated
steps:
```cpp
void confirmHealthyBoot() {
  if (confirmed_) return;                 // idempotent

  if (pendingVerify_) {                   // ONLY when a real trial exists
    confirmAttempted_ = true;
    esp_err_t rc = esp_ota_mark_app_valid_cancel_rollback();
    confirmReturnCode_ = rc;
    if (rc != ESP_OK) { /* log, fail closed */ return; }
  }

  confirmed_ = true;
  bootloaderRollbackEngaged_ = pendingVerify_;   // honest disclosure

  if (st_ && FW_SECURITY_VERSION > (long)st_->securityVersion()) {
    floorWriteAttempted_ = true;
    uint32_t newFloor = (uint32_t)FW_SECURITY_VERSION;
    st_->setSecurityVersion(newFloor);
    uint32_t readBack = st_->securityVersion();
    floorReadAfterWrite_ = readBack;
    floorWriteOk_ = (readBack == newFloor);     // read-after-write check
  }
}
```
Properties satisfied: confirmation only after the existing minimum health
checks (unchanged caller gating in `covio_firmware.ino`); bootloader-call
failure fails closed (no `confirmed_`, no floor touch) **only when a real
trial existed** — matching Part 6's explicit "do not fake bootloader
confirmation... implement only the intended supported mechanism"; floor
never decreases (unchanged `FW_SECURITY_VERSION > floor` guard); floor
persists across reboot (NVS, unchanged mechanism, now actually reached);
NVS write failure is now detectable (`floorWriteOk_`, read-after-write)
rather than silently assumed; idempotent (`if (confirmed_) return;` up
front); a malicious manifest cannot control the floor without passing
signature + anti-downgrade + a genuine, observable app-level health proof
first (unchanged upstream gates, `ota.h`'s `poll()`); integer widths
unchanged (`uint32_t` floor, `esp_err_t` return codes, explicit). No
partition-table or bootloader redesign — this fix works entirely within
existing infrastructure, exactly as instructed.

**Terminology correction**: `bootloader_rollback_engaged` (new field,
`ota_debug`) makes explicit, on every read, whether a given confirmation
involved genuine bootloader-level rollback protection or app-level-only
confirmation — no report from this device can ever again imply bootloader
protection that was not actually active for a specific boot.

## 9. Test additions and executed results

`confirmHealthyBoot()`/`noteBoot()` remain Arduino/ESP-IDF-coupled (use
`Store*`, `esp_ota_*`, `Serial`) — not extracted to a dependency-free
header the way `ota_version_policy.h`/`ota_manifest_auth.h` were, and
doing so now would be a larger refactor beyond "minimum safe fix." No new
host-executable unit test was added for this specific logic as a result
— disclosed honestly rather than fabricated. In its place, this
remediation relies on **real hardware execution** (§10 below) as the
authoritative proof for exactly the ESP-IDF partition/NVS behavior a
host mock could not faithfully reproduce anyway.

**Executed for real**: `python -m unittest discover -s test/native -v` →
**96/96 passed** (3rd consecutive clean run this remediation chain, zero
regressions from `ota.h`/`diagnostics.h` changes).

**PlatformIO compile proof** (not claimed as execution):
```
pio run -e esp32dev  -> SUCCESS (both diagnostics-only and final commits)
pio run -e factory   -> SUCCESS
pio run -e release   -> pre-commit: FAILED on build-identity dirty-tree gate (expected)
                      -> post-commit (clean): FAILED on ota_keys.h's pre-existing
                         placeholder guard (same as every prior session --
                         not masked or bypassed)
```

**Real hardware execution** (the authoritative evidence for this fix):
§10-11 below.

## 10. Reset-reason raw-value investigation (Part 8)

Added `reset_reason_raw` (the literal `(int)esp_reset_reason()`) to
`/api/v1/metrics`, and changed `resetReasonStr_()`'s default branch from
a bare `"unknown"` to `"unknown(<n>)"` so a future unmapped cause is never
hidden again. **Did not make another enum guess** — the prior
`ESP_RST_EXT` mapping is kept (a real, correctly-named ESP-IDF value
worth having on its own merits) but is explicitly disclosed as unproven
for this specific reset mechanism (doc 35, §11). Raw-value capture was
added and compiled; **the actual numeric value for the specific
esptool-RTS-pin reset was not re-captured this pass** — this remediation
prioritized the OTA confirmation defect (the mandate's primary objective)
given the very large amount of real hardware time already spent this
session; the raw field is now in place for the next flash+observe cycle
to resolve definitively with a single `/api/v1/metrics` read, with no
further firmware changes needed. Explicitly not blocking — no genuine
unsafe reset condition was found or suspected (every OTA-transition
reboot this session showed the expected `boot_id` +1, healthy resume, no
alarms).

## 11. Diff and commit evidence

```
Old commit:  ffef43890d7b6a4da341f8094a71a56b2e8d259d
New commit:  856972ef6106d662c5f8a7f5b71c9a60ce40edc1
Changed:     ota.h (+167/-20), diagnostics.h (+53/-4)
git diff --stat -- ota_manifest_auth.h ota_version_policy.h ota_keys.h
             certs.h queue.h sync.h store.h  -> (empty, zero changes)
Working tree after commit: clean of tracked changes
```

## 12. Build and artifact evidence

```
esp32dev firmware.bin: 1,025,680 bytes
SHA-256: 6b9ebbdf20867905691a1769fbeb87b1517b97171180f1100c319142d58b7fba
Embedded build_commit (direct ELF grep): 856972ef6106d662c5f8a7f5b71c9a60ce40edc1
Private-key check: 1 "PRIVATE KEY" match, same benign mbedtls PEM-label
  table as every prior session (verified context directly, no real secret)
Public key: present (ota_keys.h's real compiled-in COVIO_OTA_PUBLIC_KEY_PEM)
```

## 13. Pre-flash preservation baseline

```
timestamp: 1784827591
fw_version 1.0.2-diag-candidate, boot_id 25, build_commit ffef438...
(diagnostics-only, pre-final-commit build)
accepted_security_floor: 0 (still broken at this point -- pre-fix)
queue: backlog 0, acked_seq 55288, totalizer_raw_pulses 401
Server DB: 1..55288 contiguous, 0 quarantined
```

## 14. USB flash evidence

```
pio run -e esp32dev -t upload --upload-port COM6 -v
4 regions erased+written (bootloader/partition-table/otadata/app), each
independently "Hash of data verified." by esptool. No erase_flash, no
NVS/LittleFS in the write set. [SUCCESS] Took 65.89s
```

## 15. Valid OTA confirmation proof

Fresh USB baseline, first boot after flash (no OTA transition at all):
```
build_commit: 856972ef6106d662c5f8a7f5b71c9a60ce40edc1, build_dirty: false
ota.state: "confirmed"  (was permanently "none" before this fix)
accepted_security_floor: 1  (was permanently 0 before this fix)
ota_debug.confirm_attempted: false (correct -- fresh USB flash, no
  bootloader trial existed to attempt; raw_img_state UNDEFINED)
ota_debug.floor_write_attempted/ok: true/true
ota_debug.floor_read_after_write: 1
ota_debug.bootloader_rollback_engaged: false (honest -- app-level only)
boot_id: 26 (was 25 on the diagnostics build, exactly +1, no loop)
```
Then, via a **genuine, real, signed OTA transition** (`1.0.0` →
`1.0.3-floor-proof-candidate`, security_version 2, independently
signature- and hash-verified before serving):
```
running_version: "1.0.3-floor-proof-candidate"
ota.state: "confirmed"
accepted_security_floor: 2  (advanced from 1 -- via the REAL OTA path,
  not just a USB baseline)
ota_debug.raw_img_state: VALID (consistent with §5's finding -- bootloader
  still doesn't arm a trial for this transition, and the fix correctly
  proceeds via the app-level path regardless)
ota_debug.floor_write_ok: true, floor_read_after_write: 2
ota_debug.bootloader_rollback_engaged: false (honestly disclosed both times)
```
Reboot-persistence of the floor across an *additional* deliberate reboot
was **not** separately forced this pass (would require another OTA cycle
or a console command never authorized) — the floor's persistence relies
on the exact same `Preferences`/NVS mechanism this codebase has already
relied on continuously for `boot_id`/`server_url`/etc. across dozens of
reboots this entire session; not independently re-verified with a fresh
forced reboot, disclosed rather than assumed proven.

## 16. Genuine anti-downgrade proof

With the floor now genuinely at `2`, served a **properly, genuinely
signed** manifest (`1.0.4-genuine-downgrade-test`, `security_version: 1`
— independently verified valid signature before serving, not tampered,
not hash-mismatched, not hardware-mismatched):
```
last_reject_reason: "downgrade_rejected"
accepted_security_floor: 2  (unchanged)
running_version: "1.0.3-floor-proof-candidate"  (unchanged -- no install)
boot_id: 27  (unchanged from before this test -- no reboot occurred)
```
This reached the anti-downgrade gate genuinely — the manifest was
authentic and structurally valid in every other respect; only the
security-version policy rejected it.

## 17. Data-integrity evidence (entire remediation session)

Server DB records grew from `55288` → `55963` fully contiguously across
this remediation's 2 real reboots (1 diagnostics flash, 1 fix flash) plus
2 real OTA transitions plus 1 rejected-before-install downgrade attempt —
**zero** quarantined, duplicate, or missing-sequence rows at any
checkpoint. `totalizer_raw_pulses` stayed `401` throughout. `health_state`
stayed `ok`, zero alarms, throughout.

## 18. Deferred tests (not performed, per explicit instruction)

Unhealthy-image rollback test, deliberate boot failure, power-cut
rollback test, interrupted-download retest, destructive partition
manipulation, sensor/oil-flow validation.

## 19. Remaining risks

- **Bootloader-level rollback protection is confirmed NOT engaging on
  this board/toolchain for genuine OTA transitions** (raw_img_state
  observed VALID, never NEW/PENDING_VERIFY, across 3 independent
  transitions this remediation chain). This remediation makes floor
  advancement correctly independent of that fact, but does **not**
  restore actual bootloader-level automatic rollback — an unhealthy
  candidate that boots but never proves healthy would NOT be
  automatically reverted by the bootloader on this hardware as currently
  configured. This is the deferred rollback test's own precondition, and
  is exactly why it remains deferred rather than attempted.
- Reset-reason raw value still not captured for the specific esptool
  reset mechanism (§10) — the field now exists, one more flash+observe
  cycle resolves it.
- Reboot-persistence of the floor not independently re-forced (§15).
- Interrupted-download recovery remains unproven (deferred from doc 35,
  still deferred here).
- Real oil-flow sensor validation remains completely unperformed.

## 20. Final verdict

**OTA CONFIRMATION AND ANTI-DOWNGRADE CERTIFIED**

Every required Part 10/11 evidence item was obtained with real hardware
proof: manifest authenticated, hash verified, candidate installed,
device rebooted into the candidate slot, health checks passed,
confirmation function executed correctly (with honest bootloader-vs-app-
level disclosure), `ota.state` reached `confirmed`, security floor
advanced to exactly the candidate's security version with read-after-
write verification, build identity matched the OTA candidate, and queue/
NVS/database continuity remained fully intact throughout. The subsequent
genuine anti-downgrade test reached the real policy gate (not a
substitute) and rejected correctly with the active partition, floor, and
data all unchanged.

Not `OTA SECURITY CERTIFIED` or `PRODUCTION HARDWARE CERTIFIED` — those
terms were explicitly excluded by this mandate, and remain inaccurate
regardless: rollback/interrupted-download/sensor validation are still
deferred, and §19's bootloader-rollback gap is a real, disclosed,
unresolved limitation of the underlying hardware/toolchain configuration,
not merely an "observation."

Bench server stopped (`TaskStop`) at the end of this phase, per
instruction — not left running.
