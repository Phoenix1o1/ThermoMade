// Unit tests for the TermoMade control library (run on the PC: pio test -e native).
#include <unity.h>
#include <cmath>
#include <cstdio>
#include <random>
#include "tm_controller.h"
#include "tm_fusion.h"
#include "tm_humidity.h"
#include "tm_modulator.h"
#include "tm_pid.h"
#include "tm_schedule.h"
#include "tm_valve_fsm.h"

using namespace tmc;

void setUp() {}
void tearDown() {}

// ============================================================ modulator
static void test_modulator_table() {
    ControlParams p;
    struct { double u; uint32_t on, off; bool demand; } c[] = {
        {0.02, 180000, kForeverMs, false},
        {0.10, 180000, 1620000, true},
        {0.15, 180000, 1020000, true},
        {0.375, 180000, 300000, true},
        {0.50, 300000, 300000, true},
        {0.80, 1200000, 300000, true},
        {0.90, kForeverMs, 300000, true},
        {1.00, kForeverMs, 300000, true},
    };
    for (auto& k : c) {
        PulseTargets t = pulseTargets(p, k.u);
        TEST_ASSERT_EQUAL_UINT32(k.on, t.on_ms);
        TEST_ASSERT_EQUAL_UINT32(k.off, t.off_ms);
        TEST_ASSERT_EQUAL(k.demand, t.demand);
    }
    PulseTargets nan = pulseTargets(p, std::nan(""));
    TEST_ASSERT_FALSE(nan.demand);
}

static void test_modulator_duty_matches_u() {
    ControlParams p;
    for (double u = 0.03; u < 0.899; u += 0.01) {
        PulseTargets t = pulseTargets(p, u);
        const double duty = double(t.on_ms) / double(t.on_ms + t.off_ms);
        TEST_ASSERT_DOUBLE_WITHIN(0.002, u, duty);
        TEST_ASSERT_TRUE(t.on_ms >= p.on_min_s * 1000u);
        TEST_ASSERT_TRUE(t.off_ms >= p.off_min_s * 1000u);
    }
}

// ============================================================ valve FSM
static ValveRequest want(bool demand, uint32_t on, uint32_t off) {
    ValveRequest r;
    r.demand = demand;
    r.on_target_ms = on;
    r.off_target_ms = off;
    return r;
}

static void test_fsm_boot_lockout() {
    ControlParams p;
    ValveFsm f(p);
    f.begin(1000);
    ValveRequest r = want(true, kForeverMs, 0);
    for (uint32_t t = 1000; t < 1000 + 300000; t += 1000) TEST_ASSERT_FALSE(f.step(t, r));
    TEST_ASSERT_TRUE(f.step(1000 + 300000, r));
}

static void test_fsm_on_min_respected_without_demand() {
    ControlParams p;
    ValveFsm f(p);
    f.begin(0, true);
    TEST_ASSERT_TRUE(f.step(0, want(true, 180000, 600000)));
    ValveRequest none = want(false, 0, kForeverMs);
    for (uint32_t t = 1000; t < 180000; t += 1000) TEST_ASSERT_TRUE(f.step(t, none));
    TEST_ASSERT_FALSE(f.step(180000, none));
}

static void test_fsm_force_close_then_off_min_absolute() {
    ControlParams p;
    ValveFsm f(p);
    f.begin(0, true);
    TEST_ASSERT_TRUE(f.step(0, want(true, kForeverMs, 0)));
    ValveRequest fc = want(true, kForeverMs, 0);
    fc.force_close = true;
    TEST_ASSERT_FALSE(f.step(10000, fc));   // closes before ON_MIN when forced
    ValveRequest again = want(true, kForeverMs, 0);
    for (uint32_t t = 11000; t < 10000 + 300000; t += 1000) TEST_ASSERT_FALSE(f.step(t, again));
    TEST_ASSERT_TRUE(f.step(10000 + 300000, again));
}

static void test_fsm_continuous_stays_open() {
    ControlParams p;
    ValveFsm f(p);
    f.begin(0, true);
    ValveRequest r = want(true, kForeverMs, 300000);
    for (uint64_t t = 0; t < 48ULL * 3600000ULL; t += 30000) TEST_ASSERT_TRUE(f.step(uint32_t(t), r));
}

