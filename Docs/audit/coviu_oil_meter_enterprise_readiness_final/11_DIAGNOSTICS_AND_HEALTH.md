# 11 — Diagnostics and Health Monitoring (Part 13)

## Field-by-field (all confirmed present via live `/api/v1/info`,
`/api/v1/status`, `/api/v1/metrics`, `/api/v1/health` this session)

| Field | Endpoint | Source of truth | Refresh | Persisted? | Hardware-proven this session/chain? |
|---|---|---|---|---|---|
| Firmware version | `/api/v1/info` | `FW_VERSION` compile constant | Static per boot | No | Yes |
| Build commit | `/api/v1/info` | `BUILD_COMMIT` (injected at compile time) | Static per boot | No | Yes — exact-match proven repeatedly |
| Dirty flag | `/api/v1/info` | `BUILD_DIRTY` | Static | No | Yes |
| Build time | `/api/v1/info` | `BUILD_TIME_UTC` | Static | No | Yes |
| Security version | `/api/v1/info`, `/status` | `FW_SECURITY_VERSION` | Static | No | Yes |
| Accepted security floor | `/api/v1/info`, `/status` | NVS (`covio_sec` namespace) | Updates on confirmed healthy boot only | **Yes, NVS** | Yes — advance + persistence both proven |
| Uptime | `/api/v1/status`, `/metrics` | `millis()` | Continuous | No | Yes |
| Boot ID | `/api/v1/info` | NVS (`covio` namespace) | Increments once/boot | Yes | Yes |
| Wi-Fi RSSI | `/api/v1/status`, `/metrics` history | Live radio read | ~1/s (metrics history) | No | Yes |
| IP/connectivity | `/api/v1/status` (`wifi.connected`) | Live | Continuous | No | Yes |
| Free heap | `/api/v1/metrics` | Live | On request | No | Yes |
| Minimum free heap | `/api/v1/metrics` | `esp_get_minimum_free_heap_size()` | Cumulative-min since boot | No | Yes |
| Flash usage | `/api/v1/metrics` (`flash_size_bytes`) — **NOT flash USED, just total chip size** | `ESP.getFlashChipSize()` | Static | No | Real gap: no "flash bytes used" metric exists distinct from the LittleFS-specific `capacity_pct_used` |
| Filesystem usage | `/api/v1/status` (`queue.capacity_pct_used`) | Real `LittleFS.usedBytes()/totalBytes()` | Live | No | Yes, live this session |
| Queue depth | `/api/v1/status` (`queue.backlog`) | Maintained counter (`unackedCount_`) | Live | No (recomputed at boot via one-time scan) | Yes |
| Queue first sequence | **Not directly exposed** — only `last_seq` (highest ever locally generated) and `acked_seq` are exposed; the OLDEST unacked sequence is not a distinct field | — | — | — | **Gap**: cannot directly answer "what is the oldest pending record" from the API without inferring it |
| Queue last sequence | `/api/v1/status` (`queue.last_seq`) | `Totalizer::lastSeq()` | Live | Yes (NVS/flash) | Yes |
| Highest acked sequence | `/api/v1/status` (`queue.acked_seq`) | `AckRec` | Live | Yes | Yes |
| Last sync | `/api/v1/status` (`last_sync_ms_ago`) | `Sync::lastSyncMs()` | Live | No | Yes |
| Last server contact | Same field, plus `last_push_http_code` | Live | Live | No | Yes |
| Last failure | `/api/v1/status` (`last_write_failure`), `/metrics` reset_reason | `FailureState` (queue write failures); reset reason (device-level) | Live | Yes (FailureState survives reboot) | Yes |
| OTA state | `/api/v1/status` (`ota.state`) | RAM, derived each boot from `noteBoot()` + `confirmHealthyBoot()` | Live | No (re-derived each boot) | Yes |
| OTA rejection reason | `/api/v1/status` (`last_reject_reason`, `last_auth_reject_reason`) | RAM, set by `poll()` | Live, per poll attempt | No | Yes, both reasons proven live |
| Running partition | `/api/v1/status` (`ota_debug.running_partition`) | `esp_ota_get_running_partition()` | Static per boot | No | Yes |
| Boot partition | `/api/v1/status` (`ota_debug.boot_partition`) | `esp_ota_get_boot_partition()` | Static per boot | No | Yes |
| Rollback engaged | `/api/v1/status` (`ota_debug.bootloader_rollback_engaged`) | Set by `confirmHealthyBoot()` | Per confirmation | No | Yes — proven `false` every single time, honestly |
| Reset reason | `/api/v1/metrics` (`reset_reason`) | `esp_reset_reason()`, mapped string | Static per boot | No | Yes |
| Raw reset reason | `/api/v1/metrics` (`reset_reason_raw`) | Same, raw int | Static per boot | No | **Yes — this session resolved the previously-open `"unknown"` question: raw value 0 = genuine `ESP_RST_UNKNOWN`** |
| Sensor status | **No dedicated field** — only `pulse_frequency_hz` (via `/api/v1/metrics`) implies sensor activity indirectly; no explicit "sensor connected/disconnected" boolean or alarm exists | — | — | — | **Real gap** — confirmed this session: the sensor has read zero activity this entire audit trail with no alarm ever raised for it |
| Pulse count | `/api/v1/metrics` (`pulse_frequency_hz`), `/api/v1/status` (`totalizer_raw_pulses` is the cumulative count) | PCNT via Totalizer | Live | Yes (flash/NVS checkpoint) | Yes, cumulative count proven persistent across every reboot this session |
| Totalizer | `/api/v1/status` (`totalizer_raw_pulses`) | Same | Live | Yes | Yes |
| Alarm state | `/api/v1/health` (`alarms` array) | `diagnostics.h`'s alarm builder | Live | No | Yes — `OTA_FAILED` alarm proven to fire correctly live, this chain |
| Health state | `/api/v1/health`, `/api/v1/status` | Derived (queue/sync/ota/storage precedence) | Live | No | Yes |

