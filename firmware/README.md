# TermoMade firmware — build & bring-up

PlatformIO monorepo: base station (CYD), room node, shared libraries, PC unit tests.
Architecture and conventions: [docs/08-software.md](../docs/08-software.md). Control spec:
[docs/04](../docs/04-control-algorithm.md).

> **Bench first.** Do every step below on the desk with a lamp or an LED on the relay contacts,
> never on the valve. Mains wiring only after the 24 h soak test passes.

## 1. Set up

1. VS Code + the PlatformIO extension. Open the `firmware/` folder (not the repo root).
2. The first build downloads the pinned pioarduino platform (~600 MB).
3. Copy `include/secrets.h.example` to `include/secrets.h` and set the AP password
   (WPA2, ≥ 8 characters). `secrets.h` is git-ignored.
4. Check the relay module type and set `TM_RELAY_ACTIVE_HIGH` in `platformio.ini`
   ([docs/05 "Relay module input"](../docs/05-hardware.md#relay-module-input-which-kind-decision-d18-open)).
   Wrong flag = relay **on** when it should be off.

## 2. Tests (no hardware)

```bash
pio test -e native        # 46 tests: control, golden vectors vs simulator, protocol, utils
```

## 3. Base station (CYD)

```bash
pio run -e base_cyd -t upload
pio device monitor -e base_cyd     # 115200; type "help"
```

On every boot the screen shows three bars for 1.5 s:

| You see | Fix in `platformio.ini` (`[env:base_cyd]`) |
|---|---|
| **red · green · blue**, left to right | Correct |
| blue · green · red | Toggle `TM_LCD_BGR` |
| Colours inverted (black background looks white, etc.) | Toggle `TM_LCD_INVERT` |
| Garbage / white screen | Try `TM_LCD_ST7789=0` (ILI9341 board) |
| Image upside down or mirrored | Add `-D TM_LCD_ROTATION=3` (0–7; 4–7 are mirrored) |

### Touch calibration

1. In the monitor: `touch on`. Touch near each edge. It prints `raw x … y … -> screen x,y`.
2. See which raw value changes when you move **left → right**: if it is `raw y`, `swap` = 1.
3. Note that raw value at the **left** and **right** edges, and the other one at the **top** and
   **bottom** edges.
4. `cal <left> <right> <top> <bottom> <swap> 0 0`, e.g. `cal 3700 300 250 3800 1 0 0`
   (reversed ranges are fine). `touch off`. Saved after 10 s.

### Checks

- [ ] Relay **off** while you hold RESET, during the upload, and for the first 5 min after boot
      (`status` → `lockout`).
- [ ] `status` → `ds18b20 ok` and a sensible `base` temperature.
- [ ] SD card (FAT32): `status` → `sd ok`; `/log/events.csv` gets a `boot` line.
- [ ] `time 2026-11-09 21:30` (local time) → clock on screen, `alarms` without `time-unknown`.
- [ ] Screen goes dark after 30 s; the first touch only wakes it.
- [ ] Long-press the clock 2 s → hidden settings (8 pages, [docs/07](../docs/07-user-interface.md#hidden-settings-long-press-the-clock-2-s)).
      Set the clock on page DATA E ORA if no node is paired yet.
- [ ] VIA, then ARRIVO (same button): valve opens once the 5 min boot pause is over, `status`
      shows `arrive forced heating`, it stops after 60 min or when the room is within 2 °C.
- [ ] Light: cover / uncover the LDR with the screen dark for a minute; `status` → `light` %
      changes (needs the scale learned: one dark and one bright reading).

### Web page

Join the Wi-Fi `TermoMade-XXXX` with a phone and open http://192.168.4.1. The clock sets itself
from the phone; change a parameter under "Parametri avanzati" and check it with `get <name>` on
the console.

## 4. Room node (ESP32 DevKit)

Wiring: I²C SDA **21**, SCL **22**, 3.3 V. SHT4x (0x44) and DS3231 (0x68) on the same bus.
DHT22 build: data on **GPIO 4** (10 kΩ pull-up to 3.3 V if the module has none).
DS3231 "ZS-042" with a CR2032: remove the charging diode first.

```bash
pio run -e node_sht4x -t upload    # or node_dht22
pio device monitor -e node_sht4x   # "status", "forget"
```

Pairing: on the base `pair` (or hidden page SENSORI → ACCOPPIA SENSORE), then power or reset
the node within 60 s. The first paired node becomes the room sensor. The node LED blinks on each ACK.
Re-pair: hold the node's BOOT button 3 s (or `forget` on its console).

After pairing, the node's DS3231 takes the base's time (set with `time`) and gives it back after
a power cut.

## 5. Soak test (24 h, lamp on the relay)

- `mode comfort`, `sp 25` (room colder than that): the lamp pulses — 3 min on, then off for at
  least 5 min; never less (hard rules).
- Unplug the room node: after 5 min `room-lost`, control continues on the CYD sensor. Unplug the
  DS18B20 too: LIMP after the grace time (`EMERGENZA` on screen), pulses at the learned duty.
- Power-cycle the base: valve stays closed for 5 min; the time comes back from the node RTC.
- Check `/log/YYYY-MM-DD.csv` on a PC.

## Open points to confirm (also in docs/10)

| | Default in the code | Confirm |
|---|---|---|
| D18 relay input type | active-HIGH, `TM_RELAY_ACTIVE_HIGH=1` | Module type + pull resistor |
| D13 neutral in the 503 box | — | Needed for the PSU |
| D21 panel colours / touch | ST7789, BGR, no inversion; touch 300–3800, swap | Colour test, `cal` |
| GPIO 27 | DS18B20 | Not the backlight on this board (backlight = 21 on the 2-USB CYD) |

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and pull request:

- **tests**: `pio test -e native`.
- **firmware**: builds `base_cyd`, `node_sht4x`, `node_dht22` (no `secrets.h` needed).
- **golden-and-sim**: regenerates `test/test_golden/golden_data.h` from the simulator and fails
  if it differs from the committed file (simulator changed, vectors not regenerated).
- **ui-preview**: renders the base screens with `tools/ui_preview/build.sh` (render only).

Built binaries: GitHub → Actions → the run → **Artifacts**: `firmware-base_cyd` (`firmware.bin`,
`bootloader.bin`, `partitions.bin`), `firmware-node_sht4x`, `firmware-node_dht22`; screens in
`ui-preview`.
