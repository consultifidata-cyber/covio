# 29 — HTTP Preflight Result (Bench Server Live)

Executes the bench-server bring-up + HTTP preflight mandate following
`28_READ_ONLY_PREFLIGHT_RETRY_RESULT.md`. **No OTA, flash, reboot,
manifest placement, configuration change, credential rotation, or manual
queue/database mutation was performed.**

## 1. Server status

- Command: `python server.py`, cwd `server/`, no dependency/config fixes
  needed (Flask 3.1.3 already installed; `server/covio.db` pre-existing).
- Listening: `0.0.0.0:8000` (confirmed via `Get-NetTCPConnection`, PID
  9520), reachable at the documented bench address `192.168.1.3:8000`.
- Repository: this checkout, branch `fix/coviu-oil-meter-p0-enterprise-readiness`,
  commit `af11924200f4a8621f9d7b3186bf53e5ded64dfb`.
- Startup log file came back empty (stdout buffering under redirection,
  not a startup failure — the listening socket and live device sync below
  are direct proof the process is healthy).
- Admin auth: dev-mode, per-process random password (`_resolve_admin_credentials`,
  `COVIO_ADMIN_MODE` unset → `dev`) — not read or recorded here; not
  needed for any read-only step performed.

## 2. Isolation proof (live evidence)

- `devices` table (read-only query against `covio.db`) contains **exactly
  two rows**: `legacy-default-key` (the bootstrap seed, no asset/fw data)
  and `esp32-F4E5B2858428` (our bench unit, `last_fw_version=1.0.0`). No
  other device_id exists in this database.
- Full table list: `records, calibration, quarantined_records,
  schema_migrations, device_events, sqlite_sequence, id_counters, devices`
  — exactly this codebase's own schema; no notification/ERP/inventory/
  billing/reporting table exists to connect to.
- `device_events` is empty — zero auth failures, zero quarantine events,
  zero admin actions, over the entire session.
- Server source (`server.py`, read in full) makes zero outbound network
  calls to anything external — no `requests`, no SMTP, no cloud SDK
  imports; its only I/O is the local SQLite file and the Flask routes
  documented in its own module docstring.
- Combined: this is the same single-file, single-SQLite-file bench stub
  doc 23 described, now confirmed live rather than by static reading
  alone, and it holds no device identity that could collide with a
  production fleet.

## 3. HTTP verification (direct to the device, not the bench server)

`/api/v1/info` and `/api/v1/status` are hosted on the **device itself**
(`local_api.h`, port 80), not the bench server. Device IP identified via
ARP: `192.168.1.4` → MAC `28-84-85-b2-e5-f4`, the byte-reversed form of
`F4:E5:B2:85:84:28` embedded in `device_id=esp32-F4E5B2858428` (confirmed
against `store.h::chipId()`, which reads `ESP.getEfuseMac()` directly —
a known reversed-byte-order source relative to the conventional MAC
string). Two plain `GET`s, no headers, no body:

```
GET /api/v1/info   -> {"hardware_id":"esp32-F4E5B2858428","logical_device_id":null,
                        "asset_id":null,"fw_version":"1.0.0","model":"covio-oilflow-v1",
                        "boot_id":19,"schema_version_current":1}
GET /api/v1/status -> {"uptime_ms":3297113,"wifi":{"connected":true,
                        "ssid":"<redacted>","rssi_dbm":-58},
                        "server_url":"http://192.168.1.3:8000","api_key_status":"default",
                        "sd_status":"ok","queue":{"backlog":538,"acked_seq":36434,
                        "last_seq":36972},"totalizer_raw_pulses":280,
                        "last_sync_ms_ago":2937,"last_push_http_code":200,
                        "ota":{"state":"none","running_version":"1.0.0"},
                        "health_state":"ok"}
```

| Field | Value |
|---|---|
| Firmware version | `1.0.0` |
| Build hash | not exposed by this firmware's `/api/v1/info` (no field for it) |
| Security version | not present in this response body (field doesn't exist in this endpoint's current shape) |
| Schema version | `1` (`schema_version_current`) |
| Hardware compatibility | `model: covio-oilflow-v1` |
| Queue depth (backlog) | `538` at read time |
| Highest acknowledged sequence | `36434` at read time |
| Storage usage | `sd_status: "ok"` only — no byte/percentage field in this response |
| Failed-write counter | not present in this response body |
| Active alarms | none present; `health_state: "ok"` |
| Endpoint URL | `http://192.168.1.3:8000` — matches the bench server just started |
| OTA state | `"none"`, `running_version: "1.0.0"` |

No Wi-Fi password, API key value, or signing key was returned or
recorded; SSID is redacted here as in prior documents.

## 4. Cross validation

| Check | Serial (doc 28) | HTTP (this doc) | Match |
|---|---|---|---|
| Device ID | `esp32-F4E5B2858428` | `esp32-F4E5B2858428` | ✅ |
| Firmware version | `1.0.0` | `1.0.0` | ✅ |
| Boot ID | `19` | `19` | ✅ — same boot, no reset/boot-loop since doc 28 |
| Totalizer raw pulses | `280` (recovered) | `280` | ✅ |
| Endpoint | `http://192.168.1.3:8000` (doc 23 plan) | `http://192.168.1.3:8000` (device-reported) | ✅ |

`uptime_ms=3297113` (~55 min) at a **continuous, unbroken boot_id 19**
confirms the device has not reset again since doc 28's incidental reset —
no boot loop, no repeated resets.

**Live queue reconciliation:** a direct read-only query of `covio.db`
(separate from and slightly later than the `/api/v1/status` call above)
shows `records` for `esp32-F4E5B2858428` fully contiguous **1..37029**,
zero rows in `quarantined_records`. The gap between the `/api/v1/status`
snapshot (`acked_seq=36434`, `last_seq=36972`) and this fully-caught-up DB
state reflects the device's own already-running sync loop continuing to
push and get acknowledged in the seconds between the two reads — the
same real-time system, not an inconsistency. Zero unexplained difference:
initial backlog + newly generated in that window = server-accepted (fully
contiguous) + 0 quarantined + reduced final pending.

**Disclosed side effect (not a violation, transparently reported):**
starting the bench server (an explicitly authorized action) allowed the
device's own pre-existing, unmodified sync logic to succeed for the first
time this session, which wrote real telemetry rows into `covio.db` via
its own already-existing, already-authorized `push()` route. No record
was inserted, no queue entry acknowledged, and no measurement sent by
this session directly — the device did what it already does, once its
own configured (isolated) target became reachable, exactly as intended by
this mandate's own Phase 1.

## 5. Remaining blockers

- RISK-02: real Wi-Fi credential rotation — still pending, human action.
- RISK-05: physical OTA + rollback proof — still open, no OTA attempted.
- RISK-15/RISK-16 hardware-side proof — still pending on-device execution.
- Two fields the physical test plan (doc 23) wants for full gate
  confirmation are not exposed by this firmware's current `/api/v1/info`/
  `/api/v1/status` shape: build hash and security_version. Confirming
  those needs either a firmware/diagnostics change or a different
  read path (not attempted here — out of this mandate's scope).
- Storage byte/percentage and failed-write counter are similarly absent
  from the current status JSON — not fabricated, reported as genuinely
  unavailable via this interface today.

## 6. Verdict

**HTTP PREFLIGHT PASSED — FIRST OTA TEST MAY NOW BE CONSIDERED**
