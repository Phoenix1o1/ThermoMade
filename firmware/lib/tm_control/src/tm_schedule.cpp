#include "tm_schedule.h"

namespace tmc {

WeekSchedule WeekSchedule::makeDefault() {
    WeekSchedule w;
    for (auto& d : w.day) {
        d.n = 2;
        d.slot[0] = {6 * 60 + 30, Period::Comfort};
        d.slot[1] = {22 * 60 + 30, Period::Night};
    }
    return w;
}

bool WeekSchedule::sanitize() {
    bool ch = false;
    for (auto& d : day) {
        if (d.n > DaySchedule::kMaxSlots) { d.n = DaySchedule::kMaxSlots; ch = true; }
        // drop invalid entries
        uint8_t k = 0;
        for (uint8_t i = 0; i < d.n; ++i) {
            const Slot s = d.slot[i];
            if (s.start_min < 1440 && static_cast<uint8_t>(s.period) <= 1) d.slot[k++] = s;
            else ch = true;
        }
        d.n = k;
        // insertion sort by start time
        for (uint8_t i = 1; i < d.n; ++i) {
            Slot s = d.slot[i];
            int j = i - 1;
            while (j >= 0 && d.slot[j].start_min > s.start_min) { d.slot[j + 1] = d.slot[j]; --j; ch = true; }
            d.slot[j + 1] = s;
        }
        if (d.n == 0) {   // never leave a day empty
            d.n = 2;
            d.slot[0] = {6 * 60 + 30, Period::Comfort};
            d.slot[1] = {22 * 60 + 30, Period::Night};
            ch = true;
        }
    }
    return ch;
}

Period WeekSchedule::periodAt(uint8_t wday, uint16_t minute) const {
    wday %= 7;
    const DaySchedule& d = day[wday];
    for (int i = d.n - 1; i >= 0; --i)
        if (d.slot[i].start_min <= minute) return d.slot[i].period;
    // before the first slot of today: last slot of a previous day
    for (int back = 1; back <= 7; ++back) {
        const DaySchedule& pd = day[(wday + 7 - back) % 7];
        if (pd.n > 0) return pd.slot[pd.n - 1].period;
    }
    return Period::Comfort;
}

int32_t WeekSchedule::minutesToNextChange(uint8_t wday, uint16_t minute, Period* next) const {
    wday %= 7;
    const Period cur = periodAt(wday, minute);
    for (int dd = 0; dd <= 7; ++dd) {
        const DaySchedule& d = day[(wday + dd) % 7];
        for (int i = 0; i < d.n; ++i) {
            const int32_t abs_min = dd * 1440 + d.slot[i].start_min;
            if (abs_min <= minute) continue;            // already passed today
            if (d.slot[i].period != cur) {
                if (next) *next = d.slot[i].period;
                return abs_min - minute;
            }
        }
    }
    return -1;
}

}  // namespace tmc
