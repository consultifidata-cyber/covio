// ============================================================================
// covio_firmware.ino  —  Oil Flow Meter (flow-only build)
// ----------------------------------------------------------------------------
// Ties together: PCNT totalizer -> telemetry record -> SD queue -> sync engine
// (push + cumulative ACK + K-factor config poll) -> pull OTA with rollback,
// plus a serial provisioning console (type 'help' in Serial Monitor).
//
// The MAX31865 temperature module is intentionally OUT of this build.
//
// BOARD SETTINGS (Arduino IDE):
//   - Board: "ESP32 Dev Module"
//   - Partition Scheme: an OTA-capable one, e.g.
//       "Minimal SPIFFS (1.9MB APP with OTA)"  <-- REQUIRED for OTA
//   - Flash on USB; then the device self-updates over WiFi thereafter.
//
// FILE LAYOUT: all .h files must sit in the SAME folder as this .ino.
// ============================================================================
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "store.h"
#include "totalizer.h"
#include "queue.h"
#include "telemetry.h"
#include "sync.h"
#include "ota.h"
#include "provision.h"
#include "local_api.h"
#include "wifi_provision.h"

Store         store;
Totalizer     totalizer;
EventQueue    eventQueue;
Sync          syncEngine;
Ota           ota;
Provision     provision;
LocalApi      localApi;         // DM-Phase 1: read-only local diagnostics API
WifiProvision wifiProvision;    // DM-Phase 2: SoftAP + captive-portal provisioning

uint32_t seq = 0;              // GLOBALLY monotonic telemetry sequence
uint32_t tTelemetry = 0, tPush = 0, tConfig = 0, tOta = 0;
bool     healthySignalled = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Covio Oil Flow Meter " FW_VERSION " ===");

  // 1) config/identity first (also increments boot_id)
  store.begin();
  provision.begin(&store);
  Serial.printf("device_id=%s  boot_id=%u\n",
                store.deviceId().c_str(), store.bootId());
  Serial.println("(serial console ready — type 'help')");

  // DM-Phase 5 (ADR-005 Definition of Done: "no device leaves the factory
  // floor with the default API key active"). RELEASE_BUILD is 0 for every
  // bench/dev build (config.h's own default), so this guard never fires
  // outside a real factory/release build -- reuses diagnostics.h's own
  // existing apiKeyStatus_() check rather than a second inline comparison
  // (local_api.h already pulls diagnostics.h into this translation unit).
  // Mirrors the SD-init-failure halt pattern immediately below: loud FATAL
  // message, console stays serviceable so the key can still be fixed.
#if RELEASE_BUILD
  if (Diagnostics::apiKeyStatus_(store) == "default") {
    Serial.println("[FATAL] RELEASE_BUILD=1 but the device still has the "
                    "default API key -- factory provisioning did not "
                    "complete. Refusing normal operation.");
    Serial.println("        (console still active: 'set key <apikey>' works)");
    while (true) { provision.service(); delay(50); }
  }
#endif

  // 2) SD — the queue and totalizer both live here. Fail loud if absent.
  if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
    Serial.println("[FATAL] SD init failed — cannot persist. Halting.");
    Serial.println("        (console still active: 'show' / 'set ...' work)");
    while (true) { provision.service(); delay(50); }  // allow provisioning
  }
  Serial.println("[OK] SD ready");

  // 3) recover totalizer + queue state from SD
  totalizer.begin(store.bootId());
  // ADR-003 (Phase 2): totalizer.begin() has already recovered the queue
  // write-offset checkpoint (q_segment/q_offset) by this point, so passing
  // &totalizer here is safe -- EventQueue::begin() reads it immediately
  // (cleanupOrphanSegments_) and append()/pending()/ackThrough() read it on
  // every subsequent call. This activates the segment-based queue path;
  // the pre-Phase-2 single-file (*Legacy_) methods are no longer reached
  // during normal operation.
  eventQueue.begin(&totalizer);
  seq = totalizer.lastSeq();          // continue GLOBAL seq across reboots

  // 4) network + OTA trial-state check
  ota.begin(&store);
  ota.noteBoot();                     // "am I a freshly-OTA'd image on trial?"
  syncEngine.begin(&store, &eventQueue);
  syncEngine.wifiConnect();

  // DM-Phase 2: bounded boot-time wait for the station connection just
  // kicked off above to resolve, before deciding between normal operation
  // and AP-fallback provisioning mode (§3.1's "bounded timeout" reasoning).
  // Mirrors this file's own existing SD-init-failure blocking-with-service
  // pattern above -- a one-time boot-phase wait, never recurring, so it does
  // not affect the steady-state 1-second telemetry cadence once past this
  // point. `provision` console command requests are consumed here too, so
  // an explicit re-provision always takes the same code path as a genuine
  // connect failure.
  bool forceAp = WifiProvision::consumeReprovisionRequest();
  uint32_t staWaitStart = millis();
  while (!forceAp && WiFi.status() != WL_CONNECTED &&
         millis() - staWaitStart < AP_FALLBACK_TIMEOUT_MS) {
    provision.service();
    syncEngine.wifiService();
    delay(50);
  }

  if (!forceAp && WiFi.status() == WL_CONNECTED) {
    // DM-Phase 1: local diagnostics API. Bound after wifiConnect() so the
    // getters it reads (queue backlog, ota state, etc.) are all already wired;
    // its own mDNS start is deferred internally until the STA connection is
    // actually confirmed (local_api.h's own service() logic).
    localApi.begin(&store, &totalizer, &eventQueue, &syncEngine, &ota);
  } else {
    // DM-Phase 2: station connection did not succeed within the bounded
    // wait above, or `provision` explicitly requested re-entry -- fall back
    // to SoftAP + captive portal. Sync's own reconnect authority is paused
    // for the duration so its background retries never race
    // wifi_provision.h's own WiFi.begin() calls while testing
    // operator-submitted credentials (ADR-017 WiFi-authority consolidation).
    syncEngine.setWifiAuthorityPaused(true);
    wifiProvision.begin(&store);
  }

