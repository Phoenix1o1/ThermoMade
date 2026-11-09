// TermoMade base station — CYD ESP32-2432S028 (2-USB variant).
//
// Tasks
//   control (core 1, prio 3, 1 Hz, task watchdog 10 s): requests -> controller -> relay,
//            sensors, persistence, snapshot, log lines. The ONLY writer of the relay pin.
//   radio   (core 0, prio 4): ESP-NOW (radio_base.cpp)
//   logger  (core 0, prio 1): SD card (logger.cpp)
//   loop()  (core 1, prio 1, loop watchdog): display, touch, serial console
//
// Boot order matters: the relay output is forced inactive before anything else, and the
// controller starts in the OFF_MIN lockout (hard rule 1: a reboot never reopens early).
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "base_config.h"
#include "cli.h"
#include "logger.h"
#include "param_table.h"
#include "radio_base.h"
#include "relay.h"
#include "settings.h"
#include "shared.h"
#include "temp_local.h"
#include "timekeeper.h"
#include "tm_protocol.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef TM_AP_PASS
#define TM_AP_PASS "termomade"
#define TM_AP_PASS_IS_DEFAULT 1
#endif

namespace {

tmc::Controller g_ctl;
Settings g_cfg;                  // owned by the control task after setup()
bool g_cfg_dirty = false;
uint32_t g_cfg_dirty_ms = 0;
uint32_t g_persist_ms = 0;
uint32_t g_log_ms = 0;
uint32_t g_seq = 0;

const char* resetReason(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT-WDT";
        case ESP_RST_TASK_WDT: return "TASK-WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

const char* eventName(tmc::EventType t) {
    switch (t) {
        case tmc::EventType::ValveOpen: return "OPEN";
        case tmc::EventType::ValveClose: return "CLOSE";
        case tmc::EventType::ModeChange: return "mode";
        case tmc::EventType::SetpointChange: return "setpoint";
        case tmc::EventType::SourceChange: return "source";
        case tmc::EventType::LimpEnter: return "LIMP on";
        case tmc::EventType::LimpExit: return "LIMP off";
        case tmc::EventType::AlarmOn: return "alarm on";
        case tmc::EventType::AlarmOff: return "alarm off";
        case tmc::EventType::DayRollover: return "day";
        case tmc::EventType::Boot: return "boot";
    }
    return "?";
}

void printEvent(const tmc::Event& e) {
    Serial.printf("[ev %7lus] %-9s a=%u b=%u x=%.3f y=%.2f", static_cast<unsigned long>(e.uptime_ms / 1000u),
                  eventName(e.type), e.a, e.b, static_cast<double>(e.x), static_cast<double>(e.y));
    if (e.type == tmc::EventType::ValveOpen || e.type == tmc::EventType::ValveClose)
        Serial.printf("  (%s)", tmc::toString(static_cast<tmc::ValveReason>(e.a)));
    Serial.println();
}

void markDirty(uint32_t now) {
    g_cfg_dirty = true;
    g_cfg_dirty_ms = now;
}

void saveAll() {
    g_cfg.params = g_ctl.params();
    g_cfg.schedule = g_ctl.schedule();
    g_cfg.mode = g_ctl.mode();
    const bool a = settings::save(g_cfg);
    const bool b = settings::savePersistent(g_ctl.persistent());
    g_cfg_dirty = false;
    Serial.printf("[nvs] settings %s, learned state %s\n", a ? "saved" : "SAVE FAILED", b ? "saved" : "SAVE FAILED");
}

[[noreturn]] void restart(const char* why) {
    Serial.printf("[sys] restart: %s\n", why);
    logger::text(why, timekeeper::now());
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(300));   // let the logger write the line
    // GPIO reset releases the relay input; the external pull resistor keeps it inactive.
    esp_restart();
}

void handleRequest(const Request& r, uint32_t now) {
    switch (r.type) {
        case ReqType::SetMode:
            if (r.i >= 0 && r.i <= 4) g_ctl.setMode(static_cast<tmc::Mode>(r.i));
            break;
        case ReqType::AdjustSetpoint: g_ctl.adjustSetpoint(r.d); break;
        case ReqType::SetSetpoint: g_ctl.setSetpoint(r.d); break;
        case ReqType::StartBoost:
            if (r.i >= 1 && r.i <= 240) g_ctl.startBoost(static_cast<uint16_t>(r.i));
            break;
        case ReqType::StopBoost: g_ctl.stopBoost(); break;
        case ReqType::SetTime:
            if (timekeeper::offer(TimeSrc::Manual, r.u)) logger::text("time set manually", r.u);
            break;
        case ReqType::SetParam:
            if (r.i >= 0 && r.i < kParamCount) {
                tmc::ControlParams p = g_ctl.params();
                setParam(p, r.i, r.d);
                g_ctl.setParams(p);   // sanitized inside
                g_cfg.params = g_ctl.params();
                markDirty(now);
                Serial.printf("[cfg] %s = %.4g\n", kParams[r.i].name, getParam(g_cfg.params, r.i));
            }
            break;
        case ReqType::SaveNow: saveAll(); break;
        case ReqType::ResetLearning:
            g_ctl.resetLearning((r.i & 1) != 0, (r.i & 2) != 0);
            settings::savePersistent(g_ctl.persistent());
            logger::text((r.i & 3) == 3 ? "learning reset: all" : ((r.i & 1) ? "learning reset: duty" : "learning reset: offset"),
                         timekeeper::now());
            break;
        case ReqType::Reboot:
            saveAll();
            restart("reboot requested");
        case ReqType::FactoryReset:
            settings::factoryReset();
            restart("factory reset");
        case ReqType::SetScreen:
            if (r.i >= 10 && r.i <= 255) g_cfg.screen_timeout_s = static_cast<uint8_t>(r.i);
            if (r.v[0] >= 10 && r.v[0] <= 255) g_cfg.brightness = static_cast<uint8_t>(r.v[0]);
            markDirty(now);
            break;
        case ReqType::SetTouchCal: {
            TouchCal c;
            c.x_min = r.v[0];
            c.x_max = r.v[1];
            c.y_min = r.v[2];
            c.y_max = r.v[3];
            c.swap_xy = r.v[4] ? 1 : 0;
            c.invert_x = r.v[5] ? 1 : 0;
            c.invert_y = r.v[6] ? 1 : 0;
            if (c.x_min != c.x_max && c.y_min != c.y_max) {
                g_cfg.touch = c;
                markDirty(now);
            }
            break;
        }
    }
}

Snapshot buildSnapshot(uint32_t now) {
    Snapshot s;
    s.seq = ++g_seq;
    s.st = g_ctl.status();
    s.cfg = g_cfg;
    s.mode = g_ctl.mode();
    s.time_valid = timekeeper::valid();
    s.time_src = timekeeper::source();
    s.unix_time = timekeeper::now();
    timekeeper::wall(&s.hour, &s.minute);
    s.relay_on = Relay::isOn();
    s.sd_ok = logger::sdOk();
    s.ds18b20_present = temp_local::present();
    s.node_paired = radio_base::roomPaired();
    s.node_rssi = radio_base::roomRssi();
    s.node_age_s = radio_base::roomAgeS();
    s.pairing_open = radio_base::pairingOpen();
    s.req_dropped = shared::requestsDropped();
    (void)now;
    return s;
}

void controlTask(void*) {
    esp_task_wdt_add(nullptr);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(cfg::kControlPeriodMs));
        const uint32_t now = millis();

