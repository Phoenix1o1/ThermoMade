# TermoMade

**Pulse-controlled zone-valve thermostat for a mountain house on time-billed central heating.**
Two ESP32s: a **CYD touchscreen base station** that drives the valve, and a **wireless room
sensor**. Outdoor, bathroom and pipe sensors can be added later.

> Status: **Phase 0: planning.** No firmware yet. The plan, the knowledge base and a thermal
> simulator are in this repo.

## The idea in one paragraph

The heating is billed by **how long the zone valve is open**, and the water from the condominium
plant is very hot. A classic thermostat keeps the valve open long after the radiators are full,
sending hot water back to the plant (billed, not used). Then it overshoots, because the radiators
are still full of heat when it stops. TermoMade instead **opens the valve just long enough to fill
the circuit (3 min), closes it, and lets the radiators release that heat unbilled**. A PI
controller sets how often to pulse (and, only in very cold weather, how long), always respecting
**ON ≥ 3 min** and **OFF ≥ 5 min**.

Simulation (`tools/sim`, guessed plant parameters) vs a classic ±0.2 °C thermostat:

| | Classic thermostat | TermoMade |
|---|---|---|
| Billed valve time, winter day | 254 min | **177 min (−30 %)** |
| Compensated meter units (UNI/TR 11388-like) | 3.47 | **2.08 (−40 %)** |
| Room temperature RMS error | 0.50 °C | **0.13 °C** |
| Overshoot after cold arrival | 0.73 °C | **0.43 °C** |
| Valve cycles/day | 29 | 59 |

```mermaid
flowchart LR
  N["Room node<br/>ESP32 + SHT4x"] -- ESP-NOW --> B["Base: CYD 2.8&quot;<br/>PI + pulse modulator"]
  B -- relay --> V["Zone valve<br/>(billed while open)"]
  P["Phone"] -- Wi-Fi --> B
  F["Future: outdoor / bathroom / pipe nodes"] -.-> B
```

## Documentation

| Doc | Content |
|---|---|
| [01 Requirements](docs/01-requirements.md) | Problem, goals, functional/non-functional requirements, acceptance criteria |
| [02 Heating physics](docs/02-heating-physics.md) | Billing types, why pulsing saves, overshoot cause, simulation results |
| [03 Architecture](docs/03-architecture.md) | Nodes, data flow, room policies, modes, failure modes, network modes |
| [04 Control algorithm](docs/04-control-algorithm.md) | **The spec:** PI, pulse modulator, valve FSM, fallback, parameters, tuning |
| [05 Hardware](docs/05-hardware.md) | CYD pin map, relay stage, bypass switch, sensors, BOM, enclosures, site survey |
| [06 Communication](docs/06-communication.md) | ESP-NOW frames, pairing, channel handling, API gotchas |
| [07 User interface](docs/07-user-interface.md) | CYD screens, LED, buzzer, web UI, JSON API, serial CLI |
| [08 Software](docs/08-software.md) | Toolchain, libraries, repo layout, task model, conventions, gotchas |
| [09 Logging & evaluation](docs/09-logging-evaluation.md) | CSV formats, KPIs, baseline vs TermoMade A/B protocol |
| [10 Roadmap](docs/10-roadmap.md) | **Open decisions D1–D12**, phases, risks |
| [11 Legacy review](docs/11-legacy-review.md) | What the old `Termostato.ino` really did, bugs, what we keep |

## Repository layout

```
docs/               plan & knowledge base (start here)
tools/sim/          Python thermal simulator (design tool)
reference/legacy/   old single-board sketch, kept for reference
firmware/           PlatformIO monorepo (Phase 1+)
hardware/           wiring, enclosures (OpenSCAD), datasheets (Phase 0+)
CLAUDE.md           context for AI-assisted sessions
```

## Quick start (simulator)

```bash
cd tools/sim
python3 thermal_sim.py               # comparison tables
python3 thermal_sim.py --plot out/   # + PNG plots (matplotlib)
```
