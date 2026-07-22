// ============================================================================
// config.h  —  Covio IoT Device : build-time defaults
// ----------------------------------------------------------------------------
// PHILOSOPHY: The device does NOT know whether it talks to an LCS, the Covio
// cloud, or anything else. It POSTs telemetry to whatever `server_url` NVS
// holds. These #defines are only the FIRST-BOOT DEFAULTS; once the device has
// run, the live values come from NVS and can be changed with no reflash.
//
// The three things that ever change between deployments:
//    - API base URL   (server_url)
//    - API key        (api_key)
//    - WiFi creds      (wifi_ssid / wifi_pass)
// Nothing in firmware logic changes. That is the whole point.
// ============================================================================
#pragma once

// ---- Firmware identity (bump on every release; OTA compares against this) ---
#define FW_VERSION            "1.0.0"
#define DEVICE_MODEL          "covio-oilflow-v1"

// ---- First-boot default endpoint (OVERRIDDEN by NVS after provisioning) -----
// NOTE: use https:// in the field. sync.h/ota.h dispatch on server_url's own
// scheme (covioIsHttpsUrl(), certs.h) and automatically use a pinned-CA
// WiFiClientSecure whenever it's https:// (ADR-005 / DM-Phase 5). http://
// remains here ONLY so the bench stub (server/server.py, still plain HTTP)
// keeps working with zero config changes -- a deliberate, permanent
// bench/dev affordance, not a leftover TODO.
#define DEFAULT_SERVER_URL    "http://192.168.1.3:8000"
#define DEFAULT_API_KEY       "dev-key-change-me"

// ---- API paths (the shared contract; identical on LCS and cloud) ------------
#define PATH_PUSH             "/api/iot/flow/push"
#define PATH_CONFIG           "/api/iot/flow/config"
#define PATH_OTA_MANIFEST     "/api/iot/flow/ota/manifest"   // returns latest fw version + bin URL

// ---- First-boot default WiFi (OVERRIDDEN by NVS) ----------------------------
// P0-2 remediation (docs/audit/coviu_oil_meter_p0_remediation/03_CREDENTIAL_CONTAINMENT_AND_ROTATION.md):
// a real network's SSID/password were hardcoded here and confirmed in active
// use on a physically connected bench device. They have been replaced with
// obvious, non-functional placeholders. This does NOT retroactively secure
// the original network -- that credential must still be rotated by its
// network administrator; source-code removal alone does not invalidate an
// already-exposed credential. The intended real-credential path is AP-mode
// provisioning (wifi_provision.h), never a rebuilt fleet-wide default.
#define DEFAULT_WIFI_SSID     "YOUR_WIFI_SSID_PLACEHOLDER"
#define DEFAULT_WIFI_PASS     "YOUR_WIFI_PASSWORD_PLACEHOLDER"

// ---- Hardware pins ------------------------------------------------------------
// Retargeted for the actual unit on hand: Waveshare ESP32-S3-Relay-1CH.
// Its own schematic's GPIO occupancy table (GPIO | Relay | RTC | RS485 |
// Other columns) shows the relay on IO47, RS485 on IO17/18/21, RTC on
// IO38/39/40, native USB D-/D+ on IO19/20, and IO33-37 internally reserved
// for the WROOM-1U module's flash/PSRAM bus -- meanwhile IO22-32 (including
// the old PIN_PULSE=27/PIN_SIM=25 bench values, carried over from the
// original raw-ESP32 bring-up) aren't exposed as usable GPIOs on this
// module's package AT ALL. IO1-IO16 are the only fully free range, matching
// exactly the board's labeled external 4-pin breakout (GND/3V3/IO1/IO2) next
// to the USB-C port -- that connector's IO1/IO2 silkscreen is the literal
// GPIO number, not a relabeled index.
#define PIN_PULSE             1           // external breakout "IO1" -> GPIO1 (flow pulses, via opto-isolator)
// (MAX31865 removed for this build; its CS was GPIO5 — leave free.)
// No SD card on this unit: queue/totalizer persistence moved to internal
// flash (LittleFS, "spiffs" partition) -- see queue.h/totalizer.h/
// covio_firmware.ino. PIN_SD_CS removed; GPIO19 in particular must stay free
// on ESP32-S3 -- confirmed by this board's own schematic as the native USB
// D- line (D_N), so the old VSPI-18/23/19 SD wiring would have conflicted
// with USB even if a card were attached.

// ---- Pulse counter (PCNT) ---------------------------------------------------
#define PCNT_GLITCH_NS        1000        // hardware glitch filter, nanoseconds
                                          // raise if you see idle creep

// ---- Timing (all milliseconds) ----------------------------------------------
#define TELEMETRY_PERIOD_MS   1000UL      // build one record this often
#define PUSH_PERIOD_MS        5000UL      // attempt a queue flush this often
#define CONFIG_POLL_MS        60000UL     // re-fetch K-factor/config this often
#define OTA_POLL_MS           300000UL    // check for new firmware this often (5 min)
#define WIFI_RETRY_MS         10000UL     // base backoff for reconnect