static void test_fsm_cycle_limit() {
    ControlParams p;
    p.max_cycles_per_hour = 2;
    ValveFsm f(p);
    f.begin(0, true);
    ValveRequest r = want(true, 180000, 0);   // wants pulses as often as possible
    uint64_t opens_in_window[100] = {};
    int n = 0;
    bool last = false;
    for (uint64_t t = 0; t < 10ULL * 3600000ULL; t += 1000) {
        bool o = f.step(uint32_t(t), r);
        if (o && !last && n < 100) opens_in_window[n++] = t;
        last = o;
    }
    for (int i = 2; i < n; ++i) TEST_ASSERT_TRUE(opens_in_window[i] - opens_in_window[i - 2] >= 3600000ULL);
}

// Randomised: whatever the requests and timing, OFF_MIN is never violated and ON_MIN is only
// shortened by force_close. Runs across a millis() rollover.
static void test_fsm_fuzz_hard_rules() {
    ControlParams p;
    std::mt19937 rng(12345);
    for (int run = 0; run < 20; ++run) {
        ValveFsm f(p);
        uint32_t now = (run % 2) ? UINT32_MAX - 3600000u * 3u : 0u;
        f.begin(now);
        bool open = false;
        uint64_t t_abs = 0, last_change = 0;
        bool booted_closed = true;
        for (int k = 0; k < 200000; ++k) {
            const uint32_t dt = 1 + rng() % 45000;
            now += dt;
            t_abs += dt;
            ValveRequest r;
            r.demand = (rng() % 4) != 0;
            const int pick = rng() % 4;
            r.on_target_ms = pick == 0 ? kForeverMs : (rng() % 1200000);
            r.off_target_ms = pick == 1 ? kForeverMs : (rng() % 1200000);
            r.force_close = (rng() % 50) == 0;
            const bool o = f.step(now, r);
            if (o != open) {
                const uint64_t held = t_abs - last_change;
                if (o) TEST_ASSERT_TRUE_MESSAGE(held >= p.off_min_s * 1000ULL, "reopened before OFF_MIN");
                else if (!r.force_close) TEST_ASSERT_TRUE_MESSAGE(held >= p.on_min_s * 1000ULL, "closed before ON_MIN");
                if (o && booted_closed) booted_closed = false;
                open = o;
                last_change = t_abs;
            }
        }
    }
}

// ============================================================ PID
static void test_pid_band_and_clamp() {
    ControlParams p;
    Pid pid(p);
    pid.setI(0.2);
    double u = pid.update(25.0, 15.0, 30.0);    // huge error: outside the band, no integration
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, u);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, pid.i());
    u = pid.update(25.0, 15.0, 30.0, 0.375);   // u_max cap
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.375, u);
}

static void test_pid_coast_after_setpoint_decrease() {
    ControlParams p;
    Pid pid(p);
    pid.setI(0.2);
    for (int k = 0; k < 20; ++k) pid.update(20.0, 20.0, 30.0);
    const double i_before = pid.i();
    // night: setpoint 18.5, room slowly cools from 20 -> 18.6 (error between -1.5 and -0.1)
    double t = 20.0;
    for (int k = 0; k < 400; ++k) {
        t = 18.6 + 1.4 * std::exp(-k / 120.0);
        pid.update(18.5, t, 30.0);
        TEST_ASSERT_TRUE(pid.coasting());
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, i_before, pid.i());
    }
    for (int k = 0; k < 30; ++k) pid.update(18.5, 18.4, 30.0);   // reached -> coast ends
    TEST_ASSERT_FALSE(pid.coasting());
    TEST_ASSERT_TRUE(pid.i() > i_before);
}

static void test_pid_resume_after_gap() {
    ControlParams p;
    Pid pid(p);
    pid.setI(0.3);
    pid.update(20.0, 20.0, 30.0);
    const double i0 = pid.i();
    pid.update(20.0, 19.5, 6 * 3600.0);         // 6 h gap: must not integrate 6 h of error
    TEST_ASSERT_TRUE(std::fabs(pid.i() - i0) < 0.01);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 19.5, pid.filtered());
}

