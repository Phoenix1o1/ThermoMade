// TermoMade room node — ESP-NOW client: pairing, telemetry with retries, channel re-scan.
#pragma once
#include <Arduino.h>
#include "tm_protocol.h"

struct Pairing {
    bool paired = false;
    uint8_t base_mac[6] = {};
    uint8_t net_id = 0;
    uint8_t channel = 1;
    uint8_t role = tmp::kRoleNone;
    uint8_t report_s = 30;
};

struct RxFrame {
    uint8_t mac[6];
    int len;
    uint8_t data[tmp::kMaxFrame];
};

class Radio {
public:
    bool begin();                      // Wi-Fi STA (not connected) + ESP-NOW + saved pairing
    const Pairing& pairing() const { return pair_; }
    void forget();                     // clear pairing (NVS) -> HELLO mode

    // Unpaired: one HELLO sweep over channels 1..13. Returns true when a base answered.
    bool helloSweep(uint8_t node_type);

    // Paired: send a frame with MAC-level retries. Returns true if the base acknowledged it.
    bool sendToBase(const uint8_t* data, size_t len);
    // Paired but lost: try every channel until the base acknowledges a frame on one.
    bool rescan(const uint8_t* data, size_t len);

    // Next received frame (from the Wi-Fi task queue), validated against our network id.
    bool receive(RxFrame& f, uint32_t wait_ms);

    uint16_t nextSeq() { return ++seq_; }
    void setReportS(uint8_t s);
    void setRole(uint8_t r);

private:
    bool setChannel(uint8_t ch);
    bool ensurePeer(const uint8_t* mac);
    bool sendRaw(const uint8_t* mac, const uint8_t* data, size_t len);
    void save();

    Pairing pair_;
    uint16_t seq_ = 0;
};
