#include "ui.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "base_config.h"
#include "light.h"
#include "radio_base.h"
#include "shared.h"
#include "touch.h"
#include "ui_menu.h"

#ifndef TM_LCD_ST7789
#define TM_LCD_ST7789 1
#endif
#ifndef TM_LCD_BGR
#define TM_LCD_BGR 1
#endif
#ifndef TM_LCD_INVERT
#define TM_LCD_INVERT 0
#endif
#ifndef TM_LCD_ROTATION
#define TM_LCD_ROTATION 1
#endif

namespace {

class Lcd : public lgfx::LGFX_Device {
#if TM_LCD_ST7789
    lgfx::Panel_ST7789 panel_;
#else
    lgfx::Panel_ILI9341 panel_;
#endif
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;

public:
    Lcd() {
        {
            auto c = bus_.config();
            c.spi_host = SPI2_HOST;   // HSPI
            c.spi_mode = 0;
            c.freq_write = 40000000;
            c.freq_read = 16000000;
            c.spi_3wire = false;
            c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = cfg::kPinLcdSclk;
            c.pin_mosi = cfg::kPinLcdMosi;
            c.pin_miso = cfg::kPinLcdMiso;
            c.pin_dc = cfg::kPinLcdDc;
            bus_.config(c);
            panel_.setBus(&bus_);
        }
        {
            auto c = panel_.config();
            c.pin_cs = cfg::kPinLcdCs;
            c.pin_rst = -1;
            c.pin_busy = -1;
            c.panel_width = 240;
            c.panel_height = 320;
            c.memory_width = 240;
            c.memory_height = 320;
            c.offset_x = 0;
            c.offset_y = 0;
            c.offset_rotation = 0;
            c.readable = false;
            c.invert = TM_LCD_INVERT != 0;
            c.rgb_order = TM_LCD_BGR != 0;
            c.dlen_16bit = false;
            c.bus_shared = false;
            panel_.config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = cfg::kPinLcdBl;
            c.invert = false;
            c.freq = 12000;
            c.pwm_channel = 7;
            light_.config(c);
            panel_.setLight(&light_);
        }
        setPanel(&panel_);
    }
};

Lcd g_lcd;

// ---- colours ----
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;
constexpr uint16_t kDim = 0x8410;       // grey
constexpr uint16_t kAccent = 0xFD20;    // orange (heating)
constexpr uint16_t kBlue = 0x3D9F;
constexpr uint16_t kBtn = 0x2945;       // dark blue-grey
constexpr uint16_t kBtnOn = 0x04B3;     // teal
constexpr uint16_t kAlarm = 0xC000;     // dark red

enum BtnId : uint8_t { kNone = 0, kMinus, kPlus, kAuto, kComfort, kNight, kAway, kClock, kOff };
constexpr uint32_t kMenuTimeoutS = 120;   // hidden pages: more time to read and edit

struct Btn {
    int16_t x, y, w, h;
    BtnId id;
    const char* label;
};

const Btn kHomeBtns[] = {
    {180, 78, 62, 52, kMinus, "-"}, {252, 78, 62, 52, kPlus, "+"},
    {2, 188, 77, 50, kAuto, "AUTO"}, {82, 188, 77, 50, kComfort, "GIORNO"},
    {162, 188, 77, 50, kNight, "NOTTE"}, {242, 188, 76, 50, kAway, "VIA"},
    {232, 0, 88, 26, kClock, ""},
    {176, 1, 52, 24, kOff, "OFF"},   // SPENTO: second tap within 3 s confirms
};

Settings g_set;
volatile bool g_on = true;
uint32_t g_off_since_ms = 0;
volatile uint32_t g_last_on_ms = 0;
uint32_t g_last_touch_ms = 0;
bool g_wake_guard = false;
bool g_pressed = false;
int g_stable = 0;
uint32_t g_press_ms = 0, g_repeat_ms = 0;
BtnId g_btn = kNone;
int16_t g_px = 0, g_py = 0;          // where the current press started
bool g_long_done = false;
bool g_dump_raw = false;
uint32_t g_last_draw_ms = 0;
bool g_dirty_all = true;
// optimistic setpoint shown right after a +/- press
double g_pending_sp = 0;
uint32_t g_pending_until = 0;
uint32_t g_off_arm_until = 0;      // OFF button armed (waiting for the confirming tap)
int c_off = -1;
// caches for change detection
char c_mode[32], c_clock[8], c_temp[12], c_rh[16], c_sp[12], c_status[48], c_info[48];
uint16_t k_mode, k_clock, k_temp, k_rh, k_sp, k_status, k_info;   // colour last drawn per field
int c_active = -1;
uint16_t c_info_bg = 0;

// The 4th mode button reads VIA, or ARRIVO (orange) while the house is in VIA / SPENTO.
bool arrivalButton(tmc::Mode m) { return m == tmc::Mode::Away || m == tmc::Mode::Off; }

void drawButton(const Btn& b, bool active, const char* label = nullptr, uint16_t bg = 0) {
    g_lcd.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg ? bg : (active ? kBtnOn : kBtn));
    g_lcd.setTextColor(kFg);
    g_lcd.setTextDatum(lgfx::middle_center);
    if (b.id == kMinus || b.id == kPlus) {
        g_lcd.setFont(&fonts::FreeSansBold24pt7b);
        // draw +/- as bars: font glyphs sit off-centre
        const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        g_lcd.fillRect(cx - 14, cy - 3, 28, 6, kFg);
        if (b.id == kPlus) g_lcd.fillRect(cx - 3, cy - 14, 6, 28, kFg);
    } else {
        g_lcd.setFont(&fonts::FreeSansBold9pt7b);
        g_lcd.drawString(label ? label : b.label, b.x + b.w / 2, b.y + b.h / 2);
    }
    g_lcd.setTextDatum(lgfx::top_left);
}