// ============================================================ fusion
static void test_median_rejects_spike_follows_step() {
    Median3 m;
    m.push(20.0);
    m.push(20.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 20.0, m.push(35.0));   // single spike rejected
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 20.1, m.push(20.1));   // {20.1, 20, 35} -> 20.1
    m.push(22.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 22.0, m.push(22.0));   // real step followed after 1 sample
}

static void test_fusion_stale_and_fallback() {
    ControlParams p;
    Fusion f(p);
    f.onRoom(0, 20.0, 45.0);
    f.onBase(0, true, 21.0);
    FusionOut o = f.update(0, false);
    TEST_ASSERT_EQUAL(int(TempSource::RoomOnly), int(o.src));   // offset not learned yet
    // room node silent for > 300 s -> base only
    for (uint32_t t = 30000; t <= 330000; t += 30000) f.onBase(t, true, 21.0);
    o = f.update(330000, false);
    TEST_ASSERT_FALSE(o.room_ok);
    TEST_ASSERT_EQUAL(int(TempSource::BaseOnly), int(o.src));
    // base fails 3 times and goes stale -> none
    f.onBase(340000, false, 0);
    f.onBase(350000, false, 0);
    f.onBase(360000, false, 0);
    o = f.update(700000, false);
    TEST_ASSERT_EQUAL(int(TempSource::None), int(o.src));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, o.rh_room);
}

static void test_fusion_offset_learning_and_disagreement() {
    ControlParams p;
    Fusion f(p);
    uint32_t t = 0;
    for (int k = 0; k < 2 * 120; ++k, t += 30000) {   // 2 h steady, base reads +1.5 high
        f.onRoom(t, 20.0, 40.0);
        f.onBase(t, true, 21.5);
        f.update(t, false);
    }
    FusionOut o = f.out();
    TEST_ASSERT_TRUE(o.offset_ready);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.5, o.offset);
    TEST_ASSERT_EQUAL(int(TempSource::Fused), int(o.src));
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 20.0, o.t_c);
    // screen on (disturbed): no learning even if the gap changes
    for (int k = 0; k < 30; ++k, t += 30000) { f.onRoom(t, 20.0, 40.0); f.onBase(t, true, 23.5); f.update(t, true); }
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.5, f.out().offset);
    // persistent disagreement (2.0 > 1.5 degC for > 600 s) -> room only + flag
    TEST_ASSERT_TRUE(f.out().disagree);
    TEST_ASSERT_EQUAL(int(TempSource::RoomOnly), int(f.out().src));
}

static void test_fusion_implausible_ignored() {
    ControlParams p;
    Fusion f(p);
    f.onRoom(0, 85.0, 50);     // DS18B20 power-on value / garbage
    f.onRoom(0, -127.0, 50);
    TEST_ASSERT_EQUAL(int(TempSource::None), int(f.update(0, false).src));
}

// ============================================================ schedule
static void test_schedule_default_periods() {
    WeekSchedule w = WeekSchedule::makeDefault();
    TEST_ASSERT_EQUAL(int(Period::Night), int(w.periodAt(0, 6 * 60 + 29)));
    TEST_ASSERT_EQUAL(int(Period::Comfort), int(w.periodAt(0, 6 * 60 + 30)));
    TEST_ASSERT_EQUAL(int(Period::Comfort), int(w.periodAt(0, 22 * 60 + 29)));
    TEST_ASSERT_EQUAL(int(Period::Night), int(w.periodAt(0, 22 * 60 + 30)));
    TEST_ASSERT_EQUAL(int(Period::Night), int(w.periodAt(3, 0)));
    Period nx;
    TEST_ASSERT_EQUAL_INT32(60, w.minutesToNextChange(0, 5 * 60 + 30, &nx));
    TEST_ASSERT_EQUAL(int(Period::Comfort), int(nx));
    TEST_ASSERT_EQUAL_INT32(8 * 60, w.minutesToNextChange(6, 22 * 60 + 30, &nx));   // Sunday night -> Monday 06:30
}