// ---- DM-Phase 2 (WiFi provisioning: SoftAP + captive portal) ----------------
// Bounded boot-time wait for the station connection kicked off at boot to
// resolve, before falling back to AP-fallback provisioning mode (§3.1's
// "bounded timeout" reasoning). Well under the 60s acceptance bound an
// unconfigured unit's AP must be visible within.
#define AP_FALLBACK_TIMEOUT_MS      15000UL
// Bounded wait when live-testing operator-submitted WiFi credentials in the
// captive portal, before reporting "could not connect" back to the portal.
#define AP_TEST_CONNECT_TIMEOUT_MS  15000UL

// ---- Queue / storage --------------------------------------------------------
// SD_QUEUE_DIR still names the directory, but it now lives on internal flash
// (LittleFS "spiffs" partition, ~3.4MB on this unit's default_16MB.csv table
// -- see platformio.ini) instead of an SD card. Sized down from the SD-era
// defaults (100000/10000, which assumed near-unlimited SD capacity) to fit
// comfortably: a QRow is 36 bytes, so QUEUE_HIGHWATER=20000 rows is ~720KB
// (well under the ~3.4MB partition, leaving headroom for two ping-pong
// segments plus LittleFS's own metadata overhead) -- roughly 5.5 hours of
// offline buffering at the 1 row/sec telemetry cadence below.
#define SD_QUEUE_DIR          "/queue"    // append-only event log lives here
#define PUSH_BATCH_MAX        50          // max records per push request
#define QUEUE_HIGHWATER       20000UL     // flag quality_code if backlog exceeds

// ---- ADR-003 (Phase 2): segmented queue storage -----------------------------
// Fixed row-count cap per segment file — "a single tunable constant" per
// ADR-003's Future Extension note.
#define QUEUE_SEGMENT_ROWS     5000UL

// Segment file naming: zero-padded 6-digit decimal, so a plain FAT32
// directory listing sorts in numeric/chronological order. %06u expects an
// unsigned int argument.
#define QUEUE_SEGMENT_PATH_FMT "/queue/seg_%06u.bin"

// ---- Bench test helper ------------------------------------------------------
// SIM_PULSES=1 makes the firmware toggle GPIO2 (the breakout's "IO2", also
// free per this board's schematic) as a fake flow signal. Jumper IO2 -> IO1
// (opto output DISCONNECTED) and the totalizer climbs at ~SIM_HZ pulses/sec
// with no meter attached. Left at 0 now that a real sensor is being wired to
// PIN_PULSE (GPIO1/"IO1") -- SIM and the real opto output are mutually
// exclusive on that pin by design, never jumper both at once. NOTE: the sim
// toggle is software — it pauses briefly during network pushes (rate dips
// are normal). Real meter pulses are counted in the PCNT hardware peripheral
// and are NEVER lost to network activity.
#define SIM_PULSES            0
#define PIN_SIM               2
#define SIM_HZ                10

// ---- NVS namespace ----------------------------------------------------------
#define NVS_NS                "covio"     // all persisted globals live here

// ---- DM-Phase 5 (ADR-005 Security Hardening) --------------------------------
// Compile-time gate for a real factory/field release build. Left at 0 for
// every bench/dev build (the default -- nothing about existing bench
// workflow changes) so the whole project's established bench iteration loop
// is completely unaffected. A real release build overrides this to 1 (e.g.
// via a dedicated PlatformIO build environment/`-DRELEASE_BUILD=1` build
// flag; see Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md), which
// activates two fail-loud guards:
//   - certs.h refuses to COMPILE with its placeholder CA cert still in place.
//   - covio_firmware.ino's setup() refuses to leave the factory floor with
//     DEFAULT_API_KEY still active (ADR-005 Definition of Done: "no device
//     leaves the factory floor with the default API key active").
// #ifndef, not a bare #define, so a build-flag override (-DRELEASE_BUILD=1)
// is honored rather than silently overwritten by this file.
#ifndef RELEASE_BUILD
#define RELEASE_BUILD 0
#endif

// ---- DM-Phase 6 (ADR-008 Factory Self-Test Firmware, §11.7) -----------------
// Compile-time gate for the factory-test firmware build variant (ADR-008:
// "a dedicated factory-test firmware build (a compile-time variant of the
// same codebase)"). Left at 0 for every normal build (bench AND production)
// so the factory-only write path this enables -- local_api.h's new
// POST /api/v1/factory/provision (§11.2 Logical Device ID + API key writes,
// the desktop app's local-API channel for §11.7's factory workflow) --
// simply does not exist in the compiled binary of any image that could ever
// reach a customer site. This is the SAME "compile-time absence, not a
// runtime check" security posture RELEASE_BUILD already uses above, applied
// to ADR-008's own frozen requirement: "the factory-test image must never be
// the image that ships to a customer."
#ifndef FACTORY_TEST_BUILD
#define FACTORY_TEST_BUILD 0
#endif
