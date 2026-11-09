#include "temp_local.h"
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include "base_config.h"

namespace {
OneWire g_ow(cfg::kPinOneWire);
DallasTemperature g_ds(&g_ow);
DeviceAddress g_addr;
bool g_present = false;
bool g_pending = false;
uint32_t g_req_ms = 0;
uint32_t g_last_ms = 0;
bool g_started = false;

bool findSensor() {
    g_ds.begin();
    g_present = g_ds.getDeviceCount() > 0 && g_ds.getAddress(g_addr, 0);
    if (g_present) {
        g_ds.setResolution(g_addr, 12);
        g_ds.setWaitForConversion(false);   // never block the control task
    }
    return g_present;
}
}  // namespace

namespace temp_local {

void begin() {
    findSensor();
    Serial.printf("[ds18b20] %s on GPIO %d\n", g_present ? "found" : "NOT found", cfg::kPinOneWire);
}

bool present() { return g_present; }

bool poll(uint32_t now_ms, bool* ok, double* t_c) {
    if (!g_started) {
        g_started = true;
        g_last_ms = now_ms - cfg::kDs18b20PeriodMs;   // first reading right away
    }
    if (!g_pending) {
        if (now_ms - g_last_ms < cfg::kDs18b20PeriodMs) return false;
        g_last_ms = now_ms;
        if (!g_present && !findSensor()) {   // hot-plug / recovery
            *ok = false;
            return true;
        }
        g_ds.requestTemperaturesByAddress(g_addr);
        g_req_ms = now_ms;
        g_pending = true;
        return false;
    }
    if (now_ms - g_req_ms < 800) return false;   // 12-bit conversion: 750 ms max
    g_pending = false;
    const float t = g_ds.getTempC(g_addr);       // CRC-checked; DEVICE_DISCONNECTED_C on error
    const bool good = t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f && t != 85.0f;
    if (!good) g_present = false;                // re-detect next time
    *ok = good;
    *t_c = good ? static_cast<double>(t) : 0.0;
    return true;
}

}  // namespace temp_local
