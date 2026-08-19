// ============================================================================
// covio_firmware.ino  —  Oil Flow Meter (flow-only build)
// ----------------------------------------------------------------------------
// Ties together: PCNT totalizer -> telemetry record -> SD queue -> sync engine
// (push + cumulative ACK + K-factor config poll) -> pull OTA with rollback,
// plus a serial provisioning console (type 'help' in Serial Monitor).
//
// The MAX31865 temperature module is intentionally OUT of this build.
//
// BOARD SETTINGS (this unit, ESP32-S3 -- see platformio.ini's [env] override
// comment for why this differs from the rest of the repo's ESP32-classic
// docs/settings):
//   - Board: ESP32-S3 (16MB quad flash; the unit also has embedded PSRAM,
//       left disabled -- see platformio.ini -- since nothing here uses it)
//   - Partition Scheme: default_16MB.csv -- OTA-capable (2 app slots) and
//       sized for this unit's actual 16MB flash (platformio.ini)
//   - Flash on USB; then the device self-updates over WiFi thereafter.
//
// STORAGE: no SD card on this unit -- the queue + totalizer checkpoint live
// on internal flash (LittleFS, "spiffs" partition) instead. See queue.h/
// totalizer.h.
//
// FILE LAYOUT: all .h files must sit in the SAME folder as this .ino.
// ============================================================================
#include <LittleFS.h>
#include "esp_system.h"    // esp_reset_reason() -- Balaji V1 freeze remediation
#include "config.h"
#if WATCHDOG_ENABLE
#include "esp_task_wdt.h"  // Miki Wire hardening (F2): task watchdog -- see config.h
#endif
#include "boot_health.h"   // P1 hardening: app-level unhealthy-boot rollback safety net
#include "store.h"
#include "totalizer.h"
#include "queue.h"
#include "telemetry.h"
#include "sync.h"
#include "ota.h"
#include "provision.h"
#include "local_api.h"
#include "wifi_provision.h"
#include "sensor_stuck.h"  // Balaji V1 freeze remediation
#if SENSOR_MODE == SENSOR_MODE_CT
#include "sensor_ct.h"     // CT-clamp acquisition -- compiled ONLY into CT builds
#endif
#if MIKI_WIRE_PROFILE
#include "pulse_plausibility.h"  // Miki Wire hardening (F5) -- Miki builds only
#include "sensor_health.h"       // Miki Wire hardening (F6) -- Miki builds only
#endif

Store         store;
Totalizer     totalizer;
EventQueue    eventQueue;
Sync          syncEngine;
Ota           ota;
Provision     provision;
LocalApi      localApi;         // DM-Phase 1: read-only local diagnostics API
WifiProvision wifiProvision;    // DM-Phase 2: SoftAP + captive-portal provisioning
// Balaji V1 freeze remediation (Product Readiness Review P1-2): heuristic
// sensor-stuck-at-zero detector, see sensor_stuck.h.
SensorStuckDetector sensorStuck(SENSOR_STUCK_THRESHOLD_MS);
#if MIKI_WIRE_PROFILE
// Miki Wire hardening (F5/F6): both constructed inert (0 = disabled) and
// armed from validated NVS tunables in setup() -- INSUFFICIENT VERIFIED
// INFORMATION for real thresholds until site line parameters are confirmed
// (see config.h). Advisory-only by design: neither ever gates counting.
PulsePlausibilityMonitor pulsePlausibility(0);
SensorHealthMonitor      sensorHealth(0);
#endif
#if SENSOR_MODE == SENSOR_MODE_CT
// CT-clamp enhancement: converts debounced current-presence (DI2) into the
// same raw-pulse stream the NPN/PCNT path produces. See sensor_ct.h.
SensorCt sensorCt(CT_PULSE_HZ, CT_DEBOUNCE_MS);
#endif

uint32_t seq = 0;              // GLOBALLY monotonic telemetry sequence
uint32_t tTelemetry = 0, tPush = 0, tConfig = 0, tOta = 0;
bool     healthySignalled = false;
bool     crashStreakCleared = false;   // Balaji V1 freeze remediation

