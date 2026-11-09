# 01 — Problem statement & requirements

## The situation

- Mountain house: **one big room + a bathroom**, heated together by one zone (one valve).
  **No internet on site.**
- Centralised condominium heating, **very hot** supply water.
- Zone valve actuator: **Coster CDK 068**, 230 V~, 3-point control (**common / open / close**),
  driven today by a thermostat with a changeover contact. Opening takes ~30 s (the
  auxiliary microswitch switches at mid-stroke; the datasheet gives a 60 s full 90° stroke).
- **Billing:** the condominium's yearly fuel cost is split by **valve-open time × apartment
  size**. Your share is proportional to your hours, so every billed minute saved is real money
  ([02](02-heating-physics.md)).
- The current on/off thermostat **overshoots and undershoots**. When it stops, the radiators are
  still full of hot water and keep heating. When it restarts, the room has already cooled
  below the setpoint.

## The idea

**Pulse the zone valve.** Open it just long enough to fill the circuit with hot water (≈ 3 min),
close it, and let the radiators release that heat while nothing is billed. Repeat as often as the
room needs. A PI controller sets the pulse frequency (and, only when needed, the pulse length)
from the temperature error.

Expected (simulation): **≈ 30 % fewer billed minutes**, RMS temperature error **0.5 → 0.13 °C**.

## System concept

| Node | Board | Role |
|---|---|---|
| **Base station** | ESP32-2432S028R "CYD" 2.8" touch, in a 2-part 3D-printed box screwed onto the **503 wall box** of the old thermostat | UI, control, SPDT relay to the valve, **second temperature sensor**, logging, Wi-Fi AP for the app |
| **Room sensor** | ESP32 DevKit + SHT4x + **DS3231 RTC** | Main room temperature, battery-backed clock for the system |
| **Android app** | Small native app (parents) | Set temperature/mode from the phone **inside the house** (local Wi-Fi, no internet) |
| *Future* bathroom node | ESP32 + T/RH | Monitoring, "coldest room" policy |
| *Future* outdoor node | ESP32-C3/C6, battery | Feed-forward, degree-day statistics |

> The request mentioned "ESP32-2432S08". This plan assumes the common **ESP32-2432S028R**. Check
> which variant you have ([05](05-hardware.md#cyd-variants)).

## Functional requirements

| ID | Requirement | Priority |
|---|---|---|
| F1 | Drive the 3-point valve with an **SPDT relay**: COM = L, NO → OPEN, NC → CLOSE. De-energised = valve driven closed | Must |
| F2 | **Minimum ON time 3 min** (circuit fill time) in automatic operation | Must |
| F3 | **Minimum OFF time 5 min** (valve closing + stabilisation). Never violated, not even by manual commands | Must |
| F4 | ON/OFF durations adjusted automatically from the distance to the target (PI + pulse modulator) | Must |
| F5 | Prefer short "fill" pulses (pulse-frequency modulation); lengthen ON only when OFF is at its minimum | Must |
| F6 | Room sensor is a separate ESP32 reporting over ESP-NOW | Must |
| F7 | **Second sensor on the CYD** (same big room): fused with the room node for a better estimate, and fallback if the node is lost | Must |
| F8 | **Time-of-day schedule**: Comfort by day, lower setpoint at night (weekly program) | Must |
| F9 | Reliable time **without internet**: battery RTC (on the room node) + time sync from the phone app | Must |
| F10 | Setpoint/mode adjustable on the CYD touch screen | Must |
| F11 | **Screen fully off after 30 s without touch** (dark at night). First touch only wakes it | Must |
| F12 | Sensor-loss handling with a fallback chain ending in "limp" mode (house must not freeze) | Must |
| F13 | Settings persist across power cuts (setpoint, mode, schedule, learned integrator, parameters) | Must |
| F14 | Log at 1-min resolution; daily billed-minutes counter; 24 h graph | Must |
| F15 | **Simple Android app for the parents**: current temp, +/−, Comfort/Night/Away, works on the house Wi-Fi AP without internet. Italian UI | Should |
| F16 | Modes: Auto (schedule), Comfort, Night, Away/Antifreeze, Off, Boost (timed) | Should |
| F17 | Web UI (same pages the app shows) with graphs, settings, CSV download | Should |
| F18 | **Enclosure: 2-part parametric OpenSCAD box** screwed to the 503 box, CYD in front, room inside for PSU, relay, terminals and the second sensor | Must |
| F19 | Multi-sensor support (bathroom, outdoor, pipe) without reflashing the base | Should |
| F20 | "Central plant hours": no pulses while the condo boiler is off | Could |
| F21 | Built-in "classic thermostat" controller for A/B comparison days | Should |
| F22 | OTA firmware update over the local AP (no need to open the box) | Should |
| F23 | Optimum start: start heating early so Comfort is reached at the scheduled time | Could |

## Non-functional requirements

- **Fail-safe:** relay de-energised (valve driven closed) during boot, flashing and crashes. Hardware
  pull-down on the relay input. Task watchdog on the control task.
- **Never fight the valve:** exactly one module (the valve FSM) owns the relay pin.
- **Fully offline:** no internet, ever, for any function. Phone access only inside the house.
- **Parents-proof:** the normal use needs no menus. The app and the Home screen show only
  temperature, +/− and three mode buttons. Advanced settings are hidden.
- **Mains safety:** 230 V parts (PSU, relay contacts, terminals) in a closed compartment inside the
  503 box cavity, separated from the low-voltage side.
- **Testable:** control logic is pure C++ with injected time, unit-tested on the PC and
  cross-checked against the Python simulator.
- **Accuracy:** control temperature within ±0.2 °C after calibration.
- **Valve wear:** count cycles (the CDK 068 motor runs ~60 s per move). Expect about 25–100
  cycles/day. Max cycles/hour configurable.

## Acceptance criteria (MVP)

1. Over a 7-day run: no ON shorter than ON_MIN and no OFF shorter than OFF_MIN in the logs (except
   user OFF and faults).
2. Room temperature within ±0.3 °C of the setpoint for 80 % or more of the time in steady weather.
3. Night setback and morning return happen at the programmed times, also after a power cut with
   no phone around (RTC).
4. Unplugging the room sensor → the CYD sensor takes over within 5 min (with its learned offset),
   alarm shown. Both lost → limp mode keeps heating.
5. Power cut → reboots with valve closed, then resumes with the same setpoint, mode, schedule and
   learned integrator.
6. The parents can change the temperature from the app without help.
7. Billed hours per heating degree-day lower than last season's (from the condominium statement),
   and lower than the CLASSIC-mode comparison days ([09](09-logging-evaluation.md)).
