# 07 — Provisioning Failure: exact root cause (three cascading blockers)

**Timestamp:** 2026-07-24. **Interface:** USB serial, COM6. **Firmware on
device:** `1.0.0`, commit `443dc448...`, build `2026-07-23T19:17:22Z`.

## Executive answer

The device did NOT commission, and it **cannot** reach the production ERP
as things stand. There are **three separate blockers**, each proven with
evidence below. The first is why the portal showed an error and saved
nothing; the other two would still block it even after the first is fixed.
**No configuration was changed, nothing was erased, no reset, no reflash.**

## Steps 1–5 — Read state; verify what was saved

Serial `show` (read back after the failed provisioning):
```
device_id : esp32-F4E5B2858428
fw        : 1.0.0        build_commit=443dc448...   build=2026-07-23T19:17:22Z
server_url: http://192.168.1.3:8000          <- STILL THE OLD BENCH URL
api_key   : default (fingerprint=57c8139db729) <- STILL THE OLD BOOTSTRAP KEY
wifi_ssid : Airtel_amar_3999                  <- STILL THE OLD BENCH SSID
calib     : v1  K=1000.0  density=0.840  Tref=15.0   (preserved)
```
Boot trace (from a clean reboot):
```
[TOT] recovered total=401 writes=117796   (counters preserved)
[Q] acked_seq=58735                        (queue preserved)
[NET] connecting to Airtel_amar_3999       <- tries the OLD saved SSID
[SECURITY] server_url is http:// ... bench/dev only   <- still http://
[NET] reconnecting...                       (~10s: STA connect failing)
[PROV-AP] SoftAP started: Covio-Setup-8428  (~16s: fell back to AP mode)
```

**Verdict on step 5: the new values were NOT saved. None of them** — not
the factory Wi-Fi, not `https://data.funtastik.co.in`, not the production
token. The device still holds its old bench configuration.

## BLOCKER 1 — the portal's live Wi-Fi test failed, so it wrote nothing

The captive portal (`wifi_provision.h::handleSubmit_()`) is deliberately
sequenced:
```
validate fields  ->  testWifiCredentials_(ssid, pass)  ->  writeConfig_()  ->  reboot
                          |
                          +-- if this FAILS: show "Could not connect to
                              \"<ssid>\" -- check the WiFi network name and
                              password and try again."  AND RETURN (writes
                              NOTHING: not wifi, not url, not token)
```
Because `show` proves nothing was written, **`testWifiCredentials_()`
failed** — the ESP32 could not associate with the factory Wi-Fi using the
SSID+password entered, within the 15-second test window
(`AP_TEST_CONNECT_TIMEOUT_MS`). That failing test IS the "same error" you
saw. **This is by design** — the portal refuses to save a config it
cannot first prove the device can actually use.

**Why the Wi-Fi test failed — candidates to check ONSITE (not guessed;
I do not have the SSID/password so I cannot pick one for you):**
1. **ESP32-S3 is 2.4 GHz ONLY.** If the "Factory Wi-Fi" SSID is a 5 GHz
   band, or a band-steered combined SSID that pushed the device to 5 GHz,
   it physically cannot join. **This is the single most common cause of
   exactly this symptom** and the first thing to check — put the device
   on a 2.4 GHz SSID.
2. **Wrong password or SSID** (a typo, trailing space, or case mismatch).
3. **WPA2/WPA3-Enterprise** network (asks for a username/identity, not
   just a password). The firmware only does WPA2-PSK (`WiFi.begin(ssid,
   pass)`), so an enterprise SSID never connects.
4. **Hidden SSID** or **weak signal** where the device sits during the
   15-second test.

The firmware's Wi-Fi test code itself is correct (reviewed) — the failure
is environmental, not a firmware bug.

## BLOCKER 2 — this firmware's TLS pin is a PLACEHOLDER; HTTPS to the ERP fails closed

Even if Blocker 1 is fixed and `https://data.funtastik.co.in` is saved,
**this firmware (443dc44) cannot complete the TLS handshake to it.**

- `certs.h` ships `COVIO_CA_CERT_IS_PLACEHOLDER = 1` with a placeholder
  CA. `sync.h` pins it (`secureClient.setCACert(COVIO_PINNED_CA_CERT)`)
  for any `https://` URL. certs.h's own comment: *"With the placeholder
  left in place, an https:// connection correctly FAILS CLOSED — the TLS
  handshake is rejected... CA-pinning fails closed by design."*
- The real endpoint's certificate (checked live from the laptop):
  ```
  issuer  = C=US, O=Let's Encrypt, CN=YE1
  subject = CN=data.funtastik.co.in
  valid   = 2026-07-18 .. 2026-10-16
  ```
  It is a valid **Let's Encrypt** certificate. To trust it, the firmware's
  `certs.h` must pin **Let's Encrypt's root (ISRG Root X1)** — not the
  placeholder.

