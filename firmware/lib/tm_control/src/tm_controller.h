// TermoMade control library — the complete heating controller.
// Owns: parameters, schedule, mode, sensor fusion, PI, modulator, valve FSM, counters, events.
// The firmware calls update() about once per second and drives the relay with the result.
// No I/O, no heap, no Arduino: unit-tested on the PC (test/) and against the simulator.
#pragma once
#include <cstdint>
#include "tm_fusion.h"
#include "tm_humidity.h"
#include "tm_modulator.h"
#include "tm_params.h"
#include "tm_pid.h"
#include "tm_schedule.h"
#include "tm_types.h"
#include "tm_valve_fsm.h"

namespace tmc {

struct ControllerInputs {
    uint32_t now_ms = 0;          // monotonic millis()
    WallTime wall;                // local wall time (valid=false if unknown)
    bool base_disturbed = false;  // CYD screen on recently -> no offset learning
    bool plant_on = true;         // central plant hours gate (true = allowed)
};

// What must survive a reboot (saved by the firmware, e.g. every 30 min).
struct PersistentState {
    double i_comfort = -1.0;      // learned duty, comfort band (<0 = unknown -> I0)
    double i_away = -1.0;         // learned duty, antifreeze band
    double base_offset = 0.0;
    double offset_learned_s = 0.0;
};

enum class EventType : uint8_t {
    ValveOpen = 0, ValveClose, ModeChange, SetpointChange, SourceChange,
    LimpEnter, LimpExit, AlarmOn, AlarmOff, DayRollover, Boot,
    Arrival                       // a = 1 forced start begins, 0 ends; x = temperature
};

// Alarm bits (Status::alarms)
enum : uint16_t {
    kAlarmRoomLost = 1u << 0,
    kAlarmBaseLost = 1u << 1,
    kAlarmNoSensor = 1u << 2,     // LIMP or FAIL_OFF active
    kAlarmDisagree = 1u << 3,
    kAlarmTimeUnknown = 1u << 4,  // AUTO without valid time
    kAlarmContOn = 1u << 5,       // valve open for > CONT_WARN
    kAlarmBudget = 1u << 6,       // daily billed budget exceeded
};

struct Event {
    uint64_t uptime_ms = 0;
    EventType type = EventType::Boot;
    uint8_t a = 0;                // e.g. ValveReason, Mode, TempSource, alarm bit index
    uint8_t b = 0;
    float x = 0.0f;               // e.g. setpoint, u, billed minutes
    float y = 0.0f;               // e.g. temperature, pulses
};

struct Status {
    FusionOut fusion;
    bool heating_enabled = true;
    double sp = 0.0;
    Mode mode = Mode::Auto;
    Period period = Period::Comfort;
    bool preheating = false;
    bool override_active = false;
    bool boost = false;
    uint32_t boost_left_s = 0;
    bool arrival = false;         // ARRIVO forced start running
    uint32_t arrival_left_s = 0;
    Algo algo_active = Algo::Pulse;
    bool recovering = false;
    Recovery recovery = Recovery::Fast;
    double u = 0.0, p = 0.0, i = 0.0, d = 0.0;
    bool coast = false;
    bool limp = false;
    bool waiting_sensors = false;
    bool valve_open = false;
    ValveReason reason = ValveReason::Boot;
    uint32_t next_action_s = 0;   // UINT32_MAX = none planned
    uint32_t lockout_s = 0;
    uint32_t billed_today_s = 0;
    uint32_t pulses_today = 0;
    uint32_t open_streak_s = 0;
    uint16_t alarms = 0;
    HumidityAdvice humidity = HumidityAdvice::Unknown;
    double dew_point_c = 0.0;
    bool time_valid = false;
    uint64_t uptime_ms = 0;
};

class Controller {
public:
    Controller();

    void begin(uint32_t now_ms, const ControlParams& params, const WeekSchedule& sched,
               const PersistentState& ps, Mode mode, bool start_unlocked = false);

