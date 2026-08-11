# Plant USB Kit

Everything needed to work on a Covio device over USB, from a laptop that has
nothing installed on it. Written for someone who is **not** a firmware
engineer: they plug in a cable, open Claude Code in this folder, and paste one
prompt.

## Get it

```
git clone https://github.com/consultifidata-cyber/covio.git
cd covio/tools/plant-usb-kit
```

No git? Download the repo as a zip from GitHub and expand it — nothing here
needs git to run.

## Then paste one prompt

| Situation | Paste |
|---|---|
| Spare unit on a desk | **`BENCH-PROMPT.txt`** |
| A machine running production | **`PROMPT.txt`** — and read `START-HERE.md` |

Claude reads `CLAUDE.md` (the safety rules) and runs the scripts itself.

## Firmware is not stored here

`.bin` files are fetched from GitHub Releases, never committed:

- `scripts\05_check_repo.ps1` → the latest release, into `artifacts\bench\`
- `scripts\06_fetch_plant_firmware.ps1` → v1.0.2, into `artifacts\plant-v1.0.2\`

Each download is verified against the `.sha256` CI generated beside it. A
committed binary would eventually drift from the release it claims to be, and
production firmware should not land on a laptop merely because someone cloned
a repo. `boot_app0.bin` is the one exception — 8 KB, framework-supplied,
version-independent, and not obtainable without a full PlatformIO install.

## Bench and plant are separate on purpose

|  | Bench | Plant |
|---|---|---|
| Firmware | latest release | v1.0.2, hash-pinned |
| Script | `04_bench_flash.ps1` | `03_flash_app_only.ps1` |
| Authorization | operator types `BENCH` | founder's phrase **and** `FLASH` |
| Evidence capture first | encouraged | required — the script refuses without it |

The newest build is **not** automatically the right thing to put on a machine
that is metering production. `05_check_repo.ps1` writes only to the bench
folder; it can never update the plant one.

Before writing anything, `04_bench_flash.ps1` reads the device's own
`device_id` and refuses if it appears in `known-live-devices.txt`.

## The scripts

| | |
|---|---|
| `00_setup.ps1` | Verify the kit, find the device, install esptool |
| `01_capture.ps1` | **Read-only.** 7-minute serial capture — needs no Python |
| `02_rotate_token.ps1` | Write a new API key via the console |
| `03_flash_app_only.ps1` | Plant reflash — gated, app-only at `0x10000` |
| `04_bench_flash.ps1` | Bench flash of the latest release |
| `05_check_repo.ps1` | Fetch the latest firmware into `artifacts\bench\` |
| `06_fetch_plant_firmware.ps1` | Fetch v1.0.2 — deliberate, plant only |

Nothing here ever writes outside `0x10000` (app) and `0xe000` (boot_app0), and
no script contains an erase path. See
`artifacts/WHY-THERE-IS-NO-MERGED-IMAGE.txt` for why a whole-flash image must
never be used on a commissioned unit.

## Requirements

Windows with PowerShell. Jobs 1 and 2 need nothing else. Flashing needs Python
plus esptool — `00_setup.ps1 -InstallEsptool` handles it.

Keep every `.ps1` here **pure ASCII**: PowerShell 5.1 reads a BOM-less file as
cp1252, so a UTF-8 em dash becomes `U+201D`, which it treats as a closing
quote — the script then fails to parse, with an error pointing at an unrelated
brace.
