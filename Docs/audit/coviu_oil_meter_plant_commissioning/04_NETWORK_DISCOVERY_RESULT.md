# 04 — Network Discovery Result: locate the ESP32 on the plant LAN

**Timestamp:** 2026-07-24.

## Result: DEVICE NOT FOUND from this laptop — but this is inconclusive, because the laptop is demonstrably NOT on the plant LAN (and its own connectivity is unstable)

I could not prove the ESP32 is on the plant network, and I could not prove
it isn't — because **this laptop is not reliably on the same network the
device would be on.** What I CAN state precisely is what was searched and
why the search is not authoritative.

## What was searched (real commands, this session)

The laptop's own IP changed THREE times across this discovery session,
landing on three different subnets — none of which contained the device:

| Laptop subnet checked | Method | Hosts found | ESP32 MAC `28:84:85:B2:E5:F4` present? |
|---|---|---|---|
| `192.168.1.0/24` (earlier) | ping sweep + ARP | ~12 | No |
| `10.120.222.0/24` | ping sweep + ARP | 0 (hotspot-style) | No |
| `192.168.31.0/24` (now, "AirFiber-Balaji") | ping sweep + ARP | ~24 (mostly a `dc-62-79-d8-*` device fleet) | No |
| mDNS `covio-858428.local` (current network) | name resolution | — | Does not resolve |
| Router DHCP client list (`192.168.31.1`) | HTTP | gateway returns 307 → login page | **No admin credentials provided — cannot read leases** |

## Why "not found" is not the same as "not on the plant network"

- **ARP is link-local.** It only sees devices on the SAME layer-2
  subnet as the laptop. If the device joined a plant Wi-Fi/VLAN that this
  laptop is not attached to, an ARP scan from here cannot see it,
  regardless of whether the device is happily online.
- **The laptop is not stably on any one network.** Its IP moved
  `192.168.1.41` → `10.120.222.217` → `192.168.31.206` during this and
  the immediately preceding phase. It is currently on "AirFiber-Balaji"
  (192.168.31.x) — but there is no confirmation this is the same network
  the device was provisioned onto.
- **The plant router's DHCP lease list — the authoritative source the
  mandate's step 1 asks for — is not accessible to this session.** The
  gateway (`192.168.31.1`) responds (HTTP 307 → a login), but no router
  admin credentials were provided, so the DHCP client list cannot be
  read. This is the single most direct way to answer "is the device on
  this network and what IP did it get," and it is gated behind
  credentials this session does not have.

## Steps 3–4 (read status / check server_url): NOT REACHED

The device was never located, so its `/api/v1/info` / `/api/v1/status`
could not be read, and its configured `server_url` could not be checked.
No value was read; none is reported (reporting one would be fabrication).

## Step 5 — safe re-provisioning path (already documented, restated)

Since the device could not be found, the safe, non-destructive recovery
path from `03_AP_DISAPPEARANCE_DIAGNOSIS.md` stands, unchanged:

1. **The authoritative check requires being on the plant network itself.**
   Someone with access to the plant router should read its DHCP client
   list for MAC `28:84:85:B2:E5:F4` / hostname `covio-858428`. If present
   → the device is online; connect a laptop to THAT network and reach it
   at its lease IP or `http://covio-858428.local/`.
2. **If it is not on any Wi-Fi**, power-cycle the device and wait ~30s:
   AP `Covio-Setup-8428` reappearing means it is NOT connected (portal
   available again at `192.168.4.1`); the AP staying gone means it IS
   connected (find it via DHCP by MAC). Non-destructive either way.
3. **USB alternative**: serial console → `provision` (re-enters AP mode
   without erasing NVS/queue/config). Never `factory`.

## Final report answers

1. **Is the ESP32 on the plant Wi-Fi?** **Undetermined.** Not found on
   any subnet this laptop could reach, but the laptop is not confirmed to
   be on the plant network, and the router's DHCP list (the authoritative
   source) is credential-gated and inaccessible to this session.
2. **What IP did it receive?** **Unknown** — could not be determined
   from here.
3. **Is it reachable?** **Not from this laptop's current network
   position.** Reachability from the correct network could not be tested.
4. **What server_url is configured?** **Unknown** — the device could not
   be reached to read it. (Carryover risk, unchanged: if it WAS
   provisioned, the portal pre-fills the Server URL field with the prior
   value, so it may still carry the old bench URL `192.168.1.3:8000` —
   this MUST be verified once the device is reachable, per the standing
   blocker.)
5. **Ready for live commissioning, or what exact blocker remains?**
   **Not ready. The exact blocker is a discovery/access gap, not a device
   fault:** the commissioning must be run from a machine that is
   genuinely on the same plant LAN as the device (or has plant-router
   DHCP access), which this session's laptop is not. Until the device is
   located on its actual network and its `/api/v1/status` read, live
   commissioning cannot proceed.

## Exact next action required (human/onsite)

Put the commissioning laptop **on the plant network the device was
provisioned onto** (or obtain plant-router admin access to read the DHCP
lease for MAC `28:84:85:B2:E5:F4`). Then re-run this discovery — with the
laptop genuinely on the device's own L2 network, an ARP scan or the DHCP
lease list will locate it immediately, and steps 3–4 (read build info,
`server_url`, device ID, queue, health) can proceed read-only.

## Rules compliance

- No AP re-trigger attempted (per instruction "do NOT bring the AP back
  yet").
- No firmware flashed/modified, no NVS/LittleFS/queue erased, no factory
  reset.
- No configuration changed. No device value fabricated. Read-only
  discovery only.
