#include "tm_protocol.h"
#include <cmath>
#include <cstring>

namespace tmp {

void initHeader(Header& h, MsgType type, uint8_t net_id, uint16_t seq) {
    h.magic = kMagic;
    h.version = kVersion;
    h.type = type;
    h.net_id = net_id;
    h.flags = 0;
    h.seq = seq;
}

bool addReading(Telemetry& t, Kind kind, uint8_t index, int16_t value) {
    if (t.count >= kMaxReadings) return false;
    t.r[t.count].kind = kind;
    t.r[t.count].index = index;
    t.r[t.count].value = value;
    ++t.count;
    return true;
}

bool findReading(const Telemetry& t, Kind kind, uint8_t index, int16_t* value) {
    const uint8_t n = t.count > kMaxReadings ? kMaxReadings : t.count;
    for (uint8_t i = 0; i < n; ++i) {
        if (t.r[i].kind == kind && t.r[i].index == index) {
            if (value) *value = t.r[i].value;
            return true;
        }
    }
    return false;
}

int16_t toCenti(double v) {
    if (!(v == v)) return INT16_MIN;
    const double s = std::round(v * 100.0);
    if (s >= 32767.0) return 32767;
    if (s <= -32767.0) return -32767;
    return static_cast<int16_t>(s);
}

namespace {
size_t minLength(uint8_t type) {
    switch (type) {
        case kHello: return sizeof(Hello);
        case kHelloAck: return sizeof(HelloAck);
        case kTelemetry: return kTelemetryFixed;
        case kAck: return sizeof(Ack);
        case kCmd: return sizeof(Cmd);
        default: return 0;
    }
}
size_t structSize(uint8_t type) {
    switch (type) {
        case kHello: return sizeof(Hello);
        case kHelloAck: return sizeof(HelloAck);
        case kTelemetry: return sizeof(Telemetry);
        case kAck: return sizeof(Ack);
        case kCmd: return sizeof(Cmd);
        default: return 0;
    }
}
}  // namespace

uint8_t validate(const uint8_t* data, int len, uint8_t expect_net_id, void* copy, size_t copy_size) {
    if (!data || len < static_cast<int>(sizeof(Header)) || len > kMaxFrame) return 0;
    Header h;
    std::memcpy(&h, data, sizeof h);
    if (h.magic != kMagic || h.version != kVersion) return 0;
    const size_t need = minLength(h.type);
    if (need == 0 || static_cast<size_t>(len) < need) return 0;
    if (expect_net_id != 0 && h.net_id != expect_net_id) return 0;
    if (h.type == kTelemetry) {
        const uint8_t count = data[offsetof(Telemetry, count)];
        if (count > kMaxReadings) return 0;
        if (static_cast<size_t>(len) < telemetryLength(count)) return 0;
    }
    if (copy) {
        const size_t full = structSize(h.type);
        if (copy_size < full) return 0;
        std::memset(copy, 0, full);
        std::memcpy(copy, data, static_cast<size_t>(len) < full ? static_cast<size_t>(len) : full);
    }
    return h.type;
}

}  // namespace tmp
