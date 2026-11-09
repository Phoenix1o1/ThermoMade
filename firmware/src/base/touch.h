// TermoMade base — XPT2046 resistive touch, bit-banged on its own pins (25/32/39/33, IRQ 36).
// Bit-banging keeps the VSPI peripheral free for the microSD card (the CYD wires both to VSPI
// pins by default). Touch is only sampled when the IRQ line says the panel is pressed.
#pragma once
#include <cstdint>
#include "settings.h"

struct TouchPoint {
    bool pressed = false;
    int16_t x = 0, y = 0;        // screen coordinates (landscape 320 x 240)
    uint16_t raw_x = 0, raw_y = 0, z = 0;
};

namespace touch {
void begin(const TouchCal& cal);
void setCal(const TouchCal& cal);
TouchPoint read();
}  // namespace touch
