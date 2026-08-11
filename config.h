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

// ---- Product profile selection (MUST resolve before the identity block) -----
// This repository builds TWO products from ONE source tree. They differ in
// exactly three things: which backend they talk to, which GPIO the sensor
// lands on (the BOARD_MODE pin table further down), and their OTA hardware
// identity. Everything else -- queue, totalizer, sync, OTA, watchdog,
// provisioning, diagnostics -- is shared, byte for byte.
//
//   MIKI_WIRE_PROFILE 0 (default) : Covio oil flow meter, Balaji Foods
//   MIKI_WIRE_PROFILE 1           : Miki Wire proximity sensor, MW-001 Ranchi
//
// The #ifndef/#define pair is hoisted HERE, above the identity block, because
// DEVICE_MODEL and DEFAULT_SERVER_URL below both branch on it. The profile's
// TUNABLES (MIKI_MAX_PULSE_HZ_*, MIKI_SUSPECT_GAP_S_*) stay where they were,
// further down -- only the selector moved.
#ifndef MIKI_WIRE_PROFILE
#define MIKI_WIRE_PROFILE 0
#endif

// ---- Firmware identity (bump on every release; OTA compares against this) ---
// FW_VERSION is a property of the SOURCE TREE, not of a product: both
// products are cut from the same commit at the same version. What separates
// them for OTA purposes is DEVICE_MODEL below.
#define FW_VERSION            "1.2.1"

// ---- OTA hardware identity (hw_compat) --------------------------------------
// ota.h passes DEVICE_MODEL as the manifest's `hw_compat` field and REFUSES
// any candidate whose hw_compat does not match exactly. This string is
// therefore the ONLY thing standing between the two products' OTA channels:
// without a distinct value per product, an oil-flow image would be accepted
// by the Miki unit and vice versa -- and since the two boards put the sensor
// on different GPIOs (GPIO1 vs GPIO4/DI1), a cross-flash silently stops the
// device counting.
//
// ⚠ "covio-oilflow-v1" is FROZEN. The commissioned Balaji meter has it
// compiled in and compares against it on every OTA poll; changing it would
// cut that meter off from OTA until someone reflashes it over USB. Never
// edit it. New products get a NEW string, they never re-use this one.
#if MIKI_WIRE_PROFILE
#define DEVICE_MODEL          "miki-wire-v1"
#else
#define DEVICE_MODEL          "covio-oilflow-v1"
#endif

// ---- RISK-15 remediation (OTA anti-downgrade) --------------------------------
// A SEPARATE, monotonically-increasing integer from FW_VERSION above. Bump
// this ONLY when a release fixes a security-relevant defect (the kind of
// thing a downgrade attack would want to undo) -- an ordinary feature
// release with no security content does NOT need to bump it. ota.h rejects
// any candidate manifest whose security_version is lower than the highest
// value this device has ever confirmed healthy on (Store::securityVersion(),
// a durable NVS-backed floor -- see store.h and ota_version_policy.h).
// RAISED 1 -> 2 ON 2026-08-11, AND THIS IS WHY OTA HAS NEVER WORKED.
//
// The commissioned Balaji meter reports accepted_security_floor = 2, while
// this tree has always compiled FW_SECURITY_VERSION = 1. ota_version_policy.h
// rejects any candidate whose security_version is strictly below the device's
// floor, so every image CI has ever built was refused with
// "downgrade_rejected" on every poll -- silently, forever.
//
// A floor of 2 can only have been written by an image built with
// FW_SECURITY_VERSION = 2 (ota.h:292 advances the floor to its own compiled
// value and never lowers it). The meter runs a HAND-BUILT 1.0.1 whose source
// was never reconciled with this repo; this is the concrete proof that it
// differs from the tree in a load-bearing way.
//
// Raising this to 2 is also correct on its own merits, independent of the
// floor: the releases since 1.0.1 carry genuinely security-relevant fixes --
// the task watchdog, bounded server ack_seq, and per-product OTA hw_compat
// that stops one product's image being accepted by the other.
//
// Equal is accepted (the comparison is strictly less-than), so 2 is
// sufficient. Do not raise it further "to be safe": every increment is a
// permanent, irreversible ratchet on real hardware, and setting it above
// what a device can ever be offered is exactly how this meter locked itself
// out of updates in the first place.
#define FW_SECURITY_VERSION    2

