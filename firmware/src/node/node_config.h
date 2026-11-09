// TermoMade room node — build configuration (ESP32 DevKit / WROOM-32).
#pragma once
#include <cstdint>

#define TM_FW_MAJOR 0
#define TM_FW_MINOR 1

#if !defined(TM_NODE_SENSOR_SHT4X) && !defined(TM_NODE_SENSOR_DHT22)
#error "Select a room sensor: -D TM_NODE_SENSOR_SHT4X=1 or -D TM_NODE_SENSOR_DHT22=1 (see platformio.ini)"
#endif
#if defined(TM_NODE_SENSOR_SHT4X) && defined(TM_NODE_SENSOR_DHT22)
#error "Select only one room sensor"
#endif

namespace cfg {
constexpr int kPinSda = 21;          // I2C: SHT4x (0x44) + DS3231 (0x68)
constexpr int kPinScl = 22;
constexpr uint32_t kI2cHz = 100000;
constexpr int kPinLed = 2;           // on-board LED (active HIGH on most DevKits)
constexpr int kPinBoot = 0;          // BOOT button: hold 3 s = forget pairing
#ifdef TM_DHT_PIN
constexpr int kPinDht = TM_DHT_PIN;
#else
constexpr int kPinDht = 4;
#endif
constexpr uint32_t kReportDefaultS = 30;   // telemetry period (the base may change it)
constexpr int kSendRetries = 3;            // per frame
constexpr uint32_t kSendTimeoutMs = 120;   // wait for the MAC-level ACK callback
constexpr uint32_t kAckWaitMs = 300;       // wait for the application ACK
constexpr int kFailsBeforeRescan = 3;      // consecutive failed reports -> channel scan
constexpr uint32_t kHelloPeriodMs = 3000;  // unpaired: one HELLO sweep every 3 s
constexpr uint32_t kForgetHoldMs = 3000;
constexpr uint8_t kCpuMhz = 80;            // less self-heating, plenty for this job
}  // namespace cfg
