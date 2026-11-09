// TermoMade room node: reads the room climate every 30 s and reports it to the base over ESP-NOW.
// Optional DS3231 keeps the system clock across power cuts (the house has no internet).
// Reliability: loop watchdog, sensor retries + I2C bus recovery, MAC-level retries, channel
// re-scan, plausibility checks, pairing stored in NVS, nothing blocks longer than ~2 s.
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include "node_config.h"
#include "radio.h"
#include "sensors.h"
#include "tm_protocol.h"
#include "tm_util.h"

namespace {
ClimateSensor g_sensor;
Ds3231 g_rtc;
Radio g_radio;

uint32_t g_next_report_ms = 0;
uint32_t g_last_hello_ms = 0;
int g_fail_streak = 0;
uint32_t g_led_off_ms = 0;
uint32_t g_identify_until_ms = 0;
uint32_t g_boot_pressed_ms = 0;
uint32_t g_reports = 0, g_acked = 0;
ClimateReading g_last;

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT WDT";
        case ESP_RST_TASK_WDT: return "TASK WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_BROWNOUT: return "BROWN-OUT";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        default: return "other";
    }
}

void ledPulse(uint32_t ms) {
    digitalWrite(cfg::kPinLed, HIGH);
    g_led_off_ms = millis() + ms;
}

void serviceLed() {
    const uint32_t now = millis();
    if (static_cast<int32_t>(g_identify_until_ms - now) > 0) {
        digitalWrite(cfg::kPinLed, (now / 100) % 2);
        return;
    }
    if (static_cast<int32_t>(now - g_led_off_ms) >= 0) digitalWrite(cfg::kPinLed, LOW);
}

void handleFrame(const RxFrame& f) {
    const Pairing& p = g_radio.pairing();
    if (!p.paired || memcmp(f.mac, p.base_mac, 6) != 0) return;   // only our base
    union { tmp::Ack ack; tmp::Cmd cmd; } u;
    const uint8_t type = tmp::validate(f.data, f.len, p.net_id, &u, sizeof u);
    if (type == tmp::kAck) {
        ++g_acked;
        ledPulse(30);
        g_radio.setRole(u.ack.role);
        if (u.ack.report_s >= 10) g_radio.setReportS(u.ack.report_s);
        // phone time (best source) -> correct the RTC if it drifted or was never valid
        if (u.ack.time_src == tmp::kTimePhone && g_rtc.present() && tmu::plausibleUnix(u.ack.unix_time)) {
            uint32_t rtc = 0;
            bool valid = false;
            g_rtc.read(&rtc, &valid);
            const int64_t diff = static_cast<int64_t>(rtc) - static_cast<int64_t>(u.ack.unix_time);
            if (!valid || diff > 2 || diff < -2) {
                if (g_rtc.write(u.ack.unix_time))
                    Serial.printf("[rtc] set from phone time %lu (drift %lld s)\n",
                                  static_cast<unsigned long>(u.ack.unix_time), static_cast<long long>(diff));
            }
        }
    } else if (type == tmp::kCmd) {
        switch (u.cmd.cmd) {
            case tmp::kCmdIdentify: g_identify_until_ms = millis() + 10000; break;
            case tmp::kCmdReboot: Serial.println("[cmd] reboot"); delay(50); ESP.restart(); break;
            case tmp::kCmdForget: g_radio.forget(); delay(50); ESP.restart(); break;
            default: break;
        }
    }
}

void drainRx(uint32_t wait_ms) {
    RxFrame f;
    const uint32_t t0 = millis();
    do {
        while (g_radio.receive(f, 0)) handleFrame(f);
        if (wait_ms) delay(5);
    } while (millis() - t0 < wait_ms);
}

