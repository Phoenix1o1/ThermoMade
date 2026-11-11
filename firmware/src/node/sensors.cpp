#include "sensors.h"
#include "node_config.h"
#include "tm_util.h"

#if defined(TM_NODE_SENSOR_DHT22)
#include <DHT.h>
static DHT g_dht(cfg::kPinDht, DHT22);
#endif

namespace {
constexpr uint8_t kShtAddr = 0x44;
#if defined(TM_NODE_SENSOR_SHT31)
// SHT30 / SHT31 / SHT35: 16-bit commands
constexpr uint16_t kShtMeasureHigh = 0x2400;   // single shot, high repeatability, no clock stretching (max 15.5 ms)
constexpr uint16_t kShtSoftReset = 0x30A2;
constexpr uint16_t kShtIdent = 0xF32D;         // status register (2 bytes + CRC): used to detect the chip
constexpr int kShtIdentLen = 3;
constexpr int kShtMeasureMs = 16;
#define TM_SHT_NAME "SHT31"
#else
constexpr uint8_t kShtMeasureHigh = 0xFD;   // high repeatability, max 8.3 ms
constexpr uint8_t kShtSoftReset = 0x94;
constexpr uint8_t kShtIdent = 0x89;         // serial number (2 x (2 bytes + CRC))
constexpr int kShtIdentLen = 6;
constexpr int kShtMeasureMs = 10;
#define TM_SHT_NAME "SHT4x"
#endif
constexpr uint8_t kRtcAddr = 0x68;

bool plausibleClimate(double t, double rh) {
    return t == t && t > -40.0 && t < 80.0 && (rh < 0.0 || (rh >= 0.0 && rh <= 100.0));
}

#if defined(TM_NODE_SENSOR_SHT4X)
bool shtCommand(uint16_t cmd) {
    Wire.beginTransmission(kShtAddr);
#if defined(TM_NODE_SENSOR_SHT31)
    Wire.write(static_cast<uint8_t>(cmd >> 8));
#endif
    Wire.write(static_cast<uint8_t>(cmd & 0xFF));
    return Wire.endTransmission() == 0;
}
bool shtReadN(uint8_t* buf, int n) {
    if (Wire.requestFrom(kShtAddr, static_cast<uint8_t>(n)) != n) return false;
    for (int i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(Wire.read());
    return true;
}
bool shtRead6(uint8_t* buf) { return shtReadN(buf, 6); }
bool shtDecode(const uint8_t* raw, double* t, double* rh) {
#if defined(TM_NODE_SENSOR_SHT31)
    return tmu::sht3xDecode(raw, t, rh);
#else
    return tmu::sht4xDecode(raw, t, rh);
#endif
}
#endif
}  // namespace

void i2cBusRecover(int sda, int scl) {
    Wire.end();
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, OUTPUT_OPEN_DRAIN);
    for (int i = 0; i < 9 && digitalRead(sda) == LOW; ++i) {
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
    }
    // STOP condition
    pinMode(sda, OUTPUT_OPEN_DRAIN);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda, HIGH);
    delayMicroseconds(5);
    Wire.begin(sda, scl, cfg::kI2cHz);
    Wire.setTimeOut(50);
}

// ------------------------------------------------------------------ climate sensor
const char* ClimateSensor::name() const {
#if defined(TM_NODE_SENSOR_SHT4X)
    return TM_SHT_NAME;
#else
    return "DHT22";
#endif
}

bool ClimateSensor::begin() {
#if defined(TM_NODE_SENSOR_SHT4X)
    shtCommand(kShtSoftReset);
    delay(2);
    uint8_t sn[6] = {};
    present_ = shtCommand(kShtIdent) && (delay(2), shtReadN(sn, kShtIdentLen)) && tmu::crc8Sensirion(sn, 2) == sn[2] &&
               (kShtIdentLen < 6 || tmu::crc8Sensirion(sn + 3, 2) == sn[5]);
    if (present_) Serial.printf("[sensor] " TM_SHT_NAME " found at 0x44 (id %02X%02X)\n", sn[0], sn[1]);
    else Serial.println("[sensor] " TM_SHT_NAME " NOT found at 0x44 (check wiring / pull-ups; ADR pin must be low)");
#else
    g_dht.begin();
    present_ = true;   // a DHT22 cannot be detected without reading it
    Serial.printf("[sensor] DHT22 on GPIO %d\n", cfg::kPinDht);
#endif
    return present_;
}