void degree(int x, int y, int r, uint16_t col) {
    g_lcd.drawCircle(x, y, r, col);
    g_lcd.drawCircle(x, y, r - 1, col);
}

// Draw text in a cleared box only if the text or its colour changed. pad = text inset from x.
void field(char* cache, size_t cap, uint16_t* ccol, const char* txt, int x, int y, int w, int h,
           const lgfx::IFont* font, uint16_t fg, uint16_t bg, bool deg = false, int pad = 0) {
    if (!g_dirty_all && strcmp(cache, txt) == 0 && *ccol == fg) return;
    strncpy(cache, txt, cap - 1);
    cache[cap - 1] = 0;
    *ccol = fg;
    g_lcd.fillRect(x, y, w, h, bg);
    g_lcd.setFont(font);
    g_lcd.setTextColor(fg, bg);
    g_lcd.drawString(txt, x + pad, y);
    if (deg && txt[0] && txt[0] != '-') {
        const int tw = g_lcd.textWidth(txt);
        degree(x + pad + tw + 6, y + 6, 4, fg);
    }
}

void fmtMmSs(char* out, size_t n, uint32_t s) {
    if (s == UINT32_MAX) { snprintf(out, n, "--:--"); return; }
    snprintf(out, n, "%lu:%02lu", static_cast<unsigned long>(s / 60), static_cast<unsigned long>(s % 60));
}

const char* modeName(tmc::Mode m) {
    switch (m) {
        case tmc::Mode::Auto: return "AUTO";
        case tmc::Mode::Comfort: return "GIORNO";
        case tmc::Mode::Night: return "NOTTE";
        case tmc::Mode::Away: return "VIA";
        case tmc::Mode::Off: return "SPENTO";
    }
    return "?";
}

