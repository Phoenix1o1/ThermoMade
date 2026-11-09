#include "logger.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <atomic>
#include "base_config.h"

namespace {

struct SampleRec {
    uint32_t uptime_s;
    float t_ctrl, t_room, rh, t_base, t_base_corr, offset, sp, u, p, i, dew_c;
    uint32_t billed_today_s, pulses_today, node_age_s;
    uint16_t alarms;
    uint8_t src, mode, period, boost, valve, reason, limp, hum, arrival;
    int8_t rssi, light;
    bool room_ok, base_ok;
};

enum class Kind : uint8_t { Sample, Event, Text };

struct LogItem {
    Kind kind;
    uint32_t unix;
    union {
        SampleRec s;
        tmc::Event e;
        char text[96];
    };
    LogItem() : kind(Kind::Text), unix(0), text{} {}
};

QueueHandle_t g_q = nullptr;
SPIClass g_spi(VSPI);
volatile bool g_ok = false;
std::atomic<uint32_t> g_dropped{0};
char g_last_day[16] = "";

constexpr const char* kSampleHeader =
    "unix,local,uptime_s,t_ctrl,src,t_room,rh,t_base,t_base_corr,offset,sp,mode,period,boost,u,p,i,valve,reason,"
    "billed_today_s,pulses_today,alarms,limp,humidity,dew_c,rssi,node_age_s,arrival,light_pct\n";
constexpr const char* kEventHeader = "unix,local,uptime_s,event,a,b,x,y\n";

const char* eventName(tmc::EventType t) {
    switch (t) {
        case tmc::EventType::ValveOpen: return "valve_open";
        case tmc::EventType::ValveClose: return "valve_close";
        case tmc::EventType::ModeChange: return "mode";
        case tmc::EventType::SetpointChange: return "setpoint";
        case tmc::EventType::SourceChange: return "source";
        case tmc::EventType::LimpEnter: return "limp_on";
        case tmc::EventType::LimpExit: return "limp_off";
        case tmc::EventType::AlarmOn: return "alarm_on";
        case tmc::EventType::AlarmOff: return "alarm_off";
        case tmc::EventType::DayRollover: return "day";
        case tmc::EventType::Boot: return "boot";
        case tmc::EventType::Arrival: return "arrival";
    }
    return "?";
}

bool mount() {
    g_spi.begin(cfg::kPinSdSck, cfg::kPinSdMiso, cfg::kPinSdMosi, cfg::kPinSdCs);
    if (!SD.begin(cfg::kPinSdCs, g_spi, 10000000, "/sd", 2)) {
        SD.end();
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return false;
    }
    if (!SD.exists("/log")) SD.mkdir("/log");
    Serial.printf("[log] SD mounted, %llu MB\n", static_cast<unsigned long long>(SD.cardSize() >> 20));
    return true;
}

void unmount() {
    SD.end();
    g_ok = false;
    Serial.println("[log] SD write failed: logging paused (retry every minute)");
}

// "2026-11-09 21:30:00" (local) or "" when the time is unknown.
void localStamp(uint32_t unix, char* out, size_t n) {
    if (unix == 0) { out[0] = 0; return; }
    const time_t t = static_cast<time_t>(unix);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &lt);
}

bool appendLine(const char* path, const char* header, const char* line) {
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    bool ok = true;
    if (f.size() == 0 && header) ok = f.print(header) == strlen(header);
    ok = ok && f.print(line) == strlen(line);
    f.close();
    return ok;
}

// Keep 10 % of the card free: delete the oldest daily files (once per day).
void housekeeping() {
    const uint64_t total = SD.totalBytes();
    if (total == 0) return;
    for (int pass = 0; pass < 10 && SD.usedBytes() > total / 10 * 9; ++pass) {
        File dir = SD.open("/log");
        if (!dir) return;
        char oldest[32] = "";
        for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
            const char* n = e.name();
            if (!e.isDirectory() && strlen(n) == 14 && n[0] == '2' && strcmp(n + 10, ".csv") == 0 &&
                (oldest[0] == 0 || strcmp(n, oldest) < 0))
                strncpy(oldest, n, sizeof oldest - 1);
            e.close();
        }
        dir.close();
        if (!oldest[0] || strcmp(oldest, g_last_day) >= 0) return;   // never delete today's file
        char path[48];
        snprintf(path, sizeof path, "/log/%s", oldest);
        Serial.printf("[log] card nearly full: removing %s\n", path);
        SD.remove(path);
    }
}

void fmtFloat(char* out, size_t n, float v, bool valid, int dec) {
    if (!valid) out[0] = 0;
    else snprintf(out, n, "%.*f", dec, static_cast<double>(v));
}

