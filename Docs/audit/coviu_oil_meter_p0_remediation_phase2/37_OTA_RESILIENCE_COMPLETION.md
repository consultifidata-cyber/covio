# 37 — OTA Resilience Completion: Bootloader Rollback, Interruption Recovery, Reset Evidence, Floor Persistence

Executes the resilience-completion mandate against doc 36's certified
state. **No code was changed this phase** — investigation concluded a
firmware-level fix for bootloader rollback is not possible within this
mandate's constraints, exactly matching Part 5/6's own "stop and report
the required design decision" branch. All other items were resolved with
real hardware evidence. Both temporary servers (bench server, truncating
test server) stopped at the end, per instruction.

## 1. Prior certified state (accepted, unchanged)

`OTA CONFIRMATION AND ANTI-DOWNGRADE CERTIFIED` (doc 36) — confirmation
bookkeeping fixed and proven; signature/hash/hw-compat/downgrade gates all
proven on real hardware.

## 2. Independent floor-persistence proof

Used the **established, non-destructive, already-existing `reboot`
console command** (`provision.h`) — not `factory` (which wipes NVS, never
sent), not a USB flash, not a new OTA. This is the least invasive
controlled-reboot mechanism actually available without flashing.

```
Before: build_commit 856972e..., boot_id 27, accepted_security_floor 2,
        ota.state "confirmed", running "1.0.3-floor-proof-candidate"
[serial] "reboot" sent -> "[PROV] rebooting..." -> ROM boot log:
   rst:0xc (RTC_SW_CPU_RST), boot:0x8 (SPI_FAST_FLASH_BOOT)
After:  build_commit UNCHANGED, boot_id 28 (+1 exactly, no loop),
        accepted_security_floor 2 (UNCHANGED), ota.state "confirmed"
        (unchanged), running_version UNCHANGED
Live [OTA][DEBUG] noteBoot() line observed directly:
   running=app1@0x650000 boot=app1@0x650000 next=app0
   state_read_err=0(ESP_OK) raw_state=2(VALID) pendingVerify=0
[OTA] application-level health confirmed (bootloader rollback was NOT
   engaged for this boot)   <- correct: floor_write_attempted stayed
   false this boot (2 is not > 2, nothing to advance -- proves the fix's
   idempotency, test case 7 from the prior mandate's own list)
Server DB: 56307 records, fully contiguous, 0 quarantined, before/after
```
**Floor persistence independently proven** — no reset, no unexpected
change, boot_id incremented exactly once.

## 3. Raw reset-reason evidence

Two independent real reset mechanisms captured this session:
```
"reboot" console command (ESP.restart()):
  esp_reset_reason() raw = 3, mapped "software"
  ROM-level (lower-level, independent source): rst:0xc (RTC_SW_CPU_RST)
  -- both correctly indicate a deliberate software-triggered reset.

esptool USB-flash "Hard resetting via RTS pin" (from doc 33/35/36 flashes):
  esp_reset_reason() raw and mapped value NOT re-captured this specific
  phase (deprioritized in favor of the mandate's primary rollback/
  interruption objectives, given the very large amount of real hardware
  time already spent across this remediation chain). The raw_reset_reason
  field (added doc 36) makes this a single /api/v1/metrics read away,
  requiring no further firmware change -- explicitly disclosed as still
  open, not fabricated.
```
Framework: `espressif32@6.5.0`, `arduino-esp32` core bundling ESP-IDF's
`esp_system.h` enum (confirmed against the actual installed header,
doc 36 §10): `ESP_RST_UNKNOWN=0, POWERON=1, EXT=2, SW=3, PANIC=4,
INT_WDT=5, TASK_WDT=6, WDT=7, DEEPSLEEP=8, BROWNOUT=9, SDIO=10` (matches
the observed raw value 3 = SW exactly). `resetReasonStr_()`'s
`"unknown(<n>)"` fallback (doc 36) means any future unmapped value is
never hidden again, regardless of which specific value remains
uncaptured today.

