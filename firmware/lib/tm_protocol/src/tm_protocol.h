// TermoMade ESP-NOW protocol v1 (docs/06). Shared by every node. Pure C++17.
// Frames are packed little-endian structs, always <= 250 bytes (ESP-NOW v1 payload limit).
// Identity of a node = its MAC address (from the ESP-NOW receive info), not a field.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tmp {

constexpr uint16_t kMagic = 0x4D54;        // bytes 'T','M' on the wire (little-endian)
constexpr uint8_t kVersion = 1;
constexpr int kMaxReadings = 16;
constexpr int kMaxFrame = 250;

enum MsgType : uint8_t { kHello = 1, kHelloAck = 2, kTelemetry = 3, kAck = 4, kCmd = 5 };

// Reading kinds. Unknown kinds must be skipped by receivers (forward compatibility).
enum Kind : uint8_t {
    kAirT = 1,        // degC x100
    kRelHum = 2,      // %RH x100
    kPipeSupplyT = 3, // degC x100
    kPipeReturnT = 4, // degC x100
    kOutdoorT = 5,    // degC x100
    kOutdoorRH = 6,   // %RH x100
};

enum NodeType : uint8_t { kNodeRoom = 1, kNodeOutdoor = 2, kNodePipe = 3 };

enum Role : uint8_t { kRoleNone = 0, kRoleRoom = 1, kRoleBathroom = 2, kRoleOutdoor = 3, kRolePipe = 4 };

// Telemetry status bits
enum : uint8_t {
    kStSensorError = 1u << 0,   // last sensor read failed (readings omitted)
    kStRtcPresent = 1u << 1,    // node has a DS3231
    kStRtcValid = 1u << 2,      // rtc_time is trustworthy (oscillator never stopped)
    kStLowBattery = 1u << 3,
};

// Header flags
enum : uint8_t { kFlagRetry = 1u << 0 };

// Ack time sources (best last)
enum TimeSrc : uint8_t { kTimeNone = 0, kTimeNodeRtc = 1, kTimePhone = 2 };

enum CmdCode : uint8_t { kCmdIdentify = 1, kCmdReboot = 2, kCmdForget = 3 };

#pragma pack(push, 1)
struct Header {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t net_id;     // 0 only in HELLO before pairing
    uint8_t flags;
    uint16_t seq;
};

struct Reading {
    uint8_t kind;
    uint8_t index;
    int16_t value;
};

struct Hello {                 // node -> broadcast (pairing / re-discovery)
    Header h;
    uint8_t node_type;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t reserved;
};

struct HelloAck {              // base -> node
    Header h;                  // h.net_id = the network id to use from now on
    uint8_t base_mac[6];
    uint8_t channel;
    uint8_t role;
};

struct Telemetry {             // node -> base, unicast
    Header h;
    uint8_t node_type;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t status;
    uint16_t battery_mv;       // 0 = mains/USB powered
    uint16_t uptime_min;
    uint32_t rtc_time;         // unix time from the node RTC, 0 = none
    uint8_t count;
    Reading r[kMaxReadings];   // only `count` entries are sent
};

struct Ack {                   // base -> node, answer to every telemetry
    Header h;
    uint32_t unix_time;        // 0 = base has no valid time
    uint8_t time_src;          // TimeSrc
    uint8_t role;
    uint8_t report_s;          // requested report interval
    int16_t setpoint;          // degC x100
    int16_t control_t;         // degC x100, INT16_MIN = unknown
    uint8_t valve;             // 0 closed, 1 open, 2 lockout
    uint16_t next_s;           // seconds to next valve action, 0xFFFF = none
    uint8_t mode;
    uint8_t alarms;            // low byte of the controller alarm bits
};

struct Cmd {                   // base -> node
    Header h;
    uint8_t cmd;
    uint8_t arg;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 8, "Header size");
static_assert(sizeof(Reading) == 4, "Reading size");
static_assert(sizeof(Telemetry) <= kMaxFrame, "Telemetry too big for ESP-NOW v1");
static_assert(sizeof(Ack) <= kMaxFrame, "Ack too big");

constexpr size_t kTelemetryFixed = offsetof(Telemetry, r);

// Fill a header.
void initHeader(Header& h, MsgType type, uint8_t net_id, uint16_t seq);

// Length on the wire of a telemetry frame with `count` readings.
inline size_t telemetryLength(uint8_t count) {
    return kTelemetryFixed + static_cast<size_t>(count) * sizeof(Reading);
}

// Add a reading; returns false if full.
bool addReading(Telemetry& t, Kind kind, uint8_t index, int16_t value);

// Find a reading of a given kind/index. Returns false if absent.
bool findReading(const Telemetry& t, Kind kind, uint8_t index, int16_t* value);

// Scale helpers (round to nearest, saturate).
int16_t toCenti(double v);
inline double fromCenti(int16_t v) { return static_cast<double>(v) / 100.0; }

// Validate a received frame. expect_net_id = 0 accepts any (used only for HELLO in pairing mode).
// On success returns the message type, otherwise 0. `copy` (optional) receives the frame,
// zero-padded to its struct size so short-but-valid frames are safe to read.
uint8_t validate(const uint8_t* data, int len, uint8_t expect_net_id, void* copy = nullptr, size_t copy_size = 0);

}  // namespace tmp
