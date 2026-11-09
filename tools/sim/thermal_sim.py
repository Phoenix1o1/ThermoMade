#!/usr/bin/env python3
"""
TermoMade thermal simulator -- a DESIGN TOOL, not firmware.

Purpose
  * sanity-check the "pulse the zone valve" idea before writing firmware
  * compare controllers on billed valve-open time, comfort and valve cycles
  * pick sane default tuning (Kp, Ti, Td, ON_MIN, OFF_MIN) for the firmware

Model (all parameters are GUESSES for a ~70-80 m2 mountain apartment, edit PLANT)
  building : air node (air+furniture+surfaces) <-> mass node (walls) <-> outdoor
  radiators: the apartment circuit as N well-mixed segments in series
             (approximates plug flow: the hot front travels through the circuit
             and the circuit is "full" after volume/flow = fill time)
  valve    : on/off, actuator delay, billed while physically open
  sensor   : remote node, first-order lag (enclosure), noise, 30 s reports

Controllers
  hyst     : classic room thermostat, +-0.2 C hysteresis, no minimum times
  tpi      : PI + fixed-period time-proportioning (20 min), min ON/OFF enforced
  termomade: PI(D) + pulse modulator (fixed short ON pulses, modulated OFF;
             ON grows only when OFF has hit its minimum)

Usage
  python3 thermal_sim.py               # table for all scenarios
  python3 thermal_sim.py --plot out/   # also writes PNG plots (needs matplotlib)
"""
import argparse
import math
import random

# --------------------------------------------------------------------------- plant
PLANT = dict(
    T_supply=75.0,        # C, hot water from the central plant ("very hot")
    flow_lpm=10.0,        # L/min through the apartment when the valve is open
    rad_water_l=30.0,     # L, water in radiators + apartment piping (30 L / 10 L/min = 3 min fill)
    rad_metal_kJK=70.0,   # kJ/K, radiator metal (aluminium ~ 80 elements)
    rad_P50_W=8000.0,     # W, total radiator output at dT = 50 K (EN 442 rating)
    rad_n=1.3,            # radiator exponent
    n_seg=8,              # segments for the plug-flow approximation
    C_air_kJK=2000.0,     # kJ/K, air + furniture + internal surfaces (fast node)
    C_mass_kJK=30000.0,   # kJ/K, building mass (slow node)
    UA_air_mass=2500.0,   # W/K, ~ 8 W/m2K x ~300 m2 of internal surfaces
    UA_mass_out=110.0,    # W/K, envelope conduction
    UA_air_out=60.0,      # W/K, ventilation / infiltration / windows
    gains_W=200.0,        # W, people + appliances
    valve_delay_s=30.0,   # s, motorised zone valve stroke time
)
CP_W = 4186.0             # J/kgK

# --------------------------------------------------------------------------- controllers


class Hysteresis:
    name = "hyst"

    def __init__(self, diff=0.4):
        self.diff = diff
        self.on = False

    def tick(self, sp, t_meas, dt):
        if t_meas < sp - self.diff / 2:
            self.on = True
        elif t_meas > sp + self.diff / 2:
            self.on = False
        return self.on


class PID:
    """PI(D) producing a duty demand u in [0, 1].
    * derivative on the filtered measurement (Td = 0 by default)
    * I = learned steady-state duty, clamped to [0, 1]
    * integrates only inside a band |e| <= band (no windup on big errors:
      cold start, night setback, sun) and not when saturated high
    * 'coast': after a setpoint DECREASE, I is frozen until the room has
      cooled down to the new setpoint (e >= 0), so a night setback does not
      drain the learned load"""

    def __init__(self, kp=0.6, ti_s=120 * 60, td_s=0.0, tf_s=120, i0=0.15, band=1.0):
        self.kp, self.ti, self.td, self.tf, self.band = kp, ti_s, td_s, tf_s, band
        self.I = i0
        self.mf = None      # filtered measurement
        self.dm = 0.0       # filtered derivative of measurement (C/s)
        self.last_sp = None
        self.coast = False
        self.terms = (0, 0, 0)

    def update(self, sp, meas, dt, u_max=1.0):
        if self.mf is None:
            self.mf = meas
        if self.last_sp is not None and sp < self.last_sp - 0.05:
            self.coast = True
        self.last_sp = sp
        a = dt / (self.tf + dt)
        prev = self.mf
        self.mf += a * (meas - self.mf)
        raw_d = (self.mf - prev) / dt
        ad = dt / (max(self.td / 4, dt) + dt)
        self.dm += ad * (raw_d - self.dm)
        e = sp - self.mf
        if e >= 0:
            self.coast = False
        P = self.kp * e
        D = -self.kp * self.td * self.dm
        u_unsat = P + self.I + D
        if abs(e) <= self.band and not self.coast and not (u_unsat >= u_max and e > 0):
            self.I += self.kp * dt / self.ti * e
        self.I = min(max(self.I, 0.0), 1.0)
        u = min(max(P + self.I + D, 0.0), u_max)
        self.terms = (P, self.I, D)
        return u


