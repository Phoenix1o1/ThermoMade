#!/usr/bin/env python3
"""
make_fake_logs.py -- synthetic TermoMade SD-card logs, for testing tm_logs.py before real
logs exist.

The plant is tools/sim/thermal_sim.py (simulate()). The controller is a small stand-in for the
firmware's tmc::Controller built from the simulator's PID and PulseModulator, plus the things
the firmware does and the simulator does not: weekly schedule, AWAY, ARRIVO, the CLASSIC
algorithm on alternate days, LIMP, daily counters, reboots. The files are written in exactly
the format of firmware/src/base/logger.cpp:

    <out>/YYYY-MM-DD.csv   one line per minute
    <out>/notime.csv       minutes logged before the clock was set
    <out>/events.csv       events and free text lines
    <out>/outdoor_truth.csv  NOT a firmware file: daily mean outdoor temperature of the
                             simulation (date,t_out_mean), usable with `tm_logs.py --outdoor`

Story (17 days, simulated weather -2 +- 5 C every day):
    Fri 13 Nov 17:00  power on in a cold house, clock unknown for 25 min (-> notime.csv),
                      then the time is set and ARRIVO is pressed
    Sat-Sun           AUTO, pulse algorithm, Comfort 20.0 / Night 18.5
    Sun 15 Nov 17:00  leave: AWAY (7 C)        -> several days of pure cooling
    Fri 20 Nov 18:00  arrive: ARRIVO
    Sat 21 Nov on     algo = alternate days (CLASSIC on odd day numbers, like the firmware)
    Mon 23 Nov 13:12  power cut, 4 min without lines, counters restart at 0, one mangled line
    Tue 24 Nov 02:00  room node lost for 90 min (CYD sensor only, t_room/rh empty)
    Wed 25 Nov 15:00  all sensors lost for 40 min -> LIMP (t_ctrl empty)
    Thu 26 Nov 10:00  SD card out for 40 min (gap in the file)

Usage:  python3 make_fake_logs.py [-o fake_logs] [--days 17] [--seed 1] [--clean]
"""
import argparse
import calendar
import datetime as dt
import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sim"))
import thermal_sim as ts  # noqa: E402

HEADER = ("unix,local,uptime_s,t_ctrl,src,t_room,rh,t_base,t_base_corr,offset,sp,mode,period,boost,u,p,i,valve,"
          "reason,billed_today_s,pulses_today,alarms,limp,humidity,dew_c,rssi,node_age_s,arrival,light_pct\n")
EVENT_HEADER = "unix,local,uptime_s,event,a,b,x,y\n"

START = dt.datetime(2026, 11, 13, 17, 0, 0)          # local time (CET) at simulation t = 0
UTC_OFFSET_S = 3600                                   # CET; the story stays inside winter time

# tmc enums
MODE_AUTO, MODE_AWAY = 0, 3
R_BOOT, R_PULSE, R_CONT, R_CLASSIC, R_LIMP, R_TARGET, R_NODEMAND, R_ARRIVAL = 0, 1, 2, 3, 5, 6, 7, 12
AL_ROOM, AL_BASE, AL_NOSENSOR = 1, 2, 4

ON_MIN, OFF_MIN = 180.0, 300.0
SP_COMFORT, SP_NIGHT, SP_AWAY = 20.0, 18.5, 7.0
CLASSIC_HYST = 0.2
W_ROOM = 0.7


def H(day, hh, mm=0):
    """Seconds since START for 'day of November', hh:mm local."""
    return (dt.datetime(2026, 11, day, hh, mm) - START).total_seconds()


def dew_point(t_c, rh):
    a, b = 17.62, 243.12
    g = math.log(max(rh, 1.0) / 100.0) + a * t_c / (b + t_c)
    return b * g / (a - g)


def rh_from_dew(t_c, dew_c):
    a, b = 17.62, 243.12
    return 100.0 * math.exp(a * dew_c / (b + dew_c) - a * t_c / (b + t_c))


