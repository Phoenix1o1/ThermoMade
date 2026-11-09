#include "light.h"
#include <Arduino.h>
#include <atomic>
#include "base_config.h"

namespace {
constexpr uint32_t kSampleMs = 2000;
constexpr float kAlpha = 0.3f;              // EMA per sample
constexpr float kForget = (kSampleMs / 1000.0f) / (3.0f * 86400.0f);   // per sample: ~3 days to forget an extreme
constexpr float kMinSpan = 300.0f;          // ADC counts: below this the range is not trusted

uint32_t g_last_ms = 0;
bool g_have = false;
float g_ema = 0.0f;
float g_lo = 0.0f;    // brightest seen (lowest reading)
float g_hi = 0.0f;    // darkest seen (highest reading)
std::atomic<int8_t> g_pct{-1};
std::atomic<uint16_t> g_raw{0};
std::atomic<uint16_t> g_live{0};

uint16_t readRaw() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) sum += static_cast<uint32_t>(analogRead(cfg::kPinLdr));
    return static_cast<uint16_t>(sum / 8);
}
}  // namespace

namespace light {

void begin() {
    pinMode(cfg::kPinLdr, INPUT);
    analogSetPinAttenuation(cfg::kPinLdr, ADC_0db);   // ~0-1 V: more sensitivity for this weak divider
}

void poll(uint32_t now_ms, bool backlight_off_for_1s) {
    if (now_ms - g_last_ms < kSampleMs) return;
    g_last_ms = now_ms;
    const uint16_t r = readRaw();
    g_live = r;
    if (!backlight_off_for_1s) return;          // the backlight would light up the LDR
    const float v = static_cast<float>(r);
    if (!g_have) {
        g_have = true;
        g_ema = g_lo = g_hi = v;
    } else {
        g_ema += kAlpha * (v - g_ema);
    }
    // auto-range: extremes jump out at once and creep back slowly
    if (g_ema < g_lo) g_lo = g_ema;
    else g_lo += kForget * (g_ema - g_lo);
    if (g_ema > g_hi) g_hi = g_ema;
    else g_hi += kForget * (g_ema - g_hi);
    g_raw = static_cast<uint16_t>(g_ema + 0.5f);
    const float span = g_hi - g_lo;
    if (span < kMinSpan) {
        g_pct = -1;
        return;
    }
    float p = (g_hi - g_ema) / span * 100.0f;    // high reading = dark
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_pct = static_cast<int8_t>(p + 0.5f);
}

int8_t pct() { return g_pct; }
uint16_t raw() { return g_raw; }
uint16_t rawLive() { return g_live; }
uint16_t rangeBright() { return static_cast<uint16_t>(g_lo); }
uint16_t rangeDark() { return static_cast<uint16_t>(g_hi); }

}  // namespace light