static void test_schedule_sanitize() {
    WeekSchedule w = WeekSchedule::makeDefault();
    w.day[2].n = 3;
    w.day[2].slot[0] = {22 * 60, Period::Night};
    w.day[2].slot[1] = {7 * 60, Period::Comfort};
    w.day[2].slot[2] = {5000, Period::Comfort};   // invalid
    w.day[4].n = 0;
    TEST_ASSERT_TRUE(w.sanitize());
    TEST_ASSERT_EQUAL_UINT8(2, w.day[2].n);
    TEST_ASSERT_EQUAL_UINT16(7 * 60, w.day[2].slot[0].start_min);
    TEST_ASSERT_EQUAL_UINT8(2, w.day[4].n);
}

// ============================================================ humidity
static void test_humidity_math_and_advice() {
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 9.26, dewPointC(20.0, 50.0));
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 8.65, absHumidityGm3(20.0, 50.0));
    ControlParams p;
    HumidityAdvisor h(p);
    TEST_ASSERT_EQUAL(int(HumidityAdvice::Unknown), int(h.update(20, -1)));
    TEST_ASSERT_EQUAL(int(HumidityAdvice::TooDry), int(h.update(20, 30)));
    TEST_ASSERT_EQUAL(int(HumidityAdvice::TooDry), int(h.update(20, 37)));   // hysteresis
    TEST_ASSERT_EQUAL(int(HumidityAdvice::Ok), int(h.update(20, 39)));
    TEST_ASSERT_EQUAL(int(HumidityAdvice::TooHumid), int(h.update(20, 65)));
    TEST_ASSERT_EQUAL(int(HumidityAdvice::TooHumid), int(h.update(20, 58)));
    TEST_ASSERT_EQUAL(int(HumidityAdvice::Ok), int(h.update(20, 55)));
}

// ============================================================ controller
struct Rig {
    ControlParams p;
    Controller c;
    uint32_t now = 0;
    uint32_t t_s = 0;
    WallTime wall(uint32_t s) const {
        WallTime w;
        w.valid = true;
        w.day_number = s / 86400u;
        w.wday = uint8_t(w.day_number % 7u);
        w.minute = uint16_t((s / 60u) % 1440u);
        return w;
    }
    bool step(double room_t, bool room = true, bool plant = true, uint32_t dt_ms = 1000) {
        now += dt_ms;
        t_s = now / 1000u;
        if (room) c.onRoomSample(now, room_t, 45.0);
        ControllerInputs in;
        in.now_ms = now;
        in.wall = wall(t_s);
        in.plant_on = plant;
        return c.update(in);
    }
};

static void test_controller_limp_after_grace() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort);
    int opens = 0;
    bool last = false;
    for (int k = 0; k < 3 * 3600; ++k) {
        bool o = r.step(0, /*room=*/false);
        if (o && !last) ++opens;
        last = o;
        if (k < 80) TEST_ASSERT_FALSE(r.c.status().limp);   // boot grace
    }
    TEST_ASSERT_TRUE(r.c.status().limp);
    TEST_ASSERT_TRUE(r.c.status().alarms & kAlarmNoSensor);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.15, r.c.status().u);
    TEST_ASSERT_TRUE(opens >= 7 && opens <= 10);   // 0.15 duty -> one 3 min pulse every 20 min
}

static void test_controller_fail_off_never_opens() {
    Rig r;
    r.p.fail_policy = FailPolicy::FailOff;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort);
    for (int k = 0; k < 3600; ++k) TEST_ASSERT_FALSE(r.step(0, false));
}

static void test_controller_off_mode_and_antifreeze() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Off);
    for (int k = 0; k < 10; ++k) r.step(15.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 7.0, r.c.status().sp);
    TEST_ASSERT_TRUE(r.c.status().heating_enabled);
    r.p.antifreeze_in_off = false;
    r.c.setParams(r.p);
    for (int k = 0; k < 10; ++k) TEST_ASSERT_FALSE(r.step(3.0));
    TEST_ASSERT_FALSE(r.c.status().heating_enabled);
}

static void test_controller_boost_respects_lockout_and_expires() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort);
    r.c.startBoost(30);
    for (int k = 0; k < 299; ++k) TEST_ASSERT_FALSE(r.step(21.0));   // boot lockout first
    for (int k = 0; k < 20 * 60; ++k) TEST_ASSERT_TRUE(r.step(21.0));   // boost: open although warm
    for (int k = 0; k < 10 * 60; ++k) r.step(21.0);                  // boost ends at 30 min
    TEST_ASSERT_FALSE(r.c.status().boost);
    TEST_ASSERT_FALSE(r.c.status().valve_open);   // room is warm: closes after boost
}

