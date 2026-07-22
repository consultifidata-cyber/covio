# 00 — Executive Verdict

## Final Verdict: **NOT ENTERPRISE READY — DO NOT INSTALL IN CUSTOMER PLANT**

(with the caveat that several required tests additionally remain **CERTIFICATION INCOMPLETE** because no hardware fault-injection capability — relay, pulse generator — exists in this audit environment; both things are true at once and are not in tension: real, code-provable P0 defects were found independent of any hardware limitation, AND several mandate-required hardware tests were never executable here.)

## Executive Summary

This audit inspected the full Coviu oil-flow-meter codebase (firmware, bench server, Windows desktop app) and, for the first time in this project's recorded history, was able to **actually execute** real verification: three firmware compiles, 90 automated tests, and live read-only queries against a genuinely, physically connected ESP32-S3 device. This directly overturns the repository's own prior self-audit (`Docs/RE9_END_TO_END_VALIDATION_PLAN.md`/`RE10_FINAL_PRODUCTION_READINESS_REPORT.md`), which had correctly and honestly reported that no compiler, Python interpreter, or hardware had ever been available to verify anything beyond static review — that constraint no longer holds in this session's environment.

**What was inspected:** every firmware header, the bench server, the desktop app, and the project's own prior audit/architecture documentation (found to be substantially, though not entirely, out of date — the "SD card" architecture the root `ARCHITECTURE.md` describes was replaced by internal LittleFS flash storage some time ago; the Live Readiness Plan's "planned" local API and WiFi-provisioning features turned out to be already implemented and running on the live device).

**What was physically tested:** three real firmware compiles (two succeeded, one failed exactly as designed by a security gate), 90 real automated tests (all passing), one passive serial capture (during which the device underwent one real, incidental soft reset and recovered correctly), and two real read-only HTTP queries to the connected device's local diagnostics API.

**What could not be tested:** any power-cut, brownout, or randomized-fault-injection campaign (no relay or controlled power supply exists in this environment); any real OTA upgrade or forced-rollback test; any 30-day (or accelerated-equivalent) soak test; flash-chip-specific wear-endurance measurement; Secure Boot/flash-encryption eFuse state.

**Is the connected firmware the same as the repository?** Yes for the running binary's logical behavior (confirmed via its own reported `fw_version`/`model` and by successfully rebuilding the same source and observing matching flash/RAM usage), though this audit did not byte-for-byte diff the running binary against a freshly built one.

**Is data loss possible?** In two specific, real, code-provable ways: (1) a permanently-quarantined record can freeze local queue-pruning forever for that device (RISK-01) — the server keeps the data safely, but the device's own local copy never gets pruned and grows unbounded; (2) at true physical flash-full capacity, a failed write is silently lost with no distinct alarm (RISK-04). Beyond these two specific, real findings, the general power-loss/torn-write handling is sound **by code inspection**, but has never been proven under an actual power-cut or brownout on this or any recorded prior session.

**Are duplicates possible?** At the transport layer, yes, routinely and by design (at-least-once delivery) — but they can never become duplicate *stored* business records, because the server enforces a real database-level uniqueness constraint (`PRIMARY KEY(device_id,seq)`), confirmed directly in the schema, not merely assumed from application logic.

**Actual offline buffer duration:** roughly 5.5 hours at a 1-sample/second cadence before a soft alert threshold, and roughly 26 hours before true physical exhaustion — both figures depend on an unverified assumption about the exact LittleFS partition size (a code comment states ~3.4MB; this was not independently confirmed against the literal partition table). **Any claim of "several days" of offline buffering should not be relied upon without confirming the real sampling cadence and partition size first.**

**Does Coviu Device Manager (the local Electron app) support required remote management?** Substantially yes — real, tested code exists for discovery, status/health monitoring, provisioning, and factory-test workflows (44/44 real passing tests) — but it was not interactively exercised against the live connected device in this audit, and several fields an operator would want (last-error detail, retry counts, quarantine visibility, calibration version) are not exposed by the device's local API for it to display.

