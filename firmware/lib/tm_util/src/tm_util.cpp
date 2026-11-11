#include "tm_util.h"

namespace tmu {

uint8_t crc8Sensirion(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

bool sht4xDecode(const uint8_t raw[6], double* t_c, double* rh_pct) {
    if (crc8Sensirion(raw, 2) != raw[2] || crc8Sensirion(raw + 3, 2) != raw[5]) return false;
    const uint16_t t_ticks = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    const uint16_t rh_ticks = static_cast<uint16_t>((raw[3] << 8) | raw[4]);
    const double t = -45.0 + 175.0 * t_ticks / 65535.0;
    double rh = -6.0 + 125.0 * rh_ticks / 65535.0;
    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    if (t_c) *t_c = t;
    if (rh_pct) *rh_pct = rh;
    return true;
}

bool sht3xDecode(const uint8_t raw[6], double* t_c, double* rh_pct) {
    if (crc8Sensirion(raw, 2) != raw[2] || crc8Sensirion(raw + 3, 2) != raw[5]) return false;
    const uint16_t t_ticks = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    const uint16_t rh_ticks = static_cast<uint16_t>((raw[3] << 8) | raw[4]);
    if (t_c) *t_c = -45.0 + 175.0 * t_ticks / 65535.0;
    if (rh_pct) *rh_pct = 100.0 * rh_ticks / 65535.0;
    return true;
}

// Howard Hinnant's algorithms (public domain)
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    *y = static_cast<int>(yy + (mm <= 2));
    *m = mm;
    *d = dd;
}

uint32_t toUnix(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s) {
    const int64_t days = daysFromCivil(y, mo, d);
    return static_cast<uint32_t>(days * 86400 + h * 3600 + mi * 60 + s);
}

uint8_t weekdayMon0(uint32_t unix_s) {
    // 1970-01-01 was a Thursday (= 3 when Monday = 0)
    return static_cast<uint8_t>((unix_s / 86400u + 3u) % 7u);
}

bool ds3231Decode(const uint8_t reg[7], uint32_t* unix_s) {
    const uint8_t sec = bcd2bin(reg[0] & 0x7F);
    const uint8_t min = bcd2bin(reg[1] & 0x7F);
    uint8_t hour;
    if (reg[2] & 0x40) {                       // 12 h mode
        hour = bcd2bin(reg[2] & 0x1F) % 12;
        if (reg[2] & 0x20) hour = static_cast<uint8_t>(hour + 12);
    } else {
        hour = bcd2bin(reg[2] & 0x3F);
    }
    const uint8_t date = bcd2bin(reg[4] & 0x3F);
    const uint8_t month = bcd2bin(reg[5] & 0x1F);
    const int year = 2000 + bcd2bin(reg[6]) + ((reg[5] & 0x80) ? 100 : 0);
    if (sec > 59 || min > 59 || hour > 23 || date < 1 || date > 31 || month < 1 || month > 12) return false;
    const uint32_t t = toUnix(year, month, date, hour, min, sec);
    if (unix_s) *unix_s = t;
    return true;
}

void ds3231Encode(uint32_t unix_s, uint8_t reg[7]) {
    const uint32_t days = unix_s / 86400u;
    const uint32_t rem = unix_s % 86400u;
    int y;
    unsigned m, d;
    civilFromDays(days, &y, &m, &d);
    reg[0] = bin2bcd(static_cast<uint8_t>(rem % 60u));
    reg[1] = bin2bcd(static_cast<uint8_t>((rem / 60u) % 60u));
    reg[2] = bin2bcd(static_cast<uint8_t>(rem / 3600u));     // 24 h mode (bit 6 = 0)
    reg[3] = static_cast<uint8_t>(weekdayMon0(unix_s) + 1);  // 1..7, not used for decoding
    reg[4] = bin2bcd(static_cast<uint8_t>(d));
    reg[5] = bin2bcd(static_cast<uint8_t>(m));
    reg[6] = bin2bcd(static_cast<uint8_t>((y - 2000) % 100));
}

}  // namespace tmu
