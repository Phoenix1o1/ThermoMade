# 07 — User interface

## CYD touch screen (320×240, landscape, LVGL 9)

Resistive touch: big targets (≥ 48 px), no gestures that need multi-touch. Swipe left/right or
use the bottom tab bar to change screens. Backlight auto-dims from the LDR and drops to 10 % after
60 s idle. The first touch only wakes the screen and is never treated as a button press.

### Home

```
┌──────────────────────────────────────────────┐
│ AUTO·Comfort      ▂▄▆ Soggiorno 12s   10:42  │  ← mode, link bars + age, clock
│                                              │
│      20.3°            [ − ]  20.0°  [ + ]    │  ← control temp (big), setpoint ±0.1/0.5
│      RH 41%   ext −4.2°                      │
│                                              │
│  ▮ VALVE OPEN   closes in 1:42               │  ← state + countdown (legacy "nxt")
│  today 63 min billed · 14 pulses · u 0.18    │
│ [Home] [Graph] [Stats] [Sensors] [Settings]  │
└──────────────────────────────────────────────┘
```

- Holding −/+ repeats. The setpoint is saved to NVS **10 s after the last change** (flash wear).
- Mode chip → popup: Auto / Comfort / Eco / Away / Off / Boost 30-60-120 min.
- Valve line states: `OPEN closes in m:ss`, `LOCKOUT opens earliest in m:ss`,
  `WAITING next pulse in m:ss`, `IDLE no demand`, `CONTINUOUS`, `PLANT OFF`, `LIMP`.
- Alarm banner (red) for: sensor lost, limp mode, valve-sense mismatch, SD error, budget exceeded.

### Graph

24 h (also 6 h zoom): control temperature, setpoint (step line), valve-open bars, outdoor (if
present). Data from the RAM ring buffer (1 sample/min, 1440 points).

### Stats

Today / yesterday / 7-day bars: billed minutes, pulses, mean temp, RMS error. With an outdoor
node: billed minutes per degree-day vs baseline ([09](09-logging-evaluation.md)).

### Sensors

List of nodes: name, role, T, RH, battery, RSSI, last seen. Buttons: **Add sensor** (pairing
window), **Identify** (node LED blinks), rename, role, offset, remove.

### Settings

- Setpoints (Comfort / Eco / Away), weekly schedule (2–4 slots per day, copy day)
- Central-plant hours
- Display: brightness, dim timeout, night mode
- Network: AP name/password, home Wi-Fi join, time zone (Europe/Rome), manual time set
- Advanced (behind a long press): ON_MIN, OFF_MIN, KP, TI, TD, U_MIN, U_CONT, fallback policy,
  reset learned I
- **Diagnostics** (successor of the legacy LCD page 2): u, P, I, D, ON/OFF targets, FSM state,
  timers, cycles today, free heap, uptime, reset reason

## RGB LED (reuses the legacy color semantics)

| Color | Meaning |
|---|---|
| Blue breathing | below setpoint (heating demand) |
| Green breathing | within ±0.2 °C |
| Red breathing | above setpoint |
| Orange solid | valve open |
| Purple flashing | sensor lost / limp mode |
| Off | screen asleep at night (configurable) |

The LED is driven by the UI task only, with a single state, so nothing fights over the pixel
(legacy bug).

## Buzzer (GPIO 26)

Short beep on setpoint change (optional), triple beep on alarms (repeats every 10 min, can be
muted from the banner).

## Web UI

Served by the base on `http://termomade.local` (mDNS) or `http://192.168.4.1` in AP mode.
Single page app (HTML/JS/CSS gzipped in LittleFS), mobile-first, no external CDN (works offline).

| Page | Content |
|---|---|
| Status | Same as the Home screen + setpoint/mode controls |
| History | Interactive chart (uPlot, ~50 KB) of 24 h / 7 days from SD, CSV download per day |
| Sensors | Registry management, like the CYD screen |
| Settings | All settings, schedule editor, Advanced parameters |
| System | Firmware version, OTA upload, backup/restore settings (JSON), reboot, logs |

**Browser time sync:** every page load POSTs the phone's time (`Date.now()`) and time zone. In
AP-only mode that's how the base gets wall time.

### JSON API (draft)

| Method & path | Body / reply |
|---|---|
| `GET /api/status` | `{t, rh, sp, mode, valve, next_s, u, p, i, billed_today_s, pulses_today, nodes:[...] , alarms:[...]}` |
| `POST /api/setpoint` | `{sp}` (clamped 5–25 °C) |
| `POST /api/mode` | `{mode, boost_min?}` |
| `GET/POST /api/config` | full settings JSON (validated, versioned) |
| `GET /api/history?h=24` | compact arrays from the RAM buffer |
| `GET /api/log/2026-11-09.csv` | daily file from SD |
| `POST /api/time` | `{epoch_ms, tz}` |
| `POST /api/nodes/pair` / `PATCH /api/nodes/{mac}` | pairing and node edits |
| `POST /update` | OTA firmware upload |

All writes go through the same validated "request" path as the touch UI.

## Serial CLI (debug, 115200)

Successor of the legacy 4-digit setpoint input:
`sp 21.5`, `mode eco`, `status`, `params`, `set KP 0.8`, `nodes`, `log tail 20`,
`valve test` (one pulse, respects OFF_MIN), `reboot`.
