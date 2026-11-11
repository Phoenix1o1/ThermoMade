# tools/analysis — SD-card log analysis

`tm_logs.py` reads the base station's `/log/*.csv` files (copy the SD card's `log` folder to the PC).
Needs Python 3 with numpy and matplotlib.

```bash
python3 tm_logs.py summary <logdir>                     # per-day billed minutes, pulses, temperatures, gaps
python3 tm_logs.py plot <logdir> --last 2 -o out.png    # temperatures, setpoint, valve, demand, humidity
python3 tm_logs.py fit <logdir>                         # thermal model values for tools/sim/thermal_sim.py
python3 tm_logs.py compare <logdir> ...                 # pulse vs classic days (see --help)
python3 make_fake_logs.py -o /tmp/logs --days 4         # simulator-generated logs in the firmware's format
```

Limits: outdoor temperature and heat are not logged, so absolute heat per billed minute and
weather-dependent parameters cannot be identified (the tool says which). Written 2026-11-11 and
tested only on simulated logs; no automated tests yet.
