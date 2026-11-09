#include "shared.h"
#include <atomic>

namespace {
QueueHandle_t g_req = nullptr;
QueueHandle_t g_samples = nullptr;
SemaphoreHandle_t g_mtx = nullptr;   // protects g_snap and the schedule mailbox
Snapshot g_snap;
tmc::WeekSchedule g_sched_box;
bool g_sched_pending = false;
std::atomic<uint32_t> g_dropped{0};
}  // namespace

namespace shared {

void init() {
    g_req = xQueueCreate(16, sizeof(Request));
    g_samples = xQueueCreate(8, sizeof(RoomSample));
    g_mtx = xSemaphoreCreateMutex();
}

bool postRequest(const Request& r) {
    if (g_req && xQueueSend(g_req, &r, pdMS_TO_TICKS(50)) == pdTRUE) return true;
    ++g_dropped;
    return false;
}

bool postRoomSample(const RoomSample& s) { return g_samples && xQueueSend(g_samples, &s, 0) == pdTRUE; }
QueueHandle_t requestQueue() { return g_req; }
QueueHandle_t sampleQueue() { return g_samples; }
uint32_t requestsDropped() { return g_dropped; }

void postSchedule(const tmc::WeekSchedule& w) {
    if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(100)) != pdTRUE) return;
    g_sched_box = w;
    g_sched_pending = true;
    xSemaphoreGive(g_mtx);
}

bool takeSchedule(tmc::WeekSchedule* w) {
    bool got = false;
    if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (g_sched_pending) {
        *w = g_sched_box;
        g_sched_pending = false;
        got = true;
    }
    xSemaphoreGive(g_mtx);
    return got;
}

void publish(const Snapshot& s) {
    if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_snap = s;
        xSemaphoreGive(g_mtx);
    }
}

Snapshot read() {
    Snapshot s;
    if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
        s = g_snap;
        xSemaphoreGive(g_mtx);
    }
    return s;
}

}  // namespace shared
