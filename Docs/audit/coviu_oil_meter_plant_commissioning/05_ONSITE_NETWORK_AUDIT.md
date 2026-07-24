# 05 — Onsite Network Audit: locate the ESP32 and read its state

**Timestamp:** 2026-07-24.

## Result: cannot complete remotely — this is a genuinely physical/onsite
task, and the two authoritative sources are both unavailable to a
software agent on this laptop

The mandate's own framing ("Do a physical onsite network audit") is
correct: determining **which SSID the ESP32 is on** has exactly two
authoritative sources — (a) the device itself, or (b) the plant router's
DHCP/client table — and **neither is reachable from this session**. I did
every remote step that was actually possible; the results and the hard
limits are below, with a precise onsite runbook.

## New, useful findings this session

### The laptop's saved Wi-Fi profiles (what it CAN join)
```
Covio-Setup-8428      <- the device's OWN setup AP: the laptop HAS
                         connected to it before (confirms the AP was
                         genuinely reachable earlier, consistent with
                         "previously visible")
Airtel_amar_3999      <- the device's OLD saved station network (its
                         config through every prior phase)
AirFiber-Balaji       <- currently connected
+ ~17 phone-hotspot / office SSIDs (Amar's A34, Galaxy A22/A17, Singh 4G,
  Redmi 9/10, Infinix..., ALFA IT SOLUTIONS, SLRC_5G, RCWIFI, etc.)
```

### Currently in range (joinable set is tiny)
```
AirFiber-Balaji   (saved -> joinable; ALREADY connected, already scanned)
BALAJI-WIFI-5G    (NOT in saved profiles -> cannot join without its password)
```
`Covio-Setup-8428` is NOT currently in range. `Airtel_amar_3999` is NOT
in range.

### A real clue: "AirFiber-Balaji" spans multiple subnets
Across this session the SAME SSID "AirFiber-Balaji" handed this laptop
**two different subnets** — `192.168.1.x` earlier and `192.168.31.x` now
(and a `10.120.222.x` hotspot in between). This means AirFiber-Balaji is
a multi-AP / multi-segment network. **The ESP32 could be associated with
AirFiber-Balaji but on a different L2 segment than the laptop currently
sits on — in which case an ARP scan from here structurally cannot see
it**, even though the device is online. Only the router's client table
resolves this.

### Device MAC still not on the reachable subnet
Fresh sweep of `192.168.31.0/24`: MAC `28:84:85:B2:E5:F4` **not present**
(consistent with the multi-segment explanation above, or with the device
being on a different network entirely).

### Router DHCP table is credential-gated
Gateway `192.168.31.1` → `HTTP/1.1 307` → `https://192.168.31.1/` (a login
page). Reading the DHCP client list — the mandate's step 4, the single
most direct way to answer this — requires router admin credentials that
were not provided. **I did not and will not attempt to guess or
brute-force router credentials.**

## Answering the mandate's steps directly

1. **Which SSID is the ESP32 connected to?** **Cannot be determined
   remotely.** Not derivable from the laptop alone — it requires reading
   the device (unreachable) or the router client list (credential-gated).
   *Candidates*, by reasoning: if it was reprovisioned onsite, most
   likely one of the plant SSIDs the laptop has also seen
   (AirFiber-Balaji or BALAJI-WIFI); if it was NOT reprovisioned, its
   config still names `Airtel_amar_3999`, which is not in range here, so
   it could not have connected to that and would instead be in AP mode.
2. **Is the laptop on that same SSID?** Laptop is on `AirFiber-Balaji`.
   Whether that equals the device's network is unknown (see #1), and even
   if the SSID matches, the multi-segment finding means "same SSID" does
   not guarantee "same L2 subnet / ARP-reachable."
3. **Connect the laptop to the ESP32's Wi-Fi and repeat discovery.**
   Not actionable: the device's network is unknown (#1); of the plausible
   candidates, only `AirFiber-Balaji` is both in-range and has saved
   credentials (already joined + scanned, device not visible),
   `BALAJI-WIFI-5G` is in range but has no saved credentials to join
   with, and `Airtel_amar_3999`/`Covio-Setup-8428` are not in range.
4. **Inspect the plant router DHCP list for the MAC.** Not actionable
   from here — credential-gated (see above).
5. **Record IP, ping, open status page.** Not reached — device never
   located.
6. **Report server_url / Wi-Fi status / queue / firmware / server
   reachability.** Not reached — device never located; no value read (and
   none fabricated).

## Precise ONSITE runbook (for a person physically at the plant)

Any ONE of these locates the device and reads its state read-only —
none modify config, reset, or reflash:

**Option A — Router (fastest, authoritative):** Log into the plant
router admin (`https://192.168.31.1/` or the plant's actual gateway) with
the site's credentials. Find MAC `28:84:85:B2:E5:F4` (hostname likely
`covio-858428`) in the DHCP client / connected-devices list. Note its
leased IP. From a device on that same network, browse to
`http://<that-ip>/api/v1/status` (and `/api/v1/info`).

**Option B — Same-network laptop:** Put the laptop on the EXACT network
segment the device is on (if AirFiber-Balaji, ensure the same AP/segment
— given it's multi-segment, the router client list from Option A is the
reliable way to know which). Then `arp -a` / a subnet sweep will show the
MAC, or `http://covio-858428.local/` (its mDNS name) may resolve
directly.

**Option C — USB serial (definitive, needs a cable):** Connect the ESP32
to a laptop by USB, open the serial console @115200, type `show`. It
prints device_id, server_url, wifi_ssid, and the redacted api_key
fingerprint (the raw key is no longer printed — fixed in commit
`443dc44`). This directly answers "which SSID is it on" and "what
server_url is configured" with zero network dependency. `show` is
read-only; do NOT type `factory`.

## What to check the moment it IS reached (the standing blocker)

Read `/api/v1/status` (or serial `show`) and confirm **`server_url`**:
- If it is still `http://192.168.1.3:8000` → it is on the OLD bench
  endpoint (the pre-fill trap noted in prior reports): the device may be
  connected to Wi-Fi but cannot deliver data. **Report only, do not
  change without approval** (per the standing rule).
- If it is the real plant endpoint → proceed to live-communication
  verification.

## Rules compliance

- No AP re-trigger performed. No firmware flashed/modified. No NVS /
  LittleFS / queue erased. No factory reset. No configuration changed.
- No router credentials guessed or brute-forced.
- No device value fabricated — everything unread is reported as unread.
