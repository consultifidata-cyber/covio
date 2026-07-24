# 03 — Serial Credential Redaction (Part 2)

## Before (the confirmed finding from the enterprise re-audit)

`provision.h`'s `show` command printed `st_->apiKey().c_str()` — the raw
API key — in plaintext, to anyone typing `show` over the USB serial
console, with **no authentication at all**.

## Fix implemented

`provision.h`'s `show` command now calls a new `apiKeyDisplay_()` helper
instead of printing the raw key. That helper:
1. Classifies the key via `credential_display.h::classifyCredential()` —
   a new, dependency-free, host-testable pure module — into exactly one
   of `missing` / `default` / `configured` (same three-way convention
   `diagnostics.h::apiKeyStatus_()` already uses elsewhere in this
   codebase, not a new taxonomy).
2. Computes a short (6-byte / 12-hex-char), **irreversible** SHA-256-
   derived fingerprint of the key, via `mbedtls_sha256` (already linked
   into every build, same library `ota.h` already uses for image-hash
   verification — no new dependency).
3. Prints only `"<status> (fingerprint=<12 hex chars>)"` — never the raw
   value.

`wifi_pass` was never printed by `show` in the first place (only
`wifi_ssid`, which this codebase does not treat as a secret anywhere
else either) — confirmed unchanged, not touched by this fix.

## Tests added (native, host-testable pure logic)

`test/native_cpp/test_credential_display.cpp` — 6 cases against the real
`classifyCredential()`: empty/null key → `missing`; exact match to the
compiled default → `default`; any other value → `configured`; a
near-but-not-exact match to the default string is correctly NOT
classified as `default` (exact match only); status strings match the
existing `diagnostics.h` convention; a missing/null default-key
reference degrades safely to `configured` rather than crashing or
guessing.

**Honest disclosure, same as every prior native_cpp test this chain**:
this machine has no host C++ compiler — written and statically
reviewed, compile-verified only via the full firmware's successful
`pio run` (which includes this header). The fingerprint's own SHA-256
computation stays a thin `mbedtls` call in `provision.h` itself,
deliberately not duplicated into the host-testable module — proven
instead by the live serial test below, which is stronger evidence for
that specific piece than a host unit test could be anyway.

## Live proof (real hardware, this phase)

After flashing the fix (doc 02), sent `show` over the actual serial
console and captured the real response:

```
device_id : esp32-F4E5B2858428
fw        : 1.0.0
boot_id   : 30
server_url: http://192.168.1.3:8000
api_key   : default (fingerprint=57c8139db729)
wifi_ssid : Airtel_amar_3999
calib     : v1  K=1000.0000  density=0.840  Tref=15.0
```

**The raw key (`dev-key-change-me`) does not appear anywhere in this
output.** Only the status (`default`, correctly reflecting this
device's actual current, unrotated bootstrap key) and an irreversible
fingerprint are shown.

## Regression scan (repeated this phase)

```
grep -a -c "PRIVATE KEY" .pio/build/esp32dev/firmware.elf  -> 1 (same
  benign mbedtls PEM-label-table match verified every prior session,
  not a real secret)
grep -n "apiKey()" provision.h -> exactly 1 occurrence, inside
  apiKeyDisplay_() itself (used only to compute status+fingerprint,
  never printed raw) -- confirms the fix's scope precisely
```

## Checklist verdict

`SERIAL SECRET EXPOSURE: CLOSED`
