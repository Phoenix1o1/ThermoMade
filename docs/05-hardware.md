# 05 — Hardware

## Base station: CYD (ESP32-2432S028R)

ESP32-WROOM-32 (dual core 240 MHz, 520 KB SRAM, 4 MB flash, no PSRAM), 2.8" 320×240 TFT,
XPT2046 resistive touch, microSD, RGB LED, LDR, speaker amplifier, USB-serial (CH340).

### CYD variants

Check yours before writing display code:

| Variant | How to recognise it | Display | Notes |
|---|---|---|---|
| Original "R" | 1 × micro-USB | ILI9341 | Most examples target this |
| "2USB" | USB-C + micro-USB | ST7789 | Needs the ST7789 driver and color inversion. On some boards the green/blue LED pins are swapped |
| Some 2.8" clones | — | ILI9341/ST7789 | Backlight reported on GPIO 27 instead of 21. **Verify before using GPIO 27** |

### Pin map

| GPIO | Function on the board | TermoMade use |
|---|---|---|
| 12, 13, 14, 15, 2 | TFT SPI (HSPI): MISO, MOSI, SCLK, CS, DC | Display |
| 21 | TFT backlight | Backlight PWM (dimming via LDR) |
| 25, 32, 39, 33, 36 | Touch XPT2046: CLK, MOSI, MISO, CS, IRQ | Touch |
| 18, 19, 23, 5 | microSD (VSPI): SCK, MISO, MOSI, CS | Logging |
| 4 / 16 / 17 | RGB LED R / G / B (**active LOW**) | Status LED (legacy color semantics) |
| 34 | LDR (input only) | Auto-dim backlight at night |
| 26 | Speaker amplifier input | Alarm beeps |
| 0 | BOOT button | Long-press at boot = factory reset (planned) |
| 1 / 3 | UART0 TX/RX (CH340), connector P1 | Serial debug / CLI |
| **22** | Free (P3 and CN1) | **Relay output** |
| **27** | Free (CN1) — *check backlight variant* | **1-Wire bus** (optional DS18B20s) |
| **35** | Free (P3), **input-only, no pull-up** | **Valve-sense input** (optional, via optocoupler) |

Known board constraints:

- **Touch and SD both default to the VSPI peripheral.** To use both, run touch on a bit-banged
  or separate bus (e.g. an XPT2046 bit-bang library, or a LovyanGFX config that drives touch
  separately). This is the first thing to prove in the Phase 1 spike.
- No free I²C pins unless GPIO 22/27 are used for it. That is why the plan uses 1-Wire on 27 and
  no RTC (time comes from NTP or the browser).
- GPIO 35 has no internal pull-up/down. The opto input stage must provide the pull resistor.

### Relay stage

```
 CYD CN1 / P3                         relay board / carrier
 ┌──────────┐                ┌──────────────────────────────────────┐
 │ 5V (P1)  ├───────────────►│ +5V ──┬── coil ──┐                   │
 │ GPIO22   ├──1kΩ──┬───────►│ base  │          │  NPN (or module   │
 │          │     10kΩ       │  of   │   1N4148 ▲  opto + driver)   │
 │ GND      ├───────┴───────►│ GND ──┴──────────┘                   │
 └──────────┘   (pull-down:  │                                      │
                valve closed │  COM ──── to valve/thermostat line   │
                during boot) │  NO  ──── (replaces old thermostat)  │
                             └──────────────────────────────────────┘
```

- **Relay:** a real dry-contact relay with proper isolation, e.g. **Finder 34.51 with a 5 V DC
  coil** (slim, reinforced insulation) on a small carrier with driver and flyback diode. Or a
  quality opto-isolated module. Avoid the cheapest blue modules if they switch 230 V: the PCB
  creepage is poor.
- **Load:** a zone-valve actuator draws only a few VA. Switching every ≥ 3 min is trivial for
  the contact. Expect ~25–100 operations/day.
- **Default state:** de-energised = valve closed. The 10 kΩ pull-down keeps it closed while the
  ESP32 boots or is flashed (GPIO 22 is not a strapping pin).
- **What the old thermostat switches (Phase 0, measure it):**
  1. 230 V AC to the valve actuator (most common): the relay contact switches L to the
     actuator. Mains wiring, fuse, enclosure rules apply.
  2. 24 V AC: same, low voltage.
  3. Dry-contact input of a condo controller/meter module: the relay contact just closes the
     loop.
  **Never touch the meter/counter or the valve wiring. Only replace the thermostat contact.**

### Changeover (bypass) switch

A 3-position switch (or two-pole changeover) between the relay contact and the old thermostat:

| Position | Valve driven by |
|---|---|
| TermoMade | CYD relay |
| Old | the original thermostat (left in place) |
| Off | nothing (summer) |

It lets you revert in seconds if the electronics fail, and it is how the **baseline** is
measured ([09](09-logging-evaluation.md)).

### Valve-sense input (optional, recommended)

