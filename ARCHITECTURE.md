# Covio Oil Flow Meter — System Architecture

**Scope:** ESP32 flow-metering device (flow-only build), its firmware, the communication contract, and the receiver. This document is the reference for anyone implementing a compatible receiver (LCS, Covio cloud) or extending the device family.

---

## 1. System overview

```
 FIELD SITE                                        ANYWHERE (LAN or internet)
┌─────────────────────────────────────────┐      ┌──────────────────────────────┐
│  Oval-gear meter (12/24V pulse output)  │      │  Receiver (interchangeable): │
│        │ G (pulse)                      │      │   • bench stub (Flask)       │
│        ▼                                │      │   • LCS (Django)             │
│  PC817 opto  ──►  GPIO27                │      │   • Covio cloud              │
│  (12/24V domain    │                    │      │                              │
│   isolated from    ▼                    │ HTTP │  POST /api/iot/flow/push     │
│   3.3V logic)   ESP32 ◄── SD card       │─────►│  GET  /api/iot/flow/config   │
│                 (PCNT    (queue +       │◄─────│  GET  /api/iot/flow/ota/...  │
│                  counter) totalizer)    │ JSON │  GET  /firmware/<bin>        │
│                    │                    │      │                              │
│                 WiFi (outbound only)    │      │  Owns: K-factor, litres,     │
└─────────────────────────────────────────┘      │  dedup, dashboards, ERP sync │
                                                 └──────────────────────────────┘
```

The device dials **out** only. The site needs no inbound ports, no static IP, no VPN.

---

## 2. Design principles (the "why" behind everything)

1. **Upstream-agnostic device.** Firmware knows one thing: `server_url` from NVS. LCS vs cloud vs stub is a provisioning decision, never a code path. (Origin: your LCS architecture doc — "Do NOT build an LCS-specific device.")
2. **Raw pulses are the source of truth.** The device never converts to litres. The server computes `litres = Δpulses / K_factor`. Consequence: editing K on the website re-derives *all* history; calibration requires no device visit.
3. **Durability before transmission.** A record is written to SD *before* any network attempt. Network failure can delay data; it cannot lose it.
4. **Explicit acknowledgement.** Only a parsed `ack_seq` in a JSON body prunes the queue. HTTP 200 alone is transient noise.
5. **Idempotent receiver.** Retries and duplicates are expected behavior, absorbed by a uniqueness key — never double-counted.
6. **Pull, don't push, for firmware.** The device fetches updates from a manifest; nothing ever needs to reach *into* the site network.
7. **Hardware counts, software ships.** Pulse counting lives in the PCNT peripheral; WiFi/HTTP stalls cannot drop pulses (proven in your Phase 6).

---

## 3. Firmware module map

```
covio_firmware.ino     orchestration: boot sequence + non-blocking loop
├── config.h           build-time defaults & pins (only per-deployment edits)
├── store.h            NVS globals: url, key, wifi, cfg_ver, boot_id, cached K
├── provision.h        serial console: show / set url|key|wifi / reboot / factory
├── totalizer.h        PCNT hardware counter + dual-slot CRC checkpoint on SD
├── queue.h            append-only SD event log + durable acked-through pointer
├── telemetry.h        record struct -> push JSON (the wire contract, device side)
├── sync.h             WiFi keepalive (backoff+jitter), push/ACK, config poll
└── ota.h              manifest poll, HTTPUpdate download, health-confirm/rollback
```

The reusable "Covio Device Sync Library" = `store + queue + telemetry + sync + ota`. A motor monitor or weighing machine swaps `totalizer.h` for its own sensor module and reuses the rest unchanged.

---

## 4. Data flows

### 4.1 Measurement path (continuous)
```
meter G ──► opto ──► GPIO27 ──► PCNT (hardware, glitch-filtered)
                                  │ drained before 16-bit wrap
                                  ▼
                     64-bit lifetime totalizer (RAM)
                                  │ every 1 s
                                  ▼
                     checkpoint {total, seq, boot_id, CRC32}
                     written alternately to /totA.bin, /totB.bin
```
Recovery = newest CRC-valid slot. A torn write corrupts at most one slot and fails CRC; worst-case loss on a hard power cut ≈ the pulses since the last 1 s checkpoint.

### 4.2 Telemetry & sync path (the critical ordering)
```
every 1 s:                             every 5 s:
  seq++                                  batch = up to 50 un-acked rows
  row = {boot_id, seq, ts,               POST /api/iot/flow/push
         totalizer, quality, rssi}       ◄── {ack_seq, server_time_ms}
  SD queue.append(row)   ── 1st          if ack_seq parsed:
  checkpoint(total,seq)  ── 2nd              prune queue through ack_seq
```
**Why append-before-checkpoint:** if power dies between the two, the same seq is regenerated next boot and the duplicate is absorbed server-side. The reverse order could consume a seq with no row behind it — a permanent gap that freezes the cumulative ACK forever.

