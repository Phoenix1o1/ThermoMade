# 04 — Control algorithm (specification)

The firmware must implement exactly this. The Python reference implementation lives in
`tools/sim/thermal_sim.py` (`PID`, `PulseModulator`, `TermoMade`), so firmware and simulator
can be checked against each other.

## Layers

```mermaid
flowchart LR
  T["Sensor readings"] --> M["Measurement<br/>plausibility + EMA filter"]
  SP["Setpoint<br/>(mode / schedule)"] --> PI
  M --> PI["PI(D)<br/>u ∈ [0,1]"]
  FF["Feed-forward<br/>(outdoor, future)"] -.-> PI
  PI --> MOD["Pulse modulator<br/>u → ON/OFF targets"]
  MOD --> FSM["Valve FSM<br/>enforces ON_MIN / OFF_MIN"]
  G["Gates: plant hours,<br/>mode OFF, faults"] --> FSM
  FSM --> RELAY["Relay"]
```

`u` is the **demand**: the fraction of time the valve should be open (duty), from 0 to 1.

## 1. Measurement

- Telemetry arrives every 30 s with resolution 0.01 °C.
- **Plausibility:** reject T < 0 °C or > 40 °C, or |ΔT| > 2 °C between consecutive frames.
  Three rejections in a row = node failure.
- **Calibration offset** per node (set in UI; compare against a reference thermometer).
- **EMA filter** with τ_f = 120 s on the control temperature:
  `Tf += dt/(τ_f+dt) · (T − Tf)`.
