# 14 — Real Sensor and Calibration Validation (Part 17)

## Availability check performed this session

`config.h`'s own comment states a real sensor is intended to be wired to
`PIN_PULSE` (GPIO1) with `SIM_PULSES=0` (the simulated-pulse jumper mode
is explicitly disabled in this build, confirmed by direct read of the
current, unmodified `config.h`). This session checked
`/api/v1/metrics`'s `pulse_frequency_hz` repeatedly across many hours of
elapsed session time (this conversation, and the prior conversation
turns building up to it): **it has read `0.000` on every single check,
and `totalizer_raw_pulses` has read the identical value (`401`) on
every single check across this entire multi-day, multi-session audit
trail**, including through multiple reboots, OTA installs, and USB
reflashes.

**Conclusion: no real or simulated pulse source is currently active on
this device.** Whether a real sensor is even physically wired (vs. left
disconnected on this bench setup) cannot be determined remotely — this
requires physical inspection, which this session's execution boundary
(no physical hands) cannot perform.

## Required tests — status

| # | Test | Status |
|---|---|---|
| 1 | No-flow pulse stability | **Trivially "passed" by absence of data** — pulses have stayed at exactly 0 activity throughout, which is consistent with EITHER genuine no-flow stability OR simply no sensor connected. Cannot be distinguished remotely. |
| 2 | Controlled pulse input | **Not performed** — no pulse source available |
| 3 | Sensor cable disconnect | **Not performed** — cannot physically disconnect anything remotely |
| 4 | Sensor reconnect | **Not performed** |
| 5 | Totalizer monotonicity | **Partially proven** — the totalizer has never DECREASED across any reboot/OTA/reflash this entire session (real evidence), but it has also never INCREASED, so true monotonic-under-real-flow behavior is unproven |
| 6 | Pulse count persistence after reboot | **Proven** — `401` survived every reboot this entire audit trail, real hardware evidence |
| 7 | Device/server totalizer agreement | **Proven for the static value** — device and server both consistently show the same totalizer-derived data; never exercised under a CHANGING value |
| 8 | Known-volume/known-weight oil test | **Not performed — requires physical oil and a calibrated reference, genuinely impossible in this environment** |
| 9 | K-factor verification | **Not performed** — the server's calibration table default (`k_factor=1000.0`, seeded, confirmed by direct code read of `server.py::init_db()`) has never been checked against a real meter specification |
| 10 | Error-percentage calculation | **Not performed** — no reference measurement exists to compute against |
| 11 | Repeatability over multiple runs | **Not performed** |
| 12 | Low-flow behavior | **Not performed** |
| 13 | Normal-flow behavior | **Not performed** |
| 14 | Maximum expected flow behavior | **Not performed** |
| 15 | Noise/bounce rejection | **Not independently tested**; `PCNT_GLITCH_NS=1000` (config.h) provides a hardware glitch filter by design — code-confirmed, not empirically validated against real sensor noise |
| 16 | Flow stop/start behavior | **Not performed** |

## Verdict

**`REAL SENSOR ACCURACY NOT CERTIFIED`.**

No pulse simulation was substituted for real-flow evidence, per the
mandate's own explicit instruction. This is not a firmware defect — it
is a physical-world test that requires a person, a sensor, and oil, none
of which this remote session has access to. It remains the single
largest genuine gap standing between "firmware/data-integrity certified"
(which this audit substantially supports) and "enterprise-grade,
production-ready for real measurement" (which it does not yet support).
