# Covio Oil Flow Meter — Firmware + Server (flow-only build)

End-to-end: ESP32 counts pulses → persists a crash-safe totalizer → queues telemetry on SD → syncs to a **configured API endpoint** it knows nothing about → supports **pull-based OTA** with rollback. Litres are computed **server-side from raw pulses**, so the K-factor is calibrated from the website with no reflash.

## File map

```
covio_firmware/
├── covio_firmware.ino     main loop + boot sequence
├── config.h               <-- THE ONLY FILE YOU EDIT per deployment
├── store.h                NVS globals (url, key, wifi, versions, boot_id)
├── totalizer.h            PCNT counter + dual-slot CRC checkpoint on SD
├── queue.h                append-only SD queue + acked-through pointer
├── telemetry.h            record builder + push JSON (raw-pulses model)
├── sync.h                 wifi + push/ACK + K-factor config poll
├── provision.h            serial console (set url/key/wifi, factory reset)
├── ota.h                  pull OTA + rollback (HTTPS/signing notes inside)
├── platformio.ini         PlatformIO build config (points at this root layout)
├── IMPLEMENTATION_AND_TESTING.md   step-by-step bring-up, tests, remote setup
├── ARCHITECTURE.md        full design reference + API contract
└── server/
    └── server.py          Flask+SQLite stub: push/ACK/config/OTA + K-factor UI
```

Per ADR-013, this root-level flat layout is the single source of truth — there
is no separate `src/` copy to keep in sync. Arduino IDE compiles `.h` files
sitting next to the `.ino` directly, with nothing to flatten first. PlatformIO
also builds directly against this same root layout (see `platformio.ini`).

## Build & flash (one time, over USB)

1. **Board:** ESP32 Dev Module.
2. **Partition Scheme:** an OTA-capable one — **"Minimal SPIFFS (1.9MB APP with OTA)"**. Without two app slots, OTA cannot work.
3. Edit `config.h`: set `DEFAULT_WIFI_SSID/PASS`, and `DEFAULT_SERVER_URL` to your server host (e.g. `http://192.168.1.100:8000`).
4. Flash on USB (buck off VIN — your Rule R2). After this first flash, all future updates go over WiFi.

## Run the server

```bash
cd server
pip install flask
python server.py           # http://0.0.0.0:8000
```

Open `http://<host>:8000/` → the admin page shows each device's raw pulses, computed litres, and a **K-factor form**. Editing K bumps the version and recomputes all consumption from stored raw pulses. The device picks up the new version on its next config poll and stamps it on future records.

## The calculation model (why raw pulses)

- Device sends **cumulative raw pulses** (`totalizer`), never litres.
- Server: `litres = (totalizer_now − totalizer_prev) / K_factor`.
- Re-calibrating = editing K on the website. History recomputes; no device visit, no reflash, no lost data.
- Each record carries `kfactor_version` so the server knows which K applied over time.

## Upstream-agnostic by design

The firmware only ever calls `store.serverUrl() + path`. It has **no concept** of LCS vs cloud. To re-point a device: change `server_url` / `api_key` in NVS — never reflash. Any real receiver (LCS/Django or cloud) just implements the same four routes with the same payloads.

## Reliability contract (enforced in code)

- **ACK-gated prune:** the SD queue advances its acked pointer **only** on a parsed `ack_seq`. A bare HTTP 200 prunes nothing → retried. No silent loss.
- **Idempotent:** server `UNIQUE(device_id, seq)` (seq is globally monotonic across reboots); a lost ACK never double-counts (verified in the stub's self-test).
- **Cumulative ack_seq:** server returns the highest **contiguous** seq; a gap holds the ack until filled (verified).
- **Torn-write safe:** totalizer + ack pointer use fixed-size CRC32 records in two ping-pong slots; a corrupt slot fails CRC and is skipped, never parsed as garbage.
- **seq is globally monotonic** (continues across reboots via the checkpoint); boot_id is diagnostic data.
- **Serial provisioning console:** type `help` in Serial Monitor — `set url/key/wifi`, `factory`. NVS defaults seed once; the console is how you change them after.
- **Sim mode:** `SIM_PULSES 1` in config.h emits ~10 Hz test pulses on GPIO25 (jumper to GPIO27) for meter-free bench testing.
- **NVS = globals only.** Per-event durability is the SD queue's job.

## OTA — read before field deploy

Pull OTA downloads and **executes** whatever the manifest points at. Bench code uses plain HTTP so the stub works. **Before site:**

1. Serve the manifest **and** the `.bin` over **HTTPS with a valid cert**; pin the CA (`setCACert`), not `setInsecure()`. TODO markers in `ota.h` show exactly where.
2. Enable **Secure Boot + signed images** so a tampered binary is rejected even if the URL is compromised.
3. Rollback is already wired: a freshly-OTA'd image is on trial until the device proves it can reach the server (`confirmHealthyBoot()`); if the new image crashes first, the bootloader reverts to the last good partition.

To push an update: build the `.bin` (Sketch → Export Compiled Binary), host it, set the manifest `version` **higher than** the running `FW_VERSION`, point `url` at the `.bin`. Keeping manifest version == device version means "no update".

## Not in this build (deferred, as agreed)

MAX31865 temperature (removed), enclosure/sealing, on-site K calibration, multi-day soak, full power protection, OTA image signing. These are the post-handover hardening tail.
