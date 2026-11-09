# 05 — Hardware

## Valve actuator: Coster CDK 068 (existing, do not modify)

| Item | Value |
|---|---|
| Type | Reversible rotary actuator (90°) for Coster ball / ceramic-disc valves |
| Supply | 230 V~ ±10 %, 50–60 Hz, 4 VA |
| Control | **3-point**: common (N), open, close. Needs a **changeover (SPDT) contact** |
| Run time | 60 s for 90° (datasheet). Flow starts around mid-stroke ≈ 30 s (what you observed) |
| Limit switches | Internal, cut the motor at both ends. L may stay applied permanently |
| Auxiliary microswitch | Changeover, switches at ~mid-stroke, 250 V~ 5(1) A. **Most likely wired to the condominium hour counter. Leave it alone** |
| Delivered | Closed |

The old thermostat's three wires in the 503 box are therefore:
**L (thermostat common) / OPEN / CLOSE**. The thermostat puts L on OPEN or on CLOSE.

## Relay stage (replaces the thermostat contact)

```
            503 wall box                          printed box, mains pocket
  L  ───────────────────────────────┬────────► relay COM
                                    │          relay NO  ─────────► OPEN  (to actuator)
                                    │          relay NC  ─────────► CLOSE (to actuator)
                                    └──[fuse T500mA]──► PSU 230 V~ L
  N  (needed for the PSU, see D13) ─────────► PSU 230 V~ N
                                               PSU 5 V ──► CYD (P1/USB 5 V), relay coil
  CYD GPIO22 ──1 kΩ──► driver/opto input of relay module (10 kΩ pull-down to GND)
```

- **Relay energised = OPEN, de-energised = CLOSE.** A crash, a reboot or a dead PSU all drive
  the valve **closed**, which is the same as the old thermostat when satisfied.
- **Relay module:** 1-channel 5 V module with **changeover contacts (COM/NO/NC)**, driver
  transistor + flyback diode, ideally optocoupled, contacts rated ≥ 250 V~ / 5 A (the actual load
  is 4 VA). Prefer modules with a milled slot or large gap between the contact traces and the
  low-voltage side. For better isolation, use a Finder 34.51 (5 V coil) on a small carrier.
- **Only one relay is needed.** The SPDT contact gives both directions, exactly like the old
  thermostat.
- **Default state:** GPIO 22 is not a strapping pin. The pull-down keeps the relay off while the
  ESP32 boots or is flashed.
- **Neutral:** the actuator's N goes straight to the actuator, so the old thermostat box probably
  has **only L, OPEN, CLOSE**. The PSU needs **N**. Check the 503 box (decision D13). If there is
  no N: bring one from the nearest junction box (electrician), or power the CYD from a USB
  adapter in a nearby socket.
- **Never touch** the actuator's auxiliary switch, the meter, or anything in the condominium
  box.

## Base station: CYD (ESP32-2432S028R)

ESP32-WROOM-32 (240 MHz dual core, 520 KB SRAM, 4 MB flash, no PSRAM), 2.8" 320×240 TFT,
XPT2046 resistive touch, microSD, RGB LED (back side), LDR, speaker amp, CH340 USB-serial.
PCB ≈ 86 × 50 mm (measure yours for the enclosure).

### CYD variants

| Variant | How to recognise it | Display | Notes |
|---|---|---|---|
| Original "R" | 1 × micro-USB | ILI9341 | Most examples target this |
| "2USB" | USB-C + micro-USB | ST7789 | Needs the ST7789 driver and color inversion. On some boards the green/blue LED pins are swapped |
| Some 2.8" clones | — | ILI9341/ST7789 | Backlight reported on GPIO 27 instead of 21. **Verify before using GPIO 27** |

### Pin map (base)

| GPIO | Function on the board | TermoMade use |
|---|---|---|
| 12, 13, 14, 15, 2 | TFT SPI (HSPI) | Display |
| 21 | TFT backlight | PWM: **off after 30 s idle**, LDR-scaled when on |
| 25, 32, 39, 33, 36 | Touch XPT2046 | Touch (IRQ 36 wakes the screen) |
| 18, 19, 23, 5 | microSD (VSPI) | Logging |
| 4 / 16 / 17 | RGB LED (active LOW), **back side** | Kept off (invisible inside the box). Spare pads if ever needed |
| 34 | LDR (input only) | Backlight level when the screen is on (the LDR faces the front) |
| 26 | Speaker amplifier | Alarm beeps (optional small speaker) |
| 0 | BOOT button | Long press at boot = factory reset (planned) |
| 1 / 3 | UART0 (CH340), P1 | Serial debug / CLI. P1 also carries 5 V in |
| **22** | Free (P3 and CN1) | **Relay** |
| **27** | Free (CN1). Check the backlight variant | **1-Wire: DS18B20 local temperature sensor** |
| **35** | Free (P3), input-only, no pull-up | Spare |

