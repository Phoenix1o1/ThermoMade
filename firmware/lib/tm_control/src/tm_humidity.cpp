#include "tm_humidity.h"
#include <cmath>

namespace tmc {

namespace {
constexpr double kA = 17.62;
constexpr double kB = 243.12;   // degC
}

double dewPointC(double t_c, double rh_pct) {
    if (!(rh_pct > 0.0)) rh_pct = 0.01;
    if (rh_pct > 100.0) rh_pct = 100.0;
    const double g = std::log(rh_pct / 100.0) + kA * t_c / (kB + t_c);
    return kB * g / (kA - g);
}

double absHumidityGm3(double t_c, double rh_pct) {
    // saturation vapour pressure (hPa), Magnus; water vapour density via ideal gas law
    const double es = 6.112 * std::exp(kA * t_c / (kB + t_c));
    const double e = es * rh_pct / 100.0;
    return 216.7 * e / (273.15 + t_c);
}

HumidityAdvice HumidityAdvisor::update(double t_c, double rh, double out_t_c, double out_rh) {
    const ControlParams& p = *p_;
    if (!(rh >= 0.0 && rh <= 100.0) || !(t_c == t_c)) {
        a_ = HumidityAdvice::Unknown;
        return a_;
    }
    if (out_rh >= 0.0 && out_rh <= 100.0 && out_t_c > -60.0 && out_t_c < 60.0) {
        vent_helps_ = absHumidityGm3(out_t_c, out_rh) < absHumidityGm3(t_c, rh) - 0.5;
    } else {
        vent_helps_ = true;   // in winter outdoor air is almost always drier
    }
    switch (a_) {
        case HumidityAdvice::TooDry:
            if (rh > p.rh_low + p.rh_hyst) a_ = HumidityAdvice::Ok;
            break;
        case HumidityAdvice::TooHumid:
            if (rh < p.rh_high - p.rh_hyst) a_ = HumidityAdvice::Ok;
            break;
        default:
            if (rh < p.rh_low) a_ = HumidityAdvice::TooDry;
            else if (rh > p.rh_high) a_ = HumidityAdvice::TooHumid;
            else a_ = HumidityAdvice::Ok;
            break;
    }
    // a TooDry state can jump directly to TooHumid (and back) on big swings
    if (a_ == HumidityAdvice::Ok || a_ == HumidityAdvice::TooDry) {
        if (rh > p.rh_high) a_ = HumidityAdvice::TooHumid;
    }
    if (a_ == HumidityAdvice::TooHumid && rh < p.rh_low) a_ = HumidityAdvice::TooDry;
    return a_;
}

}  // namespace tmc
