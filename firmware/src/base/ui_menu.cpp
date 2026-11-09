#include "ui_menu.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>
#include "base_config.h"
#include "light.h"
#include "logger.h"
#include "param_table.h"
#include "radio_base.h"

namespace {

lgfx::LGFX_Device* L = nullptr;
bool g_open = false;

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;
constexpr uint16_t kDim = 0x8410;
constexpr uint16_t kAccent = 0xFD20;
constexpr uint16_t kBtn = 0x2945;
constexpr uint16_t kBtnOn = 0x04B3;
constexpr uint16_t kAlarm = 0xC000;
constexpr uint16_t kHead = 0x18E3;

enum Page : uint8_t { kStatus, kTime, kTemps, kSched, kSensors, kScreen, kAdvanced, kSystem, kPages };
const char* const kTitles[kPages] = {"STATO", "DATA E ORA", "TEMPERATURE", "ORARIO",
                                     "SENSORI", "SCHERMO", "AVANZATE", "SISTEMA"};

enum class Kind : uint8_t { Empty, Text, Adjust, Buttons, Big };

// One screen row (40 px). Text: two lines. Adjust: label, value, [-] [+]. Buttons: one wide or
// two half buttons. Big: one large line with a highlighted part (time editor).
struct Row {
    Kind kind = Kind::Empty;
    char a[64] = "";
    char b[64] = "";
    uint8_t hot = 0;          // Buttons: bit0/1 = left/right teal (active), bit2/3 = left/right red (confirm)
    int8_t sel0 = -1, sel1 = -1;
    uint16_t col = kFg;
};
constexpr int kRows = 5;
constexpr int kRowY0 = 36;
constexpr int kRowH = 40;
constexpr int kAdjMinusX = 208, kAdjPlusX = 264;

// press codes
constexpr int kMinus = -1, kPlus = 1, kLeft = 10, kRight = 11;

Page g_page = kStatus;
Row g_shown[kRows];
bool g_shown_valid[kRows] = {};

// pending parameter edit (shown until the snapshot catches up)
int g_pend_idx = -1;
double g_pend_val = 0.0;
uint32_t g_pend_until = 0;

// two-tap confirmation
int g_confirm_key = -1;
uint32_t g_confirm_until = 0;

// short feedback line ("Ora impostata!") per page
char g_note[52] = "";
uint32_t g_note_until = 0;
Page g_note_page = kStatus;

struct TimeEdit { int Y = 2026, M = 1, D = 1, h = 12, m = 0, sel = 0; } g_te;
struct SchedEdit { int day = 390, night = 1350; bool uniform = true; bool touched = false; } g_se;
struct ScreenEdit { int timeout = 30, bmax = 180, bmin = 12, autob = 1; uint32_t until = 0; } g_sc;
int g_adv_off = 0;

// ------------------------------------------------------------------ helpers
bool active(uint32_t until) { return static_cast<int32_t>(until - millis()) > 0; }

void note(const char* txt) {
    strncpy(g_note, txt, sizeof g_note - 1);
    g_note_until = millis() + 3000;
    g_note_page = g_page;
}
const char* noteFor(Page p) { return (g_note_page == p && active(g_note_until)) ? g_note : nullptr; }

void post(ReqType t, int32_t i = 0, double d = 0.0, uint32_t u = 0) {
    Request r;
    r.type = t;
    r.i = i;
    r.d = d;
    r.u = u;
    if (!shared::postRequest(r)) note("Coda piena: riprova");
}

int pidx(const char* name) {
    const int i = findParam(name);
    return i;   // names are compile-time constants of this file: always found
}

double paramValue(const Snapshot& s, int idx) {
    if (idx == g_pend_idx && active(g_pend_until)) return g_pend_val;
    return getParam(s.cfg.params, idx);
}

void fmtParam(char* out, size_t n, int idx, double v) {
    const ParamDesc& d = kParams[idx];
    if (d.type == PType::Enum || d.type == PType::Bool) {
        // unit holds "0=eff 1=fast": print the word for the current value
        char key[8];
        snprintf(key, sizeof key, "%d=", static_cast<int>(v));
        const char* p = strstr(d.unit, key);
        if (p) {
            p += strlen(key);
            size_t k = 0;
            while (p[k] && p[k] != ' ' && k + 1 < n) ++k;
            snprintf(out, n, "%d (%.*s)", static_cast<int>(v), static_cast<int>(k), p);
        } else {
            snprintf(out, n, "%d", static_cast<int>(v));
        }
        return;
    }
    if (d.type == PType::F64) {
        const int dec = d.step >= 1.0 ? 0 : (d.step >= 0.1 ? 1 : (d.step >= 0.01 ? 2 : 3));
        snprintf(out, n, "%.*f %s", dec, v, d.unit);
    } else {
        snprintf(out, n, "%.0f %s", v, d.unit);
    }
}

void adjustParam(const Snapshot& s, int idx, int dir) {
    const ParamDesc& d = kParams[idx];
    double v = paramValue(s, idx);
    if (d.type == PType::Enum || d.type == PType::Bool) {
        const double maxv = d.type == PType::Bool ? 1.0 : d.max_enum;
        v += dir;
        if (v < 0) v = maxv;
        if (v > maxv) v = 0;
    } else {
        v = std::round((v + dir * d.step) / d.step) * d.step;
        if (v < 0) v = 0;
    }
    // show exactly what the controller will keep: apply the same limits locally
    tmc::ControlParams q = s.cfg.params;
    setParam(q, idx, v);
    tmc::sanitize(q);
    g_pend_val = getParam(q, idx);
    g_pend_idx = idx;
    g_pend_until = millis() + 2500;
    post(ReqType::SetParam, idx, g_pend_val);
}

void adjustRow(Row& r, const Snapshot& s, const char* label, int idx) {
    r.kind = Kind::Adjust;
    snprintf(r.a, sizeof r.a, "%s", label);
    fmtParam(r.b, sizeof r.b, idx, paramValue(s, idx));
}

void buttons(Row& r, const char* left, const char* right = "", uint8_t hot = 0) {
    r.kind = Kind::Buttons;
    snprintf(r.a, sizeof r.a, "%s", left);
    snprintf(r.b, sizeof r.b, "%s", right);
    r.hot = hot;
}

// confirm-protected button: label becomes "CONFERMI?" (red) after the first tap
int key(int row, int side) { return static_cast<int>(g_page) * 100 + row * 10 + side; }
bool confirming(int k) { return g_confirm_key == k && active(g_confirm_until); }
const char* confirmLabel(int row, int side, const char* label) { return confirming(key(row, side)) ? "CONFERMI?" : label; }
uint8_t confirmHot(int row, int side) { return confirming(key(row, side)) ? static_cast<uint8_t>(side ? 8 : 4) : 0; }
// true = execute now; false = first tap, armed
bool confirmTap(int row, int side) {
    const int k = key(row, side);
    if (confirming(k)) {
        g_confirm_key = -1;
        return true;
    }
    g_confirm_key = k;
    g_confirm_until = millis() + 3000;
    return false;
}

void fmtUptime(char* out, size_t n, uint64_t ms) {
    const uint32_t s = static_cast<uint32_t>(ms / 1000u);
    if (s >= 86400u) snprintf(out, n, "%lug %luh", static_cast<unsigned long>(s / 86400u), static_cast<unsigned long>((s % 86400u) / 3600u));
    else snprintf(out, n, "%luh %02lum", static_cast<unsigned long>(s / 3600u), static_cast<unsigned long>((s % 3600u) / 60u));
}

void fmtLocal(char* out, size_t n, uint32_t unix_s) {
    const time_t t = static_cast<time_t>(unix_s);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, n, "%d/%m/%Y %H:%M", &lt);
}

