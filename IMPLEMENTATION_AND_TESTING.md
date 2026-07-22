# Covio Oil Flow Meter — Implementation, Testing & Remote Setup Guide

Follow this top to bottom. Every test has an expected output and a "if it fails" note. Don't skip gates — each one proves the layer the next one stands on.

---

## PART 0 — Prerequisites

- Arduino IDE with the **ESP32 board package** installed (Boards Manager → "esp32 by Espressif").
- Python 3 on your laptop.
- Laptop and ESP32 will join the **same WiFi network** for bench testing.
- Hardware as built: ESP32 + SD module (CS=GPIO4, VSPI 18/23/19) + PC817 opto (OUT→GPIO27, 10k pull-up→3.3V). MAX31865 not used in this build.
- Flashing rule (your R2): **USB only while flashing, buck OUT off VIN.**

---

## PART 1 — Bring the server up (5 min)

```bash
cd server
pip install flask
python server.py
```

Expected terminal line: `Covio bench server on http://0.0.0.0:8000`

**Find your laptop's IP** (you'll put it in the firmware):
- Windows: `ipconfig` → IPv4 Address (e.g. `192.168.1.100`)
- Linux/Mac: `ip addr` or `ifconfig` → your WLAN interface

**Gate S1:** open `http://<laptop-ip>:8000/` in a browser → the "Covio Bench Server" page loads with the K-factor form (K=1000, version 1) and an empty Devices table.

*If it fails:* firewall blocking port 8000 → allow Python/port 8000 on private networks.

---

## PART 2 — Configure & flash the firmware (15 min)

### 2.1 File layout
All `.h` files must sit **in the same folder** as `covio_firmware.ino`:
```
covio_firmware/
├── covio_firmware.ino
├── config.h  store.h  totalizer.h  queue.h
├── telemetry.h  sync.h  ota.h  provision.h
```
(The zip already has them flattened at the root — open `covio_firmware.ino` and the IDE loads the rest as tabs.)

### 2.2 Edit `config.h` — the only file you touch
```c
#define DEFAULT_SERVER_URL    "http://192.168.1.100:8000"   // <-- YOUR laptop IP
#define DEFAULT_WIFI_SSID     "your-ssid"
#define DEFAULT_WIFI_PASS     "your-pass"
#define SIM_PULSES            1        // <-- 1 for bench testing (fake pulses)
```

> **Important NVS gotcha:** these defaults are seeded into NVS **only on the very first boot**. Reflashing with changed defaults later does NOT update a device that has already run. To change settings after first boot, use the **serial console** (Part 3) or type `factory` to wipe and re-seed.

### 2.3 Board settings (Arduino IDE → Tools)
| Setting | Value |
|---|---|
| Board | **ESP32 Dev Module** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** ← required, OTA needs 2 app slots |
| Upload Speed | 921600 (drop to 115200 if flaky) |

### 2.4 Wire for bench mode
- Jumper **GPIO25 → GPIO27** (the sim signal feeds the counter pin).
- **Disconnect the opto output from GPIO27** while the jumper is on — they can't share the pin.
- The 10k pull-up on GPIO27 can stay.

### 2.5 Flash
USB in, buck off VIN → Upload → open **Serial Monitor @ 115200**.

---

## PART 3 — First boot checklist (Gate F1)

Expected serial output, in order:

```
=== Covio Oil Flow Meter 1.0.0 ===
device_id=esp32-XXXXXXXXXXXX  boot_id=1
(serial console ready — type 'help')
[OK] SD ready
[TOT] recovered total=0 writes=0
[Q] acked_seq=0
[NET] connecting to your-ssid
[SIM] test pulses ON: GPIO25 @ ~10 Hz — jumper to GPIO27
[BOOT] entering main loop
[SYNC] acked_seq=5 (sent 5)        <- appears within ~10 s once WiFi is up
```

**Try the console now** — type `show` in the Serial Monitor input line and press Enter:
```
device_id : esp32-XXXXXXXXXXXX
fw        : 1.0.0
server_url: http://192.168.1.100:8000
...
```

*If `[FATAL] SD init failed`:* card seated? FAT32? CS really on GPIO4? (The console still works in this state so you can `show`/`set`.)
*If WiFi never connects:* `set wifi <ssid> <pass>` then `reboot`.
*If no `[SYNC] acked_seq` lines:* wrong server IP → `set url http://<correct-ip>:8000` then `reboot`; also check the server terminal for incoming request logs.

---

## PART 4 — The test gates

Run in order. Keep the server terminal and Serial Monitor both visible.

