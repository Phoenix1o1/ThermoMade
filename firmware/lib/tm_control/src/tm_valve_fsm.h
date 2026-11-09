// TermoMade control library — valve state machine. THE ONLY place that decides the relay state.
// Hard rules (CLAUDE.md):
//   * OFF_MIN is absolute: once closed, the valve cannot reopen before OFF_MIN, whatever happens
//     (boot starts in lockout too).
//   * ON_MIN applies to every automatic close; only force_close (user OFF, fault, plant off)
//     closes earlier.
// Time handling: elapsed times are accumulated from wrap-safe deltas and saturate, so a
// millis() rollover (49.7 days) or a very long state never causes a wrong decision.
#pragma once
#include <cstdint>
#include "tm_params.h"
#include "tm_types.h"

namespace tmc {

struct ValveRequest {
    bool demand = false;         // there is heating demand (u >= U_MIN, or classic call)
    uint32_t on_target_ms = 0;   // desired ON duration (kForeverMs = stay open)
    uint32_t off_target_ms = kForeverMs;  // desired OFF duration
    bool force_close = false;    // close now, even before ON_MIN
    ValveReason open_reason = ValveReason::Pulse;
    ValveReason close_reason = ValveReason::NoDemand;
};

class ValveFsm {
public:
    explicit ValveFsm(const ControlParams& p) : p_(&p) {}
    void setParams(const ControlParams& p) { p_ = &p; }

    // Power-up: valve closed, lockout running (unless start_unlocked, used by the golden test
    // to mirror the simulator, which starts "closed for a long time").
    void begin(uint32_t now_ms, bool start_unlocked = false);

    // Advance time and apply the request. Returns true if the relay must be energised (OPEN).
    bool step(uint32_t now_ms, const ValveRequest& rq);

    bool isOpen() const { return open_; }
    uint32_t elapsedMs() const { return elapsed_ms_; }
    // Remaining lockout before the valve may reopen (0 if open or free).
    uint32_t lockoutRemainingMs() const;
    // Time until the next planned transition given the last request (kForeverMs if none).
    uint32_t nextActionMs() const { return next_action_ms_; }
    ValveReason reason() const { return reason_; }
    uint32_t opensTotal() const { return opens_total_; }
    uint64_t openMsTotal() const { return open_ms_total_; }
    uint64_t uptimeMs() const { return uptime_ms_; }

private:
    bool cycleLimitReached() const;
    void advance(uint32_t now_ms);

    const ControlParams* p_;
    bool started_ = false;
    bool open_ = false;
    uint32_t last_now_ = 0;
    uint32_t elapsed_ms_ = 0;          // time in the current state (saturating)
    uint64_t uptime_ms_ = 0;
    uint64_t open_ms_total_ = 0;       // billed time since boot
    uint32_t opens_total_ = 0;
    uint32_t next_action_ms_ = kForeverMs;
    ValveReason reason_ = ValveReason::Boot;
    static constexpr int kHist = 16;
    uint64_t open_hist_[kHist] = {};   // uptime of the last opens (ring)
    int hist_n_ = 0, hist_i_ = 0;
};

}  // namespace tmc
