// Golden test: the C++ Controller must reproduce the Python reference controller
// (tools/sim/thermal_sim.py) tick by tick: same setpoint, same demand u, same valve state.
// Regenerate the data with: cd tools/sim && python3 gen_golden.py ../../firmware/test/test_golden/golden_data.h
#include <unity.h>
#include <cmath>
#include <cstdio>
#include "golden_data.h"
#include "tm_controller.h"

using namespace tmc;

void setUp() {}
void tearDown() {}

static void runScenario(const GoldenScenario& sc, uint32_t now0) {
    ControlParams p;
    p.median3 = false;                 // the simulator feeds raw values
    p.sp_comfort = 20.0;
    p.sp_night = sc.schedule ? sc.sp_night : 20.0;
    p.preheat_min = static_cast<uint16_t>(sc.preheat_min);
    p.recovery_schedule = static_cast<Recovery>(sc.recovery);
    p.recovery_user = Recovery::Fast;
    p.offset_min_learn_s = 1e9;        // no base sensor in the simulator
    Controller c;
    WeekSchedule ws = WeekSchedule::makeDefault();   // Comfort 06:30-22:30
    p.sp_away = 7.0;
    p.recovery_arrival = Recovery::Efficient;   // ARRIVO: forced start, then pulse-only recovery
    p.arrival_force_min = 60;
    p.arrival_stop_c = 2.0;
    const Mode m0 = sc.arrive_tick >= 0 ? Mode::Away : (sc.schedule ? Mode::Auto : Mode::Comfort);
    c.begin(now0, p, ws, PersistentState{}, m0, /*start_unlocked=*/true);

    int u_fail = 0, v_fail = 0, sp_fail = 0, first_bad = -1;
    double max_du = 0.0;
    for (int k = 0; k < sc.n; ++k) {
        const GoldenRow& g = sc.rows[k];
        const uint32_t t_s = static_cast<uint32_t>(k) * 30u;
        const uint32_t now = now0 + t_s * 1000u;   // deliberately wraps around 2^32
        ControllerInputs in;
        in.now_ms = now;
        in.wall.valid = true;
        in.wall.day_number = t_s / 86400u;
        in.wall.wday = static_cast<uint8_t>(in.wall.day_number % 7u);
        in.wall.minute = static_cast<uint16_t>((t_s / 60u) % 1440u);
        c.onRoomSample(now, g.meas100 / 100.0, -1.0);
        if (k == sc.arrive_tick) c.arrive();
        const bool open = c.update(in);
        const Status& st = c.status();
        const double du = std::fabs(st.u - g.u);
        if (du > max_du) max_du = du;
        const bool bad_sp = std::lround(st.sp * 100.0) != g.sp100;
        const bool bad_u = du > 1e-6;
        const bool bad_v = (open ? 1 : 0) != g.open;
        if (bad_sp) ++sp_fail;
        if (bad_u) ++u_fail;
        if (bad_v) ++v_fail;
        if ((bad_sp || bad_u || bad_v) && first_bad < 0) {
            first_bad = k;
            char msg[200];
            snprintf(msg, sizeof msg, "%s tick %d: sp %.2f/%.2f u %.9f/%.9f open %d/%d", sc.name, k, st.sp,
                     g.sp100 / 100.0, st.u, g.u, open ? 1 : 0, g.open);
            TEST_MESSAGE(msg);
        }
    }
    char msg[200];
    snprintf(msg, sizeof msg, "%s: %d ticks, max |du| = %.3g, sp mismatches %d, u mismatches %d, valve mismatches %d",
             sc.name, sc.n, max_du, sp_fail, u_fail, v_fail);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sp_fail, "setpoint differs from the simulator");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, u_fail, "demand u differs from the simulator");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, v_fail, "valve state differs from the simulator");
}

static void test_golden_all_from_zero() {
    for (const auto& sc : g_scenarios) runScenario(sc, 0u);
}

static void test_golden_all_across_millis_rollover() {
    for (const auto& sc : g_scenarios) runScenario(sc, UINT32_MAX - 5000000u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_all_from_zero);
    RUN_TEST(test_golden_all_across_millis_rollover);
    return UNITY_END();
}
