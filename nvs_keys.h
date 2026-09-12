// ============================================================================
// nvs_keys.h — the NVS key-name registry + compile-time key-length guard
// ----------------------------------------------------------------------------
// WHY THIS EXISTS (Sprint 4B physical bring-up finding, bench board
// E8:F6:0A:B8:B7:BC, first boot of the esp32dev-mikiwire image):
//
//   [E][Preferences.cpp:202] putUInt(): nvs_set_u32 fail: unhealthy_streak KEY_TOO_LONG
//   [BOOT] running unconfirmed image 1.0.0 (sec=1) - unhealthy boot streak=0/3
//
// ESP-IDF NVS limits a key name to NVS_KEY_NAME_MAX_SIZE (16) INCLUDING the
// terminator, i.e. 15 characters. "unhealthy_streak" is 16. Every write of
// that key failed, the P1 unhealthy-boot rollback safety net therefore read
// 0 on every boot, and UNHEALTHY_BOOT_STREAK_LIMIT could never be reached --
// the whole feature was silently dead on real hardware while its own native
// tests (boot_health.h has no NVS in it) stayed green. Preferences.cpp only
// logs the error and returns 0; nothing in the firmware checked the return.
//
// The rule that prevents a recurrence cannot live in store.h (it includes
// Preferences.h/Arduino.h, so it is not host-compilable), so every key name
// is declared HERE, dependency-free, and checked at COMPILE TIME against the
// limit below. store.h uses these macros instead of raw string literals;
// test/native_cpp/test_nvs_keys.cpp compiles this header on the host and
// re-checks the same table at runtime (length, namespace length, uniqueness
// within a namespace), so a future key added with a raw literal in store.h
// is the only way to bypass the guard -- and that is what code review is for.
//
// The namespaces themselves come from config.h (NVS_NS / NVS_NS_SECURITY)
// and are subject to the same 15-character limit (NVS_NS_NAME_MAX_SIZE).
// ============================================================================
#pragma once
#include "config.h"

// NVS_KEY_NAME_MAX_SIZE - 1 (nvs.h) -- restated here so this header stays
// free of any ESP-IDF include and compiles on the host.
#define NVS_KEY_MAX_LEN 15
// For a string LITERAL only: sizeof includes the terminator.
#define NVS_KEY_FITS(lit) ((sizeof(lit) - 1) <= NVS_KEY_MAX_LEN)

// ---- "covio" namespace (NVS_NS): ordinary config + operational counters ----
// Wiped together by Store::factoryReset().
#define NVS_KEY_SEEDED          "seeded"
#define NVS_KEY_SERVER_URL      "server_url"
#define NVS_KEY_API_KEY         "api_key"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_CFG_VER         "cfg_ver"
#define NVS_KEY_BOOT_ID         "boot_id"
#define NVS_KEY_RESTART_CNT     "restart_cnt"
#define NVS_KEY_LOGICAL_ID      "logical_id"
#define NVS_KEY_KFACTOR         "kfactor"
#define NVS_KEY_DENSITY         "density"
#define NVS_KEY_TREF            "tref"
#define NVS_KEY_WDT_CNT         "wdt_cnt"
#define NVS_KEY_BOD_CNT         "bod_cnt"
#define NVS_KEY_PUSHFAIL_CNT    "pushfail_cnt"
#define NVS_KEY_WIFIRECON_CNT   "wifirecon_cnt"
#define NVS_KEY_CRASH_STREAK    "crash_streak"
// Miki Wire profile tunables (only written by a MIKI_WIRE_PROFILE build;
// declared unconditionally so the registry below is the same on every host
// compile).
#define NVS_KEY_MW_MAX_HZ       "mw_max_hz"
#define NVS_KEY_MW_SUSPECT_S    "mw_suspect_s"

