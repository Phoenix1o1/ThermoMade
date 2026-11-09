# 11 — Review of the legacy sketch (`reference/legacy/Termostato.ino`)

Single ESP32-S3 (NeoPixel on GPIO 48), DHT22, relay on GPIO 5, 20×4 I²C LCD, Wi-Fi AP with a web
form, NVS settings, 24 h CSV log, and a time-proportioning "PID".

## What it actually did (control)

`updatePwmControl()` runs **every loop iteration (~1 ms)**, but it uses
`elapsed = now − pwmStartTime` (time since the last relay transition, i.e. minutes) as its `dt`
(L294–299):

- `integral += error * elapsed_s` is added thousands of times per second → the integral slams into
  its ±110 clamp in milliseconds and **flips sign almost instantly** whenever the error changes
  sign.
- I-term = Ki × 110 = **±330 s**. P-term = 10 s per °C. D ≈ 0 (`lastError` is updated every ms).
- `output < 0 → 0`, then `onTime = constrain(output, 3 min, 30 min)` and
  `offTime = 30 + 10 − onTime` minutes (L304–308).

Net behaviour: **a two-level duty switch**.

| Room | ON | OFF | Duty |
|---|---|---|---|
| Below setpoint | ≈ 5.6 min (330 s + P) | ≈ 34 min | **≈ 14 %** |
| Above setpoint | 3 min (forced minimum) | 37 min | **≈ 7.5 %**: still heats when too warm |
| Error > 2.5 °C | ≈ 6 min | 10 min (OFF_MIN) | ≈ 37 % |

So in cold weather (need > 14 %) the room settles **~2.5 °C below setpoint**, where the
"large error" shortcut kicks in. In mild weather it can't stop heating. The pulsing idea was
right. The PI implementation never really modulated.

## Bugs & issues

| # | Where | Issue | Fixed in new design by |
|---|---|---|---|
| 1 | L294–299 | Wrong `dt` (time since transition, called every ms) → saturated integral, no real PI | Fixed 30 s tick with real dt ([04 §2](04-control-algorithm.md#2-pid--demand-u)) |
| 2 | L304–308 | Minimum 3 min pulse every 37 min even when too warm (7.5 % forced duty) | `U_MIN` → no pulses ([04 §3](04-control-algorithm.md#3-pulse-modulator-u--on-target-off-target)) |
| 3 | L307–308 | ON grows first, OFF fixed by `ON_MAX+OFF_MIN−ON` → long open periods, poor heat per billed minute | Frequency-first modulator |
| 4 | L316, L325 | `forceEvaluate` toggles the relay **whatever the direction**: raising the setpoint while ON switches the valve OFF. Ignores min times (valve protection) | No forced toggles. OFF_MIN absolute, targets re-evaluated |
| 5 | L432–436 | Sensor failure calls `relayOff()` directly: FSM (`pwmOnState`, timers) and relay disagree afterwards. Fail-OFF can freeze an unattended house | Single relay owner. Fallback chain + LIMP |
| 6 | L298 | Integral clamp in arbitrary units (±110 °C·s) | `I` clamped to [0, 1] = duty, persisted |
| 7 | L299, L300 | Derivative on error → kick on setpoint change; dt wrong anyway | D on filtered measurement (default off) |
| 8 | L215–216 | No validation: setpoint 200 °C or brightness 0 accepted | Clamp 5–25 °C, validated request path |
| 9 | L317 vs L341 | `relayOn/Off` used before definition, which only compiles thanks to Arduino IDE auto-prototypes. Breaks in PlatformIO `.cpp` | Proper headers |
| 10 | L100, L222, L387 | Brightness applied twice (`setBrightness` + manual scaling); `setBrightness` is lossy | n/a (no NeoPixel). Single LED owner |
| 11 | L149–170, L453–471 | Two code paths write the same pixel every loop and fight; the `static` flash counter never resets, so the flash works only once after boot | One LED state machine in the UI task |
| 12 | L438–451 | `LED_GLOW_GREEN` branch unreachable (`delta > 0` / `< 0` cover everything but exactly 0) | ±0.2 °C band for green |
| 13 | L473 | LCD rewritten every loop over I²C (~1 kHz) | LVGL with dirty-area refresh |
| 14 | L196–209 | 14 KB `String` concatenation for the CSV. Timestamps are uptime, not wall time. Uninitialised rows exported as NaN | Streamed CSV from SD, wall time (NTP/browser) |
| 15 | L24–25 | AP password hard-coded in source | `secrets.h` (git-ignored) + settings |
| 16 | L39–42 | OFF_MIN was 10 min; the new requirement is 5 min | Parameter table |

## Worth keeping (ideas carried over)

- **Time-proportioning with min/max ON/OFF** → the pulse modulator.
- **"Large error" fast path** → PI saturation to continuous ON (no special case needed).
- **"next action in N s" countdown** on the display (L271–274) → Home screen valve line.
- **Diagnostics page** (ON/OFF times, PID output) → Diagnostics screen.
- **LED color semantics** (blue below, red above, purple sensor fault, orange heating) → RGB LED.
- **Wi-Fi AP + web form + CSV log download** → web UI + JSON API.
- **NVS persistence** of settings → plus learned `I`, counters, node registry.
- **Serial setpoint input** (`2150` → 21.50 °C) → serial CLI.
- **24 h ring-buffer log** → RAM buffer for the graph, SD for history.

## Dropped

DHT22 (→ SHT4x), NeoPixel flashing logic, 20×4 I²C LCD (→ CYD), the PID implementation,
`forceEvaluate`, the blocking `WebServer` (→ async).
