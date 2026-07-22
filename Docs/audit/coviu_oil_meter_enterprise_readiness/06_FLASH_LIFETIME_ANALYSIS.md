# 06 — Flash Lifetime Analysis

**Verdict: NOT PROVEN.** This section states its assumptions explicitly, per the mandate's own instruction, rather than presenting a false-precision number.

## Write frequency by file (real, from code)

| File | Write trigger | Real-world frequency |
|---|---|---|
| `/queue/seg_NNNNNN.bin` | Every telemetry tick with data to append | Every `TELEMETRY_PERIOD_MS` = 1s, append-only (low wear per write — append, not rewrite) |
| `/totA.bin` / `/totB.bin` (alternating) | Every `totalizer.service()` call | Every 1s — this is the **hottest** file pair in the system: a full-slot rewrite roughly every 2 seconds per slot, forever |
| `/queue/ackA.bin` / `/queue/ackB.bin` (alternating) | Only on a confirmed cumulative ack | Up to every `PUSH_PERIOD_MS` = 5s, but only when there's new data to ack — meaningfully less frequent than the totalizer checkpoint |
| NVS (`server_url`,`api_key`,`wifi_*`,`cfg_ver`,`boot_id`,`logical_id`) | Provisioning events, boot_id incremented once per boot | `boot_id` writes once per power-cycle; everything else is rare (operator-driven) |

## The math, assumptions stated explicitly

Assumption 1: the totalizer checkpoint is the dominant wear driver, since it's rewritten far more often than anything else in the system (roughly every 2 seconds vs. every 5+ seconds for the ack cursor, and vs. pure appends for queue rows, which are cheaper writes than a full-file rewrite).

Assumption 2: LittleFS's internal wear-leveling correctly rotates the physical blocks backing a small, repeatedly-rewritten file across its available pool, rather than pinning it to one physical block forever — this is LittleFS's documented design intent, not something this audit independently measured on this specific flash chip.

Assumption 3 (unverified): the NOR flash chip's rated erase-cycle count. Typical consumer-grade SPI NOR flash is commonly rated for 100,000 P/E cycles per block; some parts are rated higher. **This audit does not know the exact chip on the connected unit or its datasheet rating** — the connected board is described in `platformio.ini`'s own comment as an "unbranded/clone module," which further increases uncertainty about its actual rated endurance versus a name-brand part's datasheet.

Given assumptions 1-3:
- Checkpoint rewrites/day ≈ 43,200 (once per ~2s, alternating across 2 slots → ~21,600 physical writes to each of the two slots' underlying blocks per day, before considering wear-leveling's block rotation).
- Over a 5-year field life: ≈ 39.4 million checkpoint writes total across both slots.
- If wear-leveling spreads these across, say, even a modest 50-block rotation pool (a conservative assumption, not a measured fact), each physical block would see roughly 788,000 erase cycles over 5 years — this would **exceed** a 100,000-cycle-rated chip's endurance by roughly 8x.
- If the rotation pool is instead the LittleFS default's more typical hundreds-to-low-thousands of blocks for a partition this size, per-block cycling drops into a plausible, safe range.

**This range (safe vs. 8x over-budget, depending entirely on an unverified rotation-pool-size assumption) is exactly why this section is marked NOT PROVEN rather than given a single lifetime number.** A real measurement (either a datasheet lookup for the actual flash part, or a long-duration accelerated-write endurance test against a real unit) is required before this device can be certified for multi-year unattended field service. It is not evidence the device *will* fail — it is evidence this specific question has not been answered with real data.

## Recommended concrete follow-up (not performed in this audit)
1. Identify the exact flash chip part number on a production unit (via `esptool.py flash_id`, a read-only query — deferred in this audit per the agreed non-destructive scope, safe to run in a dedicated hardware-verification pass) and pull its datasheet-rated P/E cycle count.
2. Either instrument LittleFS to report its actual block-rotation behavior for this specific access pattern, or run an accelerated-writes soak test (e.g., artificially shorten the checkpoint interval and run for a long duration, extrapolating) to get a real measured lifetime bound rather than a theoretical one.
3. Consider whether the totalizer checkpoint interval genuinely needs to be every second — the architecture's own tradeoff (minimal data-loss window vs. flash wear) could be revisited if the measured wear turns out to be a real constraint (e.g. checkpoint every N pulses or every few seconds instead of unconditionally every tick), but this is a design decision for the engineering owner, not something this audit recommends unilaterally.
