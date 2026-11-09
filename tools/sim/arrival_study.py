#!/usr/bin/env python3
"""
Arrival study: the family arrives at the cold mountain house (kept at the 7 C antifreeze
setpoint) and presses ARRIVO. How long should the valve be forced open?

Every strategy starts from the same state (air at the antifreeze setpoint, walls in steady state
with it) at 17:00 and then holds 20 C. Compared over 48 h:
  * time until the room air reaches 16 / 18 / 19.5 C
  * comfort deficit: integral of max(0, 19.5 - T) over the first 12 h (C*h, lower = warmer sooner)
  * billed valve minutes in the first 6 h and 48 h (what the condominium bills)
  * overshoot above 20 C

Usage:  python3 arrival_study.py [--plot ../../docs/img]
"""
import argparse
import math

import thermal_sim as ts

SP = 20.0
SP_AWAY = 7.0
ARRIVE_H = 17.0
HOURS = 48


def learned_i(t_out_mean):
    """Steady-state duty the PI has learned for this weather (the comfort-band I in the firmware)."""
    ctrl = ts.make("termomade")
    ts.simulate(ctrl, days=4, T_out_mean=t_out_mean)
    return ctrl.pid.I


def strategy(name, i0):
    """Return a controller for one arrival strategy. i0 = learned comfort duty."""
    def tm(eff):
        c = ts.make("termomade", eff=eff, i0=i0)
        c.last_sp = SP_AWAY       # arrival = setpoint rise 7 -> 20 C
        return c
    if name == "classic":
        return ts.Hysteresis()
    if name == "fast":
        return tm(False)
    if name == "efficient":
        return tm(True)
    kind, rest = name.split(":")
    if kind == "force_eff":
        return ts.ForcedStart(tm(True), int(rest) * 60, sp_before=SP_AWAY)
    if kind == "force_fast":
        return ts.ForcedStart(tm(False), int(rest) * 60, sp_before=SP_AWAY)
    if kind == "until_eff":        # force until sp - stop_c (no time cap)
        return ts.ForcedStart(tm(True), 48 * 3600, stop_c=float(rest), sp_before=SP_AWAY)
    raise ValueError(name)


def run(name, t_out_mean, t0_air=SP_AWAY):
    i0 = learned_i(t_out_mean)
    ctrl = strategy(name, i0)
    # walls in steady state with the antifreeze air temperature
    p = ts.PLANT
    tm = (p["UA_air_mass"] * t0_air + p["UA_mass_out"] * t_out_mean) / (p["UA_air_mass"] + p["UA_mass_out"])
    out, rec = ts.simulate(ctrl, days=HOURS / 24, sp=SP, T_out_mean=t_out_mean, T0_air=t0_air, T0_mass=tm,
                           eval_from_h=0, record=True, t0_h=ARRIVE_H)

    def reach(x):
        return next((r[0] for r in rec if r[1] >= x), float("nan"))

    def billed(h):
        return sum(r[5] for r in rec if r[0] < h)   # one record per minute

    def t_at(h):
        return min(rec, key=lambda r: abs(r[0] - h))[1]

    t195 = reach(SP - 0.5)
    after = [r[1] for r in rec if not math.isnan(t195) and r[0] >= t195]
    return dict(
        strategy=name,
        T1h=t_at(1), T3h=t_at(3), T6h=t_at(6),
        t16_h=reach(16.0), t18_h=reach(18.0), t19_5_h=t195,
        deficit_Ch=sum(max(0.0, SP - 0.5 - r[1]) / 60 for r in rec if r[0] < 12),
        billed6=billed(6), billed48=billed(48),
        overshoot=(max(after) - SP) if after else float("nan"),
        heat_kWh=out["heat_kWh_day"] * HOURS / 24,
    ), rec


class Planned:
    """Away (antifreeze) until switch_s, then the comfort setpoint: a programmed return that starts
    heating before the family arrives. Optional forced start at the switch."""

    def __init__(self, inner, switch_s, force_s=0):
        self.inner, self.name = inner, inner.name
        self.switch_s, self.force_s = switch_s, force_s
        self.t = 0.0
        self.forced = None

    @property
    def u(self):
        return getattr(self.inner, "u", float("nan"))

    def tick(self, sp, t_meas, dt):
        self.t += dt
        if self.t < self.switch_s:
            return self.inner.tick(SP_AWAY, t_meas, dt)
        if self.forced is None:
            self.forced = ts.ForcedStart(self.inner, self.force_s, sp_before=SP_AWAY)
        return self.forced.tick(sp, t_meas, dt)


