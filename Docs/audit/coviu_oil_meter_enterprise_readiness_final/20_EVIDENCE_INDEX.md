# 20 — Evidence Index

Given the scale of the full mandate (21 parts, ~300+ individual
sub-questions), this index provides one row per **Part** plus one row
per **individually critical finding** — every sub-question's full answer
lives in the referenced document, with no blank entries anywhere in this
audit. This structure keeps the index itself readable while still
satisfying "no row may be blank" — nothing is answered "TBD."

| ID | Topic | Current answer (summary) | Evidence type(s) | Remaining uncertainty | Severity | Pilot blocker | Prod blocker | Unattended blocker | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| P0 | System identity/baseline | All 4 identity sources (source/binary/flashed/API) match exactly | Direct command evidence, this session | None | — | No | No | No | Pass |
| P1.10 | Max unsynced capacity | Theoretical ~98,304 rows (~1.14 days at 1Hz); live usage shows real capacity likely lower | Real partition-table read + live device metrics | True fill-to-capacity not tested | P2 | No | Yes | Yes | Gap |
| P2.11 | Flash lifetime | Sound design (segmented, CRC'd, wear-leveled by LittleFS); not measured | Code inspection + arithmetic | No real long-duration wear measurement | P2 | No | Yes (before long-term claim) | Yes | Likely-acceptable, unproven |
| P3 | Power-failure recovery | Sound code-level design (CRC, truncate-before-append, cursor-then-delete ordering); real physical power-cut not performed | Code inspection + 8 real (non-power-loss) reboots | No genuine power-cut test; no native fault-injection re-executed (no compiler) | P1 (until measured) | No | Yes | Yes | Code-proven, execution-unverified |
| P4 | Guaranteed delivery / idempotency | At-least-once transport + exactly-once server storage, proven live | **Real HTTP tests this session** (3x duplicate POST → 1 row) | None for the tested paths | — | No | No | No | Pass |
| P5 | Offline sync engine | Works correctly for minutes/hours; "several days" fails at current sampling rate | Real live evidence (short-duration) + capacity math | 5-day claim not achievable as configured | P2 | No | Yes | Yes | Partial pass / gap |
| P6 | Queue management | Durable append-only + cumulative ack cursor (not a 4-state model); sound | Code inspection; corruption injection not executed | Corrupt-row/full-disk not executed this session | P2 | No | No | Yes | Sound design, partially unverified |
| P7 | Acknowledgement mechanism | Fail-closed on malformed/empty/stale acks, proven live | **Real HTTP tests this session** | Future-ack bounds-trust noted, not a flaw for this trust model | — | No | No | No | Pass |
| P8 | Configuration management | Only calibration is genuinely remote; everything else needs physical/AP access or reflash | Code inspection | None | P2 | No | Yes (for a real fleet) | Yes | Partial |
| P9 | Firmware updates / OTA | Signature/hash/hw-compat/downgrade/interruption all real-hardware-proven; bootloader rollback proven ABSENT | **Extensive real hardware evidence, this whole chain** | None on what was tested; rollback gap is structural | P1 (rollback) | No (with USB-present policy) | No (same) | **Yes** | Mixed — mostly pass, one structural gap |
| P10/11 | Watchdog/self-healing/time | WDT/brownout enabled (config-confirmed); several failure classes don't self-heal (sensor silence, fs-mount-fail) | Config + code inspection + real reboot evidence | Deliberate stall/OOM not tested | P1/P2 | No | Yes (sensor alarm) | Yes | Mixed |
| P12 | Security | No secure boot/flash encryption; test signing key; unauthenticated serial console leaks API key | **Direct config/binary/git inspection, this session** (including a corrected false alarm on the private key) | Rotation not exercised on live device | P1 | No (supervised) | Yes | Yes | Real gaps found |
| P13 | Diagnostics/health | Extensive real-time fields, all live-confirmed; several real gaps (sensor health, asset ID, OTA audit) | Live device reads, this session | None on existing fields | P2 | No | Yes | Yes | Mostly pass, gaps noted |
| P14 | Factory reset | Wi-Fi/API/calibration wiped, unauthenticated, no confirmation; floor/queue/totalizer/device-ID correctly preserved | Code inspection only (not executed on live device) | Not executed | P2 | No | Yes | Yes | Sound floor-protection, weak access control |
| P15 | 40-scenario failure matrix | 28/40 real-evidence pass; 3 confirmed real gaps; several sound-but-unexecuted | See doc 13 in full | Per-row, see doc 13 | Mixed | Partial | Partial | Partial | See doc 13 |
| P16 | Plant endpoint readiness | **Still the bench address, live-confirmed** | **Direct, this session** | None — confirmed fact | **P0** | **Yes** | **Yes** | **Yes** | **FAIL** |
| P17 | Sensor/calibration | No real flow ever observed this entire audit trail | Live device reads across the whole session | Total — needs a physical person+sensor+oil | P0 (for accuracy claims) | No (manual cross-check required) | **Yes** | Yes | **NOT CERTIFIED** |
| P18 | Long-duration stability | No 30-day run has occurred; filesystem-usage upward trend observed | Real session-long observation | Full 30-day soak not done | P2 | No | Yes | Yes | Not started |
| P19 | Enterprise success criteria | See doc 17 — mixed | See doc 17 | See doc 17 | Mixed | — | — | — | See doc 17 |
| P20 | End-to-end reconciliation | Exact balance, zero unexplained difference | **Real, live, this session** | None | — | No | No | No | Pass |
| P21 | Severity/gap classification | 3 P0, 6 P1, 6 P2, 3 P3 gaps identified | See doc 18 | — | — | See doc 18 | See doc 18 | See doc 18 | See doc 18 |

## Where full detail lives

Every summary row above expands into its full evidence, reasoning, and
exact quotes/commands in the correspondingly-numbered document in this
same directory (`02` through `19`). This index is a navigation aid, not
a replacement for reading the underlying document for any row the reader
needs to act on.
