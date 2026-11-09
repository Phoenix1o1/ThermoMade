# 07 — User interface

Users: **Andrea** (setup, tuning, diagnostics) and **his parents** (daily use: see temperature,
warmer/colder, day/night/away). Parent-facing text is **Italian** (D15). Code, logs and Advanced
pages are in English.

## CYD touch screen (320×240, landscape, LVGL 9)

### Screen power

- **Backlight fully off after 30 s without touch** (configurable 10–300 s). Dark room at night.
- **The first touch only wakes the screen** and is never taken as a button press, so you can't
  change the setpoint by accident in the dark.
- Touch IRQ (GPIO 36) wakes the UI. LVGL rendering pauses while the screen is off.
- When on, brightness follows the LDR (dimmer in a dark room).
- No other light: the RGB LED is on the back and stays off. Alarms show on the next wake-up and
  optionally beep (day hours only).

### Home (the only screen the parents need)

```
┌──────────────────────────────────────────────┐
│ AUTO · Giorno        ▂▄▆ 12s         10:42   │  ← mode, link to room node, clock
│                                              │
│      20.3°           [ − ]  20.0°  [ + ]     │  ← room temp (big), setpoint ±0.5 °C
│      umidità 41%                             │
│                                              │
│  [ Giorno ]   [ Notte ]   [ Via ]            │  ← one-tap modes (Comfort/Night/Away)
│  ▮ Riscaldamento attivo · chiude tra 1:42    │  ← valve state + countdown
└──────────────────────────────────────────────┘
```

- −/+ steps 0.5 °C (0.1 °C in Advanced). It applies to the current period only until the next
  schedule change, like a typical programmable thermostat. Long-press = change the schedule
  temperature itself.
- Setpoint saved to NVS 10 s after the last touch (flash wear).
- Swipe left → Andrea's screens. A small gear icon (long press) opens them too.
- Valve line (Italian): `Riscaldamento attivo · chiude tra m:ss`, `In pausa · prossimo impulso
  tra m:ss`, `Pausa valvola · riapre tra m:ss` (OFF_MIN lockout), `Nessuna richiesta`,
  `Riscaldamento continuo`, `Modalità emergenza` (LIMP).
- Red banner for alarms: room sensor lost, emergency mode, clock not set, SD error.

### Andrea's screens

| Screen | Content |
|---|---|
| Graph | 24 h / 6 h: control temperature, room node, CYD sensor (corrected), setpoint, valve bars |
| Stats | Today / yesterday / 7 days: billed minutes, pulses, mean temp, RMS error; season total vs last season |
| Schedule | Weekly program: up to 4 slots/day, Comfort/Night per slot, copy day → week |
| Sensors | Nodes: name, role, T, RH, RSSI, last seen, offset; Add sensor (pairing), Identify |
| Settings | Setpoints, screen timeout, Wi-Fi AP name/password/channel, time/date (manual), language, recovery mode, plant hours |
| Advanced | ON_MIN, OFF_MIN, KP, TI, TD, I_BAND, U_MIN, U_CONT, W_ROOM, fallback policy, reset learned I/offset, CLASSIC test mode |
| Diagnostics | u, P, I, D, ON/OFF targets, FSM state, timers, learned CYD offset, cycles today, heap, uptime, reset reason, time source |

## Buzzer (GPIO 26, optional speaker)

Short beep on setpoint change (optional). Triple beep on alarms, repeated every 30 min, **only
between 08:00 and 21:00**, and it can be muted from the banner.

## Web UI

Served by the base on its AP: `http://192.168.4.1` or `http://termomade.local`.
HTML/JS/CSS gzipped in LittleFS, no external CDN (no internet on site).

| Path | Audience | Content |
|---|---|---|
| `/` (simple) | Parents (via the app) | Same as the CYD Home: big temp, +/−, Giorno/Notte/Via, state line. Italian, large touch targets |
| `/advanced` | Andrea | Status, History chart (uPlot) 24 h/7 d, Schedule editor, Sensors, Settings, Advanced, System |
| `/advanced/system` | Andrea | Firmware version, **OTA upload**, settings backup/restore (JSON), CSV downloads, reboot |

**Time sync:** every page load POSTs the phone's time and time zone. In this house that is the best
time source ([06](06-communication.md#time-distribution)).

### JSON API (draft)

| Method & path | Body / reply |
|---|---|
| `GET /api/status` | `{t, t_room, t_base, rh, sp, mode, period, valve, next_s, u, p, i, billed_today_s, pulses_today, time_src, alarms:[...]}` |
| `POST /api/setpoint` | `{sp, scope: "period"\|"schedule"}` (clamped 5–25 °C) |
| `POST /api/mode` | `{mode, boost_min?, until?}` (e.g. Away until a date) |
| `GET/POST /api/schedule` | weekly program |
| `GET/POST /api/config` | settings JSON (validated, versioned) |
| `GET /api/history?h=24` | compact arrays from the RAM buffer |
| `GET /api/log/2026-11-09.csv` | daily file from SD |
| `POST /api/time` | `{epoch_ms, tz}` |
| `POST /api/nodes/pair`, `PATCH /api/nodes/{mac}` | pairing and node edits |
| `POST /update` | OTA firmware upload |

All writes go through the same validated "request" path as the touch UI. A simple PIN protects
`/advanced` (the AP password protects everything).

## Serial CLI (debug, 115200)

Successor of the legacy 4-digit setpoint input:
`sp 21.5`, `mode night`, `status`, `params`, `set KP 0.8`, `nodes`, `time 2026-11-09T10:42`,
`log tail 20`, `valve test` (one pulse, respects OFF_MIN), `reboot`.