int daysInMonth(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

int wrap(int v, int lo, int hi) { return v < lo ? hi : (v > hi ? lo : v); }

// --------------------------------------------------------------- page entry
tmc::WeekSchedule simpleWeek(int day_start, int night_start) {
    tmc::WeekSchedule w;
    for (auto& d : w.day) {
        d.n = 2;
        d.slot[0] = {static_cast<uint16_t>(day_start), tmc::Period::Comfort};
        d.slot[1] = {static_cast<uint16_t>(night_start), tmc::Period::Night};
    }
    w.sanitize();
    return w;
}

void enterPage(const Snapshot& s) {
    g_confirm_key = -1;
    for (bool& v : g_shown_valid) v = false;
    if (g_page == kTime) {
        struct tm lt = {};
        if (s.time_valid) {
            const time_t t = static_cast<time_t>(s.unix_time);
            localtime_r(&t, &lt);
            g_te.Y = lt.tm_year + 1900;
            g_te.M = lt.tm_mon + 1;
            g_te.D = lt.tm_mday;
            g_te.h = lt.tm_hour;
            g_te.m = lt.tm_min;
        } else {
            // no clock: start from the firmware build date (closer than 2026-01-01)
            static const char* kMon = "JanFebMarAprMayJunJulAugSepOctNovDec";
            char mon[4] = {};
            int d = 1, y = 2026;
            if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) == 3) {
                const char* p = strstr(kMon, mon);
                g_te.M = p ? static_cast<int>((p - kMon) / 3) + 1 : 1;
                g_te.D = d;
                g_te.Y = y;
            }
            g_te.h = 12;
            g_te.m = 0;
        }
        g_te.sel = 3;   // hours: the field you usually fix
    } else if (g_page == kSched) {
        // read the comfort / night start of Monday; check that one simple rule describes the week
        const tmc::WeekSchedule& w = s.cfg.schedule;
        int day = -1, night = -1;
        for (int k = 0; k < w.day[0].n; ++k)
            if (w.day[0].slot[k].period == tmc::Period::Comfort && day < 0) day = w.day[0].slot[k].start_min;
        for (int k = 0; k < w.day[0].n; ++k)
            if (w.day[0].slot[k].period == tmc::Period::Night && w.day[0].slot[k].start_min > day && night < 0)
                night = w.day[0].slot[k].start_min;
        g_se.day = day >= 0 ? day : 390;
        g_se.night = night >= 0 ? night : 1350;
        const tmc::WeekSchedule simple = simpleWeek(g_se.day, g_se.night);
        g_se.uniform = true;
        for (uint8_t d = 0; d < 7 && g_se.uniform; ++d)
            for (uint16_t t = 0; t < 1440; t += 15)
                if (w.periodAt(d, t) != simple.periodAt(d, t)) { g_se.uniform = false; break; }
        g_se.touched = false;
    } else if (g_page == kScreen) {
        g_sc.until = 0;
    }
}