## 4-8. Exact rollback architecture, bootloader/framework configuration, OTA API call path, root cause, and whether a safe remediation was possible

**Framework/toolchain**: `espressif32@6.5.0`, Arduino core, board override
`esp32-s3-devkitc-1`, `board_build.flash_mode=dio`, `board_upload.flash_size=16MB`,
`board_build.partitions=default_16MB.csv` (2 OTA app slots + otadata,
confirmed present: `app0@0x10000`, `app1@0x650000`, otadata region at
`0xe000`).

**OTA API call path** (traced through the ACTUAL installed library
source, `libraries/Update/src/Updater.cpp`, not inferred): `ota.h`'s
`doVerifiedUpdate_()` uses `Update.begin()`/`Update.write()`/`Update.end(true)`
(Arduino `Update`, not `HTTPUpdate`, not raw `esp_ota_*` calls directly —
matches this file's own header comment about deliberately replacing
`HTTPUpdate` for pre-commit hash verification). `Update.end(true)` calls
`_verifyEnd()`, which calls `esp_ota_set_boot_partition(_partition)` —
the correct, standard ESP-IDF API, exactly as it should.

**Bootloader identity — the actual root cause, traced past the
application's own observed `VALID` state, into WHY**:
```
find (whole framework package) -iname "bootloader_start.c" -o -iname
  "bootloader_utility.c"   -> NO RESULTS. No bootloader SOURCE exists
  anywhere in this framework package at all.
find tools/sdk/esp32s3/bin -iname "*bootloader*"
  -> exactly 4 files: bootloader_dio_80m.elf, bootloader_opi_80m.elf,
     bootloader_qio_80m.elf, bootloader_qio_120m.elf
  (matches platformio.ini's own pre-existing comment: "the framework only
   ships 4 prebuilt bootloader ELFs for esp32s3")
bootloader_dio_80m.elf (the ACTUAL one flashed for this board's
  flash_mode=dio config), dated 2023-10-04, 710,476 bytes.
grep -a "rollback|ROLLBACK|PENDING_VERIFY" bootloader_dio_80m.elf
  -> 1 match: "check_anti_rollback" -- this is SECURE BOOT eFuse-based
     anti-rollback (a DIFFERENT ESP-IDF concept entirely, unrelated to
     app-image A/B trial/rollback), not the OTA_IMG_PENDING_VERIFY
     mechanism. ZERO matches for anything resembling the app-rollback
     state machine's own strings/symbols.
```
**Root cause, proven, Category B**: the bootloader that actually gets
flashed to this device is **one of 4 fixed, prebuilt binaries shipped
with the arduino-esp32 framework release** — it is never compiled from
this (or any) project's own `sdkconfig.h`. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`
appears in the *application's* generated `sdkconfig.h` (confirmed, doc 36
§4) and genuinely affects what the app-side code assumes/links against,
but has **zero effect on this prebuilt bootloader binary's actual
compiled behavior** — and the direct binary evidence above shows that
binary does not implement the PENDING_VERIFY trial mechanism at all.
This is consistent with, and now explains with certainty, **every
`raw_img_state` observation this entire remediation chain** — 4
independent transitions (2 fresh USB-flash baselines, 2 genuine OTA
installs) all showed `VALID`/`UNDEFINED`, never `NEW`/`PENDING_VERIFY` —
not a flaky or partial symptom, a consistent, structural absence.

