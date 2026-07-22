# 12 — Remediation Plan

## P0 — before any plant installation, even a supervised pilot

1. **RISK-02 (hardcoded WiFi credential):** rotate the real password on the affected network now; replace `config.h:37-38` with placeholders. *Files:* `config.h`. *Test:* confirm the repo no longer contains the real credential; confirm the AP-mode provisioning flow (`wifi_provision.h`) is the documented path for setting real credentials per unit. *No migration/compatibility concern* — this is a source-level change only, does not affect any already-provisioned device's stored NVS values.
2. **RISK-01 (ack-gap poison-record stall):** decide and implement one of: (a) advance the device's effective watermark past a server-confirmed-permanent rejection, or (b) surface a specific "this seq was rejected, stop waiting for it" signal the device can act on. *Files:* `server.py` (`push()`'s ack computation), `sync.h`/`queue.h` (if a device-side reaction is added). *Test:* deliberately submit a record that fails schema validation, confirm the device's queue does not grow unbounded afterward, confirm records after the gap are still ack'd and pruned. *Migration:* affects the ack-contract between firmware and server — must be versioned carefully if any already-fielded device exists on the old contract.
3. **RISK-03 (unauthenticated admin endpoints):** add real authentication to `/admin/*` before this server (or its production successor) is reachable from anything beyond a fully trusted network. *Files:* `server.py`. *Test:* confirm every `/admin/*` route now rejects an unauthenticated request; confirm existing legitimate operator workflows still function with credentials supplied.
4. **RISK-04 (silent data loss at flash-full):** add a persisted, remotely-visible counter/alarm for failed queue writes, distinct from the existing soft `QUEUE_HIGH`/`QUEUE_CRITICAL` thresholds. *Files:* `queue.h`, `diagnostics.h`. *Test:* fill a test unit's queue partition deliberately (bench-only, isolated test device) and confirm the new alarm fires before data loss becomes silent.
5. **RISK-05 (OTA rollback never proven):** run one real successful OTA and one real forced-bad-image rollback on a bench unit. *No code change required if the mechanism already works* — this is a test-execution gap, not necessarily a code gap. *Test:* both scenarios, with before/after `/api/v1/info` and Serial Monitor evidence captured.

## P1 — before a customer pilot (beyond a fully supervised bench/lab context)

6. RISK-06: add "last error"/retry-count/quarantine-count visibility to `/api/v1/metrics` and the server-side device twin.
7. RISK-07: fix or explicitly document the K-factor retroactivity behavior in the reference dashboard before it is used for anything billing-adjacent.
8. RISK-08: require explicit customer sign-off on the physical-access/factory-reset risk profile for the specific plant environment; consider adding a device-side "I was just reset" announcement.
9. RISK-09: add replay protection (nonce/timestamp) to config-write and admin endpoints.

## P2 — before fleet scale

10. RISK-10: identify the real flash part, get/estimate its rated endurance, and either confirm multi-year safety or reduce checkpoint-write frequency.
11. RISK-11: get an explicit business decision on whether offline event-time precision matters; implement RTC/NTP-on-reconnect if so.
12. RISK-12: add explicit task-watchdog configuration and persisted restart-reason/count history.
13. RISK-14: add OTA poll jitter and staged/per-device manifest targeting before rolling OTA out across more than one or two units at a time.

## P3 — hardening

14. RISK-13: harden the ack_seq response parser against a pathological response body.
15. General: consider an automated, exhaustive secret-scanner run (this audit's secret search was a manual review of files actually read, not an automated full-repository/full-binary sweep).

## Acceptance criteria for "P0 clear, cleared for a supervised pilot"

All five P0 items above have either a real fix with a passing test, or (for RISK-05 specifically) real, executed, evidence-backed test results — not a code review alone. A supervised pilot should still operate under the constraints in `13_PLANT_INSTALLATION_RUNBOOK.md` even after P0 clearance, since several P1/P2 items materially affect remote diagnosability and long-term reliability.
