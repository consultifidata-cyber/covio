# Known Risks — Miki Wire Deployment Package

1. **Hardware validation incomplete.** Every physical test in
   `PHASE_2_VALIDATION_MATRIX.md` is BLOCKED as of packaging (no bench
   hardware was available). The candidate is host-test-proven only. The
   matrix gate verdict is DO NOT DEPLOY until the bench work passes.
2. **Monitors ship inert.** Plausibility ceiling and idle threshold are 0
   (off) pending real line measurements — the new advisories provide no
   protection until armed per the characterization doc.
3. **Sensor supply/wiring unverified** (IVI): site supply rail and actual DI
   wiring must be measured before trusting counts (matrix A2/A3).
4. **RELEASE_BUILD=0 image.** The candidate is a bench-profile build: no
   default-key refusal, serial console and LAN diagnostics fully open (as
   today's deployed firmware). `release-mikiwire` exists but requires the
   RELEASE_BUILD_DIFF.md review + provisioning verification first.
5. **No OTA path.** Any future fix is another site visit (USB). The Miki
   backend deliberately serves no OTA manifests.
6. **Unauthenticated serial console** including one-word `factory` wipe
   (physical USB access required). Replacement mechanism proposed
   (matrix L1) but not implemented — do not leave the USB port accessible
   to untrained staff.
7. **LAN diagnostics API is unauthenticated** and a slow LAN client can
   stall telemetry cadence (Phase-0 F11, unmitigated by deliberate scope
   choice). Keep the device on a trusted plant VLAN.
8. **Rollback firmware carries the original queue-fill defect** (~27 h of
   uptime to a full partition; manual `recover_queue` needed). Rollback is
   for emergencies, not extended operation.
9. **Non-byte-reproducible builds.** BUILD_TIME_UTC is embedded; verify the
   artifact you flash against `firmware.sha256`, not a rebuilt binary.
