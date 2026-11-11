#include "web.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cmath>
#include "base_config.h"
#include "param_table.h"
#include "shared.h"
#include "tm_util.h"
#include "web_page.h"

namespace {
AsyncWebServer g_srv(80);

// request argument from the form body or the query string; nullptr if absent
const char* arg(AsyncWebServerRequest* rq, const char* name) {
    if (rq->hasParam(name, true)) return rq->getParam(name, true)->value().c_str();
    if (rq->hasParam(name)) return rq->getParam(name)->value().c_str();
    return nullptr;
}

bool num(const char* s, double* out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != 0 || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

void reply(AsyncWebServerRequest* rq, bool ok, const char* msg) {
    char b[96];
    snprintf(b, sizeof b, "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", msg);
    rq->send(ok ? 200 : 400, "application/json", b);
}

bool post(ReqType t, int32_t i = 0, double d = 0.0, uint32_t u = 0) {
    Request r;
    r.type = t;
    r.i = i;
    r.d = d;
    r.u = u;
    return shared::postRequest(r);
}

// JSON number, or null for NaN/inf (printf would write "nan", which is not JSON)
void jnum(AsyncResponseStream* o, const char* key, double v, int dec, bool valid = true) {
    if (valid && std::isfinite(v)) o->printf("\"%s\":%.*f,", key, dec, v);
    else o->printf("\"%s\":null,", key);
}

void handleStatus(AsyncWebServerRequest* rq) {
    const Snapshot s = shared::read();
    const tmc::Status& st = s.st;
    const tmc::FusionOut& f = st.fusion;
    AsyncResponseStream* o = rq->beginResponseStream("application/json");
    o->addHeader("Cache-Control", "no-store");
    o->print("{");
    jnum(o, "t", f.t_c, 2, f.src != tmc::TempSource::None);
    jnum(o, "t_room", f.t_room, 2, f.room_ok);
    jnum(o, "rh", f.rh_room, 1, f.rh_room >= 0);
    jnum(o, "t_base", f.t_base_corr, 2, f.base_ok);
    jnum(o, "offset", f.offset, 2);
    jnum(o, "sp", st.sp, 2);
    jnum(o, "u", st.u, 3);
    jnum(o, "p", st.p, 3);
    jnum(o, "i", st.i, 3);
    jnum(o, "dew", st.dew_point_c, 1, f.rh_room >= 0);
    o->printf("\"src\":\"%s\",\"mode\":%d,\"period\":%d,\"heating\":%s,\"valve\":%s,\"reason\":\"%s\",",
              tmc::toString(f.src), static_cast<int>(s.mode), static_cast<int>(st.period),
              st.heating_enabled ? "true" : "false", st.valve_open ? "true" : "false", tmc::toString(st.reason));
    o->printf("\"next_s\":%ld,\"lock_s\":%lu,\"boost\":%s,\"boost_left_s\":%lu,\"arrival\":%s,\"arrival_left_s\":%lu,",
              st.next_action_s == UINT32_MAX ? -1L : static_cast<long>(st.next_action_s),
              static_cast<unsigned long>(st.lockout_s), st.boost ? "true" : "false",
              static_cast<unsigned long>(st.boost_left_s), st.arrival ? "true" : "false",
              static_cast<unsigned long>(st.arrival_left_s));
    o->printf("\"preheat\":%s,\"override\":%s,\"waiting\":%s,\"limp\":%s,\"alarms\":%u,\"humidity\":%d,",
              st.preheating ? "true" : "false", st.override_active ? "true" : "false",
              st.waiting_sensors ? "true" : "false", st.limp ? "true" : "false", st.alarms,
              static_cast<int>(st.humidity));
    o->printf("\"time_valid\":%s,\"unix\":%lu,\"hh\":%u,\"mm\":%u,\"billed_s\":%lu,\"pulses\":%lu,",
              s.time_valid ? "true" : "false", static_cast<unsigned long>(s.unix_time), s.hour, s.minute,
              static_cast<unsigned long>(st.billed_today_s), static_cast<unsigned long>(st.pulses_today));
    o->printf("\"node\":%s,\"node_age_s\":%ld,\"rssi\":%d,\"light\":%d,\"sd\":%s,\"relay\":%s,",
              s.node_paired ? "true" : "false", s.node_age_s == UINT32_MAX ? -1L : static_cast<long>(s.node_age_s),
              s.node_rssi, s.light_pct, s.sd_ok ? "true" : "false", s.relay_on ? "true" : "false");
    // simple schedule view: first Comfort / following Night start of Monday
    int day = -1, night = -1;
    const tmc::DaySchedule& d0 = s.cfg.schedule.day[0];
    for (int k = 0; k < d0.n; ++k)
        if (d0.slot[k].period == tmc::Period::Comfort && day < 0) day = d0.slot[k].start_min;
    for (int k = 0; k < d0.n; ++k)
        if (d0.slot[k].period == tmc::Period::Night && d0.slot[k].start_min > day && night < 0) night = d0.slot[k].start_min;
    o->printf("\"sched_day\":%d,\"sched_night\":%d,\"scr_timeout\":%u,\"scr_max\":%u,\"scr_min\":%u,\"scr_auto\":%u,",
              day, night, s.cfg.screen_timeout_s, s.cfg.brightness, s.cfg.brightness_min, s.cfg.auto_brightness);
    o->printf("\"fw\":\"%d.%d\",\"uptime_s\":%lu,\"reset\":\"%s\"}", TM_FW_MAJOR, TM_FW_MINOR,
              static_cast<unsigned long>(st.uptime_ms / 1000u), s.reset_reason);
    rq->send(o);
}

void handleParams(AsyncWebServerRequest* rq) {
    const tmc::ControlParams p = shared::read().cfg.params;
    AsyncResponseStream* o = rq->beginResponseStream("application/json");
    o->addHeader("Cache-Control", "no-store");
    o->print("[");
    for (int i = 0; i < kParamCount; ++i) {
        const ParamDesc& d = kParams[i];
        o->printf("%s{\"name\":\"%s\",\"value\":%.6g,\"unit\":\"%s\",\"step\":%.6g,\"kind\":%d}", i ? "," : "", d.name,
                  getParam(p, i), d.unit, d.step, (d.type == PType::Enum || d.type == PType::Bool) ? 1 : 0);
    }
    o->print("]");
    rq->send(o);
}

void handleSet(AsyncWebServerRequest* rq) {
    const char* name = arg(rq, "name");
    double v = 0;
    const int idx = name ? findParam(name) : -1;
    if (idx < 0) return reply(rq, false, "unknown parameter");
    if (!num(arg(rq, "value"), &v)) return reply(rq, false, "bad value");
    reply(rq, post(ReqType::SetParam, idx, v), "queued (clamped to the allowed range)");
}

void handleCmd(AsyncWebServerRequest* rq) {
    const char* c = arg(rq, "c");
    double v = 0;
    const bool hv = num(arg(rq, "v"), &v);
    if (!c) return reply(rq, false, "missing c");
    bool ok = false;
    if (!strcmp(c, "mode") && hv && v >= 0 && v <= 4) ok = post(ReqType::SetMode, static_cast<int32_t>(v));
    else if (!strcmp(c, "sp") && hv) ok = post(ReqType::SetSetpoint, 0, v);
    else if (!strcmp(c, "adj") && hv && v >= -5 && v <= 5) ok = post(ReqType::AdjustSetpoint, 0, v);
    else if (!strcmp(c, "boost") && hv && v >= 1 && v <= 240) ok = post(ReqType::StartBoost, static_cast<int32_t>(v));
    else if (!strcmp(c, "stop")) ok = post(ReqType::StopBoost);
    else if (!strcmp(c, "arrive")) ok = post(ReqType::Arrive);
    else if (!strcmp(c, "save")) ok = post(ReqType::SaveNow);
    else if (!strcmp(c, "time") && hv && v > 0 && v < 4294967295.0 && tmu::plausibleUnix(static_cast<uint32_t>(v)))
        ok = post(ReqType::SetTime, 0, 0.0, static_cast<uint32_t>(v));
    else if (!strcmp(c, "sched")) {      // one rule for all days: v = Comfort start, v2 = Night start (minutes)
        double n = 0;
        if (hv && num(arg(rq, "v2"), &n) && v >= 0 && v < 1440 && n >= 0 && n < 1440 && static_cast<int>(v) != static_cast<int>(n)) {
            tmc::WeekSchedule w;
            for (auto& d : w.day) {
                d.n = 2;
                d.slot[0] = {static_cast<uint16_t>(v), tmc::Period::Comfort};
                d.slot[1] = {static_cast<uint16_t>(n), tmc::Period::Night};
            }
            w.sanitize();
            shared::postSchedule(w);
            ok = true;
        }
    } else if (!strcmp(c, "screen")) {   // v = timeout s, v2 = max, v3 = min, v4 = auto (each optional)
        Request r;
        r.type = ReqType::SetScreen;
        double x = 0;
        r.i = hv ? static_cast<int32_t>(v) : 0;
        r.v[0] = num(arg(rq, "v2"), &x) ? static_cast<int16_t>(x) : -1;
        r.v[1] = num(arg(rq, "v3"), &x) ? static_cast<int16_t>(x) : -1;
        r.v[2] = num(arg(rq, "v4"), &x) ? static_cast<int16_t>(x != 0) : -1;
        ok = shared::postRequest(r);
    }
    reply(rq, ok, ok ? "queued" : "bad command or value");
}
}  // namespace

namespace web {

void begin() {
    g_srv.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) { rq->send(200, "text/html; charset=utf-8", kWebPage); });
    g_srv.on("/api/status", HTTP_GET, handleStatus);
    g_srv.on("/api/params", HTTP_GET, handleParams);
    g_srv.on("/api/set", HTTP_POST, handleSet);
    g_srv.on("/api/cmd", HTTP_POST, handleCmd);
    g_srv.onNotFound([](AsyncWebServerRequest* rq) { rq->send(404, "text/plain", "not found"); });
    g_srv.begin();
    Serial.println("[web] http://192.168.4.1");
}

}  // namespace web
