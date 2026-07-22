// ============================================================================
// diagnostics.h  —  DM-Phase 1: builds the local-API JSON bodies
// ----------------------------------------------------------------------------
// One function per §13 (Appendix A, Covio_Device_Manager_Live_Readiness_Plan.md)
// endpoint. Every field name, type, and enum value here is taken directly from
// §13 -- that section is authoritative; if anything here appears to conflict
// with it, §13 wins (per the plan's own stated convention).
//
// Hand-rolled JSON string building, matching telemetry.h's existing toJson()
// pattern -- not a new parsing/serialization library (DM-Phase 0A's decision,
// consistent with MASTER_GOVERNANCE.md §3's "no new dependency without an
// ADR" rule).
//
// This module only READS already-computed state via existing public getters
// (Store/Totalizer/EventQueue/Sync/Ota) -- it never mutates device state and
// never reaches into any module's private (trailing-underscore) fields.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_system.h"   // esp_reset_reason(), esp_get_minimum_free_heap_size()
#include "config.h"
#include "store.h"
#include "totalizer.h"
#include "queue.h"
#include "sync.h"
#include "ota.h"

class Diagnostics {
public:
  // GET /api/v1/info -- static identity, never changes without a reboot.
  static String buildInfoJson(Store& st) {
    String s;
    s.reserve(192);
    s  = "{\"hardware_id\":\"" + st.deviceId() + "\"";
    // DM-Phase 6 (§11.2/ADR-018): "" (store.h's unassigned sentinel) maps to
    // JSON null, exactly the frozen contract's "null until assigned at
    // factory commissioning" semantics -- populated for real once the
    // factory workflow's new endpoint (local_api.h) has written it.
    String logicalId = st.logicalDeviceId();
    s += ",\"logical_device_id\":" + (logicalId.length() ? ("\"" + logicalId + "\"") : String("null"));
    s += ",\"asset_id\":null";            // ADR-009: null until an installer sets it (unimplemented)
    s += ",\"fw_version\":\"" FW_VERSION "\"";
    s += ",\"model\":\"" DEVICE_MODEL "\"";
    s += ",\"boot_id\":" + String(st.bootId());
    s += ",\"schema_version_current\":" + String(SCHEMA_VERSION_CURRENT);
    s += "}";
    return s;
  }

  // GET /api/v1/status -- operational summary.
  static String buildStatusJson(Store& st, Totalizer& tot, EventQueue& q,
                                 Sync& sync, Ota& ota, bool sdPresent) {
    String healthState, alarmsJson;
    computeHealth_(q, sync, ota, sdPresent, healthState, alarmsJson);

    String s;
    s.reserve(448);
    s  = "{\"uptime_ms\":" + String(millis());
    s += ",\"wifi\":{\"connected\":";
    s += (sync.online() ? "true" : "false");
    s += ",\"ssid\":\"" + st.wifiSsid() + "\"";
    s += ",\"rssi_dbm\":" + String((int)WiFi.RSSI()) + "}";
    s += ",\"server_url\":\"" + st.serverUrl() + "\"";
    s += ",\"api_key_status\":\"" + apiKeyStatus_(st) + "\"";
    s += ",\"sd_status\":\"";
    s += (sdPresent ? "ok" : "absent");
    s += "\"";
    s += ",\"queue\":{\"backlog\":" + String(q.pendingCount());
    s += ",\"acked_seq\":" + String(q.ackedSeq());
    s += ",\"last_seq\":" + String(tot.lastSeq());
    // P0-4 remediation (RISK-04): real filesystem-reported capacity + the
    // durable failed-write record, both now genuinely remotely visible --
    // "server and Device Manager show the fault" (mandatory P0-4 test).
    s += ",\"capacity_pct_used\":" + String(q.capacityPercentUsed(), 1);
    s += ",\"failed_write_count\":" + String(q.failedWriteCount());
    if (q.hasFailedWrite()) {
      s += ",\"last_write_failure\":{\"error_code\":\"" + String(q.lastFailureCodeStr()) + "\"";
      s += ",\"first_failure_uptime_s\":" + String(q.firstFailureUptimeS());
      s += ",\"last_failure_uptime_s\":" + String(q.lastFailureUptimeS()) + "}";
    } else {
      s += ",\"last_write_failure\":null";
    }
    s += "}";
    s += ",\"totalizer_raw_pulses\":" + String((unsigned long long)tot.total());
    s += ",\"last_sync_ms_ago\":";
    s += sync.haveSync() ? String(millis() - sync.lastSyncMs()) : String("null");
    s += ",\"last_push_http_code\":";
    s += sync.havePush() ? String(sync.lastPushHttpCode()) : String("null");
    s += ",\"ota\":{\"state\":\"" + String(otaStateStr_(ota.state())) + "\"";
    s += ",\"running_version\":\"" FW_VERSION "\"";
    // RISK-15 remediation: security-version floor + last-reject-reason, so
    // Device Manager/an operator can see WHY a candidate was rejected
    // (mandate requirement: "Device Manager shows previous and current
    // versions" / distinguishing rejection reasons), not just OTA silence.
    s += ",\"security_version\":" + String(FW_SECURITY_VERSION);
    s += ",\"accepted_security_floor\":" + String(st.securityVersion());
    if (ota.lastRejectReason() != OTA_ACCEPT) {
      s += ",\"last_reject_reason\":\"" + String(otaVerdictStr(ota.lastRejectReason())) + "\"";
    } else {
      s += ",\"last_reject_reason\":null";
    }
    // RISK-16 remediation: manifest-authenticity rejection reason, separate
    // from RISK-15's anti-downgrade reason above -- an operator needs to
    // tell "this was a downgrade" apart from "this manifest's signature
    // didn't verify" apart from "the device has no time estimate yet".
    if (ota.lastAuthRejectReason() != OTA_AUTH_OK) {
      s += ",\"last_auth_reject_reason\":\"" + String(otaAuthVerdictStr(ota.lastAuthRejectReason())) + "\"";
    } else {
      s += ",\"last_auth_reject_reason\":null";
    }
    s += "}";
    s += ",\"health_state\":\"" + healthState + "\"";
    s += "}";
    return s;
  }

