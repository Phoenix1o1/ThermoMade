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
| D10 | GitHub repo | **Phoenix1o1/ThermoMade**, local folder linked as a git clone |
| D5 | CYD variant | **2-USB board (USB-C + micro-USB) → ST7789 driver** (RGB order / inversion confirmed on the boot colour test) |
| D9 | Toolchain | **PlatformIO + pioarduino** (core 3.3.12 / IDF 5.5), native unit tests |
| D19 | Room sensor | **Both, selectable at build time:** SHT4x (`node_sht4x`, recommended) or DHT22 (`node_dht22`). DHT11 not supported (1 °C steps) |
| D20 | CYD local sensor | **DS18B20** on GPIO 27 (1-Wire) |
| D22 | Arrival at the cold house | **ARRIVO button: AUTO + valve forced open 60 min (stops within 2 °C of the setpoint), then efficient pulse recovery.** Chosen from `tools/sim/arrival_study.py` ([04 §5.1](04-control-algorithm.md#51-arrivo-arriving-at-the-cold-house-decided-2026-11-09-with-toolssimarrival_studypy)). Continuous heating until warm would cost ~8.5 billed hours more per arrival |
| D24 | Light sensing | **CYD's own LDR** (GPIO 34), sampled with the screen dark, self-learning scale → auto-brightness + log. No extra sensor (a BH1750 on the room node only if calibrated lux is ever needed) |
| — | Extra requirements | Night setback schedule, Android app for the parents, screen off after 30 s, 2-part OpenSCAD box on the 503 box with PSU inside |

### Still open

| ID | Question | Why it matters | Default if unanswered |
|---|---|---|---|
| **D13** | **Is neutral (N) present in the 503 box?** | The PSU needs it. The old thermostat probably only had L/open/close | Blocking for the box. If no N: bring one in (electrician) or power via USB from a socket |
| D6 | On total sensor loss: LIMP or FAIL_OFF? | Freeze risk vs cost | LIMP |
| D8 | Condo plant hours? | Avoid billed minutes on cold water | 24 h |
| D11 | Radiators: number, material, TRVs? | Model calibration | Aluminium, TRVs open |
| D12 | Antifreeze setpoint while away | Freeze protection vs cost | 7 °C |
| D14 | Is the CDK 068 aux switch wired to the condo counter? | Only informative. We never touch it | Assume yes |
| D15 | UI language for parents | CYD + app text | Italian |
| D16 | Night setback: how cool, and when? Under time billing deep setbacks **cost** hours ([04 §5](04-control-algorithm.md#5-recovery-cold-start-morning-boost)) | Comfort vs cost | Comfort 20 °C 06:30–22:30, Night 18.5 °C, efficient recovery |
| D17 | Parents' phones: Android version? | App needs Android 10+ | Android 10+ |
| **D18** | **Relay module input: active-HIGH or active-LOW?** | Build flag `TM_RELAY_ACTIVE_HIGH` and the pull resistor must match the module ([05](05-hardware.md#relay-module-input-which-kind-decision-d18-open)) | Active-HIGH + 10 kΩ pull-down (flag = 1, as committed) |
| D21 | Touch calibration / panel colours of this CYD | Defaults are typical values | Check on first boot (colour test, `touch on`, `cal`) |
| **D23** | **Planned return ("VIA fino al …")?** Heat 18 h before arriving: arrive at ~18 °C for ~+1.8 billed hours, vs cold arrival (ARRIVO) or ~6.4 h less than heating flat out on arrival ([04 §5.1](04-control-algorithm.md#51-arrivo-arriving-at-the-cold-house-decided-2026-11-09-with-toolssimarrival_studypy)) | Comfort on arrival vs a little cost; return date must be set before leaving (no internet) | Not built; ARRIVO only |
| D25 | LDR hole in the front of the box | Light sensing needs it (`ldr_hole`, position to verify on the real board) | Off until the position is checked |

## Phases

### Phase 0 — Groundwork

- [x] Concept, control design, simulation, knowledge base
- [x] D1–D4, D7, D10 answered
- [ ] D13 (neutral) and site survey ([05](05-hardware.md#phase-0-site-survey-checklist)), photos into `hardware/wiring/`
- [ ] Last season's statement: hours → baseline
- [ ] Order parts ([BOM](05-hardware.md#bill-of-materials-mvp))

### Phase 1 — Bench spikes (de-risk)

- [ ] CYD: display + touch + **microSD together** (solved on paper: touch bit-banged; verify on the board), backlight off/wake on touch IRQ
- [ ] CYD: DS18B20 on GPIO 27 (verify that pin isn't the backlight on this board), relay on GPIO 22
      stays off through boot/flash/reset
- [ ] Room node: SHT4x + DS3231 on one I²C bus, ESP-NOW to the CYD AP (fixed channel), RTC time in
      telemetry
- [ ] Self-heating test: CYD sensor in a mock-up chamber vs room node vs reference thermometer (24 h)
- [x] Firmware image size: base v0.1 = 1.18 MB of a 1.75 MB OTA slot (web server still to add)
- [ ] Android: connect + bind to a no-internet ESP32 AP from the parents' phone model

### Phase 2 — MVP (control works in the house)

- [x] `tm_protocol`, `tm_control`, `tm_util` libraries + native unit tests (46 tests incl. hard-rule
      fuzz tests and golden vectors from the simulator)
- [x] Room node firmware v0.1 (telemetry, RTC, ACK, pairing, LED, SHT4x or DHT22) — compiles, not yet on hardware
- [x] Base v0.1: control task, valve FSM, PI + modulator, sensor fusion + learned offset, LIMP,
      NVS with CRC, watchdogs, time from node RTC / console, schedule with efficient recovery,
      humidity advice — compiles, not yet on hardware
- [x] Base UI v0.1: Home (Italian), screen off after 30 s, Diagnostics, serial console
- [x] Base v0.2: **ARRIVO** button (D22, decided by simulation), **8 hidden settings pages**
      (status, manual date/time, temperatures, schedule, sensors, screen, all parameters, system),
      **LDR light sensing** + auto-brightness, screens rendered on the PC by `tools/ui_preview`
- [x] Logging v0.1: SD per-minute CSV + events (RAM buffer and daily summary: later)
- [ ] **Bring-up on the bench** ([firmware/README.md](../firmware/README.md)): colour test, touch
      calibration, relay stays off through reset/flash, DS18B20, pairing, 24 h soak with a lamp or
      LED instead of the valve
- [ ] Time from the phone (needs the web page, Phase 3) — until then: console `time` once, then
      the node's DS3231 keeps it
- [ ] Temporary enclosure (or v1 of the printed one) + safe mains wiring
- [ ] Install, run 1–2 weeks, then CLASSIC alternate days for the A/B comparison

**Done when:** MVP acceptance criteria in [01](01-requirements.md#acceptance-criteria-mvp) pass.

### Phase 3 — Comfort & family

- [x] **Enclosure v1** in OpenSCAD ([13](13-enclosure.md)): 2 parts, CAD interference-checked, STLs exported
- [ ] Fit test print with the real CYD, then the full box; adjust cradles to the real PSU/relay
- [ ] Web pages: simple (parents) + advanced; JSON API; CSV download; OTA
- [ ] **Android app v1** ([12](12-android-app.md)) + GitHub Actions APK build, installed on the parents' phones
- [ ] All CYD screens (Graph, Stats, Schedule, Sensors, Settings), buzzer
- [ ] Modes: Away until date (D23), plant hours. (Boost, ARRIVO and the fixed 1 h pre-heat are done)

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
