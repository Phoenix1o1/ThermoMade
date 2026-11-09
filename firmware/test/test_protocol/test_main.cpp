// Unit tests for the ESP-NOW protocol frames.
#include <unity.h>
#include <cstring>
#include "tm_protocol.h"

using namespace tmp;

void setUp() {}
void tearDown() {}

static void test_sizes() {
    TEST_ASSERT_EQUAL(8, sizeof(Header));
    TEST_ASSERT_EQUAL(21, kTelemetryFixed);
    TEST_ASSERT_EQUAL(21 + 16 * 4, sizeof(Telemetry));
    TEST_ASSERT_TRUE(sizeof(Ack) <= 250);
    TEST_ASSERT_EQUAL(29, telemetryLength(2));
}

static void test_telemetry_roundtrip() {
    Telemetry t{};
    initHeader(t.h, kTelemetry, 42, 7);
    t.node_type = kNodeRoom;
    t.rtc_time = 1794215700u;
    TEST_ASSERT_TRUE(addReading(t, kAirT, 0, toCenti(20.345)));
    TEST_ASSERT_TRUE(addReading(t, kRelHum, 0, toCenti(41.2)));
    const size_t len = telemetryLength(t.count);
    uint8_t wire[250];
    std::memcpy(wire, &t, len);
    Telemetry r{};
    TEST_ASSERT_EQUAL(kTelemetry, validate(wire, int(len), 42, &r, sizeof r));
    int16_t v = 0;
    TEST_ASSERT_TRUE(findReading(r, kAirT, 0, &v));
    TEST_ASSERT_EQUAL_INT16(2035, v);   // rounded
    TEST_ASSERT_TRUE(findReading(r, kRelHum, 0, &v));
    TEST_ASSERT_EQUAL_INT16(4120, v);
    TEST_ASSERT_FALSE(findReading(r, kOutdoorT, 0, &v));
    TEST_ASSERT_EQUAL_UINT32(1794215700u, r.rtc_time);
    TEST_ASSERT_EQUAL_UINT16(7, r.h.seq);
}

static void test_validate_rejects() {
    Telemetry t{};
    initHeader(t.h, kTelemetry, 42, 1);
    addReading(t, kAirT, 0, 2000);
    uint8_t wire[250];
    const size_t len = telemetryLength(t.count);
    std::memcpy(wire, &t, len);
    TEST_ASSERT_EQUAL(0, validate(wire, int(len), 43));          // wrong network
    TEST_ASSERT_EQUAL(0, validate(wire, int(len) - 1, 42));      // truncated readings
    TEST_ASSERT_EQUAL(0, validate(wire, 4, 42));                 // shorter than a header
    wire[0] ^= 0xFF;
    TEST_ASSERT_EQUAL(0, validate(wire, int(len), 42));          // bad magic
    wire[0] ^= 0xFF;
    wire[2] = 2;
    TEST_ASSERT_EQUAL(0, validate(wire, int(len), 42));          // future major version
    wire[2] = kVersion;
    wire[offsetof(Telemetry, count)] = 17;
    TEST_ASSERT_EQUAL(0, validate(wire, 250, 42));               // count too large
    wire[3] = 99;
    TEST_ASSERT_EQUAL(0, validate(wire, int(len), 42));          // unknown type
    TEST_ASSERT_EQUAL(0, validate(nullptr, 10, 42));
}

static void test_unknown_kind_skipped_and_longer_frame_ok() {
    Telemetry t{};
    initHeader(t.h, kTelemetry, 5, 1);
    addReading(t, Kind(200), 0, 123);   // a kind from a future firmware
    addReading(t, kAirT, 0, 1999);
    uint8_t wire[250] = {};
    std::memcpy(wire, &t, telemetryLength(t.count));
    Telemetry r{};
    // a newer minor version may append bytes: still accepted
    TEST_ASSERT_EQUAL(kTelemetry, validate(wire, int(telemetryLength(t.count) + 6), 5, &r, sizeof r));
    int16_t v;
    TEST_ASSERT_TRUE(findReading(r, kAirT, 0, &v));
    TEST_ASSERT_EQUAL_INT16(1999, v);
}

static void test_hello_any_net_and_ack() {
    Hello h{};
    initHeader(h.h, kHello, 0, 1);
    h.node_type = kNodeRoom;
    TEST_ASSERT_EQUAL(kHello, validate(reinterpret_cast<uint8_t*>(&h), sizeof h, 0));
    TEST_ASSERT_EQUAL(0, validate(reinterpret_cast<uint8_t*>(&h), sizeof h, 9));   // net 0 != 9
    Ack a{};
    initHeader(a.h, kAck, 9, 3);
    a.control_t = toCenti(19.87);
    Ack r{};
    TEST_ASSERT_EQUAL(kAck, validate(reinterpret_cast<uint8_t*>(&a), sizeof a, 9, &r, sizeof r));
    TEST_ASSERT_EQUAL_INT16(1987, r.control_t);
    TEST_ASSERT_EQUAL(0, validate(reinterpret_cast<uint8_t*>(&a), sizeof a, 9, &r, 3));   // copy too small
}

static void test_centi_saturation() {
    TEST_ASSERT_EQUAL_INT16(32767, toCenti(1000.0));
    TEST_ASSERT_EQUAL_INT16(-32767, toCenti(-1000.0));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, toCenti(0.0 / 0.0));
    TEST_ASSERT_EQUAL_INT16(-1255, toCenti(-12.546));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sizes);
    RUN_TEST(test_telemetry_roundtrip);
    RUN_TEST(test_validate_rejects);
    RUN_TEST(test_unknown_kind_skipped_and_longer_frame_ok);
    RUN_TEST(test_hello_any_net_and_ack);
    RUN_TEST(test_centi_saturation);
    return UNITY_END();
}