static void test_controller_plant_off_closes_immediately() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort);
    for (int k = 0; k < 400; ++k) r.step(15.0);
    TEST_ASSERT_TRUE(r.c.status().valve_open);
    TEST_ASSERT_FALSE(r.step(15.0, true, /*plant=*/false));
    TEST_ASSERT_EQUAL(int(ValveReason::PlantOff), int(r.c.status().reason));
}

static void test_controller_efficient_recovery_caps_u() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Auto);
    r.c.status();
    // start at 03:00 (Night 18.5), room at 17.0; preheat starts Comfort at 05:30
    r.now = 3u * 3600u * 1000u;
    bool saw_cap = false;
    for (int k = 0; k < 4 * 3600; ++k) {
        r.step(17.0);
        const Status& s = r.c.status();
        if (s.recovering && s.recovery == Recovery::Efficient) {
            TEST_ASSERT_TRUE(s.u <= crossoverDuty(r.p) + 1e-12);
            saw_cap = true;
        }
    }
    TEST_ASSERT_TRUE(saw_cap);
    // a user +1 is FAST: u may exceed uc
    r.c.adjustSetpoint(+1.0);
    double umax = 0;
    for (int k = 0; k < 120; ++k) { r.step(17.0); umax = std::fmax(umax, r.c.status().u); }
    TEST_ASSERT_TRUE(umax > crossoverDuty(r.p) + 0.01);
}

static void test_controller_setpoint_rules() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort);
    r.step(20.0);
    r.c.setSetpoint(40.0);
    r.step(20.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 25.0, r.c.status().sp);
    TEST_ASSERT_TRUE(r.c.paramsChanged());
    r.c.setMode(Mode::Off);
    r.c.adjustSetpoint(+3.0);   // ignored in OFF
    r.step(20.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 7.0, r.c.status().sp);
}

static void test_controller_auto_override_expires() {
    Rig r;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Auto);
    r.now = (21u * 3600u + 0u) * 1000u;   // 21:00 Comfort
    r.step(20.0);
    r.c.setSetpoint(21.5);
    r.step(20.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 21.5, r.c.status().sp);
    TEST_ASSERT_TRUE(r.c.status().override_active);
    for (int k = 0; k < 2 * 3600; ++k) r.step(20.0);   // past 22:30
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 18.5, r.c.status().sp);
    TEST_ASSERT_FALSE(r.c.status().override_active);
}

static void test_controller_classic_hysteresis() {
    Rig r;
    r.p.algo = Algo::Classic;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort, true);
    TEST_ASSERT_TRUE(r.step(19.7));      // below 20 - 0.2 -> call
    for (int k = 0; k < 60; ++k) TEST_ASSERT_TRUE(r.step(20.5));   // ON_MIN keeps it open
    for (int k = 0; k < 200; ++k) r.step(20.5);
    TEST_ASSERT_FALSE(r.c.status().valve_open);
    for (int k = 0; k < 400; ++k) r.step(20.0);   // inside the band: stays closed
    TEST_ASSERT_FALSE(r.c.status().valve_open);
}

static void test_controller_persistent_roundtrip() {
    Rig r;
    PersistentState ps;
    ps.i_comfort = 0.22;
    ps.i_away = 0.05;
    ps.base_offset = 1.2;
    ps.offset_learned_s = 7200;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), ps, Mode::Comfort);
    PersistentState out = r.c.persistent();
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.22, out.i_comfort);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.05, out.i_away);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.2, out.base_offset);
    r.c.setMode(Mode::Away);
    r.step(15.0);
    out = r.c.persistent();
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.22, out.i_comfort);   // comfort I parked while away
}

static void test_controller_reset_learning() {
    Rig r;
    PersistentState ps;
    ps.i_comfort = 0.22;
    ps.i_away = 0.05;
    ps.base_offset = 1.2;
    ps.offset_learned_s = 7200;
    r.c.begin(0, r.p, WeekSchedule::makeDefault(), ps, Mode::Comfort);
    r.c.resetLearning(/*duty=*/false, /*offset=*/true);
    PersistentState out = r.c.persistent();
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.22, out.i_comfort);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, out.base_offset);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, out.offset_learned_s);
    r.c.resetLearning(/*duty=*/true, /*offset=*/false);
    out = r.c.persistent();
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, r.p.i0, out.i_comfort);
    TEST_ASSERT_TRUE(out.i_away < 0.0);   // unknown -> I0 when the band is entered
}

