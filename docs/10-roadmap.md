# 10 — Roadmap & open decisions

## Open decisions (answer these first)

| ID | Question | Why it matters | Default if unanswered |
|---|---|---|---|
| **D1** | What does the old thermostat switch: 230 V AC, 24 V, or a dry contact into a condo module? | Relay type, safety, enclosure | Assume 230 V, so a properly isolated relay |
| **D2** | Valve actuator type (motorised / thermoelectric), stroke time, end-switch? | Billing start, ON_MIN/OFF_MIN validity, valve-sense option | Motorised, ~30–60 s |
| **D3** | Meter type: pure time, compensated time (UNI/TR 11388), or heat meter? | How much pulsing saves | Pure/compensated time (as described) |
| **D4** | Internet / Wi-Fi at the house? | NTP, remote access, pre-heat, HA/MQTT | AP-only, offline |
| **D5** | Exact CYD variant (1× micro-USB = ILI9341, or 2× USB = ST7789)? | Display driver config | ILI9341 |
| **D6** | On total sensor loss: LIMP (keep pulsing at learned duty) or FAIL_OFF? | Freeze risk vs cost | LIMP |
| **D7** | Where can base and room node go? Distance and walls? Sockets nearby? | Placement, link, power | Base at old thermostat, node in living room |
| **D8** | Condo plant hours? | Avoid billed minutes on cold water | 24 h (no gating) |
| **D9** | Toolchain: PlatformIO (pioarduino, core 3.x) or Arduino IDE? | Repo structure, tests | PlatformIO |
| **D10** | GitHub repo name/visibility | — | Private `TermoMade` |
| **D11** | Radiators: number, material, TRVs? | Model calibration, fill time | 8 aluminium, TRVs open |
| **D12** | Antifreeze setpoint while away | Freeze protection vs cost | 7 °C |

## Phases

### Phase 0 — Groundwork (now)

- [x] Concept, control design, simulation, knowledge base (this repo)
- [ ] Answer D1–D12
- [ ] Site survey checklist ([05](05-hardware.md#phase-0-site-survey-checklist)), photos into `hardware/wiring/`
- [ ] Order parts (BOM in [05](05-hardware.md#bill-of-materials-mvp))

**Done when:** wiring known, parts on the bench, decisions recorded in this table.

### Phase 1 — Bench spikes (de-risk the unknowns)

- [ ] CYD: display + touch + **microSD at the same time** with LovyanGFX + LVGL 9 (gotcha #1)
- [ ] ESP-NOW link DevKit → CYD with the CYD in AP and in AP+STA (channel scan works)
- [ ] SHT4x node: self-heating test in the planned enclosure vs reference thermometer (24 h)
- [ ] Relay stage on GPIO 22: stays off through boot/flash/reset (scope or LED)
- [ ] Measure ESP-NOW range at the real distances (RSSI)

**Done when:** each spike has a minimal sketch in `firmware/spikes/` and a note in this doc.

### Phase 2 — MVP (control works, minimal UI)

- [ ] `tm_protocol`, `tm_control` libraries + native unit tests (min-time fuzz test passes)
- [ ] Sensor firmware v1 (telemetry, ACK, pairing, LED)
- [ ] Base: control task, valve FSM, PI + modulator, fallback/LIMP, NVS persistence, watchdog
- [ ] Base UI: Home screen + Diagnostics. Serial CLI
- [ ] Logging: RAM buffer + SD CSV + events + daily summary
- [ ] Changeover switch + (optional) valve-sense input installed
- [ ] **Shadow mode** week (baseline), then go live

**Done when:** MVP acceptance criteria in [01](01-requirements.md#acceptance-criteria-mvp) pass.

### Phase 3 — Comfort features

- [ ] All CYD screens (Graph, Stats, Sensors, Settings), LED + buzzer
- [ ] Web UI + JSON API + CSV download + browser time sync
- [ ] Modes + weekly schedule + plant hours + boost
- [ ] OTA for the base
- [ ] Enclosures v1 (OpenSCAD) for base and room node

### Phase 4 — Extensions

- [ ] Outdoor node + HDD stats + feed-forward
- [ ] Bathroom node + room policies (WEIGHTED / COLDEST)
- [ ] Pipe node + adaptive fill time + plant-off detection + heat per pulse
- [ ] Wi-Fi STA + NTP; MQTT / Home Assistant; remote pre-heat (if D4 = yes)
- [ ] ESP-NOW encryption, node OTA
- [ ] `tools/sim/fit.py`: calibrate the model from logs, re-tune

### Phase 5 — Learning & polish

- [ ] Auto-learned ON_MIN, K_ff, warm-up rate (pre-heat timing), overshoot learning
- [ ] A/B report after a season: savings per HDD, comfort, valve cycles

## Risks

| Risk | Mitigation |
|---|---|
| Billing turns out to be a real heat meter | Comfort gain still holds. Measure before investing in Phase 4 |
| Valve/actuator wear from 2–3× more cycles | Cycle counter, `MAX_CYCLES_PER_HOUR`, OFF_MIN ≥ 5 min, check actuator rating |
| Condo rules about the thermostat | Only the thermostat contact is replaced. Meter and valve are untouched. Old thermostat kept as bypass |
| Mains wiring in a DIY enclosure | Certified relay, separation, fuse, or electrician. Low-voltage-only CYD side |
| Sensor self-heating / bad placement | Enclosure baffle, calibration, plausibility checks, fallback chain |
| Unattended house + firmware bug | Watchdog, LIMP default, bypass switch, antifreeze in OFF mode |
