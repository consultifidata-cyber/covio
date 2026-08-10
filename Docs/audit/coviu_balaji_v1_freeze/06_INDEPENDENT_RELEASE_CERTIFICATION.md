# 06 — Balaji V1 Independent Release Certification

**This is an independent audit, not a continuation of the freeze
implementation.** Prior documents in this chain (01–05) were treated as
claims to verify, not facts to accept. Four independent reviews were run
against current source, a live compile, the running device, and the
project's own documented history: firmware code integrity, the ERP/API
contract, documentation accuracy, and known-limitations completeness. Two
things were also independently re-verified directly rather than taken on
faith: a fresh, read-only serial check of the live device, and the actual
presence/identity of the old OTA test signing key on disk.

## Release verdict

**NOT CERTIFIED for unconditional 12-month unattended production
operation.** The currently-deployed firmware is healthy, stable, and
correctly proven live (confirmed again this session — `boot_id: 54`
unchanged, `acked_seq` continuing to advance, no data loss). That narrower
claim — "the device running today works" — holds. But the V1 freeze
package as a whole, which this certification was asked to evaluate, has
three unresolved items that materially affect whether this specific
customer can be told "this will run correctly and be watched over for a
year." All three are closeable without new architecture — none require
inventing a feature — but none are done yet.

## Remaining blockers

**1. Sensor/flow-accuracy validation was never actually performed.**
The freeze certification's own "must do now" list required validating the
device's measurement accuracy against real, measured oil flow. What was
actually delivered is a validation *tool*
(`scripts/validate_meter_calibration.py`) — a real, tested, working tool —
but no one has run it against the physical meter with a real calibrated
volume. Every session in this project's entire audit trail, including the
most recent live commissioning proof, shows the totalizer at a static pulse
count with no real-flow measurement ever taken. The core function this
product exists to perform — converting pulses to litres correctly — is
unverified for this specific meter and tank. This is not a documentation
gap; it is an unperformed physical measurement, and it directly affects the
correctness of every litre this device will report for the next 12 months.

**2. There is no monitoring or escalation mechanism for the 12-month
unattended period, and no alarm reaches a human automatically.**
Device Manager renders whatever alarms the device reports (confirmed: its
alarm rendering is generic and data-driven, not hardcoded, so it would
display the new `SENSOR_STOPPED`/`REBOOT_LOOP` alarms correctly once
present) — but only while a person has the app open and looking at that
screen. There is no push notification, email, SMS, or webhook anywhere in
this system (device, Device Manager, or server). A prior operator-response
document in this project's own audit trail states outright that a named
escalation contact was "not populated by this report." Combined, this
means: if the device goes offline, gets stuck, or enters a reboot loop at
any point in the next year, nothing in this system tells anyone, and no one
is designated to notice. This is not a hypothetical edge case for a
12-month unattended deployment — it is the central risk that framing
implies, and it is currently unaddressed.

**3. The approved freeze improvements exist only in source — they have
not been deployed to the physical device, and the deployment path in
`05_IMPLEMENTATION_SUMMARY.md` was incompletely specified.**
The device currently running was built before this session's changes; none
of the new persistent counters, the two new alarms, or the corrected OTA
signing key are protecting the live unit today. Getting them there requires
one more step than previously documented: the currently-deployed firmware
only trusts the *old* OTA test key, not the new production key just
embedded in source — a manifest signed with the new key would be rejected
by the device as-is. This is fixable (independently confirmed: the old test
key's private half, `server/tools/.test_signing_key.pem`, is still present
on this machine and matches what the device currently trusts, so a
correctly-signed transition update is possible; a direct USB reflash also
remains available regardless), but it was not documented as a required
step, and it has not been executed. Until it is, "V1 freeze" describes the
source tree, not the production device.

## Operational recommendations

These do not block certifying the currently-running device's continued
operation, but should be addressed promptly:

- **Fix a real, low-severity counting bug**: `sync.h`'s `pushOnce()`
  increments `push_fail_count` on two of its three failure branches but not
  on `http.begin()` returning `false` — a one-line fix. No data loss occurs
  from this (the row still stays queued and retries); only the diagnostic
  counter under-reports in a low-probability path.
- **Correct the deployment documentation** (`05_IMPLEMENTATION_SUMMARY.md`,
  `03_OTA_SIGNING_KEY_CEREMONY.md`) to specify that the first update
  reaching this device — whether via OTA or USB — must either be signed
  with the still-present old test key (a one-time transition) or delivered
  by direct reflash; "OTA will pick it up automatically" as previously
  written is not accurate for this specific transition.
- **Correct a stale known-limitations claim**: `server/server.py` is
  confirmed (by its own module docstring) to be a bench/reference stub, not
  the actual production ERP (`https://data.funtastik.co.in`, a separate,
  unaudited codebase). The API-contract and admin-auth verification in this
  and prior sessions is sound *against the reference stub*; it should not
  be represented as having verified the live production ERP's behavior,
  which was not and could not be inspected from this repository. Update
  `04_API_KEY_VERIFICATION_RESULT.md`'s framing accordingly — its
  conclusion still holds (via the independent, device-side fingerprint
  evidence), but its server-side reasoning should be scoped honestly.
- **Re-examine the deferred live-Wi-Fi-credential-test decision** in light
  of this site's own history: this exact device's commissioning trail
  recorded three real Wi-Fi failures at Balaji (band mismatch, stale SSID,
  AP-mode fallback). The freeze certification's "do later" reasoning
  assumed Wi-Fi stability that this site's own history doesn't fully
  support — not severe enough to block release, but worth a periodic
  manual Wi-Fi health check rather than treating it as a closed question.
- **Once the firmware update is actually deployed**, run a live smoke test
  confirming the six new counters and both new alarms genuinely populate
  correctly on real hardware — this session's only proof is a clean
  compile and host-run unit tests, not a live run of the new diagnostics
  code on the device itself.

## Deferred roadmap confirmation

Independently re-checked, not just re-asserted: per-device calibration
modeling, a device/asset registry, bulk/fleet provisioning, remote
Wi-Fi/key reconfiguration, an OTA-trigger UI, multi-Wi-Fi-profile support,
Secure Boot, and NVS/flash-encryption retrofit remain correctly out of
scope for a single-device deployment and are unaffected by anything found
in this audit — no new information moves any of these into the blocker
category. The one related, previously-accepted limitation worth restating
plainly: this firmware trusts exactly one OTA signing key at a time, with
no rotation/trust-list mechanism (a documented, Phase-3 architectural
item) — that absence is exactly what created blocker #3's transition
friction, and remains correctly deferred as a fleet-scale concern, not a
Balaji-specific one.

## Final production acceptance statement

The physical device deployed at Balaji is, as of this session, live,
reachable, and correctly synchronizing telemetry with no data loss — that
was independently reconfirmed, not assumed. On that narrow basis, no
change is needed to keep the currently-running system operating as it has
been. But this certification was asked to evaluate whether Balaji V1 — the
freeze-remediated release — is ready to be handed to a customer as a
12-month, unattended production commitment, and the honest answer is not
yet: a real flow-accuracy measurement has not been taken, no one is
designated to notice if the device fails, and the safety nets built for
exactly that scenario are not yet running on the device. None of the three
blockers require new design or new features — they require finishing work
already scoped: running the validation that was already tooled, naming a
contact and a response path that was already flagged as missing, and
delivering firmware that was already built and tested to the unit it was
built for. This certification will be reissued once those three items are
closed.