// ---- First-boot default endpoint (OVERRIDDEN by NVS after provisioning) -----
// NOTE: use https:// in the field. sync.h/ota.h dispatch on server_url's own
// scheme (covioIsHttpsUrl(), certs.h) and automatically use a pinned-CA
// WiFiClientSecure whenever it's https:// (ADR-005 / DM-Phase 5). http://
// remains here ONLY so the bench stub (server/server.py, still plain HTTP)
// keeps working with zero config changes -- a deliberate, permanent
// bench/dev affordance, not a leftover TODO.
// Each product ships pointing at its OWN backend, so a freshly flashed unit
// is correct before anyone touches the console. The API PATHS below are
// identical for both -- only the host differs (the Miki backend runs the same
// /api/iot/flow/ contract as a compatibility adapter).
//
// Both hosts are served by certificates that chain to the roots pinned in
// certs.h -- verified against the pinned bundle alone, not the OS trust
// store, for data.funtastik.co.in and compliance.mikigroup.co.in alike.
//
// For bench work against server/server.py (plain HTTP), set the URL once over
// the serial console -- `set url http://<host>:8000` -- which persists in NVS
// and survives reflashing. That is deliberately a runtime step now: baking a
// LAN address into a shipped image is a far worse failure mode than one
// console command on a bench unit.
#if MIKI_WIRE_PROFILE
#define DEFAULT_SERVER_URL    "https://compliance.mikigroup.co.in"
#else
#define DEFAULT_SERVER_URL    "https://data.funtastik.co.in"
#endif
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

// ---- Board platform selection (multi-board enhancement) ---------------------
// Selects the BOARD's pin table at COMPILE TIME -- #ifndef + build-flag
// override, the exact pattern SENSOR_MODE below and RELEASE_BUILD/
// FACTORY_TEST_BUILD already use (-DBOARD_MODE=1 in a build flag selects
// the 8DI-8DO).
//
// BOARD_RELAY1CH (the default): Waveshare ESP32-S3-Relay-1CH -- the pin
//   table the DEPLOYED production plant's binary was built with (commit
//   e5a593b, env esp32dev; flashed + commissioned per Docs/audit/
//   coviu_oil_meter_plant_commissioning/08_PRODUCTION_FLASH_AND_COMMISSIONING.md).
//   Kept as the default so a flag-less build IS the production baseline.
// BOARD_8DI8DO: Waveshare ESP32-S3-POE-ETH-8DI-8DO industrial control
//   board (8 opto-isolated DI, 8 DO, RS485, CAN, POE Ethernet -- none of
//   the extra interfaces are used by this firmware) -- the CT-clamp
//   enhancement phase's target unit.
//
// BOARD_MODE and SENSOR_MODE (below) are DELIBERATELY independent axes:
// selecting a board never implies a sensor and vice versa. The two axes
// meet ONLY in this pin table -- each board block defines where each
// sensor type lands on that board, and nothing else anywhere in the
// firmware knows which board it is running on.
#define BOARD_RELAY1CH        0
#define BOARD_8DI8DO          1
#ifndef BOARD_MODE
#define BOARD_MODE            BOARD_RELAY1CH
#endif

// ---- Hardware pins ------------------------------------------------------------
#if BOARD_MODE == BOARD_RELAY1CH
// Waveshare ESP32-S3-Relay-1CH (the deployed production plant unit).
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
// CT sensor input on this board: the breakout's other free pin ("IO2" ->
// GPIO2). Defined so BOARD_MODE and SENSOR_MODE stay independent (a CT
// build for this board compiles), but this combination is UNVALIDATED on
// real hardware, and GPIO2 doubles as PIN_SIM below -- SIM_PULSES must
// stay 0 in any CT build on this board.
#define PIN_CT_STATE          2           // breakout "IO2" -> GPIO2 (CT builds; unvalidated combo)

#elif BOARD_MODE == BOARD_8DI8DO
// Industrial ESP32-S3 Control Board, Waveshare ESP32-S3-POE-ETH-8DI-8DO
// (PRODUCTION RETARGET, CT-clamp enhancement phase). Its 8 optocoupler-
// isolated digital inputs DI1..DI8 map to GPIO4..GPIO11 (manufacturer user
// guide + ESPHome device registry, cross-checked). GPIO1 -- the Relay-1CH
// board's breakout pin, see the BOARD_RELAY1CH block above -- is NOT an
// exposed field terminal on this board, so the NPN sensor lands on the DI1
// terminal (GPIO4). DI1's onboard bidirectional optocoupler replaces the
// external PC817 module used on the Relay-1CH unit (wiring: sensor output ->
// DI1, sensor 0V -> DI COM, field-side supply on the 7-36V terminal). The
// DI stage inverts (active input = GPIO reads LOW) -- irrelevant to PCNT
// pulse COUNTING: each physical pulse still yields exactly one rising edge.
#define PIN_PULSE             4           // DI1 terminal (GPIO4) -> NPN flow pulses (onboard opto)
#define PIN_CT_STATE          5           // DI2 terminal (GPIO5); active = LOW (opto inverts)

