// TermoMade control library — common types.
// Pure C++17, no Arduino / ESP-IDF includes: compiles on the PC for unit tests.
#pragma once
#include <cstdint>

namespace tmc {

constexpr uint32_t kForeverMs = UINT32_MAX;   // "never" for timer targets

// Operating mode chosen by the user. BOOST is an overlay, not a mode.
enum class Mode : uint8_t { Auto = 0, Comfort = 1, Night = 2, Away = 3, Off = 4 };

// Period of the weekly schedule.
enum class Period : uint8_t { Comfort = 0, Night = 1 };

// Control algorithm. Classic = +-hysteresis thermostat, used for A/B comparison days.
enum class Algo : uint8_t { Pulse = 0, Classic = 1, AlternateDays = 2 };

// What to do when no valid control temperature exists.
enum class FailPolicy : uint8_t { Limp = 0, FailOff = 1 };

// How to recover after a setpoint increase (docs/04 §5).
// How a setpoint increase is recovered (docs/04 §5):
//  Efficient = pulses only (u <= uc), cheapest per billed minute, slowest          -> profile ECO
//  Balanced  = valve may stay open while far (> recovery_wide_c) below the setpoint,
//              pulses only once closer                                            -> profile BALANCED
//  Fast      = no cap, the PI may lengthen pulses / stay open until the setpoint  -> profile PERFORMANCE
enum class Recovery : uint8_t { Efficient = 0, Fast = 1, Balanced = 2 };

// Which sensors produced the control temperature.
enum class TempSource : uint8_t { None = 0, Fused = 1, RoomOnly = 2, BaseOnly = 3 };

// Why the valve is in its current state (for UI + logs).
enum class ValveReason : uint8_t {
    Boot = 0,        // initial lockout after power-up
    Pulse = 1,       // normal modulated pulse
    Continuous = 2,  // demand >= U_CONT
    Classic = 3,     // classic thermostat call
    Boost = 4,       // user boost
    Limp = 5,        // no sensor, open-loop
    TargetReached = 6,
    NoDemand = 7,
    UserOff = 8,     // mode OFF without antifreeze
    PlantOff = 9,    // outside central-plant hours
    Fault = 10,      // FAIL_OFF policy, sensor lost
    CycleLimit = 11, // MAX_CYCLES_PER_HOUR reached (open deferred)
    Arrival = 12     // ARRIVO: forced heating after arriving in a cold house
};

enum class HumidityAdvice : uint8_t { Unknown = 0, Ok = 1, TooDry = 2, TooHumid = 3 };

// Wall-clock information supplied by the firmware (RTC / phone). valid=false when unknown.
struct WallTime {
    bool valid = false;
    uint8_t wday = 0;            // 0 = Monday ... 6 = Sunday
    uint16_t minute = 0;         // minute of the day, 0..1439
    uint32_t day_number = 0;     // days since 1970-01-01 (local), for A/B alternation and day counters
};

const char* toString(Mode m);
const char* toString(ValveReason r);
const char* toString(TempSource s);

}  // namespace tmc
