// Stand-ins for the parts of the base firmware the UI talks to (queues, radio, logger, light, touch).
#include <Arduino.h>
#include <vector>
#include "light.h"
#include "logger.h"
#include "radio_base.h"
#include "shared.h"
#include "touch.h"

uint32_t g_now_ms = 1000;
uint32_t millis() { return g_now_ms; }
FakeSerial Serial;
FakeEsp ESP;

Snapshot g_snap;
std::vector<Request> g_requests;
TouchPoint g_touch;
bool g_pairing = false;

namespace shared {
void init() {}
bool postRequest(const Request& r) { g_requests.push_back(r); return true; }
bool postRoomSample(const RoomSample&) { return true; }
QueueHandle_t requestQueue() { return nullptr; }
QueueHandle_t sampleQueue() { return nullptr; }
uint32_t requestsDropped() { return 0; }
void postSchedule(const tmc::WeekSchedule& w) { g_snap.cfg.schedule = w; }
bool takeSchedule(tmc::WeekSchedule*) { return false; }
void publish(const Snapshot& s) { g_snap = s; }
Snapshot read() { return g_snap; }
}  // namespace shared

namespace radio_base {
bool begin(uint8_t, uint8_t, const char*, const char*) { return true; }
void startTask() {}
void openPairing() { g_pairing = true; }
bool pairingOpen() { return g_pairing; }
void forgetAll() {}
bool roomPaired() { return true; }
int8_t roomRssi() { return -61; }
uint32_t roomAgeS() { return 12; }
}  // namespace radio_base

namespace logger {
void begin() {}
void sample(const Snapshot&) {}
void event(const tmc::Event&, uint32_t) {}
void text(const char*, uint32_t) {}
bool sdOk() { return true; }
uint32_t dropped() { return 0; }
}  // namespace logger

namespace light {
void begin() {}
void poll(uint32_t, bool) {}
int8_t pct() { return 37; }
uint16_t raw() { return 1830; }
uint16_t rawLive() { return 1790; }
uint16_t rangeBright() { return 410; }
uint16_t rangeDark() { return 3650; }
}  // namespace light

namespace touch {
void begin(const TouchCal&) {}
void setCal(const TouchCal&) {}
TouchPoint read() { return g_touch; }
}  // namespace touch
