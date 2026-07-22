# 09 — USB Recovery Runbook

**Not executed. Documented for use only if a stop condition in doc 08
triggers, under separate authorization for this specific action.**

## Detection

```
# List serial devices (read-only, does not open the port)
# Windows Device Manager, or:
where esptool
```
The device previously enumerated as `USB Serial Device (COM6)`,
`VID_303A&PID_1001` (Espressif). Confirm this is still the correct port
before proceeding — a different COM number would indicate a different
physical device or a changed enumeration order.

## Bootloader entry

The ESP32-S3's native USB-CDC/JTAG typically auto-resets into the
bootloader on `esptool`'s own DTR/RTS toggle sequence — no manual
BOOT-button procedure should be needed on this board, but if `esptool`
reports a connection failure, the manual fallback is: hold BOOT, tap
RESET, release BOOT (standard ESP32 bootloader entry).

## Flash recovery (exact command, NOT executed)

```
pio run -e esp32dev -t upload --upload-port COM6
```
This uses whatever is currently checked out on this branch. To restore
SPECIFICALLY the recorded known-good artifact (doc 07) rather than
whatever the working tree currently contains:
```
python -m esptool --chip esp32s3 --port COM6 --baud 460800 write_flash \
  0x10000 Docs/audit/coviu_oil_meter_p0_remediation_phase2/evidence/known-good-1.0.0.bin
```
(`0x10000` is `app0`'s offset per `default_16MB.csv` — confirmed this
session by reading the actual partition table, not assumed.)

## Partition/configuration restoration

A full-image reflash via `pio run -t upload` overwrites only the `app`
partition PlatformIO targets by default (bootloader + partition table +
app0, per its standard upload behavior) — `spiffs` (the queue) and `nvs`
(config/security-floor) are NOT touched by a normal `-t upload`, so
existing queue data and the anti-downgrade floor survive a USB recovery
reflash. This is a property of PlatformIO's default upload flags, not
independently re-verified against this exact board this session.

## Queue preservation limits

If recovery is needed specifically because the device is stuck in a boot
loop that never reaches `LittleFS.begin()`/queue recovery at all, the
queue contents are unreadable until the device boots successfully again —
a full flash+NVS erase (`erase_flash`) would be needed only in a
worse-case scenario than anything in doc 08's test plan should produce,
and is explicitly NOT part of this runbook's normal recovery path (it
would also erase the anti-downgrade security floor and all queued data).

## Spare-device substitution

No spare pre-provisioned unit exists in this session's evidence — flagged
as a genuine operational gap for a real pilot (mandate's own "a spare
provisioned unit and USB recovery image are available for pilot
operations" requirement) rather than assumed present.

## Test-abort conditions

Same as doc 08's "Stop conditions" section — reproduced here for a reader
who jumps straight to this runbook: abort and do not reflash if the
device's configured endpoint is ever found to be a production address, or
if physical device identity cannot be confirmed to match section 1 of
doc 08.
