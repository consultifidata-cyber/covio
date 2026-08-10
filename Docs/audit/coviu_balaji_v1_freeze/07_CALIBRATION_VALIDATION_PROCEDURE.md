# 07 — Calibration Validation Procedure (Blocker 1 Closure)

Closes independent certification blocker 1: "sensor/flow-accuracy
validation was never actually performed." This document is the procedure,
acceptance criteria, and record format a technician needs to actually
perform that validation at Balaji. It does not perform the validation
itself — that requires physically dispensing a known volume of oil through
the live meter, which cannot be done remotely. What follows makes that the
only remaining step: the tooling, the pass/fail rule, and the record format
are all defined and verified below.

## 1. Technician procedure

**Equipment needed:**
- A calibrated reference volume — a container/vessel of known, accurately
  measured capacity (or a certified flow reference), suitable for
  dispensing a real, measurable quantity of oil through the meter.
- A laptop on the same network as the device (or a direct connection to
  `covio-858428.local` / the device's known IP), with Python 3 available.
- The K-factor currently configured on the device (read via the serial
  console's `show` command — do **not** guess or use a value from
  documentation, since the value may have changed since any doc was
  written).

**Steps:**

1. Confirm the device is online and healthy: run `show` over serial, or
   check Device Manager's Live Monitor. Note the `calib` line's `K` value —
   this is `--configured-k-factor` for step 4.
2. Immediately before starting the flow test, capture the baseline
   totalizer reading:
   ```
   python scripts/validate_meter_calibration.py capture-before \
       --device-host covio-858428.local \
       --session balaji_calib_<date>.json
   ```
3. Dispense a known volume of oil through the meter using the calibrated
   reference (the physical action this tool cannot perform). Record the
   exact measured volume in litres. Larger test volumes reduce measurement
   error — prefer the largest volume practical to dispense in one
   continuous run.
4. Immediately after the test, capture the ending reading and evaluate:
   ```
   python scripts/validate_meter_calibration.py capture-after \
       --device-host covio-858428.local \
       --session balaji_calib_<date>.json \
       --measured-volume-liters <actual measured volume> \
       --configured-k-factor <K value from step 1> \
       --technician "<your name>" \
       --out balaji_calib_<date>_report.json
   ```
5. The tool prints `PASS` or `FAIL` and writes the full record to the
   `--out` path (see §3). Keep this file — it is the validation record for
   this device.
6. If `FAIL`: do not adjust firmware or NVS. Miscalibration is corrected
   server-side (the K-factor the device uses is server-authoritative — see
   `02_FIELD_SERVICE_GUIDE.md` §4). Escalate the failing report to whoever
   owns the calibration record on the ERP/server side, along with the
   measured K-factor this test computed, so the correct value can be set
   there.

## 2. Acceptance criteria

- **Tolerance: ±2% deviation between measured and configured K-factor**
  (the tool's own default, `--tolerance-pct 2.0`). This is a starting
  value, not an arbitrary one — it is tight enough to catch a materially
  wrong calibration (e.g. the "typo'd K-factor" scenario already flagged in
  this project's own prior audit trail, which silently re-scales all
  historical and future readings) while allowing for ordinary measurement
  error in a technician's own reference volume.
- **A single passing test is the minimum bar to close this blocker for
  first deployment.** For an ongoing operational cadence beyond this
  closure, a repeat validation is recommended whenever the physical meter
  or its installation is disturbed (a replacement, per the field service
  guide's §1), and periodically thereafter at an interval the customer
  agrees to — this document does not itself schedule that recurring
  cadence, since scheduling policy is outside this blocker's scope.
- **A FAIL result is not a tooling failure — it is exactly what this
  validation exists to catch.** Do not re-run the test with a different
  tolerance to force a pass. A failing result means the configured
  K-factor must be corrected server-side (§1, step 6) and the test
  re-run against the corrected value.

## 3. Calibration record format

The tool's `--out` JSON report **is** the record format — every field
below is already produced by the tool exactly as shown (verified in §4):

```json
{
  "device_host": "covio-858428.local",
  "technician": "<name>",
  "before_captured_at": "<ISO-8601 timestamp, technician's laptop clock>",
  "after_captured_at": "<ISO-8601 timestamp, technician's laptop clock>",
  "before_pulses": <int>,
  "after_pulses": <int>,
  "delta_pulses": <int>,
  "measured_volume_liters": <float>,
  "measured_k_factor": <float>,
  "configured_k_factor": <float>,
  "deviation_pct": <float, signed>,
  "tolerance_pct": <float>,
  "passed": <bool>
}
```

Field meanings needing explanation beyond their names:
- `before_captured_at`/`after_captured_at` use the technician's own
  laptop's real wall-clock time (the device itself has no RTC — this is
  intentionally an external, independent timestamp, not a device-reported
  one).
- `delta_pulses` = `after_pulses - before_pulses`, the raw pulse count
  attributable to the test's dispensed volume.
- `measured_k_factor` = `delta_pulses / measured_volume_liters` — the
  actual pulses-per-litre constant this specific meter demonstrated during
  the test.
- `deviation_pct` is signed: positive means the meter is producing *more*
  pulses per litre than configured (readings would under-report volume if
  left uncorrected); negative means fewer (readings would over-report).

**Retention:** keep every `--out` report file produced — this is the audit
trail proving calibration was validated, and by whom, and when. No
specific storage location is prescribed here (out of scope — this is a
procedure/format document, not an infrastructure decision); store it
wherever this project's other field-service records are kept.

## 4. Tooling verification (performed this session)

Confirmed the tool actually behaves as this procedure describes, without
needing a live device:

```
$ python scripts/validate_meter_calibration.py evaluate \
    --before-pulses 1000000 --after-pulses 1050000 \
    --measured-volume-liters 50.0 --configured-k-factor 1000.0
delta_pulses          = 50000
measured_volume_liters= 50.0
measured_k_factor     = 1000.0000
configured_k_factor   = 1000.0000
deviation_pct         = +0.000%  (tolerance +/-2.00%)
VERDICT: PASS
```

- `--help` confirms the `capture-before`/`capture-after`/`evaluate`
  subcommands and every flag named in §1 exist exactly as documented.
- The underlying computation functions (`compute_measured_k_factor`,
  `evaluate_calibration`) have 21 passing unit tests
  (`test/native/test_validate_meter_calibration.py`), including a
  realistic-failing-session case matching the exact "10% miscalibration"
  scenario this procedure's acceptance criteria are designed to catch.
- `capture-before`/`capture-after` read `totalizer_raw_pulses` from the
  device's existing, already-proven `/api/v1/status` endpoint (the same
  field read throughout this project's live commissioning sessions) — no
  new device-side capability is required for this procedure to work today,
  against the currently-deployed firmware.

## What remains (field action, not closeable remotely)

**One remaining manual step: a technician must physically perform the
procedure in §1 at Balaji, using a real calibrated reference volume, and
produce a passing (or corrected-then-passing) report.** Everything this
session could verify or prepare — the tool, its correctness, the
acceptance rule, and the record format — is done and confirmed working.
This blocker is **tooling-and-procedure-complete, execution-pending.**
