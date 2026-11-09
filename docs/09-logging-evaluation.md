# 09 — Logging & proving the savings

## What is logged

### Per minute (RAM ring buffer 24 h + daily CSV on microSD `/log/YYYY-MM-DD.csv`)

```
# TermoMade fw=0.3.0+abc1234 params: ON_MIN=180 OFF_MIN=300 KP=0.60 TI=7200 TD=0 ...
time,uptime_s,t_ctrl,t_raw_room,rh_room,t_bath,t_out,t_supply,t_return,sp,mode,u,p,i,valve,fsm,on_tgt_s,off_tgt_s,billed_s_today,node_age_s,rssi
2026-11-09T10:42:00+01:00,3600,20.31,20.28,41.2,,,,,20.00,AUTO,0.18,-0.19,0.37,1,OPEN,180,820,3780,12,-61
```

- Empty fields for sensors that don't exist (yet).
- The header line repeats whenever parameters change.

### Events (`/log/events.csv`)

Valve transitions (with reason: `PULSE`, `CONT`, `USER_OFF`, `FAULT`, `PLANT_OFF`, `BOOST`),
mode changes, setpoint changes, node lost/found, alarms, boots (with reset reason).

### Daily summary (`/log/summary.csv`, one row per day)

`date, billed_min, pulses, cycles, mean_t, rms_err, max_over, min_under, t_out_mean, hdd, billed_per_hdd, controller (termomade|baseline), notes`

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

Without an outdoor node: take the outdoor temperature from a weather service when online (e.g.
the Open-Meteo API), or log the condo's degree-day data. Even then, compare only days with similar
weather.

## Baseline vs TermoMade (A/B protocol)

The changeover switch and the valve-sense input make a fair comparison possible:

1. **Shadow mode (≥ 1 week):** switch on **Old**. TermoMade runs, logs room T and **senses the
   valve**. Its controller computes what it *would* do (logged as `fsm_shadow`) without
   driving the relay. This is the **baseline**: billed minutes per HDD with the old thermostat.
2. **Alternate days (2–4 weeks):** switch on TermoMade and Old on alternate days (or 2-day
   blocks, because thermal mass carries over). Alternating spreads weather effects evenly
   between both.
3. **Compare** billed_min/HDD and comfort KPIs per controller. Also read the condominium meter
   display (if any) weekly and photograph it, so the logs can be cross-checked against what is
   actually billed.
4. Keep the old thermostat at the **same setpoint** as TermoMade during the test.

Expected from simulation: about −30 % billed minutes (pure time) and −40 % compensated units,
with RMS error down from ~0.5 °C to ~0.15 °C. Reality will differ. This protocol tells you by how
much.

## Using the logs to improve the model

`tools/sim` gets a `fit.py` (Phase 4) that reads the CSVs and fits the plant parameters (circuit
heat capacity, radiator power, building time constants) by matching the logged room temperature to
the logged valve state. Then re-tune KP/TI in simulation before changing the house.
