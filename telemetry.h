// ============================================================================
// telemetry.h  —  build records + serialize the push payload
// ----------------------------------------------------------------------------
// THE CALCULATION MODEL (this is the crux of your "K-factor from the website"
// requirement):
//
//   The device sends RAW PULSE TOTALS. It does NOT convert to litres.
//   The server owns the K-factor and computes:
//
//       litres_this_record = (totalizer_now - totalizer_prev) / K_factor
//       (optionally temperature-corrected via density & T_ref)
//
//   Because history is stored as raw pulses, re-calibrating is just editing K
//   on the website and recomputing — no device visit, no data loss, no reflash.
//
//   The device DOES cache K locally (store.h) but only to show a *display-only*
//   live flow rate. The authoritative business number is always server-side.
//   Each record stamps `kfactor_version` so you know which calibration the
//   server should apply if K changed over time.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "queue.h"
#include "store.h"

class Telemetry {
public:
  // Build one QRow snapshot. seq is monotonic within a boot_id.
  static QRow build(Store& st, uint64_t totalizer, uint32_t seq,
                    uint32_t uptimeSec, bool backlogHigh) {
    QRow r{};
    r.schema_version = SCHEMA_VERSION_CURRENT;  // ADR-001
    r.record_type    = RECORD_TYPE_TELEMETRY;   // ADR-001
    r.boot_id   = st.bootId();
    r.seq       = seq;
    r.ts        = uptimeSec;
    r.totalizer = totalizer;
    r.quality   = QUALITY_OK;
    if (backlogHigh) r.quality |= QUALITY_BACKLOG_HIGH;
    r.rssi_abs  = (uint16_t)abs(WiFi.RSSI());
    return r;
  }

  // Serialize a batch of rows into the push JSON body.
  // Contract (shared by LCS and cloud — they must accept this identically):
  // {
  //   "device_id":"esp32-....","fw":"1.0.0","model":"covio-oilflow-v1",
  //   "kfactor_version": <uint>,
  //   "records":[
  //     {"schema_version":N,"record_type":N,"boot_id":N,"seq":N,"ts":N,
  //      "totalizer":N,"quality":N,"rssi":-N}, ...
  //   ]
  // }
  // ADR-001: schema_version/record_type are carried per-record (not just once
  // per batch) so the receiver can validate/dispatch each record independently
  // — this is what makes a batch mixing the current and previous schema
  // version (normal during an OTA rollout window) well-formed and acceptable.
  // `otaState` / `otaReject` are the device's own OTA status, forwarded to the
  // cloud in the ENVELOPE -- alongside fw/model/kfactor_version, which are
  // likewise facts about the sender rather than about any record.
  //
  // WHY THIS EXISTS. The reject reason used to live only on the LAN-only
  // /api/v1/health endpoint and the serial console, so the only way to learn
  // why a meter had refused an update was to send a person to the plant and
  // put them on its network. That cost real trips, and on one of them a live
  // production meter was nearly reflashed by mistake. A meter that can explain
  // itself to the cloud never needs that visit again.
  //
  // ADR-001 IS NOT AFFECTED. schema_version and record_type are carried
  // PER-RECORD and are unchanged; this adds an envelope-level field only, so
  // the record contract every receiver validates against is byte-for-byte what
  // it was. Receivers read the envelope with .get()-style lookups and ignore
  // what they do not know, so an older receiver simply does not see it.
  //
  // Both default to nullptr, and a nullptr is omitted rather than sent as the
  // string "null" -- absent means "this build/caller had nothing to say",
  // which is different from a JSON null meaning "asked, and there is none".
  static String toJson(Store& st, const QRow* rows, int n,
                       const char* otaState = nullptr,
                       const char* otaReject = nullptr) {
    String s;
    s.reserve(160 + n * 128);
    s  = "{\"device_id\":\"" + st.deviceId() + "\"";
    s += ",\"fw\":\"" FW_VERSION "\"";
    s += ",\"model\":\"" DEVICE_MODEL "\"";
    s += ",\"kfactor_version\":" + String(st.cfgVer());
    if (otaState || otaReject) {
      s += ",\"ota\":{";
      bool first = true;
      if (otaState)  { s += "\"state\":\"" + String(otaState) + "\""; first = false; }
      if (otaReject) {
        if (!first) s += ",";
        s += "\"last_reject_reason\":\"" + String(otaReject) + "\"";
      }
      s += ",\"security_version\":" + String(st.securityVersion());
      s += "}";
    }
    s += ",\"records\":[";
    for (int i = 0; i < n; i++) {
      if (i) s += ",";
      const QRow& r = rows[i];
      s += "{\"schema_version\":" + String(r.schema_version);
      s += ",\"record_type\":"    + String(r.record_type);
      s += ",\"boot_id\":" + String(r.boot_id);
      s += ",\"seq\":"      + String(r.seq);
      s += ",\"ts\":"       + String(r.ts);
      s += ",\"totalizer\":" + String((unsigned long long)r.totalizer);
      s += ",\"quality\":"  + String(r.quality);
      s += ",\"rssi\":-"    + String(r.rssi_abs);
      s += "}";
    }
    s += "]}";
    return s;
  }
};
