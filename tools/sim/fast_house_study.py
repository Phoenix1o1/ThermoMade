#!/usr/bin/env python3
"""
STAGED 2026-11-12, NOT RUN YET. Andrea: the real house warms faster than the model (the supply
water is extremely hot). Re-run the arrival comparison for faster houses to see whether the
ECO / BALANCED / PERFORMANCE conclusions (docs/04 §5.0, §5.1) still hold.

Each variant changes the plant in place, then prints the arrival table for the three profiles
(+ ECO with ARRIVO's forced hour). Takes a few minutes.

    cd tools/sim && python3 fast_house_study.py | tee ../../docs/img/fast_house_results.txt

Then: put the variant closest to reality (see the questions in docs/10 "D31") into PLANT, or
better, fit it from real logs with tools/analysis/tm_logs.py fit.
"""
import copy

import arrival_study as A
import thermal_sim as ts

BASE = copy.deepcopy(ts.PLANT)

# name -> multipliers / overrides on the default plant
VARIANTS = {
    "default model": {},
    "hotter water (85 C supply)": {"T_supply": 85.0},
    "radiators x1.5": {"rad_P50_W": 1.5},
    "radiators x2": {"rad_P50_W": 2.0},
    "lighter house (mass x0.5)": {"C_mass_kJK": 0.5},
    "lighter house + radiators x1.5": {"C_mass_kJK": 0.5, "rad_P50_W": 1.5},
    "light house (mass x0.33) + radiators x2 + 85 C": {"C_mass_kJK": 0.33, "rad_P50_W": 2.0, "T_supply": 85.0},
}
STRATEGIES = [("ECO", "efficient"), ("ECO + ARRIVO 60'", "force_eff:60"), ("BALANCED", "balanced"), ("PERFORMANCE", "fast")]
COLS = ["profile", "T1h", "T3h", "T6h", "t16_h", "t18_h", "billed6", "billed48", "overshoot"]


def apply(changes):
    ts.PLANT.clear()
    ts.PLANT.update(copy.deepcopy(BASE))
    for k, v in changes.items():
        if k not in ts.PLANT:
            raise KeyError(f"{k} is not a PLANT parameter: {sorted(ts.PLANT)}")
        ts.PLANT[k] = v if k == "T_supply" else ts.PLANT[k] * v


def main():
    for name, changes in VARIANTS.items():
        apply(changes)
        for t_out in (-2.0,):
            print(f"\n=== {name}: arrival at 17:00, house 7 C, outdoor {t_out:+.0f} C ===")
            rows = []
            for label, strat in STRATEGIES:
                r, _ = A.run(strat, t_out)
                r["profile"] = label
                rows.append(r)
            base = rows[0]["billed48"]
            for r in rows:
                r["extra48"] = r["billed48"] - base
            ts.table(rows, COLS + ["extra48"])
    apply({})


if __name__ == "__main__":
    main()
