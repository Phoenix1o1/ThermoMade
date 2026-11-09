// TermoMade base — wall time without internet.
// Sources (best last): node DS3231 (every telemetry), manual / phone (CLI now, app later).
// A worse source never overrides a better one set in the last 24 h. The validity survives
// software resets (watchdog, OTA) through RTC memory; a power cut needs the node RTC.
#pragma once
#include <cstdint>
#include "shared.h"
#include "tm_types.h"

namespace timekeeper {
void begin();
// Offer a time (UTC). Returns true if the clock was set.
bool offer(TimeSrc src, uint32_t unix_utc);
bool valid();
TimeSrc source();
uint32_t now();   // unix UTC, 0 if invalid
// Local wall time for the controller (Europe/Rome with DST).
tmc::WallTime wall(uint8_t* hour = nullptr, uint8_t* minute = nullptr);
}  // namespace timekeeper