// ------------------------------------------------------------------- pages
void buildStatus(const Snapshot& s, Row* r) {
    const tmc::Status& st = s.st;
    const tmc::FusionOut& f = st.fusion;
    for (int i = 0; i < kRows; ++i) r[i].kind = Kind::Text;
    snprintf(r[0].a, sizeof r[0].a, "T %.2f %s  stanza %.2f  CYD %.2f", f.t_c, tmc::toString(f.src), f.t_room,
             f.t_base_corr);
    snprintf(r[0].b, sizeof r[0].b, "sp %.2f u %.3f P %.3f I %.3f%s%s", st.sp, st.u, st.p, st.i, st.coast ? " cst" : "",
             st.recovering ? (st.recovery == tmc::Recovery::Efficient ? " eff" : " fast") : "");
    char nx[12] = "--";
    if (st.next_action_s != UINT32_MAX) snprintf(nx, sizeof nx, "%lus", static_cast<unsigned long>(st.next_action_s));
    snprintf(r[1].a, sizeof r[1].a, "valvola %s (%s)  prossima %s", st.valve_open ? "APERTA" : "chiusa",
             tmc::toString(st.reason), nx);
    snprintf(r[1].b, sizeof r[1].b, "blocco %lus  oggi %lu min, %lu impulsi", static_cast<unsigned long>(st.lockout_s),
             static_cast<unsigned long>(st.billed_today_s / 60u), static_cast<unsigned long>(st.pulses_today));
    snprintf(r[2].a, sizeof r[2].a, "allarmi 0x%02X%s%s%s", st.alarms, st.limp ? " LIMP" : "",
             st.boost ? " BOOST" : "", st.arrival ? " ARRIVO" : "");
    snprintf(r[2].b, sizeof r[2].b, "UR %.0f%%  rugiada %.1f C  algo %s", f.rh_room, st.dew_point_c,
             st.algo_active == tmc::Algo::Classic ? "CLASSIC" : "PULSE");
    if (s.node_paired)
        snprintf(r[3].a, sizeof r[3].a, "nodo stanza: %lds fa, %d dBm", s.node_age_s == UINT32_MAX ? -1L : static_cast<long>(s.node_age_s),
                 s.node_rssi);
    else snprintf(r[3].a, sizeof r[3].a, "nodo stanza: NON accoppiato");
    char t[24] = "NON IMPOSTATA";
    if (s.time_valid) fmtLocal(t, sizeof t, s.unix_time);
    snprintf(r[3].b, sizeof r[3].b, "ora %s (%s)", t,
             s.time_src == TimeSrc::Manual ? "manuale" : (s.time_src == TimeSrc::NodeRtc ? "RTC nodo" : "-"));
    char up[16];
    fmtUptime(up, sizeof up, st.uptime_ms);
    snprintf(r[4].a, sizeof r[4].a, "luce %d%% (%u)  rele %s  SD %s", s.light_pct, s.light_raw, s.relay_on ? "ON" : "off",
             s.sd_ok ? "ok" : "NO");
    snprintf(r[4].b, sizeof r[4].b, "fw %d.%d  acceso da %s  reset %s", TM_FW_MAJOR, TM_FW_MINOR, up, s.reset_reason);
}

