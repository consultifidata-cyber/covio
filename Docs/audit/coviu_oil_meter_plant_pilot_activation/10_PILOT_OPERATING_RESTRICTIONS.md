# 10 — Pilot Operating Restrictions (Part 11)

Mandatory for the entire duration of the controlled pilot, until the
larger architectural items (doc 11) are separately closed:

```
[ ] Supervisor physically present at all times during the pilot window
[ ] Manual oil measurement maintained IN PARALLEL with device readings
    for the entire pilot -- device readings are not yet trusted alone
[ ] No unsupervised OTA, ever
[ ] No OTA during active production/oil transfer
[ ] USB recovery kit (doc 07) physically onsite for the ENTIRE pilot,
    not just during an OTA window
[ ] Daily queue and database reconciliation (the identity from doc 06/12,
    repeated daily)
[ ] Daily build-identity check (/api/v1/info's build_commit matches
    443dc448423b9fec2a48fb4bf7ac02740e6c2341 -- or whatever is the
    then-current approved release, if superseded through a properly
    authorized process)
[ ] Daily totalizer reconciliation (device vs. server vs. manual reading)
[ ] Daily alarm review (/api/v1/health)
[ ] Daily free-space review (queue.capacity_pct_used -- recommended
    action threshold: 80%, per doc 06's own recommendation)
[ ] Immediate action if any outage approaches the recommended ~12-hour
    safe buffer window (doc 06)
[ ] Manual fallback process available and understood by the operator
    (pre-existing business process, not documented by this engineering
    audit)
[ ] No assumption of secure-boot protection -- it is confirmed OFF
    (enterprise re-audit doc 10)
[ ] No assumption of flash-encryption protection -- confirmed OFF
[ ] No claim of production TLS if communication remains HTTP -- state
    this plainly to anyone reviewing pilot results
```

## Additional restriction from this phase's own findings

- Because the sensor-silence alarm gap remains open (doc 08), the
  supervisor must periodically and manually check `pulse_frequency_hz`
  during expected-flow windows — nothing will page them automatically.
- Because the endpoint has not yet been reprovisioned (doc 04/05), **no
  oil-flow production activity may begin until the endpoint is verified
  pointed at the real plant server**, not the bench address.