ClimateReading ClimateSensor::read() {
    ClimateReading r;
#if defined(TM_NODE_SENSOR_SHT4X)
    for (int attempt = 0; attempt < 3 && !r.ok; ++attempt) {
        uint8_t raw[6];
        double t, rh;
        if (shtCommand(kShtMeasureHigh)) {
            delay(kShtMeasureMs);
            if (shtRead6(raw) && shtDecode(raw, &t, &rh) && plausibleClimate(t, rh)) {
                r.ok = true;
                r.t_c = t;
                r.rh = rh;
                present_ = true;
                break;
            }
        }
        ++errors_;
        if (attempt == 1) i2cBusRecover(cfg::kPinSda, cfg::kPinScl);
        delay(20);
    }
#else
    // DHT22: max one conversion every 2 s; the library returns NaN on timeout / checksum error
    for (int attempt = 0; attempt < 2 && !r.ok; ++attempt) {
        if (attempt > 0) delay(2100);
        const float t = g_dht.readTemperature(false, true);   // force a new conversion
        const float h = g_dht.readHumidity(false);             // from the same conversion
        if (!isnan(t) && !isnan(h) && plausibleClimate(t, h)) {
            r.ok = true;
            r.t_c = t;
            r.rh = h;
        } else {
            ++errors_;
        }
    }
#endif
    return r;
}

// ------------------------------------------------------------------ DS3231
bool Ds3231::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
    w_->beginTransmission(kRtcAddr);
    w_->write(reg);
    if (w_->endTransmission(false) != 0) return false;
    if (w_->requestFrom(kRtcAddr, static_cast<uint8_t>(n)) != n) return false;
    for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(w_->read());
    return true;
}

bool Ds3231::writeRegs(uint8_t reg, const uint8_t* buf, size_t n) {
    w_->beginTransmission(kRtcAddr);
    w_->write(reg);
    for (size_t i = 0; i < n; ++i) w_->write(buf[i]);
    return w_->endTransmission() == 0;
}

bool Ds3231::begin(TwoWire& w) {
    w_ = &w;
    uint8_t ctrl = 0;
    present_ = readRegs(0x0E, &ctrl, 1);
    if (!present_) {
        Serial.println("[rtc] DS3231 not found (time will come from the base / phone only)");
        return false;
    }
    if (ctrl & 0x80) {            // EOSC set: oscillator would stop on battery -> clear it
        ctrl &= 0x7F;
        writeRegs(0x0E, &ctrl, 1);
    }
    uint32_t t;
    bool valid;
    if (read(&t, &valid)) Serial.printf("[rtc] DS3231 found, time %lu (%s)\n", static_cast<unsigned long>(t), valid ? "valid" : "NOT valid");
    return true;
}

bool Ds3231::read(uint32_t* unix_utc, bool* valid) {
    if (!present_) return false;
    uint8_t st = 0, reg[7];
    if (!readRegs(0x0F, &st, 1) || !readRegs(0x00, reg, 7)) return false;
    uint32_t t = 0;
    const bool decoded = tmu::ds3231Decode(reg, &t);
    if (unix_utc) *unix_utc = t;
    if (valid) *valid = decoded && !(st & 0x80) && tmu::plausibleUnix(t);   // OSF clear
    return decoded;
}

bool Ds3231::write(uint32_t unix_utc) {
    if (!present_ || !tmu::plausibleUnix(unix_utc)) return false;
    uint8_t reg[7];
    tmu::ds3231Encode(unix_utc, reg);
    if (!writeRegs(0x00, reg, 7)) return false;
    uint8_t st = 0;
    if (!readRegs(0x0F, &st, 1)) return false;
    st &= static_cast<uint8_t>(~0x80);   // clear OSF: time is valid from now on
    return writeRegs(0x0F, &st, 1);
}