void buildTime(const Snapshot& s, Row* r) {
    r[0].kind = Kind::Text;
    if (s.time_valid) {
        char t[24];
        fmtLocal(t, sizeof t, s.unix_time);
        snprintf(r[0].a, sizeof r[0].a, "Ora attuale: %s (%s)", t, s.time_src == TimeSrc::Manual ? "manuale" : "RTC nodo");
    } else {
        snprintf(r[0].a, sizeof r[0].a, "Ora attuale: NON IMPOSTATA");
        r[0].col = kAccent;
    }
    const char* n = noteFor(kTime);
    snprintf(r[0].b, sizeof r[0].b, "%s", n ? n : "Scegli il campo con < >, cambia con - +");
    r[1].kind = Kind::Big;
    snprintf(r[1].a, sizeof r[1].a, "%02d/%02d/%04d  %02d:%02d", g_te.D, g_te.M, g_te.Y, g_te.h, g_te.m);
    static const int8_t kSel[5][2] = {{0, 2}, {3, 5}, {6, 10}, {12, 14}, {15, 17}};
    r[1].sel0 = kSel[g_te.sel][0];
    r[1].sel1 = kSel[g_te.sel][1];
    static const char* kField[5] = {"GIORNO", "MESE", "ANNO", "ORE", "MINUTI"};
    char lft[24], rgt[24];
    snprintf(lft, sizeof lft, "< %s", kField[(g_te.sel + 4) % 5]);
    snprintf(rgt, sizeof rgt, "%s >", kField[(g_te.sel + 1) % 5]);
    buttons(r[2], lft, rgt);
    buttons(r[3], "-", "+");
    buttons(r[4], "IMPOSTA QUESTA ORA", "", 1);
}

void buildTemps(const Snapshot& s, Row* r) {
    adjustRow(r[0], s, "Giorno (comfort)", pidx("sp_comfort"));
    adjustRow(r[1], s, "Notte", pidx("sp_night"));
    adjustRow(r[2], s, "Via / antigelo (max 12)", pidx("sp_away"));
    adjustRow(r[3], s, "ARRIVO: durata forzata", pidx("arrival_force_min"));
    adjustRow(r[4], s, "ARRIVO: stop a target -", pidx("arrival_stop_c"));
}

void buildSched(const Snapshot& s, Row* r) {
    r[0].kind = Kind::Adjust;
    snprintf(r[0].a, sizeof r[0].a, "Giorno (comfort) dalle");
    snprintf(r[0].b, sizeof r[0].b, "%02d:%02d", g_se.day / 60, g_se.day % 60);
    r[1].kind = Kind::Adjust;
    snprintf(r[1].a, sizeof r[1].a, "Notte dalle");
    snprintf(r[1].b, sizeof r[1].b, "%02d:%02d", g_se.night / 60, g_se.night % 60);
    adjustRow(r[2], s, "Preriscaldo mattino", pidx("preheat_min"));
    r[3].kind = Kind::Text;
    const char* n = noteFor(kSched);
    if (n) {
        snprintf(r[3].a, sizeof r[3].a, "%s", n);
        r[3].col = kAccent;
    } else if (g_se.uniform) {
        snprintf(r[3].a, sizeof r[3].a, "Orario uguale tutti i giorni");
    } else {
        snprintf(r[3].a, sizeof r[3].a, "Ora: giorni diversi (impostati da console)");
        r[3].col = kAccent;
    }
    snprintf(r[3].b, sizeof r[3].b, "APPLICA scrive questo orario lun-dom");
    buttons(r[4], "APPLICA A TUTTI I GIORNI", "", g_se.touched ? 1 : 0);
}

