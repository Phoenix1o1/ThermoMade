// TermoMade control library — humidity helpers and winter advice.
#pragma once
#include "tm_params.h"
#include "tm_types.h"

namespace tmc {

// Dew point (degC), Magnus formula (Sonntag 1990 constants), valid about -45..60 degC.
double dewPointC(double t_c, double rh_pct);
// Absolute humidity (g/m3).
double absHumidityGm3(double t_c, double rh_pct);

// TooDry (< rh_low) -> suggest humidifying; TooHumid (> rh_high) -> suggest ventilating.
// Hysteresis rh_hyst on both thresholds. Optional outdoor data refines the ventilation advice:
// ventilating only helps if the outdoor air holds less water than the indoor air.
class HumidityAdvisor {
public:
    explicit HumidityAdvisor(const ControlParams& p) : p_(&p) {}
    void setParams(const ControlParams& p) { p_ = &p; }
    // rh < 0 -> unknown. out_t/out_rh optional (NaN or <0 rh = unknown).
    HumidityAdvice update(double t_c, double rh, double out_t_c = -1000.0, double out_rh = -1.0);
    HumidityAdvice advice() const { return a_; }
    bool ventilationHelps() const { return vent_helps_; }
private:
    const ControlParams* p_;
    HumidityAdvice a_ = HumidityAdvice::Unknown;
    bool vent_helps_ = true;
};

}  // namespace tmc