**Is OTA rollback proven?** No. The mechanism is soundly designed by inspection (correct, standard ESP-IDF primitives, correctly gated on proof of post-update connectivity), but has never been exercised with a real bad image on real hardware, in this or any recorded prior session — independently re-confirmed as the single highest-priority open test.

**Can the device be installed now?** Not for an unsupervised customer plant. A closely supervised pilot is conceivable only after the five P0 items in the risk register are addressed, per the runbook in `13_PLANT_INSTALLATION_RUNBOOK.md`, and even then several P1/P2 gaps materially limit remote diagnosability and long-term (multi-year, unattended) confidence.

## Fifteen-Section Scorecard

| # | Area | Verdict | Strongest Evidence | Main Gap | Deployment Blocker |
|---|---|---|---|---|---|
| 1 | Data Persistence | PASS (design) / PARTIAL (capacity) | Real 36-byte QRow, real dual-slot CRC checkpoints, real recovery across an observed reset | Partition size unverified; silent data loss at true flash-full (RISK-04) | Yes |
| 2 | Flash Memory Reliability | NOT PROVEN | Write-frequency table fully derived from real code | Real flash-chip endurance and LittleFS wear-leveling behavior both unmeasured | Only for unattended multi-year service |
| 3 | Power Failure Recovery | PARTIAL | One real reset recovered correctly; every torn-write path sound by inspection | No real power-cut/brownout test ever performed | Yes |
| 4 | Guaranteed Delivery | PASS + 1 real defect | Real DB `PRIMARY KEY(device_id,seq)` constraint | RISK-01 ack-gap stall | Yes (for RISK-01) |
| 5 | Offline Sync Engine | PARTIAL | Real, correct exponential-backoff+jitter WiFi reconnect | "Several days" offline claim unverified; OTA download timeout unbounded | Depends on required SLA |
| 6 | Queue Management | PARTIAL (1 P0 defect) | Single-threaded loop makes multi-worker races structurally impossible | RISK-01 | Yes |
| 7 | Acknowledgement Mechanism | PASS | Invariant 6 directly re-verified against current code | Naive JSON parsing (P3) | No |
| 8 | Configuration Management | PARTIAL | AP-mode-only config writes verified in code, not just docs | K-factor retroactivity (RISK-07); unauthenticated admin endpoints | Yes (conditional) |
| 9 | Firmware Updates (OTA) | PARTIAL | Real rollback-gate code, correctly wired; release-build CA guard directly demonstrated | Real OTA + rollback never demonstrated (RISK-05) | Yes |
| 10 | Watchdog / Self-Healing | PARTIAL / NOT PROVEN | Filesystem-mount-failure fail-loud behavior confirmed in code | No watchdog config found; no crash-loop detection | Combined with #3 |
| 11 | Time Accuracy | ABSENT (by design) | No RTC/NTP anywhere, confirmed by repo-wide grep | Offline event-time precision inadequate if ever required | Depends on business requirement |
| 12 | Security | PARTIAL | Real TLS/CA-pinning, real device auth, real release-build guard demonstrated | Hardcoded live WiFi credential (RISK-02); unauthenticated admin endpoints (RISK-03) | Yes |
| 13 | Diagnostics/Health | PARTIAL | Real, correct alarm thresholds confirmed live on the connected device | No last-error/retry/quarantine visibility | No, but limits diagnosability |
| 14 | Factory Reset/Recovery | PARTIAL | Confirmed: factory reset preserves unsynced queue/totalizer data (a real positive finding) | No auth on reset; no reset-event audit trail | Conditional |
| 15 | Failure Scenario Matrix | See `04_FAILURE_INJECTION_RESULTS.md` | 24/45 scenarios PASS on code+evidence | 14/45 require hardware fault injection never performed | See matrix |

## Acceptance-Gate Results

