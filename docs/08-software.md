# 08 — Software stack & repository layout

## Toolchain (proposed, decision D9)

- **VS Code + PlatformIO**, one monorepo, several build environments (base, sensor, later
  outdoor/pipe). Code shared between firmwares (protocol, control) lives in `lib/`.
- **Arduino-ESP32 core 3.x** via the **pioarduino** platform. The official PlatformIO
  `espressif32` platform stays on Arduino core 2.x. Pin an exact release in `platformio.ini`,
  for example:
  `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip`
  (core 3.3.9 / ESP-IDF 5.5.4 at the time of writing; check for newer and pin one).
- The Arduino IDE stays possible for the sensor sketch, but the shared libraries and native
  unit tests make PlatformIO the better fit.

## Libraries

| Purpose | Library | Notes |
|---|---|---|
| Display driver | **LovyanGFX** | Handles ILI9341/ST7789 CYD variants through a config class. Touch driver included. (TFT_eSPI is the alternative, but it needs `User_Setup` edits) |
| GUI | **LVGL 9.x** | `lv_conf.h` in the project. Draw buffer 320×24 px ×2 (internal RAM; no PSRAM on the CYD) |
| UI design (optional) | EEZ Studio (free) or SquareLine | Or hand-written. 5 screens is manageable by hand |
| Web server | **ESPAsyncWebServer + AsyncTCP** (ESP32Async org) | Non-blocking, unlike the legacy `WebServer` |
| JSON | **ArduinoJson 7** | Config, API |
| T/RH sensor | Sensirion `arduino-i2c-sht4x` (or Adafruit SHT4x) | |
| 1-Wire | OneWire + DallasTemperature | Optional DS18B20s |
| Storage | Preferences (NVS), LittleFS (web app), SD (logs) | Built into the core |
| ESP-NOW | ESP-IDF `esp_now.h` behind our own small wrapper | Fewer surprises than higher-level wrappers |
| Time | ESP-IDF SNTP + POSIX TZ `CET-1CEST,M3.5.0,M10.5.0/3` | Europe/Rome |
| Charts (web) | uPlot | Tiny, served gzipped from LittleFS |

## Repository layout (target)

```
TermoMade/
├── README.md, CLAUDE.md
├── docs/                      ← this plan and knowledge base
├── reference/legacy/          ← old Termostato.ino (read-only)
├── tools/sim/                 ← Python thermal simulator (design tool)
├── hardware/
│   ├── enclosures/            ← OpenSCAD parametric models (no small radii)
│   ├── wiring/                ← wiring diagrams, photos from the site survey
│   └── datasheets/            ← links / PDFs (CYD, SHT4x, relay, valve actuator)
└── firmware/
    ├── platformio.ini         ← envs: base_cyd, sensor_devkit, native (tests)
    ├── include/               ← secrets.h.example (Wi-Fi), build config
    ├── lib/
    │   ├── tm_protocol/       ← frame structs, encode/decode, constants (shared by all nodes)
    │   ├── tm_control/        ← PID, PulseModulator, ValveFsm, Measurement — pure C++, no Arduino
    │   └── tm_common/         ← logging macros, time utils, ring buffer
    ├── src/
    │   ├── base/              ← main.cpp, app/ (state, modes, schedule), ui/ (LVGL screens),
    │   │                        net/ (espnow, wifi, web, mqtt), storage/ (nvs, sd log), hw/ (relay, led, buzzer, ldr)
    │   └── sensor/            ← main.cpp, sensor drivers, espnow client
    └── test/
        ├── test_modulator/    ← targets vs table in docs/04, monotonicity
        ├── test_valve_fsm/    ← min times never violated (random command fuzzing), boot lockout
        ├── test_pid/          ← anti-windup, clamp, bumpless setpoint change
        └── test_protocol/     ← sizes, round-trip, unknown kinds skipped
```

`build_src_filter` selects `src/base` or `src/sensor` per environment. `pio test -e native` runs
the control tests on the PC in seconds.

## Base firmware task model (FreeRTOS)

| Task | Core | Period | Owns |
|---|---|---|---|
| `control` | 1 | 1 s (tick 30 s for PI) | Valve FSM, relay pin, PI state, node registry, billed counter. **Task watchdog enabled** |
| `ui` (Arduino loop) | 1 | ~5 ms `lv_timer_handler()` | LVGL, LED, buzzer |
| `net` | 0 | event-driven | Wi-Fi, web server (async), NTP, MQTT |
| `logger` | 0 | 60 s | RAM ring buffer, SD writes (buffered, one write/min) |
| Wi-Fi task (system) | 0 | — | ESP-NOW callbacks → queue to `control` |

Shared state: one `SystemState` snapshot struct guarded by a mutex (copied out by readers).
Commands flow the other way through a `RequestQueue` (setpoint, mode, boost, config change),
validated by `control`.

## Conventions

- C++17. Units in names (`temp_c`, `timeout_s`, `period_ms`). Internal time is a monotonic
  `uint32_t` ms with unsigned subtraction (rollover-safe), or `esp_timer_get_time()` µs.
- No `String` concatenation in hot paths. Stream CSV/JSON instead (the legacy sketch built a
  14 KB `String`).
- No blocking delays except in `setup()`. Every peripheral is non-blocking or in its own task.
- Every parameter has a name, default, min, max and unit in **one table** (`tm_control/params.h`),
  used for NVS, web validation and docs.
- `secrets.h` is git-ignored. `secrets.h.example` is committed.
- Version string `MAJOR.MINOR.PATCH+gitsha` shown on screen and in the CSV header.

## Known gotchas (collect more here)

1. CYD: touch and SD both want VSPI. Prove a working combination in the Phase 1 spike.
2. CYD: ILI9341 vs ST7789 variant, backlight on 21 (or 27 on some boards), LED G/B swap.
3. GPIO 35 is input-only with no pull resistors.
4. ESP-NOW send-callback signature changed in IDF 5.5 ([06](06-communication.md#api-gotchas-arduino-esp32-core-3x--esp-idf-5x)).
5. ESP-NOW channel must match the AP/STA channel. Turn off modem sleep on the base.
6. `Adafruit_NeoPixel::setBrightness()` is lossy. Irrelevant now, but a reminder from the legacy
   code not to double-scale colors.
7. NVS: write settings debounced (10 s), `I` every 30 min, counters every 10 min.
8. Self-heating of sensor nodes (see [05](05-hardware.md#room-sensor-node-esp32-devkit)).
9. LVGL 9 API differs from LVGL 8 (display/driver creation, `lv_display_*`). Don't mix example
   code from v8.
