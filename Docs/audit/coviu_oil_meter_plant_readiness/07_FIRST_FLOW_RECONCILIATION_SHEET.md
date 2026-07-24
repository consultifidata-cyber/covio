# 07 — First-Flow Reconciliation Sheet (blank template)

This is a **template**, not completed data — real sensor/oil-flow
validation has not occurred (see 09_KNOWN_LIMITATIONS.md). To be filled
in by the physically-present operator/supervisor during the first
controlled flow.

## Device/session identity

```
Date/time: ____________________
Device ID: esp32-F4E5B2858428 (confirm matches label)
Build commit: 856972ef6106d662c5f8a7f5b71c9a60ce40edc1 (confirm via /api/v1/info)
Plant/asset mapping: ____________________
Operator name: ____________________
Supervisor name: ____________________
```

## Before first controlled flow

```
Manual/reference meter reading:        ______________
Device totalizer (totalizer_raw_pulses): ______________
Server totalizer (admin dashboard/DB):  ______________
Calibration K-factor in effect:         ______________  (server-side, /admin dashboard)
```

## During first controlled flow (observe live)

```
[ ] pulse_frequency_hz became nonzero during flow
[ ] Live totalizer increased monotonically (no decrease, no freeze)
[ ] No reset occurred (boot_id unchanged) during the flow window
[ ] No alarm appeared during the flow window
[ ] Queue backlog behaved normally (drained at the expected rate)
```

## After first controlled flow

```
Manual/reference meter reading (end):     ______________
Physical quantity delivered (reference):  ______________
Device totalizer (end):                   ______________
Device-computed litres (server K-factor):  ______________
Server-recorded litres (admin dashboard):  ______________

Absolute error (reference vs device):      ______________
Percentage error:                          ______________
Approved tolerance (business-owner-set):   ______________  %
PASS / FAIL (circle one)
```

## Database continuity check (server-side, run after the flow)

```sql
SELECT COUNT(*), MIN(seq), MAX(seq) FROM records WHERE device_id='esp32-F4E5B2858428';
SELECT COUNT(*) FROM quarantined_records WHERE device_id='esp32-F4E5B2858428';
```
```
Record count matches (max - min + 1): [ ] yes  [ ] no
Quarantined count is zero:            [ ] yes  [ ] no
```

## Sign-off

```
Operator signature:   ____________________  Date: __________
Supervisor signature: ____________________  Date: __________
```