// ---- "covio_sec" namespace (NVS_NS_SECURITY): survives factoryReset() ------
#define NVS_KEY_SEC_VER         "sec_ver"
#define NVS_KEY_LAST_OK_VER     "last_ok_ver"
#define NVS_KEY_LAST_OK_SECVER  "last_ok_secver"
// Renamed from "unhealthy_streak" (16 chars, never writable -- see header
// comment). No migration needed: the old key never existed on any device
// because every write of it was rejected.
#define NVS_KEY_UNHEALTHY_STRK  "unhealthy_strk"

// ---- Compile-time guard: every key above, plus both namespaces ------------
static_assert(NVS_KEY_FITS(NVS_NS),                 "NVS namespace name exceeds 15 chars");
static_assert(NVS_KEY_FITS(NVS_NS_SECURITY),        "NVS security namespace name exceeds 15 chars");
static_assert(NVS_KEY_FITS(NVS_KEY_SEEDED),         "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_SERVER_URL),     "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_API_KEY),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_WIFI_SSID),      "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_WIFI_PASS),      "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_CFG_VER),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_BOOT_ID),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_RESTART_CNT),    "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_LOGICAL_ID),     "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_KFACTOR),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_DENSITY),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_TREF),           "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_WDT_CNT),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_BOD_CNT),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_PUSHFAIL_CNT),   "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_WIFIRECON_CNT),  "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_CRASH_STREAK),   "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_MW_MAX_HZ),      "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_MW_SUSPECT_S),   "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_SEC_VER),        "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_LAST_OK_VER),    "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_LAST_OK_SECVER), "NVS key too long");
static_assert(NVS_KEY_FITS(NVS_KEY_UNHEALTHY_STRK), "NVS key too long");

// ---- Runtime-inspectable registry (for test_nvs_keys.cpp) -----------------
// Kept in the same header so the table and the macros cannot drift apart
// silently: a key added above without a row here shows up in review as a
// one-sided edit of this file.
struct NvsKeyEntry { const char* ns; const char* key; };
static const NvsKeyEntry kNvsKeyRegistry[] = {
  { NVS_NS,          NVS_KEY_SEEDED         },
  { NVS_NS,          NVS_KEY_SERVER_URL     },
  { NVS_NS,          NVS_KEY_API_KEY        },
  { NVS_NS,          NVS_KEY_WIFI_SSID      },
  { NVS_NS,          NVS_KEY_WIFI_PASS      },
  { NVS_NS,          NVS_KEY_CFG_VER        },
  { NVS_NS,          NVS_KEY_BOOT_ID        },
  { NVS_NS,          NVS_KEY_RESTART_CNT    },
  { NVS_NS,          NVS_KEY_LOGICAL_ID     },
  { NVS_NS,          NVS_KEY_KFACTOR        },
  { NVS_NS,          NVS_KEY_DENSITY        },
  { NVS_NS,          NVS_KEY_TREF           },
  { NVS_NS,          NVS_KEY_WDT_CNT        },
  { NVS_NS,          NVS_KEY_BOD_CNT        },
  { NVS_NS,          NVS_KEY_PUSHFAIL_CNT   },
  { NVS_NS,          NVS_KEY_WIFIRECON_CNT  },
  { NVS_NS,          NVS_KEY_CRASH_STREAK   },
  { NVS_NS,          NVS_KEY_MW_MAX_HZ      },
  { NVS_NS,          NVS_KEY_MW_SUSPECT_S   },
  { NVS_NS_SECURITY, NVS_KEY_SEC_VER        },
  { NVS_NS_SECURITY, NVS_KEY_LAST_OK_VER    },
  { NVS_NS_SECURITY, NVS_KEY_LAST_OK_SECVER },
  { NVS_NS_SECURITY, NVS_KEY_UNHEALTHY_STRK },
};
static const unsigned kNvsKeyRegistryCount = sizeof(kNvsKeyRegistry) / sizeof(kNvsKeyRegistry[0]);
