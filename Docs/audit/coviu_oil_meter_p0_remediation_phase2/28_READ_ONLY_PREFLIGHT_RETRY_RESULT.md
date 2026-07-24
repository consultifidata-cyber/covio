# 28 — Read-Only Preflight Retry Result

Re-executes the read-only preflight from `27_READ_ONLY_PREFLIGHT_RESULT.md`
now that a device is physically connected. **No write, reboot (deliberate),
flash, OTA, or configuration action was performed.** One incidental reset
is documented below per the authorization's own acknowledged-risk clause.

## Verdict

**PREFLIGHT INCONCLUSIVE — DO NOT AUTHORIZE HARDWARE WRITES**

Device identity is now cleanly confirmed. The blocking gap is entirely on
the server side: the bench server (`server/server.py`) is not running, so
`/api/v1/status`/`/api/v1/info` are unreachable and endpoint/database
isolation cannot be proven live (only by static code inspection, which is
not sufficient on its own per this mandate's isolation-proof requirement).

## 1. Serial enumeration (Step 1)

| Port | USB identity | Status |
|---|---|---|
| COM3 | Silicon Labs CP210x USB-UART, `VID_10C4&PID_EA60` | Unknown (not currently present) |
| COM4 | Silicon Labs CP210x USB-UART, `VID_10C4&PID_EA60` | Unknown (not currently present) |
| COM5 | CH340 USB-Serial, `VID_1A86&PID_7523` | Unknown (not currently present) |
| **COM6** | **`VID_303A&PID_1001`** (Espressif's own USB VID; PID 0x1001 = ESP32-S3/C3 native USB-Serial/JTAG) | **OK — present** |

Only one port shows a live "OK" status; the other three are stale entries
from previously-connected boards, not concurrently attached — no identity
ambiguity. `VID_303A&PID_1001` is consistent with the claimed hardware
(ESP32-S3 native USB), not conclusive proof of the specific unit by itself,
but combined with the device-ID match below (§3) this is a clean match to
the intended bench unit. **Actual port: COM6**, matching the plan's
assumption.

## 2. Bench server (Step 2)

```
Test-NetConnection 192.168.1.3:8000  -> TcpTestSucceeded: False
Get-NetTCPConnection -LocalPort 8000 -> (no listener)
Get-Process python|pio|platformio    -> (none running)
```

**PREFLIGHT INCONCLUSIVE — BENCH SERVER NOT RUNNING** (this step's own
required early-exit wording). Per instructions, the server was not started
or modified by this session. Repository-level facts (not live-server
facts): branch `fix/coviu-oil-meter-p0-enterprise-readiness`, commit
`af11924200f4a8621f9d7b3186bf53e5ded64dfb`, repo path this working
directory, `server/server.py` present and previously documented (doc 23)
to write to a local `server/covio.db` SQLite file. None of this is
live-verified this session because the process isn't running.

## 3. Endpoint/database isolation (Step 3)

**Not conclusively proven.** Static code review (carried over from doc 23,
not re-verified live this session) supports isolation in principle
(local SQLite file, `device_id`-scoped admin routes, no external
integration code paths in this repo). But per this mandate's own
requirement — "do not rely only on hostname/prior memory," prove it live —
that live proof is unavailable while the server is down. Treated as
**unproven, not disproven.**

## 4. Device identity and health (Step 4) — via passive serial only

One bounded (~8s), read-only serial capture, DTR/RTS explicitly held low,
zero bytes transmitted to the device:

```
=== Covio Oil Flow Meter 1.0.0 ===
device_id=esp32-F4E5B2858428  boot_id=19
(serial console ready — type 'help')
[OK] internal flash storage ready
[TOT] recovered total=280 writes=72574
[Q] acked_seq=36284
[NET] connecting to <redacted home-network SSID>
[SECURITY] ... / [SYNC] push HTTP -1 — keeping queue
```

| Field | Value | Source |
|---|---|---|
| Device ID | `esp32-F4E5B2858428` | serial banner — **matches** doc 23's expected identity exactly |
| Firmware version | `1.0.0` | serial banner — matches intended baseline |
| Build hash | not obtainable | requires `/api/v1/info` (server down) or a `help`/status command (not sent — write/interactive, out of scope) |
| Security version / hw-compat / schema version | not obtainable this session | same reason |
| Boot count | `boot_id=19` | serial banner |
| Totalizer | `total=280`, `writes=72574` | serial banner |
| Highest acknowledged sequence | `36284` | serial banner (`acked_seq`) |
| Highest local sequence / pending count / oldest-pending age / storage bytes / failed-write counter / active alarms | not obtainable this session | require `/api/v1/status` (server down) |
| Wi-Fi | attempting connection to a home-network SSID | **redacted in this document** — no password observed or logged |
| Sync/OTA | `push HTTP -1 — keeping queue` — sync attempt failed (expected, server down); queue correctly retained rather than dropped | serial banner |

No secrets (Wi-Fi password, API key, signing key) were observed, sent, or
recorded.

## 5. Incidental reset (Step 5)

The captured lines are a **full firmware startup banner** (`===...===`,
device-ID line, flash-init, totalizer recovery), not steady-state runtime
logging — this is the signature of a fresh boot, not an already-running
device being tapped mid-stream. The first line appeared ~3.1s after the
port was opened (`OPEN_TIME` to first log line), consistent with a normal
ESP32-S3 reset-to-first-log boot delay.

**Assessment: opening COM6 most likely caused an incidental USB soft
reset**, consistent with this authorization's own acknowledged risk. No
reset command was sent; DTR/RTS were held deasserted for the entire
session.

- Exact time: capture window `t=1784806111.4` (open) to `1784806119.6`
  (close); first boot-banner line at `t=1784806114.5`.
- Reset reason: not explicitly logged in the captured window (no
  `rst:0x..` reason line captured — likely printed before the reader
  attached, given the empty first line).
- Recovery: device reached steady init (`[Q] acked_seq=36284`, sync
  attempted) within the same ~8s window — no boot loop, no repeated
  reset.
- **No independent "before" reading exists** — the device was not
  connected during the prior preflight (doc 27), so there is no
  pre-reset queue/sequence snapshot to diff against. This capture *is*
  the only state this session has.
- A second serial-open was deliberately **not** attempted, specifically
  to avoid inducing a second reset — one incidental reset is within the
  acknowledged risk; deliberately re-opening to gather more fields would
  not be "incidental."

## 6. Queue reconciliation (Step 6)

| Field | Before | After |
|---|---|---|
| highest_local_sequence | unknown (not exposed via serial) | unknown |
| highest_acknowledged_sequence | none (no prior session) | 36284 |
| pending_queue_count | none | unknown (needs `/api/v1/status`) |
| oldest_pending_age | none | unknown |
| storage_used/total_bytes | none | unknown |
| failed_write_counter | none | unknown |
| active_alarms | none | unknown (none observed in captured lines) |

Full reconciliation arithmetic (`initial pending + newly generated =
server accepted + quarantined + final pending`) **cannot be performed** —
most fields require `/api/v1/status`, unavailable while the server is
down. The one number obtained (`acked_seq=36284`) has no prior baseline
to diff against. This is reported as an open gap, not papered over.

## 7. Discrepancies / stop conditions

None of the *listed* mandatory stop conditions were triggered (no
production collision, no credential exposure, no boot loop, no storage
critical state, no state-changing side effect from a GET). The controlling
issue is simply that a required component (the server) is absent, which
this mandate itself treats as an explicit early-exit condition (Step 2).

## 8. Proposed hardware tests (restated, none executed)

1. Successful authenticated OTA
2. Automatic rollback
3. Anti-downgrade rejection
4. Invalid-signature rejection
5. Hash-mismatch rejection
6. Interrupted-download recovery

## 9. Artifacts/hashes proposed for later use (unchanged from doc 22/24)

- `evidence/known-good-1.0.0.bin` + `.sha256`
- `evidence/candidate-1.0.1-candidate.bin` + `.sha256`
- `evidence/signed_manifest_candidate.json`
- `evidence/signed_manifest_downgrade_test.json`
- `evidence/signed_manifest_tampered.json`
- A hash-mismatch manifest (constructed fresh at test time per doc 22)

## 10. Writes/reboots/recovery requiring separate authorization

Unchanged from doc 24: placing manifests/binaries in `server/firmware/`
(triggers the device's own poll/download/flash cycle), the reboots
inherent to Tests 1/2, and `09_USB_RECOVERY_RUNBOOK.md`'s USB reflash as
fallback-only. None attempted this session.

## 11. Remaining pilot blockers

- RISK-02: real Wi-Fi credential rotation — still pending, human action.
- RISK-05: physical OTA/rollback proof — still open.
- RISK-15/RISK-16 hardware-side proof — still pending.
- **New, specific to this retry:** the bench server must actually be
  running for any further preflight step (isolation proof, `/api/v1/info`,
  `/api/v1/status`, queue/storage fields) to be completable. Device-side
  readiness is otherwise good — identity and firmware baseline both check
  out cleanly.

---

**READ-ONLY PREFLIGHT COMPLETE:** No firmware upload, flash write, OTA
activation, configuration change, database mutation, credential rotation,
or deliberate reset was performed. A separate explicit authorization is
required before any physical write or OTA test.