  // GET /api/v1/health -- derived health state + active alarms.
  static String buildHealthJson(EventQueue& q, Sync& sync, Ota& ota, bool sdPresent) {
    String healthState, alarmsJson;
    computeHealth_(q, sync, ota, sdPresent, healthState, alarmsJson);
    String s;
    s.reserve(64 + alarmsJson.length());
    s  = "{\"health_state\":\"" + healthState + "\"";
    s += ",\"alarms\":" + alarmsJson;
    s += "}";
    return s;
  }

  // GET /api/v1/metrics -- deep diagnostics, distinct from /status (§3.1a).
  // pulseFreqHz/rssiHistory are supplied by the caller (local_api.h owns the
  // periodic sampling that produces them -- this module stays a pure builder
  // over already-known values, consistent with its own header comment above).
  static String buildMetricsJson(EventQueue& q, Sync& sync, bool sdPresent,
                                  bool havePulseFreqHz, float pulseFreqHz,
                                  const int16_t* rssiHistory, int rssiHistoryCount) {
    String s;
    s.reserve(384 + rssiHistoryCount * 8);
    s  = "{\"free_heap_bytes\":" + String(ESP.getFreeHeap());
    s += ",\"heap_low_water_mark_bytes\":" + String(esp_get_minimum_free_heap_size());
    s += ",\"reset_reason\":\"" + resetReasonStr_() + "\"";
    s += ",\"cpu_freq_mhz\":" + String(ESP.getCpuFreqMHz());
    s += ",\"flash_size_bytes\":" + String(ESP.getFlashChipSize());

    // null if SD absent (§13 A.3) OR no row has been written yet this boot
    // (A.1's broader "a metric that cannot yet be computed is null" rule).
    bool haveSdLatency = sdPresent && q.haveRowWriteLatency();
    s += ",\"sd_write_latency_ms\":";
    s += haveSdLatency ? String(q.lastRowWriteLatencyMs()) : String("null");

    // No documented null-condition in §13's table beyond A.1's general rule;
    // null until pending() has actually run at least once this boot.
    s += ",\"queue_read_latency_ms\":";
    s += q.havePendingLatency() ? String(q.lastPendingLatencyMs()) : String("null");

    s += ",\"pulse_frequency_hz\":";
    s += havePulseFreqHz ? String(pulseFreqHz, 3) : String("null");

    // null if device offline (§13 A.3) OR no push attempted yet this boot.
    bool haveRtt = sync.online() && sync.havePush();
    s += ",\"network_rtt_ms\":";
    s += haveRtt ? String(sync.lastPushRttMs()) : String("null");

    s += ",\"rssi_history_dbm\":[";
    for (int i = 0; i < rssiHistoryCount; i++) {
      if (i) s += ",";
      s += String(rssiHistory[i]);
    }
    s += "]";

    // Not applicable to this hardware build -- the MAX31865 RTD module was
    // explicitly removed (config.h's header comment). Reserved, never
    // populated speculatively (§13 A.3's own note on this field).
    s += ",\"temperature_c\":null";
    s += "}";
    return s;
  }