void buildSensors(const Snapshot& s, Row* r) {
    const tmc::FusionOut& f = s.st.fusion;
    r[0].kind = r[1].kind = r[4].kind = Kind::Text;
    if (!s.node_paired) {
        snprintf(r[0].a, sizeof r[0].a, "Stanza: sensore NON accoppiato");
        r[0].col = kAccent;
    } else if (f.room_ok) {
        snprintf(r[0].a, sizeof r[0].a, "Stanza: %.2f C  UR %.0f%%", f.t_room, f.rh_room);
    } else {
        snprintf(r[0].a, sizeof r[0].a, "Stanza: NON risponde");
        r[0].col = kAccent;
    }
    snprintf(r[0].b, sizeof r[0].b, "dato %lds fa, %d dBm, fonte %s",
             s.node_age_s == UINT32_MAX ? -1L : static_cast<long>(s.node_age_s), s.node_rssi, tmc::toString(f.src));
    if (s.ds18b20_present)
        snprintf(r[1].a, sizeof r[1].a, "CYD: %.2f C, corretta %.2f (%+.2f)", f.t_base_raw, f.t_base_corr, -f.offset);
    else snprintf(r[1].a, sizeof r[1].a, "CYD: sonda DS18B20 assente");
    snprintf(r[1].b, sizeof r[1].b, "correzione: %.1f h di dati (%s)", f.offset_learned_s / 3600.0,
             f.offset_ready ? "in uso" : "non in uso");
    buttons(r[2], s.pairing_open ? "ACCOPPIAMENTO APERTO" : "ACCOPPIA SENSORE", "", s.pairing_open ? 1 : 0);
    buttons(r[3], confirmLabel(3, 0, "DIMENTICA"), confirmLabel(3, 1, "AZZERA CORR."),
            static_cast<uint8_t>(confirmHot(3, 0) | confirmHot(3, 1)));
    const char* n = noteFor(kSensors);
    snprintf(r[4].a, sizeof r[4].a, "%s", n ? n : "Accoppia: premi, poi accendi il sensore");
    snprintf(r[4].b, sizeof r[4].b, "entro 60 s. Il primo sensore e' la stanza.");
}

ScreenEdit screenValues(const Snapshot& s) {
    if (active(g_sc.until)) return g_sc;
    ScreenEdit e;
    e.timeout = s.cfg.screen_timeout_s;
    e.bmax = s.cfg.brightness;
    e.bmin = s.cfg.brightness_min;
    e.autob = s.cfg.auto_brightness;
    return e;
}

void buildScreen(const Snapshot& s, Row* r) {
    const ScreenEdit e = screenValues(s);
    r[0].kind = r[1].kind = r[2].kind = Kind::Adjust;
    snprintf(r[0].a, sizeof r[0].a, "Spegnimento schermo dopo");
    snprintf(r[0].b, sizeof r[0].b, "%d s", e.timeout);
    snprintf(r[1].a, sizeof r[1].a, "Luminosita massima");
    snprintf(r[1].b, sizeof r[1].b, "%d / 255", e.bmax);
    snprintf(r[2].a, sizeof r[2].a, "Luminosita al buio (auto)");
    snprintf(r[2].b, sizeof r[2].b, "%d / 255", e.bmin);
    buttons(r[3], e.autob ? "LUMINOSITA AUTOMATICA: SI" : "LUMINOSITA AUTOMATICA: NO", "", e.autob ? 1 : 0);
    r[4].kind = Kind::Text;
    if (light::pct() >= 0)
        snprintf(r[4].a, sizeof r[4].a, "Luce ambiente %d%% (lettura %u)", light::pct(), light::raw());
    else snprintf(r[4].a, sizeof r[4].a, "Luce ambiente: impara la scala (%u)", light::raw());
    snprintf(r[4].b, sizeof r[4].b, "scala %u (luce) - %u (buio)", light::rangeBright(), light::rangeDark());
}

void buildAdvanced(const Snapshot& s, Row* r) {
    for (int i = 0; i < 4; ++i) {
        const int idx = g_adv_off + i;
        if (idx < kParamCount) adjustRow(r[i], s, kParams[idx].name, idx);
        else r[i].kind = Kind::Empty;
    }
    char rgt[32];
    const int last = g_adv_off + 4 < kParamCount ? g_adv_off + 4 : kParamCount;
    snprintf(rgt, sizeof rgt, "%d-%d/%d >", g_adv_off + 1, last, kParamCount);
    buttons(r[4], "< PREC", rgt);
}