void statusText(const Snapshot& s, char* out, size_t n, uint16_t* col) {
    const tmc::Status& st = s.st;
    char t[12];
    *col = kFg;
    if (!st.heating_enabled) { snprintf(out, n, "Riscaldamento spento"); *col = kDim; return; }
    if (st.waiting_sensors) { snprintf(out, n, "Avvio: attendo i sensori..."); *col = kDim; return; }
    if (st.arrival) {
        *col = kAccent;
        if (st.valve_open)
            snprintf(out, n, "Arrivo: riscaldo al massimo, %lu min", static_cast<unsigned long>((st.arrival_left_s + 59) / 60));
        else
            snprintf(out, n, "Arrivo: la valvola apre tra %lu s", static_cast<unsigned long>(st.lockout_s));
        return;
    }
    if (st.valve_open) {
        *col = kAccent;
        if (st.next_action_s == UINT32_MAX) { snprintf(out, n, "Riscaldamento continuo"); return; }
        fmtMmSs(t, sizeof t, st.next_action_s);
        snprintf(out, n, "Riscaldamento: chiude tra %s", t);
        return;
    }
    if (st.next_action_s == UINT32_MAX) { snprintf(out, n, "Nessuna richiesta di calore"); *col = kDim; return; }
    fmtMmSs(t, sizeof t, st.next_action_s);
    if (st.lockout_s > 0 && st.lockout_s >= st.next_action_s) snprintf(out, n, "Pausa valvola: riapre tra %s", t);
    else snprintf(out, n, "In pausa: prossimo impulso tra %s", t);
}

void infoText(const Snapshot& s, char* out, size_t n, uint16_t* bg) {
    const uint16_t a = s.st.alarms;
    *bg = kAlarm;
    if (s.st.limp) { snprintf(out, n, "EMERGENZA: nessun sensore valido"); return; }
    if (a & tmc::kAlarmRoomLost) { snprintf(out, n, "Sensore stanza non risponde"); return; }
    if (a & tmc::kAlarmDisagree) { snprintf(out, n, "Sensori in disaccordo"); return; }
    if (a & tmc::kAlarmTimeUnknown) { snprintf(out, n, "Ora non impostata"); return; }
    if (a & tmc::kAlarmContOn) { snprintf(out, n, "Valvola aperta da molto tempo"); return; }
    if (a & tmc::kAlarmBudget) { snprintf(out, n, "Superato il budget giornaliero"); return; }
    *bg = kBg;
    switch (s.st.humidity) {
        case tmc::HumidityAdvice::TooDry: snprintf(out, n, "Aria secca: umidificare"); return;
        case tmc::HumidityAdvice::TooHumid: snprintf(out, n, "Aria umida: arieggiare"); return;
        default: out[0] = 0; return;
    }
}

