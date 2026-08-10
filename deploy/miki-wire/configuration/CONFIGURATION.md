# Configuration — MW-001

All configuration lives in NVS and survives reflashing. Set via serial
console @115200 (`help` lists commands).

| Item | Value / rule |
|---|---|
| `device_id` | `esp32-F84AD1A172E0` — fixed (eFuse MAC), not settable |
| `set url` | `https://compliance.mikigroup.co.in` |
| `set key` | production Compatibility-Adapter key (issued once via `POST /api/iot/flow/devices/{device_id}/api-key/`; if re-issuing, `rotate:true` required for an already-keyed device) |
| `set wifi <ssid> <pass>` | plant WiFi (SSIDs with spaces unsupported) |
| `set maxhz <0..2000>` | Miki profile: pulse plausibility ceiling. **Leave 0 (off) until derived per MIKI_WIRE_SENSOR_CHARACTERIZATION.md §4** |
| `set suspects <0|60..604800>` | Miki profile: longest plausible idle (seconds). **Leave 0 (off) until derived** |

Verification: `show` prints everything (API key as status+fingerprint only).
Boot banner must show `[MIKI] profile active: …` and `[WDT] task watchdog
armed: 60s` on the candidate firmware.
