# 07 — User interface

Users: **Andrea** (setup, tuning, diagnostics) and **his parents** (daily use: see temperature,
warmer/colder, day/night/away). Parent-facing text is **Italian** (D15). Code, logs and Advanced
pages are in English.

## CYD touch screen (320×240, landscape)

v0.1 draws directly with LovyanGFX (Home + Diagnostics). The other screens below are Phase 3,
possibly with LVGL.

### Screen power

- **Backlight fully off after 30 s without touch** (configurable 10–300 s). Dark room at night.
- **The first touch only wakes the screen** and is never taken as a button press, so you can't
  change the setpoint by accident in the dark.
- Touch IRQ (GPIO 36) wakes the UI. Drawing pauses and the panel sleeps while the screen is off.
- Brightness: fixed (console `screen <timeout> <brightness>`). LDR-scaled brightness: later.
- A press needs two consecutive touch samples (~30 ms): single-sample blips are ignored.
- No other light: the RGB LED is on the back and stays off. Alarms show on the next wake-up and
  optionally beep (day hours only).

### Home (the only screen the parents need)

As implemented in v0.1 (`src/base/ui.cpp`):

```
┌──────────────────────────────────────────────┐
│ AUTO - Giorno                         10:42  │  ← mode (+ "(pre)" preheat, "*" override), clock
│                       Impostata              │     long-press the clock 2 s → Diagnostics
│  20.3°               20.0°                   │  ← room temp (big, orange while heating), setpoint
│                     [ − ]   [ + ]            │  ← ±0.5 °C, auto-repeat when held
│  UR 41%                                      │  ← humidity (blue)
│ Riscaldamento: chiude tra 1:42               │  ← valve state + countdown
│ Aria secca: umidificare                      │  ← advice, or red alarm banner
│ [ AUTO ] [ GIORNO ] [ NOTTE ] [  VIA  ]      │  ← modes (active one highlighted)
└──────────────────────────────────────────────┘
```

- −/+ steps 0.5 °C, shown immediately (optimistic), confirmed by the controller within 1 s.
  In AUTO it is a temporary override until the next schedule change; in GIORNO/NOTTE/VIA it
  changes that mode's temperature (saved 10 s after the last change). VIA is capped at 12 °C.
  Disabled in OFF.
- OFF (SPENTO) is on the Diagnostics screen only, so it can't be pressed by accident.
- Valve line: `Riscaldamento: chiude tra m:ss`, `Riscaldamento continuo`,
  `In pausa: prossimo impulso tra m:ss`, `Pausa valvola: riapre tra m:ss` (OFF_MIN lockout),
  `Nessuna richiesta di calore`, `Avvio: attendo i sensori...`, `Riscaldamento spento`.
- Red banner (highest first): `EMERGENZA: nessun sensore valido` (LIMP),
  `Sensore stanza non risponde`, `Sensori in disaccordo`, `Ora non impostata`,
  `Valvola aperta da molto tempo`, `Superato il budget giornaliero`. Otherwise the humidity advice
  `Aria secca: umidificare` / `Aria umida: arieggiare`.
- No accented letters on screen (the built-in fonts are 7-bit).

### Diagnostics (v0.1)

Long-press the clock for 2 s. Shows firmware, uptime, relay, SD, fused/room/CYD temperatures
and the learned offset, sp/u/P/I, recovery and coast state, valve reason, next action, lockout,
minutes and pulses today, alarms, room node age and RSSI, time source, algorithm. Buttons:
**ACCOPPIA** (60 s pairing window), **BOOST 60'**, **SPENTO** (OFF), **INDIETRO**.

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

## Serial console (115200 baud, implemented in v0.1)

Type `help`. Every change goes through the same validated request path as the touch screen.

| Command | Effect |
|---|---|
| `status` (or `s`) | Everything: time, mode, temperatures, PI terms, valve, counters, alarms, link |
| `params`, `get <name>`, `set <name> <value>` | Read / change any parameter of [04 §7](04-control-algorithm.md) (clamped, saved after 10 s) |
| `mode auto\|comfort\|night\|away\|off` | Also Italian: `giorno`, `notte`, `via`, `spento` |
| `sp <degC>` | Setpoint (5–25 °C, as the +/− buttons) |
| `boost <min>\|off` | Boost 1–240 min. Never shortens the 5 min valve pause |
| `time YYYY-MM-DD HH:MM[:SS]` | Set the clock (local time, Europe/Rome with DST). Best time source; the node RTC follows it |
| `sched`, `sched default`, `sched <all\|wk\|we\|mon..sun> HH:MM=C HH:MM=N ...` | Show / edit the weekly program (max 4 slots per day) |
| `pair`, `forget-nodes` | 60 s pairing window / forget all nodes |
| `touch on\|off`, `cal xmin xmax ymin ymax swap invx invy` | Raw touch dump, touch calibration |
| `screen <timeout_s> [brightness]` | Screen timeout 10–255 s, brightness 10–255 |
| `reset-learning duty\|offset\|all`, `save`, `reboot`, `factory YES` | Maintenance |