An AC-opto detector module (for 230 V) or a simple opto + resistor (for 24 V) on **the valve
side of the changeover switch** → GPIO 35 (with external 10 kΩ pull-up to 3.3 V).
It measures **billed time no matter who commands the valve**, which gives the baseline data with
the old thermostat. It also detects a stuck relay. If the valve has an auxiliary end-switch,
sensing that is even better (true open time).

### Power

5 V USB power supply, ≥ 1 A (CYD ≈ 150–250 mA with backlight + relay coil ≈ 35–80 mA). If the
base sits in the old thermostat's wall box and only 230 V is there: a certified enclosed 5 V
module (e.g. Hi-Link HLK-PM05 class) inside a properly separated enclosure, or an electrician.

## Room sensor node: ESP32 DevKit

| Item | Choice | Why |
|---|---|---|
| Board | ESP32 DevKit (WROOM-32), USB-powered | You have it. USB power means no sleep complexity |
| Sensor | **Sensirion SHT41** (±0.2 °C typ.) or **SHT45** (±0.1 °C typ.) breakout, I²C | Far better than the DHT22 (±0.5 °C, drift, slow single-wire timing) |
| Wiring | SDA GPIO 21, SCL GPIO 22, 3.3 V, GND | Default I²C pins on the DevKit |
| Indicator | On-board LED: short blink on each ACK | Link health at a glance |
| Optional | 0.96" SSD1306 OLED (I²C, same bus) | Shows temp, setpoint, valve state from the base's ACK |

**Self-heating is the main accuracy risk.** An ESP32 with Wi-Fi radio warms its enclosure by
1–3 °C. Mount the SHT4x **outside or below** the board compartment, in a vented chamber separated
by a baffle, ≥ 3–5 cm from the module. Cut CPU to 80 MHz. Validate against a reference
thermometer for 24 h and set the offset in the base.

**Placement:** interior wall, ~1.5 m high, away from radiators, direct sun, doors, lamps, TV and
kitchen. Same room as the old thermostat's "reference" if it was well placed.

## Future nodes

| Node | Hardware | Notes |
|---|---|---|
| Outdoor | ESP32-C3/C6 small board (low quiescent LDO) + SHT41 or DS18B20, 18650 Li-ion | Deep sleep, wake every 5 min. 3D-printed radiation shield (Stevenson screen). North wall. ESP-NOW **Long Range** mode if the link is weak |
| Bathroom | DevKit or C3 + SHT41 | USB if a socket is near, otherwise battery. Humidity spikes → shower detection |
| Pipe | DevKit + 2× DS18B20 (1-Wire, 4.7 kΩ pull-up) clamped on supply/return with thermal paste and pipe insulation | Near the apartment's valve/manifold **inside the apartment**, or one probe on the farthest radiator's return. Never open the condo meter box |

## Bill of materials (MVP)

| Qty | Item |
|---|---|
| 1 | ESP32-2432S028R (CYD) |
| 1 | ESP32 DevKit (WROOM-32) |
| 1 | SHT41 or SHT45 breakout (I²C) |
| 1 | Relay 5 V DC coil with driver (Finder 34.51 + carrier, or quality opto module) |
| 1 | 3-position changeover switch (rated for the switched voltage) |
| 2 | 5 V USB power supplies (≥ 1 A) + cables |
| 1 | microSD card (≥ 4 GB, FAT32) |
| — | 1 kΩ, 10 kΩ resistors, 1N4148, JST 1.25 mm cables for CN1/P3, Dupont wires |
| opt. | AC-opto detector module (valve sense) |
| opt. | DS18B20 (TO-92 or waterproof) + 4.7 kΩ (local fallback sensor / pipe) |
| opt. | Reference thermometer for calibration |

## Enclosures (3D printed)

- Parametric **OpenSCAD** models in `hardware/enclosures/`, **no small radii or fillets**
  (chamfers or large radii only).
- **Base:** wall-mount frame for the CYD, optionally on an Italian 503 box. Rear cavity for the
  relay carrier, with mains and low-voltage sides physically separated if mains is present.
  Vent slots so the screen and ESP32 do not heat the case. Access to microSD and the USB port.
- **Room node:** two chambers: the ESP32 on top, and the sensor below behind a solid baffle with
  vertical slots for convection.
- **Material:** PETG or ASA near radiators. Not PLA, which creeps with warmth.

## Phase 0 site survey (checklist)

- [ ] Photograph the old thermostat wiring. Measure the switched voltage (AC/DC, value).
- [ ] Identify the zone valve and actuator (brand/model): motorised or thermoelectric? Stroke
      time? Auxiliary end-switch?
- [ ] Find the meter type (label/model). Pure time, compensated, or heat meter?
      Can its display be read?
- [ ] Confirm the fill time: stopwatch from thermostat "on" until the farthest radiator's return
      pipe gets hot.
- [ ] Count radiators, material (aluminium / cast iron / steel), TRVs present?
- [ ] Condo plant schedule (hours/day)? Supply temperature if a gauge exists.
- [ ] Wi-Fi/internet at the house? Distance and walls between base and room-node spots.
- [ ] Mains socket near the base and sensor positions?
