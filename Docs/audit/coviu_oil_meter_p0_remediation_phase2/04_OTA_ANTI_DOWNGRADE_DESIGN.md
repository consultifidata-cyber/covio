# 04 — OTA Anti-Downgrade Design (RISK-15)

## Version model actually implemented

Four identity fields, per manifest candidate:
- **Semantic firmware version** (`version`, string) — pre-existing, used
  only to decide "is this a different image at all" (`ota.h::poll()`,
  unchanged).
- **Monotonic security version** (`security_version`, integer) — NEW.
  `FW_SECURITY_VERSION` (`config.h`) is the compiled-in value for the
  running image; `Store::securityVersion()` (`store.h`) is the durable,
  NVS-backed **floor**: the highest `FW_SECURITY_VERSION` any image has
  ever been CONFIRMED healthy on this device.
- **Hardware compatibility identifier** (`hw_compat`, string) — NEW,
  compared against `DEVICE_MODEL` (`config.h`).
- **Partition/config schema version** (`schema_version`, integer) — NEW,
  compared against `SCHEMA_VERSION_CURRENT` (`queue.h`, pre-existing
  constant, reused rather than duplicated).

**Build hash** is intentionally NOT part of the accept/reject decision this
phase — see "What remains absent" below.

A version-STRING comparison alone (the pre-existing `ver == FW_VERSION`
check) is insufficient and remains exactly that limited; the new
`security_version` integer is the actual gate.

## Required behavior — implemented vs. deferred

| Requirement | Status |
|---|---|
| Reject lower security_version than accepted floor | **Implemented** — `evaluateOtaCandidate()`, `OTA_REJECT_DOWNGRADE` |
| Reject incompatible hardware revision | **Implemented** — `OTA_REJECT_HW_MISMATCH` |
| Reject unsupported partition/config schema | **Implemented** — `OTA_REJECT_SCHEMA_MISMATCH` |
| Reject unsigned/invalidly-signed manifest | **NOT IMPLEMENTED** — no manifest signing exists in this codebase at all (unchanged from the original static certification); building real asymmetric-crypto manifest signing is a substantially larger undertaking than RISK-15 (anti-downgrade specifically) requires, and is tracked as a separate, still-open gap, not silently folded into "done" |
| Reject image whose hash doesn't match | **NOT IMPLEMENTED** — same reasoning; TLS + CA-pinning (pre-existing, ADR-005) remains the only transport-integrity guarantee |
| Reject replayed OTA command where replay protection applies | **N/A to this architecture** — OTA here is device-POLLED (pull-based, `ota.h::poll()`), not command-pushed; there is no "OTA command" a server sends that could be replayed in the mandate's sense. A stale/replayed MANIFEST is handled: re-polling the same manifest repeatedly is idempotent by construction (`evaluateOtaCandidate()` is pure — same inputs, same verdict, every time) |
| Reject image outside permitted rollout channel | **N/A** — no rollout-channel concept exists in this single-manifest architecture (tracked as RISK-14 in the risk register, unrelated to this fix) |
| Break-glass downgrade process | **Deliberately NOT implemented** — the mandate permits one only under strict elevated-authorization/audit conditions and explicitly forbids "an ordinary configuration switch that disables downgrade protection." Building a genuinely secure break-glass mechanism (elevated auth + recorded reason + exact target version + device identity + time-limited approval + immutable audit event) is real, separate engineering scope beyond what RISK-15 requires to close. **Consequence, stated plainly:** there is currently NO way to legitimately lower a device's accepted security floor except a physical NVS-partition erase (which requires physical access and `esptool`/erase, i.e., the exact hardware boundary this session cannot cross) |

## State persistence

`Store::securityVersion()`/`setSecurityVersion()` live in a **separate NVS
namespace** (`NVS_NS_SECURITY = "covio_sec"`, `config.h`), opened
independently in `Store::begin()` and deliberately never touched by
`Store::factoryReset()` (which only clears `NVS_NS = "covio"`). This is the
single most important design decision in this fix:

> **A downgrade-after-factory-reset is exactly the attack this floor exists
> to prevent.** If the floor lived in the same namespace as everything
> else, an ordinary consumer-facing factory reset (or a stolen device being
> "wiped" before resale/reprovisioning) would silently erase the anti-
> downgrade protection along with the WiFi/API credentials it's meant to
> also help protect.

Confirmed to survive (by code construction, not yet hardware-tested — see
doc 05):
- **Reboot** — `Store::begin()` re-opens the same NVS namespace every boot; NVS itself is durable flash storage.
- **OTA rollback** — the floor only ever advances in `confirmHealthyBoot()`; a rolled-back candidate never reached that call, so the floor is exactly where it was before the attempt.
- **Server retry / Device Manager restart** — neither has any code path that touches this NVS namespace at all.
- **Configuration reset** (`factoryReset()`) — explicitly, deliberately does NOT reset it (see above).

**Documented limitation, not hidden:** a physical, offline NVS-partition
erase (e.g., `esptool.py erase_flash` or erasing just the `nvs`/
`covio_sec`-adjacent region) WOULD reset this floor to 0, since it is a
software-only gate. The mandate explicitly said not to burn eFuses for
this pilot stage, so a true hardware-enforced anti-rollback (ESP32
secure-version eFuses) was considered and rejected as inappropriate for
current scope — this tradeoff is the direct, accepted consequence, not an
oversight.

## Why the floor advances only in `confirmHealthyBoot()`

The floor must never rise on a candidate that never proves itself healthy
— otherwise a single bad update (that never boots successfully, or boots
but never reaches network/health confirmation) would permanently strand
the device unable to receive a LEGITIMATE later downgrade-shaped fix
(e.g., if a security-patched build accidentally shipped with a higher
`FW_SECURITY_VERSION` than intended and needed a corrective re-release at
the SAME security_version). Gating the floor-advance on the exact same
health proof that already gates `esp_ota_mark_app_valid_cancel_rollback()`
(WiFi connected AND at least one successful server ack/config fetch) means
the floor and the rollback-cancellation decision are always in lockstep —
they can never disagree about whether "this image is good."
