ACR ID:              ACR-001
Against ADR:         ADR-001
Raised by:           Implementation agent (Phase 1 certification review)
Date:                2026-07-08

Specific sentence/decision found impractical:
                     ADR-001, Migration Strategy: "Any device predating this
                     ADR must fully drain its SD queue to empty under its
                     current firmware before receiving the OTA that
                     introduces this schema. This is a one-time,
                     one-directional cutover. Every schema change from this
                     point forward follows the current+previous rule and
                     requires no forced-drain step."

Why it is impractical:
                     Not impractical to implement — this is not a capability
                     gap. It is a compliance finding: the first Phase 1
                     implementation (commit 8b1d94a) had the receiver accept
                     a synthetic "schema_version = 0" as if it were a
                     genuine, registered "previous" schema version, and
                     defaulted any record with no schema_version key at all
                     to that same accepted value. This directly contradicts
                     the Migration Strategy text above: the ADR calls for a
                     one-time, one-directional forced-drain cutover for the
                     initial (unversioned -> v1) transition specifically,
                     not a dual-acceptance window. The "current + previous"
                     rule is written to describe steady-state behavior for
                     every *future* schema change (v1->v2 and onward), where
                     a genuine registered previous layout exists — it was
                     never intended to cover the bootstrap transition out of
                     the pre-ADR-001 unversioned format.
                     No fielded devices exist yet (confirmed in
                     SCHEMA_REGISTRY.md's Migration Notes), so this deviation
                     had no live-data consequence, but it was a real
                     deviation from frozen ADR text and was not raised as an
                     ACR at the time it was introduced.

Narrowest possible fix proposed:
                     Revert the receiver to strict ADR-001 compliance for
                     the current single-schema-version state: only
                     CURRENT_SCHEMA_VERSION (currently 1) is accepted.
                     PREVIOUS_SCHEMA_VERSION is computed as
                     CURRENT_SCHEMA_VERSION - 1 and only added to the
                     accepted set once CURRENT_SCHEMA_VERSION > 1 (i.e. once
                     a second, genuinely-registered schema_version exists).
                     A record with schema_version missing, or equal to any
                     value other than the current registered version(s), is
                     quarantined exactly like any other unsupported version
                     - consistent with the Migration Strategy's
                     one-directional cutover. This is a one-line change to
                     the accepted-version computation in server/server.py
                     and does not touch storage, queue, protocol batching,
                     ack, retry, or record content in any other way.

                     This proposal does NOT ask to change ADR-001's text. It
                     asks to correct the implementation to match ADR-001 as
                     already written. A separate, genuinely architectural
                     question - whether ADR-001 *should* be amended in the
                     future to also allow a dual-acceptance window for the
                     very first schema-version rollout, instead of a forced
                     drain - is raised below for awareness only, and is
                     explicitly NOT approved by this ACR. If that alternative
                     is ever wanted, it requires its own superseding ADR
                     text change and its own review, not an implementation
                     shortcut.

Impact on other ADRs:
                     None. ADR-002/ADR-003/etc. reference ADR-001's
                     versioning mechanism generically ("a new schema_version
                     under ADR-001") and are unaffected by which specific
                     versions are currently accepted. No queue, ack, retry,
                     batching, or OTA behavior changes.

Status:              REJECTED (as a request to change ADR-001's text) /
                      RESOLVED (as an implementation defect) — the
                      Narrowest possible fix above has been applied directly
                      to server/server.py in this same review pass, since it
                      brings the implementation into compliance with
                      already-frozen ADR-001 text rather than changing any
                      architecture decision. No architecture change is
                      pending review. If a future maintainer wants to
                      formally adopt a dual-acceptance bootstrap window
                      instead of forced-drain, that would need a new ACR
                      proposing a superseding ADR-001 text change, evaluated
                      on its own merits — not implied or pre-approved by this
                      document.
