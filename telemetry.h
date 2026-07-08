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
  //     {"boot_id":N,"seq":N,"ts":N,"totalizer":N,"quality":N,"rssi":-N}, ...
  //   ]
  // }
  static String toJson(Store& st, const QRow* rows, int n) {
    String s;
    s.reserve(128 + n * 96);
    s  = "{\"device_id\":\"" + st.deviceId() + "\"";
    s += ",\"fw\":\"" FW_VERSION "\"";
    s += ",\"model\":\"" DEVICE_MODEL "\"";
    s += ",\"kfactor_version\":" + String(st.cfgVer());
    s += ",\"records\":[";
    for (int i = 0; i < n; i++) {
      if (i) s += ",";
      const QRow& r = rows[i];
      s += "{\"boot_id\":" + String(r.boot_id);
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