// Miki Wire hardening (F2/F3): background service invoked from the OTA
// download loop (ota.setServiceCallback below) -- the one legitimate
// multi-minute block in the firmware. Feeds the watchdog (progress is
// genuine liveness there; a dead transfer exits via OTA's own 15s stall
// detector) and, rate-limited, drains/checkpoints the totalizer so the
// 16-bit PCNT counter cannot wrap unobserved on a producing line while a
// download runs. Also reused by the AP-provisioning branch of loop().
void covioBackgroundService() {
#if WATCHDOG_ENABLE
  esp_task_wdt_reset();
#endif
  static uint32_t tBgTot = 0;
  uint32_t now = millis();
  if (now - tBgTot >= 30000UL) {   // 30s: well under any plausible PCNT wrap time
    tBgTot = now;
    totalizer.service(totalizer.lastSeq());   // drain + checkpoint, seq unchanged
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Covio Oil Flow Meter " FW_VERSION " ===");
  // Build-identity remediation: recorded in boot diagnostics (this line),
  // and separately exposed live via /api/v1/info (diagnostics.h) -- both
  // read the SAME BUILD_COMMIT/BUILD_DIRTY/BUILD_TIME_UTC compiler defines,
  // never a second hand-maintained copy.
  Serial.printf("build_commit=%s  build_dirty=%d  build_time_utc=%s\n",
                BUILD_COMMIT, BUILD_DIRTY, BUILD_TIME_UTC);

  // 1) config/identity first (also increments boot_id and restart_cnt)
  store.begin();

  // P1 hardening: app-level unhealthy-boot rollback safety net. Placed as
  // early as possible in setup() -- right after the one call it depends on
  // (store.begin()) -- so a crash anywhere below this point (filesystem
  // mount, WiFi init, or anything else) is captured by the streak. A boot
  // is "on trial" if THIS image's version pair has never reached
  // Ota::confirmHealthyBoot() before, independent of what the bootloader's
  // own (unreliable on this hardware, see config.h's UNHEALTHY_BOOT_STREAK_LIMIT
  // comment) PENDING_VERIFY state says. An already-confirmed image's
  // ordinary, unrelated crash never touches this: its version pair already
  // matches "last confirmed", so it's never on trial.
  {
    bool onTrial = isBootOnTrial(store.lastConfirmedFwVersion().c_str(), store.lastConfirmedSecurityVersion(),
                                  FW_VERSION, (uint32_t)FW_SECURITY_VERSION);
    if (onTrial) {
      store.incrementUnhealthyBootStreak();
      uint32_t streak = store.unhealthyBootStreak();
      Serial.printf("[BOOT] running unconfirmed image %s (sec=%d) - unhealthy boot streak=%u/%u\n",
                    FW_VERSION, FW_SECURITY_VERSION, streak, (unsigned)UNHEALTHY_BOOT_STREAK_LIMIT);
      if (streak >= UNHEALTHY_BOOT_STREAK_LIMIT) {
        Serial.println("[BOOT] FATAL: unhealthy boot streak limit reached -- "
                        "rolling back to the previous image (app-level safety "
                        "net, independent of bootloader PENDING_VERIFY)");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // Only reaches here if there was no valid alternate partition to
        // roll back to (e.g. this is a fresh USB-flashed baseline, not an
        // OTA-installed image) -- fall through and keep booting rather than
        // halt with no working image at all.
        Serial.println("[BOOT] rollback unavailable (no valid alternate partition) -- continuing boot");
      }
    }
  }

  // reset_ack (MW-001 commissioning reset) needs EventQueue/Totalizer;
  // safe to bind here even though their own begin() runs later below --
  // provision.begin() only stores the pointers, never dereferences them
  // until an operator actually types reset_ack over serial.
  provision.begin(&store, &eventQueue, &totalizer);
  Serial.printf("device_id=%s  boot_id=%u\n",
                store.deviceId().c_str(), store.bootId());
  Serial.println("(serial console ready — type 'help')");

#if MIKI_WIRE_PROFILE
  // Arm the Miki monitors from their validated NVS tunables (0 = inert).
  pulsePlausibility.setMaxHz(store.mikiMaxPulseHz());
  sensorHealth.setSuspectThresholdMs(store.mikiSuspectGapS() * 1000UL);
  Serial.printf("[MIKI] profile active: maxhz=%u suspect_gap_s=%u (0=off)\n",
                (unsigned)store.mikiMaxPulseHz(), (unsigned)store.mikiSuspectGapS());
#endif

  // Balaji V1 freeze remediation (Product Readiness Review P1-1): classify
  // THIS boot's cause exactly once, via the same esp_reset_reason() call
  // diagnostics.h already reads on-demand elsewhere in this same
  // translation unit -- that ESP-IDF call is stable for the whole boot, so
  // this is a second read of the same hardware-latched value, not a second
  // source of truth. Bumps the matching persistent NVS counter and the
  // crash-reset streak (cleared below once this boot proves healthy).
  {
    esp_reset_reason_t rr = esp_reset_reason();
    bool abnormal = false;
    switch (rr) {
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:
        store.incrementWatchdogResetCount();
        abnormal = true;
        break;
      case ESP_RST_BROWNOUT:
        store.incrementBrownoutResetCount();
        abnormal = true;
        break;
      case ESP_RST_PANIC:
        abnormal = true;
        break;
      default:
        break;
    }
    if (abnormal) store.incrementCrashResetStreak();
    Serial.printf("[DIAG] restarts=%u watchdog=%u brownout=%u crash_streak=%u\n",
                  store.restartCount(), store.watchdogResetCount(),
                  store.brownoutResetCount(), store.crashResetStreak());
  }

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

  // 2) internal flash filesystem — the queue and totalizer both live here
  // (no SD card on this unit). Fail loud if the partition won't mount.
  // `true` = format on first boot / if the filesystem is corrupt, matching
  // this project's existing "seed once, persist thereafter" NVS convention.
  if (!LittleFS.begin(true)) {
    Serial.println("[FATAL] Internal flash filesystem mount failed — cannot persist. Halting.");
    Serial.println("        (console still active: 'show' / 'set ...' work)");
    while (true) { provision.service(); delay(50); }  // allow provisioning
  }
  Serial.println("[OK] internal flash storage ready");

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
  // Miki Wire hardening (Phase-0 F4 corollary): resume seq from whichever is
  // higher -- the totalizer checkpoint or the durable ack watermark. On a
  // healthy boot these agree (checkpoint >= ack always); after a checkpoint
  // regression the ack floor prevents the "every new row filtered as
  // already-acked" transmission deadlock. See ack_validation.h.
  seq = resumeSeqFloor(totalizer.lastSeq(), eventQueue.ackedSeq());

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
    localApi.begin(&store, &totalizer, &eventQueue, &syncEngine, &ota, &sensorStuck);
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

#if BOARD_MODE == BOARD_8DI8DO
  // Multi-board enhancement: identify the non-default board variant on the
  // boot console (commissioning aid + cross-flash detection). The default
  // BOARD_RELAY1CH build prints nothing here, keeping the deployed
  // production baseline's serial output byte-identical.
  Serial.printf("[BOARD] ESP32-S3-POE-ETH-8DI-8DO (BOARD_MODE=%d): pulse=GPIO%d ct=GPIO%d\n",
                BOARD_MODE, PIN_PULSE, PIN_CT_STATE);
#endif

#if SENSOR_MODE == SENSOR_MODE_CT
  // CT-clamp enhancement: DI2 (PIN_CT_STATE) carries the CT current-sensing
  // switch's contact state through the board's inverting optocoupler
  // (active = GPIO reads LOW). INPUT_PULLUP gives the defined inactive-high
  // idle -- same posture as PIN_PULSE's own pull-up in totalizer.h.
  pinMode(PIN_CT_STATE, INPUT_PULLUP);
  Serial.printf("[CT] sensor mode CT: DI2/GPIO%d, %d pulse/s while current present\n",
                PIN_CT_STATE, CT_PULSE_HZ);
#endif

  // Miki Wire hardening (F2/F3): OTA's download loop proves liveness and
  // keeps the totalizer alive through the callback above.
  ota.setServiceCallback(covioBackgroundService);

#if WATCHDOG_ENABLE
  // Miki Wire hardening (F2): arm the task watchdog LAST -- after the
  // serviceable fatal-halt consoles above (which must stay reachable over
  // serial forever, not reset-loop) and after the bounded boot-time STA
  // wait. Rationale, timeout choice, and feed discipline: config.h.
  esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);   // true = panic (reset) on expiry
  esp_task_wdt_add(NULL);                        // subscribe this (loop) task
  Serial.printf("[WDT] task watchdog armed: %ds\n", WATCHDOG_TIMEOUT_S);
#endif

  Serial.println("[BOOT] entering main loop");
}