        // 1. user requests (UI, console; web later)
        Request rq;
        while (xQueueReceive(shared::requestQueue(), &rq, 0) == pdTRUE) handleRequest(rq, now);
        tmc::WeekSchedule w;
        if (shared::takeSchedule(&w)) {
            g_ctl.setSchedule(w);
            g_cfg.schedule = g_ctl.schedule();
            markDirty(now);
            logger::text("schedule changed", timekeeper::now());
        }
        if (g_ctl.mode() != g_cfg.mode) {          // mode is remembered across reboots
            g_cfg.mode = g_ctl.mode();
            markDirty(now);
        }
        if (g_ctl.paramsChanged()) {               // setpoint of a manual mode changed
            g_cfg.params = g_ctl.params();
            markDirty(now);
        }

        // 2. sensors
        RoomSample rs;
        while (xQueueReceive(shared::sampleQueue(), &rs, 0) == pdTRUE)
            if (rs.role == tmp::kRoleRoom) g_ctl.onRoomSample(now, rs.t_c, rs.rh);
        bool ok = false;
        double t = 0.0;
        if (temp_local::poll(now, &ok, &t)) g_ctl.onBaseSample(now, ok, t);

        // 3. control step -> relay (the only place the relay is written after setup)
        tmc::ControllerInputs in;
        in.now_ms = now;
        in.wall = timekeeper::wall();
        // CYD self-heating: screen on now or within the last 5 min. Signed difference: the UI task
        // may have stamped a time slightly later than this tick's `now`.
        const int32_t since_screen = static_cast<int32_t>(now - ui::lastScreenOnMs());
        in.base_disturbed = ui::screenOn() || since_screen < static_cast<int32_t>(cfg::kScreenDisturbHoldMs);
        in.plant_on = true;   // central plant hours gate: not configured yet (docs/10)
        Relay::set(g_ctl.update(in));

