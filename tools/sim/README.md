# tools/sim — thermal simulator

Design tool to check the pulse-control idea and choose default tuning. **Not firmware.**

```bash
python3 thermal_sim.py               # tables: winter / cold / mild / cold-arrival
python3 thermal_sim.py --plot out/   # + sim_steady.png, sim_cold_start.png, modulator.png
```

Needs Python 3.8+ (matplotlib only for `--plot`).

## Model

| Part | How |
|---|---|
| Building | Air node (2 MJ/K) ↔ mass node (30 MJ/K) ↔ outdoor; UA 2500 / 110 / 60 W/K; 200 W gains |
| Radiator circuit | 8 well-mixed segments in series (plug-flow approximation), 30 L water + 70 kJ/K metal, 8 kW at ΔT 50 K, n = 1.3 |
| Supply | 75 °C, 10 L/min when open → fill time 3 min |
| Valve | 30 s stroke delay, billed while open |
| Sensor | 120 s lag, σ 0.02 °C noise, 0.01 °C resolution, reports every 30 s |
| Outdoor | daily sinusoid (min at 03:00) |

All values are **guesses** in `PLANT` at the top of the script. Replace them with values fitted from
the real logs (Phase 4: `fit.py`).

## Controllers

- `Hysteresis` — classic thermostat (±0.2 °C)
- `TPI` — PI + fixed 20 min period, with min ON/OFF
- `TermoMade` = `PID` + `PulseModulator`: **reference implementation** of
  `docs/04-control-algorithm.md`. Firmware unit tests should reproduce its targets.

## Metrics

billed min/day, kWh per billed minute, compensated units (UNI/TR 11388-like: open time ×
(mean(supply, return) − room)/50), heat delivered, valve cycles, RMS error, max/min T, % time in
±0.3 °C, mean return temperature while open.
