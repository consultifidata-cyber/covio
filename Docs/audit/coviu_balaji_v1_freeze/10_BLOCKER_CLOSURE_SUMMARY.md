# 10 — Blocker Closure Summary

Status of the three release blockers identified by
`06_INDEPENDENT_RELEASE_CERTIFICATION.md`, after this session's closure
work. No architecture was reviewed, no new features were proposed, no
product redesign was performed — work was scoped strictly to closing the
three named blockers.

## Blocker 1 — Real flow calibration

**Status: tooling-and-procedure-complete; execution-pending.**

Delivered: a step-by-step technician procedure, a defined acceptance
criterion (±2% deviation tolerance, with explicit escalation instructions
on failure), the calibration record format (the validation tool's own JSON
report schema, formalized and documented field-by-field), and confirmation
that the existing tooling actually works as the procedure describes (a
scripted test run producing the exact documented output, plus 21 passing
unit tests). Full detail: `07_CALIBRATION_VALIDATION_PROCEDURE.md`.

**Remaining field action:** a technician must physically dispense a known
volume of oil through the live meter using a calibrated reference and run
the procedure, producing a passing (or corrected-then-passing) report.
This cannot be performed remotely — it requires a real, measured physical
event at the actual site.

## Blocker 2 — Monitoring and escalation

**Status: design-complete; implementation pending on both sides.**

Delivered: a review of what firmware and Device Manager already do (Device
Manager's alarm rendering is generic and would work today; nothing anywhere
notifies a human proactively), identification of the minimum information a
Notification Center needs (device_id, alarm condition, severity,
server-side-timestamped first-detection, and a silence/offline signal),
and a design for the smallest possible integration: extend the `quality`
bitfield already transmitted with every telemetry record (14 of 16 bits
unused) with four new bits mirroring alarms `diagnostics.h` already
computes, and have the receiving system react to bit transitions plus
missed-push silence — reusing the existing wire channel and reusing
whatever notification mechanism the real ERP's Notification Center already
has, exactly as instructed. Full detail:
`08_MONITORING_ESCALATION_DESIGN.md`.

**Remaining field action:** implement the four new quality bits in
firmware (small, bounded change, not done this session per the explicit
"design, do not implement" instruction), and implement the reacting logic
on the actual production ERP side — a system whose source is not available
in this repository and was not touched. Both sides need to exist and be
verified together before this is operationally real.

## Blocker 3 — OTA transition

**Status: verification-complete; migration execution-pending.**

Delivered: the exact reason the currently-deployed device cannot accept a
new-key-signed update as-is, two viable migration paths (USB reflash,
recommended; or a one-time legacy-key-signed OTA transition, viable but
not preferred for key-hygiene reasons), and — performed directly this
session, not just asserted — a real cryptographic verification: the
production private key (still present locally) produces a signature that
verifies against exactly the public key embedded in `ota_keys.h`, and a
negative control confirming the *old* key genuinely cannot verify that
same signature (proving the blocker is real, not overstated). This also
proves every future OTA release will work correctly once the migration is
complete. Full detail: `09_OTA_TRANSITION_MIGRATION_PLAN.md`.

**Remaining field action:** execute one migration path (recommended: commit
the freeze changes, then USB-reflash the device once), then move the
production private key off this laptop into a real secrets vault — a step
already flagged as outstanding in the original signing ceremony and still
not done.

---

## Overall remaining manual steps (consolidated)

1. Perform a real, technician-executed flow-calibration validation at
   Balaji (Blocker 1).
2. Implement the four new firmware `quality` bits and the corresponding
   ERP-side reaction logic, then verify end-to-end (Blocker 2) — the ERP
   half is outside this repository's scope to implement.
3. Commit the freeze-remediation source changes, then execute one OTA
   migration path (USB reflash recommended) to get the corrected signing
   key and the new diagnostics running on the physical device, and move
   the production signing key to a real vault afterward (Blocker 3).
4. Once 3 is complete, run a one-time live smoke test confirming the new
   counters/alarms populate correctly on real hardware (already
   recommended by the independent certification, not a new requirement).

## Confirmation: can a new certification be issued now?

**Not yet — but the path to one is now fully specified and largely
mechanical, not open-ended.** All three blockers went from "unaddressed"
to "ready to close, pending a specific, bounded field/implementation
action" — none require further investigation, design, or decision-making.
A new independent certification should be requested once the four
consolidated steps above are actually executed, at which point it can
verify: a real passing calibration record exists, a real notification
demonstrably reaches the device's owner when triggered, and the physical
device is confirmed running the corrected firmware with a verified,
vaulted production signing key. Until those three facts are true on the
ground — not just prepared for — the prior verdict of "not certified for
unconditional 12-month unattended production operation" still stands.
