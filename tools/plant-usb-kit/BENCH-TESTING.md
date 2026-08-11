# Bench testing v1.1.0

v1.1.0 is CI-built. It compiles, links, and carries the right identity — that
is *all* anyone has proven. **No part of it has ever run on hardware.**

That matters more than usual here, because v1.1.0 is not a version bump. It
adds a task watchdog, keeps metering alive in AP mode, activates queue segment
rollover, adds push/OTA backoff, and bounds the server ack sequence. Every one
of those touches the paths that keep readings from being lost.

So the bench unit is where v1.1.0 earns the right to go near a plant.

## Getting started

**The fresher does not need to run any of this.** Plug in USB, open Claude Code
in this folder, and paste **`BENCH-PROMPT.txt`**. Claude fetches the latest
firmware, installs it, and walks the list below.

The commands, for reference:

```powershell
.\scripts\05_check_repo.ps1                     # fetch the latest release
.\scripts\04_bench_flash.ps1                    # oil-flow build
.\scripts\04_bench_flash.ps1 -Product mikiwire  # Miki Wire build
```

`05_check_repo.ps1` updates `artifacts\bench\` only — never the plant folder,
so a newly published build can never quietly become the thing someone flashes
at a plant.

The script reads the device's `device_id` first and refuses if it appears in
`known-live-devices.txt`. It writes the application at `0x10000` only, so a
provisioned bench unit keeps its settings and its queued readings.

## What to actually check

Work down this list. **Record what happened, including anything that did not
work** — a bench test that only reports successes has told you nothing about
whether it is safe to ship.

### 1. It boots at all
Boot banner shows `1.1.0`, and `show` reports the expected `device_id`,
`server_url` and API-key fingerprint — unchanged from before the flash. If NVS
did not survive, stop; nothing else on this list matters.

### 2. The watchdog is armed
Banner shows `[WDT] task watchdog armed: 60s`. This is the headline fix — the
old firmware had three `while(true)` paths that bricked until a site visit.

To prove it actually *fires*, build with `-DWDT_TEST_BUILD=1` and use the
console's `test_hang` command, which simulates a genuine main-loop hang. Watch
for detect → reset → classify → recover. **Never ship a `WDT_TEST_BUILD` image.**

### 3. Metering survives AP mode
The old firmware returned before the telemetry block in AP mode, so a WiFi
outage stopped counting entirely. Force AP mode (drop the WiFi the unit is
joined to, or use `provision`), then feed pulses. The totalizer must keep
advancing and checkpointing. Restore WiFi and confirm the buffered readings
push.

### 4. The queue rolls over instead of destroying new data
The old ceiling was ~27 h, after which `append()` failed and the **newest**
data was lost. Fill the queue with the server unreachable — point `set url` at
a dead host — and keep pulses coming past the old limit. Confirm rollover
happens and that recent readings survive. This is slow; let it run.

### 5. Backoff behaves
With the server unreachable, push retries should space out rather than hammer.
Watch the retry interval grow in the console.

### 6. Identity is right
`show` and `/api/v1/info` must report the `hw_compat` and backend for the
product you flashed:

| Build | hw_compat | backend |
|---|---|---|
| oil flow | `covio-oilflow-v1` | `https://data.funtastik.co.in` |
| Miki Wire | `miki-wire-v1` | `https://compliance.mikigroup.co.in` |

### 7. OTA cross-product rejection — the one worth doing properly
Offer the bench unit a manifest built for the *other* product. It must refuse
on `hw_compat` mismatch, and print the rejection to serial. This is the guard
that stops an oil-flow image reaching the Miki machine, and hardware is the
only place it has never been tested.

### 8. OTA end to end
Still unproven anywhere. If the bench unit can be pointed at a server you
control, this is the safest place in the world to find out whether signature
verification, download and flash actually work — a bench unit that bricks costs
nothing, and there is no bootloader rollback to save a plant unit that does.

## When you are done

Write down, per item: pass, fail, or not attempted. "Not attempted" is a
legitimate and useful answer.

Until items 1–4 pass on real hardware, v1.1.0 does not go near the Balaji meter
or MW-001, and the plant kit keeps shipping v1.0.2.