#else
#error "Unknown BOARD_MODE -- valid values: BOARD_RELAY1CH (0, default), BOARD_8DI8DO (1)."
#endif
// (Both boards) No SD card: queue/totalizer persistence lives on internal
// flash (LittleFS, "spiffs" partition) -- see queue.h/totalizer.h/
// covio_firmware.ino. PIN_SD_CS removed; GPIO19 in particular must stay free
// on ESP32-S3 -- confirmed by the Relay-1CH schematic as the native USB
// D- line (D_N), so the old VSPI-18/23/19 SD wiring would have conflicted
// with USB even if a card were attached.

// ---- Pulse counter (PCNT) ---------------------------------------------------
#define PCNT_GLITCH_NS        1000        // hardware glitch filter, nanoseconds
                                          // raise if you see idle creep

// ---- Sensor acquisition mode (CT-clamp enhancement) -------------------------
// Selects the sensor ACQUISITION method at COMPILE TIME -- #ifndef +
// build-flag override, the exact pattern RELEASE_BUILD/FACTORY_TEST_BUILD
// below already use (-DSENSOR_MODE=1 in a build flag selects CT).
//
// SENSOR_MODE_NPN (the default): the existing PCNT hardware pulse-counting
//   path on PIN_PULSE, byte-for-byte the production code path. CT code is
//   NOT COMPILED into NPN builds at all (compile-time absence, same posture
//   as FACTORY_TEST_BUILD's factory-only route).
// SENSOR_MODE_CT: a CT current-sensing SWITCH (contact-output clamp, its
//   own built-in threshold/burden -- NOT a raw analog CT; the 8DI-8DO has
//   no exposed ADC) wired to a digital input + COM. WHICH pin carries it is
//   board-specific: PIN_CT_STATE is defined per board in the BOARD_MODE
//   blocks above (8DI-8DO: DI2/GPIO5; Relay-1CH: breakout IO2/GPIO2,
//   unvalidated). sensor_ct.h converts the debounced current-presence
//   level into a software pulse stream at CT_PULSE_HZ via time integration
//   (robust to blocking network gaps), injected into the SAME totalizer
//   accumulator the PCNT path drains into. Everything above the
//   acquisition layer -- totalizer persistence, telemetry, payloads,
//   queue, sync, server-side K-factor -- is unchanged and unaware of which
//   sensor produced the pulses.
#define SENSOR_MODE_NPN       0
#define SENSOR_MODE_CT        1
#ifndef SENSOR_MODE
#define SENSOR_MODE           SENSOR_MODE_NPN
#endif
#define CT_PULSE_HZ           1           // synthesized pulses/sec while current present
#define CT_DEBOUNCE_MS        100         // DI2 level debounce (on top of the board's own DI filtering)

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
// (LittleFS "spiffs" partition) instead of an SD card.
//
// P0-4 remediation (RISK-04): the partition size below is no longer a code-
// comment guess -- confirmed by directly reading the ACTUAL partition table
// this build uses (platformio.ini's board_build.partitions=default_16MB.csv,
// located and read from the installed PlatformIO espressif32 toolchain):
//   spiffs, data, spiffs, 0xc90000, 0x360000   <- offset, SIZE in bytes
// 0x360000 = 3,538,944 bytes exactly (3.375 MiB), not an approximation.
// QRow is 36 bytes (sizeof, verified against the packed struct above), so:
//   3,538,944 bytes / 36 bytes/row = 98,304 rows of RAW capacity if the
//   partition held nothing else at all and LittleFS had zero overhead.
// Real usable capacity is lower once LittleFS's own metadata/wear-leveling
// reserve and this queue's own non-row files (ackA/ackB.bin, failA/failB.bin,
// per-segment file overhead) are accounted for -- EventQueue::
// capacityPercentUsed() (queue.h) reports the REAL figure at runtime via
// LittleFS.usedBytes()/totalBytes(), which is authoritative; the raw-
// capacity number above is a theoretical ceiling for sizing QUEUE_HIGHWATER
// below, not a promise of exactly how many rows will fit in practice (see
// 06_FLASH_LIFETIME_ANALYSIS.md for the full worked estimate and its
// stated assumptions).
//
// QUEUE_HIGHWATER=20000 rows is ~720KB (36 bytes x 20000) -- roughly 20% of
// the raw 98,304-row ceiling above, chosen as a soft advisory threshold
// (quality_code flag on subsequent records, and now also a diagnostics.h
// STORAGE_WARNING-tier signal) well before real capacity is approached --
// roughly 5.5 hours of offline buffering at the 1 row/sec telemetry cadence
// below. This is a SOFT threshold only -- it does not stop the queue from
// continuing to accept rows past this point (see queue.h's append(), which
// only refuses a write on an actual filesystem failure, tracked durably by
// P0-4's FailureState, never on merely crossing this number).
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
// WARNING (8DI-8DO production board): GPIO2 is that board's CAN TX pin --
// SIM_PULSES must stay 0 on this hardware unless PIN_SIM is first
// reassigned to a genuinely free GPIO.
#define SIM_PULSES            0
#define PIN_SIM               2
#define SIM_HZ                10