Why 1-Wire for the local sensor: only GPIO 22/27 are free on connectors, the relay takes 22, so
one pin is left. A DS18B20 needs one pin (I²C would need two). Its ±0.5 °C absolute error doesn't
matter, because its offset is learned against the room node ([04 §1](04-control-algorithm.md#1-measurement--sensor-fusion)).

Known board constraint: **touch and SD both default to VSPI.** Prove a working combination in
the Phase 1 spike (bit-banged touch, or a LovyanGFX config that drives touch separately).

### Power budget & self-heating

| Consumer | Approx. |
|---|---|
| CYD, Wi-Fi AP on, backlight off | 0.4–0.6 W |
| Backlight on (≤ 30 s at a time) | +0.3–0.5 W |
| Relay coil (while open, ~20 % of the time) | 0.2–0.4 W |
| PSU losses (HLK-PM05 class) | 0.1–0.3 W |

About 1 W in a small box → the inside runs a few °C warm. Hence the **separate, vented sensor
chamber** at the bottom of the enclosure and the learned offset. The screen being off 99 % of the
time helps.

## Room node: ESP32 DevKit

| Item | Choice | Why |
|---|---|---|
| Board | ESP32 DevKit (WROOM-32), USB-powered from a socket | You have it. USB power = no sleep complexity |
| Sensor | **Sensirion SHT41** (±0.2 °C typ.) or **SHT45** (±0.1 °C typ.) breakout | Reference sensor of the system |
| Clock | **DS3231 RTC module** with CR2032 | System time after power cuts (no internet). ±2 ppm ≈ 1 min/year |
| Bus | I²C: SDA GPIO 21, SCL GPIO 22, 3.3 V — both modules on the same bus | DS3231 at 0x68, SHT4x at 0x44 |
| Indicator | On-board LED: short blink on each ACK | Link health at a glance |

**DS3231 gotcha:** the common "ZS-042" module has a charging circuit (diode + 200 Ω) meant for an
LIR2032. With a normal CR2032, **remove the diode or resistor**, otherwise it tries to charge a
non-rechargeable cell.

**Self-heating:** mount the SHT4x outside or below the board compartment, behind a baffle, with
vents. CPU at 80 MHz. Validate 24 h against a reference thermometer.

**Placement in the big room:** opposite side from the base if possible (two spots average
better), ~1.5 m high, interior wall, away from radiators, sun, door, kitchen, TV.

## Future nodes

| Node | Hardware | Notes |
|---|---|---|
| Bathroom | DevKit or C3 + SHT41 | USB if a socket is near, otherwise battery. RH → shower detection |
| Outdoor | ESP32-C3/C6 small board (low-Iq LDO) + SHT41/DS18B20, 18650 | Deep sleep, 5 min. 3D-printed radiation shield. ESP-NOW Long Range if the link is weak |
| Pipe | DevKit + 2× DS18B20 clamp-on supply/return | Only on pipes inside the apartment. Gives fill time, plant-off detection |

## Bill of materials (MVP)

| Qty | Item |
|---|---|
| 1 | ESP32-2432S028R (CYD) |
| 1 | ESP32 DevKit (WROOM-32) |
| 1 | SHT41 or SHT45 breakout (I²C) |
| 1 | DS3231 RTC module + CR2032 (charging diode removed) |
| 1 | DS18B20 (TO-92, or a short waterproof probe) + 4.7 kΩ pull-up |
| 1 | 1-channel 5 V relay module with COM/NO/NC, contacts ≥ 250 V~ 5 A (or Finder 34.51 + carrier) |
| 1 | Enclosed 230 V → 5 V PSU module, ≥ 1 A, e.g. HLK-PM05/HLK-5M05 class (≈ 34 × 20 × 15 mm) |
| 1 | Fuse T 500 mA (inline or PCB) + small varistor (as in the PSU datasheet application circuit) |
| 1 | Terminal block 4–5 poles (L, N, OPEN, CLOSE, spare) or lever connectors |
| 1 | USB power supply + cable for the room node |
| 1 | microSD (≥ 4 GB, FAT32) |
| — | 1 kΩ, 10 kΩ resistors, JST 1.25 mm leads for CN1/P3/P1, M3 heat-set inserts + screws |
| — | Reference thermometer for calibration |
| keep | The old thermostat, as emergency spare |

## Enclosure

Two-part parametric OpenSCAD box screwed onto the 503 box. Full spec in
[13-enclosure](13-enclosure.md).

## Phase 0 site survey (checklist)

- [x] Valve actuator: Coster CDK 068, 230 V 3-point
- [x] Old thermostat wiring: 3 wires (common, open, close), 230 V
- [ ] **Is N (neutral) available in the 503 box?** (D13)
- [ ] Photograph the 503 box: wires, free depth, screw positions. Measure the fixing-screw
      spacing (standard 83.5 mm) and the opening size
- [ ] Confirm the fill time: stopwatch from "open" until the farthest radiator's return pipe gets
      hot (expect ≈ 3 min incl. ~30 s valve travel)
- [ ] Radiators: count, material, TRVs?
- [ ] Condo plant schedule (hours/day)?
- [ ] Socket for the room node, its position in the big room, distance to the base
- [ ] Measure the CYD (outline, hole positions, connector positions) with calipers
- [ ] Last season's condominium statement: your hours (baseline)
