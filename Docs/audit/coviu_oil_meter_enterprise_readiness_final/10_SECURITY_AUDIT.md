# 10 — Security Audit (Part 12)

## Methodology note — a real correction made during this audit

An initial broad `git ls-files | grep -i "test_signing_key"` appeared to
show the private signing key tracked in git — which would have been a
severe finding. **Rather than report it either way without checking**,
five independent verification commands were run:
`git ls-files server/tools/.test_signing_key.pem` (empty — not tracked),
`git ls-files -- "*.pem"` (empty — zero tracked `.pem` files anywhere),
`git ls-files | grep -i pem` (empty on re-run), `git log --all -- <path>`
(empty — never in history), `git check-ignore -v` (confirms it IS
correctly matched by the `*.pem` ignore rule). **Conclusion: the private
key has never been committed, is properly gitignored, and is not present
in git history at any point.** The earlier alarming-looking result is
attributed to a misread of combined multi-command output, not an actual
tracking issue — disclosed here rather than silently dropped, since
getting this kind of check right (not just fast) is the actual point of
a security audit.

## Direct answers

1. **Is the API token encrypted at rest?** No — `Preferences`/NVS stores
   it in plaintext within the NVS partition (standard ESP32 NVS
   behavior; NVS encryption is a separate, opt-in ESP-IDF feature, not
   enabled here — see #2).
2. **Is flash encryption enabled?** **No** — confirmed directly from the
   installed `sdkconfig`: `# CONFIG_SECURE_FLASH_ENC_ENABLED is not set`.
3. **Is secure boot enabled?** **No** — confirmed directly:
   `# CONFIG_SECURE_BOOT is not set`.
4. **Can physical access extract credentials?** **Yes** — with flash
   encryption off, anyone with physical/USB access and `esptool` can
   dump the NVS partition and read `server_url`/`api_key`/Wi-Fi
   credentials in plaintext. This is a real, unmitigated exposure for a
   device in a physically-accessible plant location.
5. **Can physical access modify firmware?** **Yes** — with secure boot
   off, USB access allows flashing arbitrary firmware (this is in fact
   the same legitimate mechanism this entire audit trail's own recovery
   procedure relies on — the same capability is both the recovery
   feature and the attack surface).
6. **Is HTTPS/TLS used?** **No, currently** — confirmed live, this
   session: `server_url` is `http://192.168.1.3:8000`. `certs.h`'s
   pinned-CA `WiFiClientSecure` path exists in code and is exercised
   whenever `server_url` starts with `https://` (`covioIsHttpsUrl()`
   dispatch, confirmed by code read) — it is simply not the scheme in
   use on this bench device today.
7. **Are production certificates provisioned?** **No** —
   `COVIO_CA_CERT_IS_PLACEHOLDER` remains set (confirmed: this is
   exactly why `pio run -e release` still fails closed at compile time,
   re-confirmed this session).
8. **Is the current device using a test signing key?** **Yes** —
   `covio-test-key-2026-07`, confirmed this session.
9. **Is the device safe for a customer plant under that key
   classification?** For a **controlled, supervised pilot with the
   OTA-policy restrictions this audit's plant-readiness series already
   documented** — acceptably scoped. **For unattended/normal production
   at genuine customer scale, no** — a test key with no formal key-
   governance process (rotation, revocation, multi-key trust) is not an
   acceptable long-term production posture, and this gap was already
   identified as RISK-19 in this project's own prior remediation
   history, independently re-confirmed still true this session (the key
   ID and classification are unchanged).
10. **Can one compromised test key update every device?** **Yes** — one
    signing key, one trust root (`ota_keys.h`'s single compiled-in
    public key, no multi-key/rotation support — confirmed by code read,
    matching this project's own already-disclosed RISK-19 finding). A
    single key compromise would let an attacker sign an "authentic"
    malicious update for **every device trusting that same compiled-in
    public key** — a real, fleet-wide blast radius, not device-specific.
11. **Per-device or fleet-wide credentials?** **Fleet-wide for the OTA
    signing key** (one key, all devices). **Per-device for the API key**
    IN DESIGN (`server.py`'s `/admin/devices/provision` mints a unique
    key per device) — but THIS specific bench device is still using the
    SHARED bootstrap key (`api_key_status:"default"`, confirmed live,
    unchanged this entire session), not yet migrated to a unique
    per-device key.
12. **Can credentials be rotated remotely?** The API key: yes, in
    design (`/admin/devices/<id>/rotate-key` mints server-side; applying
    it to the device still needs physical/AP-mode delivery, doc 07).
    Wi-Fi: no remote path, physical/AP-mode only. OTA signing key: no
    rotation mechanism exists at all (RISK-19, confirmed still open).
13. **Is rotation proven?** **No** — this session did not exercise
    `/admin/devices/<id>/rotate-key` against the real device (would
    rotate its actual active key, explicitly out of this session's
    execution boundary without separate authorization).

## Additional items from the mandate's checklist

- **JTAG state**: not independently probed this session (would require
  `espefuse.py` introspection not attempted); the board's native
  USB-Serial/JTAG peripheral is, by its nature, present and reachable
  via the same USB port used for programming — no evidence this session
  of JTAG being deliberately locked down.
- **UART exposure**: the serial console (`provision.h`) is reachable
  over the same USB-serial connection with **no authentication at all**
  — `show`, `set url`, `set key`, `set wifi`, `reboot`, `factory` are
  all available to anyone with a USB cable and no credentials. **`show`
  prints the raw `api_key` value in plaintext** (confirmed by direct
  code read, `provision.h` line ~50) — a real, direct secret-exposure
  path requiring only physical USB access, no authentication.
- **Rate limiting**: exists for admin HTTP routes (`server.py`'s
  `_admin_rate_limited`, per-source-IP, 5 failures/60s window,
  code-confirmed) — **does not exist for the serial console** (no
  rate limit on repeated `set key`/`show` attempts over serial, though
  this requires physical access regardless).
- **Replay protection**: none for the admin HTTP API (server.py's own
  prior-documented finding, `03_ENTERPRISE_CHECKLIST_15_SECTIONS.md`,
  independently re-confirmed by this session's code read — no
  nonce/timestamp on admin routes); OTA manifests DO have replay-
  adjacent protection via `issued_at`/`expires_at` + the device's
  server-time estimate (proven this chain).
- **Downgrade protection**: proven working, real hardware, this chain.
- **Debug interfaces**: `FACTORY_TEST_BUILD`'s
  `/api/v1/factory/provision` route is compiled OUT of the current
  `esp32dev`/bench build (confirmed — that flag is 0 for this
  environment) — the debug/factory write path is not present in what's
  actually flashed.
- **Secret leakage in logs**: the serial console's `show` command (above)
  is the one confirmed leak. Standard boot/telemetry logging does not
  print secrets (confirmed by reading every `Serial.print*` call in
  `covio_firmware.ino`/`ota.h`/`sync.h` this session and prior sessions —
  none echo `api_key`/Wi-Fi password).
- **Secret leakage in binaries**: none found — this session's own fresh
  `grep -a -c "PRIVATE KEY"` against the current compiled `.elf` found
  only mbedtls's benign generic PEM-label table (verified context, not
  a real secret, consistent with every prior check this session and
  chain).
- **Test keys in plant firmware**: **yes, confirmed** — this is the
  exact release candidate frozen for tomorrow's pilot (see the
  plant-readiness series' own doc 02/09), using the test signing key.
  Acceptable ONLY under the restricted pilot conditions already
  documented, not for broader deployment.

## Summary severity note

The serial console's unauthenticated `show`/`set key` capability (raw
API key exposure over USB, no auth) is a genuine, previously-
under-emphasized finding from this session's own fresh re-read of
`provision.h` — classified in `18_GAPS_AND_REMEDIATION_PLAN.md`.
