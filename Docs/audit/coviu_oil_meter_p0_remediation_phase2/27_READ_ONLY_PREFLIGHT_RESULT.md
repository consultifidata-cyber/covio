# 27 — Read-Only Preflight Result

Executes the read-only preflight authorization request from
`24_REVISED_AUTHORIZATION_REQUEST.md` / plan section in
`23_REVISED_PHYSICAL_TEST_PLAN.md`. **No write, reboot, flash, or OTA
action was performed or attempted.**

## Verdict

**PREFLIGHT INCONCLUSIVE — DO NOT AUTHORIZE HARDWARE WRITES**

Not a security failure: neither the test device nor the bench server was
present/running at the time of this check, so no identity, endpoint, or
isolation claim could be verified either way. Re-run once both are live.

## What was checked (read-only, all commands and raw output below)

### 1. Serial port enumeration
```
[System.IO.Ports.SerialPort]::GetPortNames()        -> (empty)
Get-CimInstance Win32_PnPEntity | match 'COM\d'      -> (no rows)
```
**Result:** zero serial ports exist on this machine right now. COM6 does
not resolve to any device — not the intended ESP32-S3, not any other
device. No serial session was opened (there is nothing to open); no DTR/
RTS toggling, no incidental reset, no reset evidence to report.

### 2. Bench server endpoint reachability
```
Test-NetConnection -ComputerName 192.168.1.3 -Port 8000
  -> TcpTestSucceeded: False
Get-NetIPAddress (this machine)  -> 192.168.1.3 (Wi-Fi)
Get-Process python|pio|platformio -> (none running)
```
**Result:** this machine IS the documented bench-server host
(`192.168.1.3`), consistent with `23_REVISED_PHYSICAL_TEST_PLAN.md`'s
endpoint. Nothing is listening on port 8000 because `server/server.py`
is simply not running right now — not a hostile/production endpoint, just
not started. No HTTP GET was attempted against a closed port beyond the
TCP reachability probe (no application-layer request was sent or could
be meaningfully answered).

### 3. Tooling available for a future attempt
`pyserial 3.5` and `PlatformIO Core 6.1.15` are installed, so a genuine
read-only serial-monitor session and the documented HTTP GETs are both
executable once the device is plugged in and `server/server.py` is
started.

## Reconciliation

Not applicable — no session was opened against the device or server, so
there is no before/after sequence, queue, or storage state to reconcile.
No incidental reset occurred.

## Required follow-up before re-attempting this preflight

1. Physically connect the Coviu ESP32-S3 bench unit via USB and confirm
   Windows enumerates it as COM6 (`[System.IO.Ports.SerialPort]::GetPortNames()`
   should list it; Device Manager should show its USB VID/PID, e.g. a
   CP210x/CH34x bridge).
2. Start `server/server.py` on this machine (`192.168.1.3:8000`) so
   `/api/v1/info` and `/api/v1/status` are reachable.
3. Re-run this exact read-only preflight. Only if it PASSES with genuine
   device/endpoint evidence should physical-write authorization (doc 24)
   be considered.
