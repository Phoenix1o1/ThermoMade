// TermoMade base — DS18B20 on GPIO 27 (CYD sensor chamber). Non-blocking:
// request a conversion, collect it 800 ms later on the next poll. Rejects CRC errors,
// "disconnected" (-127) and the power-on value 85.0 that appears after a brown-out.
#pragma once
#include <cstdint>

namespace temp_local {
void begin();
// Call every second from the control task. Returns true when a new result is available
// (ok=false on a failed read).
bool poll(uint32_t now_ms, bool* ok, double* t_c);
bool present();
}  // namespace temp_local
