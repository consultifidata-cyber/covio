# 04 — Production API Key Verification Result

**Freeze-list item 3.** Question: does Balaji's device use the intended
production API key, or is it still running the shared development
bootstrap key (`dev-key-change-me`, `config.h::DEFAULT_API_KEY`)? The
Product Readiness Review flagged this as an unresolved conflict between
older audit docs (which described the key as still the shared dev
bootstrap key) and the physical commissioning session (which showed
`api_key_status: "configured"` with a fingerprint matching "the registered
production key").

## Fresh evidence (this session, 2026-07-26, read-only serial console)

A passive `show` command was sent to the already-running device over COM6
— **no reset, no power cycle, no write of any kind**:

```
device_id : esp32-F4E5B2858428
fw        : 1.0.0
boot_id   : 54
server_url: https://data.funtastik.co.in
api_key   : configured (fingerprint=18519bce8d2a)
wifi_ssid : Amar's A34
calib     : v1  K=1000.0000  density=0.840  Tref=15.0
```

`boot_id: 54` is unchanged from the physical commissioning session's final
capture — the device has been continuously running since then, not
rebooted or reprovisioned in between.

## How "configured" vs "default" is decided (traced in code)

`provision.h::apiKeyDisplay_()` classifies the key via
`credential_display.h::classifyCredential(key, DEFAULT_API_KEY)`:

- `CRED_STATUS_MISSING` if the key is empty.
- `CRED_STATUS_DEFAULT` **only if the key is byte-for-byte equal to
  `config.h::DEFAULT_API_KEY` ("dev-key-change-me")**.
- `CRED_STATUS_CONFIGURED` otherwise.

The device reports `configured`, which is a factual, code-traceable
statement that **the key currently in NVS is not the literal string
`"dev-key-change-me"`**. This alone rules out the specific "still on the
shared bootstrap default" scenario the older audit docs worried about.

## Why this is a real, unique production key, not just "some non-default value"

Independent of the device's own self-report, the server side
(`server/server.py`) enforces per-device key uniqueness structurally, not
just by convention:

- `devices.api_key_hash` is declared `TEXT UNIQUE` (`server.py:484`) — two
  devices cannot share a stored key hash.
- Keys are generated via `generate_api_key()` (`server.py:667-673`):
  `secrets.token_hex(24)` (192 bits of randomness), never a fixed/shared
  string.
- The one deliberate exception is `legacy-default-key`
  (`server.py:403-409`): a single pseudo-device row seeded specifically to
  catch any device still presenting the shared bootstrap key at
  `require_api_key()` — such a device would authenticate as
  `device_id = "legacy-default-key"`, **not** as its own identity.
- Every push in this project's live commissioning sessions (documented in
  `Docs/audit/coviu_oil_meter_plant_commissioning/`) authenticated and
  advanced the ack sequence under the device's own real identity
  (`esp32-F4E5B2858428`), not under `legacy-default-key`. A device
  presenting the shared bootstrap key would have been indistinguishable
  from every other never-provisioned device in the fleet at the server —
  it was not.

Combined, this is conclusive: the key configured on Balaji's device is not
the shared default, not empty, and has been successfully authenticating as
its own distinct device identity, not the legacy shared-key fallback
identity.

## Conclusion

**No mismatch found. No rotation performed**, per the freeze-list's own
instruction ("rotate only if a mismatch or security issue is proven") and
"preserve existing commissioning" — rotating a working, already-correct
production key would itself be the kind of unnecessary field action this
freeze is designed to avoid.

## Tooling delivered for future verification/rotation

`scripts/verify_api_key_fingerprint.py` — computes the exact same
SHA-256-derived 6-byte fingerprint `provision.h::apiKeyDisplay_()` prints
(verified byte-for-byte in `test/native/test_verify_api_key_fingerprint.py`
against an independent `hashlib` computation), from a key read via stdin or
a hidden interactive prompt — **never** as a command-line argument, and
**never** printed back. Future use: whenever the ERP issues or rotates a
key (its `/admin/devices/<id>/rotate-key` response), an operator can
confirm — without ever pasting the raw key anywhere logged or persisted —
that the value the ERP just issued matches what the device's serial
console reports, closing the loop between "the ERP thinks it assigned key
X" and "the device is actually configured with key X" without ever
exposing X in the process.

## Rules compliance

The raw API key was never read, requested, displayed, or transmitted in
this verification — only its status classification and 6-byte fingerprint,
both already-established non-secret outputs of the existing, unmodified
serial console.
