# 10 — Roadmap & decisions

## Decisions

### Answered (2026-11-09)

| ID | Question | Answer → consequence |
|---|---|---|
| D1 | What does the old thermostat switch? | **230 V, 3 wires: common / open / close** → one SPDT relay: COM = L, NO = OPEN, NC = CLOSE |
| D2 | Valve actuator? | **Coster CDK 068**, motorised 3-point, ~30 s to open (aux switch at mid-stroke; 60 s full stroke) |
| D3 | Meter / billing? | **Yearly fuel split by valve hours × apartment size** → pure time, savings are real |
| D4 | Internet at the house? | **None** → AP only, RTC on the room node + phone time sync, local-only Android app |
| D7 | Rooms / sensors | **One big room + bathroom, one zone.** Second sensor **on the CYD**, fused with the room node |
| D10 | GitHub repo | **Phoenix1o1/ThermoMade** |
| — | Extra requirements | Night setback schedule, Android app for the parents, screen off after 30 s, 2-part OpenSCAD box on the 503 box with PSU inside |

### Still open

| ID | Question | Why it matters | Default if unanswered |
|---|---|---|---|
| **D13** | **Is neutral (N) present in the 503 box?** | The PSU needs it. The old thermostat probably only had L/open/close | Blocking for the box. If no N: bring one in (electrician) or power via USB from a socket |
| D5 | Exact CYD variant (1× micro-USB = ILI9341, 2 USB = ST7789)? | Display driver config | ILI9341 |
| D6 | On total sensor loss: LIMP or FAIL_OFF? | Freeze risk vs cost | LIMP |
| D8 | Condo plant hours? | Avoid billed minutes on cold water | 24 h |
| D9 | Toolchain: PlatformIO (pioarduino, core 3.x) or Arduino IDE? | Repo structure, tests | PlatformIO |
| D11 | Radiators: number, material, TRVs? | Model calibration | Aluminium, TRVs open |
| D12 | Antifreeze setpoint while away | Freeze protection vs cost | 7 °C |
| D14 | Is the CDK 068 aux switch wired to the condo counter? | Only informative. We never touch it | Assume yes |
| D15 | UI language for parents | CYD + app text | Italian |
| D16 | Night setback: how cool, and when? Under time billing deep setbacks **cost** hours ([04 §5](04-control-algorithm.md#5-recovery-cold-start-morning-boost)) | Comfort vs cost | Comfort 20 °C 06:30–22:30, Night 18.5 °C, efficient recovery |
| D17 | Parents' phones: Android version? | App needs Android 10+ | Android 10+ |

## Phases

### Phase 0 — Groundwork

- [x] Concept, control design, simulation, knowledge base
- [x] D1–D4, D7, D10 answered
- [ ] D13 (neutral) and site survey ([05](05-hardware.md#phase-0-site-survey-checklist)), photos into `hardware/wiring/`
- [ ] Last season's statement: hours → baseline
- [ ] Order parts ([BOM](05-hardware.md#bill-of-materials-mvp))

### Phase 1 — Bench spikes (de-risk)

- [ ] CYD: display + touch + **microSD together** (LovyanGFX + LVGL 9), backlight off/wake on touch IRQ
- [ ] CYD: DS18B20 on GPIO 27 (verify that pin isn't the backlight on this board), relay on GPIO 22
      stays off through boot/flash/reset
- [ ] Room node: SHT4x + DS3231 on one I²C bus, ESP-NOW to the CYD AP (fixed channel), RTC time in
      telemetry
- [ ] Self-heating test: CYD sensor in a mock-up chamber vs room node vs reference thermometer (24 h)
- [ ] Firmware image size (LVGL + web + ESP-NOW) vs the OTA partition plan
- [ ] Android: connect + bind to a no-internet ESP32 AP from the parents' phone model

### Phase 2 — MVP (control works in the house)

- [ ] `tm_protocol`, `tm_control` libraries + native unit tests (min-time fuzz test passes)
- [ ] Room node firmware v1 (telemetry, RTC, ACK, pairing, LED)
- [ ] Base: control task, valve FSM, PI + modulator, **sensor fusion + learned offset**, fallback/LIMP,
      NVS, watchdog, **time from RTC/phone**, **schedule (Comfort/Night) with efficient recovery**
- [ ] Base UI: Home screen (Italian) with **screen off after 30 s**, Diagnostics, serial CLI
- [ ] Logging: RAM buffer + SD CSV + events + daily summary
- [ ] Temporary enclosure (or v1 of the printed one) + safe mains wiring
- [ ] Install, run 1–2 weeks, then CLASSIC alternate days for the A/B comparison

**Done when:** MVP acceptance criteria in [01](01-requirements.md#acceptance-criteria-mvp) pass.

### Phase 3 — Comfort & family

- [ ] **Enclosure v1** in OpenSCAD per [13](13-enclosure.md) (can start right after the parts arrive)
- [ ] Web pages: simple (parents) + advanced; JSON API; CSV download; OTA
- [ ] **Android app v1** ([12](12-android-app.md)) + GitHub Actions APK build, installed on the parents' phones
- [ ] All CYD screens (Graph, Stats, Schedule, Sensors, Settings), buzzer
- [ ] Modes: Away until date, Boost, plant hours; fixed 1 h pre-heat

### Phase 4 — Extensions

- [ ] Bathroom node + COLDEST policy for mornings
- [ ] Outdoor node + HDD on site + feed-forward
- [ ] Learned optimum start (warm-up rate)
- [ ] Pipe node + adaptive fill time + plant-off detection
- [ ] `tools/sim/fit.py`: calibrate the model from logs, re-tune; decide the night setback with data
- [ ] ESP-NOW encryption, node OTA

### Phase 5 — Season review

- [ ] Season hours/HDD vs last season. PULSE vs CLASSIC report. Valve cycles

## Risks

| Risk | Mitigation |
|---|---|
| No neutral at the 503 box | D13 first. Alternatives: bring N, or USB power from a socket |
| 230 V in a DIY box | Mains parts in a closed pocket inside the 503 box, fuse, enclosed PSU, terminal block, ≥ 6 mm clearances, PETG/ASA (V-0 better). Electrician if unsure |
| Valve/actuator wear from 2–3× more cycles (each move = 60 s motor run) | Cycle counter, MAX_CYCLES_PER_HOUR, OFF_MIN ≥ 5 min |
| Night setback raising the bill | Efficient recovery + shallow default + measure both ways |
| Time lost after power cut | DS3231 on the room node + phone sync. Schedule falls back to Comfort if unknown |
| CYD sensor biased by box heat | Separate vented chamber, PSU in the wall pocket, learned offset, screen off |
| Parents stuck if electronics fail | Keep the old thermostat as a spare. Short Italian "what to do" card next to the box |
| Condo rules | Only the thermostat is replaced. Actuator, aux switch and meter untouched |
