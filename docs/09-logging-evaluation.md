# 09 — Logging & proving the savings

## What is logged

### Per minute (daily CSV on microSD `/log/YYYY-MM-DD.csv`, implemented in v0.1)

```
unix,local,uptime_s,t_ctrl,src,t_room,rh,t_base,t_base_corr,offset,sp,mode,period,boost,u,p,i,valve,reason,billed_today_s,pulses_today,alarms,limp,humidity,dew_c,rssi,node_age_s,arrival,light_pct
1794256920,2026-11-09 21:42:00,3600,20.31,1,20.28,41.2,21.42,20.34,1.080,20.00,0,0,0,0.1800,-0.1900,0.3700,1,1,3780,9,0,0,1,6.9,-61,12,0,37
```

- `src`: 0 none, 1 fused, 2 room only, 3 CYD only. `mode`: 0 AUTO, 1 COMFORT, 2 NIGHT, 3 AWAY,
  4 OFF. `period`: 0 Comfort, 1 Night. `reason`: `tmc::ValveReason` (1 pulse, 2 continuous,
  4 boost, 5 limp, 6 target reached, 7 no demand, 12 arrival, ...). `humidity`: 1 ok, 2 too dry,
  3 too humid. `arrival`: 1 while ARRIVO's forced hour runs. `light_pct`: room light 0–100 %
  from the CYD's LDR (empty until its scale is learned).
- Empty fields for sensors that are missing or invalid.
- Without a valid clock the lines go to `/log/notime.csv` (the `uptime_s` column still orders them).
- Each line is appended and the file closed again: a power cut loses at most one line.
- When the card is more than 90 % full the oldest daily files are deleted (checked once a day).
  A year of logs is ~60 MB.
- Planned: a RAM ring buffer of 24 h for the web charts, a header line with the parameters.

### Events (`/log/events.csv`, implemented in v0.1)

`unix,local,uptime_s,event,a,b,x,y` with events `valve_open`/`valve_close` (a = reason,
x = u, y = temperature), `mode` (a = new, b = previous), `setpoint` (x = value), `source`,
`arrival` (a = 1 forced hour starts, 0 ends; x = temperature),
`limp_on`/`limp_off`, `alarm_on`/`alarm_off` (a = bit), `day` (x = billed minutes, y = pulses of
the day that ended), `boot`, and free `text` lines (boot reason, time set, schedule changed,
learning reset, restart).

Note: the daily counters survive a reboot on the same day (saved every 30 min with the learned
state, so up to 30 min can be missing after a power cut). The SD files are the source of truth
for totals.

### Daily summary (`/log/summary.csv`, one row per day) — planned

`date, billed_min, pulses, cycles, mean_t, rms_err, max_over, min_under, t_out_mean, hdd, billed_per_hdd, controller (PULSE|CLASSIC), night_sp, notes`

## KPIs

| KPI | Definition | Why |
|---|---|---|
| **Billed minutes/day** | Σ valve-open time | What you pay (pure-time meters) |
| **Billed minutes per HDD** | billed_min / HDD, HDD = max(0, 20 − T_out,day-mean) | Weather-normalised comparison (Italian degree-days use a 20 °C base) |
| Comfort RMS | √mean((T − SP)²) over occupied time | Stability |
| In-band % | time with abs(T − SP) ≤ 0.3 °C | Stability, easy to read |
| Overshoot | max(T − SP) after each cold start / setpoint rise | The original complaint |
| Valve cycles/day | count of OPEN transitions | Wear |
| Heat per pulse (pipe node) | ≈ C_circuit × (T_supply − T_return,start) | Pulse efficiency, model calibration |

Without an outdoor node, outdoor temperatures for HDD come later, at home: download the
season's daily means for the town from a weather archive (e.g. Open-Meteo historical API or the
regional ARPA station) and join them to the daily summary. The house itself stays offline.

## Baseline vs TermoMade

The old thermostat is removed when the box goes on the 503, so comparisons work like this:

1. **Last season (free baseline):** the condominium statement lists your valve hours for the
   season. Divide by the season's HDD (archive data) → **hours per HDD, old thermostat**.
   Note whether a night setback was in use: deep setbacks cost hours too
   ([04 §5](04-control-algorithm.md#5-recovery-cold-start-morning-boost)).
2. **CLASSIC days (same hardware, same season):** TermoMade has a built-in `CLASSIC` controller
   (±0.2 °C on/off, exactly the simulator's `hyst`). Run it on **alternate days** (or 2-day
   blocks, because of the thermal mass) for 2–4 weeks, with the same setpoints and schedule. Alternating
   spreads the weather between both controllers.
3. **Compare** billed min/HDD (and comfort KPIs) PULSE vs CLASSIC, and the season total vs last
   season.
4. Read the condominium hour counter (if it has a display) weekly and photograph it, so the logs
   can be cross-checked against what is billed.

Expected from simulation: about −30 % billed minutes with RMS error from ~0.5 °C down to
~0.15 °C. Reality will differ. This tells you by how much.

## Using the logs to improve the model

`tools/sim` gets a `fit.py` (Phase 4) that reads the CSVs and fits the plant parameters (circuit
heat capacity, radiator power, building time constants) by matching the logged room temperature to
the logged valve state. Then re-tune KP/TI in simulation before changing the house.

## Analysis tool

`tools/analysis/tm_logs.py` (summary, plot, fit, compare) reads these files on a PC; see
`tools/analysis/README.md`. Tested on simulated logs only so far.
