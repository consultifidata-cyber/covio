# 09 — First-Flow Reconciliation (blank template, deferred)

Identical in structure to the plant-readiness series' own
`07_FIRST_FLOW_RECONCILIATION_SHEET.md` — reproduced here for this
activation package's own completeness, still a template because doc 08
was not performed this session.

```
Date/time: ____________________
Device ID: esp32-F4E5B2858428 (confirm matches label)
Build commit: 443dc448423b9fec2a48fb4bf7ac02740e6c2341 (confirm via /api/v1/info)
Plant/asset mapping: ____________________  (note: no plant-ID field
  exists in the current system -- record this mapping externally)
Operator name: ____________________
Supervisor name: ____________________

Before first controlled flow:
  Manual/reference meter reading:         ______________
  Device totalizer (totalizer_raw_pulses): ______________
  Server totalizer (admin dashboard/DB):   ______________
  Calibration K-factor in effect:          ______________

During first controlled flow (observe live):
  [ ] pulse_frequency_hz became nonzero during flow
  [ ] Live totalizer increased monotonically
  [ ] No reset occurred (boot_id unchanged) during the flow window
  [ ] No alarm appeared during the flow window
  [ ] Queue backlog behaved normally

After first controlled flow:
  Manual/reference meter reading (end):     ______________
  Physical quantity delivered (reference):  ______________
  Device totalizer (end):                   ______________
  Device-computed litres (server K-factor):  ______________
  Server-recorded litres:                    ______________
  Absolute error:                            ______________
  Percentage error:                          ______________
  Approved tolerance:                        ______________  %
  PASS / FAIL

Database continuity check (server-side):
  SELECT COUNT(*), MIN(seq), MAX(seq) FROM records WHERE device_id='esp32-F4E5B2858428';
  SELECT COUNT(*) FROM quarantined_records WHERE device_id='esp32-F4E5B2858428';
  Record count matches (max-min+1): [ ] yes  [ ] no
  Quarantined count is zero:        [ ] yes  [ ] no

Sign-off:
  Operator signature:   ____________________  Date: __________
  Supervisor signature: ____________________  Date: __________
```
