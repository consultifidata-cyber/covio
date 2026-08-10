# Miki Wire Sensor Characterization — Wire Drawing Machine 1

**Status: NO MEASUREMENTS COLLECTED YET.** This document is the mandated
evidence container (Phase-2 §6). Every value below is deliberately blank or
marked IVI (INSUFFICIENT VERIFIED INFORMATION). **No number in this file may
be invented, estimated, or copied from a datasheet as if it were a site
measurement.** Datasheet figures appear only in the clearly-labeled
"manufacturer reference" section and are never a substitute for site data.

**Instrumentation already in the candidate firmware for this job:**
`peakHzObserved` / `violationCount` (pulse_plausibility, active even while
the monitor is disabled), `msSinceLastPulse` + health state (sensor_health),
serial `show`, and `/api/v1/status` + `/api/v1/metrics` on the LAN — the
technician needs no extra tooling beyond a serial console and the plant LAN.

## 1. Manufacturer reference (NOT site data)

LJ12A3-4-Z/BX: inductive, NPN, normally open, nominal sensing distance 4 mm
(ferrous), supply 6–36 VDC, switching frequency commonly quoted ≈500 Hz.
Reference only — the plant's actual target geometry, mounting distance,
supply rail, and line speed determine real behavior.

## 2. Electrical (fill from A2/A3 bench measurements)

| Measurement | Value | Evidence (photo/scope ref) |
|---|---|---|
| Sensor supply voltage at DI terminal block | IVI | |
| Sensor output voltage, inactive | IVI | |
| Sensor output voltage, active | IVI | |
| DI1 terminal voltage, inactive / active | IVI | |
| GPIO4 level, inactive / active | IVI | |
| Edge quality (bounce/oscillation on scope) | IVI | |

## 3. Pulse-rate envelope (fill from §5/§6 machine sessions)

| Quantity | Value | How measured |
|---|---|---|
| Minimum observed operating frequency (Hz) | — | peakHz + counter deltas during slowest normal run |
| Normal operating frequency (Hz) | — | ≥1 full shift of `pulse_rate` telemetry |
| Maximum observed frequency (Hz) | — | `peakHzObserved` over ≥1 week including restarts |
| Maximum physically possible frequency (Hz) | — | max line speed × pulses-per-unit-length (get both from plant engineering) |
| Startup behavior (ramp profile, spurious pulses?) | — | serial capture across ≥5 machine starts |
| Stop behavior (coast-down pulses?) | — | serial capture across ≥5 stops |
| Normal idle duration distribution | — | gap log over ≥1 week |
| Longest legitimate idle (shift breaks, changeovers, weekends?) | — | plant operations interview + observed gaps |

## 4. Threshold derivation (complete ONLY after §3 has real data)

Required form for each threshold — source, measurement, rationale, margin:

- `set maxhz <N>` (plausibility ceiling): N = max(observed max, physical max)
  × safety factor (propose 1.5×, justify against the gap to EMI-burst rates
  actually observed). Must also be sanity-checked against the sensor's own
  switching limit.
- `set suspects <S>` (longest plausible idle, seconds): S = longest
  legitimate idle from §3 × safety factor (propose 2×), bounded by what
  makes the SUSPECT advisory actually useful to operations.

Until then both remain 0 (monitors inert) — the deliberate shipped state.
