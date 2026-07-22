# MASTER_GOVERNANCE.md — Covio Oil Flow Meter Firmware

**Status:** Standing constitution for how work is done on this repository, by anyone — human or agent, today or years from now.

## Document Hierarchy (read this first)

Four documents govern this project, and each governs a different thing. None outranks another — they don't overlap:

| Document | Governs |
|---|---|
| `Firmware Architecture Audit.md` | What the firmware *was*, at the time of review — a frozen historical record, never edited to reflect new decisions. |
| `Firmware Architecture Freeze & Remediation Blueprint.md` | Why each gap matters and how findings consolidate into architectural work — a frozen analysis, never edited. |
| `Firmware Detailed Architecture Decision Record (ADR).md` | **What the firmware will do** — every technical decision. This is the only document that decides architecture. It changes only by a new ADR superseding an old one. |
| `Firmware Phase-wise Implementation Master Plan.md` | **In what order, and with what evidence, the ADRs get built.** Sequencing, preconditions, tests, checklists. |
| **`MASTER_GOVERNANCE.md` (this document)** | **How anyone is allowed to work** — coding standards, commit/PR/review discipline, the ACR workflow's exact mechanics, and the checklists that apply at every release, regardless of which phase or ADR is in flight. |

If you think two of these documents disagree, they don't — you are reading one of them for a question it was never meant to answer. Architecture questions go to the ADR. Sequencing questions go to the Master Plan. Conduct questions go here.

---

## 1. The Ten Absolute Rules

These are not guidelines. A pull request that violates any of these is rejected on sight, without a detailed review of its contents.

1. **Never redesign a frozen ADR.** If ADR-003 says truncate-before-append, you implement truncate-before-append. If it turns out to be wrong, you write an ACR (§9) — you do not quietly implement something smarter and call it a bug fix.
2. **Never implement outside the current phase.** Check `PROJECT STATUS` at the top of the Master Plan. If the current phase is Phase 2, you do not touch Phase 5's files, even if you have spare time and good intentions.
3. **Never skip a test the relevant ADR or phase card requires.** "I manually checked it and it worked" is not evidence. If the Master Plan says a native-host test and a bench test are both required, both exist before the work is marked done.
4. **Never merge unrelated work into one change.** One ADR's implementation, or one phase's contained sub-item, per commit/PR. Do not fix an unrelated typo, rename a variable "while you're in there," or bundle two ADRs because they touch the same file.
5. **Never create a duplicate implementation of something that already exists.** Before writing a new helper, a new parser, a new CRC routine — search the codebase. This project has already paid once for duplicated logic (the same minimal-JSON field extractor independently reimplemented in `sync.h` and `ota.h`, flagged in the Audit as a coupling risk) — do not repeat that pattern.
6. **Never touch `factory_test.h` (or any future factory-only code) in a way that makes it reachable from a production build.** This boundary is load-bearing, not stylistic — see ADR-008 and the Master Plan's Phase 6 stop condition.
7. **Never write receiver-side code while believing it is firmware-side work, or vice versa.** Check whether the phase card marks the item as receiver-side before writing a single line.
8. **Never ship a change to the wire format, SD row format, or NVS layout without updating `SCHEMA_REGISTRY.md` (and, if applicable, bumping `config_schema_version` per ADR-007) in the same change.**
9. **Never edit the Audit or the Blueprint.** They are historical snapshots. If something in them turns out to be inaccurate, note it in the relevant ADR or ACR — do not rewrite history.
10. **Never mark a phase or ADR "complete" without the exact evidence its card requires, archived somewhere retrievable — not just asserted in a commit message.**

---

## 2. File & Module Discipline

