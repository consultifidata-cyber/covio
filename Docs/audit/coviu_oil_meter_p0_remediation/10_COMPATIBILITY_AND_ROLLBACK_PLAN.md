# 10 — Compatibility and Rollback Plan

## Firmware compatibility

- **Old firmware (pre-this-session) contacting the NEW server code:** fully
  compatible. The wire contract (`{"ack_seq": <int>, "server_time_ms": ...}`,
  optionally now also `"quarantined": [...]`) is a strict superset of the
  old response shape. `sync.h::extractLong_()` looks up `"ack_seq"` by key
  name and ignores unrecognized keys — confirmed by reading the parser
  (`sync.h:187-198`): it does not validate the response is *only* the
  fields it knows about. An old, already-fielded device will simply never
  see or use the new `quarantined` field, with zero behavior change.
- **New firmware (with this session's P0-4 change) contacting the OLD
  server:** fully compatible — P0-4 is entirely device-local (a new durable
  failure counter + new diagnostics fields); it adds no new outbound
  request, no new required server response field, nothing the old server
  needs to understand.
- **No firmware wire-contract change was required for P0-1** — this is the
  central design decision in doc 02, specifically chosen to avoid exactly
  the mixed-fleet compatibility risk a heavier redesign (e.g. the mandate's
  illustrative multi-array contract) would have introduced.

## API compatibility

- Every existing device-facing route (`push`/`config`/`ota_manifest`) is
  byte-for-byte unchanged in its request/response shape (only the internal
  `ack_seq` *computation* changed, not the field's type or meaning from the
  device's perspective: "everything at or below this seq is safe to
  prune").
- Every `/admin/*` route's request/response shape (body schema, status
  codes for existing success/error cases) is unchanged; the ONLY new
  externally-visible behavior is a `401`/`429` on missing/invalid/rate-
  limited credentials, which is by definition new behavior only encountered
  by a caller that was previously relying on the absence of auth (i.e. the
  vulnerability itself).

## Database migration

- No schema migration was required for P0-1, P0-3, or P0-4. `records`,
  `quarantined_records`, and `device_events` are all pre-existing tables;
  this session added new **rows** (a `RECORDS_QUARANTINED` event type,
  `ADMIN_AUTH_FAILED` events) to `device_events`, which has no fixed
  `event_type` enum/constraint — confirmed by reading the schema (`event_type
  TEXT NOT NULL`, no CHECK constraint) — so this required no migration.

## Rollback compatibility (if this branch needs to be reverted)

- Reverting `server.py` to the pre-fix commit would restore RISK-01 and
  RISK-03 exactly as found — no data-loss risk from a revert itself, since
  no destructive migration was performed.
- Reverting `queue.h`/`diagnostics.h`/`config.h` to pre-fix would mean any
  `FailureState` files (`/queue/failA.bin`/`failB.bin`) already written by
  a fixed firmware build would simply be ignored by old firmware (it never
  reads those paths) — no crash risk, the old firmware's `LittleFS`
  directory listing/orphan-segment cleanup (`cleanupOrphanSegments_()`)
  only inspects `seg_*.bin`-named files by pattern match, so the new
  `failA.bin`/`failB.bin` files are silently ignored by it, not mistaken
  for a queue segment.

## Mixed-fleet behavior during a staged rollout

Not directly applicable at this project's current single-bench-unit scale
(no fleet-wide staged-rollout mechanism exists in `ota.h`/`server.py` yet —
the OTA manifest is a single static file, offered identically to every
device that polls it, confirmed by reading `ota_manifest()`). This is
already a known, tracked gap (RISK-14 in the prior risk register, P1/"fleet
scale" item) — unaffected by, and out of scope for, this P0 remediation
pass.