void loop() {
  uint32_t now = millis();
#if WATCHDOG_ENABLE
  // One feed per cooperative-loop iteration: the whole firmware is serviced
  // from this single loop, so completing an iteration IS the liveness proof
  // (see config.h). Not fed anywhere else except OTA's progress callback.
  esp_task_wdt_reset();
#endif
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
      localApi.begin(&store, &totalizer, &eventQueue, &syncEngine, &ota, &sensorStuck);
    }
    // Miki Wire hardening (Phase-0 finding F3, AP half): this early-return
    // branch previously skipped Totalizer::service() entirely -- the sole
    // PCNT drain and checkpoint writer. A device parked in provisioning
    // mode on a producing line could silently wrap the 16-bit hardware
    // counter and lose everything since the last checkpoint on power cut.
    // Counting must never depend on provisioning/network state: the same
    // rate-limited drain+checkpoint the OTA download path uses keeps it
    // alive here (telemetry records still pause -- only the lifetime count
    // and its durability continue, by design).
    covioBackgroundService();
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

#if SENSOR_MODE == SENSOR_MODE_CT
  // CT-clamp acquisition: debounced active-low DI2 level -> time-integrated
  // pulse synthesis (robust to blocking network gaps: the integral catches
  // up on return, see sensor_ct.h) -> the SAME accumulator the PCNT drain
  // path feeds. Everything below this line is identical in both modes.
  totalizer.injectSoftPulses(
      sensorCt.service(digitalRead(PIN_CT_STATE) == LOW, now));
