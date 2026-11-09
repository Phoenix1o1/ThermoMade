// TermoMade pure helpers (no Arduino): sensor decoding and time arithmetic, unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tmu {

// ---- Sensirion SHT4x ----
// CRC-8: polynomial 0x31, init 0xFF (datasheet §4.4).
uint8_t crc8Sensirion(const uint8_t* data, size_t len);
// Decode the 6-byte answer of a measure command (T msb, lsb, crc, RH msb, lsb, crc).
// Returns false on CRC error. RH is clamped to 0..100 % (datasheet recommendation).
bool sht4xDecode(const uint8_t raw[6], double* t_c, double* rh_pct);

// ---- civil time (proleptic Gregorian, UTC) ----
int64_t daysFromCivil(int y, unsigned m, unsigned d);
void civilFromDays(int64_t z, int* y, unsigned* m, unsigned* d);
uint32_t toUnix(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s);
// Weekday of a unix time: 0 = Monday ... 6 = Sunday.
uint8_t weekdayMon0(uint32_t unix_s);

// ---- DS3231 ----
inline uint8_t bcd2bin(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
inline uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
// Registers 0x00..0x06 -> unix time (UTC). Returns false if a field is out of range.
bool ds3231Decode(const uint8_t reg[7], uint32_t* unix_s);
// unix time (UTC) -> registers 0x00..0x06 (24 h mode, century bit for 2100+ unused: 2000-2099).
void ds3231Encode(uint32_t unix_s, uint8_t reg[7]);

// Plausible "now" for this project: 2025-01-01 .. 2099-12-31.
inline bool plausibleUnix(uint32_t t) { return t >= 1735689600u && t < 4102444800u; }

}  // namespace tmu
