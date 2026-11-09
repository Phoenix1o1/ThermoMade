// TermoMade base — hidden settings pages (long-press the clock for 2 s on the Home screen).
//
// 8 pages, browsed with < > in the header, X returns to Home:
//   STATO · DATA E ORA · TEMPERATURE · ORARIO · SENSORI · SCHERMO · AVANZATE · SISTEMA
// Every change goes to the control task as a Request (validated and clamped there, saved to
// NVS 10 s after the last change). Destructive actions need a second tap within 3 s.
// Reference of every field: docs/07-user-interface.md "Hidden settings".
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "shared.h"

namespace menu {
void open(lgfx::LGFX_Device* lcd, const Snapshot& s);
void close();
bool isOpen();
void draw(const Snapshot& s, bool full);                        // every ~250 ms while open
void press(int16_t x, int16_t y, bool repeat, const Snapshot& s);  // repeat = held (auto-repeat)
bool repeats(int16_t x, int16_t y);                             // point is on an auto-repeat button
}  // namespace menu
