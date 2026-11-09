# 01 — Problem statement & requirements

## The situation

- Mountain house, **centralised heating** (condominium plant). The apartment has its own
  **zone valve** opened by a room thermostat.
- The water arriving from the plant is **very hot**.
- Heating is **billed by the time the valve is open** (time-of-flow / "tempi di inserzione"
  accounting), not by measured energy. See [02-heating-physics](02-heating-physics.md) for why
  that matters.
- The current on/off thermostat **overshoots and undershoots**: when it stops, the radiators are
  still full of hot water and keep heating past the setpoint; when it restarts, the radiators are
  cold and the room has already dropped below the setpoint.

## The idea

**Pulse the zone valve.** Open it just long enough to fill the circuit with hot water (≈ 3 min),
close it, and let the radiators give that heat to the room while nothing is billed. Repeat as
often as the room needs. Pulse frequency (and, only when needed, pulse length) is set
automatically by a PI controller from the room-temperature error.

Two wins expected:

1. **Lower bill** — more heat extracted per billed minute (simulation: about 30 % fewer billed
   minutes for the same comfort; see [02](02-heating-physics.md)).
2. **Stable temperature** — small heat quanta instead of long on/off swings
   (simulation: RMS error 0.5 °C → 0.13 °C).

## Hardware concept (as requested)

| Node | Board | Role |
|---|---|---|
| **Base station** | ESP32-2432S028R "Cheap Yellow Display" (CYD), 2.8" 320×240 touch | UI and adjustment, display, control logic, relay driving the zone valve, logging, web UI |
| **Room sensor** | ESP32 DevKit (WROOM-32) + temperature/humidity sensor | Measures the room temperature, sends it to the base wirelessly |
| *Future* outdoor sensor | small ESP32 (C3/C6), battery | Outdoor temperature → weather feed-forward, degree-day statistics |
| *Future* bathroom sensor | ESP32 + T/RH sensor | Monitoring, "coldest room" policy, shower detection |
| *Optional* pipe sensor | ESP32 + 2× DS18B20 clamp-on | Measures fill time, detects central plant off, estimates heat per pulse |

> Note: the request said "ESP32-2432S08"; this plan assumes the common **ESP32-2432S028R**.
> Check which variant you have (one micro-USB = ILI9341; two USB ports = ST7789). See
> [05-hardware](05-hardware.md#cyd-variants).

## Functional requirements

| ID | Requirement | Priority |
|---|---|---|
| F1 | Control the zone valve with a relay that replaces the existing thermostat contact | Must |
| F2 | **Minimum ON time 3 min** (circuit fill time) in automatic operation | Must |
| F3 | **Minimum OFF time 5 min** (valve closing + stabilisation, protects the valve). Never violated, not even by manual commands | Must |
| F4 | ON and OFF durations adjusted automatically from the distance to the target (PI + pulse modulator) | Must |
| F5 | Prefer short "fill" pulses (pulse-frequency modulation); lengthen ON only when OFF is already at its minimum | Must |
| F6 | Room sensor is a separate ESP32 that reports over ESP-NOW | Must |
| F7 | Setpoint and mode adjustable on the CYD touch screen | Must |
| F8 | Display: room temp, setpoint, humidity, valve state, countdown to next action, link status | Must |
| F9 | Sensor-loss handling with a fallback chain ending in "limp" mode (house must not freeze) | Must |
| F10 | Settings persist across power cuts (setpoint, mode, learned integrator, parameters) | Must |
| F11 | Log at 1-min resolution; daily billed-minutes counter; 24 h graph on screen | Must |
| F12 | Modes: Auto (schedule), Comfort, Eco, Away/Antifreeze, Off, Boost (timed) | Should |
| F13 | Web UI (phone) for status, settings, graphs, CSV download | Should |
| F14 | Multi-sensor support: outdoor, bathroom, pipe nodes, added without reflashing the base | Should |
| F15 | Weekly schedule (comfort/eco times) | Should |
| F16 | "Central plant hours": no pulses while the condo boiler is off | Should |
| F17 | Daily billed-minutes budget alarm | Could |
| F18 | Remote access / pre-heat before arrival (only if the house has internet) | Could |
| F19 | Home Assistant / MQTT integration | Could |
| F20 | OTA firmware update for the base (and the nodes later) | Could |

## Non-functional requirements

- **Fail-safe:** relay de-energised (valve closed) during boot, flashing and crashes; hardware
  pull-down on the relay input; task watchdog on the control task.
- **Never fight the valve:** exactly one module (the valve FSM) owns the relay pin.
- **Works offline:** no internet needed for control. Internet only adds features.
- **Bypass:** a manual changeover switch can hand control back to the old thermostat (useful for
  failures and for the baseline measurement).
- **Testable:** the control logic is pure C++ with injected time, unit-tested on the PC and
  cross-checked against the Python simulator.
- **Accuracy:** room sensor within ±0.2 °C after calibration; resolution 0.01 °C on the wire.
- **Valve wear:** count cycles; expect about 25–100 cycles/day depending on the weather. Max
  cycles/hour is configurable.

## Acceptance criteria (MVP)

1. Over a 7-day run: no ON shorter than ON_MIN and no OFF shorter than OFF_MIN in the logs (except
   user OFF and faults).
2. Room temperature within ±0.3 °C of the setpoint for 80 % or more of the time in steady weather.
3. Unplugging the room sensor → fallback within 5 min, alarm shown, house keeps heating in limp
   mode.
4. Power cut → reboots with valve closed, then resumes with the same setpoint, mode and learned
   integrator.
5. Billed minutes per heating degree-day measurably lower than the baseline week(s) with the old
   thermostat ([09](09-logging-evaluation.md)).
