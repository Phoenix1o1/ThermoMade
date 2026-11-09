# 13 — Enclosure (parametric OpenSCAD, 2 parts, on the 503 box)

Spec for `hardware/enclosures/termomade_box.scad` (to be written in Phase 3, after measuring the
real parts). Andrea's rules: **parametric OpenSCAD with live preview**, **no small radii or
fillets** (use chamfers or large radii only).

## Concept

```
 side section (wall on the left)

  wall │
       │┌───────────────── FRONT (part 2) ─────────────────┐
  503  ││   top vent slots                                 │
  box  ││ ┌──────────────────────────────┐                 │
 ┌─────┤│ │  CYD PCB on 4 posts          │▒ display        │
 │POCKET││ │  (components face the back)  │▒ window         │
 │ PSU  ││ └──────────────────────────────┘                 │
 │ relay││═══════ solid internal wall ════════════════      │
 │ term.││   sensor chamber: DS18B20 on a clip              │
 └─────┤│   ≡≡≡ vent slots front + bottom ≡≡≡              │
       │└───────────────────────────────────────────────────┘
       │  BASE plate (part 1) screwed to the 503 box (2 screws, 83.5 mm)
```

- **Part 1, BASE:** plate against the wall with two **slotted holes** for the 503 box screws
  (standard spacing 83.5 mm, horizontal, ±3 mm slot). On its back, a **closed mains pocket**
  that sits inside the 503 box cavity. It holds the PSU, the relay module, the fuse and the
  terminal block, so all 230 V parts live inside the wall box, and the PSU heat goes into the
  wall cavity, away from the sensor. Mains wires enter the pocket from the wall box side. Only
  3 low-voltage wires (5 V, GND, relay IN) leave the pocket through one small grommet hole.
- **Part 2, FRONT:** cover holding the CYD behind a display window. Inside: the CYD
  compartment, and **below it a separate sensor chamber** with its own vents and a solid wall
  between them. The DS18B20 sits on a clip in the middle of the chamber, touching nothing.
- **Joining:** two hooks at the top of the front engage slots in the base. **Two M3 screws from
  underneath** go into heat-set inserts in the base. Nothing visible from the front, nothing a
  parent can open by accident.
- **Service access:** a slot on the side (or bottom) reaches the CYD's micro-USB, so it can be
  flashed without opening the box (OTA is the normal route). The microSD is reachable after
  removing the 2 bottom screws.

## Parameters (defaults, **measure before printing**)

| Group | Parameter | Default | Note |
|---|---|---|---|
| View | `part` | `"assembly"` | `"base"`, `"front"`, `"assembly"`, `"exploded"`, `"section"` |
| View | `show_ghosts` | `true` | Grey stand-ins for CYD, PSU, relay, terminals, 503 box |
| Outer | `box_w`, `box_h`, `box_d` | 120, 90, 30 mm | Depth above the wall surface (front + base) |
| Outer | `wall` | 2.4 mm | 6 perimeters @0.4 |
| Outer | `edge_r_vertical` | 6 mm | Large radius on vertical edges only (hull of cylinders) |
| Outer | `front_chamfer` | 1.5 mm | 45° chamfer on the front edge (prints face-down) |
| 503 box | `box503_screw_dx` | 83.5 mm | Fixing-screw centre distance. **Verify** |
| 503 box | `box503_screw_d` | 3.8 mm | Clearance hole, slot length ±3 mm |
| 503 box | `box503_open_w`, `_h`, `_d` | measure | Opening and depth of the wall box → limits the pocket |
| Pocket | `pocket_w`, `pocket_h`, `pocket_d` | 88, 40, 30 mm | Must clear the 503 opening by ≥ 2 mm per side and leave room for the wires |
| Pocket | `pocket_wall` | 2.4 mm | Closed tray. Lid = the base plate |
| Pocket | `psu_w/h/d` | 34, 20, 15 mm | HLK-PM05 class. Cradle with two ribs, no screws |
| Pocket | `relay_w/h/d`, `relay_holes` | 50, 26, 19 mm; measure | 1-ch module on 2 standoffs |
| Pocket | `term_w/h/d` | 25, 10, 15 mm | 4–5 pole terminal block |
| Pocket | `lv_hole_d` | 6 mm | Grommet / sealed passage for the 3 LV wires |
| CYD | `cyd_pcb_w`, `cyd_pcb_h`, `cyd_pcb_t` | 86, 50, 1.6 mm | Measure |
| CYD | `cyd_holes` | measure | 4 corner holes. Posts with M3 heat-set inserts or 2.5 mm self-tap |
| CYD | `cyd_back_clearance` | 12 mm | Components, connectors (JST leads) behind the PCB |
| CYD | `win_w`, `win_h`, `win_dx`, `win_dy` | 59, 45 mm; measure offsets | 2.8" active area 57.6 × 43.2 + 0.7 mm/side. The display is not centred on the PCB |
| CYD | `usb_slot` | side, 12 × 8 mm | Micro-USB access |
| CYD | `ldr_hole_d` | 3 mm | Small hole in front of the LDR (light sensing) |
| Sensor | `chamber_h` | 18 mm | Below the CYD compartment |
| Sensor | `vent_w`, `vent_pitch`, `vent_n` | 2, 4.5 mm, 10 | Slots on the front bottom and on the bottom face |
| Sensor | `ds18b20_clip` | TO-92 or 6 mm probe | Selectable |
| Top | `top_vents` | true | Exhaust slots above the CYD |
| Join | `hook_w`, `hook_h` | 10, 3 mm | 2 top hooks |
| Join | `insert_d`, `insert_l` | 4.0, 5.7 mm | M3 heat-set inserts (2 bottom) |
| Print | `fit_clearance` | 0.3 mm | Between front and base |

## Mains safety rules (designed in, not optional)

1. All 230 V parts **inside the pocket**, and the pocket inside the 503 box. The front part holds
   only low voltage.
2. **≥ 6 mm** clearance from any 230 V terminal or relay contact trace to the LV side or to the
   outside surface. The pocket wall counts as a barrier, not as clearance.
3. Fuse **T 500 mA** on the PSU input. PSU = enclosed module with reinforced isolation.
4. Wires: strain relief with cable-tie anchors in the pocket. Terminal block, no loose splices.
5. Material: **PETG or ASA** minimum. A flame-retardant (V-0) filament for the base is better.
   Not PLA (creeps when warm).
6. If anything about the 230 V side is unclear (e.g. no neutral, D13), have an electrician check it.

## Printing & assembly

- **Front:** printed face-down (smooth face, chamfer on the bed). The window has straight edges
  only. The vent slots are vertical so they bridge cleanly.
- **Base:** printed back-face-down, pocket pointing up, no supports.
- Inserts: 2 × M3 for joining, 4 × M3 (or self-tap) for the CYD posts, 2 × for the relay module.
- Order: wire the pocket (L, N, OPEN, CLOSE) → screw the base to the 503 box → connect the 3 LV
  wires to the CYD (P1 5 V/GND, GPIO 22) and the DS18B20 to CN1 (3.3 V, GND, GPIO 27) → hook the front on → 2 screws from below.

## To measure before writing the model

- [ ] 503 box: screw spacing, opening W × H, usable depth with the wires in
- [ ] CYD: PCB outline, hole positions/diameter, display window offset, USB and SD positions,
      height of the tallest part on the back
- [ ] Actual PSU and relay module dimensions and hole positions
- [ ] DS18B20 form (TO-92 or probe)