## Missing enterprise diagnostics (real gaps identified this session)

1. **No sensor-health signal** distinct from raw pulse rate — no
   "sensor disconnected" alarm.
2. **No queue's oldest-pending-record age** exposed directly (only
   inferable from `last_seq - acked_seq`, not an actual age-in-time).
3. **No true flash-bytes-used metric** distinct from the LittleFS-
   specific percentage (nothing for NVS/app-partition headroom).
4. **No plant/logical-asset identity** exposed (doesn't exist, per
   doc 07).
5. **No OTA audit trail with "who approved this rollout"** (doc 08).

## Diagnostics security/robustness

- **Leak secrets?** No (confirmed this session — every diagnostic
  endpoint's field set was read directly from `diagnostics.h`; none
  include raw `api_key`/Wi-Fi password; `api_key_status` is
  deliberately a redacted enum).
- **Crash under malformed requests?** Not independently fuzzed this
  session (all four endpoints are simple `GET`s with no
  request-body parsing at all — the attack surface for a malformed
  *request* is minimal by construction; the malformed-input tests this
  session actually performed were against the server's `POST` push
  endpoint, doc 05, not these local `GET`-only diagnostic routes).
- **Accessible without authorization?** **Yes, entirely unauthenticated**
  — `local_api.h`'s own header comment states this explicitly ("read-only
  and unauthenticated by design") — anyone on the same network segment
  as the device can read every field above with no credentials. This is
  a deliberate design choice (masked/no-secrets contract) but is still a
  real information-disclosure surface (RSSI, queue depth, OTA state, etc.
  visible to anyone on the LAN) worth stating plainly for a plant network
  threat model.
- **Available during degraded states?** Yes, confirmed — every
  degraded-health observation this session (offline, degraded OTA
  failure) still successfully answered `/api/v1/status`/`/health`
  queries; the diagnostics server did not itself fail when the rest of
  the system was unhealthy.