class FakeBase:
    """Stand-in for the base station: controller + logger. Used as the simulator's `ctrl`."""
    name = "fakebase"

    def __init__(self, out_dir, seed=1, clean=False, days=17.0):
        self.out, self.clean = out_dir, clean
        self.rnd = random.Random(seed)
        self.pid = ts.PID()
        self.mod = ts.PulseModulator(on_min=ON_MIN, off_min=OFF_MIN)
        self.t = 0.0                    # s since START (controller tick time)
        self.end = days * 86400.0
        self.boot_t = 0.0
        self.on = False
        self.t_state = 0.0              # boot = lockout: OFF_MIN counts from power-up
        self.u = 0.0
        self.last_sp = None
        self.recovering = False
        self.classic_call = False
        self.arrival_left = 0.0
        self.mode = MODE_AWAY
        self.reason = R_BOOT
        self.billed_s = 0.0
        self.pulses = 0
        self.day = None
        self.offset = 0.80              # learned CYD offset, converges to ~1.08
        self.limp = False
        self.alarms = 0
        self.src = 1
        self.node_last = None
        self.hum = 0
        self.files = {}
        self.pending_partial = None
        self.power_off_until = -1.0
        # story
        self.time_set_at = 25 * 60.0
        self.todo = [
            (25 * 60.0, "time"), (30 * 60.0, "arrive"),
            (H(15, 17), "away"), (H(20, 18), "arrive"), (H(21, 0), "alt"),
            (H(23, 13, 12), "powercut"),
        ]
        self.room_lost = [(H(24, 2), H(24, 3, 30)), (H(25, 15), H(25, 15, 40))]
        self.base_lost = [(H(25, 15), H(25, 15, 40))]
        self.sd_out = [(H(26, 10), H(26, 10, 40))]
        self.algo_alt = False
        os.makedirs(out_dir, exist_ok=True)
        for f in os.listdir(out_dir):
            if f.endswith(".csv"):
                os.remove(os.path.join(out_dir, f))

    # ------------------------------------------------------------------ helpers
    def now(self):
        return START + dt.timedelta(seconds=self.t)

    def time_valid(self):
        return self.t >= self.time_set_at

    def unix(self):
        return calendar.timegm(self.now().timetuple()) - UTC_OFFSET_S if self.time_valid() else 0

    def local(self):
        return self.now().strftime("%Y-%m-%d %H:%M:%S") if self.time_valid() else ""

    def uptime(self):
        return int(self.t - self.boot_t)

    def sd_ok(self):
        return not any(a <= self.t < b for a, b in self.sd_out)

    def append(self, name, header, line):
        if not self.sd_ok():
            return
        path = os.path.join(self.out, name)
        new = name not in self.files and not os.path.exists(path)
        with open(path, "a", newline="") as f:
            if new:
                f.write(header)
            f.write(line)
        self.files[name] = True

    def event(self, name, a=0, b=0, x=0.0, y=0.0):
        self.append("events.csv", EVENT_HEADER,
                    "%d,%s,%d,%s,%d,%d,%.3f,%.3f\n" % (self.unix(), self.local(), self.uptime(), name, a, b, x, y))

    def text(self, msg):
        msg = msg.replace(",", ";")
        self.append("events.csv", EVENT_HEADER, "%d,%s,%d,text,0,0,0,0,%s\n" % (self.unix(), self.local(),
                                                                                self.uptime(), msg))

    # ------------------------------------------------------------------ story
    def story(self):
        while self.todo and self.todo[0][0] <= self.t:
            _, what = self.todo.pop(0)
            if what == "time":
                self.text("time set from phone")
            elif what == "arrive":
                prev = self.mode
                self.mode = MODE_AUTO
                self.event("mode", MODE_AUTO, prev)
                self.arrival_left = 3600.0
                self.event("arrival", 1, 0, self.t_ctrl if self.t_ctrl is not None else 0.0)
                if self.last_sp is None:
                    self.last_sp = SP_AWAY
            elif what == "away":
                self.event("mode", MODE_AWAY, self.mode)
                self.mode = MODE_AWAY
            elif what == "alt":
                self.algo_alt = True
                self.text("param algo=2")
            elif what == "powercut":
                # the line being written is cut short; the next append lands on the same line
                if not self.clean:
                    self.pending_partial = "%d,%s,%d,20.0" % (self.unix(), self.local()[:15], self.uptime())
                self.power_off_until = self.t + 240.0
                self.on = False
                self.reason = R_BOOT

    def reboot(self):
        self.boot_t = self.t
        self.t_state = 0.0              # lockout
        self.on = False
        self.reason = R_BOOT
        self.billed_s, self.pulses = 0.0, 0
        self.pid.mf = None
        self.recovering = False
        self.classic_call = False
        self.arrival_left = 0.0
        self.event("boot")
        self.text("boot reason: POWERON")

    # ------------------------------------------------------------------ controller tick (30 s)
    def tick(self, sp_unused, meas, dt_s):
        self.t_ctrl = getattr(self, "t_ctrl", None)
        if self.t == 0.0:
            self.event("boot")
            self.text("boot reason: POWERON")
        self.story()
        if self.t < self.power_off_until:
            self.t += dt_s
            return False
        if self.power_off_until > 0:
            self.power_off_until = -1.0
            self.reboot()

        # ---- sensors and fusion
        room_ok = not any(a <= self.t < b for a, b in self.room_lost)
        base_ok = not any(a <= self.t < b for a, b in self.base_lost)
        t_room = meas
        t_base = round(meas + 1.08 + self.rnd.gauss(0, 0.03), 2)
        if room_ok and base_ok:
            self.offset += dt_s / (6 * 3600.0) * ((t_base - t_room) - self.offset)     # TAU_OFF 6 h
        t_base_corr = t_base - self.offset
        if room_ok and base_ok:
            src, T = 1, W_ROOM * t_room + (1 - W_ROOM) * t_base_corr
        elif room_ok:
            src, T = 2, t_room
        elif base_ok:
            src, T = 3, t_base_corr
        else:
            src, T = 0, None
        if room_ok:
            self.node_last = self.t - self.rnd.uniform(0, 30)
        if src != self.src:
            self.event("source", src, self.src)
            self.src = src
        self.t_ctrl = T
        alarms = (0 if room_ok else AL_ROOM) | (0 if base_ok else AL_BASE) | (AL_NOSENSOR if T is None else 0)
        for bit in range(3):
            m = 1 << bit
            if alarms & m and not self.alarms & m:
                self.event("alarm_on", bit)
            if self.alarms & m and not alarms & m:
                self.event("alarm_off", bit)
        self.alarms = alarms

        # ---- setpoint
        now = self.now()
        h = now.hour + now.minute / 60.0 + now.second / 3600.0
        period = 0 if 6.5 <= h < 22.5 else 1
        if not self.time_valid():
            period = 0
        sp = SP_AWAY if self.mode == MODE_AWAY else (SP_COMFORT if period == 0 else SP_NIGHT)
        if self.last_sp is not None and abs(sp - self.last_sp) > 0.05:
            self.event("setpoint", 0, 0, sp)
            if sp > self.last_sp:
                self.recovering = True
        self.last_sp = sp

        # ---- day rollover
        if self.time_valid():
            d = now.date()
            if self.day is not None and d != self.day:
                self.event("day", 0, 0, self.billed_s / 60.0, float(self.pulses))
                self.billed_s, self.pulses = 0.0, 0
            self.day = d
        classic = self.algo_alt and self.time_valid() and (now.date() - dt.date(1970, 1, 1)).days % 2 == 1

        # ---- demand -> targets
        limp = False
        if self.arrival_left > 0 and T is not None and T >= sp - 2.0:
            self.arrival_left = 0.0
            self.event("arrival", 0, 0, T)
        if self.arrival_left > 0:
            self.arrival_left -= dt_s
            if self.arrival_left <= 0:
                self.event("arrival", 0, 0, T if T is not None else 0.0)
            on_t, off_t, self.u = math.inf, 0.0, 1.0
            r_open = r_close = R_ARRIVAL
            self.pid.mf = None
            self.recovering = True
        elif T is None:
            limp = True
            self.u = min(max(self.pid.I, 0.05), 0.30)
            on_t, off_t = self.mod.targets(self.u)
            r_open = r_close = R_LIMP
        elif classic:
            if T < sp - CLASSIC_HYST:
                self.classic_call = True
            elif T > sp + CLASSIC_HYST:
                self.classic_call = False
            on_t, off_t = (math.inf, 0.0) if self.classic_call else (0.0, math.inf)
            self.u = 1.0 if self.classic_call else 0.0
            r_open, r_close = R_CLASSIC, R_TARGET
        else:
            if self.recovering and sp - T <= 0.3:
                self.recovering = False
            self.u = self.pid.update(sp, T, dt_s, self.mod.uc if self.recovering else 1.0)
            on_t, off_t = self.mod.targets(self.u)
            r_open = R_CONT if on_t == math.inf else R_PULSE
            r_close = R_PULSE if self.u >= self.mod.u_min else R_NODEMAND
        if limp != self.limp:
            self.event("limp_on" if limp else "limp_off", 0, 0, self.u)
            self.limp = limp

        # ---- valve FSM: OFF_MIN absolute, ON_MIN for automatic closes
        self.t_state += dt_s
        if self.on:
            self.billed_s += dt_s
            if self.t_state >= max(ON_MIN, on_t):
                self.on, self.t_state, self.reason = False, 0.0, r_close
                self.event("valve_close", r_close, 0, self.u, T or 0.0)
        elif self.t_state >= max(OFF_MIN, off_t):
            self.on, self.t_state, self.reason = True, 0.0, r_open
            self.pulses += 1
            self.event("valve_open", r_open, 0, self.u, T or 0.0)
        elif self.reason != R_BOOT or self.t_state >= OFF_MIN:
            self.reason = r_close

        # ---- humidity (room node): slowly varying moisture content
        rh = dew = None
        if room_ok:
            dew_room = 4.0 + 2.5 * math.sin(2 * math.pi * self.t / (5.3 * 86400)) + min(1.5, 0.15 * (t_room - 8))
            rh = min(max(rh_from_dew(t_room, dew_room) + self.rnd.gauss(0, 0.3), 5.0), 99.0)
            dew = dew_point(t_room, rh)
            if self.hum in (0, 1):
                self.hum = 2 if rh < 35 else 3 if rh > 60 else 1
            elif self.hum == 2 and rh > 38:
                self.hum = 1
            elif self.hum == 3 and rh < 57:
                self.hum = 1

        # ---- log line every 60 s of uptime
        if self.uptime() % 60 == 0 and self.t < self.end:
            P, I, _ = self.pid.terms
            f2 = lambda v, ok=True, d=2: ("%.*f" % (d, v)) if ok and v is not None else ""   # noqa: E731
            light = ""
            if self.t > 86400:           # LDR scale learned after the first day
                light = "%d" % max(0, min(100, int(55 * max(0.0, math.sin(math.pi * (h - 7) / 10)) +
                                                   (20 if 17 <= h < 23 and self.mode == MODE_AUTO else 0) +
                                                   self.rnd.uniform(0, 4))))
            node_age = -1 if self.node_last is None else int(self.t - self.node_last)
            line = "%d,%s,%d,%s,%d,%s,%s,%s,%s,%.3f,%.2f,%d,%d,%d,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%s\n" % (
                self.unix(), self.local(), self.uptime(), f2(T, src != 0), src, f2(t_room, room_ok),
                f2(rh, room_ok, 1), f2(t_base, base_ok), f2(t_base_corr, base_ok), self.offset, sp, self.mode,
                period, 0, self.u, P, I, int(self.on), self.reason, int(self.billed_s), self.pulses, self.alarms,
                int(limp), self.hum if room_ok else 0, f2(dew, room_ok, 1), -61 + int(self.rnd.gauss(0, 2)),
                node_age, int(self.arrival_left > 0), light)
            if self.pending_partial is not None:
                line = self.pending_partial + line        # power cut: two lines merged into garbage
                self.pending_partial = None
            name = (self.local()[:10] if self.time_valid() else "notime") + ".csv"
            self.append(name, HEADER, line)
        self.t += dt_s
        return self.on


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default="fake_logs", help="output directory (default: fake_logs)")
    ap.add_argument("--days", type=float, default=17.0, help="days to simulate from Fri 13 Nov 2026 17:00")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--clean", action="store_true", help="no mangled line at the power cut")
    a = ap.parse_args()

    base = FakeBase(a.out, seed=a.seed, clean=a.clean, days=a.days)
    t_out_mean = -2.0
    p = ts.PLANT
    t0 = 8.0                                     # house kept by antifreeze, walls in equilibrium
    tm = (p["UA_air_mass"] * t0 + p["UA_mass_out"] * t_out_mean) / (p["UA_air_mass"] + p["UA_mass_out"])
    out, rec = ts.simulate(base, days=a.days, sp=SP_COMFORT, T_out_mean=t_out_mean, T0_air=t0, T0_mass=tm,
                           eval_from_h=0.0, record=True, seed=a.seed, t0_h=START.hour + START.minute / 60.0)

    # daily mean outdoor temperature of the simulation (what a weather archive would give)
    days = {}
    for r in rec:
        d = (START + dt.timedelta(hours=r[0])).strftime("%Y-%m-%d")
        days.setdefault(d, []).append(r[3])
    with open(os.path.join(a.out, "outdoor_truth.csv"), "w") as f:
        f.write("date,t_out_mean\n")
        for d in sorted(days):
            f.write("%s,%.2f\n" % (d, sum(days[d]) / len(days[d])))

    names = sorted(n for n in os.listdir(a.out) if n.endswith(".csv"))
    print("wrote %d files to %s: %s ... %s" % (len(names), a.out, names[0], names[-1]))
    print("simulator truth for the whole run: %.0f billed min/day, %.1f kWh/day, %.2f kWh per billed minute"
          % (out["billed_min_day"], out["heat_kWh_day"], out["kWh_per_billed_min"]))
    tau_h = (p["C_air_kJK"] + p["C_mass_kJK"]) * 1000 / (p["UA_mass_out"] + p["UA_air_out"]) / 3600
    print("plant used: slow time constant (C_air+C_mass)/(UA_mass_out+UA_air_out) = %.1f h" % tau_h)


if __name__ == "__main__":
    main()