- The control temperature is built from room-type nodes per policy ([03](03-architecture.md#control-temperature--room-policies)).

## 2. PI(D) → demand u

Runs every `TS` = 30 s (fixed tick, real `dt` from a monotonic clock).

```
e  = SP − Tf
P  = Kp · e
D  = −Kp · Td · dTf/dt            (derivative on measurement, low-pass τ = Td/4; Td = 0 by default)
u_unsat = P + I + D
if not (u_unsat ≥ 1 and e > 0):   # anti-windup: stop integrating only when saturated high
    I += Kp · dt / Ti · e
I  = clamp(I, 0, 1)
u  = clamp(P + I + D, 0, 1)
```

- **`I` has a physical meaning:** the duty needed in steady state (the "learned load"). It is
  shown on the diagnostics screen and **persisted to NVS every 30 min** and on mode changes, so a
  power cut does not throw the learning away.
- **Setpoint change:** no derivative kick (D is on the measurement). `I` is kept, so the change is
  bumpless.
- **Mode `AWAY` ↔ `COMFORT`:** keep one `I` per setpoint band (≤ 12 °C vs > 12 °C). The antifreeze
  load is very different from the comfort load.
- **Integration gated off** when: central plant off, valve forced closed (mode OFF, fault), LIMP
  mode, or BOOST.

## 3. Pulse modulator: u → (ON target, OFF target)

```
uc = ON_MIN / (ON_MIN + OFF_MIN)               # 3/(3+5) = 0.375

u <  U_MIN   (0.03)  → no pulses              (ON —,      OFF ∞)
u ≤  uc              → pulse-FREQUENCY         (ON = ON_MIN, OFF = ON_MIN·(1−u)/u)
u <  U_CONT  (0.90)  → pulse-WIDTH             (ON = OFF_MIN·u/(1−u), OFF = OFF_MIN)
u ≥  U_CONT          → continuous              (ON ∞,      OFF = OFF_MIN)
```

![modulator](img/modulator.png)

| u | ON | OFF | Period | Pulses/h |
|---|---|---|---|---|
| 0.05 | 3.0 | 57.0 | 60.0 | 1.0 |
| 0.10 | 3.0 | 27.0 | 30.0 | 2.0 |
| 0.15 | 3.0 | 17.0 | 20.0 | 3.0 |
| 0.20 | 3.0 | 12.0 | 15.0 | 4.0 |
| 0.30 | 3.0 | 7.0 | 10.0 | 6.0 |
| 0.375 | 3.0 | 5.0 | 8.0 | 7.5 |
| 0.50 | 5.0 | 5.0 | 10.0 | 6.0 |
| 0.70 | 11.7 | 5.0 | 16.7 | 3.6 |
| 0.80 | 20.0 | 5.0 | 25.0 | 2.4 |
| ≥ 0.90 | continuous | — | — | — |

(minutes; with ON_MIN = 3, OFF_MIN = 5)

Why this shape: a fill minute buys 3–4× more heat than a keep-open minute ([02](02-heating-physics.md#2-hot-water-in-the-radiators-is-stored-energy-youve-already-paid-for)).
So the modulator **varies frequency first** and stretches ON only when OFF has reached its minimum.

## 4. Valve FSM (the only owner of the relay)

```mermaid
stateDiagram-v2
  [*] --> CLOSED_LOCKOUT : boot (valve closed)
  CLOSED_LOCKOUT --> CLOSED : t_off ≥ OFF_MIN
  CLOSED --> OPEN : t_off ≥ OFF_target and u ≥ U_MIN and gates OK
  OPEN --> CLOSED_LOCKOUT : t_on ≥ max(ON_MIN, ON_target)
  OPEN --> CLOSED_LOCKOUT : user OFF / fault / plant-off (immediate)
  CLOSED_LOCKOUT : valve closed, OFF_MIN not yet elapsed (no command can open it)
  CLOSED : valve closed, waiting for OFF_target
  OPEN : valve open (billed)
```

Rules:

1. Targets are **re-evaluated every tick** from the current `u`. If the room cools faster, the OFF
   wait shortens (but never below OFF_MIN). If it warms up during a pulse, the pulse still
   completes ON_MIN (the circuit is filled; stopping early saves almost nothing and adds a cycle).
2. **OFF_MIN is absolute.** BOOST, manual ON and setpoint changes all wait for the lockout to
   finish (UI shows "opens in 2:10"). The lockout also applies right after boot, because the
   valve's state before the reboot is unknown.
3. **ON_MIN** applies to every automatic close. Only user OFF, faults and plant-off close earlier.
4. `MAX_CYCLES_PER_HOUR` (default 8) is an extra wear guard: if reached, the next OFF is
   stretched.
5. Every transition is logged with timestamp, reason, `u`, P, I, targets.
6. **Billed-time counter:** accumulate valve-open seconds (from the command, or from the
   valve-sense input if fitted). Daily total persisted at midnight and every 10 min.

Fixes against the legacy sketch: no `forceEvaluate` that toggles regardless of direction, no
forced minimum duty when the room is too warm, and FSM and relay can never disagree
([11](11-legacy-review.md)).

## 5. Cold start / boost

No special mode is needed for a cold house: with Kp = 0.6, an error ≥ 1.5 °C gives P ≥ 0.9, so
the valve stays continuously open. As the error shrinks, `u` drops below U_CONT and
pulse-width, then pulse-frequency, take over. Simulated overshoot after an 8 → 20 °C arrival:
0.43 °C (vs 0.73 °C for a classic thermostat).

The **BOOST** button is a user override (timed continuous ON) and does not touch `I`.

## 6. Fallback / LIMP

When no valid control temperature exists ([03](03-architecture.md#failure-modes--reactions)):
`u = clamp(I_learned, 0.05, 0.30)`, or 0.15 if `I` was never learned. The modulator and FSM run
as usual. Alarm on screen, LED purple, short beep every 10 min. `FAIL_OFF` policy is available
for summer or if you prefer. Default = LIMP, because an unattended mountain house must not freeze.

## 7. Parameters (defaults)

| Name | Default | Range | Notes |
|---|---|---|---|
| `ON_MIN` | 180 s | 60–600 | Circuit fill time. Measure it (Phase 0) |
| `OFF_MIN` | 300 s | 180–1800 | Valve close + stabilisation |
| `TS` | 30 s | — | Controller tick |
| `TAU_F` | 120 s | 30–600 | Measurement EMA |
| `KP` | 0.6 /°C | 0.2–1.5 | Duty per °C of error |
| `TI` | 120 min | 30–300 | Integral time |
| `TD` | 0 | 0–20 min | Off by default (sim: no benefit) |
| `U_MIN` | 0.03 | 0–0.1 | Below this: no pulses |
| `U_CONT` | 0.90 | 0.8–1.0 | Above this: continuous |
| `I0` | 0.15 | 0–1 | Initial learned duty (first boot only) |
| `STALE_S` | 300 s | 120–900 | Node considered lost |
| `LIMP_U` | learned I clamped 0.05–0.30 | — | Fallback demand |
| `MAX_CYCLES_PER_HOUR` | 8 | 4–12 | Wear guard |
| `SP_COMFORT` / `SP_ECO` / `SP_AWAY` | 20.0 / 17.5 / 7.0 °C | 5–25 | Setpoints are always clamped to 5–25 °C |
| `CONT_WARN_S` | 4 h | — | Continuous-ON warning |
| `BUDGET_MIN_DAY` | 0 (off) | — | Daily billed-minutes alarm |

All parameters can be edited from the web UI "Advanced" page (and the CYD diagnostics screen),
are stored in NVS, and are logged in the CSV header when they change.

## 8. Tuning procedure (on site)

1. **Measure the fill time** (Phase 0): touch the far radiator's return pipe, or better, log a
   DS18B20 on it. Time from valve command to the moment the return gets hot = `ON_MIN`.
2. Start with the defaults. Run 3 days.
3. Read the diagnostics: if `I` keeps drifting for days, Ti is fine. If the temperature
   oscillates with a period of hours, increase `TI` (150–180 min). If recovery after setpoint
   steps is slow, increase `KP` (0.8).
4. If the room ripples more than ±0.3 °C in mild weather: that is the fill-quantum limit. Consider
   a shorter `ON_MIN` on mild days (future "partial fill").
5. Feed the logs into the simulator (`tools/sim`) to calibrate the plant model, then re-tune there
   first.

## 9. Planned extensions

- **Outdoor feed-forward** (needs outdoor node): `u = u_ff + PI`, with
  `u_ff = K_ff · max(0, SP − T_out)`. `K_ff` is learned slowly as EMA of `I/(SP − T_out)` during
  stable periods. Faster reaction to weather changes, smaller `I` swings.
- **Adaptive fill time** (needs pipe node): end each pulse when the return pipe temperature rises
  by more than 10 K over its pre-pulse value (hot front arrived), bounded to
  [ON_MIN_LOW, ON_MIN_HIGH]. Learn the median as `ON_MIN`.
- **Plant-off detection** (pipe node): supply < 35 °C after 60 s open → close, pause pulses for
  15 min, freeze `I`.
- **Overshoot learning** after continuous-ON periods: lower `U_CONT` exit margin if overshoot
  repeats.
- **Pre-heat before arrival** (internet): schedule an `AWAY → COMFORT` switch early enough, using a
  learned warm-up rate (°C/h vs ΔT outdoor).
