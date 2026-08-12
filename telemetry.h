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
#include "reset_reason.h"

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
  //   "boot":{"boot_id":N,"reset_reason":"power_on","reset_reason_raw":1,
  //           "restart_count":N,"watchdog_reset_count":N,
  //           "brownout_reset_count":N},
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

    // ---- why this device is running, and how it has been behaving --------
    //
    // WHY THIS EXISTS. A gap in the readings has two completely different
    // meanings and the cloud could not tell them apart. If the meter lost
    // power, the pump on the same supply stopped too, so no oil moved and
    // the day's total is EXACT. If the meter reset itself while the plant
    // kept running, oil flowed past an unpowered sensor and the total is a
    // floor. Both looked identical from the server, so every gap had to be
    // treated as the bad case -- which meant days that were in fact complete
    // were reported to the owner as unreliable. The device has always known
    // the answer; it just had no way to say it, because esp_reset_reason()
    // was published only on the LAN-only /api/v1/metrics endpoint that
    // nobody outside the plant network can reach.
    //
    // BOOT_ID IS LOAD-BEARING, NOT DECORATION. A push batch can carry
    // records from an EARLIER boot -- that is exactly what happens when the
    // queue flushes a backlog after a restart -- so the reset cause must
    // name the boot it belongs to. Without it a receiver would attribute
    // this boot's reason to whichever boot the records happened to come
    // from, and cheerfully clear a gap the meter was never off for.
    //
    // The raw numeric cause always travels alongside the mapped name, so an
    // enumerator this firmware does not know about arrives identifiable
    // rather than flattened into a bare "unknown". The three counters are
    // NVS-backed lifetime totals (store.h) and survive reboots: a climbing
    // brownout count is a supply problem worth an electrician, and a
    // climbing watchdog count is a firmware problem worth us.
    //
    // ADR-001 IS UNAFFECTED, exactly as with the `ota` block below:
    // schema_version and record_type stay PER-RECORD and unchanged, this is
    // envelope-level only, and receivers that do not know the key ignore it.
    {
      esp_reset_reason_t rr = esp_reset_reason();
      const char* rrName = resetReasonName(rr);
      s += ",\"boot\":{\"boot_id\":" + String(st.bootId());
      s += ",\"reset_reason\":\"";
      if (rrName) {
        s += rrName;
      } else {
        // Never a bare "unknown": the number keeps an enumerator this build
        // has not been taught about identifiable from the payload alone.
        s += "unknown(";
        s += String((int)rr);
        s += ")";
      }
      s += "\"";
      s += ",\"reset_reason_raw\":" + String((int)rr);
      s += ",\"restart_count\":" + String(st.restartCount());
      s += ",\"watchdog_reset_count\":" + String(st.watchdogResetCount());
      s += ",\"brownout_reset_count\":" + String(st.brownoutResetCount());
      s += "}";
    }

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
