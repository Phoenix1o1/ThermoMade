# 09 — Logging & proving the savings

## What is logged

### Per minute (RAM ring buffer 24 h + daily CSV on microSD `/log/YYYY-MM-DD.csv`)

```
# TermoMade fw=0.3.0+abc1234 params: ON_MIN=180 OFF_MIN=300 KP=0.60 TI=7200 TD=0 ...
time,time_src,uptime_s,t_ctrl,t_room,t_base_raw,base_offset,rh_room,t_bath,t_out,sp,mode,ctrl,u,p,i,valve,fsm,on_tgt_s,off_tgt_s,billed_s_today,node_age_s,rssi
2026-11-09T10:42:00+01:00,phone,3600,20.31,20.28,21.42,1.08,41.2,,,20.00,AUTO,PULSE,0.18,-0.19,0.37,1,OPEN,180,820,3780,12,-61
```

- Empty fields for sensors that don't exist (yet).
- The header line repeats whenever parameters change.

### Events (`/log/events.csv`)

Valve transitions (with reason: `PULSE`, `CONT`, `USER_OFF`, `FAULT`, `PLANT_OFF`, `BOOST`),
mode changes, setpoint changes, node lost/found, alarms, boots (with reset reason).

### Daily summary (`/log/summary.csv`, one row per day)

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
