// TermoMade control library — pulse modulator: demand u -> ON / OFF time targets.
//   u <  U_MIN            : no pulses                      (ON -,      OFF forever)
//   u <= uc               : pulse FREQUENCY (ON = ON_MIN,  OFF = ON_MIN*(1-u)/u)
//   uc < u < U_CONT       : pulse WIDTH     (ON = OFF_MIN*u/(1-u), OFF = OFF_MIN)
//   u >= U_CONT           : continuous      (ON forever,  OFF = OFF_MIN)
// with uc = ON_MIN / (ON_MIN + OFF_MIN). Mirrors thermal_sim.py::PulseModulator.
#pragma once
#include <cstdint>
#include "tm_params.h"

namespace tmc {

struct PulseTargets {
    uint32_t on_ms;     // kForeverMs = stay open
    uint32_t off_ms;    // kForeverMs = stay closed
    bool demand;        // false when u < U_MIN
};

PulseTargets pulseTargets(const ControlParams& p, double u);

}  // namespace tmc
