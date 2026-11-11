#!/usr/bin/env python3
"""Serve the base station's web page (firmware/src/base/web_page.h) with a fake JSON API, to try
the page on a PC or phone without hardware:  python3 mock_server.py [port]  ->  http://localhost:8080
The fake keeps a little state so buttons visibly work. It is NOT the firmware's logic."""
import json, os, re, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "firmware", "src", "base", "web_page.h")
PAGE = re.search(r'R"HTML\((.*)\)HTML"', open(SRC, encoding="utf-8").read(), re.S).group(1)

ST = dict(t=19.6, t_room=19.55, rh=41.0, t_base=19.64, offset=1.38, sp=20.0, u=0.213, p=0.24, i=0.18, dew=6.1,
          src="FUSED", mode=0, period=0, heating=True, valve=True, reason="PULSE", next_s=102, lock_s=0,
          boost=False, boost_left_s=0, arrival=False, arrival_left_s=0, preheat=False, override=False,
          waiting=False, limp=False, alarms=0, humidity=1, time_valid=False, unix=0, hh=0, mm=0,
          billed_s=2040, pulses=9, node=True, node_age_s=12, rssi=-61, light=37, sd=True, relay=True,
          sched_day=390, sched_night=1350, scr_timeout=30, scr_max=180, scr_min=12, scr_auto=1,
          pairing=False, profile=1, fw="0.3", uptime_s=11520, reset="power-on")
PARAMS = [dict(name="sp_comfort", value=20, unit="C", step=0.5, kind=0), dict(name="kp", value=0.6, unit="1/C", step=0.05, kind=0),
          dict(name="arrival_force_min", value=60, unit="min", step=15, kind=0),
          dict(name="recovery_arrival", value=0, unit="0=eff 1=fast", step=1, kind=1)]
LOG = []


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def out(self, code, ctype, body):
        b = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path == "/":
            return self.out(200, "text/html; charset=utf-8", PAGE)
        if self.path == "/api/status":
            if ST["time_valid"]:
                lt = time.localtime(ST["unix"] + int(time.time() - ST["_set_at"]))
                ST["hh"], ST["mm"] = lt.tm_hour, lt.tm_min
            return self.out(200, "application/json", json.dumps({k: v for k, v in ST.items() if not k.startswith("_")}))
        if self.path == "/api/params":
            return self.out(200, "application/json", json.dumps(PARAMS))
        if self.path == "/_log":
            return self.out(200, "application/json", json.dumps(LOG))
        self.out(404, "text/plain", "not found")

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path == "/api/update":
            LOG.append([self.path, {"bytes": len(body)}])
            return self.out(200, "application/json", json.dumps(dict(ok=b"\xe9" in body[:400], msg="mock")))
        q = {k: v[0] for k, v in parse_qs(body.decode()).items()}
        LOG.append([self.path, q])
        ok = True
        if self.path == "/api/set":
            p = next((p for p in PARAMS if p["name"] == q.get("name")), None)
            ok = p is not None
            if ok:
                p["value"] = float(q["value"])
        elif self.path == "/api/cmd":
            c, v = q.get("c"), float(q.get("v", 0))
            if c == "mode":
                ST.update(mode=int(v), sp={0: 20.0, 1: 20.0, 2: 18.5, 3: 7.0, 4: 7.0}[int(v)], arrival=False)
            elif c == "sp":
                ST["sp"] = v
            elif c == "arrive":
                ST.update(mode=0, sp=20.0, arrival=True, arrival_left_s=3600, valve=True, next_s=-1)
            elif c == "boost":
                ST.update(boost=True, boost_left_s=int(v) * 60)
            elif c == "stop":
                ST.update(boost=False, arrival=False)
            elif c == "time":
                ST.update(time_valid=True, unix=int(v), _set_at=time.time())
            elif c == "sched":
                ST.update(sched_day=int(v), sched_night=int(float(q["v2"])))
            elif c == "screen":
                ST.update(scr_timeout=int(v))
            elif c == "profile":
                ST["profile"] = int(v)
            elif c == "pair":
                ST["pairing"] = True
            elif c != "save":
                ok = False
        self.out(200 if ok else 400, "application/json", json.dumps(dict(ok=ok, msg="mock")))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"TermoMade mock on http://localhost:{port}")
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()
