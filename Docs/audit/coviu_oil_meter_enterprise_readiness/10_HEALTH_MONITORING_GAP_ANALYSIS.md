# 10 — Health Monitoring Gap Analysis

Full field-by-field table is in `03_ENTERPRISE_CHECKLIST_15_SECTIONS.md` §13. This file summarizes the gaps that matter most operationally for a plant deployment.

## The gaps that most affect an operator's ability to diagnose a struggling unit remotely

1. **No "last error" message/code is ever persisted or exposed** — only the raw last HTTP status code. An operator sees `last_push_http_code: 401` but has to guess why (revoked key? wrong key? never provisioned?) rather than reading a distinguishing error code.
2. **No retry count, no corrupt-record count, no quarantined-record count** are exposed anywhere the Device Manager or an operator can see remotely. A device silently accumulating quarantined records (RISK-01) would show **no distinct symptom at all** beyond a growing `queue.backlog` that looks identical to an ordinary connectivity outage — an operator has no way to distinguish "temporarily offline, will catch up" from "permanently stuck behind a poison record" without direct database access to the server's `quarantined_records` table (which has no dedicated API endpoint either).
3. **No restart count, brownout count, or watchdog-reset count is accumulated** — only the current boot's single reset reason. A device stuck in a slow reboot loop over days would show a healthy-looking `/api/v1/status` each time it's checked (uptime just resets), with nothing accumulating to reveal the pattern.
4. **No calibration/config version is exposed via the local API at all.**
5. **No distinct "rolled back" OTA state** — a device that just self-healed from a bad update looks identical to a device that has simply never attempted one.

## What is genuinely good here (worth stating plainly, not just cataloguing gaps)

- The alarm thresholds that do exist (`QUEUE_HIGH`/`QUEUE_CRITICAL`/`OTA_FAILED`/`SD_REMOVED`) are real, correctly implemented, and confirmed live on the connected device (`health_state: "ok"`, no alarms currently active, consistent with its real low backlog of 3).
- The `/api/v1/logs` endpoint honestly returns `501 NOT_IMPLEMENTED` rather than fabricating log data — a real, verified instance of the project's own stated "never fabricate" discipline (`local_api.h:147-151`).
- `/api/v1/metrics` is deliberately a separate, deeper endpoint from `/api/v1/status`, a sound design distinguishing "is it OK" from "why" — confirmed by reading both builders in `diagnostics.h`.
- Health telemetry does not block measurement — the two are independently timed within the same single-threaded loop, confirmed by code structure (though a genuine slow-loris-style local-API client stress test was not performed against real hardware).

## Severity and deployment gate
**P2** collectively. None of these gaps alone blocks a short, closely-supervised pilot where an engineer can inspect the server's database directly if something looks wrong — but they meaningfully limit unattended, at-scale fleet operation, and the "last error"/"quarantine visibility" gaps interact directly with RISK-01 (see `11_RISK_REGISTER.md`), making that specific defect harder to detect in the field than it should be.
