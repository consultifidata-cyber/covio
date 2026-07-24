# 04 — Power-Failure Recovery (Part 3)

## Honest capability disclosure, upfront

This session has **no ability to physically cut power** to the device
(no physical actuator; the only available interactions are USB
flash/serial and network). It also has **no host C++ compiler**, so the
native fault-injection harness (`test/native_cpp/test_queue_fault_injection.cpp`,
built against `storage_backend.h`'s fake backend specifically to
simulate write/truncate failures) could not be compiled or run this
session — it remains, as disclosed in every prior document this chain
produced, **written and statically reviewed, not independently
executed by this session**. Earlier documents' claim that this harness
was "genuinely executed... 15/15 passing" (`17_FLASH_HARNESS_EXECUTION_RESULTS.md`,
an earlier phase) is **a lead, not proof re-verified here** — this
session cannot confirm or deny it directly, and says so plainly rather
than repeating it as established fact.

**What this session DID do, real and direct**: observed and recorded
every one of the many real reboots (both `ESP.restart()`-triggered and
USB-flash-triggered) that occurred across this entire audit trail, and
confirmed recovery behavior on the actual device each time. These are
real reboots, not simulations — but they are **software/flash-triggered
resets, not power-loss events**. The distinction matters and is kept
explicit throughout this document.

## Real, this-session reboot evidence (software/flash-triggered, not power-loss)

| Trigger | Observed recovery | Evidence |
|---|---|---|
| `reboot` console command (this exact remediation chain's own Part-2-equivalent test) | `boot_id` +1 exactly, `accepted_security_floor` unchanged, queue/DB fully contiguous before/after | Direct, this session |
| USB reflash (6 separate times across this whole chain) | Filesystem mounts (`sd_status:"ok"` every time), no boot loop, queue/NVS preserved every time (NVS/LittleFS partitions never in the flash write set) | Direct, this session |
| Genuine OTA-triggered reboot (2 separate successful installs) | Clean single-increment `boot_id`, queue continuity preserved, server sequence contiguous | Direct, this session |
| Deliberately truncated/interrupted OTA download (2 attempts) | NO reboot occurred at all (correctly — `Update.abort()` never reaches `ESP.restart()`) | Direct, this session |

**None of these is a genuine power-loss event.** Every one is either a
deliberate software restart or a controlled flash-and-reset via
`esptool`'s own RTS-pin mechanism — all "clean" in the sense that RAM
state is discarded in an orderly `ESP.restart()`/bootloader-jump, not an
abrupt, mid-instruction power collapse.

## Answering the mandate's specific questions

- **Can the filesystem become corrupted?** Not observed this session
  across ~8 real reboots — but genuine mid-write power-loss corruption
  was never actually induced (see disclosure above). Code-level defenses
  exist (CRC32 per row, dual-slot metadata, truncate-before-append) —
  proven by direct code reading (doc 03), not by an executed fault
  injection this session.
- **Can one row be lost?** **Yes, by design, in one specific narrow
  window** — doc 03 §9's "During checkpoint write" row: a row durably
  written but not yet checkpoint-confirmed is discarded (truncated away)
  on the next boot's recovery scan. This is a real, disclosed,
  *intentional* trade-off (never risk accepting a torn/unconfirmed row as
  valid), not a hidden defect.
- **Can multiple rows be lost?** Not by the code's own design — the
  truncate-before-append logic only ever discards the *unconfirmed tail*
  relative to the last good checkpoint, which should be at most one
  row's worth under normal operation (checkpoint advances immediately
  after each successful write, per queue.h line 339).
- **Can acknowledged data reappear?** No mechanism in the code re-reads
  or re-sends already-pruned segments — once a segment is deleted
  (post-ack, cursor-first-then-delete ordering), there is nothing left
  to "reappear."
- **Can unacknowledged data disappear?** Only via the one-row window
  above, or a genuine, unproven flash-level failure beyond what CRC/
  truncate-before-append defends against.
- **Recovery algorithm on boot**: `EventQueue::begin()` — loads
  whichever of `ackA.bin`/`ackB.bin` has the higher `writes` counter AND
  passes CRC (dual-slot fallback), same for `FailureState`, then (in
  production, non-native-test builds) `cleanupOrphanSegments_()` removes
  any segment numbered above the checkpoint's active segment (leftover
  from an interrupted rollover), then a **one-time, boot-only** full scan
  (`computeUnackedCount_()`) seeds the maintained backlog counter.
- **Is recovery bounded in time?** The one-time boot scan is O(pending
  rows) — bounded by however large the backlog is at boot, not
  unbounded in principle, but **not independently timed this session**
  against a large (e.g. near-`QUEUE_HIGHWATER`) backlog.

## Verdict for this part

**Recovery mechanism: code-proven sound in design (explicit ordering
comments in the source itself, matching every safety property this
audit asked about). Execution proof: real reboots proven safe (8+
observed, zero anomalies); genuine mid-write power-loss and native
fault-injection remain UNVERIFIED by this session** — both due to a
genuine capability gap (no physical power control, no host compiler),
disclosed rather than assumed away.