// ARRIVO in a cold house: mode back to AUTO, valve forced open for arrival_force_min, then pulses
// with EFFICIENT recovery (u <= uc). Decided with tools/sim/arrival_study.py.
static void test_controller_arrival_cold_house() {
    Rig r;
    r.now = 17u * 3600u * 1000u;            // 17:00, Comfort period
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Away, /*start_unlocked=*/true);
    for (int k = 0; k < 60; ++k) r.step(7.5);
    TEST_ASSERT_FALSE(r.c.status().valve_open);   // just above the 7 C antifreeze setpoint: no demand
    r.c.arrive();
    r.step(7.5);
    TEST_ASSERT_EQUAL(int(Mode::Auto), int(r.c.mode()));
    TEST_ASSERT_TRUE(r.c.status().arrival);
    TEST_ASSERT_TRUE(r.c.status().valve_open);
    TEST_ASSERT_EQUAL(int(ValveReason::Arrival), int(r.c.status().reason));
    for (int k = 0; k < 60 * 60 - 2; ++k) TEST_ASSERT_TRUE(r.step(9.0));   // open for the whole hour
    for (int k = 0; k < 5; ++k) r.step(9.0);
    TEST_ASSERT_FALSE(r.c.status().arrival);
    // afterwards: efficient recovery, demand capped at uc
    bool saw_close = false;
    for (int k = 0; k < 2 * 3600; ++k) {
        const bool o = r.step(9.5);
        saw_close |= !o;
        TEST_ASSERT_TRUE(r.c.status().u <= crossoverDuty(r.p) + 1e-12);
    }
    TEST_ASSERT_TRUE(r.c.status().recovering);
    TEST_ASSERT_EQUAL(int(Recovery::Efficient), int(r.c.status().recovery));
    TEST_ASSERT_TRUE(saw_close);   // pulses, not continuous
}

static void test_controller_arrival_warm_house_no_force() {
    Rig r;
    r.now = 17u * 3600u * 1000u;
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Away, true);
    for (int k = 0; k < 60; ++k) r.step(18.5);
    r.c.arrive();
    r.step(18.5);   // 18.5 >= 20 - 2: nothing to force
    TEST_ASSERT_FALSE(r.c.status().arrival);
    TEST_ASSERT_EQUAL(int(Mode::Auto), int(r.c.mode()));
}

static void test_controller_arrival_stops_when_close_and_respects_lockout() {
    Rig r;
    r.now = 17u * 3600u * 1000u;
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Away);   // boot lockout
    r.c.arrive();
    for (int k = 0; k < 299; ++k) TEST_ASSERT_FALSE(r.step(10.0));   // OFF_MIN from boot is absolute
    TEST_ASSERT_TRUE(r.c.status().arrival);
    TEST_ASSERT_TRUE(r.step(10.0));
    // the room warms up past setpoint - 2 C: the forced phase ends early (valve may then pulse)
    for (int k = 0; k < 600; ++k) r.step(18.2);
    TEST_ASSERT_FALSE(r.c.status().arrival);
}

static void test_controller_arrival_cancelled_by_mode() {
    Rig r;
    r.now = 17u * 3600u * 1000u;
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Away, true);
    r.c.arrive();
    for (int k = 0; k < 400; ++k) r.step(8.0);
    TEST_ASSERT_TRUE(r.c.status().arrival);
    r.c.setMode(Mode::Away);
    r.step(8.0);
    TEST_ASSERT_FALSE(r.c.status().arrival);
    r.p.arrival_force_min = 0;   // feature disabled: ARRIVO only switches to AUTO
    r.c.setParams(r.p);
    r.c.arrive();
    r.step(8.0);
    TEST_ASSERT_FALSE(r.c.status().arrival);
    TEST_ASSERT_EQUAL(int(Mode::Auto), int(r.c.mode()));
}

