# 03 — Why did "Covio-Setup-8428" disappear? Firmware root-cause diagnosis

**Timestamp:** 2026-07-24.

## Bottom line (most likely, and it is BENIGN)

**The AP most likely disappeared because the device connected to a Wi-Fi
network and shut its own setup-AP down — which is exactly what the
firmware is designed to do.** The setup AP is a *fallback state*, not a
persistent mode; it stops the instant a station (Wi-Fi client) connection
is confirmed. The firmware is not faulty — this is documented, intended
behavior. Below is the exact code path, traced (not assumed), plus the
less-likely alternatives and the safe onsite recovery action.

I could not observe the device directly this session (it is not on USB —
zero serial ports — and not on any network this laptop is currently
reachable on; the laptop itself has bounced across `192.168.1.41` →
`10.120.222.217` during this session and is not on the plant LAN). So
this diagnosis is grounded in the firmware's actual control flow, which
is the authoritative source for the "intentional disable / timeout"
questions the mandate asks.

## The exact AP lifecycle, traced from source

### When the AP turns ON (`covio_firmware.ino::setup()`)
```
1. syncEngine.wifiConnect()  -- starts a station connect to the SAVED creds
2. Bounded wait loop: up to AP_FALLBACK_TIMEOUT_MS = 15000 ms (15 seconds)
     while (!forceAp && WiFi.status() != WL_CONNECTED && within 15s) { ... }
3. After the loop:
     if (!forceAp && WiFi.status() == WL_CONNECTED)  -> localApi.begin(...)   // STATION MODE, AP NEVER STARTS
     else                                            -> wifiProvision.begin() // AP STARTS
```
**The AP only ever starts if the device FAILS to join a saved Wi-Fi
within 15 seconds at boot (or the `provision` console command forced
it).** For the first ~15 seconds after ANY reset, the AP is not yet
broadcasting at all — the code is still in the station-attempt wait loop.

### When the AP turns OFF (`wifi_provision.h::service()` → `stop_()`)
```
begin(): WiFi.mode(WIFI_AP_STA)   // AP up, but STA ALSO keeps retrying saved creds in the background
service() (every loop):
     if (WiFi.status() == WL_CONNECTED) { stop_(); return true; }
stop_(): WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);  // AP torn down
```
**Because AP mode runs as `WIFI_AP_STA`, the background station keeps
trying to connect. The moment it succeeds, `service()` calls `stop_()`
and the AP vanishes — by design.** The captive portal's own success page
even says: *"This setup network will disappear shortly."*

## Ranked explanations for THIS disappearance

