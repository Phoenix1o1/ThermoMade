#include "tm_modulator.h"
#include <cmath>

namespace tmc {

namespace {
// Rounded UP (to the ms) so that "elapsed >= target" behaves exactly like the
// floating-point comparison in the simulator.
uint32_t secondsToMs(double s) {
    if (!(s >= 0.0)) return 0;
    const double ms = std::ceil(s * 1000.0 - 1e-6);
    if (ms >= static_cast<double>(kForeverMs - 1)) return kForeverMs - 1;  // finite but huge
    return static_cast<uint32_t>(ms);
}
}  // namespace

PulseTargets pulseTargets(const ControlParams& p, double u) {
    const double on_min = static_cast<double>(p.on_min_s);
    const double off_min = static_cast<double>(p.off_min_s);
    const double uc = crossoverDuty(p);
    PulseTargets t{};
    if (!(u == u) || u < p.u_min) {                 // includes NaN
        t.on_ms = secondsToMs(on_min);
        t.off_ms = kForeverMs;
        t.demand = false;
    } else if (u >= p.u_cont) {
        t.on_ms = kForeverMs;
        t.off_ms = secondsToMs(off_min);
        t.demand = true;
    } else if (u <= uc) {
        t.on_ms = secondsToMs(on_min);
        t.off_ms = secondsToMs(on_min * (1.0 - u) / u);
        t.demand = true;
    } else {
        t.on_ms = secondsToMs(off_min * u / (1.0 - u));
        t.off_ms = secondsToMs(off_min);
        t.demand = true;
    }
    return t;
}

}  // namespace tmc