class TPI:
    name = "tpi"

    def __init__(self, pid, period_s=20 * 60, on_min=180, off_min=300, u_min=0.03):
        self.pid, self.period, self.on_min, self.off_min, self.u_min = pid, period_s, on_min, off_min, u_min
        self.on = False
        self.t_state = 1e9
        self.u = 0.0

    def tick(self, sp, t_meas, dt):
        self.u = u = self.pid.update(sp, t_meas, dt)
        self.t_state += dt
        on_t = 0.0 if u < self.u_min else max(self.on_min, u * self.period)
        off_t = max(self.off_min, self.period - on_t)
        if u >= 0.97:
            on_t = math.inf
        if self.on and self.t_state >= max(self.on_min, on_t):
            self.on, self.t_state = False, 0.0
        elif (not self.on) and on_t > 0 and self.t_state >= off_t:
            self.on, self.t_state = True, 0.0
        return self.on


class PulseModulator:
    """TermoMade modulator.
    u <= uc  : ON = ON_MIN (one 'fill'), OFF = ON_MIN*(1-u)/u  (pulse-frequency)
    u >  uc  : OFF = OFF_MIN, ON = OFF_MIN*u/(1-u)              (pulse-width)
    u >= u_cont: continuous ON (boost)    u < u_min: no pulses
    with uc = ON_MIN / (ON_MIN + OFF_MIN). Targets are re-evaluated every tick,
    minimum times are never violated."""

    def __init__(self, on_min=180, off_min=300, u_min=0.03, u_cont=0.90):
        self.on_min, self.off_min, self.u_min, self.u_cont = on_min, off_min, u_min, u_cont
        self.uc = on_min / (on_min + off_min)

    def targets(self, u):
        if u < self.u_min:
            return self.on_min, math.inf
        if u >= self.u_cont:
            return math.inf, self.off_min
        if u <= self.uc:
            return self.on_min, self.on_min * (1 - u) / u
        return self.off_min * u / (1 - u), self.off_min


class TermoMade:
    """PID + PulseModulator + valve FSM (min times enforced).
    efficient_recovery: after a setpoint INCREASE (e.g. Night -> Comfort) the
    demand is capped at uc (pulse-frequency only, never long/continuous ON)
    until the room is within 0.3 C of the setpoint. Continuous ON returns hot
    water to the plant: ~0.13 kWh per billed minute instead of ~0.47."""
    name = "termomade"

    def __init__(self, pid, mod, efficient_recovery=False):
        self.pid, self.mod, self.eff = pid, mod, efficient_recovery
        self.on = False
        self.t_state = 1e9
        self.u = 0.0
        self.last_sp = None
        self.recovering = False

    def tick(self, sp, t_meas, dt):
        if self.last_sp is not None and sp > self.last_sp + 0.05:
            self.recovering = True
        self.last_sp = sp
        if self.recovering and sp - t_meas <= 0.3:
            self.recovering = False
        u_max = self.mod.uc if (self.eff and self.recovering) else 1.0
        self.u = u = self.pid.update(sp, t_meas, dt, u_max)
        self.t_state += dt
        on_t, off_t = self.mod.targets(u)
        if self.on and self.t_state >= max(self.mod.on_min, on_t):
            self.on, self.t_state = False, 0.0
        elif (not self.on) and self.t_state >= max(self.mod.off_min, off_t):
            self.on, self.t_state = True, 0.0
        return self.on


class Schedule:
    """Wraps a controller and replaces the setpoint with a daily program."""

    def __init__(self, inner, day_start_h=6.5, day_end_h=22.5, sp_day=20.0, sp_night=17.0, preheat_h=0.0):
        self.inner, self.name = inner, inner.name
        self.a, self.b, self.d, self.n, self.pre = day_start_h - preheat_h, day_end_h, sp_day, sp_night, preheat_h
        self.t = 0.0

    @property
    def u(self):
        return getattr(self.inner, "u", float("nan"))

    def tick(self, sp, t_meas, dt):
        h = (self.t / 3600) % 24
        self.t += dt
        return self.inner.tick(self.d if self.a <= h < self.b else self.n, t_meas, dt)


