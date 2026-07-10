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
#define DEFAULT_SERVER_URL    "http://192.168.1.100:8000"
#define DEFAULT_API_KEY       "dev-key-change-me"

// ---- API paths (the shared contract; identical on LCS and cloud) ------------
#define PATH_PUSH             "/api/iot/flow/push"
#define PATH_CONFIG           "/api/iot/flow/config"
#define PATH_OTA_MANIFEST     "/api/iot/flow/ota/manifest"   // returns latest fw version + bin URL

// ---- First-boot default WiFi (OVERRIDDEN by NVS) ----------------------------
#define DEFAULT_WIFI_SSID     "your-ssid"
#define DEFAULT_WIFI_PASS     "your-pass"

// ---- Hardware pins (from your proven build) ---------------------------------
#define PIN_PULSE             27          // opto output -> GPIO27 (flow pulses)
#define PIN_SD_CS             4           // SD card chip-select (VSPI: 18/23/19)
// (MAX31865 removed for this build; its CS was GPIO5 — leave free.)

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
#define SD_QUEUE_DIR          "/queue"    // append-only event log lives here
#define PUSH_BATCH_MAX        50          // max records per push request
#define QUEUE_HIGHWATER       100000UL    // flag quality_code if backlog exceeds

// ---- ADR-003 (Phase 2): segmented queue storage -----------------------------
// Fixed row-count cap per segment file — "a single tunable constant" per
// ADR-003's Future Extension note. Introduced in P2-T1; not consumed by any
// behavior yet — segment rollover and truncate-before-append are later
// Phase 2 tasks (P2-T5). No behavioral change from this task alone.
#define QUEUE_SEGMENT_ROWS     10000UL

// Segment file naming: zero-padded 6-digit decimal, so a plain FAT32
// directory listing sorts in numeric/chronological order. %06u expects an
// unsigned int argument.
#define QUEUE_SEGMENT_PATH_FMT "/queue/seg_%06u.bin"

// ---- Bench test helper ------------------------------------------------------
// SIM_PULSES=1 makes the firmware toggle GPIO25 as a fake flow signal.
// Jumper GPIO25 -> GPIO27 (opto output DISCONNECTED) and the totalizer climbs
// at ~SIM_HZ pulses/sec with no meter attached. Set back to 0 for the real
// meter. NOTE: the sim toggle is software — it pauses briefly during network
// pushes (rate dips are normal). Real meter pulses are counted in the PCNT
// hardware peripheral and are NEVER lost to network activity.
#define SIM_PULSES            0
#define PIN_SIM               25
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