void buildSystem(const Snapshot& s, Row* r) {
    char up[16];
    fmtUptime(up, sizeof up, s.st.uptime_ms);
    r[0].kind = Kind::Text;
    snprintf(r[0].a, sizeof r[0].a, "fw %d.%d  acceso da %s  reset %s", TM_FW_MAJOR, TM_FW_MINOR, up, s.reset_reason);
    const char* n = noteFor(kSystem);
    if (n) {
        snprintf(r[0].b, sizeof r[0].b, "%s", n);
    } else {
        snprintf(r[0].b, sizeof r[0].b, "SD %s  log persi %lu  memoria %lu kB", s.sd_ok ? "ok" : "NO",
                 static_cast<unsigned long>(logger::dropped()), static_cast<unsigned long>(ESP.getFreeHeap() / 1024u));
    }
    buttons(r[1], "BOOST 60'", "STOP BOOST",
            static_cast<uint8_t>((s.st.boost ? 1 : 0) | (s.st.arrival ? 2 : 0)));
    buttons(r[2], "SPENTO (OFF)", "SALVA ADESSO", s.mode == tmc::Mode::Off ? 1 : 0);
    buttons(r[3], confirmLabel(3, 0, "RIAVVIA"), confirmLabel(3, 1, "AZZERA APPR."),
            static_cast<uint8_t>(confirmHot(3, 0) | confirmHot(3, 1)));
    buttons(r[4], confirmLabel(4, 0, "RIPRISTINO DI FABBRICA"), "", confirmHot(4, 0));
}

void build(const Snapshot& s, Row* r) {
    switch (g_page) {
        case kStatus: buildStatus(s, r); break;
        case kTime: buildTime(s, r); break;
        case kTemps: buildTemps(s, r); break;
        case kSched: buildSched(s, r); break;
        case kSensors: buildSensors(s, r); break;
        case kScreen: buildScreen(s, r); break;
        case kAdvanced: buildAdvanced(s, r); break;
        case kSystem: buildSystem(s, r); break;
        default: break;
    }
}

// ----------------------------------------------------------------- actions
void postScreen(const ScreenEdit& e) {
    Request q;
    q.type = ReqType::SetScreen;
    q.i = e.timeout;
    q.v[0] = static_cast<int16_t>(e.bmax);
    q.v[1] = static_cast<int16_t>(e.bmin);
    q.v[2] = static_cast<int16_t>(e.autob);
    if (!shared::postRequest(q)) note("Coda piena: riprova");
}