Sequence diagram, including an outage:
```
DEVICE                                   SERVER
  │ append seq 101..105 to SD               │
  │ POST push [101..105] ────────────────►  │ upsert; contiguous-max = 105
  │ ◄──────────────────── {ack_seq:105}     │
  │ prune ≤105                              │
  │            (WiFi dies)                  │
  │ append 106..160 (queued, retried)       │
  │            (WiFi returns)               │
  │ POST push [106..155] ───────────────►   │ upsert; contiguous-max = 155
  │ ◄──────────────────── {ack_seq:155}     │
  │ POST push [156..160] ───────────────►   │ → 160 ... queue drained
```

### 4.3 Configuration path (calibration)
```
every 60 s: GET /config ──► {K_factor, density, T_ref, version}
  if version != cached: store K locally (display only), stamp future
  records with new kfactor_version
```
Authoritative litre math happens server-side at read time: `(max(totalizer)-min(totalizer))/K`. Editing K re-prices all stored pulses instantly.

### 4.4 OTA path
```
every 5 min: GET /ota/manifest ──► {"version":"1.0.1","url":".../fw.bin"}
  version == FW_VERSION → sleep.
  else: HTTPUpdate downloads .bin into the INACTIVE app partition → reboot
        new image runs "on trial" → after first successful server contact,
        confirmHealthyBoot() marks it valid (cancels rollback)
        crash before that → bootloader reverts to previous partition*
```
\* Trial/rollback marking requires `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` in the core build; see the honesty note in the testing guide. A failed *download* always leaves the old image untouched — the dual-partition design guarantees that much unconditionally.

---

## 5. Identity, sequencing & the ACK model

| Field | Source | Property |
|---|---|---|
| `device_id` | eFuse MAC | globally unique, stable for the board's life |
| `boot_id` | NVS counter, +1 per power-up | diagnostic: which power-cycle produced a record |
| `seq` | checkpoint, continues across boots | **globally monotonic per device** — never resets |

**Dedup key: `(device_id, seq)`.** Because seq never resets, this alone identifies a record. `boot_id` is carried as data (it tells you *when* power-cycled), not as part of identity.

> Deviation from the original LCS doc: that doc proposed `UNIQUE(device_id, boot_id, seq)`, which implicitly assumes seq restarts at 1 each boot. This implementation keeps seq continuous instead (simpler recovery: one number in the checkpoint), so the faithful uniqueness key is `(device_id, seq)`. A receiver implementing this contract must use the latter. This exact mismatch caused a freeze-after-first-reboot ACK bug in an earlier stub revision; the scenario is now a permanent regression test.

**Cumulative ACK:** the server returns the highest **contiguous** seq it holds for the device across all boots. A hole (missing seq) pins the ack below it until the retry fills it — which is precisely what forces retransmission and guarantees ordered, gapless ingestion. The device prunes its queue through `ack_seq` and no further.

---

## 6. Storage layout

### NVS (namespace `covio`) — globals only, never per-event state
| Key | Meaning |
|---|---|
| `server_url`, `api_key` | the endpoint — the ONLY deployment-specific coupling |
| `wifi_ssid`, `wifi_pass` | network credentials |
| `boot_id` | power-cycle counter |
| `cfg_ver`, `kfactor`, `density`, `tref` | cached calibration (display use) |
| `seeded` | first-boot guard: defaults seed once; console edits thereafter |

### SD card
| Path | Content | Protection |
|---|---|---|
| `/totA.bin`, `/totB.bin` | totalizer+seq checkpoint | fixed-size, CRC32, ping-pong slots, `writes` counter picks newest |
| `/queue/log.bin` | append-only telemetry rows | per-row magic+CRC32; torn rows skipped on read |
| `/queue/ackA.bin`, `ackB.bin` | acked-through pointer | same dual-slot CRC scheme |

Compaction: the log file is deleted only when fully drained (everything acked) — a safe point requiring no partial rewrite.

---

## 7. API contract (the shared spec — any receiver must match)

All requests carry header `X-Api-Key: <key>`.

### POST `/api/iot/flow/push`
```json
{"device_id":"esp32-ABCD12345678","fw":"1.0.0","model":"covio-oilflow-v1",
 "kfactor_version":2,
 "records":[
   {"boot_id":3,"seq":1041,"ts":862,"totalizer":528440,"quality":0,"rssi":-61}
 ]}
```
Response `200`:
```json
{"ack_seq":1041,"server_time_ms":1767423998123}
```
Rules: upsert on `(device_id, seq)`; `ack_seq` = highest contiguous seq stored; `quality` is a bitfield (`0x0001` backlog-high, `0x0002` time-unsynced); `ts` is device uptime seconds — the server stamps wall-clock at receipt (`recv_ms`).

