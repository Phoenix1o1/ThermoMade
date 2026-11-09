// TermoMade base — data shared between tasks.
//   control task (core 1, prio 3): owns the Controller, the Settings and the relay; publishes Snapshot
//   radio task   (core 0, prio 4): ESP-NOW frames -> samples; answers with ACKs from the Snapshot
//   logger task  (core 0, prio 1): SD card
//   loop()       (core 1, prio 1): display + touch + serial CLI -> Requests
// Nothing outside the control task changes the controller or the settings directly: everything
// goes through the request queue (small changes) or the schedule mailbox (a whole week).
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "tm_controller.h"

// ---- requests to the control task (UI / CLI / radio / web later) ----
enum class ReqType : uint8_t {
    SetMode,         // i = tmc::Mode
    AdjustSetpoint,  // d = delta degC
    SetSetpoint,     // d = degC (clamped 5..25 by the controller)
    StartBoost,      // i = minutes
    StopBoost,
    SetTime,         // u = unix UTC (manual / phone)
    SetParam,        // i = index in kParams, d = value
    SaveNow,         // write settings + learned state to NVS now
    ResetLearning,   // i bit0 = duty, bit1 = CYD sensor offset
    Reboot,          // save, then restart
    FactoryReset,    // erase NVS, then restart
    SetScreen,       // i = timeout s, v[0] = max brightness, v[1] = min brightness, v[2] = auto (<0: keep)
    SetTouchCal,     // v[0..6] = x_min x_max y_min y_max swap invx invy
    Arrive,          // ARRIVO: back to AUTO + forced start (tmc::Controller::arrive)
};
struct Request {
    ReqType type = ReqType::SaveNow;
    int32_t i = 0;
    uint32_t u = 0;
    double d = 0.0;
    int16_t v[8] = {};
};

// ---- samples from the radio task ----
struct RoomSample {
    uint32_t rx_ms = 0;
    double t_c = 0.0;
    double rh = -1.0;       // -1 = none
    uint8_t role = 0;
};

// ---- time ----
enum class TimeSrc : uint8_t { None = 0, NodeRtc = 1, Manual = 2 };   // Manual = phone / CLI (best)

// ---- snapshot published by the control task every second ----
struct Snapshot {
    uint32_t seq = 0;
    tmc::Status st;
    Settings cfg;                 // current settings (params, schedule, display, ...)
    tmc::Mode mode = tmc::Mode::Auto;
    bool time_valid = false;
    TimeSrc time_src = TimeSrc::None;
    uint32_t unix_time = 0;
    uint8_t hour = 0, minute = 0;
    bool relay_on = false;
    bool sd_ok = false;
    bool ds18b20_present = false;
    // room node link
    bool node_paired = false;
    int8_t node_rssi = 0;
    uint32_t node_age_s = UINT32_MAX;
    bool pairing_open = false;
    uint32_t req_dropped = 0;     // requests lost because the queue was full
    int8_t light_pct = -1;        // room light 0..100 % from the CYD LDR, -1 = not known yet
    uint16_t light_raw = 0;       // LDR ADC reading (higher = darker)
    const char* reset_reason = "";
};

namespace shared {
void init();
bool postRequest(const Request& r);            // any task (waits up to 50 ms)
bool postRoomSample(const RoomSample& s);      // radio task (never waits)
QueueHandle_t requestQueue();
QueueHandle_t sampleQueue();
uint32_t requestsDropped();
// Weekly schedule mailbox: the last posted schedule wins.
void postSchedule(const tmc::WeekSchedule& w);
bool takeSchedule(tmc::WeekSchedule* w);       // control task
void publish(const Snapshot& s);               // control task
Snapshot read();                               // any task (copy)
}  // namespace shared
