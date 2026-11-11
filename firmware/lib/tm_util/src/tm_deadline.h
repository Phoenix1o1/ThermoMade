// One-shot timeout that cannot "come back". The usual (int32_t)(until - now) > 0 test turns true
// again 24.9 days after the deadline (uint32 wrap); this keeps an explicit flag instead. Poll
// active() regularly (at least once every 24 days) or clear() it when its context ends.
#pragma once
#include <cstdint>

namespace tmu {
struct Deadline {
    uint32_t until_ms = 0;
    bool on = false;
    void arm(uint32_t now_ms, uint32_t duration_ms) { until_ms = now_ms + duration_ms; on = true; }
    void clear() { on = false; }
    bool active(uint32_t now_ms) {
        if (on && static_cast<int32_t>(until_ms - now_ms) <= 0) on = false;
        return on;
    }
};
}  // namespace tmu