#if SIM_PULSES
  pinMode(PIN_SIM, OUTPUT);
  digitalWrite(PIN_SIM, LOW);
  Serial.printf("[SIM] test pulses ON: GPIO%d @ ~%d Hz — jumper to GPIO%d\n",
                PIN_SIM, SIM_HZ, PIN_PULSE);
#endif

  Serial.println("[BOOT] entering main loop");
}

void loop() {
  uint32_t now = millis();
  provision.service();                // serial console (help/show/set/...)
  syncEngine.wifiService();                 // keep WiFi up (non-blocking, backoff); no-op while AP authority is paused

  // DM-Phase 2: while provisioning mode is active, localApi has never been
  // begin()'d in this boot path (see setup() above) -- service only the
  // provisioning path and skip everything below that assumes normal
  // operation is running.
  if (wifiProvision.active()) {
    if (wifiProvision.service()) {
      // Station WiFi just confirmed while AP mode was active -- hand WiFi
      // authority back to Sync and start normal-operation local diagnostics.
      syncEngine.setWifiAuthorityPaused(false);
      localApi.begin(&store, &totalizer, &eventQueue, &syncEngine, &ota);
    }
    delay(5);
    return;
  }

  localApi.service();                 // DM-Phase 1: handle any pending local HTTP request

#if SIM_PULSES
  // Software square wave on GPIO25 for bench tests. Pauses during blocking
  // network calls (rate dips are normal); real-meter counting is hardware.
  static uint32_t tSim = 0;
  if (now - tSim >= (1000UL / (SIM_HZ * 2))) {
    tSim = now;
    digitalWrite(PIN_SIM, !digitalRead(PIN_SIM));
  }
#endif

  // ---- build a telemetry record every TELEMETRY_PERIOD_MS ----
  if (now - tTelemetry >= TELEMETRY_PERIOD_MS) {
    tTelemetry = now;
    seq++;
    uint64_t total = totalizer.total();     // live read (base + acc + PCNT)
    QRow row = Telemetry::build(store, total, seq, now / 1000, false);
    eventQueue.append(row);                      // 1) durable row FIRST
    totalizer.service(seq);                 // 2) THEN checkpoint total+seq
    // ORDER MATTERS: if power dies between 1 and 2, this seq regenerates on
    // next boot and the duplicate row is absorbed by the server (idempotent).
    // The reverse order could burn a seq with no row behind it — a permanent
    // gap that freezes the cumulative ACK forever.
  }

  // ---- flush queue to the configured endpoint ----
  if (now - tPush >= PUSH_PERIOD_MS) {
    tPush = now;
    bool acked = syncEngine.pushOnce();
    // Mark firmware healthy after the device has proven it can reach the server.
    if (acked && !healthySignalled) {
      ota.confirmHealthyBoot();
      healthySignalled = true;
    }
  }

  // ---- poll K-factor / calibration config ----
  if (now - tConfig >= CONFIG_POLL_MS) {
    tConfig = now;
    syncEngine.pollConfig();
    if (syncEngine.online() && !healthySignalled) {
      ota.confirmHealthyBoot();
      healthySignalled = true;
    }
  }

  // ---- check for firmware updates ----
  if (now - tOta >= OTA_POLL_MS) {
    tOta = now;
    ota.poll(syncEngine.online());    // DM-Phase 2: WiFi-authority consolidation -- may download + reboot into new image
  }

  delay(5);                           // yield
}
