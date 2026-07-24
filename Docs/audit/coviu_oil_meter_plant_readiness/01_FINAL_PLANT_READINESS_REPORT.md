# 01 — Final Plant Readiness Report

Companion documents: `02_RELEASE_ARTIFACT_MANIFEST.md`,
`03_REGRESSION_AND_BASELINE_EVIDENCE.md`, `04_USB_RECOVERY_RUNBOOK.md`,
`05_PLANT_INSTALLATION_CHECKLIST.md`,
`06_OPERATOR_QUICK_RESPONSE_GUIDE.md`,
`07_FIRST_FLOW_RECONCILIATION_SHEET.md`,
`08_FIRST_SHIFT_MONITORING_SHEET.md`, `09_KNOWN_LIMITATIONS.md`.

## Accepted prior state (unchanged, not softened)

`OTA CONFIRMATION AND ANTI-DOWNGRADE CERTIFIED` and
`OTA RESILIENCE DESIGN CHANGE REQUIRED` (docs 36/37 of the
`coviu_oil_meter_p0_remediation_phase2` series) — both accepted exactly
as issued.

## Release candidate frozen (doc 02)

Commit `856972ef6106d662c5f8a7f5b71c9a60ce40edc1`, clean tree, esp32dev
environment, `firmware.bin` SHA-256
`136ef36ccf3d1885bb78a17473ee68d47f65e83318e6aff7e5fc9953f0fb2031`, 96/96
existing tests re-passed, flashed to the bench device with full esptool
hash verification, identity independently confirmed live
(`build_commit`/`build_dirty` match exactly).

---

# Deployment Classification

## Level 1 — Controlled Plant Pilot

Requires: physical accessibility, a trained person with USB present,
recovery equipment onsite, restricted OTA, close first-run observation,
manual-process fallback available.

