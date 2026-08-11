# Covio Oil Meter — Plant Visit

> **First, tell Claude which of these you are doing.** It changes everything
> that follows.
>
> | | |
> |---|---|
> | **Plant visit** | You are at a machine that is running production. Read this page. |
> | **Bench testing** | You have a spare unit on a desk. Plug in USB, open Claude Code here, and paste **`BENCH-PROMPT.txt`**. Claude does the rest. |
>
> Also say **which machine**: the Balaji oil flow meter, or the Miki Wire
> sensor at Ranchi. They run different firmware and are not interchangeable —
> the wrong image boots fine and then silently counts nothing.

**You need:** a laptop, a **USB data cable** (a charge-only cable will not work),
and this folder.

The meter is live production equipment. Everything in the normal visit is
**read-only** — you cannot break it by following these steps.

---

## Your three steps

**You do not have to type any commands.** Claude runs everything. You plug in
the cable and answer its questions.

**1. Plug the USB cable into the meter.**
Windows needs no driver for this board. Wait about 10 seconds.

**2. Open Claude Code in this folder.**
Right-click the `Covio-Plant-USB-Kit` folder → **Open in Terminal**, then start
Claude Code there. Opening it *in this folder* matters — that is how it picks up
the safety rules.

**3. Open `PROMPT.txt`, copy all of it, paste it into Claude, press Enter.**

Then just follow what Claude tells you. It will run the checks itself, listen to
the meter for about 7 minutes, and explain what it found. **Do not unplug the
cable until Claude says you are done.**

That's the whole visit. If you manage those three steps, the trip was a success.

---

## The one line we are hunting for

Somewhere in the captured log there should be a line like:

```
[OTA] REJECTED candidate 1.0.2: OTA_AUTH_NO_TIME_SOURCE
```

The meter has been refusing a software update and **only the meter knows why**
— it says so on this cable and nowhere else. Claude will spot the line and tell
you. If it doesn't appear, ask Claude to capture again for longer.

It is also possible the line never appears because no update is being offered
right now. That is a server-side thing, not a fault with the meter, and not
something you can see or fix from the plant. Claude knows to tell you the
difference — don't worry about it either way.

---

## Do NOT do these

You will not run into them by following the steps above, but if anyone tells
you to, **stop and phone the founder first**:

- ❌ Do not type **`factory`** into any serial terminal. It wipes the meter's
  identity and the meter stops metering.
- ❌ Do not flash any file with **`merged`** in its name. Same damage. That file
  is deliberately not in this kit.
- ❌ Do not run **`erase_flash`**.
- ❌ Do not run `03_flash_app_only.ps1` unless the founder gives you the
  authorization phrase. Reflashing is not part of a normal visit.

Everything in `scripts\01_capture.ps1` is safe to run as many times as you like.

---

## If something looks wrong

| What you see | What to do |
|---|---|
| No COM port found | Try a different USB cable — most cables are charge-only. Then try a different USB socket. |
| Port found, but nothing printed | Do not flash anything. Save the log and call the founder. |
| Red text in the window | Read it — the scripts say plainly what happened and whether anything was written. Then call the founder. |
| You typed something into the meter by mistake | Say so immediately. Nothing is unrecoverable if it's known about; a hidden mistake is what causes real damage. |

**Ask rather than guess.** Nobody expects you to know this device.

---

## What's in this folder

| | |
|---|---|
| `PROMPT.txt` | The prompt to paste into Claude Code |
| `CLAUDE.md` | Safety rules Claude reads automatically — don't edit |
| `scripts\` | The four scripts. **Claude runs these — you don't need to.** |
| `artifacts\` | The verified firmware image (only needed for an authorized reflash) |
| `evidence\` | Everything captured at the plant ends up here |

---

## Fallback: if Claude Code will not start

Only if the laptop has no working Claude Code. Open PowerShell in this folder
and run these two, in order:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\00_setup.ps1
.\scripts\01_capture.ps1
```

Then send the whole `evidence\` folder to the founder. Do not run anything else.