# --------------------------------------------------------------------------- simulation


def simulate(ctrl, days=4.0, sp=20.0, T_out_mean=-2.0, T_out_amp=5.0, T0_air=None, T0_mass=None,
             eval_from_h=24.0, dt=5.0, ts=30.0, seed=1, record=False, p=PLANT):
    rnd = random.Random(seed)
    N = p["n_seg"]
    C_seg = (p["rad_water_l"] * CP_W + p["rad_metal_kJK"] * 1000) / N
    mcp = p["flow_lpm"] / 60.0 * CP_W
    P50 = p["rad_P50_W"] / N
    C_air, C_mass = p["C_air_kJK"] * 1000, p["C_mass_kJK"] * 1000

    def t_out(t):
        return T_out_mean - T_out_amp * math.cos(2 * math.pi * (t / 3600 - 3) / 24)  # min at 03:00

    if T0_air is None:
        T0_air = sp
    if T0_mass is None:  # steady state for that air temperature
        To = T_out_mean
        T0_mass = (p["UA_air_mass"] * T0_air + p["UA_mass_out"] * To) / (p["UA_air_mass"] + p["UA_mass_out"])
    Ta, Tm = T0_air, T0_mass
    seg = [Ta] * N
    Ts_sensor = Ta
    cmd = False
    cmd_hist = []          # (time, cmd) for actuator delay
    valve_open = False
    meas = Ta
    t, next_tick, next_report = 0.0, 0.0, 0.0

    m = dict(billed_s=0.0, comp_units=0.0, heat_J=0.0, cycles=0, n=0, se=0.0, err_sum=0.0,
             tmax=-99, tmin=99, in_band=0, ret_sum=0.0, ret_n=0)
    rec = []
    steps = int(days * 86400 / dt)
    for _ in range(steps):
        # --- sensor (lag + noise, 30 s reports)
        Ts_sensor += dt / 120.0 * (Ta - Ts_sensor)
        if t >= next_report:
            meas = round(Ts_sensor + rnd.gauss(0, 0.02), 2)
            next_report += 30.0
        # --- controller
        if t >= next_tick:
            new_cmd = ctrl.tick(sp, meas, ts)
            if new_cmd and not cmd:
                m_cycle = True
            else:
                m_cycle = False
            cmd = new_cmd
            cmd_hist.append((t, cmd))
            next_tick += ts
        else:
            m_cycle = False
        # --- valve with stroke delay
        while cmd_hist and cmd_hist[0][0] + p["valve_delay_s"] <= t:
            valve_open = cmd_hist.pop(0)[1]
        # --- radiator circuit
        P_emit = 0.0
        T_in = p["T_supply"]
        for i in range(N):
            q_flow = mcp * (T_in - seg[i]) if valve_open else 0.0
            dT = max(seg[i] - Ta, 0.0)
            q_emit = P50 * (dT / 50.0) ** p["rad_n"] if dT > 0 else 0.0
            T_in = seg[i]
            seg[i] += dt * (q_flow - q_emit) / C_seg
            P_emit += q_emit
        T_ret = seg[-1]
        # --- building
        To = t_out(t)
        q_am = p["UA_air_mass"] * (Ta - Tm)
        Ta += dt * (P_emit + p["gains_W"] - q_am - p["UA_air_out"] * (Ta - To)) / C_air
        Tm += dt * (q_am - p["UA_mass_out"] * (Tm - To)) / C_mass
        # --- metrics
        if t >= eval_from_h * 3600:
            if valve_open:
                m["billed_s"] += dt
                Tmean = (p["T_supply"] + T_ret) / 2
                m["comp_units"] += dt * max(Tmean - Ta, 0) / 50.0   # UNI/TR 11388-like
                m["ret_sum"] += T_ret
                m["ret_n"] += 1
            if m_cycle:
                m["cycles"] += 1
            m["heat_J"] += P_emit * dt
            e = Ta - sp
            m["n"] += 1
            m["se"] += e * e
            m["err_sum"] += e
            m["tmax"] = max(m["tmax"], Ta)
            m["tmin"] = min(m["tmin"], Ta)
            m["in_band"] += abs(e) <= 0.3
        if record and int(t) % 60 == 0:
            rec.append((t / 3600, Ta, meas, To, T_ret, int(valve_open), getattr(ctrl, "u", float("nan"))))
        t += dt

    ev_days = days - eval_from_h / 24
    out = dict(
        billed_min_day=m["billed_s"] / 60 / ev_days,
        comp_h_day=m["comp_units"] / 3600 / ev_days,
        heat_kWh_day=m["heat_J"] / 3.6e6 / ev_days,
        cycles_day=m["cycles"] / ev_days,
        mean_err=m["err_sum"] / m["n"],
        rms=math.sqrt(m["se"] / m["n"]),
        tmax=m["tmax"], tmin=m["tmin"],
        in_band=100.0 * m["in_band"] / m["n"],
        ret_avg=m["ret_sum"] / m["ret_n"] if m["ret_n"] else float("nan"),
    )
    out["kWh_per_billed_min"] = out["heat_kWh_day"] / out["billed_min_day"] if out["billed_min_day"] else float("nan")
    return out, rec