| Gate | Result | Evidence | Blocking Issue |
|---|---|---|---|
| A — Measurement Integrity | PARTIAL | PCNT hardware counting sound; calibration versioned per-record | No sensor-fault/implausible-rate detection; dashboard K-factor retroactivity (RISK-07) |
| B — Durable Persistence | PARTIAL | Real dual-slot CRC, real recovery across one reset | RISK-04 silent loss at flash-full; capacity vs. offline-SLA unconfirmed |
| C — Idempotent Delivery | PARTIAL | Real DB unique constraint, confirmed | RISK-01 ack-gap stall |
| D — Recovery | NOT PROVEN | Sound by code; one real soft-reset recovery observed | No real power-cut/brownout/OTA-rollback test ever performed |
| E — Fleet Management | PARTIAL | Real per-device key issuance/rotation/revocation, tested | Unauthenticated admin endpoints (RISK-03); no replay protection (RISK-09) |
| F — Safe OTA | FAIL | Rollback mechanism sound by inspection | Never demonstrated on hardware (RISK-05); no artifact signing |
| G — Security | FAIL | Real TLS/auth/release-gate all directly demonstrated | Hardcoded live credential (RISK-02); unauthenticated admin (RISK-03) |
| H — Observability | PARTIAL | Real, correct alarm thresholds | No last-error/retry/quarantine/restart-count visibility |
| I — Auditability | PARTIAL | Real server-side event log (device_events), tested | No on-device audit trail for config/reset actions |
| J — Stability | NOT PROVEN | 90 real passing automated tests | No genuine 30-day soak test exists or was run |

**A single unresolved P0 automatically requires NO-GO per the mandate's own rule — and this audit found five (RISK-01 through RISK-05). Combined with two failed gates (F, G) and three not-proven gates (D, J, and effectively F again), the verdict is unambiguous.**

## Reconciliation Results
See `05_DEVICE_SERVER_RECONCILIATION.md` in full. Summary: the one real event observed this session (a single incidental soft reset) reconciled cleanly with no unexplained gap or duplicate. No controlled pulse-generation reconciliation campaign was run (no pulse generator exists in this environment) — fabricating one would misrepresent evidence that does not exist.

## Final Plant Recommendation
- **One device may be installed only as a supervised pilot**, after the five P0 remediation items are complete, per `13_PLANT_INSTALLATION_RUNBOOK.md`.
- **Maximum permitted offline duration:** not yet confirmable — depends on an unverified partition-size assumption; get this measured before setting any customer-facing SLA.
- **Required monitoring:** per the runbook — queue backlog trend, health alarms, and (until fixed) manual checks of the server's quarantine table.
- **Required rollback/replacement procedure:** physical USB reflash, since OTA rollback is unproven; keep a spare provisioned unit ready.
- **This device must not control billing, inventory, or compliance records as an authoritative source** until RISK-01 and RISK-07 are resolved.
- **Remaining risks the business would be accepting** by piloting despite this verdict are enumerated in full in `11_RISK_REGISTER.md`.

## Honest Evidence Boundary

**PROVEN ON THE CONNECTED DEVICE:**
- The device is a real ESP32-S3 running firmware 1.0.0, pointed at a bench/dev endpoint with the default API key still active.
- Its totalizer and queue-ack cursor correctly recovered across one real, incidental soft reset.
- Its local HTTP API (`/api/v1/info`, `/api/v1/status`) is real and returns correct, live data via genuine mDNS discovery and HTTP requests.
- A real sync cycle completed against its configured server during this session.

**PROVEN BY CODE/SERVER INSPECTION ONLY (not exercised on hardware this session):**
- Torn-write CRC handling, dual-slot checkpoint fallback, orphan-segment cleanup, filesystem-mount-failure fail-loud behavior.
- The database's idempotency constraint and the server's per-record quarantine logic.
- The OTA rollback gate's wiring and logic.
- The release-build security guards (one of which — the CA-cert guard — was additionally *directly demonstrated* by deliberately triggering it this session, which is a stronger form of evidence than inspection alone).

**NOT PROVEN / REQUIRES FURTHER TESTING:**
- Any behavior under an actual power cut, brownout, or randomized fault-injection campaign.
- A real OTA upgrade and a real forced-bad-image rollback.
- Real flash-chip erase-cycle endurance and LittleFS's actual wear-leveling behavior for this access pattern.
- Secure Boot / flash-encryption eFuse state on the physical unit.
- A genuine 30-day (or rigorously justified accelerated-equivalent) stability soak test.
- Interactive use of the Coviu Device Manager desktop app against the live device.
