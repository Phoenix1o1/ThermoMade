#!/usr/bin/env python3
"""
tm_logs.py -- turn the TermoMade base station's SD-card logs into answers.

    python3 tm_logs.py summary <logdir> [--outdoor weather.csv]
    python3 tm_logs.py plot    <logdir> [--day YYYY-MM-DD | --last N] -o out.png
    python3 tm_logs.py fit     <logdir> [--outdoor weather.csv | --t-out C]
    python3 tm_logs.py compare <logdir> [--a-days ... --b-days ...] [--outdoor weather.csv]

<logdir> is a copy of the card's /log folder: YYYY-MM-DD.csv (one line per minute),
notime.csv (lines written before the clock was set) and events.csv. The format is defined by
firmware/src/base/logger.cpp. Columns are read by header name, so new columns do not break the
tool. Malformed lines are skipped and counted, never fatal.

Needs Python 3.8+, numpy; matplotlib only for `plot`. See README.md for what each number means.
"""
import argparse
import datetime as dt
import math
import os
import re
import sys

import numpy as np

# ----------------------------------------------------------------------------- format
SAMPLE_COLS = ("unix,local,uptime_s,t_ctrl,src,t_room,rh,t_base,t_base_corr,offset,sp,mode,period,boost,u,p,i,valve,"
               "reason,billed_today_s,pulses_today,alarms,limp,humidity,dew_c,rssi,node_age_s,arrival,light_pct"
               ).split(",")
EVENT_COLS = "unix,local,uptime_s,event,a,b,x,y".split(",")
REQUIRED = ("unix", "uptime_s", "valve")            # a line without these is useless
TEMP_COLS = ("t_ctrl", "t_room", "t_base", "t_base_corr", "sp", "dew_c", "t_out")
DAY_FILE = re.compile(r"^(\d{4}-\d{2}-\d{2})\.csv$")

MODE = {0: "AUTO", 1: "COMFORT", 2: "NIGHT", 3: "AWAY", 4: "OFF"}
REASON = {0: "BOOT", 1: "PULSE", 2: "CONT", 3: "CLASSIC", 4: "BOOST", 5: "LIMP", 6: "TARGET", 7: "NO_DEMAND",
          8: "USER_OFF", 9: "PLANT_OFF", 10: "FAULT", 11: "CYCLE_LIMIT", 12: "ARRIVAL"}
ALARM = {0: "ROOM_LOST", 1: "BASE_LOST", 2: "NO_SENSOR", 3: "DISAGREE", 4: "TIME_UNKNOWN", 5: "CONT_ON", 6: "BUDGET"}

LOG_PERIOD_S = 60.0
GAP_S = 90.0                 # two lines further apart than this = missing minute(s)
UC = 0.375                   # ON_MIN / (ON_MIN + OFF_MIN): above it the pulses get longer
HDD_BASE_C = 20.0

# PLANT defaults of tools/sim/thermal_sim.py, used when the simulator cannot be imported
PLANT_FALLBACK = dict(C_air_kJK=2000.0, C_mass_kJK=30000.0, UA_mass_out=110.0, UA_air_out=60.0)
Q_PULSE_KWH_MIN = 0.47       # simulator: heat per billed minute in the pulse regime (docs/02 §2)


# ----------------------------------------------------------------------------- parsing
class Table:
    """Minute samples as numpy columns (float64, NaN = empty field)."""

    def __init__(self, cols=None, local=None, name=""):
        self.c = cols or {k: np.zeros(0) for k in SAMPLE_COLS if k != "local"}
        self.local = local or []
        self.name = name
        self.bad = 0                 # malformed lines skipped
        self.bad_examples = []
        self.lines = 0               # non-empty lines seen (without headers)

    def __len__(self):
        return len(self.local)

    def __getitem__(self, k):
        if k in self.c:
            return self.c[k]
        return np.full(len(self), np.nan)

    def has(self, k):
        return k in self.c and len(self) > 0 and np.isfinite(self.c[k]).any()


def _num(s):
    s = s.strip()
    if s == "":
        return math.nan
    v = float(s)                 # ValueError -> malformed line
    if math.isinf(v):
        raise ValueError(s)
    return v


