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
  CYD GPIO22 (CN1) ───► relay module IN   (+ 10 kΩ pull resistor, see "Relay module input" below)
  CYD 3V3/GND/IO27 (CN1) ► DS18B20 (4.7 kΩ pull-up DQ → 3V3)
```

- **Relay energised = OPEN, de-energised = CLOSE.** A crash, a reboot or a dead PSU all drive
  the valve **closed**, which is the same as the old thermostat when satisfied.
- **Relay module:** 1-channel 5 V module with **changeover contacts (COM/NO/NC)**, driver
  transistor + flyback diode, ideally optocoupled, contacts rated ≥ 250 V~ / 5 A (the actual load
  is 4 VA). Prefer modules with a milled slot or large gap between the contact traces and the
  low-voltage side. For better isolation, use a Finder 34.51 (5 V coil) on a small carrier.
- **Only one relay is needed.** The SPDT contact gives both directions, exactly like the old
  thermostat.
- **Default state:** GPIO 22 is not a strapping pin. An external pull resistor keeps the relay
  off while the ESP32 boots, is flashed, crashes or is reset. The firmware writes the inactive
  level before it enables the output (`Relay::begin()`, first line of `setup()`).

### Relay module input: which kind? (decision D18, open)

Relay modules come in two input types. The firmware supports both with one build flag in
`platformio.ini`; **the flag and the pull resistor must match the module**.

| | Active-HIGH ("high level trigger", NPN transistor input) — **recommended** | Active-LOW ("low level trigger", usually optocoupler) |
|---|---|---|
| Relay ON when IN is | HIGH (3.3 V) | LOW (0 V) |
| Build flag | `-D TM_RELAY_ACTIVE_HIGH=1` | `-D TM_RELAY_ACTIVE_HIGH=0` |
| Pull resistor on IN | **10 kΩ to GND** (pull-down) | **10 kΩ to 3.3 V** (pull-up) |
| Supply | VCC = 5 V | **Input side VCC = 3.3 V, coil JD-VCC = 5 V** (remove the JD-VCC jumper) |
| Why | Simple, fails off | With the opto LED fed from 5 V, a 3.3 V "HIGH" leaves ~1.7 V across the LED: the relay may not release or may chatter. Feeding the opto side from 3.3 V fixes it |
| Check | 3.3 V on IN switches the relay reliably (some 5 V modules need ≥ 4 V: test on the bench) | Relay releases fully with IN at 3.3 V |

Modules with an H/L jumper: set it to **H** and treat the module as active-HIGH.
Bench test before installing: power the CYD with the stock firmware, press RESET and hold it:
the relay must stay **off**; flash the firmware: the relay must stay **off** during the upload.
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
| **"2USB" ← Andrea's board** | USB-C + micro-USB | ST7789 | ST7789 driver, BGR colour order, **no** inversion (community CYD2USB config; confirm with the boot colour test). On some boards the green/blue LED pins are swapped |
| Some 2.8" clones | — | ILI9341/ST7789 | Backlight reported on GPIO 27 instead of 21. **Verify before using GPIO 27** |

### Pin map (base)

| GPIO | Function on the board | TermoMade use |
|---|---|---|
| 12, 13, 14, 15, 2 | TFT SPI (HSPI) | Display |
| 21 | TFT backlight | PWM: **off after 30 s idle**, LDR-scaled when on |
| 25, 32, 39, 33, 36 | Touch XPT2046 | Touch, **bit-banged** (IRQ 36 = pressed). Keeps VSPI for the SD card |
| 18, 19, 23, 5 | microSD (VSPI) | Logging |
| 4 / 16 / 17 | RGB LED (active LOW), **back side** | Kept off (invisible inside the box). Spare pads if ever needed |
| 34 | LDR (input only), front side | **Room light** (implemented): auto-brightness, light column in the log. See "Light sensing" |
| 26 | Speaker amplifier | Driven LOW (silent, no hum). Alarm beeps later (optional small speaker) |
| 0 | BOOT button | Not used by the firmware (factory reset is a console command) |
| 1 / 3 | UART0 (CH340), P1 | Serial debug / CLI. P1 also carries 5 V in |
| **22** | Free (P3 and CN1) | **Relay** |
| **27** | Free (CN1). Check the backlight variant | **1-Wire: DS18B20 local temperature sensor** |
| **35** | Free (P3), input-only, no pull-up | Spare |

Why 1-Wire for the local sensor: only GPIO 22/27 are free on connectors, the relay takes 22, so
one pin is left. A DS18B20 needs one pin (I²C would need two). Its ±0.5 °C absolute error doesn't
matter, because its offset is learned against the room node ([04 §1](04-control-algorithm.md#1-measurement--sensor-fusion)).

Known board constraint: touch and SD both default to VSPI. **Solved in firmware:** the touch
controller is bit-banged on its own pins (it needs only a few kHz), the SD card keeps VSPI and
the display keeps HSPI.

**DS18B20 wiring (CN1):** 3V3 → VDD, GND → GND, IO27 → DQ, 4.7 kΩ between DQ and 3V3. Normal
(not parasite) power. Put the sensor in the vented chamber of the box, away from the PSU and the
ESP32 module. The firmware rejects CRC errors, the "disconnected" value (−127 °C) and the 85.0 °C
power-on value, and re-detects the sensor after a failure.

### Clock: the CYD has no RTC chip

The ESP32 has an internal RTC timer: it keeps the time while powered and across resets and
watchdog reboots (the firmware keeps "time valid" in RTC memory), but **not across a power cut**.
There is no battery-backed clock chip on the CYD and no free pin pair for one (I²C needs two pins;
only GPIO 22/27 are free and both are used). The system clock after a power cut therefore comes
from the **DS3231 on the room node** (CR2032, ±2 ppm), within 30 s of the node's first frame.
Without the node: set it on the CYD (hidden page DATA E ORA) or over USB (`time`). With an
unknown time, AUTO runs as Comfort and the screen shows `Ora non impostata`.

### Light sensing: the CYD's own LDR is enough

The CYD has a light-dependent resistor on GPIO 34, on the **front**, left of the display. Its
stock circuit has a small range and it sees the display backlight, so the firmware:

- reads it only after the backlight has been off for 1 s (every 2 s, 8-sample average, ADC at 0 dB
  for more sensitivity), keeps that value while the screen is on;
- learns the scale itself (brightest / darkest readings, forgotten over ~3 days) and reports
  **0–100 %**, not lux;
- uses it for **auto-brightness** at wake-up and logs it every minute (`light_pct`), useful later
  to spot occupancy and sun on the room.

For this the front of the box needs the small hole over the LDR (`ldr_hole = true` in the
OpenSCAD model, after checking `ldr_pos` against the real board; [13](13-enclosure.md)). Ideally
add a thin light barrier between the hole and the display edge.

An extra sensor is only needed for **calibrated lux** (e.g. to correlate sun with the heating
model): a BH1750 (I²C, ~2 €) fits on the room node's I²C bus next to the SHT4x and DS3231, no CYD
pin needed. Not planned now.

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
| Sensor | **Sensirion SHT41** (±0.2 °C typ.) or **SHT45** (±0.1 °C typ.) breakout. **DHT22 also supported** (`node_dht22` build) | Reference sensor of the system. See "Room sensor choice" |
| Clock | **DS3231 RTC module** with CR2032 | System time after power cuts (no internet). ±2 ppm ≈ 1 min/year |
| Bus | I²C: SDA GPIO 21, SCL GPIO 22, 3.3 V — both modules on the same bus | DS3231 at 0x68, SHT4x at 0x44 |
| Indicator | On-board LED: short blink on each ACK | Link health at a glance |

### Room sensor choice (temperature + humidity)

Both builds report temperature **and** relative humidity. The base uses the humidity for the
dew point and the "air too dry / too humid" advice on the screen (winter: < 35 % → humidify,
> 60 % → ventilate; 3 % hysteresis).

| Sensor | Accuracy (T / RH) | Interface | Verdict |
|---|---|---|---|
| **SHT41 / SHT45** | ±0.2 / ±0.1 °C, ±1.8 / ±1.0 %RH | I²C with CRC, shares the DS3231 bus | **Recommended** (a few €). Build `node_sht4x` |
| DHT22 / AM2302 | ±0.5 °C, ±2–5 %RH | One proprietary timing-critical wire, max one reading per 2 s | Works. Build `node_dht22`, data on **GPIO 4** with 10 kΩ pull-up to 3.3 V (most modules have it). RH known to drift after long periods at high humidity |
| DHT11 | ±2 °C, **1 °C steps**, ±5 %RH | as DHT22 | **Not supported.** 1 °C steps make the PI see a staircase; useless for a 0.2 °C controller |
| DS18B20 | ±0.5 °C, no RH | 1-Wire | Good for temperature only; you would lose the humidity advice |

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

Two-part parametric OpenSCAD box screwed onto the 503 box: `hardware/enclosures/termomade_box.scad`,
print-ready STLs, CYD mechanical data and assembly in [13-enclosure](13-enclosure.md).

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
