// TermoMade control library — tunable parameters (source of truth: docs/04 §7).
#pragma once
#include <cstdint>
#include "tm_types.h"

namespace tmc {

constexpr double kSetpointMin = 5.0;
constexpr double kSetpointMax = 25.0;
constexpr double kAwayMaxC = 12.0;     // AWAY setpoint cap; setpoints <= this use the antifreeze I band

struct ControlParams {
    // --- valve timing (hard limits) ---
    uint32_t on_min_s = 180;        // circuit fill time; minimum ON for automatic closes
    uint32_t off_min_s = 300;       // valve protection; ABSOLUTE minimum OFF
    uint32_t tick_s = 30;           // PI controller period
    uint8_t max_cycles_per_hour = 8;

    // --- PI(D) ---
    double kp = 0.6;                // duty per degC
    double ti_s = 7200.0;           // integral time
    double td_s = 0.0;              // derivative time (off by default)
    double tf_s = 120.0;            // measurement EMA time constant
    double i_band_c = 1.0;          // integrate only when |e| <= band
    double i0 = 0.15;               // initial learned duty (first boot)

    // --- pulse modulator ---
    double u_min = 0.03;            // below: no pulses
    double u_cont = 0.90;           // above: continuous ON

    // --- setpoints ---
    double sp_comfort = 20.0;
    double sp_night = 18.5;
    double sp_away = 7.0;           // antifreeze
    bool antifreeze_in_off = true;  // mode OFF still protects against freezing

    // --- recovery after setpoint increases ---
    Recovery recovery_schedule = Recovery::Efficient;  // Night -> Comfort
    Recovery recovery_user = Recovery::Fast;           // +/- and manual modes
    Recovery recovery_arrival = Recovery::Efficient;   // Away/Off -> heating (after the ARRIVO forced start)
    double recovery_done_c = 0.3;   // recovery ends when sp - T <= this
    uint16_t preheat_min = 60;      // start Comfort earlier (fixed until learned)

    // --- ARRIVO button (tools/sim/arrival_study.py, docs/04 §5) ---
    uint16_t arrival_force_min = 60;   // forced heating after arriving (0 = no forced start)
    double arrival_stop_c = 2.0;       // ...ends early once T >= setpoint - this

    // --- sensors / fusion ---
    uint32_t stale_s = 300;         // node silent -> lost
    double w_room = 0.7;            // fusion weight of the room node
    double tau_offset_s = 6 * 3600.0;
    double offset_learn_max_slope_c_h = 0.5;
    double offset_min_learn_s = 3600.0;  // learning time before the base sensor joins the fusion
    double disagree_c = 1.5;        // |room - base_corrected| alarm threshold
    uint32_t disagree_s = 600;
    double t_valid_min_c = -20.0;   // plausibility window
    double t_valid_max_c = 50.0;
    bool median3 = true;            // median-of-3 spike filter on each sensor

    // --- fallback ---
    FailPolicy fail_policy = FailPolicy::Limp;
    double limp_min = 0.05;
    double limp_max = 0.30;
    double limp_default = 0.15;

    // --- classic thermostat (A/B) ---
    Algo algo = Algo::Pulse;
    double classic_hyst_c = 0.2;

    // --- warnings ---
    uint32_t cont_warn_s = 4 * 3600;
    uint32_t budget_min_day = 0;    // 0 = off

    // --- humidity advice ---
    double rh_low = 35.0;
    double rh_high = 60.0;
    double rh_hyst = 3.0;
};

// Clamp every field into its allowed range. Returns true if something was changed.
bool sanitize(ControlParams& p);

// uc = ON_MIN / (ON_MIN + OFF_MIN): boundary between pulse-frequency and pulse-width modulation.
inline double crossoverDuty(const ControlParams& p) {
    return static_cast<double>(p.on_min_s) / static_cast<double>(p.on_min_s + p.off_min_s);
}

inline double clampSetpoint(double sp) {
    if (!(sp == sp)) return kSetpointMin;  // NaN guard
    return sp < kSetpointMin ? kSetpointMin : (sp > kSetpointMax ? kSetpointMax : sp);
}

}  // namespace tmc
