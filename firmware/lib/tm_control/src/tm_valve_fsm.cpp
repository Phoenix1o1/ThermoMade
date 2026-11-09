#include "tm_valve_fsm.h"

namespace tmc {

namespace {
constexpr uint32_t kSatMax = kForeverMs - 1;  // elapsed never reaches "forever"

uint32_t satAdd(uint32_t a, uint32_t b) { return (a > kSatMax - b) ? kSatMax : a + b; }
uint32_t maxU(uint32_t a, uint32_t b) { return a > b ? a : b; }
}  // namespace

void ValveFsm::begin(uint32_t now_ms, bool start_unlocked) {
    started_ = true;
    open_ = false;
    last_now_ = now_ms;
    elapsed_ms_ = start_unlocked ? kSatMax : 0;   // 0 = lockout: OFF_MIN must elapse first
    uptime_ms_ = 0;
    open_ms_total_ = 0;
    opens_total_ = 0;
    hist_n_ = hist_i_ = 0;
    reason_ = ValveReason::Boot;
    next_action_ms_ = start_unlocked ? kForeverMs : static_cast<uint32_t>(p_->off_min_s) * 1000u;
}

void ValveFsm::advance(uint32_t now_ms) {
    if (!started_) begin(now_ms);
    const uint32_t dt = now_ms - last_now_;          // unsigned: rollover-safe
    last_now_ = now_ms;
    elapsed_ms_ = satAdd(elapsed_ms_, dt);
    uptime_ms_ += dt;
    if (open_) open_ms_total_ += dt;
}

bool ValveFsm::cycleLimitReached() const {
    const uint8_t limit = p_->max_cycles_per_hour;
    if (limit == 0) return false;
    int n = 0;
    for (int k = 0; k < hist_n_; ++k) {
        if (uptime_ms_ - open_hist_[k] < 3600000ULL) ++n;
    }
    return n >= limit;
}

uint32_t ValveFsm::lockoutRemainingMs() const {
    if (open_) return 0;
    const uint32_t off_min = p_->off_min_s * 1000u;
    return elapsed_ms_ >= off_min ? 0 : off_min - elapsed_ms_;
}

bool ValveFsm::step(uint32_t now_ms, const ValveRequest& rq) {
    advance(now_ms);
    const uint32_t on_min = p_->on_min_s * 1000u;
    const uint32_t off_min = p_->off_min_s * 1000u;

    if (open_) {
        if (rq.force_close) {
            open_ = false;
            elapsed_ms_ = 0;
            reason_ = rq.close_reason;
        } else {
            const uint32_t need = rq.demand ? maxU(on_min, rq.on_target_ms) : on_min;
            if (elapsed_ms_ >= need) {
                open_ = false;
                elapsed_ms_ = 0;
                reason_ = rq.close_reason;
            } else {
                next_action_ms_ = (need >= kSatMax) ? kForeverMs : need - elapsed_ms_;
                return true;
            }
        }
        // just closed: lockout starts now
        next_action_ms_ = rq.demand && !rq.force_close ? maxU(off_min, rq.off_target_ms) : kForeverMs;
        if (next_action_ms_ != kForeverMs && next_action_ms_ >= kSatMax) next_action_ms_ = kForeverMs;
        return false;
    }

    // closed
    if (rq.force_close || !rq.demand) {
        reason_ = rq.close_reason;
        next_action_ms_ = kForeverMs;
        return false;
    }
    const uint32_t need = maxU(off_min, rq.off_target_ms);
    if (elapsed_ms_ >= need) {
        if (cycleLimitReached()) {
            reason_ = ValveReason::CycleLimit;
            next_action_ms_ = 60000;   // re-evaluated every step anyway
            return false;
        }
        open_ = true;
        elapsed_ms_ = 0;
        ++opens_total_;
        open_hist_[hist_i_] = uptime_ms_;
        hist_i_ = (hist_i_ + 1) % kHist;
        if (hist_n_ < kHist) ++hist_n_;
        reason_ = rq.open_reason;
        const uint32_t on_need = maxU(on_min, rq.on_target_ms);
        next_action_ms_ = on_need >= kSatMax ? kForeverMs : on_need;
        return true;
    }
    next_action_ms_ = (need >= kSatMax) ? kForeverMs : need - elapsed_ms_;
    return false;
}

}  // namespace tmc
