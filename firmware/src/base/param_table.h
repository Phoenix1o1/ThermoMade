// TermoMade base — name table for the tunable parameters (serial CLI now, web API later).
#pragma once
#include <cstddef>
#include <cstdint>
#include "tm_params.h"

enum class PType : uint8_t { U8, U16, U32, F64, Bool, Enum };

struct ParamDesc {
    const char* name;
    PType type;
    size_t offset;
    const char* unit;
    double step;        // touch-screen -/+ step
    double max_enum;    // Enum/Bool: highest value (the -/+ buttons wrap around)
};

#define TM_P(field, type, unit, step) {#field, PType::type, offsetof(tmc::ControlParams, field), unit, step, 1}
#define TM_E(field, unit, maxv) {#field, PType::Enum, offsetof(tmc::ControlParams, field), unit, 1, maxv}

// Order = order on the AVANZATE page. Ranges are enforced by tmc::sanitize() (docs/04 §7).
static const ParamDesc kParams[] = {
    TM_P(sp_comfort, F64, "C", 0.5),          TM_P(sp_night, F64, "C", 0.5),          TM_P(sp_away, F64, "C", 0.5),
    TM_P(antifreeze_in_off, Bool, "1=yes", 1), TM_P(preheat_min, U16, "min", 15),
    TM_P(arrival_force_min, U16, "min", 15),  TM_P(arrival_stop_c, F64, "C", 0.5),
    TM_E(recovery_schedule, "0=eff 1=fast", 1), TM_E(recovery_user, "0=eff 1=fast", 1),
    TM_E(recovery_arrival, "0=eff 1=fast", 1),  TM_P(recovery_done_c, F64, "C", 0.1),
    TM_P(on_min_s, U32, "s", 10),             TM_P(off_min_s, U32, "s", 10),          TM_P(max_cycles_per_hour, U8, "/h", 1),
    TM_P(kp, F64, "1/C", 0.05),               TM_P(ti_s, F64, "s", 600),              TM_P(td_s, F64, "s", 60),
    TM_P(tf_s, F64, "s", 10),                 TM_P(i_band_c, F64, "C", 0.1),          TM_P(i0, F64, "", 0.01),
    TM_P(u_min, F64, "", 0.01),               TM_P(u_cont, F64, "", 0.01),            TM_P(tick_s, U32, "s", 5),
    TM_P(stale_s, U32, "s", 30),              TM_P(w_room, F64, "", 0.05),            TM_P(tau_offset_s, F64, "s", 1800),
    TM_P(offset_learn_max_slope_c_h, F64, "C/h", 0.1), TM_P(offset_min_learn_s, F64, "s", 600),
    TM_P(disagree_c, F64, "C", 0.1),          TM_P(disagree_s, U32, "s", 60),         TM_P(median3, Bool, "1=on", 1),
    TM_E(fail_policy, "0=limp 1=off", 1),     TM_P(limp_min, F64, "", 0.01),          TM_P(limp_max, F64, "", 0.01),
    TM_P(limp_default, F64, "", 0.01),        TM_E(algo, "0=pulse 1=classic 2=alt", 2),
    TM_P(classic_hyst_c, F64, "C", 0.05),     TM_P(cont_warn_s, U32, "s", 1800),      TM_P(budget_min_day, U32, "min", 10),
    TM_P(rh_low, F64, "%", 1),                TM_P(rh_high, F64, "%", 1),             TM_P(rh_hyst, F64, "%", 0.5),
    TM_P(room_offset_c, F64, "C", 0.1),
};
#undef TM_P
#undef TM_E

constexpr int kParamCount = sizeof(kParams) / sizeof(kParams[0]);

inline int findParam(const char* name) {
    for (int i = 0; i < kParamCount; ++i) {
        const char* a = kParams[i].name;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

inline double getParam(const tmc::ControlParams& p, int i) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&p) + kParams[i].offset;
    switch (kParams[i].type) {
        case PType::U8: case PType::Enum: return *reinterpret_cast<const uint8_t*>(base);
        case PType::U16: return *reinterpret_cast<const uint16_t*>(base);
        case PType::U32: return *reinterpret_cast<const uint32_t*>(base);
        case PType::F64: return *reinterpret_cast<const double*>(base);
        case PType::Bool: return *reinterpret_cast<const bool*>(base) ? 1.0 : 0.0;
    }
    return 0.0;
}

inline void setParam(tmc::ControlParams& p, int i, double v) {
    uint8_t* base = reinterpret_cast<uint8_t*>(&p) + kParams[i].offset;
    if (!(v == v)) return;
    switch (kParams[i].type) {
        case PType::U8: case PType::Enum: *reinterpret_cast<uint8_t*>(base) = static_cast<uint8_t>(v < 0 ? 0 : v); break;
        case PType::U16: *reinterpret_cast<uint16_t*>(base) = static_cast<uint16_t>(v < 0 ? 0 : v); break;
        case PType::U32: *reinterpret_cast<uint32_t*>(base) = static_cast<uint32_t>(v < 0 ? 0 : v); break;
        case PType::F64: *reinterpret_cast<double*>(base) = v; break;
        case PType::Bool: *reinterpret_cast<bool*>(base) = v != 0.0; break;
    }
    // sanitize() is applied by Controller::setParams afterwards
}
