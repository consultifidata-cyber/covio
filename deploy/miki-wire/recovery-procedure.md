# Recovery Procedure — Miki Wire MW-001

## A. Firmware rollback (candidate misbehaves)
1. USB serial console: capture `show` + the failure symptom (photo/log).
2. From repo @ snapshot `73d82ff`: `pio run -e esp32dev-8di8do-npn -t upload`
   (or flash `rollback/covio-mw001-deployed-1.0.0-73d82ff.bin`, sha256
   `082954a4…5805`). This is the exact source state deployed 2026-08-04.
3. NVS survives — no reprovisioning needed. Verify `show` values intact.
4. Verify backend Online + totalizer continuity. The lifetime pulse total is
   preserved on flash by both firmwares (same checkpoint format).

## B. Telemetry queues but never ACKs (seq/ack divergence)
Console: `reset_ack` → fast-forwards the ack cursor to the current write
position. Touches only the queue AckRec; NVS, calibration, lifetime total
untouched. Then `reboot`.

## C. "No more free space" on every append (queue partition full)
Console: `recover_queue` → deletes the exhausted segment file and resets
write cursors; unsent backlog is lost, lifetime total is NOT. (The candidate
firmware's rollover makes this condition structurally unlikely; on the
rollback firmware it recurs after ~27 h of uptime — known limitation of the
deployed build.)

## D. Device unreachable on WiFi
Power-cycle; if boot-time STA connect fails 15 s, the device opens AP
`Covio-Setup-<last4>` with the captive portal for credential re-entry.
Counting continues during AP mode on the candidate firmware.

## E. Full reprovision (last resort — destroys NVS config, NOT totals)
Console: `factory` (one word — **no confirmation prompt; do not type it
casually**) → NVS wiped, reboots to defaults; then `set url`, `set key`
(requires a key re-issue or the original raw key), `set wifi`, `reboot`.
The queue, lifetime total, and OTA security floor survive a factory reset.
