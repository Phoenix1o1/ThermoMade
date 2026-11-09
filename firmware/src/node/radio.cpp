#include "radio.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "node_config.h"

namespace {
QueueHandle_t g_rxq = nullptr;
SemaphoreHandle_t g_txdone = nullptr;
volatile bool g_tx_ok = false;
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Callbacks run in the Wi-Fi task: copy and return immediately.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSent(const esp_now_send_info_t* /*info*/, esp_now_send_status_t status) {
#else
void onSent(const uint8_t* /*mac*/, esp_now_send_status_t status) {
#endif
    g_tx_ok = (status == ESP_NOW_SEND_SUCCESS);
    if (g_txdone) xSemaphoreGive(g_txdone);
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!info || !data || len <= 0 || len > tmp::kMaxFrame || !g_rxq) return;
    RxFrame f;
    memcpy(f.mac, info->src_addr, 6);
    f.len = len;
    memcpy(f.data, data, static_cast<size_t>(len));
    xQueueSend(g_rxq, &f, 0);   // drop if full: the next report retries anyway
}
}  // namespace

bool Radio::begin() {
    g_rxq = xQueueCreate(4, sizeof(RxFrame));
    g_txdone = xSemaphoreCreateBinary();

    Preferences pr;
    pr.begin("tmnode", true);
    pair_.paired = pr.getBool("paired", false);
    pr.getBytes("mac", pair_.base_mac, 6);
    pair_.net_id = pr.getUChar("net", 0);
    pair_.channel = pr.getUChar("ch", 1);
    pair_.role = pr.getUChar("role", tmp::kRoleNone);
    pair_.report_s = pr.getUChar("rep", static_cast<uint8_t>(cfg::kReportDefaultS));
    pr.end();
    if (pair_.channel < 1 || pair_.channel > 13) pair_.channel = 1;
    if (pair_.report_s < 10) pair_.report_s = static_cast<uint8_t>(cfg::kReportDefaultS);
    if (pair_.net_id == 0) pair_.paired = false;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    setChannel(pair_.paired ? pair_.channel : 1);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[radio] esp_now_init FAILED");
        return false;
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onRecv);
    ensurePeer(kBroadcast);
    if (pair_.paired) ensurePeer(pair_.base_mac);
    Serial.printf("[radio] MAC %s, %s", WiFi.macAddress().c_str(), pair_.paired ? "paired" : "NOT paired\n");
    if (pair_.paired)
        Serial.printf(" to %02X:%02X:%02X:%02X:%02X:%02X net %u ch %u role %u\n", pair_.base_mac[0], pair_.base_mac[1],
                      pair_.base_mac[2], pair_.base_mac[3], pair_.base_mac[4], pair_.base_mac[5], pair_.net_id,
                      pair_.channel, pair_.role);
    return true;
}

bool Radio::setChannel(uint8_t ch) {
    esp_wifi_set_promiscuous(true);
    const esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    return e == ESP_OK;
}

bool Radio::ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;              // 0 = whatever channel the radio is on
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

bool Radio::sendRaw(const uint8_t* mac, const uint8_t* data, size_t len) {
    xSemaphoreTake(g_txdone, 0);   // clear a stale completion
    g_tx_ok = false;
    if (esp_now_send(mac, data, len) != ESP_OK) return false;
    if (xSemaphoreTake(g_txdone, pdMS_TO_TICKS(cfg::kSendTimeoutMs)) != pdTRUE) return false;
    return g_tx_ok;
}

bool Radio::sendToBase(const uint8_t* data, size_t len) {
    if (!pair_.paired) return false;
    for (int i = 0; i < cfg::kSendRetries; ++i) {
        if (sendRaw(pair_.base_mac, data, len)) return true;
        delay(15 + (esp_random() % 30));
    }
    return false;
}

bool Radio::rescan(const uint8_t* data, size_t len) {
    if (!pair_.paired) return false;
    for (uint8_t ch = 1; ch <= 13; ++ch) {
        setChannel(ch);
        if (sendRaw(pair_.base_mac, data, len)) {
            if (ch != pair_.channel) {
                Serial.printf("[radio] base found on channel %u (was %u)\n", ch, pair_.channel);
                pair_.channel = ch;
                save();
            }
            return true;
        }
    }
    setChannel(pair_.channel);
    return false;
}

bool Radio::helloSweep(uint8_t node_type) {
    tmp::Hello h = {};
    tmp::initHeader(h.h, tmp::kHello, 0, nextSeq());
    h.node_type = node_type;
    h.fw_major = TM_FW_MAJOR;
    h.fw_minor = TM_FW_MINOR;
    for (uint8_t ch = 1; ch <= 13; ++ch) {
        setChannel(ch);
        sendRaw(kBroadcast, reinterpret_cast<const uint8_t*>(&h), sizeof h);
        RxFrame f;
        const uint32_t t0 = millis();
        while (millis() - t0 < 100) {
            if (!receive(f, 20)) continue;
            tmp::HelloAck a;
            if (tmp::validate(f.data, f.len, 0, &a, sizeof a) != tmp::kHelloAck) continue;
            if (memcmp(a.base_mac, f.mac, 6) != 0 || a.h.net_id == 0 || a.channel < 1 || a.channel > 13) continue;
            pair_.paired = true;
            memcpy(pair_.base_mac, a.base_mac, 6);
            pair_.net_id = a.h.net_id;
            pair_.channel = a.channel;
            pair_.role = a.role;
            save();
            setChannel(pair_.channel);
            ensurePeer(pair_.base_mac);
            Serial.printf("[radio] PAIRED with %02X:%02X:%02X:%02X:%02X:%02X net %u ch %u role %u\n", f.mac[0], f.mac[1],
                          f.mac[2], f.mac[3], f.mac[4], f.mac[5], pair_.net_id, pair_.channel, pair_.role);
            return true;
        }
    }
    setChannel(1);
    return false;
}

bool Radio::receive(RxFrame& f, uint32_t wait_ms) {
    return g_rxq && xQueueReceive(g_rxq, &f, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void Radio::setReportS(uint8_t s) {
    if (s < 10 || s == pair_.report_s) return;
    pair_.report_s = s;
    save();
}

void Radio::setRole(uint8_t r) {
    if (r == pair_.role) return;
    pair_.role = r;
    save();
}

void Radio::forget() {
    if (pair_.paired) esp_now_del_peer(pair_.base_mac);
    pair_ = Pairing{};
    Preferences pr;
    pr.begin("tmnode", false);
    pr.clear();
    pr.end();
    Serial.println("[radio] pairing forgotten");
}

void Radio::save() {
    Preferences pr;
    pr.begin("tmnode", false);
    pr.putBool("paired", pair_.paired);
    pr.putBytes("mac", pair_.base_mac, 6);
    pr.putUChar("net", pair_.net_id);
    pr.putUChar("ch", pair_.channel);
    pr.putUChar("role", pair_.role);
    pr.putUChar("rep", pair_.report_s);
    pr.end();
}
