// TermoMade base station (CYD ESP32-2432S028, 2-USB variant) — pins and constants.
#pragma once
#include <cstdint>

#define TM_FW_MAJOR 0
#define TM_FW_MINOR 3

#ifndef TM_RELAY_ACTIVE_HIGH
#error "Set -D TM_RELAY_ACTIVE_HIGH=1 (active-HIGH module) or 0 (active-LOW module) in platformio.ini"
#endif

namespace cfg {
// --- free CYD pins ---
constexpr int kPinRelay = 22;        // P3/CN1. ONLY the control task writes it (via Relay)
constexpr int kPinOneWire = 27;      // CN1. DS18B20 (+4.7k pull-up to 3.3 V)
// --- display (HSPI) ---
constexpr int kPinLcdSclk = 14, kPinLcdMosi = 13, kPinLcdMiso = 12, kPinLcdDc = 2, kPinLcdCs = 15, kPinLcdBl = 21;
// --- touch XPT2046 (bit-banged, keeps VSPI free for the SD card) ---
constexpr int kPinTouchClk = 25, kPinTouchMosi = 32, kPinTouchMiso = 39, kPinTouchCs = 33, kPinTouchIrq = 36;
// --- microSD (VSPI) ---
constexpr int kPinSdSck = 18, kPinSdMiso = 19, kPinSdMosi = 23, kPinSdCs = 5;
// --- misc ---
constexpr int kPinLdr = 34;
constexpr int kPinSpeaker = 26;     // audio amp input: driven LOW so the speaker stays silent (no hum)
constexpr int kPinLedR = 4, kPinLedG = 16, kPinLedB = 17;   // RGB LED on the back, active LOW: kept off

// --- timing ---
constexpr uint32_t kControlPeriodMs = 1000;
constexpr uint32_t kTaskWdtMs = 10000;          // control task must check in every 10 s
constexpr uint32_t kDs18b20PeriodMs = 30000;
constexpr uint32_t kPersistEveryMs = 30u * 60u * 1000u;   // learned I + offset
constexpr uint32_t kSettingsDebounceMs = 10000;
constexpr uint32_t kLogEveryMs = 60000;
constexpr uint32_t kPairWindowMs = 60000;
constexpr uint32_t kScreenTimeoutDefaultS = 30;
constexpr uint32_t kScreenDisturbHoldMs = 5u * 60u * 1000u;  // CYD sensor self-heating after screen use

// --- radio ---
constexpr uint8_t kApChannel = 1;
constexpr int kMaxNodes = 8;
constexpr uint8_t kReportS = 30;

// --- time ---
constexpr const char* kTz = "CET-1CEST,M3.5.0,M10.5.0/3";   // Europe/Rome
}  // namespace cfg