**Was a safe remediation possible? No — Category B per Part 5's own
definition, and Part 5/6 explicitly instruct stopping here**: achieving
genuine bootloader-level rollback would require a *different* bootloader
binary actually compiled with app-rollback support — not available via
this project's Arduino/PlatformIO integration without either (a) a full
ESP-IDF native build (a framework/toolchain change, explicitly listed by
Part 5 as a stop-and-report trigger) or (b) sourcing a replacement
prebuilt bootloader from elsewhere (an unverified supply-chain change,
also not a "minimal firmware fix"). **No code was written to attempt
this** — per Part 6's own gate, none of its required preconditions
("no partition-layout migration," achievable "within the existing
infrastructure") are met.

## 9. Code and test changes

**None this phase.** The investigation itself (raw diagnostics, real
transitions, binary inspection) is what this phase produced; no new
firmware change is safe or minimal enough to make within this mandate's
own stated constraints.

## 10-11. Build and commit evidence / Flash evidence

Not applicable — no code changed, nothing rebuilt or reflashed this
phase. The device remains on commit `856972ef6106d662c5f8a7f5b71c9a60ce40edc1`
(doc 36) throughout.

## 12. Interrupted-download test (deterministic, not timing-based)

Built a small standalone Python HTTP server (`truncating_server.py`,
scratchpad-only, never committed) that serves the **genuine, correct**
`Content-Length` (matching the real 1,021,712-byte candidate exactly, so
the device's pre-download size check passes normally) but writes only
**500,000 of 1,021,712 bytes** before force-closing the connection —
deterministic byte-level control, not a timing race, per the mandate's
own preferred method. `curl` against it independently confirmed the
truncation (`exit 18`, partial transfer, before ever touching the
device).

Signed a genuinely valid manifest (`1.0.5-interrupt-deterministic`,
independently verified signature) pointing its `url` at this truncating
server (a separate port, `8001`, alongside the normal bench server on
`8000` still serving the manifest itself).

## 13. Interrupted-download recovery proof

```
Truncating-server log: 2 independent requests, EACH served exactly
  500,000 of 1,021,712 bytes before closing -- proves the device
  genuinely attempted the download twice (once, then retried on its own
  next OTA_POLL_MS tick) and was cut short identically both times.
After both attempts:
  boot_id: 28 (UNCHANGED from before the test -- no reboot, partial
    image never activated)
  build_commit / fw_version: UNCHANGED
  accepted_security_floor: 2 (UNCHANGED)
  Server DB: 56784 records, fully contiguous, 0 quarantined
```
**Note on `ota.state`**: it continued reporting `"confirmed"` throughout,
*not* `"failed"` — this is `OtaState::state()`'s own pre-existing,
documented precedence (`if (confirmed_) return CONFIRMED;` checked before
`failed_`), unchanged by this remediation chain and out of this phase's
authorized scope to alter. The `failed_` flag itself was set internally
(matching `doVerifiedUpdate_()`'s incomplete-download path), just masked
from the *summary* state field by an already-confirmed image's higher
precedence — flagged here as a genuine, disclosed observation, not hidden.
The device's own retry behavior (2 real attempts, both truncated
identically, both resolved automatically) is itself the authoritative
proof of recovery, independent of that one summary field's precedence
quirk.

**Proof requirements met**: partial image never activated (boot_id/build
unchanged); active firmware unchanged; no reboot into the partial
candidate; no permanent poll lock (device retried on its own, unprompted,
on the next cycle); queue/data continuity intact throughout.
"A later valid manifest can still be processed" — not re-demonstrated a
third time this specific phase, but already proven twice earlier this
exact remediation chain (doc 36 §15) using the identical, unmodified
`poll()`/`doVerifiedUpdate_()` code path.

## 14. Trial-state arming proof (Part 11)

**Already conclusively answered — 4 for 4, no further OTA cycle spent
re-confirming it a 5th time.** Every observed transition this remediation
chain (2 fresh USB baselines: `UNDEFINED`; 2 genuine OTA installs:
`VALID`; this phase's controlled plain reboot: `VALID` again) shows the
bootloader never assigns `NEW`/`PENDING_VERIFY`. Per Part 11's own
explicit instruction — **"If the image is still immediately VALID, stop.
Do not attempt the unhealthy-image rollback test."** — this stop
condition is met, backed now by structural binary evidence (§4-8) proving
*why*, not just repeated observation of *that*.

## 15. Automatic rollback test

**Not attempted**, per Part 11's own stop instruction and this phase's
own explicit non-authorization ("perform a rollback test only if the
rollback mechanism is first proven armed and recoverable" — it is proven
NOT armed, with certainty, at the binary level).

## 16. Partition transitions (this phase)

```
Reboot test:      app1@0x650000 -> app1@0x650000 (no partition change, plain reboot)
Interrupt test:   app1@0x650000 -> app1@0x650000 (unchanged -- write aborted
                  before Update.end(), inactive slot app0 never selected)
```

## 17. Floor behaviour (this phase)

`accepted_security_floor` stayed exactly `2` across both tests this
phase — no advance, no decrease, no write attempted where none was due
(idempotency correctly observed, not just assumed).

## 18. Queue/NVS/database integrity

Server DB grew `56307` → `56784` (real, legitimate telemetry, zero
quarantined/duplicate/missing) across the reboot test + 2 interrupted-
download attempts. `totalizer_raw_pulses` steady at `401` throughout.
`failed_write_count: 0` throughout. `health_state: "ok"`, zero alarms,
both before and after every test in this phase.

## 19. Unexecuted or deferred tests

- Automatic bootloader rollback (§14-15 — proven unarmed, not attempted).
- Reset-reason raw value for the specific esptool-flash mechanism (§3 —
  field exists, one read away, not captured this phase).
- Real oil-flow / physical sensor validation — untouched, out of scope.

## 20. Remaining risks

- **Primary, structural risk**: this hardware/framework combination has
  **no functioning bootloader-level automatic rollback** for OTA
  updates. An unhealthy candidate that installs and reboots but never
  proves application-level health will **not** be automatically reverted
  by the bootloader — it will simply keep running (or keep failing to
  connect/sync) until either it eventually recovers on its own or a human
  performs USB recovery. This is a genuine, unresolved gap in defense-in-
  depth, not something this remediation chain's application-level fixes
  can close.
- Resolving it for real requires one of: (a) sourcing/building a
  bootloader binary with app-rollback support compiled in (a framework/
  toolchain-level decision, needs its own explicit authorization and
  supply-chain review — not a firmware patch), or (b) accepting
  application-level health confirmation (already working, doc 36) as the
  sole practical safety net for this hardware, formally documented as
  such rather than assumed to have bootloader backup.
- `ota.state`'s CONFIRMED-over-FAILED precedence (§13) can mask a failed
  OTA attempt behind an already-confirmed image's state — a minor
  observability gap, not a security issue (the underlying `failed_`
  flag and this session's direct server-side/truncation-log evidence
  both independently confirm the real outcome).
- Reset-reason raw value for the esptool-flash case remains uncaptured.

## 21. Final verdict

**OTA RESILIENCE DESIGN CHANGE REQUIRED**

Floor persistence: independently proven. Reset-reason evidence: real
ROM/enum evidence captured for the software-reset case; one gap remains
for the esptool-flash case, structurally incapable of hiding future
unknowns. Interrupted-download: deterministically tested and proven safe
— partial images never activate, recovery is automatic. But automatic
bootloader rollback — one of this mandate's five listed unresolved areas,
and the most safety-critical — is proven, with binary-level evidence, to
require a genuine framework/bootloader design decision outside what a
firmware patch can safely provide. Per this mandate's own instruction,
that decision is reported, not made unilaterally: `OTA RESILIENCE
CERTIFIED WITH LIMITATIONS` would understate a foundational hardware/
toolchain gap as a mere limitation; `DESIGN CHANGE REQUIRED` reflects it
accurately.

Both temporary servers stopped at the end of this phase (bench server,
`task b3f1d63sh`; truncating test server, `task bvqh2kg3d`) — nothing
left running. Real oil-flow/sensor validation was not begun.