void drawHome(const Snapshot& s) {
    if (g_dirty_all) {
        g_lcd.fillScreen(kBg);
        g_lcd.drawFastHLine(0, 27, 320, kBtn);
        g_lcd.setFont(&fonts::Font2);
        g_lcd.setTextColor(kDim, kBg);
        g_lcd.drawString("Impostata", 200, 31);
        c_active = -1;
    }
    char buf[48];
    // top bar: mode + clock
    if (s.st.boost) snprintf(buf, sizeof buf, "BOOST %lu'", static_cast<unsigned long>((s.st.boost_left_s + 59) / 60));
    else if (s.st.arrival) snprintf(buf, sizeof buf, "ARRIVO %lu'", static_cast<unsigned long>((s.st.arrival_left_s + 59) / 60));
    else if (s.mode == tmc::Mode::Auto)
        snprintf(buf, sizeof buf, "AUTO - %s%s", s.st.period == tmc::Period::Comfort ? "Giorno" : "Notte",
                 s.st.preheating ? " (pre)" : (s.st.override_active ? " *" : ""));
    else snprintf(buf, sizeof buf, "%s", modeName(s.mode));
    field(c_mode, sizeof c_mode, &k_mode, buf, 6, 5, 168, 20, &fonts::FreeSansBold9pt7b, kFg, kBg);
    if (s.time_valid) snprintf(buf, sizeof buf, "%02u:%02u", s.hour, s.minute);
    else snprintf(buf, sizeof buf, "--:--");
    field(c_clock, sizeof c_clock, &k_clock, buf, 262, 5, 56, 20, &fonts::FreeSansBold9pt7b, s.time_valid ? kFg : kAlarm,
          kBg);

    // big room temperature
    if (s.st.fusion.src != tmc::TempSource::None) snprintf(buf, sizeof buf, "%.1f", s.st.fusion.t_c);
    else snprintf(buf, sizeof buf, "--.-");
    field(c_temp, sizeof c_temp, &k_temp, buf, 10, 40, 165, 60, &fonts::FreeSansBold24pt7b,
          s.st.valve_open ? kAccent : kFg, kBg, true);
    if (s.st.fusion.rh_room >= 0) snprintf(buf, sizeof buf, "UR %.0f%%", s.st.fusion.rh_room);
    else buf[0] = 0;
    field(c_rh, sizeof c_rh, &k_rh, buf, 12, 104, 160, 22, &fonts::FreeSans12pt7b, kBlue, kBg);

    // setpoint (optimistic while a +/- is pending)
    const double sp = (static_cast<int32_t>(g_pending_until - millis()) > 0) ? g_pending_sp : s.st.sp;
    if (s.st.heating_enabled) snprintf(buf, sizeof buf, "%.1f", sp);
    else snprintf(buf, sizeof buf, "--");
    field(c_sp, sizeof c_sp, &k_sp, buf, 196, 50, 110, 28, &fonts::FreeSansBold18pt7b, kFg, kBg, true);

    // status + info lines
    uint16_t col = kFg;
    statusText(s, buf, sizeof buf, &col);
    field(c_status, sizeof c_status, &k_status, buf, 6, 136, 314, 22, &fonts::FreeSans9pt7b, col, kBg);
    uint16_t bg = kBg;
    infoText(s, buf, sizeof buf, &bg);
    if (g_dirty_all || bg != c_info_bg) c_info[0] = 1;   // force redraw on colour change
    c_info_bg = bg;
    field(c_info, sizeof c_info, &k_info, buf, 0, 160, 320, 24, &fonts::FreeSans9pt7b, kFg, bg, false, 6);

    // buttons
    int active = 2 + static_cast<int>(s.mode);   // index in kHomeBtns for AUTO..AWAY
    if (s.mode == tmc::Mode::Off) active = -2;
    if (g_dirty_all || active != c_active) {
        for (const Btn& b : kHomeBtns) {
            if (b.id == kClock || b.id == kOff) continue;
            const int idx = static_cast<int>(&b - kHomeBtns);
            if (b.id == kAway && arrivalButton(s.mode)) drawButton(b, false, "ARRIVO", kAccent);
            else drawButton(b, idx == active);
        }
        c_active = active;
    }
    // OFF button in the top bar: grey, red "OK?" while armed, red "OFF" while the heating is off
    const bool armed = static_cast<int32_t>(g_off_arm_until - millis()) > 0;
    const int off_state = s.mode == tmc::Mode::Off ? 2 : (armed ? 1 : 0);
    if (g_dirty_all || off_state != c_off) {
        for (const Btn& b : kHomeBtns)
            if (b.id == kOff) drawButton(b, false, off_state == 1 ? "OK?" : "OFF", off_state ? kAlarm : kBtn);
        c_off = off_state;
    }
}

// Backlight level: fixed, or between min and max with the room light (sampled with the screen off).
uint8_t backlightLevel() {
    const int p = light::pct();
    if (!g_set.auto_brightness || p < 0) return g_set.brightness;
    const int lo = g_set.brightness_min, hi = g_set.brightness;
    return static_cast<uint8_t>(lo + (hi - lo) * p / 100);
}

const Btn* hit(int16_t x, int16_t y) {
    for (const Btn& b : kHomeBtns) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return &b;
    }
    return nullptr;
}

void post(ReqType t, int32_t i = 0, double d = 0.0) {
    Request r;
    r.type = t;
    r.i = i;
    r.d = d;
    shared::postRequest(r);
}

