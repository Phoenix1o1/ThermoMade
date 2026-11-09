// TermoMade base — display + touch UI (LovyanGFX, drawn directly; no LVGL in the MVP).
// Parent-facing text is Italian (7-bit fonts: no accented letters on screen).
// Screen goes fully dark after the timeout; the first touch only wakes it.
// Display settings and touch calibration are read from the Snapshot (owned by the control task).
#pragma once
#include <cstdint>
#include "settings.h"

namespace ui {
void begin(const Settings& s);
void colorTest();                    // boot: red / green / blue bars to verify panel flags
void loop();                         // call from Arduino loop()
// Last time the screen was on (for the CYD-sensor self-heating flag).
uint32_t lastScreenOnMs();
bool screenOn();
void setRawTouchDump(bool on);       // CLI "touch": print raw coordinates
}  // namespace ui
