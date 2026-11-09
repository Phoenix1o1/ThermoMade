// TermoMade base — relay output. Energised = L on OPEN (valve opens), de-energised = L on CLOSE.
// Hardware must hold the module input at its INACTIVE level while the ESP32 boots:
//   active-HIGH module -> 10k pull-DOWN on IN;  active-LOW module -> 10k pull-UP on IN.
// begin() is the very first call in setup(); afterwards only the control task calls set().
#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include "base_config.h"

class Relay {
public:
    static void begin() {
        // Load the inactive level into the output register BEFORE enabling the driver: no glitch.
        // (Arduino digitalWrite() ignores pins not yet set up as GPIO in core 3.x, so use IDF.)
        gpio_set_level(static_cast<gpio_num_t>(cfg::kPinRelay), inactiveLevel());
        pinMode(cfg::kPinRelay, OUTPUT);
        digitalWrite(cfg::kPinRelay, inactiveLevel());
        on_ = false;
    }
    static void set(bool on) {
        if (on == on_) return;
        digitalWrite(cfg::kPinRelay, on ? activeLevel() : inactiveLevel());
        on_ = on;
    }
    static bool isOn() { return on_; }

private:
    static constexpr uint8_t activeLevel() { return TM_RELAY_ACTIVE_HIGH ? HIGH : LOW; }
    static constexpr uint8_t inactiveLevel() { return TM_RELAY_ACTIVE_HIGH ? LOW : HIGH; }
    static inline bool on_ = false;
};