// Audit 2026-11-11: a boost must not survive OFF/AWAY, must stop when the room is clearly above the
// setpoint, and forced heating (boost / ARRIVO) must not run blind without any sensor.
static void test_controller_boost_limits() {
    Rig r;
    r.now = 17u * 3600u * 1000u;
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort, true);
    for (int k = 0; k < 30; ++k) r.step(19.0);
    r.c.startBoost(60);
    r.step(19.0);
    TEST_ASSERT_TRUE(r.c.status().boost);
    r.c.setMode(Mode::Off);
    r.step(19.0);
    TEST_ASSERT_FALSE(r.c.status().boost);
    r.c.setMode(Mode::Comfort);
    r.c.startBoost(60);
    for (int k = 0; k < 10; ++k) r.step(22.5);       // 20 + 2 C reached
    TEST_ASSERT_FALSE(r.c.status().boost);
}

static void test_fusion_stale_latches_and_colder_wins() {
    ControlParams p;
    p.median3 = false;
    Fusion f(p);
    uint32_t t = 1000;
    f.onRoom(t, 20.0, 40.0);
    f.update(t, false);
    TEST_ASSERT_TRUE(f.out().room_ok);
    f.update(t + 301000u, false);                     // stale
    TEST_ASSERT_FALSE(f.out().room_ok);
    f.update(t + 0xFFFFFFF0u, false);                 // ~49.7 days later the age wraps: still lost
    TEST_ASSERT_FALSE(f.out().room_ok);
    // disagreement with the CYD sensor colder than a (stuck) room sensor: the colder one is used
    Fusion g(p);
    t = 0;
    for (int k = 0; k < 200; ++k, t += 30000) { g.onRoom(t, 20.0, 40.0); g.onBase(t, true, 21.0); g.update(t, false); }
    TEST_ASSERT_TRUE(g.out().offset_ready);
    for (int k = 0; k < 40; ++k, t += 30000) { g.onRoom(t, 20.0, 40.0); g.onBase(t, true, 6.0); g.update(t, true); }
    TEST_ASSERT_TRUE(g.out().disagree);
    TEST_ASSERT_EQUAL(int(TempSource::BaseOnly), int(g.out().src));
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 5.0, g.out().t_c);
}

// Today's billed time and pulse count survive a reboot on the same day, not into the next day.
static void test_controller_counters_survive_reboot() {
    Rig r;
    r.now = (5u * 24u + 10u) * 3600u * 1000u;   // day 5, 10:00
    r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Comfort, true);
    for (int k = 0; k < 3600; ++k) r.step(18.0);                 // cold room: valve runs
    const PersistentState ps = r.c.persistent();
    TEST_ASSERT_TRUE(ps.billed_today_s > 600);
    TEST_ASSERT_TRUE(ps.counters_day != 0);
    Rig same;                                                    // reboot, same day
    same.now = r.now;
    same.c.begin(same.now, same.p, WeekSchedule::makeDefault(), ps, Mode::Comfort);
    same.step(21.0);
    TEST_ASSERT_EQUAL_UINT32(ps.billed_today_s, same.c.status().billed_today_s);
    TEST_ASSERT_EQUAL_UINT32(ps.pulses_today, same.c.status().pulses_today);
    Rig next;                                                    // reboot the day after
    next.now = r.now + 24u * 3600u * 1000u;
    next.c.begin(next.now, next.p, WeekSchedule::makeDefault(), ps, Mode::Comfort);
    next.step(21.0);
    TEST_ASSERT_EQUAL_UINT32(0, next.c.status().billed_today_s);
}

