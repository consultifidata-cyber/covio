# 08 — Monitoring & Escalation Design (Blocker 2 Closure)

Closes independent certification blocker 2: "no monitoring or escalation
mechanism exists, and no alarm reaches a human automatically." This is a
**design document only — nothing in this section was implemented this
session**, per the explicit instruction to design, not build. It specifies
the smallest possible change that lets the existing ERP Notification Center
(a system this repository does not contain the source of, and did not
invent) learn when Balaji's device needs attention, by reusing a data
channel that already exists rather than adding a new one.

## What was reviewed

**Firmware:** `diagnostics.h`'s `computeHealth_()` already computes a
complete, correctly-triggered set of alarms —
`QUEUE_HIGH`/`QUEUE_CRITICAL`, `OTA_FAILED`, `SD_REMOVED`,
`QUEUE_WRITE_FAILURE`, `STORAGE_WARNING`/`HIGH`/`CRITICAL`/`FULL`, and (this
freeze's own addition) `SENSOR_STOPPED`/`REBOOT_LOOP` — all exposed today
via the device's **local** HTTP API (`/api/v1/health`, `/api/v1/status`).
`telemetry.h`'s push payload (the data that actually leaves the device over
the internet, every 5 seconds, to whatever `server_url` is configured) is
completely separate and much narrower: `device_id`, `fw`, `model`,
`kfactor_version`, and per-record `schema_version`/`record_type`/
`boot_id`/`seq`/`ts`/`totalizer`/**`quality`**/`rssi`. **None of the alarm
computation in `diagnostics.h` reaches the server today.** A remote
Notification Center cannot see any of it unless it happens to be on the
same LAN and polling the device's local API directly — which an ERP is
not.

**Device Manager:** confirmed (independent audit, prior session) that its
alarm rendering is generic and data-driven (`dashboard.js`/`liveMonitor.js`
iterate whatever `health.alarms` array the device returns) — it would
display new alarm types correctly with zero code change, but only while a
person has the app open. No push/email/SMS/webhook capability exists
anywhere in `tools/device-manager/` or `server/server.py`.

**The one existing channel worth reusing:** `queue.h` already defines a
`quality` bitfield (`uint16_t`, two bits currently used —
`QUALITY_BACKLOG_HIGH=0x0001`, `QUALITY_TIME_UNSYNCED=0x0002`) that is
stamped on **every telemetry record**, transmitted in every push, and
**already stored server-side** — `server/server.py:317`'s `telemetry` table
has a `quality INTEGER NOT NULL` column, populated from the device's own
value at `server.py:748-752`. Fourteen bits are unused. This is the
smallest possible transport for getting alarm information off the device
and onto the server without adding a new field, a new request, or a new
protocol.

Also present and reusable: `server/server.py`'s `device_events` table and
`record_event()` function (`server.py:640-648`) — the existing audit-log
write path already used for admin actions (key rotation, calibration
changes, auth failures). This is a natural place to also log "device X
raised alarm Y," if the real ERP's Notification Center is built to watch
that table (or an equivalent) for entries worth notifying on — this
document does not assume which mechanism the real Center actually watches,
since that system's source is not available for inspection; it only
specifies what data should be made available to it.

## Minimum information the Notification Center needs

To notify "the owner" that the device requires attention, at minimum:

| Field | Source (already exists) | Notes |
|---|---|---|
| `device_id` | Every push payload (`telemetry.h`) | The only device identity that exists today — no plant/tank/asset model is being invented for this (out of scope, per this task's own instruction not to redesign the product). |
| Alarm condition | New `quality` bits (see below) | Which specific condition fired. |
| Severity | Derived from which bit is set | WARNING vs CRITICAL, matching `diagnostics.h`'s existing severity assignment per alarm type. |
| First-detected time | Server's own `recv_ms` (already stored per push, `server.py`) | The device has no RTC (unchanged, documented limitation) — the server's own receive timestamp is the only trustworthy time source, and it already exists. |
| "Went silent" signal | Absence of expected pushes | A device that has stopped reporting cannot report why via `quality` bits — this must be detected server-side by elapsed time since the last received push, not by anything the device sends. |

## Smallest integration design

**Two additions, no new endpoints, no new protocol:**

1. **Firmware: extend the existing `quality` bitfield** (`queue.h`) with a
   small number of new bits, each mapped one-to-one to an alarm
   `diagnostics.h` already computes but currently only surfaces locally:
   - `QUALITY_SENSOR_STUCK   = 0x0004` — mirrors the `SENSOR_STOPPED` alarm.
   - `QUALITY_REBOOT_LOOP    = 0x0008` — mirrors the `REBOOT_LOOP` alarm.
   - `QUALITY_STORAGE_CRITICAL = 0x0010` — mirrors `STORAGE_CRITICAL`/`STORAGE_FULL` (the two most severe capacity alarms; the milder `WARNING`/`HIGH` tiers are deliberately not proposed for remote notification, to avoid alerting on conditions that don't yet need a human).
   - `QUALITY_WRITE_FAILURE  = 0x0020` — mirrors `QUEUE_WRITE_FAILURE`, the one alarm that means data has already been lost.
   These would be OR'd into the same `r.quality` value `Telemetry::build()`
   already computes, using the same `computeHealth_()` logic that already
   exists — no new alarm logic, just exposing existing conclusions on the
   wire. (Not implemented this session, per instruction.)

2. **Server/ERP: react to the bits already arriving.** Whichever system
   owns the Notification Center needs, on each received push: (a) compare
   the incoming `quality` value's new bits against the device's
   previously-stored value (edge-triggered — notify once when a bit
   transitions from clear to set, not every 5 seconds while it stays set),
   and (b) run a periodic check for "no push received from this device in
   longer than `N` minutes" (an offline/silence detector, since a fully
   unresponsive device can't send any bit at all). Both of these are
   logic changes to the receiving system, not to this firmware/repo, and
   are exactly the kind of thing an existing Notification Center should
   already be positioned to do once it has the data — this document
   deliberately does not specify how that Center delivers a notification
   (email/SMS/dashboard/etc.), since reusing whatever it already does is
   the entire point.

**Why the bitfield, not a bigger payload change:** the alternative (sending
a full `health_state`/`alarms` JSON array with every push, mirroring the
local API's richer shape) would work too, but is a larger wire-contract
change for no benefit here — a single device only ever has one active
"most severe" condition worth escalating at a time in practice, and the
existing bitfield already has 14 free bits, more than enough headroom.
Reusing it keeps this the smallest possible change to close the blocker,
consistent with the instruction not to invent new mechanisms.

## What this design does not include (explicitly out of scope)

- No new alerting technology (email/SMS/webhook library, etc.) — the whole
  point is reusing whatever the ERP's Notification Center already does.
- No device/asset/plant registry — `device_id` remains the only identity,
  matching every other part of this project correctly scoped to one
  device.
- No implementation. The two additions above are specified precisely
  enough to implement in a small, bounded change later, but neither the
  firmware bit definitions nor any server-side reaction logic were written
  this session.

## What remains (field action / follow-up work)

1. **Firmware:** add the four `QUALITY_*` bit definitions to `queue.h` and
   OR them into `Telemetry::build()`'s existing quality computation —
   small, bounded, no schema/architecture change. Requires a firmware
   build + deployment (the same OTA-transition path documented in
   `09_OTA_TRANSITION_MIGRATION_PLAN.md`).
2. **ERP side (owned by whoever maintains the actual production
   Notification Center at `data.funtastik.co.in` — not this repository):**
   implement the edge-triggered quality-bit check and the stale-push/offline
   check described above, wired to notify the device's owner through
   whatever channel that Center already uses. This cannot be implemented
   or verified from this repository, since that system's source is not
   available here.
3. Once both sides exist, verify end-to-end with a real test (e.g.
   deliberately disconnect the device's flow sensor or Wi-Fi briefly and
   confirm a notification actually arrives) before relying on it
   operationally.

This blocker is **design-complete; implementation on both the firmware and
ERP side remains outstanding.**