void onPress(BtnId id, const Snapshot& s, bool repeat) {
    switch (id) {
        case kMinus:
        case kPlus: {
            if (!s.st.heating_enabled || s.mode == tmc::Mode::Off) return;   // OFF: nothing to adjust
            const double delta = id == kPlus ? 0.5 : -0.5;
            const double base = (static_cast<int32_t>(g_pending_until - millis()) > 0) ? g_pending_sp : s.st.sp;
            double nsp = base + delta;
            const double hi = s.mode == tmc::Mode::Away ? tmc::kAwayMaxC : tmc::kSetpointMax;   // same caps as the controller
            if (nsp < tmc::kSetpointMin) nsp = tmc::kSetpointMin;
            if (nsp > hi) nsp = hi;
            g_pending_sp = nsp;
            g_pending_until = millis() + 2500;
            post(ReqType::SetSetpoint, 0, nsp);
            break;
        }
        case kAuto: if (!repeat) post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Auto)); break;
        case kComfort: if (!repeat) post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Comfort)); break;
        case kNight: if (!repeat) post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Night)); break;
        case kOff:
            if (repeat || s.mode == tmc::Mode::Off) break;
            if (static_cast<int32_t>(g_off_arm_until - millis()) > 0) {   // second tap: switch off
                g_off_arm_until = 0;
                post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Off));
            } else {
                g_off_arm_until = millis() + 3000;
            }
            break;
        case kAway:
            if (repeat) break;
            if (arrivalButton(s.mode)) post(ReqType::Arrive);   // ARRIVO: back to AUTO + forced start
            else post(ReqType::SetMode, static_cast<int32_t>(tmc::Mode::Away));
            break;
        default: break;
    }
}

void screenOff() {
    g_on = false;
    g_off_since_ms = millis();
    g_lcd.setBrightness(0);
    g_lcd.sleep();
    menu::close();   // next wake-up always shows Home
}

void screenOnNow() {
    g_lcd.wakeup();
    g_lcd.setBrightness(backlightLevel());   // light level was measured while the screen was dark
    g_on = true;
    g_dirty_all = true;
    g_last_touch_ms = millis();
}

void handleTouch(const Snapshot& s) {
    const uint32_t now = millis();
    const TouchPoint tp = touch::read();
    if (tp.pressed && g_dump_raw)
        Serial.printf("[touch] raw x %u y %u z %u -> screen %d,%d\n", tp.raw_x, tp.raw_y, tp.z, tp.x, tp.y);
    g_stable = tp.pressed ? (g_stable < 3 ? g_stable + 1 : 3) : 0;
    const bool pressed = g_stable >= 2;   // two consecutive samples: ignore blips
    if (pressed) g_last_touch_ms = now;

    if (pressed && !g_on) {               // first touch only wakes the screen
        screenOnNow();
        g_wake_guard = true;
        return;
    }
    if (g_wake_guard) {                   // swallow the press that woke the screen / opened the menu
        if (!pressed) {
            g_wake_guard = false;
            g_pressed = false;
        }
        return;
    }
    if (menu::isOpen()) {                 // hidden settings pages
        if (pressed && !g_pressed) {
            g_pressed = true;
            g_press_ms = g_repeat_ms = now;
            g_px = tp.x;
            g_py = tp.y;
            menu::press(tp.x, tp.y, false, s);
            if (!menu::isOpen()) g_dirty_all = true;   // X: back to Home
        } else if (pressed && g_pressed) {
            if (menu::repeats(g_px, g_py) && now - g_press_ms > 600 && now - g_repeat_ms > 200) {
                g_repeat_ms = now;
                menu::press(g_px, g_py, true, s);
            }
        } else if (!pressed && g_pressed) {
            g_pressed = false;
        }
        return;
    }
    if (pressed && !g_pressed) {
        g_pressed = true;
        g_press_ms = g_repeat_ms = now;
        g_long_done = false;
        const Btn* b = hit(tp.x, tp.y);
        g_btn = b ? b->id : kNone;
        if (g_btn != kNone && g_btn != kClock) onPress(g_btn, s, false);
    } else if (pressed && g_pressed) {
        if ((g_btn == kMinus || g_btn == kPlus) && now - g_press_ms > 600 && now - g_repeat_ms > 250) {
            g_repeat_ms = now;
            onPress(g_btn, s, true);
        }
        if (g_btn == kClock && !g_long_done && now - g_press_ms > 2000) {   // hidden settings
            g_long_done = true;
            menu::open(&g_lcd, s);
            g_dirty_all = true;
            g_wake_guard = true;          // the long press must not also hit a menu button
        }
    } else if (!pressed && g_pressed) {
        g_pressed = false;
        g_btn = kNone;
    }
}
// Display settings and touch calibration come from the control task (single owner of Settings).
void applySettings(const Settings& c) {
    const TouchCal& a = c.touch;
    const TouchCal& b = g_set.touch;
    if (a.x_min != b.x_min || a.x_max != b.x_max || a.y_min != b.y_min || a.y_max != b.y_max ||
        a.swap_xy != b.swap_xy || a.invert_x != b.invert_x || a.invert_y != b.invert_y)
        touch::setCal(a);
    const bool bl = c.brightness != g_set.brightness || c.brightness_min != g_set.brightness_min ||
                    c.auto_brightness != g_set.auto_brightness;
    g_set.touch = a;
    g_set.brightness = c.brightness;
    g_set.brightness_min = c.brightness_min;
    g_set.auto_brightness = c.auto_brightness;
    g_set.screen_timeout_s = c.screen_timeout_s < 10 ? 10 : c.screen_timeout_s;
    if (bl && g_on) g_lcd.setBrightness(backlightLevel());   // show the new level at once
}
}  // namespace