def run_planned(lead_h, t_out_mean, eff=True, force_min=0):
    """Simulation starts 24 h before arrival with the house at the antifreeze setpoint."""
    i0 = learned_i(t_out_mean)
    inner = ts.make("termomade", eff=eff, i0=i0)
    pre = 24.0
    ctrl = Planned(inner, (pre - lead_h) * 3600, force_min * 60)
    p = ts.PLANT
    tm = (p["UA_air_mass"] * SP_AWAY + p["UA_mass_out"] * t_out_mean) / (p["UA_air_mass"] + p["UA_mass_out"])
    _, rec = ts.simulate(ctrl, days=(pre + 48) / 24, sp=SP, T_out_mean=t_out_mean, T0_air=SP_AWAY, T0_mass=tm,
                         eval_from_h=0, record=True, t0_h=(ARRIVE_H - pre) % 24)

    def t_at(h):
        return min(rec, key=lambda r: abs(r[0] - (pre + h)))[1]
    label = ("cold arrival" if lead_h == 0 else f"start {lead_h:g} h before") + \
        (", efficient" if eff else ", fast") + (f", force {force_min}'" if force_min else "")
    return dict(
        plan=label, T_arrival=t_at(0), T3h=t_at(3), T6h=t_at(6),
        deficit_Ch=sum(max(0.0, SP - 0.5 - r[1]) / 60 for r in rec if pre <= r[0] < pre + 12),
        billed_before=sum(r[5] for r in rec if r[0] < pre),
        billed_total=sum(r[5] for r in rec),
    )


STRATEGIES = ["classic", "fast", "efficient",
              "force_eff:15", "force_eff:30", "force_eff:60", "force_eff:90", "force_eff:120",
              "force_eff:180", "force_eff:240", "until_eff:4", "until_eff:2"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plot", help="directory for the PNG plot")
    a = ap.parse_args()
    cols = ["strategy", "T1h", "T3h", "T6h", "t16_h", "t18_h", "deficit_Ch", "billed6", "billed48", "overshoot"]
    recs = {}
    for t_out in (-8.0, -2.0, 4.0):
        print(f"\n=== ARRIVAL at 17:00, house at {SP_AWAY:.0f} C, T_out mean {t_out:+.0f} C (+-5), "
              f"hold {SP:.0f} C, 48 h (learned duty I = {learned_i(t_out):.3f}) ===")
        rows = []
        for s in STRATEGIES:
            r, rec = run(s, t_out)
            rows.append(r)
            recs[(t_out, s)] = rec
        base = next(r for r in rows if r["strategy"] == "efficient")
        for r in rows:
            r["extra48"] = r["billed48"] - base["billed48"]
        ts.table(rows, cols + ["extra48"])
    for t_out in (-8.0, -2.0):
        print(f"\n=== PLANNED RETURN vs ARRIVING COLD, T_out mean {t_out:+.0f} C: window = 24 h before "
              f"to 48 h after arrival (17:00) ===")
        rows = [run_planned(0, t_out, True), run_planned(0, t_out, True, 60), run_planned(0, t_out, False)]
        rows += [run_planned(h, t_out, True) for h in (6, 12, 18, 24)]
        rows += [run_planned(12, t_out, False)]
        base = rows[0]["billed_total"]
        for r in rows:
            r["extra"] = r["billed_total"] - base
        ts.table(rows, ["plan", "T_arrival", "T3h", "T6h", "deficit_Ch", "billed_before", "billed_total", "extra"])
    if a.plot:
        import os
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        os.makedirs(a.plot, exist_ok=True)
        fig, axs = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
        show = [("classic", "#7f8c8d"), ("fast", "#e67e22"), ("efficient", "#2e86de"),
                ("force_eff:30", "#16a085"), ("force_eff:60", "#c0392b"), ("force_eff:120", "#8e44ad")]
        for name, c in show:
            rec = [r for r in recs[(-2.0, name)] if r[0] <= 12]
            h = [r[0] for r in rec]
            axs[0].plot(h, [r[1] for r in rec], color=c, lw=1.4, label=name)
            cum, tot = [], 0
            for r in rec:
                tot += r[5]
                cum.append(tot)
            axs[1].plot(h, cum, color=c, lw=1.4, label=name)
        axs[0].axhline(SP, color="#888", ls="--", lw=0.8)
        axs[0].set_ylabel("room air C")
        axs[0].set_title("Arrival at 17:00 in a house at 7 C, T_out -2 C: room temperature", fontsize=10, loc="left")
        axs[0].legend(ncol=3, fontsize=8)
        axs[1].set_ylabel("billed valve minutes (cumulative)")
        axs[1].set_xlabel("hours since arrival")
        axs[1].set_title("Billed time", fontsize=10, loc="left")
        fig.tight_layout()
        path = os.path.join(a.plot, "sim_arrival.png")
        fig.savefig(path, dpi=110)
        print(f"\nplot written to {path}")


if __name__ == "__main__":
    main()