bool write(const LogItem& it) {
    char local[24];
    localStamp(it.unix, local, sizeof local);
    char line[320];
    if (it.kind == Kind::Sample) {
        const SampleRec& s = it.s;
        char day[16] = "notime";
        if (it.unix) memcpy(day, local, 10), day[10] = 0;
        if (strcmp(day, g_last_day) != 0) {
            strncpy(g_last_day, day, sizeof g_last_day - 1);
            if (it.unix) housekeeping();
        }
        char tc[12], tr[12], rh[12], tb[12], tbc[12], dew[12], lp[8] = "";
        if (s.light >= 0) snprintf(lp, sizeof lp, "%d", s.light);
        fmtFloat(tc, sizeof tc, s.t_ctrl, s.src != 0, 2);
        fmtFloat(tr, sizeof tr, s.t_room, s.room_ok, 2);
        fmtFloat(rh, sizeof rh, s.rh, s.rh >= 0.0f, 1);
        fmtFloat(tb, sizeof tb, s.t_base, s.base_ok, 2);
        fmtFloat(tbc, sizeof tbc, s.t_base_corr, s.base_ok, 2);
        fmtFloat(dew, sizeof dew, s.dew_c, s.rh >= 0.0f, 1);
        snprintf(line, sizeof line,
                 "%lu,%s,%lu,%s,%u,%s,%s,%s,%s,%.3f,%.2f,%u,%u,%u,%.4f,%.4f,%.4f,%u,%u,%lu,%lu,%u,%u,%u,%s,%d,%ld,%u,%s\n",
                 static_cast<unsigned long>(it.unix), local, static_cast<unsigned long>(s.uptime_s), tc, s.src, tr, rh,
                 tb, tbc, static_cast<double>(s.offset), static_cast<double>(s.sp), s.mode, s.period, s.boost,
                 static_cast<double>(s.u), static_cast<double>(s.p), static_cast<double>(s.i), s.valve, s.reason,
                 static_cast<unsigned long>(s.billed_today_s), static_cast<unsigned long>(s.pulses_today), s.alarms,
                 s.limp, s.hum, dew, s.rssi, s.node_age_s == UINT32_MAX ? -1L : static_cast<long>(s.node_age_s),
                 s.arrival, lp);
        char path[40];
        snprintf(path, sizeof path, "/log/%s.csv", day);
        return appendLine(path, kSampleHeader, line);
    }
    if (it.kind == Kind::Event) {
        const tmc::Event& e = it.e;
        snprintf(line, sizeof line, "%lu,%s,%lu,%s,%u,%u,%.3f,%.3f\n", static_cast<unsigned long>(it.unix), local,
                 static_cast<unsigned long>(e.uptime_ms / 1000u), eventName(e.type), e.a, e.b, static_cast<double>(e.x),
                 static_cast<double>(e.y));
        return appendLine("/log/events.csv", kEventHeader, line);
    }
    snprintf(line, sizeof line, "%lu,%s,%lu,text,0,0,0,0,%s\n", static_cast<unsigned long>(it.unix), local,
             static_cast<unsigned long>(millis() / 1000u), it.text);
    return appendLine("/log/events.csv", kEventHeader, line);
}

void task(void*) {
    uint32_t last_try = millis();
    LogItem it;
    for (;;) {
        const bool got = xQueueReceive(g_q, &it, pdMS_TO_TICKS(5000)) == pdTRUE;
        if (!g_ok && millis() - last_try >= 60000u) {
            last_try = millis();
            g_ok = mount();
        }
        if (!got || !g_ok) continue;   // no card: the line is dropped
        if (!write(it)) {
            unmount();
            last_try = millis();
        }
    }
}

void post(const LogItem& it) {
    if (!g_q || xQueueSend(g_q, &it, 0) != pdTRUE) ++g_dropped;
}
}  // namespace

namespace logger {

void begin() {
    g_q = xQueueCreate(24, sizeof(LogItem));
    g_ok = mount();
    if (!g_ok) Serial.println("[log] no SD card: logging disabled (retry every minute)");
    xTaskCreatePinnedToCore(task, "logger", 6144, nullptr, 1, nullptr, 0);
}

void sample(const Snapshot& snap) {
    const tmc::Status& st = snap.st;
    LogItem it;
    it.kind = Kind::Sample;
    it.unix = snap.time_valid ? snap.unix_time : 0;
    SampleRec& s = it.s;
    s = SampleRec{};
    s.uptime_s = static_cast<uint32_t>(st.uptime_ms / 1000u);
    s.src = static_cast<uint8_t>(st.fusion.src);
    s.t_ctrl = static_cast<float>(st.fusion.t_c);
    s.room_ok = st.fusion.room_ok;
    s.base_ok = st.fusion.base_ok;
    s.t_room = static_cast<float>(st.fusion.t_room);
    s.rh = static_cast<float>(st.fusion.rh_room);
    s.t_base = static_cast<float>(st.fusion.t_base_raw);
    s.t_base_corr = static_cast<float>(st.fusion.t_base_corr);
    s.offset = static_cast<float>(st.fusion.offset);
    s.sp = static_cast<float>(st.sp);
    s.mode = static_cast<uint8_t>(snap.mode);
    s.period = static_cast<uint8_t>(st.period);
    s.boost = st.boost ? 1 : 0;
    s.u = static_cast<float>(st.u);
    s.p = static_cast<float>(st.p);
    s.i = static_cast<float>(st.i);
    s.valve = st.valve_open ? 1 : 0;
    s.reason = static_cast<uint8_t>(st.reason);
    s.billed_today_s = st.billed_today_s;
    s.pulses_today = st.pulses_today;
    s.alarms = st.alarms;
    s.limp = st.limp ? 1 : 0;
    s.hum = static_cast<uint8_t>(st.humidity);
    s.dew_c = static_cast<float>(st.dew_point_c);
    s.rssi = snap.node_rssi;
    s.node_age_s = snap.node_age_s;
    s.arrival = st.arrival ? 1 : 0;
    s.light = snap.light_pct;
    post(it);
}

void event(const tmc::Event& e, uint32_t unix_utc) {
    LogItem it;
    it.kind = Kind::Event;
    it.unix = unix_utc;
    it.e = e;
    post(it);
}

void text(const char* line, uint32_t unix_utc) {
    LogItem it;
    it.kind = Kind::Text;
    it.unix = unix_utc;
    strncpy(it.text, line, sizeof it.text - 1);
    it.text[sizeof it.text - 1] = 0;
    // CSV safety: no commas or newlines inside the free-text column
    for (char* c = it.text; *c; ++c)
        if (*c == ',' || *c == '\n' || *c == '\r') *c = ';';
    post(it);
}

bool sdOk() { return g_ok; }
uint32_t dropped() { return g_dropped; }

}  // namespace logger
