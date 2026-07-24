# 07 — Recovery Kit Verification (Part 9)

## Items confirmed present/working, this session and this whole chain

```
[x] Laptop with PlatformIO installed        -- this exact machine, Core 6.1.15
[x] Working USB cable                        -- used for 7+ successful flashes
                                                this engagement, including this
                                                phase's own flash
[x] Correct USB driver                       -- COM6 correctly enumerates as
                                                VID_303A&PID_1001 every time,
                                                no driver issue ever observed
[x] Flashing tool                            -- `pio run -e esp32dev -t upload
                                                --upload-port COM6`, proven 7+
                                                times, including this phase
[x] Known-good firmware binary               -- THIS PHASE'S OWN candidate is
                                                now the known-good baseline:
                                                covio_firmware, commit
                                                443dc448423b9fec2a48fb4bf7ac02740e6c2341,
                                                SHA-256
                                                e0cfb551adaf3422f234f831317dd62efccd518742c560d063e3307151d04ba6
[x] Firmware SHA-256                         -- above, independently
                                                recomputed and matched this
                                                session
[x] Exact flash command                      -- documented, doc 02
[x] Post-flash validation commands           -- /api/v1/info, /status, /health
                                                (exact fields specified,
                                                doc 02 of this phase)
```

## Items requiring onsite confirmation (not verifiable remotely)

```
[ ] Responsible recovery person (named, physically present during any
    OTA window) -- not this report's to name
[ ] Escalation contact -- not populated by this or any prior report;
    the business must name a specific, reachable person/role
```

## Dry-run vs. real flash this phase

**A REAL flash was performed this phase** (the credential-redaction
fix itself required one) — using the exact same laptop, cable, and
command as every prior recovery-relevant flash this engagement. This
counts as fresh, real proof of the recovery mechanism, not merely a
dry run, satisfying the mandate's own "use the prior verified USB-flash
evidence if the same laptop and cable are present" allowance with
something stronger: a flash actually executed THIS session.

## Mandatory OTA restrictions (unchanged from the enterprise re-audit, restated because bootloader rollback remains absent)

- No OTA without this recovery kit physically present.
- No OTA during active production.
- No unattended OTA, ever, on this hardware configuration.
- The current known-good firmware (this phase's own artifact) must
  remain locally available on the recovery laptop at all times during
  the pilot.

## Verdict

`RECOVERY KIT: VERIFIED` (mechanism and artifacts); **onsite named-person
and escalation-contact items remain open, business-side action items**.
