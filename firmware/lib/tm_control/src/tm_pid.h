// TermoMade control library — PI(D) producing a duty demand u in [0, u_max].
// Mirrors tools/sim/thermal_sim.py::PID line by line (golden-vector tested).
#pragma once
#include "tm_params.h"

namespace tmc {

class Pid {
public:
    explicit Pid(const ControlParams& p) : p_(&p), i_(p.i0) {}

    void setParams(const ControlParams& p) { p_ = &p; }

    // One controller step. sp/meas in degC, dt in seconds, u_max = 1 or uc (efficient recovery).
    // Large dt (resume after a pause) re-initialises the filters instead of integrating a jump.
    double update(double sp, double meas, double dt, double u_max = 1.0);

    // Forget filter state (e.g. after the sensor was lost). Keeps the learned I.
    void resetFilter() { init_ = false; dm_ = 0.0; }

    double i() const { return i_; }
    void setI(double i) { i_ = clamp01(i); }
    double p() const { return p_term_; }
    double d() const { return d_term_; }
    double u() const { return u_; }
    double filtered() const { return mf_; }
    bool coasting() const { return coast_; }

private:
    static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    const ControlParams* p_;
    double i_;
    double mf_ = 0.0;      // filtered measurement
    double dm_ = 0.0;      // filtered derivative of measurement (degC/s)
    bool init_ = false;
    bool have_sp_ = false;
    double last_sp_ = 0.0;
    bool coast_ = false;
    double p_term_ = 0.0, d_term_ = 0.0, u_ = 0.0;
};

}  // namespace tmc