// ---- NVS namespace ----------------------------------------------------------
#define NVS_NS                "covio"     // all persisted globals live here
// RISK-15 remediation: the security-version anti-downgrade floor lives in
// its OWN namespace, deliberately never cleared by Store::factoryReset()
// (see store.h) -- a downgrade-after-reset is exactly what this floor
// defends against.
#define NVS_NS_SECURITY        "covio_sec"

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

// ---- Build identity remediation ---------------------------------------------
// BUILD_COMMIT/BUILD_DIRTY/BUILD_TIME_UTC are injected by
// scripts/generate_build_identity_extra.py (a PlatformIO pre-build
// extra_script, wired in via platformio.ini's shared [env] section) --
// computed fresh from git at build time, never hand-maintained here. The
// #ifndef fallbacks below exist ONLY as a defense-in-depth safety net (same
// posture as RELEASE_BUILD/FACTORY_TEST_BUILD above): if the extra_script
// somehow did not run, "unknown"/dirty=1 are themselves placeholder/unsafe
// values, which the #error guard immediately below refuses to ship as a
// release image, exactly like certs.h/ota_keys.h's existing placeholder
// guards for CA cert / OTA public key.
#ifndef BUILD_COMMIT
#define BUILD_COMMIT "unknown"
#endif
#ifndef BUILD_DIRTY
#define BUILD_DIRTY 1
#endif
#ifndef BUILD_TIME_UTC
#define BUILD_TIME_UTC "unknown"
#endif

#if RELEASE_BUILD
  #if BUILD_DIRTY
    #error "RELEASE_BUILD=1 but BUILD_DIRTY=1 -- the build-identity extra_script did not run (or ran against a dirty/unresolvable tree). Refusing to build an unidentified/dirty release image (see scripts/build_identity.py)."
  #endif
#endif

// ---- Balaji V1 freeze remediation (long-term diagnostics) -------------------
// Product Readiness Review P1-1 (persistent failure counters) / P1-2
// (sensor-stuck-at-zero alarm). Both are heuristic, operator-facing
// advisories, not hard faults -- tune per-site once real behavior/flow
// patterns are observed, per each constant's own comment below.
//
// How long the lifetime pulse total must stay completely UNCHANGED before
// SENSOR_STOPPED is raised. Deliberately generous: a real oil meter can
// legitimately see zero flow for extended idle periods with nothing wrong.
// 48h default -- lower it once this site's actual idle/dispensing pattern
// is known, if 48h proves too slow to be useful in practice.
#define SENSOR_STUCK_THRESHOLD_MS   (48UL * 3600UL * 1000UL)

// Consecutive abnormal (watchdog/brownout/panic) resets, each occurring
// before the device ever proves a healthy run (see
// HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS below), before REBOOT_LOOP fires.
#define CRASH_RESET_STREAK_ALARM    3

// How long a boot must run without incident before it clears the crash-
// reset streak above -- i.e. how long counts as proof "this boot is not
// part of a crash loop." This device has no RTC (RISK-11, unchanged), so
// this is measured in device uptime (millis()), not wall-clock time.
#define HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS  (5UL * 60UL * 1000UL)

