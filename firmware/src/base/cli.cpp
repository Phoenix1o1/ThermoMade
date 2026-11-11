#include "cli.h"
#include <Arduino.h>
#include <time.h>
#include "base_config.h"
#include "light.h"
#include "logger.h"
#include "param_table.h"
#include "radio_base.h"
#include "shared.h"
#include "ui.h"

namespace {

char g_line[128];
size_t g_len = 0;
bool g_overflow = false;

const char* kAlarmNames[] = {"room-lost", "base-lost", "no-sensor", "disagree", "time-unknown", "cont-on", "budget"};
const char* kDayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

void post(ReqType t, int32_t i = 0, double d = 0.0, uint32_t u = 0) {
    Request r;
    r.type = t;
    r.i = i;
    r.d = d;
    r.u = u;
    if (!shared::postRequest(r)) Serial.println("ERR request queue full, try again");
}

bool parseDouble(const char* s, double* out) {
    if (!s) return false;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != 0 || !(v == v)) return false;
    *out = v;
    return true;
}

bool parseLong(const char* s, long* out) {
    if (!s) return false;
    char* end = nullptr;
    const long v = strtol(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = v;
    return true;
}

void help() {
    Serial.println(
        "TermoMade base console\n"
        "  status | params | get <name> | set <name> <value>\n"
        "  mode auto|comfort|night|away|off      sp <degC>      boost <min>|off\n"
        "  arrive   (ARRIVO: back to AUTO + forced heating, see 'get arrival_force_min')\n"
        "  time YYYY-MM-DD HH:MM[:SS]            (local time, Europe/Rome)\n"
        "  sched | sched default | sched <all|wk|we|mon..sun> HH:MM=C|N [HH:MM=C|N ...]\n"
        "  pair | forget-nodes\n"
        "  touch on|off | cal <xmin> <xmax> <ymin> <ymax> <swap> <invx> <invy>\n"
        "  screen <timeout_s> [max 10-255] [min 1-255] [auto 0/1]\n"
        "  reset-learning duty|offset|all | save | reboot | factory YES");
}

void printSchedule(const tmc::WeekSchedule& w) {
    for (int d = 0; d < 7; ++d) {
        Serial.printf("  %s:", kDayNames[d]);
        for (int k = 0; k < w.day[d].n; ++k) {
            const tmc::Slot& s = w.day[d].slot[k];
            Serial.printf(" %02u:%02u=%c", s.start_min / 60, s.start_min % 60,
                          s.period == tmc::Period::Comfort ? 'C' : 'N');
        }
        Serial.println();
    }
}

void status() {
    const Snapshot s = shared::read();
    const tmc::Status& st = s.st;
    const tmc::FusionOut& f = st.fusion;
    Serial.printf("TermoMade base fw %d.%d  uptime %lus  snapshot #%lu\n", TM_FW_MAJOR, TM_FW_MINOR,
                  static_cast<unsigned long>(st.uptime_ms / 1000u), static_cast<unsigned long>(s.seq));
    if (s.time_valid) {
        char b[24];
        const time_t t = static_cast<time_t>(s.unix_time);
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &lt);
        Serial.printf("time   %s (%s)\n", b, s.time_src == TimeSrc::Manual ? "manual/phone" : "node RTC");
    } else {
        Serial.println("time   UNKNOWN (AUTO runs as Comfort; set it with 'time')");
    }
    Serial.printf("mode   %s  period %s%s%s%s  sp %.2f  heating %s\n", tmc::toString(s.mode),
                  st.period == tmc::Period::Comfort ? "Comfort" : "Night", st.preheating ? " +preheat" : "",
                  st.override_active ? " +override" : "", st.boost ? " +BOOST" : "", st.sp,
                  st.heating_enabled ? "enabled" : "DISABLED");
    if (st.boost) Serial.printf("boost  %lu s left\n", static_cast<unsigned long>(st.boost_left_s));
    if (st.arrival) Serial.printf("arrive forced heating, %lu s left\n", static_cast<unsigned long>(st.arrival_left_s));
    Serial.printf("temp   %s %.2f  room %s %.2f rh %.1f  base %s %.2f -> %.2f (offset %+.2f, learned %.1f h%s)%s\n",
                  tmc::toString(f.src), f.t_c, f.room_ok ? "ok" : "--", f.t_room, f.rh_room,
                  f.base_ok ? "ok" : "--", f.t_base_raw, f.t_base_corr, f.offset, f.offset_learned_s / 3600.0,
                  f.offset_ready ? ", in fusion" : "", f.disagree ? "  DISAGREE" : "");
    Serial.printf("pi     u %.4f  P %.4f  I %.4f  D %.4f%s%s  algo %s\n", st.u, st.p, st.i, st.d,
                  st.coast ? "  coast" : "",
                  st.recovering ? (st.recovery == tmc::Recovery::Efficient ? "  recovery-eff" : (st.recovery == tmc::Recovery::Balanced ? "  recovery-bal" : "  recovery-fast")) : "",
                  st.algo_active == tmc::Algo::Classic ? "CLASSIC" : "PULSE");
    Serial.printf("valve  %s (%s)  next %ld s  lockout %lu s  relay %s%s\n", st.valve_open ? "OPEN" : "closed",
                  tmc::toString(st.reason), st.next_action_s == UINT32_MAX ? -1L : static_cast<long>(st.next_action_s),
                  static_cast<unsigned long>(st.lockout_s), s.relay_on ? "ON" : "off", st.limp ? "  LIMP" : "");
    Serial.printf("today  %.1f min open, %lu pulses, open streak %lu s\n", st.billed_today_s / 60.0,
                  static_cast<unsigned long>(st.pulses_today), static_cast<unsigned long>(st.open_streak_s));
    Serial.printf("alarms 0x%02X", st.alarms);
    for (int b = 0; b < 7; ++b)
        if (st.alarms & (1u << b)) Serial.printf(" %s", kAlarmNames[b]);
    Serial.println();
    static const char* hum[] = {"unknown", "ok", "too dry", "too humid"};
    Serial.printf("air    %s, dew point %.1f\n", hum[static_cast<int>(st.humidity) & 3], st.dew_point_c);
    if (s.node_paired)
        Serial.printf("node   paired, last frame %ld s ago, RSSI %d dBm\n",
                      s.node_age_s == UINT32_MAX ? -1L : static_cast<long>(s.node_age_s), s.node_rssi);
    else
        Serial.println("node   NOT paired ('pair' then power the node, or hold its BOOT 3 s)");
    Serial.printf("light  %d %% (raw %u, live %u, range %u bright .. %u dark)  screen %u s, bright %u..%u %s\n",
                  s.light_pct, s.light_raw, light::rawLive(), light::rangeBright(), light::rangeDark(),
                  s.cfg.screen_timeout_s, s.cfg.brightness_min, s.cfg.brightness, s.cfg.auto_brightness ? "auto" : "fixed");
    Serial.printf("misc   ds18b20 %s  sd %s  pairing %s  dropped: req %lu log %lu  heap %lu  reset %s\n",
                  s.ds18b20_present ? "ok" : "MISSING", s.sd_ok ? "ok" : "no", s.pairing_open ? "OPEN" : "closed",
                  static_cast<unsigned long>(s.req_dropped), static_cast<unsigned long>(logger::dropped()),
                  static_cast<unsigned long>(ESP.getFreeHeap()), s.reset_reason);
}

