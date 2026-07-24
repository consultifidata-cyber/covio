# 01 — Pilot Activation Verdict

Companion documents `02` through `12` in this same directory. Continues
from `Docs/audit/coviu_oil_meter_enterprise_readiness_final/` without
repeating the full audit, per instruction.

## What this phase closed, with real evidence

1. **Serial credential exposure — CLOSED.** `provision.h`'s `show`
   command no longer prints the raw API key. Fixed, tested (96/96
   regression + 6 new native tests), built, committed
   (`443dc448423b9fec2a48fb4bf7ac02740e6c2341`), flashed via the
   established hash-verified USB method, and **proven live** — a real
   `show` command against the actual device now returns
   `api_key : default (fingerprint=57c8139db729)`, never the raw value.
2. **Offline/reconnect resilience — re-confirmed with a fresh,
   deliberate 5-minute test.** Zero data loss, exact reconciliation
   (`58,732 = 58,730 + 2 + 0`).
3. **Recovery kit — re-verified with a REAL flash this session**, not
   just a dry run, using the exact laptop/cable/command a plant recovery
   would use.
4. **Bench-address occurrences — fully enumerated and classified.**
   Exactly one real, active-runtime dependency exists (the device's own
   `server_url`); everything else is test-only or documentation-only.

## What this phase correctly stopped at, rather than guessing

1. **Plant endpoint/credential/device-ID/plant-ID reprovisioning** — no
   real values were available; nothing was invented. Doc 04 lists
   exactly what's needed.
2. **Real plant-server registration proof** — no real plant server
   exists yet to test against; doc 05 states this plainly rather than
   substituting bench-server proof for it.
3. **Real sensor/calibration validation** — requires a physically
   present person, real sensor hardware, real oil, and a calibrated
   reference; none available remotely. Doc 08 provides the complete
   procedure, unperformed.

## Final Verdicts

### Device as configured, right now

**`DEVICE AS CONFIGURED — PLANT NO-GO`**

The device's `server_url` still reads the bench address
(`http://192.168.1.3:8000`) — per the mandatory No-Go conditions
(restated below), this alone is sufficient. This is a correctable,
well-documented, physically-quick reprovisioning step (doc 04), not an
architectural defect, but it has not been performed, and this device in
its CURRENT state must not go to a plant.

### Controlled Plant Pilot

**`CONTROLLED PLANT PILOT — CONDITIONAL GO`**

Conditional on, in order of priority: (1) completing the endpoint/
credential reprovisioning (doc 04) with real, approved plant values;
(2) completing the sensor validation procedure (doc 08) with a
physically present operator and supervisor; (3) the operating
restrictions in doc 10 being genuinely observed for the full pilot
duration; (4) the recovery kit (doc 07) and a named escalation contact
being physically present for the entire window, not just during OTA.

### Individual checklist verdicts

```
PLANT ENDPOINT:                  NOT VERIFIED
PLANT SERVER REGISTRATION:       NOT VERIFIED
SERIAL SECRET EXPOSURE:          CLOSED
REAL SENSOR FLOW:                NOT CERTIFIED
CALIBRATION:                     NOT CERTIFIED
RECOVERY KIT:                    VERIFIED
NORMAL PRODUCTION:               NOT YET CERTIFIED
REMOTE UNATTENDED OPERATION:     NO-GO
REMOTE UNATTENDED OTA:           NO-GO
PRODUCTION COMMUNICATION SECURITY: NOT CERTIFIED
```

## Mandatory No-Go Conditions — checked against this phase's own fresh evidence

| Condition | Status |
|---|---|
| Runtime endpoint still the bench URL | **TRUE — triggers NO-GO** |
| Device ID or plant ID wrong | Device ID correct; plant ID doesn't exist as a concept (needs a business decision, not just reprovisioning) |
| API key visible through serial output | **FALSE now — closed this phase** |
| Plant server cannot authenticate the device | Cannot be evaluated — no real plant server exists yet |
| Records land in wrong plant | No plant concept exists — cannot be wrong, but also cannot be right |
| Sensor does not generate pulses during real flow | Cannot be evaluated — not tested |
| Pulses continue during confirmed no-flow | Not observed (also not tested with real flow) |
| Calibration exceeds tolerance | Cannot be evaluated — not tested |
| Recovery kit unavailable | Verified available and freshly re-proven this phase |
| Queue/server reconciliation fails | **Balances exactly, this phase's own fresh test** |
| Unexplained sequence gaps | **None found** |
| Duplicate database rows | **None found, structurally prevented** |
| Flashed identity differs from approved artifact | **Matches exactly** (this phase's own new approved artifact) |
| Manual fallback unavailable | Assumed to exist (pre-existing business process), not independently confirmed by this engineering audit |

**Because the endpoint condition remains triggered, this report does not
soften the device's own verdict to a conditional pass — it is NO-GO
until reprovisioned, full stop.** The pilot-level conditional GO reflects
that everything else needed for a successful, safe pilot is either
already proven or has a clear, documented, executable path — not that
the device may go as-is.

## Cleanup

All temporary test processes stopped: bench server (tasks `b4czjvy27`,
`be47411i1`, stopped this phase) — nothing left running.