def make(name, **kw):
    if name == "hyst":
        return Hysteresis()
    pid = PID(kp=kw.get("kp", 0.6), ti_s=kw.get("ti", 120) * 60, td_s=kw.get("td", 0) * 60, i0=kw.get("i0", 0.15))
    if name == "tpi":
        return TPI(pid)
    return TermoMade(pid, PulseModulator(on_min=kw.get("on_min", 180), off_min=kw.get("off_min", 300)),
                     efficient_recovery=kw.get("eff", False))


def cold_start(ctrl, sp=20.0, T0=8.0, T_out_mean=-2.0, hours=24, p=PLANT):
    """Arrival in a cold house: everything at T0, measure time-to-comfort and overshoot."""
    out, rec = simulate(ctrl, days=hours / 24, sp=sp, T_out_mean=T_out_mean, T0_air=T0, T0_mass=T0,
                        eval_from_h=0, record=True, p=p)
    t_reach = next((r[0] for r in rec if r[1] >= sp - 0.5), float("nan"))
    after = [r[1] for r in rec if not math.isnan(t_reach) and r[0] >= t_reach]
    out["t_reach_h"] = t_reach
    out["overshoot"] = (max(after) - sp) if after else float("nan")
    return out, rec


def table(rows, cols):
    w = [max(len(c), 9) for c in cols]
    print("  ".join(c.rjust(x) for c, x in zip(cols, w)))
    for r in rows:
        print("  ".join((f"{r[c]:.2f}" if isinstance(r[c], float) else str(r[c])).rjust(x) for c, x in zip(cols, w)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plot", help="directory for PNG plots")
    a = ap.parse_args()

    cols = ["ctrl", "billed_min_day", "kWh_per_billed_min", "comp_h_day", "heat_kWh_day", "cycles_day",
            "rms", "tmax", "tmin", "in_band", "ret_avg"]
    for label, kw in [("WINTER  T_out -2 +-5 C", dict(T_out_mean=-2.0)),
                      ("COLD    T_out -10 +-5 C", dict(T_out_mean=-10.0)),
                      ("MILD    T_out +8 +-5 C", dict(T_out_mean=8.0))]:
        print(f"\n=== {label}, setpoint 20 C, days 2-4 evaluated ===")
        rows = []
        for name in ("hyst", "tpi", "termomade"):
            r, _ = simulate(make(name), **kw)
            r["ctrl"] = name
            rows.append(r)
        table(rows, cols)

    print("\n=== COLD ARRIVAL: house at 8 C, setpoint 20 C, T_out -2 C, first 24 h ===")
    rows, recs = [], {}
    for name in ("hyst", "tpi", "termomade"):
        r, rec = cold_start(make(name))
        r["ctrl"] = name
        rows.append(r)
        recs[name] = rec
    table(rows, ["ctrl", "t_reach_h", "overshoot", "billed_min_day", "heat_kWh_day", "cycles_day"])

    print("\n=== NIGHT SETBACK (Comfort 20 C 06:30-22:30), T_out -2 C, days 3-5 ===")
    print("    day-time comfort is the same in all rows; T@06:30 / T@07:30 = room temperature at wake-up")
    rows = []
    for label, ctrl, night, pre in [
            ("hyst, no setback", "hyst", 20.0, 0.0),
            ("hyst, night 17", "hyst", 17.0, 0.0),
            ("pulse, no setback", "termomade", 20.0, 0.0),
            ("pulse, night 17, fast", "termomade", 17.0, 0.0),
            ("pulse, night 17, efficient", "termomade_eff", 17.0, 0.0),
            ("pulse, night 17, eff, +2h", "termomade_eff", 17.0, 2.0),
            ("pulse, night 18.5, fast", "termomade", 18.5, 0.0),
            ("pulse, night 18.5, eff, +1h", "termomade_eff", 18.5, 1.0)]:
        inner = make("termomade", eff=True) if ctrl == "termomade_eff" else make(ctrl)
        r, rec = simulate(Schedule(inner, sp_night=night, preheat_h=pre), days=5, eval_from_h=48, record=True)

        def t_at(h):
            return min(rec, key=lambda x: abs(x[0] - h))[1]
        r["ctrl"] = label
        r["T@06:30"] = sum(t_at(d * 24 + 6.5) for d in (2, 3, 4)) / 3
        r["T@07:30"] = sum(t_at(d * 24 + 7.5) for d in (2, 3, 4)) / 3
        rows.append(r)
    table(rows, ["ctrl", "billed_min_day", "heat_kWh_day", "kWh_per_billed_min", "T@06:30", "T@07:30", "tmax"])

    if a.plot:
        import os
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        os.makedirs(a.plot, exist_ok=True)
        fig, axs = plt.subplots(3, 1, figsize=(11, 8), sharex=True)
        for ax, name in zip(axs, ("hyst", "tpi", "termomade")):
            _, rec = simulate(make(name), days=2.0, record=True)
            rec = [r for r in rec if 24 <= r[0] <= 30]
            h = [r[0] - 24 for r in rec]
            ax.plot(h, [r[1] for r in rec], color="#c0392b", lw=1.4, label="room air")
            ax.axhline(20, color="#888", ls="--", lw=0.8)
            ax.set_ylim(19.0, 21.0)
            ax.set_ylabel("C")
            ax2 = ax.twinx()
            ax2.fill_between(h, 0, [r[5] for r in rec], step="post", color="#2e86de", alpha=0.25, label="valve open")
            ax2.set_ylim(0, 4)
            ax2.set_yticks([])
            ax.set_title(f"{name}: 6 h of steady winter operation", fontsize=10, loc="left")
        axs[-1].set_xlabel("hours")
        fig.tight_layout()
        fig.savefig(os.path.join(a.plot, "sim_steady.png"), dpi=110)

        fig, ax = plt.subplots(figsize=(11, 4))
        for name, c in (("hyst", "#7f8c8d"), ("tpi", "#2e86de"), ("termomade", "#c0392b")):
            rec = recs[name]
            ax.plot([r[0] for r in rec], [r[1] for r in rec], color=c, lw=1.4, label=name)
        ax.axhline(20, color="#888", ls="--", lw=0.8)
        ax.set_xlabel("hours since arrival")
        ax.set_ylabel("room air C")
        ax.set_title("Cold arrival: 8 C -> 20 C", fontsize=10, loc="left")
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(a.plot, "sim_cold_start.png"), dpi=110)

        mod = PulseModulator()
        us = [i / 1000 for i in range(30, 900)]
        on = [mod.targets(u)[0] / 60 for u in us]
        off = [mod.targets(u)[1] / 60 for u in us]
        fig, ax = plt.subplots(figsize=(9, 4))
        ax.plot(us, on, color="#c0392b", lw=2, label="ON time (valve open)")
        ax.plot(us, off, color="#2e86de", lw=2, label="OFF time (valve closed)")
        ax.axvline(mod.uc, color="#888", ls=":", lw=1)
        ax.text(mod.uc + 0.01, 40, "uc = 0.375", fontsize=8, color="#555")
        ax.text(0.12, 52, "pulse-FREQUENCY\n(ON fixed = fill time)", fontsize=9, color="#2e86de")
        ax.text(0.50, 52, "pulse-WIDTH\n(OFF fixed = OFF_MIN)", fontsize=9, color="#c0392b")
        ax.set_ylim(0, 60)
        ax.set_xlim(0, 0.92)
        ax.set_xlabel("controller demand u (duty fraction)")
        ax.set_ylabel("minutes")
        ax.set_title("Pulse modulator: ON_MIN 3 min, OFF_MIN 5 min (u>=0.90 -> continuous)", fontsize=10, loc="left")
        ax.legend(loc="center left", bbox_to_anchor=(0.40, 0.55))
        fig.tight_layout()
        fig.savefig(os.path.join(a.plot, "modulator.png"), dpi=110)
        print(f"\nplots written to {a.plot}")


if __name__ == "__main__":
    main()
