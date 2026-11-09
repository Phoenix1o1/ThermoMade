#include "touch.h"
#include <Arduino.h>
#include "base_config.h"

namespace {
TouchCal g_cal;
constexpr uint16_t kZMin = 300;   // pressure threshold

uint16_t xfer12(uint8_t cmd) {
    // send 8-bit command, then clock in 16 bits; the 12-bit result is left-aligned (bits 14..3)
    for (int i = 7; i >= 0; --i) {
        digitalWrite(cfg::kPinTouchMosi, (cmd >> i) & 1);
        digitalWrite(cfg::kPinTouchClk, HIGH);
        delayMicroseconds(1);
        digitalWrite(cfg::kPinTouchClk, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(cfg::kPinTouchMosi, LOW);
    uint16_t v = 0;
    for (int i = 0; i < 16; ++i) {
        digitalWrite(cfg::kPinTouchClk, HIGH);
        delayMicroseconds(1);
        v = static_cast<uint16_t>((v << 1) | (digitalRead(cfg::kPinTouchMiso) ? 1 : 0));
        digitalWrite(cfg::kPinTouchClk, LOW);
        delayMicroseconds(1);
    }
    return static_cast<uint16_t>((v >> 3) & 0x0FFF);
}

int16_t mapAxis(int32_t raw, int32_t lo, int32_t hi, int32_t size) {
    if (hi == lo) return 0;
    int32_t v = (raw - lo) * size / (hi - lo);
    if (v < 0) v = 0;
    if (v >= size) v = size - 1;
    return static_cast<int16_t>(v);
}
}  // namespace

namespace touch {

void begin(const TouchCal& cal) {
    g_cal = cal;
    pinMode(cfg::kPinTouchCs, OUTPUT);
    digitalWrite(cfg::kPinTouchCs, HIGH);
    pinMode(cfg::kPinTouchClk, OUTPUT);
    digitalWrite(cfg::kPinTouchClk, LOW);
    pinMode(cfg::kPinTouchMosi, OUTPUT);
    pinMode(cfg::kPinTouchMiso, INPUT);
    pinMode(cfg::kPinTouchIrq, INPUT);   // GPIO 36: input-only, pulled up on the board
}

void setCal(const TouchCal& cal) { g_cal = cal; }

TouchPoint read() {
    TouchPoint p;
    if (digitalRead(cfg::kPinTouchIrq) == HIGH) return p;   // not touched
    digitalWrite(cfg::kPinTouchCs, LOW);
    const uint16_t z1 = xfer12(0xB1);
    const uint16_t z2 = xfer12(0xC1);
    int32_t z = static_cast<int32_t>(z1) + 4095 - static_cast<int32_t>(z2);
    uint32_t sx = 0, sy = 0;
    xfer12(0x91);                                   // dummy (settle)
    for (int i = 0; i < 4; ++i) {
        sy += xfer12(0x91);
        sx += xfer12(0xD1);
    }
    xfer12(0xD0);                                   // power down, keep IRQ enabled
    digitalWrite(cfg::kPinTouchCs, HIGH);
    if (z < kZMin) return p;
    p.raw_x = static_cast<uint16_t>(sx / 4);
    p.raw_y = static_cast<uint16_t>(sy / 4);
    p.z = static_cast<uint16_t>(z);
    int32_t ax = g_cal.swap_xy ? p.raw_y : p.raw_x;
    int32_t ay = g_cal.swap_xy ? p.raw_x : p.raw_y;
    int16_t x = mapAxis(ax, g_cal.x_min, g_cal.x_max, 320);
    int16_t y = mapAxis(ay, g_cal.y_min, g_cal.y_max, 240);
    if (g_cal.invert_x) x = static_cast<int16_t>(319 - x);
    if (g_cal.invert_y) y = static_cast<int16_t>(239 - y);
    p.x = x;
    p.y = y;
    p.pressed = true;
    return p;
}

}  // namespace touch
