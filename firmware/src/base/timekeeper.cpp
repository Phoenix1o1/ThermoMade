#include "timekeeper.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>
#include "base_config.h"
#include "tm_util.h"

namespace {
constexpr uint32_t kRtcMagic = 0x54494D45;   // "TIME"
RTC_NOINIT_ATTR uint32_t g_rtc_magic;
RTC_NOINIT_ATTR uint8_t g_rtc_src;
RTC_NOINIT_ATTR uint32_t g_rtc_set_unix;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_valid = false;
volatile TimeSrc g_src = TimeSrc::None;
volatile uint32_t g_set_unix = 0;     // when the current source last set the clock
}  // namespace

namespace timekeeper {

void begin() {
    setenv("TZ", cfg::kTz, 1);
    tzset();
    const esp_reset_reason_t rr = esp_reset_reason();
    const bool soft = rr == ESP_RST_SW || rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
                      rr == ESP_RST_WDT;
    const uint32_t t = static_cast<uint32_t>(time(nullptr));
    if (soft && g_rtc_magic == kRtcMagic && tmu::plausibleUnix(t) && g_rtc_src <= 2) {
        g_valid = true;
        g_src = static_cast<TimeSrc>(g_rtc_src);
        g_set_unix = g_rtc_set_unix;
        Serial.printf("[time] kept across reset: %lu (src %u)\n", static_cast<unsigned long>(t), g_rtc_src);
    } else {
        g_rtc_magic = 0;
    }
}

bool offer(TimeSrc src, uint32_t unix_utc) {
    if (!tmu::plausibleUnix(unix_utc) || src == TimeSrc::None) return false;
    const uint32_t cur = static_cast<uint32_t>(time(nullptr));
    bool apply = false;
    portENTER_CRITICAL(&g_mux);
    if (!g_valid) apply = true;
    else if (src > g_src) apply = true;
    else if (src == g_src) apply = (unix_utc > cur + 2) || (unix_utc + 2 < cur);
    else apply = cur > g_set_unix + 86400u;   // worse source only if the better one is > 24 h old
    portEXIT_CRITICAL(&g_mux);
    if (!apply) return false;
    if (src < TimeSrc::Manual && g_valid) {
        const int64_t diff = static_cast<int64_t>(unix_utc) - cur;
        if (diff >= -2 && diff <= 2) return false;   // already in agreement
    }
    timeval tv = {static_cast<time_t>(unix_utc), 0};
    settimeofday(&tv, nullptr);
    portENTER_CRITICAL(&g_mux);
    g_valid = true;
    g_src = src;
    g_set_unix = unix_utc;
    portEXIT_CRITICAL(&g_mux);
    g_rtc_magic = kRtcMagic;
    g_rtc_src = static_cast<uint8_t>(src);
    g_rtc_set_unix = unix_utc;
    Serial.printf("[time] set to %lu from %s\n", static_cast<unsigned long>(unix_utc),
                  src == TimeSrc::Manual ? "phone/manual" : "node RTC");
    return true;
}

bool valid() { return g_valid; }
TimeSrc source() { return g_src; }
uint32_t now() { return g_valid ? static_cast<uint32_t>(time(nullptr)) : 0; }

tmc::WallTime wall(uint8_t* hour, uint8_t* minute) {
    tmc::WallTime w;
    if (!g_valid) return w;
    const time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    w.valid = true;
    w.wday = static_cast<uint8_t>((lt.tm_wday + 6) % 7);   // tm: 0 = Sunday -> 0 = Monday
    w.minute = static_cast<uint16_t>(lt.tm_hour * 60 + lt.tm_min);
    w.day_number = static_cast<uint32_t>(tmu::daysFromCivil(lt.tm_year + 1900, static_cast<unsigned>(lt.tm_mon + 1),
                                                            static_cast<unsigned>(lt.tm_mday)));
    if (hour) *hour = static_cast<uint8_t>(lt.tm_hour);
    if (minute) *minute = static_cast<uint8_t>(lt.tm_min);
    return w;
}

}  // namespace timekeeper
