#!/usr/bin/env python3
"""Browser test of the base's web page against the mock API (needs: pip install playwright,
playwright install chromium). Starts the mock itself. Usage: test_page.py [--shots DIR]"""
import json, os, subprocess, sys, tempfile, time, urllib.request
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8097
U = f"http://localhost:{PORT}"
shots = sys.argv[sys.argv.index("--shots") + 1] if "--shots" in sys.argv else None
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "mock_server.py"), str(PORT)], stdout=subprocess.DEVNULL)
try:
    time.sleep(1.0)
    errs = []
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 400, "height": 820}, device_scale_factor=2)
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.on("console", lambda m: errs.append(m.text) if m.type == "error" else None)
        pg.goto(U)
        pg.wait_for_timeout(1200)
        assert pg.inner_text("#t").startswith("19.6"), pg.inner_text("#t")
        assert pg.inner_text("#clock") != "--:--", "phone time not sent"
        if shots:
            pg.screenshot(path=os.path.join(shots, "web_home.png"))
        pg.click("text=+")
        pg.click("#m3")
        pg.wait_for_timeout(900)
        assert pg.inner_text("#m3") == "ARRIVO"
        pg.click("#m3")
        pg.wait_for_timeout(900)
        assert "ARRIVO" in pg.inner_text("#mode")
        pg.click("#off")
        assert pg.inner_text("#off") == "OK?"
        pg.click("text=Parametri avanzati")
        pg.wait_for_timeout(600)
        inp = pg.locator("#ptab input").nth(1)
        inp.fill("0.8")
        inp.dispatch_event("change")
        pg.click("text=Impostazioni")
        pg.select_option("#prof", "0")
        pg.fill("#sd", "07:00")
        pg.click("text=Applica orario")
        pg.click("text=Sensori")
        pg.click("#pair")
        pg.wait_for_timeout(900)
        assert "aperto" in pg.inner_text("#pair")
        pg.click("text=Aggiornamento firmware")
        f = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
        f.write(b"\xe9" + b"\0" * 2000)
        f.close()
        pg.set_input_files("#fw", f.name)
        pg.click("#fwbtn")
        pg.wait_for_timeout(800)
        assert "riavvia" in pg.inner_text("#fwmsg"), pg.inner_text("#fwmsg")
        if shots:
            pg.screenshot(path=os.path.join(shots, "web_settings.png"), full_page=True)
        b.close()
    log = json.load(urllib.request.urlopen(U + "/_log"))
    sent = [(p, q.get("c") or q.get("name") or "upload") for p, q in log]
    for want in [("/api/cmd", "time"), ("/api/cmd", "sp"), ("/api/cmd", "mode"), ("/api/cmd", "arrive"),
                 ("/api/set", "kp"), ("/api/cmd", "sched"), ("/api/cmd", "profile"), ("/api/cmd", "pair"), ("/api/update", "upload")]:
        assert want in sent, f"missing request {want}; got {sent}"
    assert not errs, errs
    print("web page test OK:", len(log), "requests")
finally:
    srv.terminate()
