#include "tm_controller.h"

namespace tmc {

namespace {
constexpr uint64_t kBootGraceMs = 90000;   // wait for the first sensor frames before LIMP
constexpr double kBandAwayMaxC = kAwayMaxC;
constexpr double kBoostMaxOverC = 2.0;   // a boost ends once the room is this far above the setpoint

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint32_t msToS(uint32_t ms) { return ms == kForeverMs ? UINT32_MAX : (ms + 999u) / 1000u; }
}  // namespace

Controller::Controller()
    : sched_(WeekSchedule::makeDefault()), fusion_(p_), pid_(p_), fsm_(p_), hum_(p_) {}

void Controller::begin(uint32_t now_ms, const ControlParams& params, const WeekSchedule& sched,
                       const PersistentState& ps, Mode mode, bool start_unlocked) {
    p_ = params;
    sanitize(p_);
    sched_ = sched;
    sched_.sanitize();
    mode_ = prev_mode_ = (static_cast<uint8_t>(mode) <= 4) ? mode : Mode::Auto;
    i_band_[0] = ps.i_comfort;
    i_band_[1] = ps.i_away;
    fusion_.restoreOffset(ps.base_offset, ps.offset_learned_s);
    restore_ = ps;
    band_away_ = false;
    pid_.setI(learnedI(false));
    fsm_.begin(now_ms, start_unlocked);
    started_ = true;
    last_now_ = now_ms;
    uptime_ms_ = 0;
    begin_uptime_ = 0;
    pid_acc_ms_ = static_cast<uint64_t>(p_.tick_s) * 1000u;   // run the PI on the first update
    pid_ran_ = false;
    have_last_sp_ = false;
    recovering_ = false;
    pushEvent(EventType::Boot, static_cast<uint8_t>(mode_));
}

double Controller::learnedI(bool away) const {
    const double v = i_band_[away ? 1 : 0];
    return (v >= 0.0 && v <= 1.0) ? v : p_.i0;
}

void Controller::setParams(const ControlParams& p) {
    ControlParams np = p;
    sanitize(np);
    p_ = np;   // pid/fsm/fusion/hum hold a pointer to p_: they see the new values immediately
}

void Controller::setSchedule(const WeekSchedule& s) {
    sched_ = s;
    sched_.sanitize();
}

void Controller::setMode(Mode m) {
    if (static_cast<uint8_t>(m) > 4 || m == mode_) return;
    prev_mode_ = mode_;
    mode_ = m;
    override_active_ = false;
    if (m == Mode::Off || m == Mode::Away) boost_ = false;   // leaving / switching off ends a boost
    if (arrival_ && m != Mode::Auto) {   // any other mode choice cancels the forced start
        arrival_ = false;
        pushEvent(EventType::Arrival, 0);
    }
    pending_cause_ = (prev_mode_ == Mode::Away || prev_mode_ == Mode::Off) ? Cause::Arrival : Cause::User;
    pushEvent(EventType::ModeChange, static_cast<uint8_t>(m), static_cast<uint8_t>(prev_mode_));
}

void Controller::setSetpoint(double sp_c) {
    sp_c = clampSetpoint(sp_c);
    switch (mode_) {
        case Mode::Auto:
            override_active_ = true;
            override_sp_ = sp_c;
            override_period_ = st_.period;
            break;
        case Mode::Comfort: p_.sp_comfort = sp_c; params_dirty_ = true; break;
        case Mode::Night: p_.sp_night = sp_c; params_dirty_ = true; break;
        case Mode::Away: p_.sp_away = sp_c > kAwayMaxC ? kAwayMaxC : sp_c; params_dirty_ = true; break;
        case Mode::Off: return;   // nothing to adjust
    }
    pending_cause_ = Cause::User;
    pushEvent(EventType::SetpointChange, static_cast<uint8_t>(mode_), 0, static_cast<float>(sp_c));
}

void Controller::adjustSetpoint(double delta_c) {
    if (mode_ == Mode::Off) return;
    const double base = (st_.sp > 0.0) ? st_.sp : p_.sp_comfort;
    setSetpoint(base + delta_c);
}

void Controller::startBoost(uint16_t minutes) {
    if (minutes == 0) { stopBoost(); return; }
    if (minutes > 240) minutes = 240;
    boost_ = true;
    boost_until_ms_ = uptime_ms_ + static_cast<uint64_t>(minutes) * 60000u;
}

void Controller::stopBoost() {
    boost_ = false;
    if (arrival_) {
        arrival_ = false;
        pushEvent(EventType::Arrival, 0);
    }
}

void Controller::arrive() {
    if (mode_ != Mode::Auto) setMode(Mode::Auto);   // from AWAY/OFF: pending cause = Arrival
    else pending_cause_ = Cause::Arrival;
    if (p_.arrival_force_min == 0) return;
    arrival_ = true;
    arrival_started_ = false;   // the forced time is counted from the next update
}

Controller::Resolved Controller::resolve(const WallTime& wall) {
    Resolved r{true, p_.sp_comfort, Period::Comfort, false, false};
    switch (mode_) {
        case Mode::Auto: {
            if (wall.valid) {
                r.period = sched_.periodAt(wall.wday, wall.minute);
                r.sp = (r.period == Period::Comfort) ? p_.sp_comfort : p_.sp_night;
                if (r.period == Period::Night && p_.preheat_min > 0) {
                    Period next = Period::Night;
                    const int32_t n = sched_.minutesToNextChange(wall.wday, wall.minute, &next);
                    if (next == Period::Comfort && n >= 0 && n <= static_cast<int32_t>(p_.preheat_min)) {
                        r.sp = p_.sp_comfort;
                        r.preheat = true;
                    }
                }
            } else {
                r.time_unknown = true;          // no clock: stay in Comfort
            }
            if (override_active_) {
                if (override_period_ == r.period) r.sp = override_sp_;
                else override_active_ = false;   // period changed: override expires
            }
            break;
        }
        case Mode::Comfort: r.sp = p_.sp_comfort; break;
        case Mode::Night: r.sp = p_.sp_night; r.period = Period::Night; break;
        case Mode::Away: r.sp = p_.sp_away; break;
        case Mode::Off:
            if (p_.antifreeze_in_off) r.sp = p_.sp_away;
            else r.enabled = false;
            break;
    }
    r.sp = clampSetpoint(r.sp);
    return r;
}

void Controller::swapBand(double sp) {
    const bool away = sp <= kBandAwayMaxC;
    if (away == band_away_) return;
    i_band_[band_away_ ? 1 : 0] = pid_.i();
    band_away_ = away;
    pid_.setI(learnedI(away));
}

bool Controller::update(const ControllerInputs& in) {
    if (!started_) begin(in.now_ms, p_, sched_, PersistentState{}, mode_);
    const uint32_t dt = in.now_ms - last_now_;   // rollover-safe
    last_now_ = in.now_ms;
    uptime_ms_ += dt;
    pid_acc_ms_ += dt;

    // ---- sensors ----
    const FusionOut& fo = fusion_.update(in.now_ms, in.base_disturbed);
    const bool have_t = fo.src != TempSource::None;
    const double T = fo.t_c;
    const bool grace = !have_t && (uptime_ms_ - begin_uptime_) < kBootGraceMs &&
                       last_src_ == TempSource::None;

    // ---- setpoint ----
    const Resolved r = resolve(in.wall);
    if (boost_ && uptime_ms_ >= boost_until_ms_) boost_ = false;
    if (boost_ && have_t && T >= r.sp + kBoostMaxOverC) boost_ = false;   // boost never runs away
    if (arrival_) {
        if (!arrival_started_) arrival_until_ms_ = uptime_ms_ + static_cast<uint64_t>(p_.arrival_force_min) * 60000u;
        // ends after the time, or as soon as the room is close to the setpoint (also right away
        // if ARRIVO is pressed in a house that is already nearly warm)
        const bool warm = have_t && T >= r.sp - p_.arrival_stop_c;
        if (uptime_ms_ >= arrival_until_ms_ || warm || !r.enabled) {
            arrival_ = false;
            if (arrival_started_) pushEvent(EventType::Arrival, 0, 0, have_t ? static_cast<float>(T) : 0.0f);
        } else if (!arrival_started_) {
            arrival_started_ = true;
            pushEvent(EventType::Arrival, 1, 0, have_t ? static_cast<float>(T) : 0.0f);
        }
    }

    // recovery: classify setpoint increases (docs/04 §5)
    if (have_last_sp_ && r.sp > last_sp_ + 0.05) {
        recovering_ = true;
        switch (pending_cause_) {
            case Cause::User: recovery_ = p_.recovery_user; break;
            case Cause::Arrival: recovery_ = p_.recovery_arrival; break;
            default: recovery_ = p_.recovery_schedule; break;
        }
    } else if (have_last_sp_ && r.sp < last_sp_ - 0.05) {
        recovering_ = false;
    }
    if (have_last_sp_ && (r.sp > last_sp_ + 0.05 || r.sp < last_sp_ - 0.05) && pending_cause_ == Cause::None) {
        pushEvent(EventType::SetpointChange, static_cast<uint8_t>(mode_), 1, static_cast<float>(r.sp));
    }
    pending_cause_ = Cause::None;
    have_last_sp_ = true;
    last_sp_ = r.sp;
    if (recovering_ && have_t && r.sp - T <= p_.recovery_done_c) recovering_ = false;
    double u_max = 1.0;
    if (recovering_ && recovery_ != Recovery::Fast) {
        const bool far = have_t && r.sp - T > p_.recovery_wide_c;
        if (recovery_ == Recovery::Efficient || !far) u_max = crossoverDuty(p_);   // pulses only
    }

    swapBand(r.sp);

    // ---- algorithm selection ----
    Algo algo = p_.algo;
    if (algo == Algo::AlternateDays) algo = (in.wall.valid && (in.wall.day_number % 2u) == 1u) ? Algo::Classic : Algo::Pulse;

    // ---- build the valve request ----
    ValveRequest rq;
    bool limp = false;
    const bool pid_due = pid_acc_ms_ >= static_cast<uint64_t>(p_.tick_s) * 1000u;

    if (!r.enabled) {
        rq.force_close = true;
        rq.close_reason = ValveReason::UserOff;
        u_ = 0.0;
    } else if (!in.plant_on) {
        rq.force_close = true;
        rq.close_reason = ValveReason::PlantOff;
        u_ = 0.0;
    } else if ((boost_ || arrival_) && have_t) {   // blind forcing is not allowed: no sensor -> LIMP path
        // forced heating: open as soon as the FSM allows (OFF_MIN is never shortened)
        const ValveReason why = boost_ ? ValveReason::Boost : ValveReason::Arrival;
        rq.demand = true;
        rq.on_target_ms = kForeverMs;
        rq.off_target_ms = 0;
        rq.open_reason = why;
        rq.close_reason = why;
        u_ = 1.0;
        pid_.resetFilter();   // the PI restarts from the current reading when the forced phase ends
    } else if (!have_t) {
        if (grace) {
            rq.demand = false;
            rq.close_reason = ValveReason::Boot;
        } else if (p_.fail_policy == FailPolicy::Limp) {
            limp = true;
            // open-loop at the learned steady-state duty of the current band
            const double learned = pid_.i();
            u_ = clampD(learned > 0.0 ? learned : p_.limp_default, p_.limp_min, p_.limp_max);
            const PulseTargets t = pulseTargets(p_, u_);
            rq.demand = t.demand;
            rq.on_target_ms = t.on_ms;
            rq.off_target_ms = t.off_ms;
            rq.open_reason = ValveReason::Limp;
            rq.close_reason = ValveReason::Limp;
        } else {
            rq.force_close = true;
            rq.close_reason = ValveReason::Fault;
            u_ = 0.0;
        }
        pid_.resetFilter();
    } else if (algo == Algo::Classic) {
        if (T < r.sp - p_.classic_hyst_c) classic_call_ = true;
        else if (T > r.sp + p_.classic_hyst_c) classic_call_ = false;
        rq.demand = classic_call_;
        rq.on_target_ms = classic_call_ ? kForeverMs : 0;
        rq.off_target_ms = classic_call_ ? 0 : kForeverMs;
        rq.open_reason = ValveReason::Classic;
        rq.close_reason = ValveReason::TargetReached;
        u_ = classic_call_ ? 1.0 : 0.0;
    } else {
        if (pid_due) {
            const double dt_s = static_cast<double>(pid_acc_ms_) / 1000.0;
            u_ = pid_.update(r.sp, T, dt_s, u_max);
            pid_ran_ = true;
        } else if (u_ > u_max) {
            u_ = u_max;   // a recovery cap that starts between PI ticks applies immediately
        }
        const PulseTargets t = pulseTargets(p_, u_);
        rq.demand = t.demand;
        rq.on_target_ms = t.on_ms;
        rq.off_target_ms = t.off_ms;
        rq.open_reason = (t.on_ms == kForeverMs) ? ValveReason::Continuous : ValveReason::Pulse;
        rq.close_reason = t.demand ? ValveReason::Pulse : ValveReason::NoDemand;
    }
    if (pid_due) pid_acc_ms_ = 0;

    const bool open = fsm_.step(in.now_ms, rq);

    // ---- counters ----
    if (in.wall.valid) {
        if (!have_day_) {
            have_day_ = true;
            day_ = in.wall.day_number;
            if (restore_.counters_day != 0 && restore_.counters_day == day_) {   // same day as before the reboot
                billed_ms_today_ += static_cast<uint64_t>(restore_.billed_today_s) * 1000u;
                pulses_today_ += restore_.pulses_today;
            }
            restore_.counters_day = 0;
        }
        else if (in.wall.day_number != day_) {
            pushEvent(EventType::DayRollover, 0, 0, static_cast<float>(static_cast<double>(billed_ms_today_) / 60000.0),
                      static_cast<float>(pulses_today_));
            day_ = in.wall.day_number;
            billed_ms_today_ = 0;
            pulses_today_ = 0;
        }
    }
    const uint64_t open_total = fsm_.openMsTotal();
    billed_ms_today_ += open_total - last_open_total_ms_;
    last_open_total_ms_ = open_total;
    const uint32_t opens = fsm_.opensTotal();
    pulses_today_ += opens - last_opens_total_;
    last_opens_total_ = opens;
    open_streak_ms_ = open ? open_streak_ms_ + dt : 0;

    // ---- events ----
    if (open != last_open_) {
        pushEvent(open ? EventType::ValveOpen : EventType::ValveClose, static_cast<uint8_t>(fsm_.reason()), 0,
                  static_cast<float>(u_), have_t ? static_cast<float>(T) : 0.0f);
        last_open_ = open;
    }
    if (fo.src != last_src_) {
        pushEvent(EventType::SourceChange, static_cast<uint8_t>(fo.src), static_cast<uint8_t>(last_src_));
        last_src_ = fo.src;
    }
    if (limp != last_limp_) {
        pushEvent(limp ? EventType::LimpEnter : EventType::LimpExit, 0, 0, static_cast<float>(u_));
        last_limp_ = limp;
    }

    // ---- humidity ----
    double rh = fo.rh_room;
    hum_.update(fo.t_room, rh);

    // ---- status ----
    st_.fusion = fo;
    st_.heating_enabled = r.enabled;
    st_.sp = r.sp;
    st_.mode = mode_;
    st_.period = r.period;
    st_.preheating = r.preheat;
    st_.override_active = override_active_;
    st_.boost = boost_;
    st_.boost_left_s = boost_ ? static_cast<uint32_t>((boost_until_ms_ - uptime_ms_) / 1000u) : 0;
    st_.arrival = arrival_;
    st_.arrival_left_s = arrival_ ? static_cast<uint32_t>((arrival_until_ms_ - uptime_ms_) / 1000u) : 0;
    st_.algo_active = algo;
    st_.recovering = recovering_;
    st_.recovery = recovery_;
    st_.u = u_;
    st_.p = pid_.p();
    st_.i = pid_.i();
    st_.d = pid_.d();
    st_.coast = pid_.coasting();
    st_.limp = limp;
    st_.waiting_sensors = grace;
    st_.valve_open = open;
    st_.reason = fsm_.reason();
    st_.next_action_s = msToS(fsm_.nextActionMs());
    st_.lockout_s = msToS(fsm_.lockoutRemainingMs());
    st_.billed_today_s = static_cast<uint32_t>(billed_ms_today_ / 1000u);
    st_.pulses_today = pulses_today_;
    st_.open_streak_s = static_cast<uint32_t>(open_streak_ms_ / 1000u);
    st_.humidity = hum_.advice();
    st_.dew_point_c = (rh >= 0.0) ? dewPointC(fo.t_room, rh) : 0.0;
    st_.time_valid = in.wall.valid;
    st_.uptime_ms = uptime_ms_;

    uint16_t al = 0;
    if (!fo.room_ok && !grace) al |= kAlarmRoomLost;
    if (!fo.base_ok && !grace) al |= kAlarmBaseLost;
    if (!have_t && !grace && r.enabled) al |= kAlarmNoSensor;
    if (fo.disagree) al |= kAlarmDisagree;
    if (r.time_unknown) al |= kAlarmTimeUnknown;
    if (open_streak_ms_ >= static_cast<uint64_t>(p_.cont_warn_s) * 1000u) al |= kAlarmContOn;
    if (p_.budget_min_day > 0 && billed_ms_today_ >= static_cast<uint64_t>(p_.budget_min_day) * 60000u) al |= kAlarmBudget;
    const uint16_t changed = al ^ last_alarms_;
    for (uint8_t b = 0; b < 16; ++b) {
        if (changed & (1u << b)) pushEvent((al & (1u << b)) ? EventType::AlarmOn : EventType::AlarmOff, b);
    }
    last_alarms_ = al;
    st_.alarms = al;
    return open;
}

void Controller::resetLearning(bool duty, bool offset) {
    if (duty) {
        i_band_[0] = i_band_[1] = -1.0;
        pid_.setI(p_.i0);
    }
    if (offset) fusion_.resetOffset();
}

PersistentState Controller::persistent() const {
    PersistentState ps;
    ps.i_comfort = band_away_ ? i_band_[0] : pid_.i();
    ps.i_away = band_away_ ? pid_.i() : i_band_[1];
    ps.base_offset = fusion_.offset();
    ps.offset_learned_s = fusion_.offsetLearnedS();
    if (have_day_) {
        ps.counters_day = day_;
        ps.billed_today_s = static_cast<uint32_t>(billed_ms_today_ / 1000u);
        ps.pulses_today = pulses_today_;
    } else {
        ps = PersistentState{ps.i_comfort, ps.i_away, ps.base_offset, ps.offset_learned_s,
                             restore_.counters_day, restore_.billed_today_s, restore_.pulses_today};   // date not known yet: keep what was stored
    }
    return ps;
}

void Controller::pushEvent(EventType t, uint8_t a, uint8_t b, float x, float y) {
    Event& e = evq_[(ev_head_ + ev_n_) % kEvq];
    if (ev_n_ == kEvq) ev_head_ = (ev_head_ + 1) % kEvq;   // overwrite the oldest
    else ++ev_n_;
    e.uptime_ms = uptime_ms_;
    e.type = t;
    e.a = a;
    e.b = b;
    e.x = x;
    e.y = y;
}

bool Controller::popEvent(Event& e) {
    if (ev_n_ == 0) return false;
    e = evq_[ev_head_];
    ev_head_ = (ev_head_ + 1) % kEvq;
    --ev_n_;
    return true;
}

}  // namespace tmc