**Therefore the production HTTPS build must be flashed** — the one whose
`certs.h` contains the real Let's Encrypt CA (your referenced "e6075342").

## BLOCKER 3 — the production ERP does not expose the firmware's API contract (all paths 404)

This is the most consequential finding, and it is **independent of Wi-Fi
and TLS.** The firmware POSTs telemetry to `<server_url>/api/iot/flow/push`
(and polls `/api/iot/flow/config`, `/api/iot/flow/ota/manifest`). Probed
live against `https://data.funtastik.co.in`:
```
GET/POST /api/iot/flow/push        -> HTTP 404  (with and without trailing slash)
GET      /api/iot/flow/config      -> HTTP 404
GET      /api/iot/flow/ota/manifest-> HTTP 404
GET      /api/  (the base)         -> HTTP 404
Root  /  -> HTTP 200, <title>Balaji Foods</title>, a Django LOGIN page
```
`404` (not `401`/`403`) with an API-key header present means the paths
**do not exist** on this host — it is not an auth problem. `data.funtastik.co.in`
is the **Balaji Foods human-facing Django web app**, not an endpoint
implementing the device's IoT ingest contract. The project's own
`server/server.py` header states the rule plainly: *"Any real receiver
(LCS/Django or cloud) must expose these same routes with the same
payloads."* **This host does not.**

Implication: either (a) the device is being pointed at the **wrong tier**
— it may be meant to talk to a **Local Server (LCS)** that exposes the
`/api/iot/flow/*` contract, with `data.funtastik.co.in` being the ERP
behind it — or (b) the ERP has not yet implemented the IoT ingest routes.
Until a reachable endpoint actually serves `/api/iot/flow/push`, the
device cannot deliver data there **regardless of firmware or Wi-Fi.**

## Step 8 — Is this the production HTTPS build (e6075342)?

**No.** This is dev/bench build **`443dc44`**: `RELEASE_BUILD=0`, a
**placeholder** CA in certs.h. It contains HTTPS *code* (WiFiClientSecure)
but no usable production CA pin, and its API paths don't match the ERP.

## Step 9 — Flash the production firmware? I CANNOT, and here is exactly why

- **Commit `e6075342` does not exist in anything I can access.** Checked
  `git cat-file` and every fetched ref: not in my local history, not in
  `origin/main` (`1a880c6`), not in `origin/master` (`9874ec8`). I have
  neither the binary nor the source for it.
- I **cannot build it** either: it requires the real Let's Encrypt CA in
  `certs.h`, which — per certs.h's own rule and ADR-005 — is operational
  key material that must not be authored/guessed in a coding session.
- Even if I flashed it, **Blocker 3 (API 404) would remain** unless that
  build also retargets the ERP's real telemetry endpoint — unverifiable
  without the artifact.

Flashing a firmware I do not have, or fabricating a CA, is not something
I will do. This is a hard external blocker.

## Step 10 — Exact technical reason the device cannot connect to the ERP

All three of the following must be resolved; none is resolvable from this
session with the information/artifacts available:

1. **Wi-Fi:** the ESP32 (2.4 GHz only) could not join the factory Wi-Fi
   with the entered credentials → provisioning saved nothing. **Action:**
   put the device on a **2.4 GHz** WPA2-PSK SSID with correct credentials
   (rule out 5 GHz / enterprise / typo), then re-submit the portal.
2. **TLS:** this build pins a placeholder CA; `data.funtastik.co.in` uses
   Let's Encrypt. **Action:** flash the production build whose `certs.h`
   pins **ISRG Root X1 (Let's Encrypt)** — the "e6075342" firmware, which
   must be **provided** (I do not have it).
3. **API contract:** `data.funtastik.co.in` returns **404** for every
   `/api/iot/flow/*` path the firmware uses. **Action:** confirm the
   correct telemetry-ingest endpoint (likely an **LCS** exposing the
   `/api/iot/flow/*` contract, or the ERP must implement those routes),
   and provision THAT URL — not the Django web-app root.

## What was preserved / not touched

- No NVS erase, no factory reset, no reflash, no config write.
- Calibration (`K=1000.0`), totalizer (`401`), and queue (`acked_seq=58735`)
  all confirmed intact in the boot log.
- Device is currently in AP-fallback mode (`Covio-Setup-8428`) holding its
  prior config — safe, no data loss.

## Bottom line

**LIVE COMMISSIONING — CANNOT COMPLETE from here.** The exact reasons are
the three blockers above. To proceed, the onsite team must supply: a
working **2.4 GHz** factory Wi-Fi, the **production firmware artifact**
(Let's-Encrypt-pinned, "e6075342"), and the **correct IoT ingest endpoint**
that actually serves `/api/iot/flow/push` (the current ERP URL 404s on it).