// ---- Miki Wire hardening (Phase-0 finding F2): task watchdog ---------------
// Before this, NO watchdog existed anywhere in the firmware -- nothing
// configured or fed esp_task_wdt, so a hang inside a blocking HTTP/TLS call
// simply hung the device forever. The whole firmware is one cooperative
// loop, so subscribing the loop task and feeding once per iteration IS the
// meaningful liveness proof: every subsystem is serviced from that loop, and
// a hang anywhere in it stops the feed. The one legitimate long-blocking
// path (the OTA image download) proves liveness explicitly via the
// Ota service-callback hook (fed only while download progress/waiting is
// genuinely being made -- its own 15s stall detector aborts a dead
// transfer long before this timeout).
//
// TIMEOUT CHOICE: must exceed the worst legitimate uninterrupted block --
// push HTTP timeout 8s + TLS handshake, config poll 6s, captive-portal
// credential test 15s -- with generous margin, because a false watchdog
// reset in production is worse than slow hang detection (Phase-1 mandate:
// "avoid false watchdog resets"). 60s catches every genuine hang while
// sitting 4x above the worst legitimate stall.
//
// The watchdog is armed at the END of setup(), deliberately AFTER the two
// serviceable fatal-halt consoles (LittleFS-mount failure, RELEASE_BUILD
// default-key refusal) -- those halts must stay reachable over serial
// forever, not reset-loop. A watchdog reset is classified by the existing
// esp_reset_reason() boot code (wdt_cnt / crash streak / REBOOT_LOOP alarm)
// with zero new diagnostics needed.
#ifndef WATCHDOG_ENABLE
#define WATCHDOG_ENABLE     1
#endif
#ifndef WATCHDOG_TIMEOUT_S
#define WATCHDOG_TIMEOUT_S  60
#endif

// ---- Miki Wire profile (compile-time plant hardening layer) -----------------
// Selects the Miki Wire proximity-hardening modules (pulse_plausibility.h,
// sensor_health.h + their NVS tunables and console commands) at COMPILE
// TIME -- #ifndef + build-flag override, the exact pattern BOARD_MODE/
// SENSOR_MODE/RELEASE_BUILD above use (-DMIKI_WIRE_PROFILE=1 in the
// mikiwire PlatformIO envs). The flag-less build -- the deployed Balaji
// baseline -- contains NONE of this code (compile-time absence, the same
// posture SENSOR_MODE_CT and FACTORY_TEST_BUILD already established), and
// test_board_config.cpp's default compile enforces that with an #error.
//
// TUNABLE DEFAULTS ARE DELIBERATELY 0 = FEATURE INERT. The real ceilings
// for Wire Drawing Machine 1 (max line speed -> max plausible pulse Hz;
// longest legitimate idle gap) are INSUFFICIENT VERIFIED INFORMATION at
// implementation time -- the monitors ship compiled-in but dormant, and are
// armed per-site over the serial console (`set maxhz` / `set suspects`)
// once real line parameters are confirmed. Bounds below are validation
// limits for those commands, not operating guesses:
//   - MIKI_MAX_PULSE_HZ_LIMIT 2000: an LJ12A3-4-Z/BX tops out around
//     500 Hz switching; 2 kHz is an absolute electrical ceiling with
//     margin, above which a configured value is certainly a typo.
//   - suspect gap 60 s .. 7 days: below a minute would alarm on ordinary
//     pauses; above a week the feature is indistinguishable from off.
//
// The MIKI_WIRE_PROFILE selector itself now lives at the TOP of this file --
// DEVICE_MODEL and DEFAULT_SERVER_URL branch on it, so it has to resolve
// before them. Only the tunables below remain here.

// Phase-2 watchdog failure-injection build (validation matrix E2): compiles
// the serial console's `test_hang` command (provision.h), which simulates a
// genuine main-loop hang so the task watchdog's detect->reset->classify->
// recover chain can be demonstrated on real hardware. NEVER set in any
// shipping env -- bench-only, passed explicitly as -DWDT_TEST_BUILD=1.
#ifndef WDT_TEST_BUILD
#define WDT_TEST_BUILD 0
#endif
#if MIKI_WIRE_PROFILE
#define MIKI_MAX_PULSE_HZ_DEFAULT    0UL          // 0 = monitor inert
#define MIKI_MAX_PULSE_HZ_LIMIT      2000UL
#define MIKI_SUSPECT_GAP_S_DEFAULT   0UL          // 0 = SUSPECT disabled
#define MIKI_SUSPECT_GAP_S_MIN       60UL
#define MIKI_SUSPECT_GAP_S_MAX       (7UL * 24UL * 3600UL)
#endif
