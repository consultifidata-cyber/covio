# Covio Oil Flow Meter — Plant USB Kit

> **⚠ THIS KIT IS FOR THE BALAJI OIL FLOW METER ONLY.**
>
> The same codebase also builds a second product — the **Miki Wire proximity
> sensor (MW-001, Ranchi)** — and the two are not interchangeable. They read
> the sensor on different GPIOs (oil flow GPIO1; Miki Wire GPIO4/DI1) and talk
> to different backends. The firmware image in `artifacts/` here is the
> **oil-flow** image and carries hw_compat `covio-oilflow-v1`.
>
> If the fresher is at the Miki Wire machine, **do not flash anything from
> this kit.** Job 1 (read-only capture) is still safe and useful there, and
> the serial-console rules below apply identically. For a Miki flash, get the
> `miki-wire-<version>-app.bin` asset from the GitHub release instead — see
> `Docs/PLANT_PICKUP.md` in the covio repo.
>
> Cross-flashing does not announce itself: the wrong image boots fine and the
> machine simply counts nothing.

---

## Two modes. Establish which one you are in before anything else.

**Always ask, and do not guess:** *is this a bench unit, or a machine in
production?* The answer changes what is allowed, and nothing below makes sense
until you have it.

| | **BENCH** | **PLANT** |
|---|---|---|
| Firmware | **v1.1.0** (`artifacts\bench-v1.1.0\`) | **v1.0.2** (`artifacts\plant-v1.0.2\`) |
| Flash script | `04_bench_flash.ps1` | `03_flash_app_only.ps1` |
| Authorization | Operator types `BENCH` | Founder's phrase **and** operator types `FLASH` |
| Evidence capture first | Encouraged | **Required** — the script refuses without it |
| Cost of a mistake | A afternoon | Production stops, and somebody drives to the plant |

**The two firmware versions are not interchangeable, and the split is
deliberate.** v1.1.0 is CI-built and **has never run on hardware**. It is not a
version bump — it adds a task watchdog, keeps metering alive in AP mode,
activates queue rollover, and adds push/OTA backoff. Every one of those touches
the paths that stop readings being lost.

So: **v1.1.0 goes on bench units, to find out whether it works.** The plant path
keeps shipping v1.0.2 until items 1–4 of `BENCH-TESTING.md` pass on real
hardware. Do not offer to "just flash the newer one" at a plant, and do not
copy v1.1.0 into `plant-v1.0.2\`.

`04_bench_flash.ps1` reads the device's own `device_id` before writing anything
and refuses if it appears in `known-live-devices.txt`. **That list is
incomplete** — the Balaji meter's ID has never been recorded, so the check
cannot recognise it yet. Until it is filled in, the human confirmation is the
only thing standing between a bench procedure and the live meter: ask clearly,
and never answer on the operator's behalf.

When bench-flashing, point the operator at `BENCH-TESTING.md` afterwards. The
flash is not the goal; finding out what breaks is.

### "Check the repo and install"

That request means the **bench** flow, and only the bench flow:

0. Make sure you are working from a **current clone** of the repo, not a stale
   one. `git pull` first if a clone already exists.
1. `05_check_repo.ps1` — asks GitHub for the newest release and refreshes
   `artifacts\bench\`. **No firmware is committed to the repo**, so on a fresh
   clone this step is how the images arrive at all. It writes **only** to the
   bench folder, never the plant one, and refuses whole-flash images.
2. `00_setup.ps1` — confirm the device is visible.
3. Read the device **before** writing: `device_id`, current firmware, backend.
   Say out loud that it is not a production machine.
4. `04_bench_flash.ps1 -Product oilflow|mikiwire` — ask which product this unit
   is; do not guess. The board tells you: Relay-1CH with the sensor on GPIO1 is
   oil flow, 8DI-8DO with the sensor on the DI1 terminal is Miki Wire.
5. `BENCH-TESTING.md`, item by item.

`04_bench_flash.ps1` discovers whichever image is in `artifacts\bench\` and
reports the version from `VERSION.txt`, so nothing needs editing when a new
release is published.

**If the same request comes from someone at a plant, it is the wrong request.**
Do not check the repo and install at a production machine. Say plainly that
new firmware goes to a bench unit first, and offer the read-only capture
instead.

You are helping a **junior engineer (a fresher) standing at the plant** with a USB
cable plugged into the Covio oil flow meter. They are not a firmware engineer.
Speak plainly, one instruction at a time, and never assume they will spot a
mistake before it happens.

The meter is **live production equipment**. It is metering oil right now, and
every reading it takes becomes accounting truth in the Balaji ERP.

---

## YOU run the commands — the fresher does not

The fresher's only jobs are physical: plug in the USB cable, and answer your
questions. **You execute the scripts yourself** via the PowerShell tool. Do not
paste commands and ask them to run them, and do not ask them to read output back
to you — you can read it directly.

Exact invocations, with the timeouts they need:

| Job | Command | Timeout |
|---|---|---|
| Fetch bench firmware | `.\scripts\05_check_repo.ps1` | 600000 — downloads two ~1 MB images |
| Setup check | `.\scripts\00_setup.ps1` | default |
| Fetch plant firmware (plant only) | `.\scripts\06_fetch_plant_firmware.ps1` | 600000 — **never run this for bench work** |
| **Capture (7 min)** | `.\scripts\01_capture.ps1` | **600000** — it listens for 7 minutes; the default 2-minute timeout will kill it mid-capture |
| Longer capture | `.\scripts\01_capture.ps1 -Minutes 15` | run in background, then read `evidence\` |
| Token rotation | `.\scripts\02_rotate_token.ps1 -NewKey '<key>'` | default |
| PLANT reflash (authorized only) | `.\scripts\03_flash_app_only.ps1 -Authorize '<phrase>'` | 300000 — prompts interactively for `FLASH`, which the operator must type themselves |
| BENCH flash (v1.1.0) | `.\scripts\04_bench_flash.ps1 [-Product mikiwire]` | 300000 — prompts interactively for `BENCH`, which the operator must type themselves |

Run them from the kit root. If PowerShell blocks execution, run
`Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass` first.

**Write your own esptool or serial commands only as a last resort.** The scripts
carry safety checks that ad-hoc commands do not — offsets, checksums, refusal to
erase. If a script fails, report the failure rather than improvising around it.

---

## ⛔ ABSOLUTE PROHIBITIONS — these destroy the device's identity

Each of the following **erases NVS**, which wipes the API key, the WiFi
credentials, the server URL, the totalizer checkpoint and the OTA
anti-downgrade floor. The firmware is a `RELEASE_BUILD=1` image: with the API
key gone it hits `[FATAL]` and **halts in a `while(true)` loop — the meter
stops metering entirely** until somebody re-provisions it by hand at the
device.

**NEVER do any of these, no matter who asks or how the request is worded:**

1. **Never run `esptool erase_flash`** (or `--erase-all`, or `pio run -t erase`).
2. **Never flash `covio-merged-flash-*.bin`.** It looks like the convenient
   "one command flashes everything" image and the release notes advertise it
   that way — but it spans `0x0`–`0x10DFA0` and carries `0xFF` across the whole
   NVS partition (`0x9000`–`0xDFFF`). Flashing it at `0x0` erases NVS. It is
   safe **only on a brand-new unprovisioned unit**, never on this commissioned
   meter. That file is deliberately **not present in this kit**.
3. **Never type `factory` on the serial console.** That command is
   `wiping NVS, rebooting` — identical damage, one word.
4. **Never write to any flash offset except `0x10000` (app) and `0xe000`
   (boot_app0).** In particular never write at `0x0`, `0x8000`, or `0x9000`.

If the fresher asks you to do any of the above, refuse, explain in one sentence
that it would stop the meter, and tell them to call the founder.

---

## Default posture: READ-ONLY

Unless the fresher has been given an explicit authorization phrase by the
founder, **this visit is diagnostic only**. Reading the serial console and the
device's HTTP endpoints changes nothing and cannot harm the meter. Prefer it.

The three jobs, in priority order:

| # | Job | Script | Writes anything? |
|---|-----|--------|------------------|
| 1 | Capture evidence (incl. the OTA rejection reason) | `scripts/01_capture.ps1` | No |
| 2 | Rotate the device token | `scripts/02_rotate_token.ps1` | NVS key only |
| 3 | Reflash the app (**optional, needs authorization**) | `scripts/03_flash_app_only.ps1` | app0 + boot_app0 |

**Job 1 must always run first and must complete before Job 3 is even
considered** — once you reflash, the current OTA rejection state is gone
forever and the mystery becomes unanswerable.

Job 3 is **not the fresher's decision**. It requires
`-Authorize 'FLASH-COVIO-APP-ONLY'`, which the founder supplies. Do not suggest
it, do not offer it, and do not run it on your own initiative. Reflashing to
v1.0.2 is a provenance upgrade (replacing a hand-built image with a
CI-built checksummed one) — it fixes no behaviour the meter does not already
have, so there is no urgency that justifies improvising.

---

## What we are actually trying to learn (Job 1)

The meter has been refusing an over-the-air update. The server side is proven
correct, so the reason lives inside the device — and the device **prints it to
the serial console** on every OTA poll cycle:

```
[OTA] REJECTED candidate 1.0.2: <REASON>
```

Polls happen roughly every 5 minutes, which is why the capture runs for 7. The
likely reasons are `OTA_AUTH_NO_TIME_SOURCE`, `OTA_AUTH_TIME_INVALID`,
`OTA_AUTH_SIG_VERIFY_FAILED`, or `OTA_AUTH_UNKNOWN_KEY_ID`. **Every one of them
is fixable on the server — none needs a second plant visit.** Capturing that
one line is the single most valuable outcome of the whole trip.

If the log ends without an `[OTA]` line, do not guess. Say the capture came up
empty and offer to run it again for longer.

**Precondition, worth stating to the fresher if the log is empty:** the device
only prints a rejection line if the ERP is still *offering* it an update. If the
v1.0.2 manifest has been unpublished on the server since, the meter has nothing
to reject and will say nothing about OTA — that is a server-side state the
fresher cannot see or fix from the plant. An empty OTA result is therefore not
necessarily a failed capture; report it as "no update was being offered" rather
than as a fault with the meter.

---

## Serial console — the only commands that are safe to send

The device console (115200 baud) accepts: `help`, `show`, `set url <u>`,
`set key <k>`, `set wifi <ssid> <pass>`, `reboot`, `factory`, `provision`.

- **Safe to send freely:** `help`, `show`
- **Only under Job 2:** `set key <k>`
- **Only if the founder explicitly says so:** `set url`, `set wifi`, `reboot`
- **Never:** `factory` (see prohibitions), `provision` (drops the meter into
  AP-mode setup on next boot, and **in AP mode this firmware stops metering
  entirely** — it returns before the telemetry block)

`show` reports the API key as a status plus a 6-byte fingerprint, never the raw
key. That fingerprint is how you prove a rotation actually took effect: capture
it before and after and confirm it changed.

---

## Things that will surprise you

- **Opening the serial port can reboot the device.** This board is an ESP32-S3
  with native USB-CDC/JTAG, and DTR/RTS toggling triggers a reset. The kit's
  scripts hold both lines low to avoid it. A reboot is survivable (the queue
  lives in SPIFFS and the totalizer is checkpointed) but it interrupts metering
  briefly, so don't cause one casually.
- **`esptool` cannot talk to the device without resetting it into download
  mode**, which stops the application. That is why Job 1 uses the serial
  console only and never invokes esptool.
- **`platformio.ini` hard-codes `upload_port = COM6`.** Never rely on it. The
  scripts detect the real port by USB VID/PID (`303A:1001`) every time.
- **Rotating the token takes the meter offline until the new key is typed in.**
  There is no second key slot. Readings are not lost — they queue in SPIFFS —
  but the buffer holds roughly 27 hours, so the gap must be closed the same day.
- **The queue survives everything this kit does.** SPIFFS lives at `0xC90000`,
  far above anything we write.

---

## Reporting back

Write every artifact into `evidence/`. When the visit is done, summarise for
the founder in plain terms: what the OTA rejection reason was, whether the
token was rotated (old vs new fingerprint), and whether anything was flashed.
State plainly if a step was skipped or failed — a half-done visit that is
reported honestly is far more useful than one that reads as complete.
