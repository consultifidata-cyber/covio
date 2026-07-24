# 04 — Plant Configuration Record (Part 5) — STOPPED AT THE HUMAN-INPUT BOUNDARY

Per this mandate's own explicit instruction: **"If the real plant
endpoint, credentials, device ID, or plant ID are not available, stop at
the exact human-input boundary and list what is required."** None of the
required real values are available to this session. No value below was
guessed, invented, or assumed. Nothing was reprovisioned.

## Exactly what is required before this step can proceed

```
[ ] Plant server URL          -- e.g. https://<real-host>:<port>
                                  MUST be https:// for a genuine
                                  production endpoint (the pinned-CA
                                  path already exists in this firmware's
                                  code and should actually be exercised,
                                  not left at http://)
[ ] Plant API credential       -- obtained via POST /admin/devices/provision
                                  against the REAL plant/production
                                  server (never the bench stub), which
                                  mints a unique per-device key
[ ] Device ID mapping          -- this device's hardware ID
                                  (esp32-F4E5B2858428) as it should be
                                  registered against the plant's asset
                                  records
[ ] Plant ID                   -- no field for this exists in the
                                  current data model at all (confirmed,
                                  enterprise re-audit doc 07); if the
                                  business requires per-plant
                                  attribution, this is a data-model gap
                                  to resolve BEFORE reprovisioning, not
                                  something reprovisioning alone can fix
[ ] Wi-Fi SSID (plant network) -- real value, not this bench network's
[ ] Wi-Fi password (plant network) -- real value
[ ] TLS requirement            -- confirm whether the plant endpoint is
                                  reachable over https:// with a
                                  certificate this device's pinned-CA
                                  logic can validate (certs.h currently
                                  ships only a placeholder CA)
[ ] Expected server certificate/hostname -- needed to actually populate
                                  certs.h's pinned CA correctly (a
                                  separate, already-known-open item --
                                  this is exactly why `pio run -e release`
                                  still fails closed)
[ ] Approved K-factor           -- the real meter's calibration value,
                                  from the business/metrology owner, not
                                  the server's current seeded default
                                  (1000.0, confirmed live and via
                                  server.py's own init_db() seed)
[ ] Sync interval               -- confirm whether the current
                                  compiled-in PUSH_PERIOD_MS(5s)/
                                  CONFIG_POLL_MS(60s)/OTA_POLL_MS(5min)
                                  are acceptable for the plant, or need
                                  a firmware change (compile-time only,
                                  doc 08 of the enterprise re-audit)
[ ] Sampling interval           -- confirm whether TELEMETRY_PERIOD_MS
                                  (1s, compile-time) is acceptable given
                                  the ~1.14-day offline-capacity ceiling
                                  this implies (enterprise re-audit doc 03)
[ ] Timezone                    -- confirm whether the business needs
                                  wall-clock-timezone-aware reporting;
                                  currently no timezone concept exists
                                  anywhere in this system at all
```

## What this session did NOT do, and will not do without the above

- Did not invent a plant URL, credential, device ID, or plant ID.
- Did not touch the live device's `server_url`/`api_key`/Wi-Fi.
- Did not change the K-factor.
- Did not change sync/sampling intervals (would require a firmware
  rebuild regardless — doc 08 of the enterprise re-audit already
  established these are compile-time-only).

## The mechanism that WILL apply these values, once available (unchanged, already verified this whole audit trail)

```
1. Physical serial console (USB) or AP-mode:
   set url https://<real-plant-endpoint>
   set key <the-real-per-device-key>
   set wifi <plant-ssid> <plant-password>
   reboot
2. K-factor: POST /admin/kfactor against the REAL plant server (device
   picks it up automatically within CONFIG_POLL_MS=60s, no reflash).
3. Read-back verification (redacted): /api/v1/status's server_url
   (safe to display, contains no credential), api_key_status (redacted
   enum only), wifi.connected (boolean only).
```

## Verdicts affected by this stop

`PLANT ENDPOINT: NOT VERIFIED`
`PLANT SERVER REGISTRATION: NOT VERIFIED`

These carry directly into `19...` — wait, this audit's own final verdict
document (`01_PILOT_ACTIVATION_VERDICT.md`) in this same directory.