#endif

  // ---- build a telemetry record every TELEMETRY_PERIOD_MS ----
  if (now - tTelemetry >= TELEMETRY_PERIOD_MS) {
    tTelemetry = now;
    seq++;
    uint64_t total = totalizer.total();     // live read (base + acc + PCNT)
    // Miki Wire hardening (Phase-0 finding F10): QUEUE_HIGHWATER was defined
    // but never consulted -- backlogHigh was hardcoded false, so the
    // QUALITY_BACKLOG_HIGH bit (already in the payload contract, already
    // stored by the server) never fired. Wire the real backlog state in so
    // storage pressure is finally visible remotely, not just on the LAN API.
    QRow row = Telemetry::build(store, total, seq, now / 1000,
                                eventQueue.pendingCount() > QUEUE_HIGHWATER);
#if MIKI_WIRE_PROFILE
    // Miki Wire hardening (F5/F6): stamp advisory quality bits BEFORE the
    // durable append (append() computes the row CRC over the final bytes).
    // Live-reload of console-changed tunables is one cheap NVS read per
    // second, matching how cfgVer is already polled elsewhere.
    pulsePlausibility.setMaxHz(store.mikiMaxPulseHz());
    sensorHealth.setSuspectThresholdMs(store.mikiSuspectGapS() * 1000UL);
    if (pulsePlausibility.update(total, now))                    row.quality |= QUALITY_SUSPECT_RATE;
    if (sensorHealth.update(total, now) == SENSOR_HEALTH_SUSPECT) row.quality |= QUALITY_SENSOR_SUSPECT;
#endif
    eventQueue.append(row);                      // 1) durable row FIRST
    totalizer.service(seq);                 // 2) THEN checkpoint total+seq
    // ORDER MATTERS: if power dies between 1 and 2, this seq regenerates on
    // next boot and the duplicate row is absorbed by the server (idempotent).
    // The reverse order could burn a seq with no row behind it — a permanent
    // gap that freezes the cumulative ACK forever.

    // Balaji V1 freeze remediation (Product Readiness Review P1-2): feed
    // the same `total` reading already taken above into the stuck-sensor
    // heuristic, once per telemetry cycle. Read-only elsewhere (local_api.h)
    // via isStuck()/msSinceLastChange() -- this is the ONLY call site that
    // mutates sensorStuck's state.
    sensorStuck.update(total, now);
  }

  // Balaji V1 freeze remediation (Product Readiness Review P1-1): a run
  // that survives HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS (config.h) is
  // evidence THIS boot is not part of a crash loop, whatever caused past
  // resets -- clears the persisted streak exactly once per boot.
  if (!crashStreakCleared && now >= HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS) {
    store.clearCrashResetStreak();
    crashStreakCleared = true;
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
    ota.poll(syncEngine.online(), syncEngine);    // DM-Phase 2: WiFi-authority consolidation -- may download + reboot into new image.
                                                   // RISK-16: syncEngine also supplies the best-effort time estimate for manifest expiry checks.
  }

  delay(5);                           // yield
}
