// TermoMade control library — weekly Comfort/Night schedule.
#pragma once
#include <cstdint>
#include "tm_types.h"

namespace tmc {

struct Slot {
    uint16_t start_min;   // minute of the day the period starts (0..1439)
    Period period;
};

struct DaySchedule {
    static constexpr int kMaxSlots = 4;
    uint8_t n = 0;
    Slot slot[kMaxSlots] = {};
};

struct WeekSchedule {
    DaySchedule day[7];   // 0 = Monday

    // Default: Comfort 06:30-22:30, Night otherwise, every day.
    static WeekSchedule makeDefault();

    // Sort slots, drop invalid ones, ensure every day has at least one slot. Returns true if changed.
    bool sanitize();

    // Period active at (wday, minute). Looks back into previous days when needed.
    Period periodAt(uint8_t wday, uint16_t minute) const;

    // Minutes from (wday, minute) until the next slot boundary whose period differs from the
    // current one. Returns -1 if the period never changes. *next is set to that period.
    int32_t minutesToNextChange(uint8_t wday, uint16_t minute, Period* next) const;
};

}  // namespace tmc