namespace ui {

void begin(const Settings& s) {
    g_set = s;
    // RGB LED on the back of the CYD: off (active LOW)
    for (int p : {cfg::kPinLedR, cfg::kPinLedG, cfg::kPinLedB}) {
        pinMode(p, OUTPUT);
        digitalWrite(p, HIGH);
    }
    pinMode(cfg::kPinSpeaker, OUTPUT);
    digitalWrite(cfg::kPinSpeaker, LOW);
    g_lcd.init();
    g_lcd.setRotation(TM_LCD_ROTATION);
    g_lcd.setBrightness(g_set.brightness);
    g_lcd.fillScreen(kBg);
    touch::begin(s.touch);
    light::begin();
    g_last_touch_ms = millis();
    g_last_on_ms = millis();
}

void colorTest() {
    g_lcd.fillRect(0, 0, 107, 240, TFT_RED);
    g_lcd.fillRect(107, 0, 106, 240, TFT_GREEN);
    g_lcd.fillRect(213, 0, 107, 240, TFT_BLUE);
    g_lcd.setFont(&fonts::FreeSansBold12pt7b);
    g_lcd.setTextColor(TFT_WHITE);
    g_lcd.setTextDatum(lgfx::middle_center);
    g_lcd.drawString("ROSSO", 53, 120);
    g_lcd.drawString("VERDE", 160, 120);
    g_lcd.drawString("BLU", 266, 120);
    g_lcd.setTextDatum(lgfx::top_left);
}

void loop() {
    const Snapshot s = shared::read();
    if (s.seq != 0) applySettings(s.cfg);
    handleTouch(s);
    const uint32_t now = millis();
    light::poll(now, !g_on && now - g_off_since_ms >= 1000u);
    if (g_on) {
        g_last_on_ms = now;
        uint32_t timeout_s = g_set.screen_timeout_s;
        if (menu::isOpen() && timeout_s < kMenuTimeoutS) timeout_s = kMenuTimeoutS;
        if (now - g_last_touch_ms > timeout_s * 1000u) {
            screenOff();
            return;
        }
        if (g_dirty_all || now - g_last_draw_ms >= 250) {
            g_last_draw_ms = now;
            if (menu::isOpen()) menu::draw(s, g_dirty_all);
            else drawHome(s);
            g_dirty_all = false;
        }
    }
}

uint32_t lastScreenOnMs() { return g_on ? millis() : g_last_on_ms; }
bool screenOn() { return g_on; }
void setRawTouchDump(bool on) { g_dump_raw = on; }

}  // namespace ui