### T1 — Pulses are being counted
**Do:** nothing — sim is running.
**Expect:** dashboard (`http://<ip>:8000/`, refresh) shows your device; **Raw pulses** climbs ~10/sec; **Litres** = pulses ÷ 1000.
**Note:** the sim rate dips briefly during each network push (it's a software toggle). That's normal and does NOT happen with the real meter — hardware PCNT never pauses.

### T2 — K-factor hot swap (the calibration requirement)
**Do:** on the dashboard, change K-factor from `1000` to `500`, submit.
**Expect:**
1. Dashboard header shows `version 2`; **Litres doubles instantly** for the same pulses (history recomputed — that's the whole point).
2. Within 60 s the device serial prints: `[SYNC] new calibration v2  K=500.0000`
3. `show` on the console now reports `calib : v2 K=500.0000`.
**This proves:** calibration is a website edit — no reflash, no data loss.

### T3 — Offline outage → zero-loss catch-up (the headline)
**Do:** turn off your WiFi router/hotspot. Wait 5–10 minutes. Serial shows `[NET] reconnecting...` and `[SYNC] push HTTP -1 — keeping queue` style lines; pulses keep counting. Turn WiFi back on.
**Expect:** within ~30 s, serial shows reconnection then a burst of `[SYNC] acked_seq=...` lines as the backlog drains in batches of 50. On the dashboard, **Records** count catches up to ≈ one per second of total runtime, with **no gaps** (ack only advances contiguously — a gap would freeze it, so a climbing ack IS the no-gap proof).
**This proves:** ACK-gated queueing; no silent loss during outages.

### T4 — Power-cut totalizer survival
**Do:** note the current Raw pulses on the dashboard. **Yank the USB cable** mid-run. Wait 10 s. Replug.
**Expect serial:**
```
device_id=...  boot_id=2                  <- incremented
[TOT] recovered total=<N> writes=<M>      <- NOT 0; within ~1-2 s of pre-cut value
[Q] acked_seq=<K>                          <- preserved
```
Dashboard totals **continue** from where they were (≤ ~1–2 s of pulses lost — that's the checkpoint cadence, by design).
**This proves:** dual-slot CRC checkpointing; totalizer survives hard power cuts.

### T5 — Server-down resilience + idempotency
**Do:** Ctrl+C the server. Watch serial: `[SYNC] push HTTP -1 — keeping queue` (or similar) every 5 s; device keeps queueing. After 2–3 min, restart `python server.py`.
**Expect:** backlog drains; on the dashboard, Records = max seq exactly — **no duplicates** even though some records were inevitably re-sent around the restart (the server absorbed them via `INSERT OR IGNORE` on `(device_id, seq)`).

### T6 — Provisioning console round-trip
**Do:** `set url http://10.0.0.99:8000` → `reboot` → watch pushes fail → `set url http://<real-ip>:8000` → `reboot`.
**Expect:** device re-points **with no reflash**, backlog from the "broken" minutes syncs on recovery. This is exactly how you'll re-point the device to the LCS or cloud later.

### Gate: all six green → the sync engine is proven. Move to OTA.

---

## PART 5 — OTA on the bench (do this BEFORE you leave town)

### 5.1 Make a visibly different v1.0.1
In `config.h`: `#define FW_VERSION "1.0.1"`. Optionally change the boot banner text so the update is unmistakable.

### 5.2 Export the binary
Arduino IDE → **Sketch → Export Compiled Binary**. Find `covio_firmware.ino.bin` in the sketch folder (Sketch → Show Sketch Folder; it may be under `build/esp32.esp32.esp32/`).

### 5.3 Host it
```
server/firmware/covio_firmware.ino.bin      <- copy the .bin here
server/firmware/manifest.json               <- create this file:
```
```json
{"version":"1.0.1","url":"http://<laptop-ip>:8000/firmware/covio_firmware.ino.bin"}
```
No server restart needed — the manifest is read per-request.

### 5.4 Watch it happen
The device polls every `OTA_POLL_MS` (5 min; temporarily set it to `60000` for a faster demo — but that change itself needs one last USB flash, so decide the cadence *before* your final USB flash).

Expected serial:
```
[OTA] update offered: 1.0.1 (running 1.0.0)
... download progress ...
[OTA] OK — rebooting
=== Covio Oil Flow Meter 1.0.1 ===          <- NEW VERSION
[TOT] recovered total=<N> ...                <- data survived the update
[OTA] running a NEW image on trial — must confirm health     (see note)
[SYNC] acked_seq=...
[OTA] new image confirmed valid — rollback cancelled
```

**Gate F-OTA:** version string changed, totalizer/queue intact, records still syncing.

> **Rollback honesty note:** the trial/rollback state (`PENDING_VERIFY`) depends on the bootloader config baked into your Arduino core build; on some core versions the two `[OTA] ... trial/confirmed` lines won't appear because the state isn't enabled. The update mechanism itself works regardless, and a *failed download* always leaves the old image running. If those lines don't appear on your core, treat auto-rollback as unverified: test a deliberately broken build on the bench before trusting remote pushes (flash a v1.0.2 whose `setup()` calls `abort()`; if the device does NOT auto-revert, you know to be conservative — push updates only when someone is near the device, or move to ESP-IDF later for guaranteed rollback).

### 5.5 After the test
Delete or version-match `manifest.json` (or set its version equal to the running one) so the device stops seeing an update.

---

## PART 6 — Real remote setup (before going out of station)

The bench stub on your laptop only works on your LAN. For true remote operation the **server** must be reachable from the site's internet connection. The device only ever dials **out** — the site needs no port forwarding, no static IP, nothing inbound.

### 6.1 Put the server on the internet
Options, simplest first:
1. **A small VPS** (any provider; the cheapest instance is fine). Install Python, copy `server/`, run it behind a reverse proxy.
2. **Your existing LCS box**, if it's internet-reachable — implement the same 4 routes there (the contract in ARCHITECTURE.md is the spec).

Minimum production dressing on the VPS:
```bash
pip install flask gunicorn
gunicorn -b 127.0.0.1:8000 server:app        # instead of the dev server
# + nginx or Caddy in front, with a domain and Let's Encrypt TLS
```
Caddy is the least-effort TLS: a 2-line Caddyfile (`yourdomain.com { reverse_proxy 127.0.0.1:8000 }`) gets you automatic HTTPS certificates.

**Verify from your phone (mobile data, not WiFi):** `https://yourdomain.com/api/iot/flow/config` returns the K-factor JSON.

### 6.2 Switch the firmware to HTTPS
One code change, two files — replace the plain begin with the CA-pinned overload:
```cpp
// at top of sync.h and ota.h:
static const char ROOT_CA[] PROGMEM = R"(-----BEGIN CERTIFICATE-----
...your CA chain root (e.g. ISRG Root X1 for Let's Encrypt)...
-----END CERTIFICATE-----)";

// sync.h / ota.h — wherever http.begin(url) appears:
http.begin(url, ROOT_CA);

// ota.h doUpdate_ — replace WiFiClient with:
WiFiClientSecure client;  client.setCACert(ROOT_CA);
```
Get the root: for Let's Encrypt, embed **ISRG Root X1** (from letsencrypt.org/certificates). Do **not** ship `setInsecure()` to the field — that re-opens the exact attack OTA must resist.

### 6.3 Point the device at it (no reflash)
Serial console: `set url https://yourdomain.com` → `reboot` → confirm the device appears on the VPS dashboard. Do this **while the device is still on your bench**, then take it to site knowing it already talks to the real endpoint.

### 6.4 Your while-away workflow
| You want to | You do (from anywhere) | Device does |
|---|---|---|
| Re-calibrate K | Edit K on the dashboard | Picks up new version ≤ 60 s; server recomputes all history |
| Push new firmware | Build `.bin` → upload to `server/firmware/` → edit `manifest.json`, bump version | Pulls + self-updates within 5 min |
| Check health | Open dashboard: last-seen, records, RSSI trend | — |
| Roll back firmware | Point manifest at the previous known-good `.bin` with a NEW higher version string | Device "updates" onto the old code |

**Keep every `.bin` you ever ship.** The rollback row above only works if you still have the good binary.

### 6.5 Pre-departure checklist
- [ ] Server on VPS, HTTPS working, reachable from mobile data
- [ ] Firmware built with `SIM_PULSES 0`, HTTPS CA pinned, final `OTA_POLL_MS` decided
- [ ] Device `set url https://...` done and verified syncing on the bench
- [ ] One full OTA cycle executed against the **VPS** (not just the laptop)
- [ ] Known-good `.bin` archived
- [ ] T3 (outage) and T4 (power cut) re-run once on the final build

---

## PART 7 — Going live with the real meter

1. `config.h`: `SIM_PULSES 0`, flash (this is your last USB flash).
2. Remove the **GPIO25→GPIO27 jumper**; reconnect **opto OUT → GPIO27**.
3. Meter powered at its **confirmed supply voltage** (the 24 V question — resolve it before this step), opto input wired per your Phase 7 output-type result.
4. Induce flow: **pulses 0 at rest, climbing with flow**, records land on the dashboard, litres correct against a known volume → that known-volume run is also your first real K-factor calibration: `K = pulses_counted / litres_actually_passed`, entered on the dashboard.