// The big one: random user actions, sensor dropouts, plant gating and params changes for 20 days.
// Hard rules must hold: OFF_MIN never violated, ON_MIN only shortened by forced closes,
// setpoint always in [5, 25], u always in [0, 1].
static void test_controller_fuzz_hard_rules() {
    std::mt19937 rng(777);
    for (int run = 0; run < 6; ++run) {
        Rig r;
        r.now = (run % 2) ? UINT32_MAX - 7200000u : 0u;
        r.c.begin(r.now, r.p, WeekSchedule::makeDefault(), PersistentState{}, Mode::Auto);
        bool open = false;
        uint64_t t_abs = 0, last_change = 0;
        double room = 18.0;
        bool plant = true;
        for (int k = 0; k < 20 * 86400 / 5; ++k) {
            const uint32_t dt = 1000 + rng() % 9000;
            t_abs += dt;
            room += (open ? 0.002 : -0.001) * (dt / 1000.0) + ((rng() % 100) - 50) * 0.0004;
            if (room < 2) room = 2;
            if (room > 30) room = 30;
            const int ev = rng() % 4000;
            if (ev == 0) r.c.setMode(Mode(rng() % 5));
            else if (ev == 1) r.c.adjustSetpoint(((rng() % 7) - 3) * 0.5);
            else if (ev == 2) r.c.startBoost(uint16_t(rng() % 120));
            else if (ev == 3) r.c.stopBoost();
            else if (ev == 4) plant = !plant;
            else if (ev == 5) { ControlParams q = r.p; q.algo = Algo(rng() % 3); r.c.setParams(q); }
            else if (ev == 6) r.c.arrive();
            const bool sensor_ok = (t_abs / 3600000ULL) % 7 != 3;   // one hour in seven without sensor
            const bool o = r.step(room, sensor_ok, plant, dt);
            const Status& s = r.c.status();
            TEST_ASSERT_TRUE(s.sp >= 5.0 && s.sp <= 25.0);
            TEST_ASSERT_TRUE(s.u >= 0.0 && s.u <= 1.0);
            if (o != open) {
                const uint64_t held = t_abs - last_change;
                if (o) TEST_ASSERT_TRUE_MESSAGE(held >= r.p.off_min_s * 1000ULL, "reopened before OFF_MIN");
                else {
                    const bool forced = s.reason == ValveReason::UserOff || s.reason == ValveReason::PlantOff ||
                                        s.reason == ValveReason::Fault;
                    if (!forced) TEST_ASSERT_TRUE_MESSAGE(held >= r.p.on_min_s * 1000ULL, "closed before ON_MIN");
                }
                open = o;
                last_change = t_abs;
            }
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_modulator_table);
    RUN_TEST(test_modulator_duty_matches_u);
    RUN_TEST(test_fsm_boot_lockout);
    RUN_TEST(test_fsm_on_min_respected_without_demand);
    RUN_TEST(test_fsm_force_close_then_off_min_absolute);
    RUN_TEST(test_fsm_continuous_stays_open);
    RUN_TEST(test_fsm_cycle_limit);
    RUN_TEST(test_fsm_fuzz_hard_rules);
    RUN_TEST(test_pid_band_and_clamp);
    RUN_TEST(test_pid_coast_after_setpoint_decrease);
    RUN_TEST(test_pid_resume_after_gap);
    RUN_TEST(test_median_rejects_spike_follows_step);
    RUN_TEST(test_fusion_stale_and_fallback);
    RUN_TEST(test_fusion_offset_learning_and_disagreement);
    RUN_TEST(test_fusion_implausible_ignored);
    RUN_TEST(test_schedule_default_periods);
    RUN_TEST(test_schedule_sanitize);
    RUN_TEST(test_humidity_math_and_advice);
    RUN_TEST(test_controller_limp_after_grace);
    RUN_TEST(test_controller_fail_off_never_opens);
    RUN_TEST(test_controller_off_mode_and_antifreeze);
    RUN_TEST(test_controller_boost_respects_lockout_and_expires);
    RUN_TEST(test_controller_plant_off_closes_immediately);
    RUN_TEST(test_controller_efficient_recovery_caps_u);
    RUN_TEST(test_controller_setpoint_rules);
    RUN_TEST(test_controller_auto_override_expires);
    RUN_TEST(test_controller_classic_hysteresis);
    RUN_TEST(test_controller_persistent_roundtrip);
    RUN_TEST(test_controller_reset_learning);
    RUN_TEST(test_controller_arrival_cold_house);
    RUN_TEST(test_controller_arrival_warm_house_no_force);
    RUN_TEST(test_controller_arrival_stops_when_close_and_respects_lockout);
    RUN_TEST(test_controller_arrival_cancelled_by_mode);
    RUN_TEST(test_controller_boost_limits);
    RUN_TEST(test_fusion_stale_latches_and_colder_wins);
    RUN_TEST(test_controller_counters_survive_reboot);
    RUN_TEST(test_controller_fuzz_hard_rules);
    return UNITY_END();
}
