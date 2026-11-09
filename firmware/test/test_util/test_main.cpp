// Unit tests for tm_util: SHT4x decoding, civil time, DS3231 registers.
#include <unity.h>
#include "tm_util.h"

using namespace tmu;

void setUp() {}
void tearDown() {}

static void test_crc8_datasheet_vector() {
    const uint8_t d[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, crc8Sensirion(d, 2));   // Sensirion datasheet example
}

static void test_sht4x_decode() {
    // T ticks 0x6666 = 26214 -> -45 + 175*26214/65535 = 25.0 degC
    // RH ticks 0x7AE1 = 31457 -> -6 + 125*31457/65535 = 54.0 %
    uint8_t raw[6] = {0x66, 0x66, 0, 0x7A, 0xE1, 0};
    raw[2] = crc8Sensirion(raw, 2);
    raw[5] = crc8Sensirion(raw + 3, 2);
    double t = 0, rh = 0;
    TEST_ASSERT_TRUE(sht4xDecode(raw, &t, &rh));
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 25.0, t);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 54.0, rh);
    raw[1] ^= 1;   // corrupt
    TEST_ASSERT_FALSE(sht4xDecode(raw, &t, &rh));
    // RH clamped
    uint8_t hi[6] = {0x66, 0x66, 0, 0xFF, 0xFF, 0};
    hi[2] = crc8Sensirion(hi, 2);
    hi[5] = crc8Sensirion(hi + 3, 2);
    TEST_ASSERT_TRUE(sht4xDecode(hi, &t, &rh));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, rh);
}

static void test_civil_time() {
    TEST_ASSERT_EQUAL_UINT32(0u, toUnix(1970, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1794268800u, toUnix(2026, 11, 10, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(951782400u, toUnix(2000, 2, 29, 0, 0, 0));
    int y; unsigned m, d;
    civilFromDays(1794268800 / 86400, &y, &m, &d);
    TEST_ASSERT_EQUAL_INT(2026, y);
    TEST_ASSERT_EQUAL_UINT(11, m);
    TEST_ASSERT_EQUAL_UINT(10, d);
    TEST_ASSERT_EQUAL_UINT8(0, weekdayMon0(toUnix(2026, 11, 9, 12, 0, 0)));   // Monday
    TEST_ASSERT_EQUAL_UINT8(6, weekdayMon0(toUnix(2026, 11, 15, 23, 59, 59))); // Sunday
}

static void test_ds3231_roundtrip() {
    const uint32_t samples[] = {toUnix(2026, 11, 9, 21, 7, 33), toUnix(2028, 2, 29, 23, 59, 59),
                                toUnix(2099, 12, 31, 0, 0, 0), toUnix(2025, 1, 1, 0, 0, 0)};
    for (uint32_t t : samples) {
        uint8_t reg[7];
        ds3231Encode(t, reg);
        uint32_t back = 0;
        TEST_ASSERT_TRUE(ds3231Decode(reg, &back));
        TEST_ASSERT_EQUAL_UINT32(t, back);
    }
    // 12 h mode, 11 PM
    uint8_t r12[7] = {0x00, 0x30, static_cast<uint8_t>(0x40 | 0x20 | 0x11), 1, 0x09, 0x11, 0x26};
    uint32_t t = 0;
    TEST_ASSERT_TRUE(ds3231Decode(r12, &t));
    TEST_ASSERT_EQUAL_UINT32(toUnix(2026, 11, 9, 23, 30, 0), t);
    // garbage (all 0xFF from a missing chip) is rejected
    uint8_t bad[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(ds3231Decode(bad, &t));
    TEST_ASSERT_TRUE(plausibleUnix(toUnix(2026, 11, 9, 0, 0, 0)));
    TEST_ASSERT_FALSE(plausibleUnix(0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_datasheet_vector);
    RUN_TEST(test_sht4x_decode);
    RUN_TEST(test_civil_time);
    RUN_TEST(test_ds3231_roundtrip);
    return UNITY_END();
}
