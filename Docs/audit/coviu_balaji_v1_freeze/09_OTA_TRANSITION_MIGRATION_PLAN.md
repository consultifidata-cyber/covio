# 09 — OTA Transition Migration Plan (Blocker 3 Closure)

Closes independent certification blocker 3: the freeze remediation's
corrected OTA signing key exists only in source, and the currently-deployed
device cannot accept anything signed with it as-is. This document states
the exact migration and verifies, cryptographically, that it will work.

## The exact problem, restated precisely

- The device currently running at Balaji (`boot_id: 54`, firmware
  `e5a593b`, reconfirmed live this session) has the **old placeholder test
  key** compiled in as its one and only trusted OTA key
  (`COVIO_OTA_KEY_ID = "covio-test-key-2026-07"`).
- This session's ceremony replaced that placeholder in **source** with a
  real production key (`COVIO_OTA_KEY_ID = "covio-prod-key-2026-07"`).
  `ota.h` trusts exactly one compiled-in key at a time (no trust list, a
  documented, unchanged limitation) — a manifest signed with the new
  production key would be rejected outright by the device as it exists
  today, since it doesn't recognize that key id at all.
- Therefore: to get the freeze-remediated firmware (which itself embeds the
  new key) onto the device, the *delivery* of that one firmware image
  cannot be authenticated with the new key — only the old one, which the
  device still trusts, or a non-OTA path.

## Two viable migration paths

**Path A — One-time USB reflash (recommended).**
Flash the new firmware directly via `esptool`/PlatformIO
(`pio run -e release -t upload`, after committing the freeze changes —
`RELEASE_BUILD`'s existing dirty-tree gate requires this regardless of
path). OTA's signature verification is bypassed entirely for a direct
flash (it only applies to the OTA download/verify/apply pathway in
`ota.h`) — this is the same mechanism already used for every firmware
flash in this project's history, including the currently-running image.
**No dependency on any signing key surviving anywhere.** Requires one
physical visit, which is an already-accepted operational model for this
single device (see the canonical architecture document's Balaji-specific
scoping).

**Path B — One-time legacy-signed OTA transition.**
Sign a manifest for the new firmware image using the **old test private
key** (still present, confirmed below) with `--key-id
covio-test-key-2026-07` — the device, still running the old firmware,
verifies it correctly (it's the key it already trusts), downloads,
installs, and reboots into the new image, which itself embeds the new
production key going forward. No physical visit required.

**Recommendation: Path A (USB reflash).** Not because Path B is
cryptographically unsound — it verified correctly in testing below — but
because this project has otherwise carefully maintained a strict
separation between the test key (generated for "automated and isolated
hardware testing," per `ota_keys.h`'s own header comment, held with no
vault/handling discipline beyond a gitignored local file) and the
production key (generated under a documented ceremony, intended for a
proper secrets vault). Using the test key to authorize a real,
customer-facing firmware delivery — even once, even for a transition —
blurs that separation for no operational necessity here, since physical
access to this one device is already an accepted, unremarkable action.
Path B remains documented as a legitimate fallback if a physical visit is
genuinely not available before the next required update.

## Verification performed this session (both paths)

**Confirmed the production keypair is real, complete, and matches
`ota_keys.h`** — without ever printing the private key:

```
production key file public key == ota_keys.h embedded key: True
Signature verification against ota_keys.h public key: PASS
Confirmed: old test key correctly REJECTS a production-key-signed manifest (as expected)
```

Method: loaded `server/tools/.production_signing_key.pem` (still present
locally — see "what remains" below), derived its public key, confirmed it
is byte-for-byte identical to the public key embedded in `ota_keys.h`,
signed a synthetic manifest canonical string with the production private
key, and verified that signature against **only** the embedded public key
— the exact check `ota.h::verifyManifestAuthenticity_()`'s
`mbedtls_pk_verify()` call performs on-device. This is a real, independent
cryptographic proof, not a restatement of the ceremony record.

**Negative control, confirming the migration is actually necessary (not
just theoretically neat):** the same signature was checked against the
**old test key's** public key and correctly failed to verify — proving
that the currently-deployed device, which only trusts the old key, would
genuinely reject a manifest signed with the new one, exactly as this
document's problem statement claims. This rules out the possibility that
the "blocker" was overstated.

**Confirmed both private key files are still present on this machine:**
`server/tools/.test_signing_key.pem` (dated 2026-07-23, the original
placeholder) and `server/tools/.production_signing_key.pem` (dated
2026-07-26, this freeze's ceremony) both exist. This means **either
migration path (A or B) is available right now** — nothing has been lost
or moved out of reach since the ceremony.

## Will future OTA updates work after the migration?

**Yes, verified.** Once the device is running firmware built from the
current source (either path), it has the new public key compiled in and
`COVIO_OTA_KEY_ID = "covio-prod-key-2026-07"`. The verification above
already proves the mechanics: a manifest signed with the production
private key and `--key-id covio-prod-key-2026-07` verifies correctly
against exactly the public key that firmware embeds. Every subsequent
release, signed the normal way per
`03_OTA_SIGNING_KEY_CEREMONY.md`'s existing instructions, will be accepted
by the device with no further transition needed. This is a one-time
migration, not a recurring procedure.

## What remains (field action)

1. **Choose and execute Path A or Path B** (Path A recommended). Neither
   was performed this session — both require either physical device access
   (A) or publishing a real signed manifest to the actual server the device
   polls (B), neither of which is appropriate to execute unattended as part
   of a documentation/design task.
2. **Commit the freeze-remediation source changes first**, regardless of
   path — `RELEASE_BUILD`'s existing dirty-tree gate (unrelated to OTA,
   already verified working correctly) requires a clean commit before
   `[env:release]` will compile at all.
3. **After the migration, move `server/tools/.production_signing_key.pem`
   to a real secrets vault and delete the local copy** — this was flagged
   as outstanding in `03_OTA_SIGNING_KEY_CEREMONY.md` and remains
   outstanding; do not let it linger on this laptop past the migration.
4. Once the new firmware is confirmed running (`show`'s `fw`/`build_commit`
   line, or `/api/v1/info`), the new diagnostics counters and alarms from
   blocker-closure work in this freeze become live for the first time —
   worth a one-time live smoke test (already recommended in the
   independent certification's operational recommendations).

This blocker is **verification-complete; execution of one migration path
(A or B) remains outstanding as a field action.**
