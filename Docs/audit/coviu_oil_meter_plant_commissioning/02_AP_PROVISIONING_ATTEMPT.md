# 02 — AP Provisioning Attempt (Covio-Setup-8428)

**Attempt timestamp:** 2026-07-24.

## Result: STOPPED AT PHASE 1 — the Covio setup AP is not in radio range of this laptop

Not a claim that the device is off or broken. The device's AP SSID claim
is **genuine and firmware-accurate** — but this laptop's Wi-Fi radio
cannot see it, so there is no AP to associate with from here.

## The SSID claim is real — verified against firmware

`wifi_provision.h::apSsid_()` builds the AP name as:
```cpp
return "Covio-Setup-" + hwid.substring(hwid.length() - 4);
```
This device's ID is `esp32-F4E5B2858428`; its last 4 characters are
`8428`, producing exactly **`Covio-Setup-8428`**. The broadcast name in
the mandate matches the firmware precisely — so the device genuinely
would broadcast this SSID when in AP-provisioning mode. The claim is not
doubted.

## But the AP is not visible from this laptop

Two independent fresh Wi-Fi scans this session:

```
Wi-Fi interface state: connected to "AirFiber-Balaji" @ 99% signal
Complete list of in-range SSIDs:
  - Dispatch Server_5G
  - BALAJI-WIFI-4G
  - Dispatch Server
  - DIRECT-08-HP 2606 LaserJet Tank
  - AirFiber-Balaji
Explicit search for "Covio": NOT FOUND
```

`Covio-Setup-8428` is not among the visible networks. This laptop is at a
"Balaji"/"Dispatch Server" location (consistent with the 192.168.1.41 /
BALAJI-WIFI gateway `14:c3:5e:17:7b:4c` observed in the prior
commissioning attempt) — **not within RF range of the device's setup
AP**, which is presumably at the plant.

## Why this is a hard stop, not something to work around

- I cannot instruct this laptop to associate with an access point its
  radio does not detect — there is nothing to connect to from here.
- Fabricating a captive-portal session (laptop IP `192.168.4.2`,
  browsing to `192.168.4.1`, reading back a config) would be inventing
  evidence for a connection that does not exist. Every prior phase of
  this engagement has held the line on not fabricating device readings;
  this is the same line.
- This is precisely the mandate's own first STOP CONDITION —
  "provisioning interface inaccessible."

## A second, independent blocker also remains (would stop Phase 4 even if the AP were reachable)

**No plant configuration values were supplied.** The mandate's Phase 4
says "When the owner provides: plant SSID, Wi-Fi password, production
endpoint, API token, device ID, plant ID — enter them." None were
provided in this session. Per the standing rule ("Do NOT guess endpoints
or credentials"), these cannot be invented. Even a successful AP
connection would stop here.

## Phases 2–7 — not attempted

All depend on an actual association with the setup AP (Phase 1), which
could not occur.

## Final report answers

1. **Was the ESP32 successfully provisioned?** No — the provisioning
   interface could not be reached (AP out of range).
2. **Did it join the plant Wi-Fi?** Unknown/no — nothing was
   provisioned.
3. **What IP did it receive?** None obtained — not observable.
4. **Did it reach the production server?** Not observable.
5. **Did the first live reading arrive?** No data flow observed.
6. **Ready for supervised commissioning?** Cannot be certified this
   session.

## What is required to actually run AP provisioning

1. **Physical RF proximity**: the commissioning laptop must be within
   Wi-Fi range of the device's `Covio-Setup-8428` AP (i.e. physically at
   the plant, near the installed device), so the SSID appears in a Wi-Fi
   scan and can be joined. The AP is open (no password, by design —
   `wifi_provision.h`, RF-proximity is the trust boundary), so once in
   range the association itself needs no credential.
2. **The real plant configuration values** (plant SSID + password,
   production HTTPS endpoint, per-device API token, device-ID/asset
   mapping, plant ID) — the same outstanding list documented in
   `Docs/audit/coviu_oil_meter_plant_pilot_activation/04_PLANT_CONFIGURATION_RECORD_REDACTED.md`.

With (1) in place, the captive portal at the AP's gateway IP (ESP32
SoftAP default `192.168.4.1`) can be opened, current config read, and —
with (2) — the plant values submitted through the portal's form, which
writes via the firmware's existing `setWifi`/`setServerUrl`/`setApiKey`
and reboots into station mode.

## Standing-rules compliance

- No firmware flashed, no NVS/queue/LittleFS erased, no factory reset.
- No endpoints or credentials guessed.
- No device readings fabricated; no captive-portal session invented.
- No connection to the device was possible, so nothing was changed and
  no data loss is possible.
