# CLAUDE.md — context for AI-assisted work on TermoMade

Read this first, then the doc that matches the task (index in `README.md`).
GitHub: `Phoenix1o1/ThermoMade`. Local folder name: `TermoMade`.

## What this is

A DIY thermostat for Andrea's mountain house. One big room + bathroom, one heating zone,
centralised condominium heating with very hot supply water. **Billing: the yearly fuel cost is
split by valve-open hours × apartment size** (pure time). **No internet on site.**
Strategy: **pulse the zone valve**. Open ≈ fill time (3 min), then closed while the radiators
release the heat. A PI controller picks pulse frequency first and pulse width only when needed.

- Valve actuator: **Coster CDK 068**, 230 V~, 3-point (common/open/close), 60 s stroke, flow from
  ~30 s. Its aux microswitch probably feeds the condo meter. **Never touch it.**
- Base: **ESP32-2432S028 "CYD", 2-USB variant (USB-C + micro, ST7789 display)** in a 2-part printed box on the old thermostat's **503 box**.
  UI, control, SPDT relay, **local DS18B20** (fused with the room node, learned offset), logging,
  Wi-Fi **AP only**, web pages + JSON API.
- Room node: **ESP32 DevKit + SHT4x (or DHT22, build-selectable) + DS3231 RTC** (system clock
  after power cuts), ESP-NOW every 30 s. Humidity → dew point + "too dry / too humid" advice.
- Parents: **Android WebView shell app** (binds to the no-internet AP), Italian simple page
  served by the base.
- Future: bathroom node, outdoor node, pipe node.

## Status

Phase 0 planning done (2026-11-09). D1–D5, D7, D9, D10, D19, D20 answered (CYD = 2-USB variant,
ST7789; PlatformIO; room sensor SHT4x or DHT22; CYD sensor DS18B20).
Enclosure v1 exists: `hardware/enclosures/termomade_box.scad` + STLs (not yet test-printed).
**Firmware v0.1 written (2026-11-09), compiles, 42 native tests pass, NOT yet run on hardware.**
Bring-up: `firmware/README.md`. **Open: D13 (neutral in the 503 box?), D18 (relay module
active-HIGH/LOW → `TM_RELAY_ACTIVE_HIGH`), D21 (panel colours/touch cal)** and others in
`docs/10-roadmap.md`. Next: bench bring-up, then web server/JSON API, OTA, Android app (Phase 3).

## Hard rules (do not break in code)

1. **OFF_MIN (default 300 s) is absolute.** Nothing may reopen the valve earlier: not boost, not
   manual commands, not setpoint changes, not a reboot (boot starts in lockout).
2. **ON_MIN (default 180 s)** applies to every automatic close. Only user OFF, faults and
   plant-off may close earlier.
3. **Only the valve FSM touches the relay pin** (GPIO 22). Relay energised = L→OPEN,
   de-energised = L→CLOSE. Hardware pull-down; safe through boot/flash/crash.
4. Total sensor loss → **LIMP** (open-loop at learned duty, capped at 0.30) by default, not OFF.
5. Setpoints are clamped to 5–25 °C at every input path.
6. Control logic (`firmware/lib/tm_control`) is **pure C++ with injected time**: no Arduino
   includes, unit-tested natively, behaviour matching `tools/sim/thermal_sim.py`
   (`PID`, `PulseModulator`, `TermoMade`, `Schedule`).
7. Everything works offline. No feature may depend on internet.

## Key parameters (source of truth: `docs/04-control-algorithm.md` §7)

ON_MIN 180 s · OFF_MIN 300 s · tick 30 s · EMA τ 120 s · KP 0.6/°C · TI 120 min · TD 0 ·
I_BAND 1.0 °C (+ coast after setpoint decrease) · U_MIN 0.03 · U_CONT 0.90 · uc = 0.375 ·
STALE 300 s · I0 0.15 · MAX_CYCLES_PER_HOUR 8 · W_ROOM 0.7 · TAU_OFF 6 h ·
Comfort 20.0 / Night 18.5 / Away 7.0 °C · schedule Comfort 06:30–22:30 ·
recovery EFFICIENT (u ≤ uc) for scheduled rises, FAST for arrival/boost.

