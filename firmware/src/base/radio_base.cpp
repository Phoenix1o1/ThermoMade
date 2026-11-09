#include "radio_base.h"
#include <Arduino.h>
#include <atomic>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "base_config.h"
#include "shared.h"
#include "timekeeper.h"
#include "tm_protocol.h"
#include "tm_util.h"

namespace {
struct RxItem {
    uint8_t mac[6];
    int8_t rssi;
    int len;
    uint8_t data[tmp::kMaxFrame];
};

struct NodeEntry {
    bool used = false;
    uint8_t mac[6] = {};
    uint8_t role = tmp::kRoleNone;
    // runtime only
    bool seq_valid = false;
    uint16_t last_seq = 0;
    uint32_t last_rx_ms = 0;
    bool heard = false;
    int8_t rssi = 0;
};

struct StoredNodes {
    uint8_t count;
    struct { uint8_t mac[6]; uint8_t role; } n[cfg::kMaxNodes];
};

QueueHandle_t g_rxq = nullptr;
NodeEntry g_nodes[cfg::kMaxNodes];
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_net_id = 0;
uint8_t g_channel = 1;
uint8_t g_ap_mac[6] = {};
volatile uint32_t g_pair_until_ms = 0;
volatile bool g_pair_open = false;
std::atomic<uint32_t> g_tx_fail{0};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSent(const esp_now_send_info_t* /*info*/, esp_now_send_status_t status) {
#else
void onSent(const uint8_t* /*mac*/, esp_now_send_status_t status) {
#endif
    if (status != ESP_NOW_SEND_SUCCESS) ++g_tx_fail;
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!info || !data || len <= 0 || len > tmp::kMaxFrame || !g_rxq) return;
    RxItem it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi = info->rx_ctrl ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
    it.len = len;
    memcpy(it.data, data, static_cast<size_t>(len));
    xQueueSend(g_rxq, &it, 0);
}

bool ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;
    p.ifidx = WIFI_IF_AP;   // the base talks on its soft-AP interface
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

void saveNodes() {
    StoredNodes s = {};
    portENTER_CRITICAL(&g_mux);
    for (int i = 0; i < cfg::kMaxNodes; ++i) {
        if (!g_nodes[i].used) continue;
        memcpy(s.n[s.count].mac, g_nodes[i].mac, 6);
        s.n[s.count].role = g_nodes[i].role;
        ++s.count;
    }
    portEXIT_CRITICAL(&g_mux);
    Preferences pr;
    pr.begin("tmnodes", false);
    pr.putBytes("nodes", &s, sizeof s);
    pr.end();
}

void loadNodes() {
    StoredNodes s = {};
    Preferences pr;
    pr.begin("tmnodes", true);
    const size_t n = pr.getBytes("nodes", &s, sizeof s);
    pr.end();
    if (n != sizeof s || s.count > cfg::kMaxNodes) return;
    for (int i = 0; i < s.count; ++i) {
        g_nodes[i].used = true;
        memcpy(g_nodes[i].mac, s.n[i].mac, 6);
        g_nodes[i].role = s.n[i].role;
    }
}

int findNode(const uint8_t* mac) {
    for (int i = 0; i < cfg::kMaxNodes; ++i)
        if (g_nodes[i].used && memcmp(g_nodes[i].mac, mac, 6) == 0) return i;
    return -1;
}

int addNode(const uint8_t* mac) {
    bool have_room = false;
    for (auto& n : g_nodes) have_room |= n.used && n.role == tmp::kRoleRoom;
    for (int i = 0; i < cfg::kMaxNodes; ++i) {
        if (g_nodes[i].used) continue;
        portENTER_CRITICAL(&g_mux);
        g_nodes[i] = NodeEntry{};
        g_nodes[i].used = true;
        memcpy(g_nodes[i].mac, mac, 6);
        g_nodes[i].role = have_room ? tmp::kRoleBathroom : tmp::kRoleRoom;   // first node = room
        portEXIT_CRITICAL(&g_mux);
        saveNodes();
        return i;
    }
    return -1;
}

uint8_t timeSrcWire() {
    switch (timekeeper::source()) {
        case TimeSrc::Manual: return tmp::kTimePhone;
        case TimeSrc::NodeRtc: return tmp::kTimeNodeRtc;
        default: return tmp::kTimeNone;
    }
}

void sendAck(const uint8_t* mac, uint8_t role) {
    const Snapshot s = shared::read();
    tmp::Ack a = {};
    tmp::initHeader(a.h, tmp::kAck, g_net_id, 0);
    a.unix_time = timekeeper::now();
    a.time_src = a.unix_time ? timeSrcWire() : static_cast<uint8_t>(tmp::kTimeNone);
    a.role = role;
    a.report_s = cfg::kReportS;
    a.setpoint = tmp::toCenti(s.st.sp);
    a.control_t = s.st.fusion.src != tmc::TempSource::None ? tmp::toCenti(s.st.fusion.t_c) : INT16_MIN;
    a.valve = s.st.valve_open ? 1 : (s.st.lockout_s > 0 ? 2 : 0);
    a.next_s = s.st.next_action_s > 0xFFFE ? 0xFFFF : static_cast<uint16_t>(s.st.next_action_s);
    a.mode = static_cast<uint8_t>(s.mode);
    a.alarms = static_cast<uint8_t>(s.st.alarms & 0xFF);
    esp_now_send(mac, reinterpret_cast<const uint8_t*>(&a), sizeof a);
}

void handle(const RxItem& it) {
    union { tmp::Hello hello; tmp::Telemetry tel; } u;
    const uint8_t type = tmp::validate(it.data, it.len, 0, &u, sizeof u);
    if (type == tmp::kHello) {
        int idx = findNode(it.mac);
        if (idx < 0) {
            if (!g_pair_open) return;               // strangers only during the pairing window
            idx = addNode(it.mac);
            if (idx < 0) return;
            Serial.printf("[radio] paired new node %02X:%02X:%02X:%02X:%02X:%02X role %u\n", it.mac[0], it.mac[1],
                          it.mac[2], it.mac[3], it.mac[4], it.mac[5], g_nodes[idx].role);
        }
        ensurePeer(it.mac);
        tmp::HelloAck a = {};
        tmp::initHeader(a.h, tmp::kHelloAck, g_net_id, 0);
        memcpy(a.base_mac, g_ap_mac, 6);
        a.channel = g_channel;
        a.role = g_nodes[idx].role;
        esp_now_send(it.mac, reinterpret_cast<const uint8_t*>(&a), sizeof a);
        return;
    }
    if (type != tmp::kTelemetry || u.tel.h.net_id != g_net_id) return;
    const int idx = findNode(it.mac);
    if (idx < 0) return;                            // not one of ours
    NodeEntry& n = g_nodes[idx];
    const uint32_t now = millis();
    const bool dup = n.seq_valid && u.tel.h.seq == n.last_seq && now - n.last_rx_ms < 5000;
    portENTER_CRITICAL(&g_mux);
    n.seq_valid = true;
    n.last_seq = u.tel.h.seq;
    n.last_rx_ms = now;
    n.heard = true;
    n.rssi = it.rssi;
    portEXIT_CRITICAL(&g_mux);
    ensurePeer(it.mac);
    sendAck(it.mac, n.role);
    if (dup) return;                                // retransmission: ACK again, don't count twice

    if ((u.tel.status & tmp::kStRtcValid) && tmu::plausibleUnix(u.tel.rtc_time))
        timekeeper::offer(TimeSrc::NodeRtc, u.tel.rtc_time);

    int16_t t = 0, rh = 0;
    if (!(u.tel.status & tmp::kStSensorError) && tmp::findReading(u.tel, tmp::kAirT, 0, &t)) {
        RoomSample s;
        s.rx_ms = now;
        s.t_c = tmp::fromCenti(t);
        s.rh = tmp::findReading(u.tel, tmp::kRelHum, 0, &rh) ? tmp::fromCenti(rh) : -1.0;
        s.role = n.role;
        shared::postRoomSample(s);
    }
}

void radioTask(void*) {
    RxItem it;
    for (;;) {
        if (xQueueReceive(g_rxq, &it, pdMS_TO_TICKS(500)) == pdTRUE) handle(it);
        if (g_pair_open && static_cast<int32_t>(millis() - g_pair_until_ms) >= 0) {
            g_pair_open = false;
            Serial.println("[radio] pairing window closed");
        }
    }
}
}  // namespace

