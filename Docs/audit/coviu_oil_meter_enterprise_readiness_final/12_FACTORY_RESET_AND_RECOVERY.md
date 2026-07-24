# 12 — Factory Reset and Recovery (Part 14)

**Not performed on the real, data-bearing device this session** —
answered entirely via direct code inspection (`provision.h`, `store.h`),
per the mandate's own instruction to use code inspection or a disposable
environment rather than the live unit.

## Trace

```
provision.h:
  else if (line == "factory") {
    Serial.println("[PROV] factory reset: wiping NVS, rebooting...");
    st_->factoryReset(); delay(200); ESP.restart();
  }

store.h:
  void factoryReset() { p_.clear(); }   // clears ONLY the "covio" namespace
```

## Answers

1. **Trigger**: the serial console command `factory` (typed over USB,
   no other physical action triggers it — no button/pin-based reset
   exists in this firmware).
2. **Authentication required?** **None** — same unauthenticated serial
   console as every other command (doc 10).
3. **Confirmation required?** **None** — a single line, `factory`,
   executes immediately with no "are you sure" prompt.
4. **Partitions erased?** **None, at the partition level** —
   `factoryReset()` calls `Preferences::clear()`, which clears keys
   within the `covio` NVS **namespace** only, not the NVS partition as a
   whole, and does not touch LittleFS/app/otadata partitions at all.
5. **Wi-Fi credentials erased?** **Yes** — `wifi_ssid`/`wifi_pass` live
   in the `covio` namespace.
6. **API token erased?** **Yes** — `api_key` is in the same namespace.
7. **Device ID erased?** **No** — `device_id` is not NVS-derived at all
   (it's `ESP.getEfuseMac()`-derived, permanent, immune to any reset).
8. **Plant ID erased?** N/A — doesn't exist.
9. **Calibration erased?** **Yes** — `kfactor`/`density`/`tref` cache
   values are in the `covio` namespace (note: this is only the
   device's local DISPLAY-only cache; the authoritative K-factor lives
   server-side and is unaffected).
10. **Accepted security floor erased?** **No — by explicit design** (the
    `covio_sec` namespace is a SEPARATE `Preferences` instance,
    deliberately never touched by `factoryReset()`, exactly per
    `store.h`'s own comment: "a downgrade-after-reset is exactly the
    attack this floor exists to prevent"). Confirmed by direct code
    read, consistent with this whole chain's behavior.
11. **Queue data erased?** **No** — LittleFS/`/queue/*` is untouched by
    `factoryReset()` (which only touches NVS `Preferences`).
12. **Totalizer erased?** **No** — same reasoning; totalizer checkpoint
    lives on LittleFS/flash, not the `covio` NVS namespace (per
    `config.h`'s own architecture description).
13. **Audit records erased?** N/A on-device (no device-side audit log
    exists); server-side `device_events` are obviously unaffected by a
    device-side reset.
14. **Can config be restored remotely after a factory reset?** **No** —
    a factory-reset device reverts to `config.h`'s compiled-in first-boot
    defaults (`DEFAULT_SERVER_URL`, placeholder Wi-Fi), which are
    non-functional placeholders (confirmed, doc 07) — it cannot
    reconnect to a real network on its own; physical/AP-mode
    reprovisioning is required.
15. **Can a reset device reconnect automatically?** **No** — see above;
    the placeholder Wi-Fi credentials cannot connect to any real
    network.
16. **Can factory reset cause duplicate sequence reuse?** **This is the
    most safety-relevant finding in this section.** `boot_id` (NVS,
    `covio` namespace) IS cleared by `factoryReset()` — it would
    restart from 0 on the next boot. However, the GLOBAL telemetry
    `seq` counter is seeded from `totalizer.lastSeq()` (LittleFS-backed,
    NOT NVS, NOT cleared by `factoryReset()`) — so **`seq` itself
    survives a factory reset and continues monotonically**, meaning
    duplicate `(device_id, seq)` reuse from THIS specific reset path is
    **NOT observed as a risk by code inspection** — `boot_id` resetting
    to 0 is cosmetic (used only for diagnostic/disambiguation purposes
    per `queue.h`'s own comment: "boot_id is stored for diagnostics
    only"), not the actual server-side uniqueness key (`seq` is).
    **This is a positive finding, not a gap** — but it was reached by
    code inspection only, not by actually performing a factory reset and
    checking, so it is reported as "code-proven, not hardware-executed."

## Is factory-reset behavior safe for industrial use?

**Partially.** The security-floor protection (item 10) is a genuinely
good, deliberate safety property. But the complete absence of
authentication or confirmation (items 2-3) on a command reachable by
anyone with a USB cable — combined with the fact that it silently wipes
Wi-Fi/API credentials and the local calibration cache with zero prompt
— is a real operational risk: an accidental or malicious `factory`
keystroke during a maintenance session would require full physical
reprovisioning to recover, with no remote path back. **Classified as a
P2 operational-safety gap** (not P0/P1 — it does not lose oil-flow data
or compromise the security floor), in `18_GAPS_AND_REMEDIATION_PLAN.md`.