    // --- configuration (validated copies are kept) ---
    void setParams(const ControlParams& p);
    const ControlParams& params() const { return p_; }
    void setSchedule(const WeekSchedule& s);
    const WeekSchedule& schedule() const { return sched_; }

    // --- user requests ---
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    // +/- buttons: in AUTO a temporary override until the next period change,
    // in COMFORT/NIGHT/AWAY changes that mode's setpoint (paramsChanged() becomes true).
    void adjustSetpoint(double delta_c);
    void setSetpoint(double sp_c);
    void startBoost(uint16_t minutes);
    void stopBoost();               // also ends an ARRIVO forced start
    // ARRIVO: back to AUTO (if away/off) and force the valve open for arrival_force_min, ending
    // early once T >= setpoint - arrival_stop_c. Then normal control with the arrival recovery.
    // OFF_MIN still applies (the FSM never reopens early).
    void arrive();
    // Forget what was learned: the duty per band (back to I0) and/or the CYD sensor offset.
    void resetLearning(bool duty, bool offset);
    bool paramsChanged() { bool c = params_dirty_; params_dirty_ = false; return c; }

    // --- sensor feed ---
    void onRoomSample(uint32_t now_ms, double t_c, double rh) { fusion_.onRoom(now_ms, t_c, rh); }
    void onBaseSample(uint32_t now_ms, bool ok, double t_c) { fusion_.onBase(now_ms, ok, t_c); }

    // --- main step: returns true if the relay must be ON (valve OPEN) ---
    bool update(const ControllerInputs& in);

    const Status& status() const { return st_; }
    PersistentState persistent() const;
    bool popEvent(Event& e);

private:
    enum class Cause : uint8_t { None, Schedule, User, Arrival };
    struct Resolved { bool enabled; double sp; Period period; bool preheat; bool time_unknown; };

    Resolved resolve(const WallTime& wall);
    void pushEvent(EventType t, uint8_t a = 0, uint8_t b = 0, float x = 0, float y = 0);
    void swapBand(double sp);
    double learnedI(bool away) const;

    ControlParams p_;
    WeekSchedule sched_;
    Fusion fusion_;
    Pid pid_;
    ValveFsm fsm_;
    HumidityAdvisor hum_;
    Status st_;

    Mode mode_ = Mode::Auto;
    Mode prev_mode_ = Mode::Auto;
    Cause pending_cause_ = Cause::None;
    bool params_dirty_ = false;

    bool override_active_ = false;
    double override_sp_ = 20.0;
    Period override_period_ = Period::Comfort;

    bool boost_ = false;
    uint64_t boost_until_ms_ = 0;
    bool arrival_ = false;
    uint64_t arrival_until_ms_ = 0;
    bool arrival_started_ = false;   // first update after arrive() not yet seen

    bool have_last_sp_ = false;
    double last_sp_ = 0.0;
    bool recovering_ = false;
    Recovery recovery_ = Recovery::Fast;

    bool band_away_ = false;
    double i_band_[2] = {-1.0, -1.0};   // [0] comfort, [1] away

    bool classic_call_ = false;
    double u_ = 0.0;
    uint64_t pid_acc_ms_ = 0;
    bool pid_ran_ = false;

    bool started_ = false;
    uint32_t last_now_ = 0;
    uint64_t uptime_ms_ = 0;
    uint64_t begin_uptime_ = 0;
    uint64_t billed_ms_today_ = 0;
    uint64_t last_open_total_ms_ = 0;
    uint32_t pulses_today_ = 0;
    uint32_t last_opens_total_ = 0;
    bool have_day_ = false;
    uint32_t day_ = 0;
    uint64_t open_streak_ms_ = 0;
    TempSource last_src_ = TempSource::None;
    bool last_limp_ = false;
    bool last_open_ = false;
    uint16_t last_alarms_ = 0;

    static constexpr int kEvq = 32;
    Event evq_[kEvq];
    int ev_head_ = 0, ev_n_ = 0;
};

}  // namespace tmc