void printParam(const tmc::ControlParams& p, int i) {
    const double v = getParam(p, i);
    if (kParams[i].type == PType::F64) Serial.printf("  %-28s %.4g %s\n", kParams[i].name, v, kParams[i].unit);
    else Serial.printf("  %-28s %.0f %s\n", kParams[i].name, v, kParams[i].unit);
}

bool parseMode(const char* s, tmc::Mode* m) {
    if (!s) return false;
    if (!strcmp(s, "auto")) *m = tmc::Mode::Auto;
    else if (!strcmp(s, "comfort") || !strcmp(s, "giorno")) *m = tmc::Mode::Comfort;
    else if (!strcmp(s, "night") || !strcmp(s, "notte")) *m = tmc::Mode::Night;
    else if (!strcmp(s, "away") || !strcmp(s, "via")) *m = tmc::Mode::Away;
    else if (!strcmp(s, "off") || !strcmp(s, "spento")) *m = tmc::Mode::Off;
    else return false;
    return true;
}

// "all" "wk" (Mon-Fri) "we" (Sat-Sun) or a day name -> bit mask (bit 0 = Monday)
uint8_t parseDays(const char* s) {
    if (!s) return 0;
    if (!strcmp(s, "all")) return 0x7F;
    if (!strcmp(s, "wk")) return 0x1F;
    if (!strcmp(s, "we")) return 0x60;
    for (int d = 0; d < 7; ++d)
        if (!strcmp(s, kDayNames[d])) return static_cast<uint8_t>(1u << d);
    return 0;
}

