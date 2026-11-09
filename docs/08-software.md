# 08 — Software stack & repository layout

Status (2026-11-09): **base firmware v0.2, room node v0.1**, compiling, 46 native tests passing.
Not yet run on hardware. Bring-up steps: [`firmware/README.md`](../firmware/README.md).
v0.2 adds the ARRIVO button, the hidden settings pages and LDR light sensing.

## Toolchain (decision D9: PlatformIO)

- **VS Code + PlatformIO**, one monorepo under `firmware/`, several build environments.
  Code shared between firmwares lives in `firmware/lib/`.
- **pioarduino** platform, pinned: `55.03.312-1` = Arduino-ESP32 core **3.3.12 / ESP-IDF 5.5**.
  The official PlatformIO `espressif32` platform stays on core 2.x, so it is not used.
- All library versions are pinned in `platformio.ini`.

| Env | Target | Command |
|---|---|---|
| `base_cyd` | CYD base station | `pio run -e base_cyd -t upload` |
| `node_sht4x` | Room node with SHT40/41/45 (+ DS3231) | `pio run -e node_sht4x -t upload` |
| `node_dht22` | Room node with DHT22/AM2302 (+ DS3231) | `pio run -e node_dht22 -t upload` |
| `native` | PC unit tests (no hardware) | `pio test -e native` |

Build size: base v0.2 1.20 MB of the 1.75 MB OTA slot (66 %), RAM 17 %. Node 0.94 MB (72 % of
the default 1.25 MB app partition).

## Libraries

| Purpose | Library (pinned) | Notes |
|---|---|---|
| Display | **LovyanGFX 1.2.34** | ST7789 (2-USB CYD) or ILI9341, selected by build flag. **Drawn directly, no LVGL in the MVP**: one Home screen + one Diagnostics screen don't justify LVGL's RAM and flash. LVGL 9 stays an option for Phase 3 screens |
| Touch | **own bit-banged XPT2046 driver** (`src/base/touch.cpp`) | Keeps VSPI free for the microSD card. Resolves the "touch and SD both on VSPI" risk without a spike |
| 1-Wire | OneWire 2.3.8 + DallasTemperature 4.0.6 | CYD DS18B20, non-blocking (request, read 800 ms later) |
| SHT4x | **own driver** (`src/node/sensors.cpp`) | 40 lines, CRC-checked, I²C bus recovery. No extra dependency |
| DHT22 | Adafruit DHT 1.4.7 (+ Unified Sensor 1.1.15) | Only in `node_dht22` |
| DS3231 | **own driver** | Uses the OSF flag as "time valid". RTClib not needed |
| ESP-NOW | ESP-IDF `esp_now.h` | Small wrappers in `radio_base.cpp` / `node/radio.cpp` |
| Storage | Preferences (NVS), SD (logs) | Built into the core |
| Time | `settimeofday()` + POSIX TZ `CET-1CEST,M3.5.0,M10.5.0/3` | No NTP (no internet) |
| Web server, JSON | ESPAsyncWebServer, ArduinoJson 7 | **Phase 3** (not in v0.1) |
| Charts (web) | uPlot | Phase 3 |

## Repository layout (actual)

```
firmware/
├── platformio.ini            envs: base_cyd, node_sht4x, node_dht22, native
├── partitions_base.csv       4 MB: nvs, otadata, 2 × 1.75 MB app (OTA-ready), 384 KB spiffs, coredump
├── include/secrets.h.example copy to secrets.h (git-ignored): AP password
├── lib/
│   ├── tm_control/           pure C++17, no Arduino: params, PID, modulator, valve FSM, fusion,
│   │                         schedule, humidity advice, Controller (the whole thermostat)
│   ├── tm_protocol/          ESP-NOW frames v1 (packed structs, validate, helpers)
│   └── tm_util/              CRC-8, SHT4x decode, DS3231 BCD, civil date <-> days
├── src/
│   ├── base/                 main.cpp (tasks), relay.h, shared.*, settings.*, timekeeper.*,
│   │                         temp_local.* (DS18B20), radio_base.*, touch.*, ui.* (Home),
│   │                         ui_menu.* (hidden pages), light.* (LDR), logger.*, cli.*,
│   │                         param_table.h (names, steps), base_config.h (pins, timings)
│   └── node/                 main.cpp, sensors.* (SHT4x | DHT22, DS3231), radio.*, node_config.h
└── test/
    ├── test_control/         34 tests: modulator, FSM (+ fuzz), PID, fusion, schedule,
    │                         humidity, controller, ARRIVO (+ 20-day fuzz of the hard rules)
    ├── test_golden/          C++ controller vs Python simulator, 6 scenarios (2 ARRIVO), 23 040 ticks
    ├── test_protocol/        frame sizes, round trip, validation
    └── test_util/            CRC, SHT4x/DS3231 decode, dates
```

`tools/sim/gen_golden.py` regenerates `test/test_golden/golden_data.h` from the simulator. Run
it (without `python -I`) whenever the simulator or the control spec changes. The golden test
proves the firmware does exactly what the simulator predicts.

`tools/ui_preview/build.sh` compiles the real `ui.cpp` + `ui_menu.cpp` on the PC against a small
software stand-in for LovyanGFX (real font data), drives them with scripted touches and writes
the screens to `docs/img/ui/`. Use it after any UI change: it caught an overlapping label, a
colour that didn't refresh and a text at the screen edge before any hardware existed.

