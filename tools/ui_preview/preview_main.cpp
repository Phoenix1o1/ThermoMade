// UI preview: drives the REAL base UI code (src/base/ui.cpp + ui_menu.cpp) with scripted touches and
// snapshots, and writes what the 320x240 screen shows as PPM images (build.sh converts them to PNG).
#include <Arduino.h>
#include <cstdio>
#include <string>
#include <vector>
#include "LovyanGFX.hpp"
#include "shared.h"
#include "touch.h"
#include "ui.h"

extern uint32_t g_now_ms;
extern Snapshot g_snap;
extern std::vector<Request> g_requests;
extern TouchPoint g_touch;

static std::string g_out = ".";

static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 15) {
        g_now_ms += 15;
        ui::loop();
    }
}

static void tap(int x, int y, uint32_t hold_ms = 120) {
    g_touch.pressed = true;
    g_touch.x = static_cast<int16_t>(x);
    g_touch.y = static_cast<int16_t>(y);
    run(hold_ms);
    g_touch.pressed = false;
    run(300);
}

static void shot(const char* name) {
    run(300);   // let the 250 ms redraw happen
    const lgfx::LGFX_Device* d = lgfx::g_device;
    const std::string path = g_out + "/" + name + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; ++i) {
        const uint16_t c = d->fb[i];
        const unsigned char rgb[3] = {static_cast<unsigned char>(((c >> 11) & 0x1F) * 255 / 31),
                                      static_cast<unsigned char>(((c >> 5) & 0x3F) * 255 / 63),
                                      static_cast<unsigned char>((c & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path.c_str());
}

static void baseSnapshot() {
    Snapshot& s = g_snap;
    s.seq = 1;
    s.mode = tmc::Mode::Auto;
    s.time_valid = true;
    s.time_src = TimeSrc::NodeRtc;
    s.unix_time = 1794256920;   // 2026-11-09 21:42 CET
    s.hour = 21;
    s.minute = 42;
    s.sd_ok = true;
    s.ds18b20_present = true;
    s.node_paired = true;
    s.node_rssi = -61;
    s.node_age_s = 12;
    s.light_pct = 37;
    s.light_raw = 1830;
    s.reset_reason = "power-on";
    tmc::Status& st = s.st;
    st.fusion.src = tmc::TempSource::Fused;
    st.fusion.t_c = 19.6;
    st.fusion.room_ok = st.fusion.base_ok = true;
    st.fusion.t_room = 19.55;
    st.fusion.rh_room = 41;
    st.fusion.t_base_raw = 21.02;
    st.fusion.offset = 1.38;
    st.fusion.t_base_corr = 19.64;
    st.fusion.offset_learned_s = 5.5 * 3600;
    st.fusion.offset_ready = true;
    st.sp = 20.0;
    st.mode = tmc::Mode::Auto;
    st.period = tmc::Period::Comfort;
    st.u = 0.213;
    st.p = 0.24;
    st.i = 0.18;
    st.valve_open = true;
    st.reason = tmc::ValveReason::Pulse;
    st.next_action_s = 102;
    st.billed_today_s = 34 * 60;
    st.pulses_today = 9;
    st.humidity = tmc::HumidityAdvice::Ok;
    st.dew_point_c = 6.1;
    st.time_valid = true;
    st.uptime_ms = (3 * 3600 + 12 * 60) * 1000ULL;
}

int main(int argc, char** argv) {
    if (argc > 1) g_out = argv[1];
    baseSnapshot();
    Settings cfg;
    g_snap.cfg = cfg;
    ui::begin(cfg);
    run(500);
    shot("home_auto");

    // dry air advice
    g_snap.st.humidity = tmc::HumidityAdvice::TooDry;
    g_snap.st.fusion.rh_room = 31;
    shot("home_auto_dry");
    g_snap.st.humidity = tmc::HumidityAdvice::Ok;
    g_snap.st.fusion.rh_room = 41;

    // house empty: VIA -> the 4th button becomes ARRIVO
    g_snap.mode = g_snap.st.mode = tmc::Mode::Away;
    g_snap.st.sp = 7.0;
    g_snap.st.fusion.t_c = 8.4;
    g_snap.st.valve_open = false;
    g_snap.st.next_action_s = UINT32_MAX;
    shot("home_via");

    // ARRIVO pressed: the UI posts Request::Arrive
    g_requests.clear();
    tap(280, 213);
    printf("requests after ARRIVO tap: %zu (type %d)\n", g_requests.size(),
           g_requests.empty() ? -1 : static_cast<int>(g_requests[0].type));
    g_snap.mode = g_snap.st.mode = tmc::Mode::Auto;
    g_snap.st.sp = 20.0;
    g_snap.st.arrival = true;
    g_snap.st.arrival_left_s = 52 * 60;
    g_snap.st.valve_open = true;
    g_snap.st.reason = tmc::ValveReason::Arrival;
    g_snap.st.next_action_s = UINT32_MAX;
    g_snap.st.recovering = true;
    shot("home_arrivo");

    // hidden settings: long-press the clock
    tap(290, 12, 2300);
    static const char* kNames[] = {"menu_1_stato", "menu_2_ora", "menu_3_temperature", "menu_4_orario",
                                   "menu_5_sensori", "menu_6_schermo", "menu_7_avanzate", "menu_8_sistema"};
    for (int p = 0; p < 8; ++p) {
        shot(kNames[p]);
        if (p == 6) {                 // AVANZATE: also the second list page
            tap(240, 213);
            shot("menu_7_avanzate_p2");
        }
        if (p == 7) {                 // SISTEMA: first tap on RIAVVIA asks for confirmation
            tap(80, 173);
            shot("menu_8_sistema_conferma");
        }
        if (p < 7) tap(252, 16);      // ">"
    }
    tap(298, 16);                     // X: back to Home
    shot("home_after_menu");
    printf("requests posted during the run: %zu\n", g_requests.size());
    return 0;
}