### GET `/api/iot/flow/config`
```json
{"K_factor":500.0,"density":0.84,"T_ref":15.0,"version":2}
```

### GET `/api/iot/flow/ota/manifest`
```json
{"version":"1.0.1","url":"https://host/firmware/covio_firmware.ino.bin"}
```
Empty `version`/`url` = "no update". Version is compared for **inequality** with the running `FW_VERSION` (so pointing at an older build with a new version string is a valid rollback mechanism).

### GET `/firmware/<file>` — serves the binary (HTTPS in the field).

---

## 8. Failure-mode matrix

| Failure | What happens | Data outcome |
|---|---|---|
| WiFi outage (minutes–days) | records queue on SD; backoff+jitter reconnect | zero loss; drained in order on recovery |
| Server down / 5xx / timeouts | push fails, queue retained, retried each cycle | zero loss |
| HTTP 200 but garbled/empty body | not treated as ack; queue retained | zero loss (Invariant: explicit ack only) |
| ACK response lost in transit | rows re-sent next cycle; server upsert ignores | no duplicates (idempotent key) |
| Power cut mid-checkpoint | one slot torn → CRC fails → other slot used | ≤ ~1 s of pulses lost; totalizer valid |
| Power cut between row-append and checkpoint | seq regenerated next boot; duplicate row sent | absorbed server-side; no gap, no freeze |
| Torn queue row on SD | magic/CRC check skips it | that 1 s sample lost, stream continues |
| OTA download fails/interrupted | inactive partition abandoned; old image keeps running | no impact |
| OTA image boots but crashes | bootloader rollback (if enabled in core; see §4.4) | device recovers on previous firmware |
| SD card absent/dead at boot | fail-loud halt; provisioning console stays alive | intentional: no silent unpersisted operation |
| Queue backlog beyond highwater | `quality` flag set on records | server can alert; nothing dropped |

---

## 9. Timing & cadence

| Action | Period | Constant |
|---|---|---|
| Telemetry record + checkpoint | 1 s | `TELEMETRY_PERIOD_MS` |
| Push attempt (batch ≤ 50) | 5 s | `PUSH_PERIOD_MS` |
| Config (K-factor) poll | 60 s | `CONFIG_POLL_MS` |
| OTA manifest poll | 5 min | `OTA_POLL_MS` |
| WiFi reconnect backoff | 10 s → ×2 → cap 2 min, +jitter | `WIFI_RETRY_MS` |

Steady-state throughput: 86,400 records/day/device ≈ 3–4 MB/day on SD before compaction; a 16 GB card holds years of un-acked backlog.

---

## 10. Security model

**Now (bench):** plain HTTP, static API key. Acceptable only on a trusted LAN.

**Field checklist (in order of importance):**
1. **HTTPS with pinned CA** for *all four* endpoints — above all the OTA manifest+binary, because pull-OTA executes what it downloads. `http.begin(url, ROOT_CA)` / `WiFiClientSecure::setCACert`. Never `setInsecure()`.
2. **Per-device API keys** on the server, revocable individually.
3. **Signed firmware / Secure Boot** so even a compromised server cannot execute arbitrary code on devices (ESP-IDF signing; a later hardening step).
4. Keys live in NVS, never in the repo; `config.h` defaults are bench placeholders.

Threats accepted at this stage: physical access to the device (serial console is open — it's also your recovery path), and NVS is unencrypted (flash-encryption is a later step alongside secure boot).

---

## 11. Deployment modes & provisioning

| Mode | server_url points at | Notes |
|---|---|---|
| Bench | `http://<laptop>:8000` (Flask stub) | full contract incl. OTA hosting |
| Factory + LCS | LCS host implementing the 4 routes | LCS then relays to ERP via the **existing** LCS→Live sync engine (unchanged) |
| Cloud SaaS | Covio cloud, same routes | same firmware, different URL |

Provisioning = the serial console (`set url`, `set key`, `set wifi`, `factory`), or first-boot seeding from `config.h`. **Reflashing does not change a seeded device's NVS** — this is deliberate (an OTA update must never clobber site provisioning).

## 12. Extension points

- **Temperature returns:** re-add MAX31865 on the shared SPI bus (CS GPIO5 is reserved free), extend `QRow` with `temp_c`, bump a payload `schema` field; server applies density(T) correction against `T_ref`. Storage/sync layers unchanged.
- **New device types** (motor runtime, weighing): replace `totalizer.h` with the sensor module; reuse store/queue/telemetry/sync/ota as the standard Covio device library; add a parallel route family (`/api/iot/<type>/push`) on the receiver.
- **Time sync:** add SNTP; until synced, records carry `quality|=TIME_UNSYNCED` and the server's `recv_ms` remains the authoritative clock.
