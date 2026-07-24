# 07 — Configuration Management (Part 8)

Evidence: direct code read of `store.h`, `provision.h`, `wifi_provision.h`,
`local_api.h`, `sync.h`, `server.py`, plus this session's live device
checks (`api_key_status`, `server_url`, unchanged throughout).

| Field | Storage | Remote update path | Auth required | Reboot needed | Proven on hardware |
|---|---|---|---|---|---|
| API endpoint (`server_url`) | NVS (`covio` namespace) | Serial console `set url <u>` (needs "reboot" to apply cleanly, per its own message), OR AP-mode `/api/v1/config` POST | Physical serial access OR AP-mode presence — **no remote-over-internet path exists** (`local_api.h`'s normal-mode `/api/v1/config` unconditionally returns 403, `handleConfigForbidden_`) | Yes (console message says so explicitly) | Serial-console path used routinely this session (confirmed working) |
| API token (`api_key`) | NVS (`covio` namespace) | Same as above (`set key <k>`) — also `POST /admin/devices/<id>/rotate-key` mints a NEW key server-side, but **delivering it onto the device still requires the same AP-mode/serial path** (server.py's own comment: "getting it onto the physical device is the operator's job via the desktop app's provisioning wizard") | Serial: physical access. Admin rotate: HTTP Basic Auth (`require_admin`) | Not for the server-side mint; yes for applying it to the device | Server-side `/admin/devices/<id>/rotate-key` exists in code; NOT exercised this session (would rotate the real bootstrap key on this active device — out of execution boundary without explicit authorization) |
| Device ID | Derived from `ESP.getEfuseMac()` (`store.h::chipId()`) | **Not configurable at all, remotely or locally** — hardware-derived, permanent | N/A | N/A | N/A — by design, immutable |
| Plant ID | **Does not exist as a field anywhere in this codebase** (confirmed, doc 05) | N/A | N/A | N/A | N/A |
| Wi-Fi credentials | NVS (`covio` namespace) | Serial console `set wifi <ssid> <pass>`, or AP-mode captive portal (`wifi_provision.h`) | Physical presence (serial) or AP-mode presence | Yes | AP-mode provisioning code exists; not exercised this session (would require disconnecting this device from its current network) |
| Calibration factor / K-factor | **Server-side only** (`server.py`'s `calibration` table, single global row) | `POST /admin/kfactor` (admin dashboard form) → device picks it up via its own periodic `pollConfig()` (`CONFIG_POLL_MS=60000`, GET `/api/iot/flow/config`) | HTTP Basic Auth (admin role) on the server side; the device itself does not authenticate this specific pull beyond its normal `X-Api-Key` | No reboot needed — device re-polls within 60s | **Genuinely remote-updatable without reflashing, by design** — this is this system's ONE real "remote configuration" capability; not independently re-exercised this session (doing so would change the real device's calibration, out of execution boundary) |
| Sync interval (`PUSH_PERIOD_MS`, `CONFIG_POLL_MS`, `OTA_POLL_MS`) | **Compile-time constants in `config.h`** | **None — requires a firmware rebuild and reflash** | N/A | Yes (full reflash) | N/A |
| Timezone | **Does not exist as a concept anywhere in this firmware** — telemetry timestamps are device-uptime seconds, not wall-clock; `server_time_ms` is used only for OTA manifest expiry estimation (doc 11) | N/A | N/A | N/A | N/A |
| Sampling interval (`TELEMETRY_PERIOD_MS`) | Compile-time constant | **None — requires reflash** | N/A | Yes | N/A |

## Remote configuration: complete, partial, or absent?

**Partial.** Exactly one field (calibration/K-factor) is genuinely
remotely configurable without any physical access, proven by this
project's own design and prior real hardware history
(`03_CREDENTIAL_CONTAINMENT_AND_ROTATION.md`-era and this chain's own
docs referencing the admin dashboard). Every other field in the
mandate's list requires either physical serial access, AP-mode physical
proximity, or a full firmware reflash. **There is no "Coviu Device
Manager" remote-configuration capability for endpoint, token, device
identity, Wi-Fi, sync/sampling intervals, or timezone** beyond what is
described above — `tools/device-manager` (the Electron desktop app) is
itself a LOCAL/USB-adjacent provisioning tool (per its own architecture,
consistent with `wifi_provision.h`'s AP-mode design), not a
remote-over-internet management plane.

## Fields requiring USB or physical reflashing

- Sync interval, sampling interval, timezone concept (nonexistent):
  require a firmware rebuild + reflash.
- Endpoint, token, Wi-Fi: require physical proximity (serial or AP-mode)
  — not reflashing specifically, but not remote either.

## Secret-field handling (test performed: verified via fingerprint, not value)

`api_key_status` (`/api/v1/status`) reports `"default"` throughout this
entire session — a redacted status string (`"missing"`/`"default"`/
`"configured"`), never the raw key. This session did not attempt to
change the real device's API key or Wi-Fi credentials (out of execution
boundary) — so "failed updates preserve the old value" and "malformed
updates rejected" were **not exercised against the real device this
session**; `store.h`'s accessors (`setApiKey`/`setWifi`) are
unconditional writes with no validation logic at all (confirmed by
direct code read) — meaning **a malformed value would currently be
accepted as-is** (e.g. an empty string), a real, disclosed gap, not
"proven safe."