### 1. (Most likely) Provisioning already succeeded → AP intentionally shut down
The captive-portal submit path (`handleSubmit_()`) does: validate → live-
test the Wi-Fi creds → `writeConfig_()` → **`ESP.restart()`**. On the
reboot, the device connects to the newly-saved network inside the 15s
window and takes the `localApi.begin` (station) branch — **the AP is
never restarted.** This matches the reported sequence exactly ("was
visible → now gone"). It is the leading explanation because the device's
*old* saved credentials from every prior phase were `Airtel_amar_3999`,
which is not present at this plant location — so for the device to have
connected to Wi-Fi and dropped its AP, it must have been given **new
credentials via the portal.** In other words: **someone (or you) likely
completed the provisioning, and the disappearing AP is the success
signal, not a failure.**

### 2. Device reset and is inside its 15-second boot window
If the device power-cycled or reset, there is a ~15-second window where
neither the AP nor a station link is up (the code is still in the boot-
time station-attempt loop). A rescan during that exact window shows no
AP. If it then connects to a saved network, the AP never reappears (same
as #1); if it fails, the AP comes back after ~15s.

### 3. (Unlikely) Reboot loop
A crash loop would make the AP appear only intermittently. Nothing in
this device's history supports this — it has been stable across dozens of
reboots this entire engagement, and `CONFIG_ESP_TASK_WDT_PANIC` +
brownout detection are enabled (would surface a crash as a reset reason,
not a silent AP loss).

### Ruled OUT by code inspection:
- **Hidden AP**: `WiFi.softAP(ssid.c_str())` uses the open, *broadcast*
  form — no `ssid_hidden` argument is passed, so the SSID is never
  hidden.
- **Radio failed / Wi-Fi init failed**: the AP was visible earlier, so
  the radio demonstrably works; `WiFi.softAP()` failure is also logged
  loudly (`[PROV-AP] WARNING...`) rather than failing silently.
- **AP hard timeout that turns it off after N minutes**: there is **no
  such timer in the code.** The AP does not self-expire on a clock — it
  only stops on a confirmed station connection (`service()` → `stop_()`).
  So "it timed out on its own after sitting idle" is NOT a possible
  explanation for this firmware; if the AP is gone, the station side
  connected.

## Direct answers to the mandate's questions

1. **Why did Covio-Setup-8428 disappear?** Almost certainly because the
   device's station side connected to a Wi-Fi network, triggering the
   firmware's by-design AP shutdown (`service()` → `stop_()`). Given the
   old saved SSID isn't at this location, that most plausibly means **new
   Wi-Fi credentials were successfully entered via the captive portal
   (provisioning succeeded)**. The alternative is a reset placing it in
   its 15-second boot window. It is NOT a self-timeout (no such timer
   exists) and NOT a hidden SSID.
2. **Is the ESP32 alive?** Cannot be confirmed directly from here (no USB,
   not on a reachable network), but nothing indicates it is dead — the
   disappearance pattern matches normal post-provisioning AP shutdown,
   not a failure. The device MAC (`28:84:85:b2:e5:f4`) was not found on
   the laptop's current subnets, but the laptop is not on the plant LAN,
   so that is expected and is not evidence of death.
3. **Is it connected to another Wi-Fi?** Most likely **yes** — that is
   the leading explanation for the AP shutdown. Unconfirmed from here
   only because this laptop is not on the same network.
4. **Is the AP intentionally disabled?** **Yes, if the station
   connected** — the firmware intentionally and by design stops the AP
   the moment Wi-Fi is confirmed. This is correct behavior, not a fault.
5. **What action should the onsite engineer take to make it reachable
   again, without data loss or reflashing?** See below.

## Safe onsite recovery — no reflash, no NVS/queue/config erase

**Step 1 — First check whether it already provisioned successfully
(the happy path):** On the plant Wi-Fi router, open the DHCP client /
connected-devices list and look for MAC `28:84:85:b2:e5:f4` (or hostname
`covio-858428`). If it's there, **the device provisioned successfully —
no recovery is needed.** Connect the laptop to that same plant network
and reach the device at its DHCP IP or `http://covio-858428.local/`
(its mDNS name), then run the normal live-verification (`/api/v1/info`,
`/api/v1/status`).

**Step 2 — If it is NOT on any Wi-Fi and you need the setup AP back, use
the single cleanest non-destructive trigger: power-cycle the device and
wait ~25-30 seconds, then rescan for `Covio-Setup-8428`.**
- If the AP **reappears** → the device is NOT connected to any Wi-Fi
  (its saved creds aren't reachable), it fell into AP fallback after the
  15s boot timeout, and the captive portal is available again at
  `http://192.168.4.1/` — re-enter the plant Wi-Fi + endpoint + key.
- If the AP **stays gone** after a clean power-cycle + 30s → the device
  DID connect to a saved Wi-Fi (i.e. it is provisioned); go back to
  Step 1 and find it on the network by MAC.

This power-cycle test is itself the diagnosis: **AP-back = not connected;
AP-stays-gone = connected.** Neither outcome erases anything.

**Step 3 — Alternative AP trigger if a USB cable is available (also
non-destructive):** connect USB, open the serial console @115200, and
type `provision`. That sets a one-shot RTC flag and reboots into AP mode
**without touching NVS, queue, totalizer, or saved config** (it is
explicitly NOT a factory reset — see `provision.h` / `wifi_provision.h`
`requestReprovision()`). Do **not** use `factory` (that one wipes NVS).

## What must NOT be done (and was not done)

- No firmware flashed, no NVS/LittleFS/queue erased, no factory reset,
  no firmware modified — only inspection and network diagnosis, per the
  rules.
- Queue/totalizer/security-floor/config are untouched and safe; a device
  that connected to Wi-Fi or is in AP fallback is buffering locally with
  the no-data-loss behavior proven throughout this engagement.

## One caveat worth stating plainly

If the device DID just provision successfully onto a plant network, then
the endpoint it will try to sync to is whatever was entered in the
portal. If the portal was submitted with the **old bench endpoint**
(`http://192.168.1.3:8000`) still in the "Server URL" field (which the
form PRE-FILLS with the current value), the device is now on Wi-Fi but
still pointed at the (nonexistent-here) bench server — connected but not
delivering data. Verify the `server_url` via `/api/v1/status` once the
device is reachable again, and confirm it is the real plant endpoint,
not the pre-filled bench default.