## Base firmware task model (FreeRTOS)

| Task | Core / prio | Period | Owns |
|---|---|---|---|
| `control` | 1 / 3 | 1 s (`vTaskDelayUntil`) | The `tmc::Controller`, the `Settings`, **the relay pin**, NVS writes, the Snapshot. **Task watchdog 10 s** (panic → reboot → relay released → boot lockout) |
| `loop()` (Arduino) | 1 / 1 | ~15 ms | Display, touch, serial console. **Loop watchdog** on too |
| `radio` | 0 / 4 | event-driven | ESP-NOW: pairing, telemetry → room samples, immediate ACK built from the Snapshot, node RTC → timekeeper |
| `logger` | 0 / 1 | queue | microSD writes. Never blocks the control task (queue, no wait, drop when full) |

Data flow:

- **Requests** (`shared.h`, a FreeRTOS queue): UI / console / (later) web → control task. Mode,
  setpoint, boost, time, parameter, screen, touch calibration, save, reset learning, reboot,
  factory reset. The control task validates every one.
- **Schedule mailbox**: a whole week at once (console now, web later). Last one wins.
- **Snapshot** (mutex-protected copy, published every second): status, settings, time, link
  info. Everyone else reads only this.
- **Room samples** queue: radio → control.

Boot order (`setup()`):

1. `Relay::begin()`: output written inactive **before** it is enabled (no glitch).
2. Serial, settings + learned state from NVS (CRC-checked blobs, defaults if corrupt).
3. Timekeeper (time kept across software resets in RTC memory).
4. Display + **colour test** (red | green | blue, 1.5 s), DS18B20, Wi-Fi AP + ESP-NOW, SD logger.
5. `Controller::begin(..., start_unlocked=false)`: the valve may open only after OFF_MIN
   (hard rule 1), whatever the reset reason.
6. Watchdog, radio task, control task, loop watchdog.

Persistence (NVS, namespace `termomade`):

| Blob | Content | When written |
|---|---|---|
| `cfg` | params, schedule, mode, touch calibration, screen timeout/brightness, net id, AP channel | 10 s after the last change (debounced), on `save`, before `reboot` |
| `ps` | learned duty per band (comfort / antifreeze), CYD sensor offset + learning time | every 30 min, on `save`, `reboot`, `reset-learning` |
| `tmnodes` | paired nodes (MAC, role) | on pairing / forget |

Each blob carries magic, version, size and CRC-32. A mismatch loads the defaults instead of a
half-valid structure. **Bump `kVersion` in `settings.cpp` when the layout of `Settings` changes.**

## Room node firmware

`loop()` with the loop watchdog. Every 30 s (the base can change it): read the sensor (SHT4x:
CRC + 3 tries + I²C bus recovery; DHT22: 2 tries), read the DS3231, send telemetry with up to 3
retries, wait 300 ms for the ACK. 3 failed reports in a row → channel scan. Unpaired → HELLO
sweep over channels 1–13 every 3 s; the base answers only during its 60 s pairing window. BOOT
held 3 s = forget pairing. The ACK carries the base's time: if it came from the phone/manual
source and differs by more than 2 s, the node sets its DS3231.

## Conventions

- C++17. Units in names (`_c`, `_s`, `_ms`). Monotonic `uint32_t` ms with unsigned subtraction
  (rollover-safe); the controller extends it to a 64-bit uptime internally.
- No `String` in hot paths. `snprintf` into fixed buffers.
- No blocking delays except in `setup()`, `restart()` and the 15 ms yield in `loop()`.
- Parameters: one struct (`tm_params.h`) with defaults, one `sanitize()` with the limits, one
  name table (`param_table.h`) for the console and later the web API.
- `secrets.h` is git-ignored; `secrets.h.example` is committed. Without it the AP uses a default
  password and the base prints a warning.

## Known gotchas (collect more here)

1. CYD: touch and SD both default to VSPI → **solved**: touch is bit-banged on its own pins.
2. CYD: ILI9341 vs ST7789 variant, backlight on 21 (or 27 on some boards), RGB order and colour
   inversion. → build flags `TM_LCD_ST7789`, `TM_LCD_BGR`, `TM_LCD_INVERT`, `TM_LCD_ROTATION`
   and the boot colour test.
3. GPIO 35 is input-only with no pull resistors.
4. ESP-NOW send-callback signature changed in IDF 5.5 (`const esp_now_send_info_t*`). Handled with
   an `ESP_IDF_VERSION` check.
5. ESP-NOW channel must match the AP channel (fixed: 1). Peers on the base use `WIFI_IF_AP`.
6. Unity silently skips double assertions unless `UNITY_INCLUDE_DOUBLE` is defined (set in the
   `native` env).
7. `namespace tm` collides with C's `struct tm`: the control library uses `tmc`.
8. Self-heating of the CYD sensor: offset learned only while the screen has been off for 5 min.
9. DS3231 "ZS-042" modules try to charge the coin cell. Remove the diode/resistor with a CR2032.
10. Android talking to a no-internet AP: bind to the network or traffic goes to mobile data
    ([12](12-android-app.md)).
11. The first `pio run` downloads the pinned platform and toolchain (~600 MB). Allow time.
