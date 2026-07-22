# 13 — Plant Installation Runbook

**This runbook applies only if the business decides to proceed with a supervised pilot despite the open P0 items in `11_RISK_REGISTER.md`. It is not a recommendation to do so — see `00_EXECUTIVE_VERDICT.md` for the actual go/no-go recommendation.**

## Pre-install (must complete first)
1. Rotate the WiFi credential currently hardcoded in `config.h` (RISK-02) — do this before the unit leaves a controlled environment.
2. Run the device through `POST /admin/devices/provision` to obtain a real, unique per-device API key (today it is running on the shared default bootstrap key).
3. Change `server_url` to the real production/staging endpoint via the AP-mode provisioning flow — **do not** install with `server_url` still pointing at a bench IP.
4. Confirm the target `server_url` uses `https://` with a real, non-placeholder CA cert pinned (requires a real `env:release` build, which requires replacing `certs.h`'s placeholder — this cannot be skipped; the `release` build environment will refuse to compile otherwise, by design).
5. Confirm the business's actual required offline-buffering duration against Section 1's capacity analysis (and get the partition size independently confirmed, since this audit could not verify it byte-exact) — do not rely on an unqualified "several days" claim.
6. Get an explicit answer on whether precise offline event-time attribution matters for this installation (RISK-11) — if yes, this blocks the install until addressed.

## Required monitoring during the pilot
- Watch `/api/v1/status.queue.backlog` and `.last_sync_ms_ago` — a backlog that grows and never shrinks, even while `wifi.connected: true`, is the visible symptom of RISK-01 (the ack-gap stall) until that is fixed; treat it as an active incident, not a transient blip, if it persists past a normal outage window.
- Watch `/api/v1/health` for `QUEUE_HIGH`/`QUEUE_CRITICAL`/`OTA_FAILED`/`SD_REMOVED` alarms.
- Periodically check the server's `quarantined_records` table directly (no dedicated API exists yet) for any rows against this device — this is currently the only way to detect RISK-01 occurring.
- Periodically poll `/api/v1/info` for `boot_id` — an unexpectedly fast-incrementing `boot_id` across check-ins indicates a reboot loop (RISK-12), which has no other remote signal today.

## Maximum permitted offline duration
Bound by Section 1's capacity analysis: the soft-alert threshold is reached at roughly 5.5 hours at a 1-sample/second cadence (unverified partition-size assumption), with hard physical exhaustion at roughly 4-5x that. **Confirm the actual field sampling cadence and partition size before setting an operational SLA** — do not use "several days" without that confirmation.

## Rollback / replacement procedure
- If the device malfunctions: physical USB access + serial console (`factory` command) resets configuration **without** losing unsynced queue/totalizer data (confirmed in this audit, Section 14) — this is a safe recovery step for configuration problems.
- If firmware is suspected corrupted/misbehaving: do **not** rely on OTA rollback as a proven recovery path yet (RISK-05) — plan for physical reflash via USB (`pio run -t upload`) as the only currently-proven recovery method.
- Keep a spare provisioned unit ready to physically swap in, given OTA rollback and long-term flash endurance are both not yet certified.

## Scope restriction
**This device must not be used to control billing, inventory, or compliance records as an authoritative source until RISK-01 and RISK-07 are resolved** — RISK-01 can cause silent local queue growth (though not server-side data loss) that complicates trust in real-time backlog figures, and RISK-07 means historical consumption figures are not immutable once calculated. It may be used as a **monitoring/informational** data source during the pilot.

## Remaining risks the business is accepting by choosing to pilot despite P0 items open
All items in `11_RISK_REGISTER.md` marked "Blocks Deployment: Yes" that are not yet remediated at the time of install. This must be an explicit, informed business decision, not a default.