**Assessment: firmware/data-integrity prerequisites are met (see Go/No-Go
below). Physical/site prerequisites are NOT yet confirmed (network,
sensor, recovery-kit-onsite are all "ready in principle, not yet
executed at the actual site").**

## Level 2 — Normal Plant Production

Requires everything in Level 1, **plus**: real sensor flow proven,
calibration within tolerance, at least one full production cycle
reconciled, clean first-shift monitoring.

**Assessment: NOT YET CERTIFIED.** None of Level 2's additional
requirements can be met without a physically present person and real oil
flow — explicitly out of scope for this phase, per the mandate itself.

## Level 3 — Remote Unattended Production

Requires no technician physically present, remote OTA, automatic
recovery from a bad candidate, no extended downtime tolerance.

**Assessment: NO-GO, structurally.** Doc 37 proved the bootloader cannot
automatically roll back a bad-but-authentic candidate on this hardware.
Remote OTA without a physically present recovery option is unsafe until
either genuine bootloader rollback is restored (a framework/toolchain
decision, explicitly out of this phase's scope) or the business formally
accepts that a bad OTA can require onsite USB recovery, with an
operational plan for that acceptance.

---

# Part 4 — Plant Network Readiness

Performed against the closest available pre-deployment equivalent (this
session's own bench LAN), since the actual plant network is not
accessible from here:

| Item | Status |
|---|---|
| Device can reconnect after reboot | **Proven** — every reboot this remediation chain (dozens) showed automatic WiFi/server reconnection |
| Queued records sync after connectivity returns, no duplicates | **Proven repeatedly** — zero duplicates observed at any checkpoint, this entire audit trail |
| Transient loss does not stop local counting | **Proven by design** — PCNT hardware counting is independent of network state; queue buffers offline (doc 34/36/37 evidence) |
| TLS/transport security configured as designed | **Partially** — `server_url` is `http://` by design for the bench stub (`certs.h`'s own comment: "http:// remains ONLY so the bench stub keeps working"); a **production** endpoint must be `https://` with the pinned CA, which is not yet provisioned (see doc 09 §3) |
| System clock / server time source available | **Proven working** post-fix (doc 35) — `server_time_ms` correctly parsed, OTA time-source gate functions |
| Plant SSID / credentials provisioned securely | **NOT VERIFIED — onsite-only.** This device's WiFi credentials are the bench network's own (redacted throughout this audit trail); plant credentials must be provisioned via AP-mode setup or the serial console before/during plant install |
| DHCP/static-IP expectations documented | **NOT DOCUMENTED for the plant** — this bench setup uses DHCP-assigned addressing on the bench LAN; plant IP addressing scheme is unknown to this report |
| DNS resolution where required | **NOT APPLICABLE on the bench** (server accessed by raw IP); if the plant endpoint uses a hostname, DNS resolution must be separately verified onsite |
| Firewall/router rules won't block required traffic | **NOT VERIFIED — onsite-only** |
| Endpoint not accidentally pointed at the test bench | **Explicit stop condition for deployment** — the device currently has `server_url: http://192.168.1.3:8000` (the bench address) baked into its NVS from this entire test session. **This MUST be reprovisioned to the real plant/production endpoint before deployment** — this is not optional and is the single most important pre-deployment configuration step. |

**Recorded as an operational risk, per the mandate's own instruction**: a
full plant-network validation has not been completed and **must** occur
onsite before production starts, particularly the server-endpoint
reprovisioning above.

## Part 5 — Sensor and Totalizer Readiness

See `09_KNOWN_LIMITATIONS.md` §2 and `07_FIRST_FLOW_RECONCILIATION_SHEET.md`.
**Not performed this phase, by explicit design** (requires a physically
present person). Until completed, any deployment is, at most, a
**supervised pilot with mandatory manual cross-checking** — never a
declaration of measurement accuracy.

## Part 6 — Operational Failure Modes

See `06_OPERATOR_QUICK_RESPONSE_GUIDE.md` — every listed failure mode has
a concrete first response, an explicit stop/continue rule, and an
explicit USB-required determination. None reduce to "contact developer"
alone.

## Part 7 — OTA Operational Policy (mandatory, given the rollback limitation)

Given doc 37's proof that bootloader automatic rollback is unavailable,
the following controls are **required**, not optional, for any pilot
deployment:

1. **No unsupervised OTA updates.** A human must be present and aware
   whenever an OTA is served.
2. **No OTA during active oil transfer/production.** Only during a
   declared maintenance window.
3. Every OTA candidate must have, before being served: clean commit
   identity, firmware SHA-256, valid signature (independently verified —
   this exact process, docs 33-37, is proven and repeatable), a stated
   security version, a test report, and an approved-deployment record
   (who approved it, when).
4. **The USB recovery laptop must be physically present during any OTA**
   attempt — not "available within an hour," physically there.
5. USB cable/driver verified working **before** the OTA window starts,
   not discovered broken after a bad candidate installs.
6. The current known-good firmware (this release, `856972e`) must be
   stored locally on the recovery laptop, hash-verified, ready to flash
   immediately.
7. Configuration/recovery commands are documented (doc 04) and must be
   printed or otherwise available without needing network access to
   fetch them.
8. Post-OTA checks (doc 04 §5) must complete and pass before production
   resumes.
9. **A failed post-OTA health check requires immediate USB recovery** —
   not a wait-and-see period, given no automatic bootloader safety net
   exists.
10. **Whether OTA can be administratively disabled/hidden during the
    pilot**: not verified this phase — the firmware has no
    operator-facing "disable OTA" switch; the only real control is
    procedural (who has access to place a manifest on the server) and
    physical (restricting who can reach the OTA server at all). This
    must be enforced by access control and procedure, not firmware, and
    is flagged here explicitly rather than assumed handled.

## Part 8 — USB Recovery Drill

Complete dry run performed this session (doc 04): COM-port
identification procedure confirmed against the actual bench unit's real
USB identity, release artifact hash independently re-verified, exact
flash command documented, post-flash checks specified. **No additional
physical recovery flash was performed** — the identical command has
already been executed and esptool-hash-verified 6 times this remediation
chain; repeating it again would not add evidence.

---

# Go/No-Go Decision Rules Applied

## Controlled Plant Pilot

| Required condition | Met? |
|---|---|
| Exact candidate identity frozen | **Yes** (doc 02) |
| Tests green | **Yes** (doc 03 — 96/96 executed, compile-clean, extensive hardware proof) |
| Device health green | **Yes** (doc 03 baseline — `health_state:"ok"`, zero alarms) |
| Queue and database reconciled | **Yes** (doc 03 — fully contiguous, zero quarantined, throughout) |
| Plant network works or onsite setup is ready | **NOT YET** — bench-proven mechanisms only; actual plant SSID/endpoint/firewall unverified, and the bench endpoint must be reprovisioned before deployment |
| USB recovery equipment onsite | **Runbook and artifact ready; physical onsite presence not this report's to confirm** |
| Manual fallback process exists | **Assumed exists (pre-existing manual process); not documented by this report** |
| A responsible person physically present | **Onsite requirement, not verifiable remotely** |
| OTA disabled or administratively restricted | **Procedural/physical control only — no firmware switch (Part 7 item 10)** |
| First oil flow supervised | **Onsite requirement** |
| Sensor accuracy manually cross-checked | **Plan exists (doc 07); not yet executed** |
| Known bootloader rollback limitation accepted | **This report states it plainly (doc 09 §1); business acceptance is not this report's to grant** |

**Several required conditions are genuine open items, not failures** —
they require actions only completable at or immediately before the
physical site, by the deploying team, not by this remote engineering
session.

## Normal Plant Production

Requires real sensor flow proven, calibration within tolerance, one full
reconciled production cycle, clean first-shift monitoring, no
release-blocking alarms. **None of the sensor/production-cycle items are
met.** → **NOT YET CERTIFIED.**

## Remote Unattended Production

Requires genuine bootloader rollback OR formal business acceptance of
onsite-USB-recovery risk for a bad authentic OTA. **Neither is in place.**
→ **NO-GO**, per the mandate's own explicit default.

---

# Mandatory Stop Conditions (restated for the plant team, unchanged from the mandate)

Stop production and revert to the manual process if: pulses don't move
during real flow; pulses move during confirmed no-flow; measured error
exceeds approved tolerance; totalizer decreases; device/server totals
diverge without explanation; records duplicate; sequence gaps appear;
queue can't drain after connectivity returns; boot loop occurs; repeated
unknown resets occur; sensor wiring is unstable; device identity differs
from the approved release; storage initialization fails; NVS unexpectedly
resets; accepted security floor changes incorrectly; an OTA starts
unexpectedly; recovery equipment is unavailable when required.

---

# Final Verdicts

**`CONTROLLED PLANT PILOT — CONDITIONAL GO`**

Conditions that must be closed onsite before oil actually flows through
the device, all listed explicitly rather than left implicit:

1. Reprovision `server_url` away from the bench address
   (`http://192.168.1.3:8000`) to the real plant/production endpoint —
   **the single highest-priority item**.
2. Provision the plant's real Wi-Fi credentials.
3. Confirm plant network reachability, firewall rules, and (if
   applicable) DNS for the production endpoint.
4. Have the USB recovery kit (doc 04) physically onsite, cable/driver
   pre-verified.
5. Have a responsible, trained person physically present for the entire
   first run.
6. Complete the sensor/totalizer readiness test (doc 07) — supervised,
   manually cross-checked, before trusting any device-reported quantity.
7. Business owner to explicitly acknowledge the bootloader-rollback
   limitation (doc 09 §1) and the dev-build/test-key status (doc 09 §3)
   in writing before go-live.
8. Restrict/control who can reach the OTA server, per Part 7 item 10,
   since no firmware-level OTA-disable switch exists.

**`NORMAL PRODUCTION: NOT YET CERTIFIED`**
**`REMOTE UNATTENDED OTA: NO-GO`**
**`REAL SENSOR ACCURACY: NOT YET CERTIFIED`**

Not `PRODUCTION HARDWARE CERTIFIED` — real sensor flow and calibration
have not been completed, exactly as this mandate requires stating.
