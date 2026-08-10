# 09 — Live Hotspot End-to-End Test Result

**Timestamp:** 2026-07-24. **Device:** esp32-F4E5B2858428, fw commit
`e5a593b` (real Let's Encrypt CA). **Hotspot:** Amar's A34 (Ch 11, 2.4 GHz).

## VERDICT: ❌ B — Exact failing step + root cause

The physical ESP32 **connected to the mobile hotspot and reached the
production server over HTTPS successfully** — but it is **not** sending
live data, because the ERP's telemetry API paths have changed and no
longer match the firmware. Every layer works up to the HTTP application
layer; the failure is a **server-side API contract change (externally
owned)**.

## What works — proven live (serial evidence)

```
[NET] connecting to Amar's A34
[LOCALAPI] HTTP server started on :80        <- only runs AFTER Wi-Fi joins
[LOCALAPI] mDNS started: covio-858428.local  <- confirms association + IP
[SYNC] push HTTP 404 -- keeping queue
```
| Stage | Result |
|---|---|
| Wi-Fi association to Amar's A34 | ✅ connected (no "reconnecting"; LOCALAPI + mDNS started) |
| DHCP IP obtained | ✅ (services came up) |
| DNS resolution of data.funtastik.co.in | ✅ (resolved after first attempt) |
| **TLS handshake (pinned Let's Encrypt CA)** | ✅ **the CA fix works** — an HTTP 404 is an application-layer reply, which is only possible if the TLS handshake completed with no cert error |
| Reaches data.funtastik.co.in over HTTPS | ✅ |
| **HTTP POST /api/iot/flow/push** | ❌ **HTTP 404** (repeating every ~5 s) |

Device holds the correct config: `server_url=https://data.funtastik.co.in`,
`api_key` fingerprint `18519bce8d2a` (**verified = SHA-256 of the exact
production key**), `wifi_ssid=Amar's A34`. Queue is retained on 404 (no
data loss).

## The exact failing step

```
POST https://data.funtastik.co.in/api/iot/flow/push   ->  HTTP 404
GET  https://data.funtastik.co.in/api/iot/flow/config  ->  HTTP 404
```

## Precise root cause: the ERP's telemetry API was refactored; firmware paths no longer exist

The Django 404 debug page lists the URL patterns the ERP **currently**
serves. The firmware's endpoints are **gone**; new ones took their place:

```
Firmware uses (hardcoded, config.h):        ERP current endpoint map (probed live):
  POST /api/iot/flow/push                      GET  /api/iot/flow/push        -> 404  (GONE)
  GET  /api/iot/flow/config                    GET  /api/iot/flow/config      -> 404  (GONE)
  GET  /api/iot/flow/ota/manifest              GET  /api/device/push/         -> 405  (exists, POST-only)  <- NEW
                                               GET  /api/device/heartbeat/    -> 405  (exists, POST-only)  <- NEW
                                               GET  /api/iot-devices/         -> 401  (exists, needs auth)
                                               POST /api/device/push/         -> 401  (exists; rejects X-Api-Key)
                                               POST /api/device/heartbeat/    -> 401  (exists; rejects X-Api-Key)
```

**Two independent changes on the server side:**
1. **Paths moved:** `/api/iot/flow/{push,config}` → `/api/device/{push,heartbeat}/`.
   The firmware POSTs to the old path → **404**.
2. **Auth changed:** the new `/api/device/*` endpoints return **401** to
   the firmware's `X-Api-Key: <token>` header — they expect a different
   authentication scheme/credential than the firmware sends.

**Timeline proof this is a recent server change (not a firmware defect):**
~1 hour ago (doc 08), `GET /api/iot/flow/config` → **200**
(`{"K_factor":1.0,...}`) and `POST /api/iot/flow/push` → **200** with this
same key. Those exact requests now return **404**. The firmware did not
change between those tests; the ERP did.

## Split-DNS note (observed, not the blocker)
On the plant Wi-Fi, `data.funtastik.co.in` resolves to `192.168.31.1`
(local) **and** `187.127.132.243` (public); on the hotspot, only the
public IP. Both currently serve the same refactored Django app
(`erp_project.urls`) that 404s the firmware's paths — so the path
mismatch is the blocker on **both** networks, independent of DNS.

## This blocker is externally owned — precise corrective actions (I did NOT do either, per the rules)

Pick one; both are outside "commission this ESP32" and one is explicitly
forbidden here ("do not modify the ERP", "do not rebuild firmware"):

**Option 1 — ERP side (server team):** re-expose the firmware's contract,
i.e. serve `POST /api/iot/flow/push` and `GET /api/iot/flow/config`
accepting the `X-Api-Key` header — the same routes that returned 200 an
hour ago. Simplest if the change was unintentional.

**Option 2 — Firmware side (needs a new build + the new auth spec):**
update `config.h`'s `PATH_PUSH`/`PATH_CONFIG` to `/api/device/push/` and
the heartbeat/config equivalents, and change `sync.h`'s auth to whatever
the new endpoints require (they 401 on `X-Api-Key`, so the server team
must specify the expected scheme — e.g. `Authorization: Bearer`, a
different header, or a device-registration step). Cannot be done blind;
the new auth contract must be provided.

## Device left in a good state
Online on Amar's A34, correct URL/key/CA, queue intact (no data loss),
retrying every 5 s. **The moment the ERP path/auth matches the firmware
(Option 1), this device starts delivering data with zero further
action** — everything else is already proven working.

## Rules compliance
- No ERP modified, no firmware rebuilt/flashed this step, no NVS erase,
  no factory reset. Calibration/totalizer/queue preserved. Wi-Fi password
  never printed.
