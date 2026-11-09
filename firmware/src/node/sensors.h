// TermoMade room node — climate sensor (SHT4x or DHT22, chosen at build time) and DS3231 RTC.
#pragma once
#include <Arduino.h>
#include <Wire.h>

struct ClimateReading {
    bool ok = false;
    double t_c = 0.0;
    double rh = -1.0;
};

class ClimateSensor {
public:
    bool begin();
    ClimateReading read();       // retries internally; ok=false if every attempt failed
    const char* name() const;
    uint32_t errors() const { return errors_; }
private:
    uint32_t errors_ = 0;
    bool present_ = false;
};

class Ds3231 {
public:
    // Detects the chip and makes sure the oscillator runs on battery (EOSC = 0).
    bool begin(TwoWire& w);
    bool present() const { return present_; }
    // valid=false if the oscillator stopped since the last set (battery dead / first use).
    bool read(uint32_t* unix_utc, bool* valid);
    // Sets the time (UTC) and clears the oscillator-stop flag.
    bool write(uint32_t unix_utc);
private:
    bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
    bool writeRegs(uint8_t reg, const uint8_t* buf, size_t n);
    TwoWire* w_ = nullptr;
    bool present_ = false;
};

// Free a stuck I2C bus (slave holding SDA low after a brown-out): clock SCL up to 9 times.
void i2cBusRecover(int sda, int scl);