void report() {
    const Pairing& p = g_radio.pairing();
    g_last = g_sensor.read();

    tmp::Telemetry t = {};
    tmp::initHeader(t.h, tmp::kTelemetry, p.net_id, g_radio.nextSeq());
    t.node_type = tmp::kNodeRoom;
    t.fw_major = TM_FW_MAJOR;
    t.fw_minor = TM_FW_MINOR;
    t.uptime_min = static_cast<uint16_t>(millis() / 60000u);
    if (g_last.ok) {
        tmp::addReading(t, tmp::kAirT, 0, tmp::toCenti(g_last.t_c));
        if (g_last.rh >= 0.0) tmp::addReading(t, tmp::kRelHum, 0, tmp::toCenti(g_last.rh));
    } else {
        t.status |= tmp::kStSensorError;
    }
    if (g_rtc.present()) {
        t.status |= tmp::kStRtcPresent;
        uint32_t rtc = 0;
        bool valid = false;
        if (g_rtc.read(&rtc, &valid) && valid) {
            t.status |= tmp::kStRtcValid;
            t.rtc_time = rtc;
        }
    }
    const size_t len = tmp::telemetryLength(t.count);
    ++g_reports;
    bool ok = g_radio.sendToBase(reinterpret_cast<const uint8_t*>(&t), len);
    if (!ok && ++g_fail_streak >= cfg::kFailsBeforeRescan) {
        Serial.println("[radio] base unreachable, scanning channels");
        ok = g_radio.rescan(reinterpret_cast<const uint8_t*>(&t), len);
    }
    if (ok) {
        g_fail_streak = 0;
        drainRx(cfg::kAckWaitMs);
    }
    if (g_last.ok)
        Serial.printf("[report] T %.2f C RH %.1f %% -> %s (sent %lu, acked %lu, sensor errors %lu)\n", g_last.t_c,
                      g_last.rh, ok ? "ok" : "FAILED", static_cast<unsigned long>(g_reports),
                      static_cast<unsigned long>(g_acked), static_cast<unsigned long>(g_sensor.errors()));
    else
        Serial.printf("[report] SENSOR ERROR -> %s\n", ok ? "ok" : "FAILED");
}

void handleButton() {
    if (digitalRead(cfg::kPinBoot) == LOW) {
        if (g_boot_pressed_ms == 0) g_boot_pressed_ms = millis();
        else if (millis() - g_boot_pressed_ms > cfg::kForgetHoldMs) {
            Serial.println("[button] held: forgetting pairing");
            g_radio.forget();
            delay(100);
            ESP.restart();
        }
    } else {
        g_boot_pressed_ms = 0;
    }
}

void handleSerial() {
    static char buf[16];
    static uint8_t n = 0;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            if (!strcmp(buf, "forget")) { g_radio.forget(); ESP.restart(); }
            else if (!strcmp(buf, "status")) {
                const Pairing& p = g_radio.pairing();
                Serial.printf("paired %d net %u ch %u role %u report %u s, last T %.2f RH %.1f ok %d\n", p.paired,
                              p.net_id, p.channel, p.role, p.report_s, g_last.t_c, g_last.rh, g_last.ok);
            } else if (n) Serial.println("commands: status, forget");
            n = 0;
        } else if (n < sizeof(buf) - 1) {
            buf[n++] = c;
        }
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    setCpuFrequencyMhz(cfg::kCpuMhz);
    pinMode(cfg::kPinLed, OUTPUT);
    digitalWrite(cfg::kPinLed, LOW);
    pinMode(cfg::kPinBoot, INPUT_PULLUP);
    Serial.printf("\nTermoMade room node fw %d.%d, reset: %s\n", TM_FW_MAJOR, TM_FW_MINOR, resetReason());

    Wire.begin(cfg::kPinSda, cfg::kPinScl, cfg::kI2cHz);
    Wire.setTimeOut(50);
    g_sensor.begin();
    g_rtc.begin(Wire);
    g_radio.begin();
    enableLoopWDT();   // loop() must return within the task-watchdog timeout (5 s)
    g_next_report_ms = millis() + 2000;
}

void loop() {
    handleButton();
    handleSerial();
    serviceLed();
    drainRx(0);

    const uint32_t now = millis();
    if (!g_radio.pairing().paired) {
        if (now - g_last_hello_ms >= cfg::kHelloPeriodMs) {
            g_last_hello_ms = now;
            ledPulse(50);
            g_radio.helloSweep(tmp::kNodeRoom);
        }
        delay(10);
        return;
    }
    if (static_cast<int32_t>(now - g_next_report_ms) >= 0) {
        const uint32_t period = static_cast<uint32_t>(g_radio.pairing().report_s) * 1000u;
        g_next_report_ms += period;
        if (static_cast<int32_t>(now - g_next_report_ms) >= 0) g_next_report_ms = now + period;   // fell behind
        report();
    }
    delay(10);
}
