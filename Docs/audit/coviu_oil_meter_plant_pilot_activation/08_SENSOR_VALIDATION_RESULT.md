# 08 — Real Sensor Validation (Part 10) — STOPPED, requires a physically present person

**Not performed this session, for the same reason stated in every prior
phase of this audit trail: this session has no physical access to the
device's sensor, no ability to induce real or simulated flow, and no
calibrated reference container.** No pulse simulation was substituted
for real evidence.

`totalizer_raw_pulses` has read `401` — completely unchanged — across
this entire multi-day, multi-session, multi-phase audit trail, including
throughout this exact phase's own flash and outage tests.

## The 20-item procedure (from the mandate), to be executed onsite by a physically present operator + supervisor

```
1. Visual wiring inspection
2. Sensor power verification
3. No-flow pulse check (pulse_frequency_hz should read 0.000)
4. Controlled flow start
5. Pulse movement confirmation (pulse_frequency_hz becomes nonzero)
6. Totalizer monotonicity (totalizer_raw_pulses increases, never decreases)
7. Flow stop
8. Confirmation pulses stop (pulse_frequency_hz returns to 0.000)
9. Sensor cable disconnect
10. Alarm/diagnostic observation (NOTE -- enterprise re-audit finding:
    NO alarm currently fires for this condition; the operator must
    notice `pulse_frequency_hz` staying at 0 during expected flow
    themselves -- this is a real, disclosed gap, not fixed this phase)
11. Sensor reconnect
12. Recovery confirmation
13. Reboot persistence (confirm totalizer value survives a reboot)
14. Device/server totalizer comparison
15. Known-volume or known-weight oil test (requires real oil + a
    calibrated reference container/scale)
16. K-factor calculation (compare against the device's calculation)
17. Error-percentage calculation
18. Repeat at least 3 times (repeatability)
19. Business tolerance sign-off (the approved tolerance % must be
    supplied by the business owner BEFORE this test, not decided
    after seeing the result)
20. Operator and supervisor signatures
```

## Recording template (to be filled in onsite)

```
Reference quantity:      ______________
Device quantity:         ______________
Server quantity:         ______________
Variance:                ______________
Variance percentage:     ______________
Approved tolerance:      ______________  %
Pass / Fail:             ______________
```

## K-factor change discipline (if the onsite test reveals the current
seeded value, 1000.0, is wrong)

Any change must record: old value, new value, reason, approving person,
test evidence, timestamp — via `POST /admin/kfactor` against the real
plant server (once doc 04/05's real endpoint exists), never silently.
**No K-factor change was made this session.**

## Verdict

`REAL SENSOR FLOW: NOT CERTIFIED`
`CALIBRATION: NOT CERTIFIED`