        // 4. events -> console + SD
        tmc::Event ev;
        while (g_ctl.popEvent(ev)) {
            printEvent(ev);
            logger::event(ev, timekeeper::now());
        }

        // 5. persistence
        if (g_cfg_dirty && now - g_cfg_dirty_ms >= cfg::kSettingsDebounceMs) saveAll();
        if (now - g_persist_ms >= cfg::kPersistEveryMs) {
            g_persist_ms = now;
            settings::savePersistent(g_ctl.persistent());
        }

        // 6. snapshot for UI / radio / console, log line every minute
        const Snapshot snap = buildSnapshot(now);
        shared::publish(snap);
        if (now - g_log_ms >= cfg::kLogEveryMs) {
            g_log_ms = now;
            logger::sample(snap);
        }

        esp_task_wdt_reset();
    }
}

void setupWatchdog() {
    esp_task_wdt_config_t wc = {};
    wc.timeout_ms = cfg::kTaskWdtMs;
    wc.idle_core_mask = 0;        // watch our tasks explicitly, not the idle tasks
    wc.trigger_panic = true;      // hang -> reboot -> relay released -> boot lockout
    if (esp_task_wdt_reconfigure(&wc) != ESP_OK) esp_task_wdt_init(&wc);
}

}  // namespace

void setup() {
    Relay::begin();   // FIRST: relay inactive (valve CLOSE) before anything else runs

    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    const esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("\n\nTermoMade base fw %d.%d  reset: %s\n", TM_FW_MAJOR, TM_FW_MINOR, resetReason(rr));

    shared::init();
    g_cfg = settings::load();
    tmc::PersistentState ps;
    if (!settings::loadPersistent(&ps)) {
        ps = tmc::PersistentState{};
        Serial.println("[nvs] no learned state: starting from I0");
    }
    timekeeper::begin();

    ui::begin(g_cfg);
    ui::colorTest();             // red | green | blue: wrong order -> flip TM_LCD_BGR / TM_LCD_INVERT
    delay(1500);

    temp_local::begin();

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[33];
#ifdef TM_AP_SSID
    snprintf(ssid, sizeof ssid, "%s", TM_AP_SSID);
#else
    snprintf(ssid, sizeof ssid, "TermoMade-%02X%02X", mac[4], mac[5]);
#endif
#ifdef TM_AP_PASS_IS_DEFAULT
    Serial.println("[wifi] WARNING: default AP password - create include/secrets.h (see secrets.h.example)");
#endif
    radio_base::begin(g_cfg.net_id, g_cfg.ap_channel, ssid, TM_AP_PASS);

    logger::begin();
    char line[96];
    snprintf(line, sizeof line, "boot fw %d.%d reset %s", TM_FW_MAJOR, TM_FW_MINOR, resetReason(rr));
    logger::text(line, timekeeper::now());

    // controller: starts in lockout (OFF_MIN from now) whatever the reset reason
    g_ctl.begin(millis(), g_cfg.params, g_cfg.schedule, ps, g_cfg.mode, /*start_unlocked=*/false);
    g_cfg.params = g_ctl.params();
    g_cfg.schedule = g_ctl.schedule();
    g_persist_ms = g_log_ms = millis();
    shared::publish(buildSnapshot(millis()));

    setupWatchdog();
    radio_base::startTask();
    xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 3, nullptr, 1);
    enableLoopWDT();             // UI/console hang -> reboot as well
    Serial.println("ready. Type 'help'.");
}

void loop() {
    ui::loop();
    cli::loop();
    vTaskDelay(pdMS_TO_TICKS(15));   // ~60 Hz touch sampling; lets lower-priority work run
}
