# 19 — Plant Go/No-Go (Part 16 + Mandatory No-Go Conditions)

## Part 16 — Real Plant Endpoint Readiness (re-verified live, this session, not assumed)

```
GET /api/v1/info (live, this session):  server_url NOT in this endpoint
GET /api/v1/status (live, this session): "server_url":"http://192.168.1.3:8000"
```

**The device is still pointing at the bench address.** Checked directly,
not assumed to have been corrected by a prior report.

```
Is it the real approved plant endpoint?           NO
Is it 192.168.1.3:8000?                            YES -- exactly the
                                                     address this session
                                                     independently
                                                     confirmed is THIS
                                                     WORKSTATION's own
                                                     LAN IP (matched via
                                                     ARP earlier this
                                                     whole engagement)
Bench address in compiled defaults?                YES -- config.h's
                                                     DEFAULT_SERVER_URL
                                                     is still
                                                     "http://192.168.1.3:8000"
                                                     (this is the
                                                     FIRST-BOOT default,
                                                     overridden by NVS
                                                     after provisioning
                                                     -- but the CURRENT
                                                     NVS value is ALSO
                                                     this same bench
                                                     address, confirmed
                                                     live)
Bench address in test manifests?                   YES -- every signed
                                                     manifest this whole
                                                     engagement pointed
                                                     at 192.168.1.3:8000
                                                     or :8001 (test-only,
                                                     never served to any
                                                     other device)
Bench address in OTA manifest / Device Manager?     Not separately
                                                     checked this session
                                                     (tools/device-manager
                                                     not exercised)
TLS hostname matches?                               N/A -- no TLS in use
Credentials appropriate for plant?                  NO -- api_key_status
                                                     still "default"
                                                     (shared bootstrap
                                                     key), Wi-Fi is the
                                                     bench network's own
Device ID / plant ID match server registration?     Plant ID doesn't
                                                     exist as a concept
                                                     (doc 07); device_id
                                                     is only registered
                                                     in the BENCH
                                                     database
First server contact succeeds?                      Yes, but only
                                                     against the BENCH
                                                     server -- meaningless
                                                     for plant readiness
Records land under correct device/plant?             Correct device,
                                                     no plant concept
                                                     exists at all
```

## Required safe reprovisioning step (not performed — reported, per instruction not to silently reprovision)

```
1. Physically connect via serial console (USB) or trigger AP-mode.
2. `set url https://<real-plant-endpoint>`   (note: MUST be https:// for
   a genuine production endpoint -- the pinned-CA path exists in code
   and should be exercised for real, not left at http://)
3. `set key <the-real-per-device-API-key>`   (obtain via
   POST /admin/devices/provision against the PRODUCTION server, not
   the bench stub)
4. `set wifi <plant-ssid> <plant-password>`
5. `reboot`
6. Verify via `/api/v1/status`: server_url is now the plant endpoint,
   api_key_status reads "configured" (not "default"), wifi.connected
   is true against the plant network, last_push_http_code becomes 200
   against the REAL plant server.
```

## Mandatory No-Go Conditions — checked one by one, this session

| Condition | Triggered? | Evidence |
|---|---|---|
| Device still points to a bench endpoint | **YES — TRIGGERED** | Live, this session |
| Plant/device identity incorrect | Plant identity doesn't exist as a field; device identity itself (MAC-derived) is correct | Not separately triggered |
| Server cannot deduplicate retries | No — proven it CAN, live this session | Not triggered |
| Durable queue can silently lose unacknowledged rows | Only the one disclosed, bounded 1-row window (doc 03/04) — not "silent," it's understood and documented | Not triggered as a silent-loss condition |
| Queue capacity insufficient for required outage duration, no mitigation | **Real concern** — theoretical ~1.14 days vs. "several days" target; mitigation (shorter offline windows via the supervised-pilot's own network-readiness requirements) is procedural, not architectural | Borderline — flagged, not an absolute trigger for a SHORT supervised pilot |
| Filesystem-full behavior can silently discard data | No — proven it fails LOUD (`FailureState`, alarms) | Not triggered |
| Current firmware contains test credentials/signing material not accepted for plant deployment | **Business decision required** — test key IS present; whether it's "not accepted" depends on the business's own risk tolerance for a controlled pilot (already flagged, doc 09/10) | Conditional — requires explicit business acceptance |
| Communication unencrypted where TLS required | **YES if the plant requires TLS** (current state is `http://`) | Triggered, pending the plant's actual TLS requirement |
| Sensor pulses cannot be proven on real hardware | **YES — confirmed, doc 14** | **TRIGGERED for "normal production" / accuracy claims** |
| Calibration outside business tolerance | Not evaluated — no reference test performed (doc 14) | Cannot be evaluated, not "cleared" |
| Device/server totals do not reconcile | No — proven they DO reconcile exactly (doc 16) | Not triggered |
| Unexplained sequence gaps exist | No — zero found, this entire session | Not triggered |
| Recovery kit unavailable where automatic rollback absent | Kit and runbook exist (prior plant-readiness phase); PHYSICAL onsite presence not verifiable by this report | Conditional — must be confirmed onsite |
| No manual fallback process | Assumed to exist (pre-existing business process); not documented by this audit | Conditional — must be confirmed |
| Current flashed identity does not match the approved candidate | No — exact match confirmed, doc 02 | Not triggered |

## Verdict for this document

**At least one mandatory No-Go condition is unambiguously triggered
right now** (bench endpoint) — per the mandate's own rule, **this alone
requires `PLANT DEPLOYMENT — NO-GO` for the device in its CURRENT,
unmodified state.** This is a correctable, well-documented, low-effort
provisioning step (above), not an architectural defect — but until it is
actually performed and re-verified, the honest verdict for the device
AS IT SITS RIGHT NOW is NO-GO, carried into the final executive verdict
(doc 01) precisely.
