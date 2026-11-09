// TermoMade base — room light from the CYD's own LDR (GPIO 34, front side, left of the display).
//
// The stock LDR circuit has a small range and sees the display backlight, so:
//  * it is sampled only while the backlight has been OFF for at least 1 s (no self-glare);
//    while the screen is on, the last dark-screen value is kept
//  * the range is learned automatically: the brightest and darkest readings of the last days
//    (slowly forgotten) map to 100 % and 0 %. No calibration needed.
// Uses: auto-brightness when the screen wakes, the light column in the log, diagnostics.
// Coarse (dark / dim / lit / sunny), not lux. For real lux add a BH1750 to the room node.
#pragma once
#include <cstdint>

namespace light {
void begin();
// Call from loop() (UI task) about every 15 ms; samples every 2 s when allowed.
void poll(uint32_t now_ms, bool backlight_off_for_1s);
int8_t pct();          // 0..100, -1 = not known yet (range not learned)
uint16_t raw();        // last dark-screen reading (filtered), 0..4095, higher = darker
uint16_t rawLive();    // last reading whatever the backlight state
uint16_t rangeBright();
uint16_t rangeDark();
}  // namespace light
