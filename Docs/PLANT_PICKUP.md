# Plant Pickup — the two products this repo ships

One source tree, two machines. If you are standing at a plant with a cable in
your hand, this is the only page you need.

| | **Covio oil flow meter** | **Miki Wire proximity sensor** |
|---|---|---|
| Site | Balaji Foods | Miki Wire Line 1, Ranchi — Wire Drawing Machine 1 |
| Device | mDNS `covio-858428.local` (device_id is the eFuse MAC — read it with `show`) | `esp32-F84AD1A172E0` (MW-001) |
| Board | Waveshare ESP32-S3-Relay-1CH | Waveshare ESP32-S3-POE-ETH-8DI-8DO |
| Sensor | NPN, via external PC817 opto | NPN proximity (LJ12A3-4-Z/BX class), onboard opto |
| Sensor pin | **GPIO1** (breakout `IO1`) | **GPIO4** (`DI1` terminal) |
| Backend | `https://data.funtastik.co.in` | `https://compliance.mikigroup.co.in` |
| OTA identity (`hw_compat`) | `covio-oilflow-v1` — **FROZEN** | `miki-wire-v1` |
| Build environment | `[env:release]` | `[env:release-mikiwire]` |
| Release asset | `covio-oilflow-vX.Y.Z-app.bin` | `miki-wire-vX.Y.Z-app.bin` |

Everything else — queue, totalizer, sync, OTA, watchdog, provisioning,
diagnostics, the whole `/api/iot/flow/` contract — is identical. The Miki
backend runs the same API paths as a compatibility adapter; only the host
differs.

---

## Flashing over USB

**The normal case, for any unit already installed and working:**

```
python -m esptool --chip esp32s3 --port COMx --baud 460800 \
  write_flash 0x10000 <the -app.bin for your machine>
```

Writing at `0x10000` preserves NVS (API key, WiFi, server URL, totalizer
checkpoint, OTA anti-downgrade floor) and the SPIFFS reading queue at
`0xC90000`. **Never run `erase_flash`.**

Check the `.sha256` before you flash. Every asset has one.

Both boards are ESP32-S3 with native USB-CDC/JTAG (`VID_303A`/`PID_1001`), so
Windows needs no driver. `platformio.ini` hard-codes `upload_port = COM6` —
never rely on it; pass `--upload-port` explicitly.

**⚠ The file you must not use on a working machine:**
`<product>-vX.Y.Z-FACTORY-ONLY-full.bin` is for virgin units. It is written at
`0x0` and blanks NVS (`0x9000`–`0xDFFF`), which erases the API key. A
`RELEASE_BUILD` image with no API key halts at boot — the machine stops
metering until somebody re-provisions it by hand at the device.

**⚠ Never flash one product's image to the other machine.** The firmware
refuses a cross-product *OTA* on its own (`hw_compat` mismatch), and CI fails
the build if either image ever carries the other's identity — but nothing
stops a USB cable. The sensor is on a different GPIO on each board, so a
cross-flashed unit keeps running while counting nothing.

## Flashing from source

```
pio run -e release          -t upload --upload-port COMx   # oil flow
pio run -e release-mikiwire -t upload --upload-port COMx   # Miki Wire
```

PlatformIO's normal upload writes bootloader, partition table, boot_app0 and
the app as separate segments — it does not touch NVS or SPIFFS. Never add
`-t erase`.

## Serial console (115200)

`help` `show` `set url <u>` `set key <k>` `set wifi <ssid> <pass>` `reboot`
`factory` `provision`

- `show` reports the API key as a status plus a 6-byte fingerprint, never the
  key itself. Capture the fingerprint before and after a rotation to prove it
  took effect.
- ⛔ **`factory`** wipes NVS — the same damage as the factory image, one word.
- ⛔ **`provision`** drops the unit into AP-mode setup on next boot, and in AP
  mode this firmware stops metering entirely.

## OTA

OTA is published per product and gated on `hw_compat`, so a manifest for one
machine is structurally incapable of being accepted by the other.

Manifests are signed **offline** with `server/tools/sign_manifest.py`. The
private key never goes on a server. The canonical signed string is frozen in
`ota_manifest_auth.h`; `schema_version` must equal 1 exactly and
`security_version` must be ≥ the device's stored floor.

**Status, stated honestly:** manifest delivery is proven in the field. Signature
verification, download and flash are **not** — no device has yet completed an
OTA end to end. Until one has, treat OTA as unproven and prefer USB. The
bootloader arms no rollback trial, so a bad image on a unit with no USB access
is unrecoverable; that is why publishing is a deliberate, separate step
(`--publish` / `--unpublish`).

## Adding a third product later

1. Add a profile flag in `config.h` and give it its **own** `DEVICE_MODEL`.
   Never re-use another product's — that string is the entire cross-flash
   barrier.
2. Add its pin table under a new `BOARD_MODE` block.
3. Add `[env:release-<product>]` in `platformio.ini`.
4. Add it to `PRODUCTS` in `ci.yml` and to the asset check in `release.yml`.
5. Extend the `static_assert` block in `test/native_cpp/test_board_config.cpp`
   and add a CI compile variant for it.