def _read_lines(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        print("warning: cannot read %s: %s" % (path, e), file=sys.stderr)
        return []
    text = raw.replace(b"\x00", b"").decode("utf-8", errors="replace")
    return [ln.strip("\r\n \t") for ln in text.split("\n")]


def parse_samples(path, dated=True):
    """Read one sample file. Never raises on content: bad lines are counted in Table.bad."""
    t = Table(name=os.path.basename(path))
    cols = list(SAMPLE_COLS)
    rows, local = [], []
    for ln in _read_lines(path):
        if not ln:
            continue
        if ln.startswith("unix,"):                    # header (also when repeated inside the file)
            if not rows:
                cols = [c.strip() for c in ln.split(",")]
            continue
        t.lines += 1
        f = ln.split(",")
        try:
            if len(f) != len(cols):
                raise ValueError("field count %d, expected %d" % (len(f), len(cols)))
            vals = {}
            loc = ""
            for name, s in zip(cols, f):
                if name == "local":
                    loc = s.strip()
                else:
                    vals[name] = _num(s)
            for k in REQUIRED:
                if k not in vals or math.isnan(vals[k]):
                    raise ValueError("missing %s" % k)
            if vals["valve"] not in (0.0, 1.0) or vals["unix"] < 0 or vals["uptime_s"] < 0:
                raise ValueError("value out of range")
            for k in TEMP_COLS:
                v = vals.get(k, math.nan)
                if not math.isnan(v) and not -50.0 <= v <= 100.0:
                    raise ValueError("%s out of range" % k)
            if dated:
                if vals["unix"] <= 0:
                    raise ValueError("no time in a dated file")
                dt.datetime.strptime(loc, "%Y-%m-%d %H:%M:%S")
        except ValueError as e:
            t.bad += 1
            if len(t.bad_examples) < 3:
                t.bad_examples.append("%s: %s" % (e, ln[:60]))
            continue
        rows.append(vals)
        local.append(loc)
    names = [c for c in cols if c != "local"]
    t.c = {k: np.array([r.get(k, math.nan) for r in rows], dtype=float) for k in names}
    t.local = local
    return t


def concat(tables):
    tables = [t for t in tables if len(t)]
    out = Table()
    if not tables:
        return out
    keys = []
    for t in tables:
        for k in t.c:
            if k not in keys:
                keys.append(k)
    out.c = {k: np.concatenate([t[k] for t in tables]) for k in keys}
    out.local = [s for t in tables for s in t.local]
    return out


def parse_events(path):
    """-> (events, bad). events = list of dict(unix, local, uptime_s, event, a, b, x, y, text)."""
    ev, bad = [], 0
    for ln in _read_lines(path):
        if not ln or ln.startswith("unix,"):
            continue
        f = ln.split(",")
        try:
            if len(f) < 8:
                raise ValueError
            e = dict(unix=_num(f[0]), local=f[1].strip(), uptime_s=_num(f[2]), event=f[3].strip(),
                     a=_num(f[4]), b=_num(f[5]), x=_num(f[6]), y=_num(f[7]), text=",".join(f[8:]))
            if not e["event"] or math.isnan(e["unix"]):
                raise ValueError
        except ValueError:
            bad += 1
            continue
        ev.append(e)
    return ev, bad


class Logs:
    def __init__(self, logdir):
        self.dir = logdir
        self.days = {}               # "YYYY-MM-DD" -> Table
        self.notime = Table(name="notime.csv")
        self.events, self.events_bad = [], 0
        self.other = []
        if not os.path.isdir(logdir):
            raise SystemExit("error: %s is not a directory" % logdir)
        for fn in sorted(os.listdir(logdir)):
            p = os.path.join(logdir, fn)
            m = DAY_FILE.match(fn)
            if m:
                self.days[m.group(1)] = parse_samples(p)
            elif fn == "notime.csv":
                self.notime = parse_samples(p, dated=False)
            elif fn == "events.csv":
                self.events, self.events_bad = parse_events(p)
            else:
                self.other.append(fn)

    def dates(self):
        return sorted(self.days)

    def bad_total(self):
        return sum(t.bad for t in self.days.values()) + self.notime.bad

    def require_data(self):
        if not any(len(t) for t in self.days.values()) and not len(self.notime):
            raise SystemExit("error: no usable log lines in %s (expected YYYY-MM-DD.csv files)" % self.dir)


def read_outdoor(path):
    """Optional weather file: 'date,t_mean' per line (daily mean outdoor temperature, C)."""
    out = {}
    if not path:
        return out
    for ln in _read_lines(path):
        f = [x.strip() for x in ln.replace(";", ",").split(",")]
        if len(f) < 2 or not re.match(r"^\d{4}-\d{2}-\d{2}", f[0]):
            continue
        try:
            out[f[0][:10]] = float(f[1])
        except ValueError:
            pass
    return out


# ----------------------------------------------------------------------------- helpers
def counter_total(c):
    """Total counted by a 'today' counter that restarts at 0 after a reboot: sum of the
    increases; after a decrease (restart) the new value counts from 0. The first value is
    what the counter already held (since midnight, or since boot)."""
    c = c[np.isfinite(c)]
    if len(c) == 0:
        return math.nan, 0
    d = np.diff(c)
    resets = int((d < 0).sum())
    return float(c[0] + d[d > 0].sum() + c[1:][d < 0].sum()), resets


def nanstat(fn, a):
    a = a[np.isfinite(a)]
    return float(fn(a)) if len(a) else math.nan


def minute_of_day(local):
    return int(local[11:13]) * 60 + int(local[14:16])


def fnum(v, fmt="%.1f", dash="-"):
    return dash if v is None or (isinstance(v, float) and math.isnan(v)) else fmt % v


def print_table(rows, cols, out=None):
    out = out or sys.stdout
    w = [max(len(c), max((len(str(r[i])) for r in rows), default=0)) for i, c in enumerate(cols)]
    out.write("  ".join(c.rjust(x) for c, x in zip(cols, w)) + "\n")
    for r in rows:
        if r == "---":
            out.write("  ".join("-" * x for x in w) + "\n")
        else:
            out.write("  ".join(str(v).rjust(x) for v, x in zip(r, w)) + "\n")


def day_stats(date, t, prev_tail=None, first=False, last=False, t_out=None):
    """All per-day numbers. prev_tail = (unix, valve, uptime) of the line before this file."""
    s = dict(date=date, rows=len(t), bad=t.bad)
    n = len(t)
    if n == 0:
        s.update(empty=True, gaps=0 if (first or last) else 1440, valve_min=0, pulses=0, billed_ctr_min=math.nan,
                 resets=0, reboots=0, limp_min=0, no_temp_min=0, alarms=[], algo="-", t_mean=math.nan,
                 t_min=math.nan, t_max=math.nan, sp_mean=math.nan, rms_comfort=math.nan, sse=0.0, n_comfort=0,
                 in_band=math.nan, n_in_band=0, rh_mean=math.nan, t_out=t_out, pulses_ctr=math.nan,
                 away_min=0, arrival_min=0, boost_min=0, comfort_min=0)
        return s
    unix, valve, up = t["unix"], t["valve"], t["uptime_s"]
    # --- gaps
    d = np.diff(unix)
    gaps = int(sum(round(x / LOG_PERIOD_S) - 1 for x in d if x > GAP_S))
    if not first:
        gaps += minute_of_day(t.local[0])
    if not last:
        gaps += 1439 - minute_of_day(t.local[-1])
    s["gaps"] = max(gaps, 0)
    s["clock_jumps"] = int((d < 0).sum())
    # --- billed time
    s["valve_min"] = int(valve.sum())
    s["billed_ctr_min"], s["resets"] = counter_total(t["billed_today_s"])
    s["billed_ctr_min"] /= 60.0
    s["pulses_ctr"], _ = counter_total(t["pulses_today"])
    reb = int((np.diff(up) < 0).sum())
    if prev_tail is not None and up[0] < prev_tail[2]:
        reb += 1
    s["reboots"] = reb
    # --- pulses = 0 -> 1 edges
    prev = prev_tail[1] if (prev_tail is not None and 0 <= unix[0] - prev_tail[0] <= GAP_S) else 0.0
    v = np.concatenate([[prev], valve])
    s["pulses"] = int(((v[1:] == 1) & (v[:-1] == 0)).sum())
    # --- temperatures
    T, sp = t["t_ctrl"], t["sp"]
    s["t_mean"], s["t_min"], s["t_max"] = nanstat(np.mean, T), nanstat(np.min, T), nanstat(np.max, T)
    s["sp_mean"] = nanstat(np.mean, sp)
    limp = np.nan_to_num(t["limp"]) == 1
    mode, period = t["mode"], t["period"]
    comfort = ((mode == 1) | ((mode == 0) & (period == 0))) & np.isfinite(T) & np.isfinite(sp) & ~limp
    err = (T - sp)[comfort]
    s["n_comfort"], s["sse"] = int(comfort.sum()), float((err ** 2).sum())
    s["comfort_min"] = s["n_comfort"]
    s["rms_comfort"] = math.sqrt(s["sse"] / len(err)) if len(err) else math.nan
    s["n_in_band"] = int((np.abs(err) <= 0.3).sum())
    s["in_band"] = 100.0 * s["n_in_band"] / len(err) if len(err) else math.nan
    s["limp_min"] = int(limp.sum())
    s["no_temp_min"] = int((~np.isfinite(T)).sum())
    s["away_min"] = int(((mode == 3) | (mode == 4)).sum())
    s["arrival_min"] = int((np.nan_to_num(t["arrival"]) == 1).sum())
    s["boost_min"] = int((np.nan_to_num(t["boost"]) == 1).sum())
    s["rh_mean"] = nanstat(np.mean, t["rh"])
    bits = 0
    for a in t["alarms"][np.isfinite(t["alarms"])]:
        bits |= int(a)
    s["alarms"] = [ALARM.get(b, "BIT%d" % b) for b in range(16) if bits >> b & 1]
    # --- which algorithm ran (reason 3 = classic thermostat call)
    reason = t["reason"]
    classic = bool((reason == 3).any())
    pulse = bool(((valve == 1) & ((reason == 1) | (reason == 2))).any())
    s["algo"] = "MIXED" if classic and pulse else "CLASSIC" if classic else "PULSE" if pulse else "-"
    # --- outdoor: from a (future) t_out column, else from the weather file
    if t.has("t_out"):
        s["t_out"] = nanstat(np.mean, t["t_out"])
    else:
        s["t_out"] = t_out
    return s


def all_day_stats(logs, outdoor=None):
    outdoor = outdoor or {}
    dates = logs.dates()
    out, prev_tail = [], None
    for i, d in enumerate(dates):
        t = logs.days[d]
        out.append(day_stats(d, t, prev_tail, first=(i == 0), last=(i == len(dates) - 1), t_out=outdoor.get(d)))
        if len(t):
            prev_tail = (t["unix"][-1], t["valve"][-1], t["uptime_s"][-1])
    return out


def hdd(s):
    return max(0.0, HDD_BASE_C - s["t_out"]) if s.get("t_out") is not None and not math.isnan(s["t_out"]) else math.nan


# ----------------------------------------------------------------------------- summary
def cmd_summary(a):
    logs = Logs(a.logdir)
    logs.require_data()
    outdoor = read_outdoor(a.outdoor)
    st = all_day_stats(logs, outdoor)
    have_out = any(not math.isnan(hdd(s)) for s in st)

    # firmware's own day totals from the 'day' events (x = billed minutes of the day that ended)
    fw_day = {}
    for e in logs.events:
        if e["event"] == "day" and len(e["local"]) >= 10:
            try:
                d = dt.datetime.strptime(e["local"][:10], "%Y-%m-%d").date() - dt.timedelta(days=1)
                fw_day[d.isoformat()] = e["x"]
            except ValueError:
                pass

    cols = ["date", "lines", "gaps", "billed", "ctr", "chk", "pulses", "Tmean", "Tmin", "Tmax", "SP", "rmsC",
            "band%", "RH%", "limp"]
    if have_out:
        cols += ["Tout", "HDD", "b/HDD"]
    cols += ["algo", "alarms"]
    rows = []
    for s in st:
        ctr = s["billed_ctr_min"]
        if math.isnan(ctr):
            chk = "-"
        elif abs(ctr - s["valve_min"]) <= max(3.0, 0.02 * s["valve_min"]):
            chk = "ok"
        else:
            chk = "DIFF"
        r = [s["date"], s["rows"], s["gaps"], s["valve_min"], fnum(ctr, "%.0f"), chk, s["pulses"],
             fnum(s["t_mean"], "%.2f"), fnum(s["t_min"], "%.2f"), fnum(s["t_max"], "%.2f"), fnum(s["sp_mean"], "%.2f"),
             fnum(s["rms_comfort"], "%.2f"), fnum(s["in_band"], "%.0f"), fnum(s["rh_mean"], "%.0f"), s["limp_min"]]
        if have_out:
            h = hdd(s)
            r += [fnum(s["t_out"]), fnum(h), fnum(s["valve_min"] / h if h and h > 0 else math.nan)]
        r += [s["algo"], "+".join(s["alarms"]) or "-"]
        rows.append(r)

    print("TermoMade log summary: %s" % os.path.abspath(a.logdir))
    nd = len(st)
    print("%d daily files, %s .. %s" % (nd, st[0]["date"], st[-1]["date"]) if nd else "no daily files")
    print()
    if rows:
        print_table(rows, cols)
    print()
    print("billed = minutes with valve=1 (one line per minute) · ctr = same from the billed_today_s counter")
    print("chk    = ok when the two agree within 3 min or 2 %; DIFF usually means missing lines (see gaps)")
    print("rmsC / band% = RMS of (T - setpoint) and share within +-0.3 C, Comfort period only · limp = minutes")

    # ---- totals
    tot_valve = sum(s["valve_min"] for s in st)
    tot_ctr = sum(s["billed_ctr_min"] for s in st if not math.isnan(s["billed_ctr_min"]))
    tot_pulses = sum(s["pulses"] for s in st)
    tot_gaps = sum(s["gaps"] for s in st)
    full = [s for s in st if s["rows"] and s["gaps"] <= 72]
    sse, nc = sum(s["sse"] for s in st), sum(s["n_comfort"] for s in st)
    print()
    print("TOTALS")
    print("  billed valve time     : %d min = %.1f h (by counter: %.0f min)" % (tot_valve, tot_valve / 60.0, tot_ctr))
    if full:
        print("  per complete day      : %.0f min/day, %.1f pulses/day  (%d days with <= 5 %% missing)" % (
            np.mean([s["valve_min"] for s in full]), np.mean([s["pulses"] for s in full]), len(full)))
    print("  pulses (valve opens)  : %d" % tot_pulses)
    if tot_valve and tot_pulses:
        print("  mean pulse length     : %.1f min" % (tot_valve / tot_pulses))
    print("  comfort RMS error     : %s C over %d comfort minutes" % (fnum(math.sqrt(sse / nc) if nc else math.nan, "%.2f"), nc))
    print("  LIMP                  : %d min · no control temperature: %d min" % (
        sum(s["limp_min"] for s in st), sum(s["no_temp_min"] for s in st)))
    print("  missing minutes       : %d (%.1f %% of the period)" % (tot_gaps, 100.0 * tot_gaps / max(1, tot_gaps + sum(s["rows"] for s in st))))
    print("  reboots               : %d seen in the minute lines (uptime restarts), counter restarts: %d" % (
        sum(s["reboots"] for s in st), sum(s["resets"] for s in st)))
    alarms = sorted({x for s in st for x in s["alarms"]})
    print("  alarms seen           : %s" % (", ".join(alarms) or "none"))
    if have_out:
        hs = [(s["valve_min"], hdd(s)) for s in full if not math.isnan(hdd(s)) and hdd(s) > 0]
        if hs:
            print("  billed min per HDD    : %.1f  (complete days with outdoor data: %d, HDD base %.0f C)" % (
                sum(x for x, _ in hs) / sum(h for _, h in hs), len(hs), HDD_BASE_C))
    else:
        print("  outdoor temperature   : not logged (no outdoor node). Give --outdoor date,t_mean.csv for HDD.")
    print("  heat per billed minute: not available from these logs (needs the pipe node or a heat meter)")
    bad = logs.bad_total()
    print("  malformed lines       : %d skipped%s" % (bad, " (events.csv: %d)" % logs.events_bad if logs.events_bad else ""))
    for t in list(logs.days.values()) + [logs.notime]:
        for ex in t.bad_examples:
            print("      %s: %s" % (t.name, ex))

    # ---- cross-check with the firmware's own day totals
    chk = [(s["date"], s["valve_min"], fw_day[s["date"]]) for s in st if s["date"] in fw_day]
    off = [c for c in chk if abs(c[1] - c[2]) > max(3.0, 0.02 * c[1])]
    if chk:
        print("  firmware 'day' events : %d days, %d differ from the minute lines by > 3 min / 2 %%%s" % (
            len(chk), len(off), "".join("\n      %s: lines %d min, event %.0f min (reboot or gap that day?)" % c for c in off)))

    # ---- notime.csv
    nt = logs.notime
    if len(nt) or nt.bad:
        sessions = 1 + int((np.diff(nt["uptime_s"]) < 0).sum()) if len(nt) else 0
        v = np.concatenate([[0.0], nt["valve"]])
        print()
        print("notime.csv (clock not set, not assigned to any day): %d lines in %d power-on session(s), "
              "%d billed min, %d pulses, T mean %s C, %d malformed" % (
                  len(nt), sessions, int(nt["valve"].sum()), int(((v[1:] == 1) & (v[:-1] == 0)).sum()),
                  fnum(nanstat(np.mean, nt["t_ctrl"]), "%.2f"), nt.bad))
        print("  these minutes ARE billed by the condominium: total incl. notime = %d min" % (tot_valve + int(nt["valve"].sum())))

    # ---- events
    if logs.events:
        cnt = {}
        for e in logs.events:
            cnt[e["event"]] = cnt.get(e["event"], 0) + 1
        print()
        print("events.csv: " + ", ".join("%s %d" % (k, cnt[k]) for k in sorted(cnt)))
        texts = [e for e in logs.events if e["event"] == "text"]
        for e in texts[-a.texts:] if a.texts else []:
            print("  %s  %s" % (e["local"] or "(no time)          ", e["text"]))
    return 0


# ----------------------------------------------------------------------------- plot
def select_days(logs, day=None, last=None):
    dates = [d for d in logs.dates() if len(logs.days[d])]
    if day:
        if day not in logs.days or not len(logs.days[day]):
            raise SystemExit("error: no data for %s (have %s .. %s)" % (day, dates[0] if dates else "-", dates[-1] if dates else "-"))
        return [day]
    return dates[-(last or 1):]


def cmd_plot(a):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    logs = Logs(a.logdir)
    logs.require_data()
    sel = select_days(logs, a.day, a.last)
    if not sel:
        raise SystemExit("error: no dated log files to plot")
    t = concat([logs.days[d] for d in sel])
    x = np.array([mdates.date2num(dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S")) for s in t.local])
    unix = t["unix"]
    brk = np.concatenate([[False], (np.diff(unix) > GAP_S) | (np.diff(unix) < 0)])   # lines break at gaps

    def gapped(y):
        y = y.astype(float).copy()
        y[brk] = np.nan
        return y

    INK, MUTED, GRID, AXIS, SURF = "#0b0b0b", "#898781", "#e1e0d9", "#c3c2b7", "#fcfcfb"
    C_CTRL, C_ROOM, C_BASE = "#2a78d6", "#eb6834", "#1baf7a"
    fig, axs = plt.subplots(3, 1, figsize=(12, 8), sharex=True, gridspec_kw=dict(height_ratios=[3.2, 1, 1.2]),
                            facecolor=SURF)
    ax, axu, axh = axs
    for z in axs:
        z.set_facecolor(SURF)
        z.grid(True, color=GRID, lw=0.6)
        z.set_axisbelow(True)
        for sp_ in ("top", "right"):
            z.spines[sp_].set_visible(False)
        for sp_ in ("left", "bottom"):
            z.spines[sp_].set_color(AXIS)
        z.tick_params(colors=MUTED, labelsize=9)

    # valve as shaded bars (a state, not a series: neutral grey), on the temperature and u panels
    v = t["valve"]
    edges = np.flatnonzero(np.diff(np.concatenate([[0], v, [0]])))
    step = LOG_PERIOD_S / 86400.0
    for i0, i1 in zip(edges[::2], edges[1::2]):
        for z in (ax, axu):
            z.axvspan(x[i0], x[i1 - 1] + step, color="#52514e", alpha=0.16, lw=0)
    ax.plot(x, gapped(t["t_room"]), color=C_ROOM, lw=1.0, label="room node")
    ax.plot(x, gapped(t["t_base_corr"]), color=C_BASE, lw=1.0, label="CYD corrected")
    ax.plot(x, gapped(t["t_ctrl"]), color=C_CTRL, lw=2.0, label="control temperature")
    ax.step(x, gapped(t["sp"]), where="post", color=INK, lw=1.2, ls=(0, (5, 3)), label="setpoint")
    ax.fill_between([], [], [], color="#52514e", alpha=0.16, lw=0, label="valve open")
    ax.set_ylabel("°C", color=MUTED)
    temps = np.concatenate([t["t_ctrl"], t["t_room"], t["t_base_corr"]])
    temps = temps[np.isfinite(temps)]
    if len(temps):
        lo, hi = temps.min(), temps.max()
        spv = t["sp"][np.isfinite(t["sp"])]
        if len(spv) and spv.min() >= lo - 3:          # keep a far-away AWAY setpoint from squashing the plot
            lo = min(lo, spv.min())
        if len(spv):
            hi = max(hi, min(spv.max(), hi + 3))
        ax.set_ylim(lo - 0.3, hi + 0.3)
    ax.legend(loc="upper left", ncol=5, fontsize=9, frameon=False, bbox_to_anchor=(0, 1.11))
    limp = np.nan_to_num(t["limp"]) == 1
    if limp.any():
        e2 = np.flatnonzero(np.diff(np.concatenate([[0], limp.astype(int), [0]])))
        for i0, i1 in zip(e2[::2], e2[1::2]):
            ax.annotate("LIMP", (x[i0], ax.get_ylim()[1]), fontsize=8, color=INK, va="top", ha="left")
    axu.step(x, gapped(t["u"]), where="post", color=INK, lw=1.2)
    axu.axhline(UC, color=MUTED, lw=0.8, ls=":")
    axu.annotate("uc", (x[0], UC), fontsize=8, color=MUTED, va="bottom")
    axu.set_ylim(-0.03, 1.03)
    axu.set_ylabel("demand u", color=MUTED)
    axh.plot(x, gapped(t["rh"]), color=C_CTRL, lw=1.6)
    axh.set_ylabel("humidity %", color=MUTED)
    rh = t["rh"][np.isfinite(t["rh"])]
    if len(rh):
        axh.set_ylim(max(0, rh.min() - 5), min(100, rh.max() + 5))
        for lim, txt in ((35, "too dry below 35"), (60, "too humid above 60")):
            if axh.get_ylim()[0] < lim < axh.get_ylim()[1]:
                axh.axhline(lim, color=MUTED, lw=0.8, ls=":")
                axh.annotate(txt, (x[0], lim), fontsize=8, color=MUTED, va="bottom")
    else:
        axh.text(0.5, 0.5, "no humidity data", transform=axh.transAxes, ha="center", color=MUTED)
    loc = mdates.AutoDateLocator(minticks=6, maxticks=14)
    axh.xaxis.set_major_locator(loc)
    axh.xaxis.set_major_formatter(mdates.ConciseDateFormatter(loc))
    v0 = np.concatenate([[0.0], v])
    title = "%s%s  ·  %d billed min, %d pulses" % (
        sel[0], "" if len(sel) == 1 else " .. " + sel[-1], int(v.sum()), int(((v0[1:] == 1) & (v0[:-1] == 0)).sum()))
    fig.suptitle("TermoMade  " + title, x=0.01, ha="left", fontsize=12, color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(a.out, dpi=110, facecolor=SURF)
    bad = sum(logs.days[d].bad for d in sel)
    print("wrote %s (%d lines, %s%s%s)" % (a.out, len(t), sel[0], "" if len(sel) == 1 else " .. " + sel[-1],
                                           ", %d malformed lines skipped" % bad if bad else ""))
    return 0


# ----------------------------------------------------------------------------- fit
def contiguous_segments(time_s, uptime_s, max_step=150.0):
    """Index ranges [i0, i1) without reboot and without holes (one missed line is tolerated)."""
    n = len(time_s)
    if n == 0:
        return []
    d, du = np.diff(time_s), np.diff(uptime_s)
    cut = np.flatnonzero((d <= 0) | (d > max_step) | (du < 0)) + 1
    b = np.concatenate([[0], cut, [n]])
    return [(int(b[i]), int(b[i + 1])) for i in range(len(b) - 1) if b[i + 1] - b[i] >= 2]


def slope_per_h(t_s, y):
    ok = np.isfinite(y)
    if ok.sum() < 10 or np.ptp(t_s[ok]) <= 0:
        return math.nan
    return float(np.polyfit(t_s[ok] / 3600.0, y[ok], 1)[0])


def fit_exponential(t_h, y, clock_h=None):
    """y = Tinf + A exp(-t/tau) [+ a 24 h sine when clock_h is given: the outdoor daily swing]:
    grid over tau, linear least squares for the rest.
    -> dict(tau, lo, hi, tinf, rms, bounded). The interval is a profile of the fit error with a
    reduced number of independent samples (slow residuals, not white noise)."""
    ok = np.isfinite(y)
    t_h, y = t_h[ok], y[ok]
    extra = []
    if clock_h is not None:
        w = 2 * math.pi * clock_h[ok] / 24.0
        extra = [np.sin(w), np.cos(w)]
    taus = np.logspace(math.log10(4.0), math.log10(600.0), 260)
    sse = np.empty(len(taus))
    par = []
    for k, tau in enumerate(taus):
        A = np.column_stack([np.ones_like(t_h), np.exp(-t_h / tau)] + extra)
        p, *_ = np.linalg.lstsq(A, y, rcond=None)
        r = y - A @ p
        sse[k] = float(r @ r)
        par.append(p)
    k = int(np.argmin(sse))
    n_eff = max(5.0, np.ptp(t_h) / 6.0)
    thr = sse[k] * (1.0 + 4.0 / max(1.0, n_eff - 3.0 - len(extra)))
    inside = np.flatnonzero(sse <= thr)
    lo, hi = taus[inside[0]], taus[inside[-1]]
    bounded = inside[0] > 0 and inside[-1] < len(taus) - 1 and 0 < k < len(taus) - 1
    return dict(tau=float(taus[k]), lo=float(lo), hi=float(hi), tinf=float(par[k][0]),
                rms=math.sqrt(sse[k] / len(y)), bounded=bool(bounded), harmonic=bool(extra))


def load_plant():
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "sim"))
    try:
        import thermal_sim
        return dict(thermal_sim.PLANT), True
    except Exception:                                    # noqa: BLE001 - any import problem: use the fallback
        return dict(PLANT_FALLBACK), False
    finally:
        sys.path.pop(0)


def analyse_fit(logs, outdoor=None, t_out_const=None, settle_min=90, min_cool_min=120, window_min=120):
    """All the numbers of `fit` as a dict (used by cmd_fit and by the tests)."""
    outdoor = outdoor or {}
    series = []
    dated = concat([logs.days[d] for d in logs.dates()])
    if len(dated):
        series.append((0, dated, dated["unix"]))
    if len(logs.notime):
        series.append((1, logs.notime, logs.notime["uptime_s"]))

    cool, cool_w, heat_w, pulses = [], [], [], []
    W = window_min
    hours_total = 0.0
    for base, tb, time_s in series:
        T, valve = tb["t_ctrl"], tb["valve"]
        limp = np.nan_to_num(tb["limp"]) == 1
        T = np.where(limp, np.nan, T)
        for i0, i1 in contiguous_segments(time_s, tb["uptime_s"]):
            ts_, Ts, vs = time_s[i0:i1], T[i0:i1], valve[i0:i1]
            hours_total += (ts_[-1] - ts_[0]) / 3600.0
            n = i1 - i0
            # ---- valve-closed stretches
            e = np.flatnonzero(np.diff(np.concatenate([[1], vs, [1]])))        # runs of 0
            closed = np.zeros(n, dtype=bool)                                   # closed AND settled
            for j0, j1 in zip(e[::2], e[1::2]):
                k0 = j0 + settle_min
                if j1 - k0 < min_cool_min:
                    continue
                closed[k0:j1] = True
                tt, yy = ts_[k0:j1], Ts[k0:j1]
                if np.isfinite(yy).sum() < 0.8 * len(yy):
                    continue
                rate = slope_per_h(tt, yy)
                st = dict(base=base, t_mid=float(tt.mean()), hours=(tt[-1] - tt[0]) / 3600.0, rate=rate,
                          t_mean=nanstat(np.mean, yy), t_start=float(yy[np.isfinite(yy)][0]),
                          t_end=float(yy[np.isfinite(yy)][-1]), date=tb.local[i0 + (k0 + j1) // 2][:10], exp=None)
                st["drop"] = st["t_start"] - st["t_end"]
                if st["hours"] >= 18.0 and st["drop"] >= 1.5:
                    # a daily sine needs more than one day of data to be told from the decay
                    clock = tt[::5] / 3600.0 if st["hours"] >= 30.0 else None
                    st["exp"] = fit_exponential((tt[::5] - tt[0]) / 3600.0, yy[::5], clock)
                to = outdoor.get(st["date"], t_out_const)
                st["t_out"] = to
                st["tau_out"] = (st["t_mean"] - to) / -rate if (to is not None and rate < -1e-4) else math.nan
                cool.append(st)
            # ---- windows: duty and temperature slope
            for w0 in range(0, n - W + 1, W):
                tt, yy, vv = ts_[w0:w0 + W], Ts[w0:w0 + W], vs[w0:w0 + W]
                if np.isfinite(yy).sum() < 0.9 * W:
                    continue
                w = dict(base=base, t_mid=float(tt.mean()), t_mean=nanstat(np.mean, yy), rate=slope_per_h(tt, yy),
                         duty=float(vv.mean()), date=tb.local[i0 + w0 + W // 2][:10])
                if w["duty"] == 0.0:
                    if closed[w0:w0 + W].all():
                        cool_w.append(w)
                else:
                    heat_w.append(w)
            # ---- single pulses: how much the room rises after one fill
            o = np.flatnonzero(np.diff(np.concatenate([[0], vs, [0]])))
            starts, ends = o[::2], o[1::2]
            for k, (p0, p1) in enumerate(zip(starts, ends)):
                nxt = starts[k + 1] if k + 1 < len(starts) else n
                prv = ends[k - 1] if k > 0 else 0
                if not (2 <= p1 - p0 <= 5) or p0 - prv < 10 or p0 < 3 or nxt - p1 < 15:
                    continue
                after = Ts[p0:min(nxt, p0 + 40)]
                before = Ts[p0 - 3:p0 + 1]
                if not np.isfinite(after).all() or not np.isfinite(before).all():
                    continue
                pulses.append(dict(rise=float(after.max() - before.mean()), t_peak=int(np.argmax(after)),
                                   on_min=int(p1 - p0), off_before=int(p0 - prv)))

    res = dict(hours=hours_total, cool=cool, n_cool_windows=len(cool_w), pulses=pulses, settle_min=settle_min,
               window_min=W)

    # ---- cooling summary
    rates = np.array([c["rate"] for c in cool if np.isfinite(c["rate"])])
    res["cool_hours"] = float(sum(c["hours"] for c in cool))
    res["cool_rate_med"] = float(np.median(rates)) if len(rates) else math.nan
    exps = [c for c in cool if c["exp"]]
    good = [c for c in exps if c["exp"]["bounded"] and c["hours"] >= 24 and c["drop"] >= 2.0 and
            (c["exp"]["hi"] - c["exp"]["lo"]) / c["exp"]["tau"] < 0.6]
    res["tau_exp"] = float(np.median([c["exp"]["tau"] for c in good])) if good else math.nan
    res["tau_exp_lo"] = float(min(c["exp"]["lo"] for c in good)) if good else math.nan
    res["tau_exp_hi"] = float(max(c["exp"]["hi"] for c in good)) if good else math.nan
    res["tau_exp_n"] = len(good)
    res["n_exp_tried"] = len(exps)
    to = np.array([c["tau_out"] for c in cool if np.isfinite(c["tau_out"]) and c["tau_out"] > 0])
    res["tau_out_n"] = len(to)
    res["tau_out_days"] = len({c["date"] for c in cool if np.isfinite(c["tau_out"]) and c["tau_out"] > 0})
    res["tau_out"] = float(np.median(to)) if len(to) else math.nan
    res["tau_out_iqr"] = (float(np.percentile(to, 25)), float(np.percentile(to, 75))) if len(to) else (math.nan, math.nan)
    res["tau_out_ok"] = len(to) >= 5 and res["tau_out_days"] >= 3 and \
        (res["tau_out_iqr"][1] - res["tau_out_iqr"][0]) / res["tau_out"] < 0.6
    res["tinf"], tinf_tout = math.nan, None
    if res["tau_exp_n"]:
        res["tau"], res["tau_src"] = res["tau_exp"], "exponential fit of long cooling stretches"
        res["tinf"] = float(np.median([c["exp"]["tinf"] for c in good]))
        touts = [c["t_out"] for c in good if c["t_out"] is not None]
        tinf_tout = float(np.mean(touts)) if len(touts) == len(good) else None
    elif res["tau_out_ok"]:
        res["tau"], res["tau_src"] = res["tau_out"], "cooling rate / (T - T_outdoor)"
    else:
        res["tau"], res["tau_src"] = math.nan, None

    def model_ref(w):
        """Cooling slope (C/h) the fitted model predicts at this window's room temperature."""
        to = outdoor.get(w["date"], t_out_const)
        if res["tau_exp_n"]:
            tinf = res["tinf"] + (to - tinf_tout if (to is not None and tinf_tout is not None) else 0.0)
        elif to is not None:
            tinf = to
        else:
            return math.nan
        return -(w["t_mean"] - tinf) / res["tau"]
    res["ref_mode"] = "model" if np.isfinite(res["tau"]) else "windows"

    # ---- heating: gain per billed minute, per duty class
    cw_T = np.array([w["t_mean"] for w in cool_w])
    cw_r = np.array([w["rate"] for w in cool_w])
    cw_t = np.array([w["t_mid"] for w in cool_w])
    cw_b = np.array([w["base"] for w in cool_w])
    dropped = 0
    for w in heat_w:
        w["k"] = math.nan
        if res["ref_mode"] == "model":
            w["ref"] = model_ref(w)
            if np.isfinite(w["ref"]) and np.isfinite(w["rate"]):
                w["k"] = (w["rate"] - w["ref"]) / (w["duty"] * 60.0)
            else:
                dropped += 1
            continue
        if not len(cw_T) or not np.isfinite(w["rate"]):
            dropped += 1
            continue
        band = np.abs(cw_T - w["t_mean"]) <= 1.5
        near = band & (cw_b == w["base"]) & (np.abs(cw_t - w["t_mid"]) <= 48 * 3600.0)
        pick = near if near.sum() >= 3 else band
        if pick.sum() < 3:
            dropped += 1
            continue
        w["ref"] = float(np.median(cw_r[pick]))
        w["k"] = (w["rate"] - w["ref"]) / (w["duty"] * 60.0)          # C per billed minute
    res["heat_dropped"] = dropped
    bins = [(0.0, 0.08), (0.08, 0.16), (0.16, 0.25), (0.25, UC), (UC, 0.90), (0.90, 1.0001)]
    res["bins"] = []
    for lo, hi in bins:
        ws = [w for w in heat_w if lo < w["duty"] <= hi or (hi > 1 and w["duty"] >= lo)]
        ks = np.array([w["k"] for w in ws if np.isfinite(w["k"])])
        b = dict(lo=lo, hi=min(hi, 1.0), n_all=len(ws), n=len(ks), duty=float(np.mean([w["duty"] for w in ws])) if ws else math.nan,
                 rate=float(np.median([w["rate"] for w in ws])) if ws else math.nan,
                 k=float(np.median(ks)) if len(ks) else math.nan,
                 se=robust_se(ks))
        b["conf"] = confidence(b["n"], b["k"], b["se"])
        res["bins"].append(b)
    ks = np.array([w["k"] for w in heat_w if np.isfinite(w["k"]) and w["duty"] <= UC])
    res["k_low"] = float(np.median(ks)) if len(ks) else math.nan
    res["k_low_n"] = len(ks)
    res["k_low_se"] = robust_se(ks)
    res["k_low_conf"] = confidence(len(ks), res["k_low"], res["k_low_se"])
    return res


def robust_se(x):
    """Standard error of the median from the MAD (outlier-proof)."""
    if len(x) < 3:
        return math.nan
    mad = float(np.median(np.abs(x - np.median(x))))
    return 1.2533 * 1.4826 * mad / math.sqrt(len(x))


def confidence(n, k, se):
    if n < 3 or not np.isfinite(k) or not np.isfinite(se) or k <= 0:
        return "none" if n < 3 else "low"
    rel = se / k
    if n >= 20 and rel < 0.15:
        return "good"
    if n >= 8 and rel < 0.30:
        return "fair"
    return "low"


def cmd_fit(a):
    logs = Logs(a.logdir)
    logs.require_data()
    outdoor = read_outdoor(a.outdoor)
    r = analyse_fit(logs, outdoor, a.t_out, a.settle_min, a.min_cool_min, a.window_min)
    plant, from_sim = load_plant()

    print("TermoMade thermal fit: %s" % os.path.abspath(a.logdir))
    print("usable contiguous data: %.1f h (%.1f days); malformed lines skipped: %d" % (r["hours"], r["hours"] / 24, logs.bad_total()))
    if r["hours"] < 48:
        print("\nNOT ENOUGH DATA: less than 2 days of contiguous logs. Everything below is indicative at best.")

    # ---- 1. cooling
    print("\n[1] COOLING with the valve closed (first %d min after each close skipped: radiators still warm)" % r["settle_min"])
    cool = r["cool"]
    if not cool:
        print("    no closed stretch of >= %d min after settling: cooling not identifiable from this data." % a.min_cool_min)
        print("    (needs night setbacks, AWAY periods or mild days with long pauses)")
    else:
        print("    stretches: %d, total %.1f h, on %d different days" % (len(cool), r["cool_hours"], len({c["date"] for c in cool})))
        print("    cooling rate: median %.3f C/h (min %.3f, max %.3f) at room temperatures %.1f .. %.1f C" % (
            r["cool_rate_med"], max(c["rate"] for c in cool), min(c["rate"] for c in cool),
            min(c["t_mean"] for c in cool), max(c["t_mean"] for c in cool)))
        long_ = [c for c in cool if c["exp"]]
        if long_:
            print("    long stretches (>= 18 h and >= 1.5 C drop), fitted with T = Tinf + A exp(-t/tau):")
            rows = [[c["date"], "%.1f" % c["hours"], "%.1f" % c["t_start"], "%.1f" % c["t_end"], "%.0f" % c["exp"]["tau"],
                     "%.0f..%.0f" % (c["exp"]["lo"], c["exp"]["hi"]) if c["exp"]["bounded"] else "unbounded",
                     "%.1f" % c["exp"]["tinf"], "%.2f" % c["exp"]["rms"]] for c in long_]
            sys.stdout.write("      ")
            w = sys.stdout
            import io
            buf = io.StringIO()
            print_table(rows, ["mid date", "hours", "T from", "T to", "tau h", "interval h", "Tinf C", "resid C"], buf)
            w.write(buf.getvalue().replace("\n", "\n      ").rstrip() + "\n")
        else:
            print("    no stretch long enough (>= 18 h, >= 1.5 C drop) to see the curve bend:")
            print("    the time constant cannot be separated from the outdoor temperature with short stretches.")
        if r["tau_exp_n"]:
            print("    => cooling time constant tau = %.0f h (interval %.0f..%.0f h, %d stretch%s). Confidence: %s" % (
                r["tau_exp"], r["tau_exp_lo"], r["tau_exp_hi"], r["tau_exp_n"], "" if r["tau_exp_n"] == 1 else "es",
                "fair (single stretch)" if r["tau_exp_n"] == 1 else "good"))
        elif long_:
            print("    => the long stretches do not pin tau down (interval too wide or unbounded).")
        if r["tau_out_n"]:
            print("    with outdoor temperature (%s): tau = (T - T_out) / rate = median %.0f h, IQR %.0f..%.0f h, "
                  "%d stretches on %d days -> %s" % (
                      "daily means from file" if outdoor else "constant %.1f C" % a.t_out, r["tau_out"],
                      r["tau_out_iqr"][0], r["tau_out_iqr"][1], r["tau_out_n"], r["tau_out_days"],
                      "usable" if r["tau_out_ok"] else "too few / too scattered to trust"))
            print("      (biased low: nights are colder than the daily mean, and short stretches still see the walls"
                  " settling)")
        elif not outdoor and a.t_out is None:
            print("    outdoor temperature is not logged: give --outdoor date,t_mean.csv (weather archive) or --t-out C")
            print("    to turn the cooling RATE into a time constant.")

    # ---- 2. heating
    print("\n[2] HEATING: room temperature gained per billed minute (windows of %d min, by duty)" % r["window_min"])
    print("    gain = (temperature slope in the window - cooling slope at the same room temperature) / billed minutes")
    if r["ref_mode"] == "model":
        print("    cooling slope from the model of [1]: -(T - Tinf) / tau, tau = %.0f h%s" % (
            r["tau"], ", Tinf = %.1f C%s" % (r["tinf"], " shifted by each day's outdoor temperature" if (outdoor or a.t_out is not None) else
                                             " (the weather of the cooling stretch is assumed for every day)") if np.isfinite(r["tinf"]) else ", Tinf = outdoor temperature"))
    else:
        print("    cooling slope measured in valve-closed windows within +-1.5 C of the same room temperature")
    if r["ref_mode"] != "model" and r["n_cool_windows"] < 3:
        print("    NOT ENOUGH DATA: needs at least 3 valve-closed windows as the cooling reference (have %d)." % r["n_cool_windows"])
    else:
        rows = []
        for b in r["bins"]:
            rows.append(["%.2f-%.2f" % (b["lo"], b["hi"]), b["n"], fnum(b["duty"], "%.3f"), fnum(b["rate"], "%+.3f"),
                         fnum(b["k"], "%.4f"), fnum(b["se"], "%.4f"), b["conf"] if b["n_all"] else "no data"])
        import io
        buf = io.StringIO()
        print_table(rows, ["duty", "windows", "mean duty", "slope C/h", "gain C/min", "+-SE", "confidence"], buf)
        sys.stdout.write("      " + buf.getvalue().replace("\n", "\n      ").rstrip() + "\n")
        if r["heat_dropped"]:
            print("    %d windows left out: no cooling reference for their room temperature%s" % (
                r["heat_dropped"], "" if r["ref_mode"] == "model" else " (no closed window within +-1.5 C; a time constant from [1] would lift this)"))
        if r["k_low_n"]:
            print("    => pulse regime (duty <= %.3f): %.4f +- %.4f C per billed minute, %d windows. Confidence: %s" % (
                UC, r["k_low"], r["k_low_se"], r["k_low_n"], r["k_low_conf"]))
        hi = r["bins"][-1]
        if hi["n"] >= 3 and r["k_low_n"] >= 3 and hi["k"] > 0:
            print("    => continuous regime (duty >= 0.90): %.4f C per billed minute = %.0f %% of the pulse regime (%d windows, %s)"
                  % (hi["k"], 100 * hi["k"] / r["k_low"], hi["n"], hi["conf"]))
        print("    caution: closed-loop data. While the thermostat holds the setpoint the slope is ~0 and the gain is")
        print("    just (cooling rate) / duty, with the cooling rate measured at night and the duty by day.")

    # ---- 3. single pulses
    print("\n[3] SINGLE PULSES (2-5 min open, >= 10 min closed before, >= 15 min after)")
    ps = r["pulses"]
    if len(ps) < 5:
        print("    NOT ENOUGH DATA: %d isolated pulses (need >= 5)." % len(ps))
    else:
        rise = np.array([p["rise"] for p in ps])
        print("    %d pulses: room rises by median %.2f C (IQR %.2f..%.2f), peak %.0f min after the valve opens" % (
            len(ps), np.median(rise), np.percentile(rise, 25), np.percentile(rise, 75), np.median([p["t_peak"] for p in ps])))
        print("    (a target to match in the simulator: it depends on C_air, UA_air_mass, the radiator and the sensor lag together)")

    # ---- 4. suggestions
    print("\n[4] SUGGESTED PLANT VALUES for tools/sim/thermal_sim.py")
    ua0 = plant["UA_mass_out"] + plant["UA_air_out"]
    tau0 = (plant["C_air_kJK"] + plant["C_mass_kJK"]) * 1000.0 / ua0 / 3600.0
    print("    current PLANT%s: (C_air + C_mass) / (UA_mass_out + UA_air_out) = %.0f h" % (
        "" if from_sim else " (simulator not importable, built-in defaults)", tau0))
    tau_ok = np.isfinite(r["tau"])
    k_ok = r["k_low_conf"] in ("good", "fair")
    if not tau_ok and not k_ok:
        print("    none: neither the cooling time constant nor the heating gain is trustworthy with this data.")
    if tau_ok and not k_ok:
        cm = r["tau"] * 3600.0 * ua0 / 1000.0 - plant["C_air_kJK"]
        print("    tau = %.0f h from %s." % (r["tau"], r["tau_src"]))
        print("    Only the ratio C/UA is identified. Keeping the UA guesses:")
        print("        C_mass_kJK = %.0f      (was %.0f)" % (round(cm, -2), plant["C_mass_kJK"]))
        print("    heating gain not trustworthy yet: UA values left alone.")
    if k_ok and not tau_ok:
        print("    heating gain is usable (%.4f C per billed minute) but there is no trustworthy time constant:" % r["k_low"])
        print("    C and UA cannot be separated. No values suggested. An AWAY period of 2+ days would fix this.")
    if tau_ok and k_ok:
        q_w = a.q_pulse_kwh_min * 3.6e6 / 60.0                      # W while the valve is open
        c_tot = q_w / (r["k_low"] / 60.0)                           # J/K
        ua = c_tot / (r["tau"] * 3600.0)
        f = ua / ua0
        print("    tau = %.0f h (%s); gain = %.4f C per billed minute (%s)." % (r["tau"], r["tau_src"], r["k_low"], r["k_low_conf"]))
        print("    ASSUMING the simulator's heat per billed minute in the pulse regime is right (%.2f kWh/min," % a.q_pulse_kwh_min)
        print("    --q-pulse-kwh-min; it follows from the T_supply/flow guesses and cannot be checked from these logs):")
        print("        C_mass_kJK  = %.0f      (was %.0f; C_air kept at %.0f)" % (
            round(c_tot / 1000.0 - plant["C_air_kJK"], -2), plant["C_mass_kJK"], plant["C_air_kJK"]))
        print("        UA_mass_out = %.0f      (was %.0f)   } both scaled by %.2f:" % (plant["UA_mass_out"] * f, plant["UA_mass_out"], f))
        print("        UA_air_out  = %.0f      (was %.0f)   } only their sum is identified" % (plant["UA_air_out"] * f, plant["UA_air_out"]))
        print("    If the real heat per billed minute is x times the assumed one, C and UA are x times these values.")
        print("    Check: run the simulator with these values and compare billed min/day with `summary`.")
    print("    NOT identifiable from these logs:")
    print("      - T_supply, flow_lpm, rad_water_l (fill time), rad_metal_kJK, rad_P50_W, rad_n, valve_delay_s:")
    print("        need the pipe node (supply/return temperature) or a heat meter")
    print("      - absolute C and UA (only C/UA and heat/C): no power measurement")
    print("      - UA_mass_out vs UA_air_out split, gains_W, and anything tied to weather: outdoor temperature is")
    print("        not logged (Tinf above = outdoor mean + gains/UA lumped together)")
    print("      - C_air and UA_air_mass separately: [3] gives one number to match by simulation")
    return 0


# ----------------------------------------------------------------------------- compare
def parse_days(spec):
    """'2026-11-21,2026-11-23..2026-11-25' -> set of ISO dates."""
    out = set()
    for part in (spec or "").replace(" ", ",").split(","):
        if not part:
            continue
        try:
            if ".." in part:
                d0, d1 = (dt.date.fromisoformat(x) for x in part.split("..", 1))
                while d0 <= d1:
                    out.add(d0.isoformat())
                    d0 += dt.timedelta(days=1)
            else:
                out.add(dt.date.fromisoformat(part).isoformat())
        except ValueError:
            raise SystemExit("error: bad date '%s' (use YYYY-MM-DD or YYYY-MM-DD..YYYY-MM-DD)" % part)
    return out


def comparable(s):
    """Reason why a day is not a fair A/B day, or None."""
    if not s["rows"]:
        return "no data"
    if s["gaps"] > 72:
        return "%d min missing" % s["gaps"]
    if s["away_min"]:
        return "AWAY/OFF %d min" % s["away_min"]
    if s["arrival_min"] or s["boost_min"]:
        return "ARRIVO/boost"
    if s["limp_min"] > 30:
        return "LIMP %d min" % s["limp_min"]
    if s["algo"] in ("MIXED", "-"):
        return "algorithm %s" % ("mixed" if s["algo"] == "MIXED" else "not visible (valve never opened)")
    return None


def group_stats(days):
    g = dict(n=len(days))
    if not days:
        return g
    b = np.array([s["valve_min"] for s in days], dtype=float)
    g["billed"], g["billed_sd"] = float(b.mean()), float(b.std(ddof=1)) if len(b) > 1 else math.nan
    g["pulses"] = float(np.mean([s["pulses"] for s in days]))
    nc = sum(s["n_comfort"] for s in days)
    g["rms"] = math.sqrt(sum(s["sse"] for s in days) / nc) if nc else math.nan
    g["in_band"] = 100.0 * sum(s["n_in_band"] for s in days) / nc if nc else math.nan
    g["t_mean"] = float(np.nanmean([s["t_mean"] for s in days]))
    g["sp_mean"] = float(np.nanmean([s["sp_mean"] for s in days]))
    g["t_max"] = float(np.nanmax([s["t_max"] for s in days]))
    g["t_min"] = float(np.nanmin([s["t_min"] for s in days]))
    h = [(s["valve_min"], hdd(s)) for s in days if not math.isnan(hdd(s)) and hdd(s) > 0]
    g["n_hdd"] = len(h)
    g["b_hdd"] = sum(x for x, _ in h) / sum(y for _, y in h) if h else math.nan
    g["t_out"] = float(np.mean([20.0 - y for _, y in h])) if h else math.nan
    g["b"] = b
    return g


def cmd_compare(a):
    logs = Logs(a.logdir)
    logs.require_data()
    outdoor = read_outdoor(a.outdoor)
    st = {s["date"]: s for s in all_day_stats(logs, outdoor)}
    manual = bool(a.a_days or a.b_days)
    excluded = []
    if manual:
        if not (a.a_days and a.b_days):
            raise SystemExit("error: give both --a-days and --b-days (or neither for the automatic split)")
        da, db = parse_days(a.a_days), parse_days(a.b_days)
        if da & db:
            raise SystemExit("error: days in both groups: %s" % ", ".join(sorted(da & db)))
        for d in sorted((da | db) - set(st)):
            excluded.append((d, "no log file"))
        A = [st[d] for d in sorted(da) if d in st and st[d]["rows"]]
        B = [st[d] for d in sorted(db) if d in st and st[d]["rows"]]
        la, lb = a.a_label, a.b_label
        how = "days given on the command line (no day excluded automatically)"
        warn = [(s["date"], comparable(s)) for s in A + B if comparable(s) and not comparable(s).startswith("algorithm")]
    else:
        A, B, warn = [], [], []
        for d in sorted(st):
            why = comparable(st[d])
            if why and not a.include_partial:
                excluded.append((d, why))
            elif st[d]["algo"] == "PULSE":
                A.append(st[d])
            elif st[d]["algo"] == "CLASSIC":
                B.append(st[d])
            else:
                excluded.append((d, why or "algorithm not visible"))
        la, lb = "PULSE", "CLASSIC"
        how = "automatic: CLASSIC = days with valve reason 3 (classic thermostat call), PULSE = reason 1/2"

    print("TermoMade A/B comparison: %s" % os.path.abspath(a.logdir))
    print("split: %s" % how)
    print("A = %s: %s" % (la, ", ".join(s["date"] for s in A) or "none"))
    print("B = %s: %s" % (lb, ", ".join(s["date"] for s in B) or "none"))
    if excluded:
        print("left out: " + "; ".join("%s (%s)" % e for e in excluded))
    for d, why in warn:
        print("warning: %s is not a clean day (%s)" % (d, why))
    if not A or not B:
        print("\nNOT ENOUGH DATA: need at least one day in each group." +
              ("" if manual else " No CLASSIC days found: set algo = 1 (classic) or 2 (alternate days) on the base,"
               " or name the days with --a-days/--b-days."))
        return 1
    ga, gb = group_stats(A), group_stats(B)
    have_out = ga["n_hdd"] and gb["n_hdd"]

    def row(name, key, fmt):
        return [name, fnum(ga.get(key, math.nan), fmt), fnum(gb.get(key, math.nan), fmt)]
    rows = [["days", ga["n"], gb["n"]],
            row("billed min/day", "billed", "%.0f"), row("  day-to-day sd", "billed_sd", "%.0f"),
            row("pulses/day", "pulses", "%.1f"), row("comfort RMS error C", "rms", "%.2f"),
            row("within +-0.3 C %", "in_band", "%.0f"), row("mean T C", "t_mean", "%.2f"),
            row("max T C", "t_max", "%.2f"), row("min T C", "t_min", "%.2f"), row("mean setpoint C", "sp_mean", "%.2f")]
    if have_out:
        rows += [row("mean outdoor C", "t_out", "%.1f"), row("billed min per HDD", "b_hdd", "%.1f")]
    print()
    print_table(rows, ["", "A " + la, "B " + lb])
    print()
    d = 100.0 * (ga["billed"] - gb["billed"]) / gb["billed"] if gb["billed"] else math.nan
    print("billed time, A vs B: %s %% (%+.0f min/day)" % (fnum(d, "%+.0f"), ga["billed"] - gb["billed"]))
    if have_out and gb["b_hdd"]:
        print("billed per HDD, A vs B: %+.0f %%  (weather-normalised, HDD base %.0f C)" % (
            100.0 * (ga["b_hdd"] - gb["b_hdd"]) / gb["b_hdd"], HDD_BASE_C))
    if ga["n"] >= 3 and gb["n"] >= 3:
        se = math.sqrt(ga["billed_sd"] ** 2 / ga["n"] + gb["billed_sd"] ** 2 / gb["n"])
        diff = ga["billed"] - gb["billed"]
        print("difference %.0f min/day, about +-%.0f (2 standard errors): %s" % (
            diff, 2 * se, "clear" if abs(diff) > 2 * se else "NOT clear yet, more days needed"))
    if min(ga["n"], gb["n"]) < 5:
        print("few days (%d vs %d): treat as a first indication. docs/09 asks for 2-4 weeks of alternate days." % (ga["n"], gb["n"]))
    if abs(ga["sp_mean"] - gb["sp_mean"]) > 0.3:
        print("warning: mean setpoints differ by %.2f C, the groups are not comparable as they are." % abs(ga["sp_mean"] - gb["sp_mean"]))
    if have_out:
        print("weather: outdoor means above. billed per HDD assumes billed time is proportional to (20 - T_out).")
    else:
        print("weather: NOT accounted for. The outdoor temperature is not logged, and the two groups saw different")
        print("days. Alternating days spreads the weather; for a normalised result give --outdoor date,t_mean.csv.")
    print("also: the building remembers the day before (thermal mass), so single alternate days blur the difference.")
    return 0


# ----------------------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description="TermoMade SD-card log analysis", epilog="details: tools/analysis/README.md")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("summary", help="per-day table and totals")
    p.add_argument("logdir")
    p.add_argument("--outdoor", help="CSV 'date,t_mean' with daily mean outdoor temperatures (adds HDD columns)")
    p.add_argument("--texts", type=int, default=5, help="show the last N text lines of events.csv (default 5)")
    p.set_defaults(fn=cmd_summary)
    p = sub.add_parser("plot", help="temperatures, setpoint, valve, demand, humidity -> PNG")
    p.add_argument("logdir")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--day", help="YYYY-MM-DD")
    g.add_argument("--last", type=int, help="the last N days (default 1)")
    p.add_argument("-o", "--out", required=True, help="output PNG")
    p.set_defaults(fn=cmd_plot)
    p = sub.add_parser("fit", help="identify a simple thermal model for the simulator")
    p.add_argument("logdir")
    p.add_argument("--outdoor", help="CSV 'date,t_mean' with daily mean outdoor temperatures")
    p.add_argument("--t-out", type=float, help="one outdoor temperature for the whole period (rough)")
    p.add_argument("--settle-min", type=int, default=90, help="minutes skipped after each valve close (default 90)")
    p.add_argument("--min-cool-min", type=int, default=120, help="shortest cooling stretch used (default 120)")
    p.add_argument("--window-min", type=int, default=120, help="window for the heating analysis (default 120)")
    p.add_argument("--q-pulse-kwh-min", type=float, default=Q_PULSE_KWH_MIN,
                   help="assumed heat per billed minute in the pulse regime (default %.2f, from the simulator)" % Q_PULSE_KWH_MIN)
    p.set_defaults(fn=cmd_fit)
    p = sub.add_parser("compare", help="pulse vs classic days: billed minutes and comfort")
    p.add_argument("logdir")
    p.add_argument("--a-days", help="days of group A, e.g. 2026-11-21,2026-11-23..2026-11-25")
    p.add_argument("--b-days", help="days of group B")
    p.add_argument("--a-label", default="A")
    p.add_argument("--b-label", default="B")
    p.add_argument("--outdoor", help="CSV 'date,t_mean' with daily mean outdoor temperatures (adds billed per HDD)")
    p.add_argument("--include-partial", action="store_true", help="automatic split: keep incomplete/unusual days")
    p.set_defaults(fn=cmd_compare)
    a = ap.parse_args(argv)
    if not a.cmd:
        ap.print_help()
        return 2
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
