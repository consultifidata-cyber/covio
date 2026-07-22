# 01 — System Inventory (Phase 0)

## Repository
- Path: `C:\Users\Dell\Documents\ConsultiFi_Data\covio-main`
- **No `.git` directory exists anywhere in this tree.** There is no branch, no commit hash, no working-tree diff, no commit history to audit. This is confirmed by direct filesystem inspection (`ls -la .git` -> "No such file or directory") and by `git status` failing with "not a git repository." Any "commit"-based provenance claim about this codebase is therefore **ABSENT**, not merely unexecuted.
- Firmware project path: repository root (flat layout, `platformio.ini`'s `src_dir = .`, per ADR-013 — deliberately no `src/` folder).
- `.github/workflows/`: `ci.yml`, `desktop-package.yml`, `release.yml` exist as files, but with no git remote/no commits, none has ever executed on a real GitHub Actions runner. This audit did not attempt to create a remote or push anything — out of scope and would be a significant, irreversible, shared-state action requiring separate authorization.

## Toolchain (real, checked this session)
| Tool | Found | Version |
|---|---|---|
| Python | Yes | 3.11.9 |
| Flask | Yes | 3.1.3 |
| Node.js | Yes | v24.18.0 |
| npm | Yes | 11.16.0 |
| PlatformIO Core | Yes | 6.1.15 |

This directly contradicts `Docs/RE9_END_TO_END_VALIDATION_PLAN.md` / `Docs/RE10_FINAL_PRODUCTION_READINESS_REPORT.md`, both of which state plainly that no Python interpreter, no PlatformIO, and no compiler had ever been available in "this environment." Those reports were authored in a different, more constrained session. This session's environment is materially more capable, and this audit used that capability for real, non-destructive verification (see `evidence/`).

## Firmware build (real, this session)
- Build system: PlatformIO, `platform = espressif32@6.5.0`, `framework = arduino` (arduino-esp32 core 2.0.14).
- Board override: `esp32-s3-devkitc-1` (the connected unit is a genuine ESP32-S3, not the classic ESP32 the rest of the repo's docs assume — `platformio.ini`'s own `[env]` comment discloses this was already confirmed via `esptool` in an earlier session).
- Partition table: `default_16MB.csv` (OTA-capable, two app slots, ~3.4 MB LittleFS "spiffs" partition per the file's own comment — **not independently re-verified against the literal CSV bytes in this audit**, see §1's Data Persistence section for the load-bearing distinction between this comment and a byte-exact partition dump).
- Three build environments compiled for real this session — see `evidence/firmware_compile_results.log`:
  - `esp32dev` (bench/dev): **SUCCESS**, RAM 15.0%, Flash 15.6%, 27.44s.
  - `release` (`RELEASE_BUILD=1`): **FAILED by design** — `certs.h:50`'s compile-time guard correctly refused to build with the placeholder CA certificate in place.
  - `factory` (`FACTORY_TEST_BUILD=1`): **SUCCESS**, RAM 15.1%, Flash 15.8%, 84.05s.
- No `-t upload` was ever run. The physically connected device's currently-running firmware was never reflashed by this audit.

## Connected device (real, this session)
- Serial: `COM6`, USB descriptor `VID_303A&PID_1001` (Espressif's native USB-Serial/JTAG VID), consistent with an ESP32-S3.
- Identity (from real `/api/v1/info`): `hardware_id = esp32-F4E5B2858428`, `fw_version = 1.0.0`, `model = covio-oilflow-v1`, `boot_id = 18` at time of capture.
- `logical_device_id: null` — confirmed by both the live `/api/v1/info` response and a real serial-log NVS error (`getString(): nvs_get_str len fail: logical_id NOT_FOUND`). **This physical unit has never been through Factory Provisioning.**
- Reset reason observed during passive serial capture: `USB_UART_CHIP_RESET` (a known ESP32-S3 native-USB-CDC characteristic triggered by the OS opening the port — not a deliberate action, not a power-cut, not a brownout). See `evidence/serial_capture.log`.
- **Endpoint / environment determination (real, not inferred):** the live `/api/v1/status` shows `"server_url":"http://192.168.1.3:8000"` (a private LAN IP, matching the reference bench server's default port 8000) and `"api_key_status":"default"` (the placeholder `dev-key-change-me` key is still active). **This is conclusively a bench/dev configuration, not staging or production.** See `evidence/device_http_snapshot.json`.
- WiFi: connected to a network whose SSID matches `config.h`'s hardcoded `DEFAULT_WIFI_SSID`/`DEFAULT_WIFI_PASS` first-boot default exactly — i.e., this unit has never had its WiFi credentials changed from the firmware's build-time bootstrap values either. That SSID/password pair is a real, non-placeholder home-network credential (unlike `DEFAULT_API_KEY`, which is an obvious placeholder string) — **flagged as a live credential committed to source control**, see `07_SECURITY_REVIEW.md`.
- Secure Boot / Flash Encryption status: **NOT independently verified in this audit.** `Docs/RE10_FINAL_PRODUCTION_READINESS_REPORT.md` §7 disclosed both as not implemented (one-way eFuse burn, never performed). This audit did not run `espefuse.py summary` against the connected device — that was consciously deferred rather than risked without explicit authorization, since eFuse queries interact with security-relevant hardware state on a real unit and the audit's scope was agreed as "static analysis + safe local checks only." **Mark NOT PROVEN**, not "confirmed absent," though the source code has no Secure Boot/flash-encryption configuration to be found either (see `07_SECURITY_REVIEW.md`).

## Backup status
No backup of NVS, LittleFS queue/totalizer contents, or the device's current configuration was taken from the physical unit — this would require either a `pio run -t uploadfs`-adjacent read-back tool or `esptool.py read_flash`, neither of which was run (out of the agreed non-destructive scope, and not needed since nothing risky was subsequently done to the device). The device's queue/totalizer state was left completely untouched other than the one incidental USB-CDC reset described above, across which it recovered correctly (see `evidence/serial_capture.log`).

## Test ledger
No fault-injection test campaign was run (no relay, no pulse generator — see `04_FAILURE_INJECTION_RESULTS.md`). The "test ledger" the mandate requested (test ID / sequence range / pulses generated / reboots / reconciliation) has no rows to report beyond the single incidental reset described above, whose before/after totalizer and queue state is fully captured in `evidence/serial_capture.log`.