void act(int row, int code, bool repeat, const Snapshot& s) {
    const bool adj = code == kMinus || code == kPlus;
    if (repeat && !adj && !(g_page == kTime && row == 3)) return;   // only -/+ auto-repeat
    switch (g_page) {
        case kTime:
            if (row == 2) g_te.sel = (g_te.sel + (code == kLeft ? 4 : 1)) % 5;
            if (row == 3) {
                const int d = code == kLeft ? -1 : 1;
                switch (g_te.sel) {
                    case 0: g_te.D = wrap(g_te.D + d, 1, daysInMonth(g_te.Y, g_te.M)); break;
                    case 1: g_te.M = wrap(g_te.M + d, 1, 12); break;
                    case 2: g_te.Y = g_te.Y + d < 2025 ? 2025 : (g_te.Y + d > 2099 ? 2099 : g_te.Y + d); break;
                    case 3: g_te.h = wrap(g_te.h + d, 0, 23); break;
                    case 4: g_te.m = wrap(g_te.m + d, 0, 59); break;
                }
                if (g_te.D > daysInMonth(g_te.Y, g_te.M)) g_te.D = daysInMonth(g_te.Y, g_te.M);
            }
            if (row == 4) {
                struct tm lt = {};
                lt.tm_year = g_te.Y - 1900;
                lt.tm_mon = g_te.M - 1;
                lt.tm_mday = g_te.D;
                lt.tm_hour = g_te.h;
                lt.tm_min = g_te.m;
                lt.tm_isdst = -1;   // Europe/Rome rules decide CET / CEST
                const time_t t = mktime(&lt);
                if (t > 0) {
                    post(ReqType::SetTime, 0, 0.0, static_cast<uint32_t>(t));
                    note("Ora impostata!");
                }
            }
            break;
        case kTemps: {
            static const char* kNames[5] = {"sp_comfort", "sp_night", "sp_away", "arrival_force_min", "arrival_stop_c"};
            if (adj) adjustParam(s, pidx(kNames[row]), code);
            break;
        }
        case kSched:
            if (row == 0 && adj) { g_se.day = wrap(g_se.day + 15 * code, 0, 1425); g_se.touched = true; }
            if (row == 1 && adj) { g_se.night = wrap(g_se.night + 15 * code, 0, 1425); g_se.touched = true; }
            if (row == 2 && adj) adjustParam(s, pidx("preheat_min"), code);
            if (row == 4) {
                if (g_se.day == g_se.night) { note("Giorno e notte alla stessa ora?"); break; }
                shared::postSchedule(simpleWeek(g_se.day, g_se.night));
                g_se.uniform = true;
                g_se.touched = false;
                note("Orario salvato per tutti i giorni");
            }
            break;
        case kSensors:
            if (row == 2) radio_base::openPairing();
            if (row == 3 && code == kLeft && confirmTap(3, 0)) { radio_base::forgetAll(); note("Sensori dimenticati"); }
            if (row == 3 && code == kRight && confirmTap(3, 1)) { post(ReqType::ResetLearning, 2); note("Correzione CYD azzerata"); }
            break;
        case kScreen: {
            ScreenEdit e = screenValues(s);
            if (row == 0 && adj) e.timeout = e.timeout + 5 * code < 10 ? 10 : (e.timeout + 5 * code > 255 ? 255 : e.timeout + 5 * code);
            if (row == 1 && adj) e.bmax = e.bmax + 10 * code < 10 ? 10 : (e.bmax + 10 * code > 255 ? 255 : e.bmax + 10 * code);
            if (row == 2 && adj) e.bmin = e.bmin + 2 * code < 1 ? 1 : (e.bmin + 2 * code > 255 ? 255 : e.bmin + 2 * code);
            if (row == 3) e.autob = e.autob ? 0 : 1;
            if (e.bmin > e.bmax) e.bmin = e.bmax;
            e.until = millis() + 2500;
            g_sc = e;
            postScreen(e);
            break;
        }
        case kAdvanced:
            if (row < 4 && adj && g_adv_off + row < kParamCount) adjustParam(s, g_adv_off + row, code);
            if (row == 4 && code == kLeft) g_adv_off = g_adv_off >= 4 ? g_adv_off - 4 : ((kParamCount - 1) / 4) * 4;
            if (row == 4 && code == kRight) g_adv_off = g_adv_off + 4 < kParamCount ? g_adv_off + 4 : 0;
            break;
        case kSystem:
            if (row == 1 && code == kLeft) post(ReqType::StartBoost, 60);
            if (row == 1 && code == kRight) post(ReqType::StopBoost);
            if (row == 2 && code == kLeft) post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Off));
            if (row == 2 && code == kRight) { post(ReqType::SaveNow); note("Salvato"); }
            if (row == 3 && code == kLeft && confirmTap(3, 0)) post(ReqType::Reboot);
            if (row == 3 && code == kRight && confirmTap(3, 1)) { post(ReqType::ResetLearning, 3); note("Apprendimento azzerato"); }
            if (row == 4 && confirmTap(4, 0)) post(ReqType::FactoryReset);
            break;
        default: break;
    }
}

// ----------------------------------------------------------------- drawing
void button(int x, int y, int w, int h, const char* label, uint16_t bg) {
    L->fillRoundRect(x, y, w, h, 6, bg);
    L->setFont(&fonts::FreeSansBold9pt7b);
    L->setTextColor(kFg);
    L->setTextDatum(lgfx::middle_center);
    if (!strcmp(label, "-") || !strcmp(label, "+")) {   // bars: font glyphs sit off-centre
        const int cx = x + w / 2, cy = y + h / 2;
        L->fillRect(cx - 10, cy - 2, 20, 4, kFg);
        if (label[0] == '+') L->fillRect(cx - 2, cy - 10, 4, 20, kFg);
    } else {
        L->drawString(label, x + w / 2, y + h / 2 + 1);
    }
    L->setTextDatum(lgfx::top_left);
}

void drawHeader(const Snapshot&) {
    L->fillRect(0, 0, 320, 34, kHead);
    button(0, 0, 44, 32, "<", kBtn);
    button(230, 0, 44, 32, ">", kBtn);
    button(276, 0, 44, 32, "X", kAlarm);
    char t[32];
    snprintf(t, sizeof t, "%s  %d/%d", kTitles[g_page], g_page + 1, kPages);
    L->setFont(&fonts::FreeSansBold9pt7b);
    L->setTextColor(kFg, kHead);
    L->setTextDatum(lgfx::middle_center);
    L->drawString(t, 137, 17);
    L->setTextDatum(lgfx::top_left);
}

bool same(const Row& a, const Row& b) {
    return a.kind == b.kind && !strcmp(a.a, b.a) && !strcmp(a.b, b.b) && a.hot == b.hot && a.sel0 == b.sel0 &&
           a.sel1 == b.sel1 && a.col == b.col;
}

