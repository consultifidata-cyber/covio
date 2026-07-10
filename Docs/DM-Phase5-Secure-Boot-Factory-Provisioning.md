# DM-Phase 5 — Secure Boot V2 + Flash Encryption Factory Provisioning

**Status:** Documented procedure only — **NOT executed, NOT verified**. This
document exists because ADR-005 and the Live Readiness Plan's DM-Phase 5
card both classify Secure Boot V2 + Flash Encryption as a **factory
process** ("Files touched: ... factory process (not a firmware file — an
eFuse-burning step at first flash)"), not firmware source code. It requires
physical ESP32 hardware and the `espsecure.py`/`esptool.py` (or
`idf.py`-equivalent) toolchain, neither of which exists in the environment
this document was authored in (no ESP32 device attached, no Python
interpreter, no `pio`/`arduino-cli` binary — confirmed absent repeatedly
throughout this project's development). **Do not treat this document as
evidence that Secure Boot/Flash Encryption has been implemented or
verified.** It is a rehearsal script for whoever next has access to real
disposable bench hardware, per the Live Readiness Plan's own explicit
instruction: *"Rehearse extensively on disposable bench units first."*

## Why this is separate from `ota.h`/`config.h`

Secure Boot V2 and Flash Encryption are ESP32 **eFuse** features — one-time,
physically irreversible hardware fuses burned into the chip. They are
enabled by (a) build-time configuration that signs the application image
and (b) a one-time factory-flash step using signing/encryption keys that
must never be committed to source control (ADR-005: *"RSA-3072 signing keys
generated and held by Covio and never distributed to the field or committed
to source control"*). No application `.h`/`.ino` file can express "burn this
eFuse" — that action happens entirely outside the compiled firmware image,
at flash time.

## Prerequisites (not present in this repo or this environment)

- A real ESP32 board connected via USB.
- ESP-IDF's `espsecure.py`/`esptool.py` (bundled with the Arduino-ESP32 core
  toolchain, or a standalone ESP-IDF install) able to talk to that board.
- A secure offline machine to generate and store the RSA-3072 signing key —
  **never** the same machine that has this source repository checked out
  with any kind of broad network/AI-agent access, and never committed to git.

## Procedure (to be rehearsed on disposable bench units only)

1. **Generate the signing key once**, offline, on a machine dedicated to key
   custody:
   ```
   espsecure.py generate_signing_key --version 2 covio_secure_boot_signing_key.pem
   ```
   Store this file in an offline, access-controlled location. It signs every
   production firmware image from this point forward; losing it means no
   future OTA can be signed for already-Secure-Boot-enabled units, and
   leaking it defeats Secure Boot entirely.

2. **Generate the Flash Encryption key** the same way, with the same
   custody rules:
   ```
   espsecure.py generate_flash_encryption_key covio_flash_encryption_key.bin
   ```

3. **Build configuration.** The production/release PlatformIO environment
   (a new `[env:release]`, separate from the existing bench `[env:esp32dev]`
   in `platformio.ini` — not added by this phase, since it cannot be
   compiled or tested here) must set the ESP-IDF `sdkconfig` options that
   enable Secure Boot V2 signing and Flash Encryption at build time, and
   must be built with `-DRELEASE_BUILD=1` (see `config.h`) so this
   codebase's own compile-time guards (`certs.h`'s placeholder-CA `#error`,
   and `covio_firmware.ino`'s default-API-key boot refusal) are active for
   that build.

4. **First factory flash, per unit** (this is the one-way, irreversible
   step):
   - Flash the signed application image and bootloader using the signing
     key from step 1.
   - Burn the Secure Boot V2 eFuses (`espsecure.py burn_key` / the
     equivalent `idf.py` factory-flash flow) so the bootloader thereafter
     refuses to boot any image not signed by that key.
   - Enable Flash Encryption using the key from step 2, so NVS secrets
     (API key, WiFi credentials) are unreadable via a direct flash read.
   - **This step must never be run against a unit you cannot afford to
     lose.** A mistake here is permanent — there is no un-burn.

5. **Factory API key provisioning** happens in the same session, reusing
   the existing ADR-008 factory-test console flow (`set key <apikey>`) —
   this part already exists and is unaffected by Secure Boot/Flash
   Encryption. Combined with this file's `covio_firmware.ino` guard
   (`RELEASE_BUILD=1` refuses to leave `setup()` with the default key still
   active), this closes ADR-005's *"no device leaves the factory floor with
   the default API key active"* Definition-of-Done item end-to-end.

## Verification (ADR-005's Definition of Done — not yet performed)

- [ ] All four cloud endpoints verified reachable over HTTPS against the
      real pinned CA, on a real device, on a real network.
- [ ] A MITM bench test (an untrusted intercepting proxy) is verified
      **rejected** by the pinned-CA `WiFiClientSecure` — confirms
      `certs.h`'s pin is actually doing its job, not silently bypassed.
- [ ] Secure Boot + Flash Encryption verified **enabled** via an eFuse
      summary (`espefuse.py summary`) on a factory-flashed bench unit.
- [ ] A deliberately incorrectly-signed OTA image is verified **rejected**
      by the bootloader (attempt an OTA update signed with the wrong key,
      or unsigned, and confirm the device refuses to boot it).
- [ ] No unit leaves the factory floor with `DEFAULT_API_KEY` still active
      (verified by attempting the RELEASE_BUILD boot guard against a
      deliberately unprovisioned unit, confirming it halts as designed).

Every checkbox above requires physical hardware this environment does not
have. They remain open until performed on real bench units.
