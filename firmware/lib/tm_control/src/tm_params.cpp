#include "tm_params.h"

namespace tmc {

namespace {
template <typename T>
bool clampField(T& v, T lo, T hi) {
    if (!(v == v)) { v = lo; return true; }  // NaN guard (floating point)
    if (v < lo) { v = lo; return true; }
    if (v > hi) { v = hi; return true; }
    return false;
}
}  // namespace

const char* toString(Mode m) {
    switch (m) {
        case Mode::Auto: return "AUTO";
        case Mode::Comfort: return "COMFORT";
        case Mode::Night: return "NIGHT";
        case Mode::Away: return "AWAY";
        case Mode::Off: return "OFF";
    }
    return "?";
}

const char* toString(ValveReason r) {
    switch (r) {
        case ValveReason::Boot: return "BOOT";
        case ValveReason::Pulse: return "PULSE";
        case ValveReason::Continuous: return "CONT";
        case ValveReason::Classic: return "CLASSIC";
        case ValveReason::Boost: return "BOOST";
        case ValveReason::Limp: return "LIMP";
        case ValveReason::TargetReached: return "TARGET";
        case ValveReason::NoDemand: return "NO_DEMAND";
        case ValveReason::UserOff: return "USER_OFF";
        case ValveReason::PlantOff: return "PLANT_OFF";
        case ValveReason::Fault: return "FAULT";
        case ValveReason::CycleLimit: return "CYCLE_LIMIT";
        case ValveReason::Arrival: return "ARRIVAL";
    }
    return "?";
}

const char* toString(TempSource s) {
    switch (s) {
        case TempSource::None: return "NONE";
        case TempSource::Fused: return "FUSED";
        case TempSource::RoomOnly: return "ROOM";
        case TempSource::BaseOnly: return "BASE";
    }
    return "?";
}

bool sanitize(ControlParams& p) {
    bool ch = false;
    ch |= clampField<uint32_t>(p.on_min_s, 60, 900);
    ch |= clampField<uint32_t>(p.off_min_s, 180, 3600);
    ch |= clampField<uint32_t>(p.tick_s, 5, 120);
    ch |= clampField<uint8_t>(p.max_cycles_per_hour, 2, 12);
    ch |= clampField(p.kp, 0.05, 3.0);
    ch |= clampField(p.ti_s, 600.0, 6 * 3600.0);
    ch |= clampField(p.td_s, 0.0, 1800.0);
    ch |= clampField(p.tf_s, 0.0, 900.0);
    ch |= clampField(p.i_band_c, 0.2, 5.0);
    ch |= clampField(p.i0, 0.0, 1.0);
    ch |= clampField(p.u_min, 0.0, 0.2);
    ch |= clampField(p.u_cont, 0.5, 1.0);
    ch |= clampField(p.sp_comfort, kSetpointMin, kSetpointMax);
    ch |= clampField(p.sp_night, kSetpointMin, kSetpointMax);
    ch |= clampField(p.sp_away, kSetpointMin, kAwayMaxC);
    ch |= clampField(p.recovery_done_c, 0.05, 2.0);
    ch |= clampField<uint16_t>(p.preheat_min, 0, 360);
    ch |= clampField<uint16_t>(p.arrival_force_min, 0, 240);
    ch |= clampField(p.arrival_stop_c, 0.5, 10.0);
    ch |= clampField<uint32_t>(p.stale_s, 60, 1800);
    ch |= clampField(p.w_room, 0.0, 1.0);
    ch |= clampField(p.tau_offset_s, 600.0, 48 * 3600.0);
    ch |= clampField(p.offset_learn_max_slope_c_h, 0.05, 5.0);
    ch |= clampField(p.offset_min_learn_s, 0.0, 24 * 3600.0);
    ch |= clampField(p.disagree_c, 0.3, 10.0);
    ch |= clampField<uint32_t>(p.disagree_s, 60, 7200);
    ch |= clampField(p.limp_min, 0.0, 0.5);
    ch |= clampField(p.limp_max, p.limp_min, 0.6);
    ch |= clampField(p.limp_default, p.limp_min, p.limp_max);
    ch |= clampField(p.classic_hyst_c, 0.05, 2.0);
    ch |= clampField<uint32_t>(p.cont_warn_s, 600, 24 * 3600);
    ch |= clampField<uint32_t>(p.budget_min_day, 0, 1440);
    ch |= clampField(p.rh_low, 10.0, 60.0);
    ch |= clampField(p.rh_high, p.rh_low + 5.0, 95.0);
    ch |= clampField(p.rh_hyst, 0.5, 10.0);
    if (p.u_min >= p.u_cont) { p.u_min = 0.03; p.u_cont = 0.90; ch = true; }
    if (p.t_valid_min_c >= p.t_valid_max_c) { p.t_valid_min_c = -20.0; p.t_valid_max_c = 50.0; ch = true; }
    if (static_cast<uint8_t>(p.recovery_schedule) > 1) { p.recovery_schedule = Recovery::Efficient; ch = true; }
    if (static_cast<uint8_t>(p.recovery_user) > 1) { p.recovery_user = Recovery::Fast; ch = true; }
    if (static_cast<uint8_t>(p.recovery_arrival) > 1) { p.recovery_arrival = Recovery::Efficient; ch = true; }
    if (static_cast<uint8_t>(p.fail_policy) > 1) { p.fail_policy = FailPolicy::Limp; ch = true; }
    if (static_cast<uint8_t>(p.algo) > 2) { p.algo = Algo::Pulse; ch = true; }
    return ch;
}

}  // namespace tmc
