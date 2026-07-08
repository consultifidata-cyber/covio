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
// NOTE: use https:// in the field. http:// is here only so the bench stub on
// your laptop works without TLS. See ota.h for the security warning.
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

// ---- Queue / storage --------------------------------------------------------
#define SD_QUEUE_DIR          "/queue"    // append-only event log lives here
#define PUSH_BATCH_MAX        50          // max records per push request
#define QUEUE_HIGHWATER       100000UL    // flag quality_code if backlog exceeds

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