Modulator: u ≤ uc → ON = ON_MIN, OFF = ON_MIN·(1−u)/u; uc < u < U_CONT → OFF = OFF_MIN,
ON = OFF_MIN·u/(1−u); u ≥ U_CONT → continuous; u < U_MIN → no pulses.

Key finding: under time billing, **continuous ON is the expensive regime** (≈ 0.13 vs 0.47 kWh
per billed minute). Deep night setbacks increase billed time unless recovery is pulse-only
(docs/04 §5).

## CYD pins in use

Display HSPI 12/13/14/15/2, BL 21 (off after 30 s idle) · Touch 25/32/39/33/IRQ 36 (**bit-banged**,
so VSPI stays free for SD) · SD 18/19/23/5 · RGB 4/16/17 off (back side, invisible) · LDR 34 ·
speaker 26 driven LOW · **relay 22** · **DS18B20 27** (verify the backlight isn't on 27 for this
board) · 35 spare (input-only). All in `firmware/src/base/base_config.h`.

## Toolchain (D9: PlatformIO)

VS Code + PlatformIO, **pioarduino 55.03.312-1** (Arduino-ESP32 core 3.3.12 / IDF 5.5, pinned).
LovyanGFX drawn directly (no LVGL in the MVP), own XPT2046/SHT4x/DS3231 drivers,
OneWire/DallasTemperature, Adafruit DHT (node_dht22 only). Later: ESPAsyncWebServer (ESP32Async),
ArduinoJson 7. ESP-NOW via IDF `esp_now.h`; the **send callback signature changed in IDF 5.5**
(`const esp_now_send_info_t*`). Android: Kotlin WebView shell, APK built by GitHub Actions.

Commands (in `firmware/`): `pio test -e native` · `pio run -e base_cyd` · `pio run -e node_sht4x`
· `pio run -e node_dht22`. After changing the simulator or the control spec, regenerate
`test/test_golden/golden_data.h` with `python3 tools/sim/gen_golden.py` (not `-I`).

## Firmware map

- `lib/tm_control` (`namespace tmc`): `Controller` = whole thermostat (params, schedule, fusion,
  PID, modulator, valve FSM, humidity, counters, events). Pure, unit-tested, golden-matched.
- `lib/tm_protocol` (`tmp`): ESP-NOW frames v1. `lib/tm_util` (`tmu`): CRC, decoders, dates.
- `src/base`: control task owns Controller + Settings + relay; UI/console/radio talk to it only
  through `shared.h` (request queue, schedule mailbox, Snapshot).
- `src/node`: room node (SHT4x | DHT22 + DS3231).

## Andrea's preferences relevant here

- Enclosures/3D parts: **parametric OpenSCAD** with live preview, **no small radii/fillets**
  (chamfers or large radii). Spec: `docs/13-enclosure.md`.
- Parent-facing UI in Italian. Docs and code in English.
- Plan before code. Keep docs in sync when decisions change: decisions table in
  `docs/10-roadmap.md`, parameter table in `docs/04`.

## Conventions

C++17, units in names (`_c`, `_s`, `_ms`), monotonic `uint32_t` ms with unsigned subtraction,
no `String` building in hot paths, no blocking delays outside `setup()`, secrets in git-ignored
`firmware/include/secrets.h`. Protocol structs only in `firmware/lib/tm_protocol`, frames ≤ 250 B.

## Where things are

- Legacy sketch + review: `reference/legacy/Termostato.ino`, `docs/11-legacy-review.md`
- Simulator: `tools/sim/thermal_sim.py`; results/plots: `docs/img/`
- Android app plan: `docs/12-android-app.md`; enclosure: `docs/13-enclosure.md`,
  `hardware/enclosures/termomade_box.scad` (CYD geometry from the community STEP model, see doc 13)
