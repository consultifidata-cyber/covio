// ============================================================================
// provision.h  —  serial provisioning console
// ----------------------------------------------------------------------------
// WHY THIS EXISTS: NVS defaults from config.h are seeded ONCE (first boot).
// After that, reflashing with new #defines changes NOTHING. This console is
// how you change server_url / api_key / WiFi on a running device with no
// reflash — plug in USB, open Serial Monitor @115200, type commands.
//
// Commands (newline-terminated):
//   help                      list commands
//   show                      print identity + current NVS config
//   set url <http(s)://...>   change server endpoint      (reboot to apply)
//   set key <apikey>          change API key
//   set wifi <ssid> <pass>    change WiFi (no spaces in ssid — limitation)
//   reboot                    restart now
//   factory                   wipe NVS (re-seeds from config.h defaults) + reboot
// ============================================================================
#pragma once
#include <Arduino.h>
#include "store.h"

class Provision {
public:
  void begin(Store* st) { st_ = st; }

  void service() {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (buf_.length()) handle_(buf_);
        buf_ = "";
      } else if (buf_.length() < 220) buf_ += c;
    }
  }

private:
  void handle_(String line) {
    line.trim();
    if (line == "help") {
      Serial.println("cmds: show | set url <u> | set key <k> | set wifi <ssid> <pass> | reboot | factory");
    } else if (line == "show") {
      Serial.printf("device_id : %s\n", st_->deviceId().c_str());
      Serial.printf("fw        : %s\n", FW_VERSION);
      Serial.printf("boot_id   : %u\n", st_->bootId());
      Serial.printf("server_url: %s\n", st_->serverUrl().c_str());
      Serial.printf("api_key   : %s\n", st_->apiKey().c_str());
      Serial.printf("wifi_ssid : %s\n", st_->wifiSsid().c_str());
      Serial.printf("calib     : v%u  K=%.4f  density=%.3f  Tref=%.1f\n",
                    st_->cfgVer(), st_->kFactor(), st_->density(), st_->tRef());
    } else if (line.startsWith("set url ")) {
      st_->setServerUrl(line.substring(8));
      Serial.println("[PROV] server_url saved. 'reboot' to apply cleanly.");
    } else if (line.startsWith("set key ")) {
      st_->setApiKey(line.substring(8));
      Serial.println("[PROV] api_key saved.");
    } else if (line.startsWith("set wifi ")) {
      String rest = line.substring(9);
      int sp = rest.indexOf(' ');
      if (sp > 0) {
        st_->setWifi(rest.substring(0, sp), rest.substring(sp + 1));
        Serial.println("[PROV] wifi saved. 'reboot' to apply.");
      } else Serial.println("[PROV] usage: set wifi <ssid> <pass>");
    } else if (line == "reboot") {
      Serial.println("[PROV] rebooting..."); delay(200); ESP.restart();
    } else if (line == "factory") {
      Serial.println("[PROV] factory reset: wiping NVS, rebooting...");
      st_->factoryReset(); delay(200); ESP.restart();
    } else {
      Serial.println("[PROV] unknown. type: help");
    }
  }

  Store* st_ = nullptr;
  String buf_;
};
