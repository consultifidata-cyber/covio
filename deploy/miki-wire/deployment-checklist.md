# Miki Wire Site Deployment Checklist — MW-001

**Target device:** `esp32-F84AD1A172E0` — Miki Wire Line 1, RANCHI, Wire
Drawing Machine 1. Backend: `https://compliance.mikigroup.co.in`.

**GATE: this package may only be deployed after the BLOCKED rows of
`Docs/audit/miki_wire_hardening/PHASE_2_VALIDATION_MATRIX.md` have been
executed on the bench and passed.** As shipped, the matrix gate verdict is
DO NOT DEPLOY.

## 1. Identify
- [ ] Serial console @115200 → `show` → confirm `device_id: esp32-F84AD1A172E0`
- [ ] Record current firmware line from boot banner (expect `1.0.0`, `build_commit=` of the running image) and `diag` counters

## 2. Pre-flash state capture (rollback insurance)
- [ ] `show` output photographed/saved (server_url, api_key fingerprint, wifi_ssid, calib, diag)
- [ ] Confirm backend shows the device Online and note current totalizer value on the dashboard

## 3. Verify firmware hash
- [ ] `sha256sum firmware/covio-mikiwire-1.0.0-41e31af.bin` matches `firmware.sha256`
      (`c55c0031…11d8`)

## 4. Flash (USB — there is no OTA path to this device)
- [ ] Machine stopped / production paused with operator agreement
- [ ] `pio run -e esp32dev-mikiwire -t upload` from repo @ `41e31af` (or
      esptool with the packaged .bin at 0x10000 + matching boot/partition
      images — prefer the PlatformIO route)
- [ ] NVS is NOT erased by a normal flash — server_url/api_key/wifi survive.
      **Never** use `-t erase` here.

## 5. Configure / verify config survived
- [ ] `show`: server_url, api_key fingerprint, wifi_ssid unchanged from step 2
- [ ] Boot banner shows `[MIKI] profile active: maxhz=0 suspect_gap_s=0 (0=off)`
      and `[WDT] task watchdog armed: 60s`

## 6. Verify sensor
- [ ] With machine briefly jogged / target passed by hand: totalizer increments
      (serial `show` or `/api/v1/status` on the plant LAN)

## 7. Verify network + server
- [ ] `[SYNC] acked_seq=…` lines advancing; backend dashboard Online; pulse
      count mirrors the on-device totalizer

## 8. Pulse test
- [ ] Known pulse count (e.g. 50 hand passes) → totalizer delta == 50 →
      backend total advances by 50 (validation matrix B1, now on-site)

## 9. Telemetry confirmation window
- [ ] 30 min continuous: queue `pending` steady-state ~0, zero failed writes,
      no watchdog resets (`diag` counters unchanged)

## 10. Threshold arming (ONLY with plant-verified numbers)
- [ ] Complete `MIKI_WIRE_SENSOR_CHARACTERIZATION.md` measurement sections
- [ ] `set maxhz <derived>` / `set suspects <derived>` per its §4 derivation
      rules — never guessed values

## Rollback (any failure above)
Follow `recovery-procedure.md` — flash
`rollback/covio-mw001-deployed-1.0.0-73d82ff.bin` (byte-identical source
state to the release that has been running since 2026-08-04).
