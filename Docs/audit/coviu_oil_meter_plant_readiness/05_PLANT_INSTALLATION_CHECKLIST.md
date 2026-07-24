# 05 — Plant Installation Checklist

Onsite checklist for the first controlled deployment. Items this session
could verify on the bench are marked **(bench-verified)**; items that
genuinely require the physical plant site or physical sensor hardware are
marked **(onsite-only — not yet performed)**.

## Before Power-On

- [ ] Inspect enclosure — **(onsite-only)**
- [ ] Inspect power supply — **(onsite-only)**
- [ ] Inspect sensor wiring — **(onsite-only)** — see 09_KNOWN_LIMITATIONS.md; `totalizer_raw_pulses` has read a flat, unchanging value for this entire multi-day audit trail, meaning no real flow has been counted at any point this session
- [ ] Verify antenna position — **(onsite-only)**
- [ ] Verify device label/identity matches `esp32-F4E5B2858428` (this exact bench unit) — **(bench-verified: confirmed via `/api/v1/info` throughout)**
- [ ] Confirm server endpoint is the intended PRODUCTION/plant endpoint, not the bench address `192.168.1.3:8000` — **(onsite-only — see 07_PLANT_NETWORK_READINESS section of the main report; the bench endpoint must not be carried into the plant deployment)**
- [ ] Confirm recovery equipment onsite (laptop, cable, verified binary — doc 04) — **(onsite-only, but the runbook and artifact are ready)**
- [ ] Confirm manual fallback (existing manual measurement process) is available and understood by the operator — **(onsite-only)**

## After Power-On

- [ ] Verify expected boot ID increment (compare to last known value before shipping) — **(bench-verified mechanism; specific pre-shipment boot_id must be recorded onsite)**
- [ ] Verify expected build identity: `build_commit=856972ef6106d662c5f8a7f5b71c9a60ce40edc1`, `build_dirty=false` via `/api/v1/info` — **(bench-verified, this exact check)**
- [ ] Verify WiFi connection to the **plant** network (not the bench network) — **(onsite-only)**
- [ ] Verify server contact (`last_push_http_code:200`, `last_sync_ms_ago` small) against the **plant/production** endpoint — **(onsite-only)**
- [ ] Verify queue initializes (`sd_status:"ok"`) — **(bench-verified mechanism)**
- [ ] Verify no alarms (`/api/v1/health` → `alarms: []`) — **(bench-verified mechanism)**
- [ ] Verify totalizer baseline — record the exact starting `totalizer_raw_pulses` value before any real flow — **(onsite-only; must be a REAL sensor reading, not the bench's static value)**
- [ ] Verify dashboard/API visibility from the intended monitoring location — **(onsite-only)**

## Before First Oil Flow

- [ ] Record manual meter reading — **(onsite-only, requires the physical reference meter)**
- [ ] Record device totalizer (`totalizer_raw_pulses`) — **(onsite-only)**
- [ ] Record server totalizer (via the bench-style admin dashboard or DB query, against the production server) — **(onsite-only)**
- [ ] Confirm correct plant/device mapping (this device is registered to the correct plant/asset) — **(onsite-only — no device-registry provisioning for a named plant asset has occurred in this entire audit trail; `logical_device_id` has read `null` throughout)**
- [ ] Confirm production batch/context, if applicable — **(onsite-only)**

## During First Controlled Flow

- [ ] Observe live pulse movement (`pulse_frequency_hz` in `/api/v1/metrics` should become nonzero) — **(onsite-only — never observed nonzero this entire session)**
- [ ] Compare physical and digital readings in real time — **(onsite-only)**
- [ ] Confirm queue/sync behaviour (backlog draining, acked_seq advancing) — **(bench-verified mechanism, extensively)**
- [ ] Watch for resets or alarms — **(bench-verified mechanism)**
- [ ] Ensure no duplicate records (server-side `SELECT COUNT(*), COUNT(DISTINCT seq)` equality) — **(bench-verified methodology, zero duplicates observed at every checkpoint this entire audit trail)**

## After First Controlled Flow

- [ ] Reconcile measured quantity — **(onsite-only)**
- [ ] Reconcile device totalizer — **(onsite-only)**
- [ ] Reconcile server totalizer — **(onsite-only)**
- [ ] Confirm database continuity (contiguous sequence range, zero quarantined) — **(bench-verified methodology)**
- [ ] Obtain operator and supervisor sign-off — **(onsite-only, human process step)**

## Explicit summary

Every **firmware/software mechanism** this checklist depends on
(identity reporting, queue/sync behaviour, alarm reporting, database
continuity checking) has been proven repeatedly on real hardware this
audit trail. **Every physical/sensor/plant-network item is genuinely
unperformed** and requires a physically present person at the actual
site — this checklist does not, and cannot, substitute for that.