namespace radio_base {

bool begin(uint8_t net_id, uint8_t channel, const char* ssid, const char* pass) {
    g_net_id = net_id;
    g_channel = channel;
    g_rxq = xQueueCreate(8, sizeof(RxItem));
    loadNodes();
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, pass, channel, 0, 4)) Serial.println("[radio] softAP FAILED");
    esp_wifi_get_mac(WIFI_IF_AP, g_ap_mac);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[radio] esp_now_init FAILED");
        return false;
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onRecv);
    const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ensurePeer(bc);
    for (auto& n : g_nodes)
        if (n.used) ensurePeer(n.mac);
    Serial.printf("[radio] AP '%s' ch %u, MAC %02X:%02X:%02X:%02X:%02X:%02X, net %u\n", ssid, channel, g_ap_mac[0],
                  g_ap_mac[1], g_ap_mac[2], g_ap_mac[3], g_ap_mac[4], g_ap_mac[5], net_id);
    return true;
}

void startTask() { xTaskCreatePinnedToCore(radioTask, "radio", 6144, nullptr, 4, nullptr, 0); }

void openPairing() {
    g_pair_until_ms = millis() + cfg::kPairWindowMs;
    g_pair_open = true;
    Serial.println("[radio] pairing window open for 60 s");
}

bool pairingOpen() { return g_pair_open; }

void forgetAll() {
    for (auto& n : g_nodes)
        if (n.used) esp_now_del_peer(n.mac);   // outside the critical section (may block)
    portENTER_CRITICAL(&g_mux);
    for (auto& n : g_nodes) n = NodeEntry{};
    portEXIT_CRITICAL(&g_mux);
    saveNodes();
}

static const NodeEntry* roomNode() {
    for (auto& n : g_nodes)
        if (n.used && n.role == tmp::kRoleRoom) return &n;
    return nullptr;
}

bool roomPaired() { return roomNode() != nullptr; }

int8_t roomRssi() {
    const NodeEntry* n = roomNode();
    return n ? n->rssi : 0;
}

uint32_t roomAgeS() {
    const NodeEntry* n = roomNode();
    if (!n || !n->heard) return UINT32_MAX;
    return (millis() - n->last_rx_ms) / 1000u;
}

}  // namespace radio_base