- **Maximum file size guideline: ~400 lines per `.h`/`.ino`/`.py` file.** This project's files today range 69–194 lines; 400 is a generous ceiling, not a target. If a file is approaching it, that is a signal — per the Blueprint's Task 4 findings — that responsibilities have blended (e.g., `EventQueue` already bundles three concerns; `Sync` bundles four). **Do not silently split a file to get under the limit.** Splitting a module's responsibilities is a design change and requires its own ADR, exactly like any other architectural decision. Growing past the guideline without addressing the underlying responsibility mix is the violation to flag; the size number itself is just the trigger.
- **Module boundaries are fixed by the existing include graph** (Audit §1, §16): `config.h` has zero dependencies; `store.h` depends only on `config.h`; `totalizer.h`/`queue.h` depend only on `config.h`; `telemetry.h` depends on `queue.h`+`store.h`; `sync.h` depends on `store.h`+`queue.h`+`telemetry.h`; `ota.h` depends on `store.h`; `provision.h` depends on `store.h`. **No new module may introduce a dependency that creates a cycle**, and no module may reach into another module's private (trailing-underscore) state directly — only through its existing public methods.
- **No new global variables** in `covio_firmware.ino` beyond what an approved ADR/phase explicitly calls for. The current globals (`store`, `totalizer`, `eventQueue`, `syncEngine`, `ota`, `provision`, `seq`, the four timer variables, `healthySignalled`) are the known, reviewed set — anything added joins that reviewed set through the same phase/ADR process, not ad hoc.
- **Module ownership assignments from the Blueprint's Task 4 must be respected once decided.** Specifically: whichever module ends up owning queue-depth/backlog awareness, OTA health-confirmation policy, and WiFi-connectivity-state authority (flagged as currently-ambiguous in the Blueprint) must have that ownership stated explicitly in code comments once Phase 2/3/4 implement the relevant logic — this is a Definition-of-Done item for whichever phase resolves it, not something to leave implicit a second time.

## 3. Coding Standards & Naming Conventions

These are extracted directly from the existing, working codebase — match them exactly. Consistency with what's already there matters more than any abstract "best practice."

| Element | Convention | Example from the existing code |
|---|---|---|
| Class names | `PascalCase` | `Store`, `Totalizer`, `EventQueue`, `Sync`, `Ota`, `Provision` |
| Public methods | `camelCase` | `begin()`, `deviceId()`, `serverUrl()`, `pushOnce()`, `pollConfig()` |
| Private methods | `camelCase` with a trailing underscore | `crc32_()`, `persist_()`, `setupPCNT_()`, `extractLong_()` |
| Private member variables | trailing underscore | `p_`, `cp_`, `base_`, `ack_`, `st_`, `q_`, `backoff_`, `buf_` |
| Wire/storage struct fields | `snake_case` | `boot_id`, `seq`, `totalizer`, `acked_seq`, `rssi_abs` |
| `#define` constants | `ALL_CAPS_WITH_UNDERSCORES` | `FW_VERSION`, `TELEMETRY_PERIOD_MS`, `PCNT_UNIT_USED` |
| File names | lowercase, no separator, `.h` | `store.h`, `totalizer.h`, `queue.h` |

- **Comments explain *why*, not *what*** — the existing codebase's comment style (e.g., the "ORDER MATTERS" note in `covio_firmware.ino`, the CRC/ping-pong rationale in `totalizer.h`) is the standard to match. Do not add comments describing what a line of code obviously does.
- **No new external library dependency without an ADR.** This codebase is deliberately dependency-free for JSON parsing (ADR-001 explicitly rejected protobuf/CBOR for exactly this reason). Adding a library is an architectural decision, not an implementation convenience.
- **Fixed-size, packed structs remain the storage/wire convention** for anything on the SD card or in the binary row format, consistent with ADR-001 and ADR-003. Variable-length encoding is not introduced without a superseding ADR.

## 4. Documentation Update Rules