  // DM-Phase 1: thin accessor so the HTML status page (local_api.h) can
  // reuse the exact same health_state precedence rule as buildStatusJson()/
  // buildHealthJson() without parsing either JSON body back apart.
  static String healthStateOnly(EventQueue& q, Sync& sync, Ota& ota, bool sdPresent) {
    String healthState, alarmsJson;
    computeHealth_(q, sync, ota, sdPresent, healthState, alarmsJson);
    return healthState;
  }

  // Public (not just used internally by the JSON builders above): the HTML
  // status page (local_api.h) reuses these two exactly rather than
  // duplicating the same mapping a second time.
  static String apiKeyStatus_(Store& st) {
    String key = st.apiKey();
    if (key.length() == 0)      return "missing";
    if (key == DEFAULT_API_KEY) return "default";
    return "configured";
  }

  static const char* otaStateStr_(OtaState s) {
    switch (s) {
      case OTA_STATE_PENDING_VERIFY: return "pending_verify";
      case OTA_STATE_CONFIRMED:      return "confirmed";
      case OTA_STATE_FAILED:         return "failed";
      default:                       return "none";
    }
  }

private:
  static String resetReasonStr_() {
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   return "power_on";
      case ESP_RST_SW:        return "software";
      case ESP_RST_PANIC:     return "panic";
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:       return "watchdog";
      case ESP_RST_BROWNOUT:  return "brownout";
      case ESP_RST_DEEPSLEEP: return "deepsleep";
      default:                 return "unknown";
    }
  }

  // Shared by buildStatusJson()/buildHealthJson() so the health_state
  // precedence rule (§13 A.4) and the alarm set (§11.4, DM-Phase-1-producible
  // subset only) are computed exactly once, not duplicated (MASTER_GOVERNANCE
  // §1 Rule 5 -- avoid the kind of duplicated logic already flagged once in
  // this project between sync.h/ota.h's JSON extractors).
  //
  // Only alarm types buildable from DM-Phase 1's own new state are ever
  // produced: QUEUE_HIGH/QUEUE_CRITICAL (maintained backlog counter),
  // OTA_FAILED (new ota.state()), SD_REMOVED (present/absent check). Every
  // other §11.4 alarm type requires either a counter this phase doesn't
  // introduce (WIFI_RECONNECT_LOOP, REBOOT_LOOP) or an ADR explicitly out of
  // scope here (OTA_ROLLBACK/BROWNOUT_DETECTED -> ADR-006, SENSOR_STOPPED ->
  // ADR-015) -- never fabricated.
  //
  // raised_at_ms_ago is always 0: these are level-triggered conditions
  // recomputed fresh on every request, not tracked state transitions. Real
  // "since when" tracking is future work (§11.5's event timeline, DM-Phase 4)
  // -- reporting a fabricated duration here would be worse than reporting an
  // honest "detected as of this instant".
  static void computeHealth_(EventQueue& q, Sync& sync, Ota& ota, bool sdPresent,
                              String& healthStateOut, String& alarmsJsonOut) {
    String alarms = "[";
    bool first = true;
    bool haveCritical = false, haveWarning = false;

    uint32_t backlog = q.pendingCount();
    if (backlog > 10000UL) {
      if (!first) alarms += ",";
      alarms += "{\"type\":\"QUEUE_CRITICAL\",\"severity\":\"CRITICAL\","
                "\"raised_at_ms_ago\":0,"
                "\"message\":\"Unacked queue backlog exceeds 10000 rows\"}";
      first = false; haveCritical = true;
    } else if (backlog > 1000UL) {
      if (!first) alarms += ",";
      alarms += "{\"type\":\"QUEUE_HIGH\",\"severity\":\"WARNING\","
                "\"raised_at_ms_ago\":0,"
                "\"message\":\"Unacked queue backlog exceeds 1000 rows\"}";
      first = false; haveWarning = true;
    }

    if (ota.state() == OTA_STATE_FAILED) {
      if (!first) alarms += ",";
      alarms += "{\"type\":\"OTA_FAILED\",\"severity\":\"WARNING\","
                "\"raised_at_ms_ago\":0,"
                "\"message\":\"Last OTA update attempt failed\"}";
      first = false; haveWarning = true;
    }

    if (!sdPresent) {
      if (!first) alarms += ",";
      alarms += "{\"type\":\"SD_REMOVED\",\"severity\":\"CRITICAL\","
                "\"raised_at_ms_ago\":0,"
                "\"message\":\"Internal flash storage (LittleFS) not mounted\"}";
      first = false; haveCritical = true;
    }

    // P0-4 remediation (RISK-04): a durable, previously-silent measurement
    // loss is now a loud CRITICAL alarm -- this is the mandate's own
    // required invariant ("the device must enter a clearly observable
    // degraded/fault state rather than silently continue as if data were
    // safe"), not merely a log line nobody may ever read. Stays raised for
    // the rest of the device's life (EventQueue::hasFailedWrite() is
    // monotonic, see queue.h) -- a historical loss must remain visible,
    // not silently age out.
    if (q.hasFailedWrite()) {
      if (!first) alarms += ",";
      alarms += "{\"type\":\"QUEUE_WRITE_FAILURE\",\"severity\":\"CRITICAL\","
                "\"raised_at_ms_ago\":0,"
                "\"message\":\"" + String(q.failedWriteCount()) + " measurement(s) failed to persist "
                "durably (last: " + String(q.lastFailureCodeStr()) + ")\"}";
      first = false; haveCritical = true;
    }

    // P0-4 remediation (RISK-04): real filesystem-reported capacity
    // thresholds (mandate requirement: "define behavior at 80%/90%/95%/
    // 100% capacity"), sourced from LittleFS.usedBytes()/totalBytes() --
    // not an estimated row count. Checked highest-first so only the single
    // most severe threshold currently crossed is ever reported.
    // RISK-04 phase-2: the threshold SELECTION itself now lives in queue.h's
    // capacityAlarmLevel() (a pure function, host-tested) -- this block only
    // maps that already-decided level to the alarm JSON shape.
    switch (capacityAlarmLevel(q.capacityPercentUsed())) {
      case QCAP_CRITICAL_100:
        if (!first) alarms += ",";
        alarms += "{\"type\":\"STORAGE_FULL\",\"severity\":\"CRITICAL\","
                  "\"raised_at_ms_ago\":0,"
                  "\"message\":\"Queue storage partition is full\"}";
        first = false; haveCritical = true;
        break;
      case QCAP_CRITICAL_95:
        if (!first) alarms += ",";
        alarms += "{\"type\":\"STORAGE_CRITICAL\",\"severity\":\"CRITICAL\","
                  "\"raised_at_ms_ago\":0,"
                  "\"message\":\"Queue storage >=95% full\"}";
        first = false; haveCritical = true;
        break;
      case QCAP_WARNING_90:
        if (!first) alarms += ",";
        alarms += "{\"type\":\"STORAGE_HIGH\",\"severity\":\"WARNING\","
                  "\"raised_at_ms_ago\":0,"
                  "\"message\":\"Queue storage >=90% full\"}";
        first = false; haveWarning = true;
        break;
      case QCAP_WARNING_80:
        if (!first) alarms += ",";
        alarms += "{\"type\":\"STORAGE_WARNING\",\"severity\":\"WARNING\","
                  "\"raised_at_ms_ago\":0,"
                  "\"message\":\"Queue storage >=80% full\"}";
        first = false; haveWarning = true;
        break;
      default: break;
    }

    alarms += "]";
    alarmsJsonOut = alarms;

    // §13 A.4 health_state precedence, evaluated top-to-bottom, first match wins.
    bool offline = !sync.haveSync() || (millis() - sync.lastSyncMs() > 300000UL);
    if (offline)          healthStateOut = "offline";
    else if (haveCritical) healthStateOut = "degraded";
    else if (haveWarning)  healthStateOut = "degraded";
    else                    healthStateOut = "ok";
  }
};