void drawRow(int i, const Row& r) {
    const int y = kRowY0 + i * kRowH;
    L->fillRect(0, y, 320, kRowH, kBg);
    switch (r.kind) {
        case Kind::Text:
            L->setFont(&fonts::Font2);
            L->setTextColor(r.col, kBg);
            L->drawString(r.a, 4, y + 3);
            L->setTextColor(kDim, kBg);
            L->drawString(r.b, 4, y + 21);
            break;
        case Kind::Adjust:
            L->setFont(&fonts::Font2);
            L->setTextColor(kDim, kBg);
            L->drawString(r.a, 4, y + 2);
            L->setFont(&fonts::FreeSansBold9pt7b);
            L->setTextColor(kFg, kBg);
            L->drawString(r.b, 4, y + 20);
            button(kAdjMinusX, y + 2, 52, 36, "-", kBtn);
            button(kAdjPlusX, y + 2, 52, 36, "+", kBtn);
            break;
        case Kind::Buttons: {
            const uint16_t lb = (r.hot & 4) ? kAlarm : ((r.hot & 1) ? kBtnOn : kBtn);
            const uint16_t rb = (r.hot & 8) ? kAlarm : ((r.hot & 2) ? kBtnOn : kBtn);
            if (!r.b[0]) {
                button(4, y + 2, 312, 36, r.a, lb);
            } else {
                button(4, y + 2, 154, 36, r.a, lb);
                button(162, y + 2, 154, 36, r.b, rb);
            }
            break;
        }
        case Kind::Big: {
            L->setFont(&fonts::FreeSansBold12pt7b);
            int x = 20;
            const int len = static_cast<int>(strlen(r.a));
            for (int k = 0; k < len; ++k) {
                const char ch[2] = {r.a[k], 0};
                const int w = L->textWidth(ch);
                const bool sel = k >= r.sel0 && k < r.sel1;
                if (sel) L->fillRect(x, y + 2, w, 34, kAccent);
                L->setTextColor(kFg, sel ? kAccent : kBg);
                L->drawString(ch, x, y + 9);
                x += w;
            }
            break;
        }
        case Kind::Empty: break;
    }
}
}  // namespace

namespace menu {

void open(lgfx::LGFX_Device* lcd, const Snapshot& s) {
    L = lcd;
    g_open = true;
    g_page = kStatus;
    enterPage(s);
}

void close() { g_open = false; }
bool isOpen() { return g_open; }

void draw(const Snapshot& s, bool full) {
    if (!g_open || !L) return;
    if (full) {
        L->fillScreen(kBg);
        drawHeader(s);
        for (bool& v : g_shown_valid) v = false;
    }
    Row rows[kRows];
    build(s, rows);
    for (int i = 0; i < kRows; ++i) {
        if (g_shown_valid[i] && same(rows[i], g_shown[i])) continue;
        drawRow(i, rows[i]);
        g_shown[i] = rows[i];
        g_shown_valid[i] = true;
    }
}

void press(int16_t x, int16_t y, bool repeat, const Snapshot& s) {
    if (!g_open) return;
    if (y < 34) {
        if (repeat) return;
        if (x < 46) g_page = static_cast<Page>((g_page + kPages - 1) % kPages);
        else if (x >= 228 && x < 275) g_page = static_cast<Page>((g_page + 1) % kPages);
        else if (x >= 275) { close(); return; }
        else return;
        enterPage(s);
        draw(s, true);
        return;
    }
    const int i = (y - kRowY0) / kRowH;
    if (i < 0 || i >= kRows || !g_shown_valid[i]) return;
    const Row& r = g_shown[i];
    if (r.kind == Kind::Adjust) {
        if (x >= kAdjMinusX && x < kAdjMinusX + 54) act(i, kMinus, repeat, s);
        else if (x >= kAdjPlusX) act(i, kPlus, repeat, s);
    } else if (r.kind == Kind::Buttons) {
        act(i, (!r.b[0] || x < 160) ? kLeft : kRight, repeat, s);
    }
    draw(s, false);   // immediate feedback
}

bool repeats(int16_t x, int16_t y) {
    if (!g_open || y < kRowY0) return false;
    const int i = (y - kRowY0) / kRowH;
    if (i < 0 || i >= kRows || !g_shown_valid[i]) return false;
    if (g_shown[i].kind == Kind::Adjust) return x >= kAdjMinusX;
    return g_page == kTime && i == 3;
}

}  // namespace menu
