#!/usr/bin/env python3
"""Mock of the ESP-12E live endpoints, so the cockpit page (firmware/esp12e_dashboard/
live.html) can be verified in a browser with no hardware. Emulates:
   GET /            -> serves live.html
   GET /data        -> JSON telemetry (rpm wanders, effort responds to params)
   GET /set?p=&v=   -> apply a param (mu / orders / gain)
   GET /cmd?c=      -> c/r/s mode transitions
Run:  python tools/mock_esp.py   (then open http://localhost:8771)
"""
import json, math, time, os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

PORT = 8771
HTML = os.path.join(os.path.dirname(__file__), "..", "firmware", "esp12e_dashboard", "live.html")
T0 = None  # set on first request (avoid time at import)

S = {"mu": 0.05, "orders": 6, "gain": 1.0, "mode": "idle", "cal_t": 0.0, "cal": 0}
SMAG_CAL = [0.42, 0.38, 0.55, 0.30, 0.22, 0.18]   # plausible per-order |S| once calibrated

def telemetry(now):
    t = now - T0
    running = S["mode"] == "running"
    lock = t > 1.5                       # "tach locks" shortly after boot
    rpm = 1830 + 60 * math.sin(t * 0.5) + (8 * math.sin(t * 7) if running else 0)
    f0 = rpm / 60.0
    # per-order anti-noise effort: rises when running, scaled by gain, only active orders
    base = [1.0, 0.72, 0.85, 0.5, 0.34, 0.22]
    ordv = []
    for i in range(6):
        on = running and lock and i < S["orders"]
        e = (base[i] * S["gain"] * (0.6 + 0.4 * math.sin(t * 0.8 + i))) if on else 0.0
        ordv.append(round(max(0, e), 3))
    if S["mode"] == "calibrating" and (now - S["cal_t"]) > 3.0:
        S["mode"] = "stopped"; S["cal"] = 1
    cpu = 34 + (12 if running else 0) + 3 * math.sin(t * 2)
    smag = SMAG_CAL if S["cal"] else [1.0] * 6
    return {"f0": round(f0, 1), "rpm": round(rpm), "cpu": round(cpu, 1),
            "lock": 1 if lock else 0, "age": 0.0, "ord": ordv,
            "mu": S["mu"], "orders": S["orders"], "gain": S["gain"], "mode": S["mode"],
            "cal": S["cal"], "smag": smag}

class H(BaseHTTPRequestHandler):
    def _send(self, code, ctype, body):
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body))); self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        global T0
        if T0 is None: T0 = time.time()
        u = urlparse(self.path); q = parse_qs(u.query); now = time.time()
        if u.path in ("/", "/live.html"):
            self._send(200, "text/html", open(HTML, "rb").read()); return
        if u.path == "/data":
            self._send(200, "application/json", json.dumps(telemetry(now)).encode()); return
        if u.path == "/set":
            p = q.get("p", [""])[0]; v = q.get("v", ["0"])[0]
            if p == "orders": S["orders"] = max(1, min(6, int(float(v))))
            elif p == "mu":   S["mu"] = max(0.0, float(v))
            elif p == "gain": S["gain"] = max(0.0, min(1.0, float(v)))
            print(f"[set] {p} = {v}  -> would send 'SET {p} {v}' to Teensy")
            self._send(200, "text/plain", b"ok"); return
        if u.path == "/cmd":
            c = q.get("c", [""])[0]
            if c == "c": S["mode"] = "calibrating"; S["cal_t"] = now; S["cal"] = 0
            elif c == "r": S["mode"] = "running"
            elif c == "s": S["mode"] = "calibrated" if S["mode"] != "idle" else "idle"
            print(f"[cmd] {c}  -> would send '{c}' to Teensy   (mode now {S['mode']})")
            self._send(200, "text/plain", b"ok"); return
        self._send(404, "text/plain", b"nope")

    def log_message(self, *a): pass  # quiet

if __name__ == "__main__":
    print(f"mock ESP on http://localhost:{PORT}  (serving {os.path.relpath(HTML)})")
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
