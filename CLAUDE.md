# CLAUDE.md — context for AI-assisted work on TermoMade

Read this first, then the doc that matches the task (index in `README.md`).

## What this is

DIY thermostat for Andrea's mountain house. Centralised condominium heating, very hot supply
water, **billed by zone-valve open time**. Strategy: **pulse the valve** (open ≈ fill time 3 min,
then closed while the radiators release the heat) with a PI controller choosing pulse frequency
first and pulse width only when needed.

- Base station: **ESP32-2432S028R "CYD"** (2.8" touch). UI, control, relay, logging, web.
- Room sensor: **ESP32 DevKit + SHT4x**, reports over **ESP-NOW** every 30 s.
- Future: outdoor (feed-forward, degree-days), bathroom (room policies), pipe node (fill time,
  plant-off detection).

## Status

Phase 0 (planning) complete as of 2026-11-09: docs + simulator. **No firmware yet.** The open
decisions D1–D12 are in `docs/10-roadmap.md`. Check whether they have been answered before
designing hardware-dependent code.

## Hard rules (do not break in code)

1. **OFF_MIN (default 300 s) is absolute.** Nothing may reopen the valve earlier: not boost, not
   manual commands, not setpoint changes, not a reboot (boot starts in lockout).
2. **ON_MIN (default 180 s)** applies to every automatic close. Only user OFF, faults and
   plant-off may close earlier.
3. **Only the valve FSM touches the relay pin** (GPIO 22). Everything else sends requests.
4. Relay de-energised = valve closed. Hardware pull-down; safe through boot/flash/crash.
5. Total sensor loss → **LIMP** (open-loop at learned duty, capped at 0.30) by default, not OFF.
   The house must not freeze.
6. Setpoints are clamped to 5–25 °C at every input path.
7. Control logic (`firmware/lib/tm_control`) is **pure C++ with injected time**: no Arduino
   includes, unit-tested natively, behaviour matching `tools/sim/thermal_sim.py`.

## Key parameters (source of truth: `docs/04-control-algorithm.md` §7)

ON_MIN 180 s · OFF_MIN 300 s · tick 30 s · EMA τ 120 s · KP 0.6/°C · TI 120 min · TD 0 ·
U_MIN 0.03 · U_CONT 0.90 · uc = ON_MIN/(ON_MIN+OFF_MIN) = 0.375 · STALE 300 s ·
I0 0.15 · MAX_CYCLES_PER_HOUR 8.

Modulator: u ≤ uc → ON = ON_MIN, OFF = ON_MIN·(1−u)/u; uc < u < U_CONT → OFF = OFF_MIN,
ON = OFF_MIN·u/(1−u); u ≥ U_CONT → continuous; u < U_MIN → no pulses.

## Toolchain (proposed, D9)

VS Code + PlatformIO, **pioarduino** platform (Arduino-ESP32 core 3.x, pinned release), LVGL 9 +
LovyanGFX, ESPAsyncWebServer (ESP32Async), ArduinoJson 7. ESP-NOW via IDF `esp_now.h`.
Note: the ESP-NOW **send callback signature changed in IDF 5.5** (`const esp_now_send_info_t*`).

## CYD pins in use

Display HSPI 12/13/14/15/2, BL 21 · Touch 25/32/39/33/36 · SD 18/19/23/5 · RGB 4/16/17
(active LOW) · LDR 34 · speaker 26 · **relay 22** · **1-Wire 27** (verify the backlight isn't on 27
for this board) · **valve-sense 35** (input-only).
Known risk: touch and SD both default to VSPI. Prove the combination first (Phase 1 spike).

## Andrea's preferences relevant here

- Enclosures/3D parts: **parametric OpenSCAD**, **no small radii/fillets** (hard to print).
- Hobby electronics background (ESP32, ESP8266, CYD considered before, Home Assistant planned
  for other projects, no MQTT broker yet).
- Wants a plan before code. Keep docs in sync when decisions change: update
  `docs/10-roadmap.md` decisions table and the parameter table in `docs/04`.

## Conventions

C++17, units in names (`_c`, `_s`, `_ms`), monotonic `uint32_t` ms with unsigned subtraction,
no `String` building in hot paths, no blocking delays outside `setup()`, secrets in git-ignored
`firmware/include/secrets.h`. Protocol structs only in `firmware/lib/tm_protocol` (shared by all
nodes). Frames ≤ 250 bytes.

## Where things are

- Legacy sketch and its review: `reference/legacy/Termostato.ino`, `docs/11-legacy-review.md`
- Simulator: `tools/sim/thermal_sim.py` (PID, PulseModulator, TermoMade classes = reference
  implementation)
- Plots/results: `docs/img/`