void cmdSched(char* save) {
    const char* a = strtok_r(nullptr, " ", &save);
    tmc::WeekSchedule w = shared::read().cfg.schedule;
    if (!a) {
        printSchedule(w);
        return;
    }
    if (!strcmp(a, "default")) {
        shared::postSchedule(tmc::WeekSchedule::makeDefault());
        Serial.println("OK default schedule (Comfort 06:30-22:30 every day)");
        return;
    }
    const uint8_t mask = parseDays(a);
    if (!mask) { Serial.println("ERR days: all|wk|we|mon..sun"); return; }
    tmc::DaySchedule ds;
    for (const char* t = strtok_r(nullptr, " ", &save); t; t = strtok_r(nullptr, " ", &save)) {
        unsigned hh = 99, mm = 99;
        char c = 0;
        if (sscanf(t, "%u:%u=%c", &hh, &mm, &c) != 3 || hh > 23 || mm > 59 || (c != 'C' && c != 'N' && c != 'c' && c != 'n')) {
            Serial.printf("ERR bad slot '%s' (use HH:MM=C or HH:MM=N)\n", t);
            return;
        }
        if (ds.n >= tmc::DaySchedule::kMaxSlots) { Serial.println("ERR max 4 slots per day"); return; }
        ds.slot[ds.n].start_min = static_cast<uint16_t>(hh * 60 + mm);
        ds.slot[ds.n].period = (c == 'C' || c == 'c') ? tmc::Period::Comfort : tmc::Period::Night;
        ++ds.n;
    }
    if (ds.n == 0) { Serial.println("ERR give at least one slot"); return; }
    for (int d = 0; d < 7; ++d)
        if (mask & (1u << d)) w.day[d] = ds;
    w.sanitize();
    shared::postSchedule(w);
    Serial.println("OK new schedule:");
    printSchedule(w);
}

void cmdTime(char* save) {
    const char* date = strtok_r(nullptr, " ", &save);
    const char* clock = strtok_r(nullptr, " ", &save);
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    if (!date || !clock || sscanf(date, "%d-%d-%d", &Y, &M, &D) != 3 ||
        sscanf(clock, "%d:%d:%d", &h, &m, &sec) < 2 || Y < 2025 || Y > 2099 || M < 1 || M > 12 || D < 1 ||
        D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59) {
        Serial.println("ERR usage: time YYYY-MM-DD HH:MM[:SS]  (local time)");
        return;
    }
    struct tm lt = {};
    lt.tm_year = Y - 1900;
    lt.tm_mon = M - 1;
    lt.tm_mday = D;
    lt.tm_hour = h;
    lt.tm_min = m;
    lt.tm_sec = sec;
    lt.tm_isdst = -1;   // let the TZ rules decide (CET / CEST)
    const time_t t = mktime(&lt);
    if (t <= 0 || lt.tm_mday != D) { Serial.println("ERR invalid date"); return; }
    post(ReqType::SetTime, 0, 0.0, static_cast<uint32_t>(t));
    Serial.printf("OK time set request: unix %lu\n", static_cast<unsigned long>(t));
}

