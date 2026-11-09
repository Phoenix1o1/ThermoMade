# hardware/

| Path | Content |
|---|---|
| `enclosures/termomade_box.scad` | **Base-station box v1**: parametric OpenSCAD, 2 parts on the 503 wall box ([docs/13](../docs/13-enclosure.md)) |
| `enclosures/stl/` | Print-ready STLs: `termomade_fit_test` (print first), `termomade_front_print`, `termomade_base_print` |
| `enclosures/img/` | Renders used in the docs |
| `wiring/` | Site-survey photos, wiring of old thermostat/valve, TermoMade wiring diagrams |
| `datasheets/` | CYD, SHT4x, relay, PSU, Coster CDK 068 |

Regenerate the STLs after changing parameters:

```bash
cd hardware/enclosures
for p in front_print base_print fit_test; do openscad -o stl/termomade_$p.stl -D "part=\"$p\"" termomade_box.scad; done
```

Hardware decisions and the BOM are in [docs/05-hardware.md](../docs/05-hardware.md).