| If your change touches... | You must also update, in the same PR... |
|---|---|
| The wire JSON or SD row format | `SCHEMA_REGISTRY.md` |
| The NVS key set/layout | `config_schema_version`'s migration changelog (ADR-007) |
| The toolchain/core version in use | `VERSIONS.md` |
| Anything in `PROJECT STATUS` (phase completed, ACR opened/closed, owner changed) | The `PROJECT STATUS` block at the top of the Master Plan |
| A frozen ADR decision (only via a superseding ADR, never a silent edit) | A new `ADR-0nn` entry, with the superseded one marked superseded, not deleted |

Never edit `Firmware Architecture Audit.md` or `Firmware Architecture Freeze & Remediation Blueprint.md` for any reason — they are point-in-time records (Rule 9, §1).

## 5. Git & Commit Policy

*(This repository is not yet under version control as of this writing — initialize it before Phase 0 work begins, then follow the rules below from the first commit.)*

- **One logical change per commit.** A commit implements one ADR, or one clearly-scoped sub-item of an ADR already split across phases (e.g., ADR-014's Phase 6 half and Phase 9 half are two separate commits, never one).
- **Commit message format:** `[Phase N][ADR-0nn] short imperative description`, e.g. `[Phase 2][ADR-003] add truncate-before-append framing to queue log`. For governance/process-only changes with no ADR: `[Governance] short description`.
- **Never bundle firmware-side and receiver-side changes in one commit**, even when they implement the same ADR — they are different systems (per the Master Plan's explicit separation rule) and should be independently revertible.
- **Never force-push, rebase-and-lose-history, or amend a commit that has already been reviewed.** Fix forward with a new commit.
- **Branch naming:** `phase-N-adr-0nn-short-slug` (e.g., `phase-2-adr-003-queue-framing`). One branch per ADR-implementation-unit, matching the commit granularity above.

## 6. Pull Request Checklist

Every PR description must state, explicitly:

- [ ] Which Phase and which ADR(s) this PR implements (exactly the set listed in that phase's card — no more, no fewer).
- [ ] Which files were changed, and confirmation none outside the phase's "Files expected" list were touched (or, if one was, why — e.g., a shared logging touch-point per ADR-012).
- [ ] Which tests were added, and where they live in the shared native harness (`test/native/`).
- [ ] Which bench verification steps were run, with a link to or transcript of the evidence.
- [ ] Whether any of §4's "must also update" triggers apply, and confirmation they were done.
- [ ] Whether this PR closes any open ACR, and if so, which one.
- [ ] Confirmation that `PROJECT STATUS` was updated if this PR completes a phase.

## 7. Evidence Requirements

Restating the Master Plan's standing rule with one addition: **evidence is a durable artifact, not a claim.** A bench transcript, a captured packet, a database dump, a CI log, an eFuse summary printout — something a reviewer three years from now could open and independently verify. "I tested it and it worked" does not satisfy any Definition of Done in the ADR or Master Plan documents. Store evidence in a location retrievable by phase and ADR number (e.g., a per-phase evidence folder, or attached to the PR that closes that phase's work) — the exact storage mechanism is a tooling choice, not an architecture decision, but the requirement that it exist and be retrievable is not optional.

## 8. Architecture Change Request (ACR) Workflow

An ACR is raised the moment a frozen ADR decision proves impractical during implementation — never silently worked around (Rule 1, §1).

**ACR numbering:** `ACR-001`, `ACR-002`, ... sequential, never reused, tracked in `PROJECT STATUS`'s "Open ACRs" field.

**ACR template (one per document, stored alongside the ADR document):**

```
ACR ID:              ACR-0nn
Against ADR:         ADR-0nn
Raised by:           
Date:                
Specific sentence/decision found impractical:
                     (quote the exact ADR text)
Why it is impractical:
                     (what was actually encountered — a missing API, a wrong
                      number, a library limitation — with evidence)
Narrowest possible fix proposed:
                     (change only what must change; do not propose a
                      redesign when a parameter or a single mechanism swap
                      would resolve it)
Impact on other ADRs:
                     (does this ripple into any other frozen decision?)
Status:              OPEN / APPROVED / REJECTED / SUPERSEDES-ADR-0nn
```

**Approval:** an ACR requires the same review rigor as the original ADR — it is reviewed by whoever holds the `Architecture Owner` role in `PROJECT STATUS`, not unilaterally self-approved by the implementer who raised it. Implementation of the blocked step does not resume until the ACR is marked `APPROVED` (with any resulting ADR text change published as a new, explicitly-superseding ADR entry, per §4) or `REJECTED` (with an alternative path also approved before work resumes).

**No phase may be marked complete while it has an open ACR against any ADR it depends on.**

## 9. Code Review Checklist

A reviewer approving any PR under this governance confirms, explicitly:

- [ ] The change matches its cited ADR(s) exactly — no interpretation, no "close enough," no quiet improvement layered in.
- [ ] The change is within the current phase's scope per `PROJECT STATUS` and the Master Plan.
- [ ] Required tests exist and pass, in the shared native harness.
- [ ] Required bench evidence exists and is attached/linked.
- [ ] No file outside the phase's expected set was touched without explicit justification.
- [ ] No naming/style convention from §3 was violated.
- [ ] No new external dependency was introduced without an ADR.
- [ ] Documentation triggers from §4 were satisfied.
- [ ] If this PR closes a phase: `PROJECT STATUS` is updated in the same PR.

## 10. Release Checklist

The authoritative release checklist is `Firmware Phase-wise Implementation Master Plan.md`, Part 3, Section D — do not maintain a second copy here that could drift out of sync. This governance document adds exactly one standing rule on top of it: **no release is certified (Phase 11) while any ACR anywhere in the project remains open**, full stop, regardless of which ADR it was raised against.

## 11. Production Deployment Checklist

Distinct from *release* (the build is certified correct) is *deployment* (the certified build is actually rolled out to real devices/a real receiver). Before any fleet-wide rollout of a certified release:

- [ ] The release has been deployed to at least one canary device (or a small canary batch) first, not the full fleet simultaneously.
- [ ] The canary device's `HEALTH` record (ADR-002) has been observed for at least one full cycle of every periodic interval it reports (push, config-poll, OTA-poll, health) before wider rollout — silence or an abnormal `sd_status`/`sensor_plausible`/`reset_reason` value on the canary blocks further rollout.
- [ ] For any release touching ADR-005 (security/transport): the production server's certificate and pinned CA have been independently verified correct and not near expiry — per the Master Plan's Phase 5 stop condition, this is unrecoverable-without-a-physical-visit if wrong.
- [ ] For any release touching ADR-001/ADR-003 (schema/storage): the forced full-drain precondition has been confirmed for every device receiving the update, individually, not assumed from a sample.
- [ ] A rollback trigger threshold is defined before rollout begins (e.g., "if more than N% of canary/early-wave devices fail to health-confirm within X minutes, halt the rollout") — not decided reactively after something looks wrong.
- [ ] The receiver-side counterpart of this release (Phase 10 scope, if applicable) is already live and verified before any firmware carrying the corresponding change reaches a device — never the reverse order.

## 12. Long-Term Maintenance Rules

- **This document, the Master Plan's `PROJECT STATUS` block, and `SCHEMA_REGISTRY.md`/`VERSIONS.md` are living documents** — updated as work proceeds. The Audit, the Blueprint, and each individual ADR entry are not — they are amended only by a new, superseding ADR, never rewritten in place.
- **Re-review cadence:** `PROJECT STATUS`'s `Last Reviewed` date should not go stale for more than one phase's worth of elapsed work without an explicit re-confirmation that the status block still reflects reality.
- **If a future engineer or agent believes this governance document itself needs to change** (a new rule, a relaxed constraint), that is itself a change to be proposed explicitly and reviewed by the `Architecture Owner` — it is not amended silently mid-implementation, for the same reason ADRs aren't.
