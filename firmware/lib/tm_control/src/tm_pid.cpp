#include "tm_pid.h"

namespace tmc {

double Pid::update(double sp, double meas, double dt, double u_max) {
    const ControlParams& p = *p_;
    if (!(dt > 0.0)) dt = static_cast<double>(p.tick_s);
    // Resume after a long pause (sensor lost, plant off, ...): restart the filters, no jump.
    if (init_ && dt > 4.0 * static_cast<double>(p.tick_s)) {
        init_ = false;
        dt = static_cast<double>(p.tick_s);
    }
    if (!init_) {
        mf_ = meas;
        dm_ = 0.0;
        init_ = true;
    }
    if (have_sp_ && sp < last_sp_ - 0.05) coast_ = true;   // setpoint decreased -> coast
    last_sp_ = sp;
    have_sp_ = true;

    const double a = dt / (p.tf_s + dt);
    const double prev = mf_;
    mf_ += a * (meas - mf_);
    const double raw_d = (mf_ - prev) / dt;
    const double td4 = p.td_s / 4.0;
    const double ad = dt / ((td4 > dt ? td4 : dt) + dt);
    dm_ += ad * (raw_d - dm_);

    const double e = sp - mf_;
    if (e >= 0.0) coast_ = false;                         // cooled down to the new setpoint
    p_term_ = p.kp * e;
    d_term_ = -p.kp * p.td_s * dm_;
    const double u_unsat = p_term_ + i_ + d_term_;
    const double abs_e = e < 0.0 ? -e : e;
    if (abs_e <= p.i_band_c && !coast_ && !(u_unsat >= u_max && e > 0.0)) {
        i_ += p.kp * dt / p.ti_s * e;
    }
    i_ = clamp01(i_);
    double u = p_term_ + i_ + d_term_;
    if (u < 0.0) u = 0.0;
    if (u > u_max) u = u_max;
    u_ = u;
    return u;
}

}  // namespace tmc
