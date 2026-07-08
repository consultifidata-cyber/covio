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

Store      store;
Totalizer  totalizer;
EventQueue eventQueue;
Sync       syncEngine;
Ota        ota;
Provision  provision;

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

  // 2) SD — the queue and totalizer both live here. Fail loud if absent.
  if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
    Serial.println("[FATAL] SD init failed — cannot persist. Halting.");
    Serial.println("        (console still active: 'show' / 'set ...' work)");
    while (true) { provision.service(); delay(50); }  // allow provisioning
  }
  Serial.println("[OK] SD ready");

  // 3) recover totalizer + queue state from SD
  totalizer.begin(store.bootId());
  eventQueue.begin();
  seq = totalizer.lastSeq();          // continue GLOBAL seq across reboots

  // 4) network + OTA trial-state check
  ota.begin(&store);
  ota.noteBoot();                     // "am I a freshly-OTA'd image on trial?"
  syncEngine.begin(&store, &eventQueue);
  syncEngine.wifiConnect();

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
  syncEngine.wifiService();                 // keep WiFi up (non-blocking, backoff)

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
    ota.poll();                       // may download + reboot into new image
  }

  delay(5);                           // yield
}
