// eth_link.h -- wired Ethernet (W5500) uplink for the Waveshare
// ESP32-S3-POE-ETH-8DI-8DO board.
//
// Why: some plants offer only a LAN cable, no Wi-Fi (Auto Profile, 2026-11-28).
// Until now this firmware was Wi-Fi only and the board's W5500 sat unused.
//
// Design (deliberately additive -- nothing about Wi-Fi changes):
//  * Uses ESP-IDF 4.4's own W5500 driver (CONFIG_ETH_SPI_ETHERNET_W5500=y in
//    the arduino-esp32 2.0.14 prebuilt SDK). Arduino 2.0.x's ETH class only
//    knows RMII PHYs, so it cannot drive the W5500.
//  * DHCP on the wired port via the stock "ETH_DEF" esp_netif, so mDNS and
//    lwIP treat it like any default Ethernet interface.
//  * route_prio is raised above Wi-Fi STA: when the cable has a link + IP,
//    all traffic (push, config, OTA) leaves over the wire; when the cable is
//    pulled, lwIP falls back to Wi-Fi automatically.
//  * Any init failure is logged and leaves the device exactly as a Wi-Fi-only
//    board -- Ethernet can never stop counting or Wi-Fi sync.
//
// Pins: Waveshare wiki "W5500 Ethernet chip" table -- SCLK 15, MOSI 13,
// MISO 14, CS 16, INT 12, RST 39. None overlap DI1-8 (GPIO4-11), the
// station I2C (41/42) or the TF slot (45/47/48).
#pragma once

#include "config.h"

#if ETH_ENABLE
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"

extern bool tcpipInit();   // arduino-esp32 WiFiGeneric.cpp: esp_netif_init + default event loop, once

namespace EthLink {

namespace detail {
  inline volatile bool& linkUp() { static volatile bool v = false; return v; }
  inline volatile bool& gotIp()  { static volatile bool v = false; return v; }
  inline volatile uint32_t& ip() { static volatile uint32_t v = 0; return v; }
  inline bool& started()         { static bool v = false; return v; }
  inline esp_netif_t*& netif()   { static esp_netif_t* v = nullptr; return v; }

  inline void onEthEvent(void*, esp_event_base_t, int32_t id, void*) {
    switch (id) {
      case ETHERNET_EVENT_CONNECTED:
        linkUp() = true;
        Serial.println("[ETH] link up");
        break;
      case ETHERNET_EVENT_DISCONNECTED:
        linkUp() = false; gotIp() = false; ip() = 0;
        Serial.println("[ETH] link down");
        break;
      default: break;
    }
  }
  inline void onIpEvent(void*, esp_event_base_t, int32_t id, void* data) {
    if (id == IP_EVENT_ETH_GOT_IP) {
      const ip_event_got_ip_t* e = (const ip_event_got_ip_t*)data;
      ip() = e->ip_info.ip.addr;
      gotIp() = true;
      Serial.printf("[ETH] got IP " IPSTR "\n", IP2STR(&e->ip_info.ip));
    } else if (id == IP_EVENT_ETH_LOST_IP) {
      gotIp() = false; ip() = 0;
      Serial.println("[ETH] lost IP");
    }
  }
  inline bool fail(const char* what, esp_err_t err) {
    Serial.printf("[ETH] init failed at %s: %s -- Wi-Fi only\n", what, esp_err_to_name(err));
    return false;
  }
}  // namespace detail

// True when the cable has link AND DHCP gave an address -- i.e. usable uplink.
inline bool up() { return detail::linkUp() && detail::gotIp(); }
inline bool started() { return detail::started(); }
inline bool linkUp() { return detail::linkUp(); }
inline String ipString() {
  uint32_t a = detail::ip();
  if (!a) return String("");
  char buf[16];
  snprintf(buf, sizeof buf, "%u.%u.%u.%u", (unsigned)(a & 0xff), (unsigned)((a >> 8) & 0xff),
           (unsigned)((a >> 16) & 0xff), (unsigned)((a >> 24) & 0xff));
  return String(buf);
}

// Call once from setup(), AFTER Wi-Fi has been started (WiFi.mode/begin), so
// arduino-esp32 owns creation of the default event loop as it expects.
inline bool begin() {
  if (detail::started()) return true;
  if (!tcpipInit()) return detail::fail("tcpipInit", ESP_FAIL);

  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return detail::fail("isr_service", err);

  // Hardware reset pulse so a W5500 left wedged by a brownout starts clean.
  pinMode(ETH_PIN_RST, OUTPUT);
  digitalWrite(ETH_PIN_RST, LOW);  delay(5);
  digitalWrite(ETH_PIN_RST, HIGH); delay(50);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = ETH_PIN_MOSI;
  bus.miso_io_num = ETH_PIN_MISO;
  bus.sclk_io_num = ETH_PIN_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  err = spi_bus_initialize(ETH_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) return detail::fail("spi_bus_initialize", err);

  spi_device_interface_config_t dev = {};
  dev.command_bits = 16;      // W5500 address phase
  dev.address_bits = 8;       // W5500 control phase
  dev.mode = 0;
  dev.clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000;
  dev.spics_io_num = ETH_PIN_CS;
  dev.queue_size = 20;
  spi_device_handle_t spi = nullptr;
  err = spi_bus_add_device(ETH_SPI_HOST, &dev, &spi);
  if (err != ESP_OK) return detail::fail("spi_bus_add_device", err);

  eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(spi);
  w5500.int_gpio_num = ETH_PIN_INT;
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  phyCfg.reset_gpio_num = -1;   // reset already pulsed above
  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500, &macCfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phyCfg);
  if (!mac || !phy) return detail::fail("mac/phy alloc", ESP_ERR_NO_MEM);

  esp_eth_config_t ethCfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth = nullptr;
  err = esp_eth_driver_install(&ethCfg, &eth);
  if (err != ESP_OK) return detail::fail("driver_install", err);

  // W5500 has no MAC of its own: use this chip's factory Ethernet MAC.
  uint8_t macAddr[6];
  esp_read_mac(macAddr, ESP_MAC_ETH);
  esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, macAddr);

  // Stock ETH_DEF netif with a higher route priority than Wi-Fi STA (100).
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.route_prio = ETH_ROUTE_PRIO;
  esp_netif_config_t nc = {};
  nc.base = &base;
  nc.driver = nullptr;
  nc.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  detail::netif() = esp_netif_new(&nc);
  if (!detail::netif()) return detail::fail("esp_netif_new", ESP_ERR_NO_MEM);
  err = esp_netif_attach(detail::netif(), esp_eth_new_netif_glue(eth));
  if (err != ESP_OK) return detail::fail("netif_attach", err);

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &detail::onEthEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &detail::onIpEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &detail::onIpEvent, nullptr);

  err = esp_eth_start(eth);
  if (err != ESP_OK) return detail::fail("eth_start", err);
  detail::started() = true;
  Serial.printf("[ETH] W5500 started (MAC %02X:%02X:%02X:%02X:%02X:%02X), waiting for cable/DHCP\n",
                macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  return true;
}

}  // namespace EthLink

#else   // !ETH_ENABLE -- Wi-Fi-only boards: everything compiles away.
#include <Arduino.h>
namespace EthLink {
inline bool begin() { return false; }
inline bool up() { return false; }
inline bool started() { return false; }
inline bool linkUp() { return false; }
inline String ipString() { return String(""); }
}  // namespace EthLink
#endif