void execute(char* line) {
    char* save = nullptr;
    const char* cmd = strtok_r(line, " ", &save);
    if (!cmd) return;
    const char* a1 = nullptr;
    const char* a2 = nullptr;
    auto args = [&]() { a1 = strtok_r(nullptr, " ", &save); a2 = strtok_r(nullptr, " ", &save); };

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { help(); return; }
    if (!strcmp(cmd, "status") || !strcmp(cmd, "s")) { status(); return; }
    if (!strcmp(cmd, "params")) {
        const tmc::ControlParams p = shared::read().cfg.params;
        for (int i = 0; i < kParamCount; ++i) printParam(p, i);
        return;
    }
    if (!strcmp(cmd, "get")) {
        args();
        const int i = a1 ? findParam(a1) : -1;
        if (i < 0) { Serial.println("ERR unknown parameter (see 'params')"); return; }
        printParam(shared::read().cfg.params, i);
        return;
    }
    if (!strcmp(cmd, "set")) {
        args();
        const int i = a1 ? findParam(a1) : -1;
        double v = 0;
        if (i < 0 || !parseDouble(a2, &v)) { Serial.println("ERR usage: set <name> <value> (see 'params')"); return; }
        post(ReqType::SetParam, i, v);
        Serial.println("OK (out-of-range values are clamped; check with 'get')");
        return;
    }
    if (!strcmp(cmd, "mode")) {
        args();
        tmc::Mode m;
        if (!parseMode(a1, &m)) { Serial.println("ERR usage: mode auto|comfort|night|away|off"); return; }
        post(ReqType::SetMode, static_cast<int32_t>(m));
        Serial.println("OK");
        return;
    }
    if (!strcmp(cmd, "sp")) {
        args();
        double v = 0;
        if (!parseDouble(a1, &v)) { Serial.println("ERR usage: sp <degC> (5..25)"); return; }
        post(ReqType::SetSetpoint, 0, v);
        Serial.println("OK");
        return;
    }
    if (!strcmp(cmd, "boost")) {
        args();
        long mins = 0;
        if (a1 && !strcmp(a1, "off")) { post(ReqType::StopBoost); Serial.println("OK"); return; }
        if (!parseLong(a1, &mins) || mins < 1 || mins > 240) { Serial.println("ERR usage: boost <1..240 min>|off"); return; }
        post(ReqType::StartBoost, static_cast<int32_t>(mins));
        Serial.println("OK (boost never shortens the 5 min valve pause)");
        return;
    }
    if (!strcmp(cmd, "arrive") || !strcmp(cmd, "arrivo")) {
        post(ReqType::Arrive);
        Serial.println("OK ARRIVO: AUTO + forced heating (ends early within arrival_stop_c of the setpoint)");
        return;
    }
    if (!strcmp(cmd, "time")) { cmdTime(save); return; }
    if (!strcmp(cmd, "sched")) { cmdSched(save); return; }
    if (!strcmp(cmd, "pair")) { radio_base::openPairing(); Serial.println("OK pairing open for 60 s"); return; }
    if (!strcmp(cmd, "forget-nodes")) { radio_base::forgetAll(); Serial.println("OK all nodes forgotten"); return; }
    if (!strcmp(cmd, "touch")) {
        args();
        const bool on = a1 && !strcmp(a1, "on");
        ui::setRawTouchDump(on);
        Serial.printf("OK raw touch dump %s\n", on ? "on (touch the corners)" : "off");
        return;
    }
    if (!strcmp(cmd, "cal")) {
        Request r;
        r.type = ReqType::SetTouchCal;
        for (int k = 0; k < 7; ++k) {
            long v = 0;
            if (!parseLong(strtok_r(nullptr, " ", &save), &v) || v < 0 || v > 4095) {
                Serial.println("ERR usage: cal <xmin> <xmax> <ymin> <ymax> <swap 0/1> <invx 0/1> <invy 0/1>");
                return;
            }
            r.v[k] = static_cast<int16_t>(v);
        }
        if (!shared::postRequest(r)) Serial.println("ERR request queue full");
        else Serial.println("OK");
        return;
    }
    if (!strcmp(cmd, "screen")) {
        args();
        const char* a3 = strtok_r(nullptr, " ", &save);
        const char* a4 = strtok_r(nullptr, " ", &save);
        long to = 0, mx = -1, mn = -1, au = -1;
        if (!parseLong(a1, &to) || to < 10 || to > 255 || (a2 && (!parseLong(a2, &mx) || mx < 10 || mx > 255)) ||
            (a3 && (!parseLong(a3, &mn) || mn < 1 || mn > 255)) || (a4 && (!parseLong(a4, &au) || au < 0 || au > 1))) {
            Serial.println("ERR usage: screen <timeout 10-255 s> [max 10-255] [min 1-255] [auto 0/1]");
            return;
        }
        Request r;
        r.type = ReqType::SetScreen;
        r.i = static_cast<int32_t>(to);
        r.v[0] = static_cast<int16_t>(mx);
        r.v[1] = static_cast<int16_t>(mn);
        r.v[2] = static_cast<int16_t>(au);
        if (shared::postRequest(r)) Serial.println("OK");
        return;
    }
    if (!strcmp(cmd, "reset-learning")) {
        args();
        int32_t bits = 0;
        if (a1 && !strcmp(a1, "duty")) bits = 1;
        else if (a1 && !strcmp(a1, "offset")) bits = 2;
        else if (a1 && !strcmp(a1, "all")) bits = 3;
        if (!bits) { Serial.println("ERR usage: reset-learning duty|offset|all"); return; }
        post(ReqType::ResetLearning, bits);
        Serial.println("OK");
        return;
    }
    if (!strcmp(cmd, "save")) { post(ReqType::SaveNow); Serial.println("OK"); return; }
    if (!strcmp(cmd, "reboot")) { post(ReqType::Reboot); Serial.println("OK rebooting (valve closes, 5 min lockout)"); return; }
    if (!strcmp(cmd, "factory")) {
        args();
        if (!a1 || strcmp(a1, "YES") != 0) { Serial.println("ERR type 'factory YES' to erase all settings and nodes"); return; }
        post(ReqType::FactoryReset);
        Serial.println("OK erasing and rebooting");
        return;
    }
    Serial.printf("ERR unknown command '%s' (help)\n", cmd);
}

}  // namespace

namespace cli {

void loop() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            g_line[g_len] = 0;
            if (g_overflow) Serial.println("ERR line too long");
            else execute(g_line);
            g_len = 0;
            g_overflow = false;
            continue;
        }
        if (g_len + 1 < sizeof g_line) g_line[g_len++] = static_cast<char>(c);
        else g_overflow = true;
    }
}

}  // namespace cli
