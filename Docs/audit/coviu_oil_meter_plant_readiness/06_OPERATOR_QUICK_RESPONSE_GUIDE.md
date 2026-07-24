# 06 — Operator Quick-Response Guide

For every failure mode: what the operator sees, whether measurement can
continue, whether records queue locally, when production must stop, who
to contact, how recovery is performed, whether USB access is required.
**"Contact developer" is never the sole instruction** — each entry has a
concrete first step an onsite operator/supervisor can take.

| Failure mode | What operator sees | Measurement continues? | Queues locally? | Stop production? | Recovery | USB needed? |
|---|---|---|---|---|---|---|
| WiFi unavailable | Dashboard/API unreachable; device itself keeps running | **Yes** — pulses/totalizer are hardware-counted (PCNT), independent of network | **Yes** — queue buffers offline (proven, doc 34/36/37: zero data loss across every disconnect this audit trail) | No — this is exactly the designed offline-safe mode | Restore WiFi/router; device reconnects and drains queue automatically | No |
| Server unavailable | `last_push_http_code` stops updating / goes stale; queue backlog grows | **Yes** | **Yes** | No, unless backlog approaches storage capacity (see below) | Restart/repair the server; device resumes syncing automatically once reachable | No |
| Queue increasing | `queue.backlog` rising in `/api/v1/status` | Yes | Yes (that's what's happening) | No, monitor `capacity_pct_used` | Resolve the underlying WiFi/server issue above | No |
| Queue full / storage warning | `capacity_pct_used` approaching 100%, or a `QUEUE_HIGH`/`QUEUE_CRITICAL`-class alarm in `/api/v1/health` | Yes, until storage genuinely exhausts | Degrading | **Yes, if storage-critical** — contact support before capacity is fully exhausted | Restore connectivity urgently to drain backlog; if truly exhausted, escalate for a data-recovery plan before continuing | No (unless a physical storage fault is suspected) |
| Sensor disconnected | `pulse_frequency_hz` reads `0.000` and totalizer stops advancing during known real flow | **No — real measurement stops** | Yes (whatever was already queued) | **Yes — stop oil transfer, revert to manual metering** | Physically inspect and reconnect sensor wiring | No |
| Pulse value frozen | Totalizer unchanged despite confirmed physical flow | **No** | Yes | **Yes** | Same as sensor-disconnected above; also check `PIN_PULSE` wiring/opto-isolator | No |
| Abnormal pulse spike | Totalizer jumps far beyond plausible flow rate | Measurement suspect | Yes | **Yes — verify before trusting the reading** | Inspect for electrical noise/wiring fault; cross-check against manual reading | No |
| Device reboot | `boot_id` increments unexpectedly; brief gap in live readings | Resumes automatically | Yes — totalizer/queue survive reboot by design (proven repeatedly) | No, if isolated and reset_reason is benign (`power_on`/`software`) | None — self-recovers | No |
| Repeated resets | `boot_id` incrementing rapidly / boot loop | **No** | Uncertain | **Yes — stop and investigate** | Escalate; do not leave unattended | Likely yes |
| Health state degraded | `/api/v1/health` → `health_state:"degraded"`, check `alarms` array for the specific reason | Depends on the alarm | Depends | **Yes, until the specific alarm is understood** | Read the alarm's `message`/`type`; act on the specific cause above | Depends |
| OTA failure alarm | `alarms` includes `OTA_FAILED` (proven real behavior, doc 34) | Yes — a failed OTA never affects the currently-running, already-proven firmware | Yes | No — the device is still running its last-known-good build | Review why the OTA failed before retrying; do NOT retry the same candidate blindly | No, unless retry also fails |
| Wrong build identity | `/api/v1/info`'s `build_commit` does not match the approved release (`856972ef6106d662c5f8a7f5b71c9a60ce40edc1`) | Uncertain — treat as unverified | Yes | **Yes — stop until identity is confirmed correct** | Reflash the approved release (doc 04) | **Yes** |
| Security-floor mismatch | `accepted_security_floor` unexpectedly lower than previously observed | Should not happen (floor is monotonic, proven) | Yes | **Yes — stop, this indicates NVS corruption or tampering** | Escalate immediately, do not attempt self-recovery | Likely yes |
| Accidental downgrade attempt | `last_reject_reason:"downgrade_rejected"` appears | Yes — correctly rejected, no action needed | Yes | No — this is the anti-downgrade gate working as designed (proven, doc 36) | None required; note who attempted the downgrade and why | No |
| Device unreachable | No response from `/api/v1/*` or the dashboard | Device may still be counting hardware pulses locally | Yes, if the device itself is merely off-network | **Yes, if physical access confirms it is actually down (not just network)** | Physically check power/connectivity; escalate if unresolved | Possibly |
| Corrupted configuration | Unexpected `server_url`/Wi-Fi behavior, device won't reconnect as expected | Uncertain | Uncertain | **Yes** | Reprovision via the serial console (`set url`, `set wifi`) or AP-mode provisioning; escalate if this doesn't resolve it | Yes (serial console) |
| Power loss during operation | Device off; resumes on power restore | No, while powered off | Queue/totalizer preserved across power loss (NVS/LittleFS, not RAM-only) | No, once power and health are confirmed restored | Confirm clean boot, no alarms, queue draining | No |

## Escalation

Named contact: **not populated by this report** — the business must
designate a specific, reachable person/role before deployment (see also
doc 04's same open item). This guide deliberately does not invent one.
