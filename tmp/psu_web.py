#!/usr/bin/env python3
"""
psu_web.py -- Local web control panel that talks to a UDP power-control bridge.

Architecture:
    browser  <--HTTP-->  psu_web.py (this)  <--UDP-->  powercontrol2udp bridge  <-->  PSU

This process is a dumb pass-through: it serves the page and forwards request
strings to the bridge over UDP, returning the bridge's JSON reply verbatim.
It knows nothing about VISA/SCPI -- that lives in the bridge.

UDP protocol (the contract with the bridge):
    send "status"          -> JSON status of all channels
    send "output <ch> on"  -> JSON status (reflecting the change)
    send "output <ch> off" -> JSON status
  Status JSON:
    {"connected":true,"idn":"...","channels":[
        {"ch":1,"output":true,"vmeas":5.0,"imeas":0.42,"pmeas":2.1,"vset":5.0,"iset":1.0}, ...]}

Stdlib only. Run:
    python psu_web.py
    # open http://localhost:5000
"""

import json
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ------------------------- configuration ---------------------------------
LISTEN_HOST = "127.0.0.1"     # web UI bind (localhost only)
LISTEN_PORT = 5000
BRIDGE_HOST = "127.0.0.1"     # where powercontrol2udp is listening
BRIDGE_PORT = 5005
UDP_TIMEOUT = 1.0             # seconds to wait for a bridge reply
# -------------------------------------------------------------------------


def udp_request(cmd: str) -> bytes:
    """Send one datagram to the bridge, return its reply bytes (JSON)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(UDP_TIMEOUT)
        s.sendto(cmd.encode(), (BRIDGE_HOST, BRIDGE_PORT))
        data, _ = s.recvfrom(65535)
        s.close()
        return data
    except socket.timeout:
        return json.dumps({"connected": False,
                           "error": "No reply from bridge (timeout).",
                           "channels": []}).encode()
    except Exception as e:
        return json.dumps({"connected": False,
                           "error": f"Bridge error: {e}",
                           "channels": []}).encode()


class Handler(BaseHTTPRequestHandler):
    def _send(self, body: bytes, ctype: str, code: int = 200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/":
            self._send(PAGE.encode(), "text/html; charset=utf-8")
        elif self.path == "/api/status":
            self._send(udp_request("status"), "application/json")
        else:
            self._send(b"not found", "text/plain", 404)

    def do_POST(self):
        if self.path == "/api/output":
            n = int(self.headers.get("Content-Length", 0))
            try:
                body = json.loads(self.rfile.read(n) or b"{}")
                ch = int(body.get("ch", 1))
                state = "on" if str(body.get("state", "")).lower() == "on" else "off"
            except Exception:
                self._send(b'{"error":"bad request"}', "application/json", 400)
                return
            self._send(udp_request(f"output {ch} {state}"), "application/json")
        else:
            self._send(b"not found", "text/plain", 404)

    def log_message(self, *_):        # keep the console quiet
        pass


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PSU Control</title>
<style>
  :root{
    --bg:#0d1117; --panel:#161b22; --panel-2:#1c2430; --line:#30363d;
    --text:#e6edf3; --muted:#8b949e; --on:#3fb950; --on-dim:#238636;
    --on-glow:rgba(63,185,80,.45); --off:#f85149;
  }
  *{box-sizing:border-box}
  body{margin:0; background:var(--bg); color:var(--text);
       font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
  .wrap{max-width:960px; margin:0 auto; padding:28px 20px 60px}
  header{display:flex; align-items:center; justify-content:space-between; gap:16px;
         flex-wrap:wrap; padding-bottom:18px; border-bottom:1px solid var(--line); margin-bottom:24px}
  h1{margin:0; font-size:18px; font-weight:650}
  .idn{font-size:12.5px; color:var(--muted); font-family:ui-monospace,Consolas,monospace; margin-top:4px}
  .link{display:flex; align-items:center; gap:9px; font-size:13px; color:var(--muted)}
  .lamp{width:9px; height:9px; border-radius:50%; background:var(--off)}
  .lamp.ok{background:var(--on); box-shadow:0 0 10px var(--on-glow); animation:pulse 2.4s infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
  @media (prefers-reduced-motion:reduce){.lamp.ok{animation:none}}
  .grid{display:grid; gap:18px; grid-template-columns:repeat(auto-fill,minmax(300px,1fr))}
  .card{background:var(--panel); border:1px solid var(--line); border-radius:12px;
        padding:20px; position:relative; overflow:hidden}
  .rail{position:absolute; left:0; top:0; bottom:0; width:4px; background:var(--line)}
  .card.live .rail{background:var(--on); box-shadow:0 0 14px var(--on-glow)}
  .chead{display:flex; align-items:center; justify-content:space-between; margin-bottom:16px}
  .name{font-size:15px; font-weight:650; letter-spacing:.4px}
  .badge{font-size:11px; font-weight:650; letter-spacing:.8px; text-transform:uppercase;
         padding:4px 9px; border-radius:999px; border:1px solid var(--line); color:var(--muted)}
  .badge.on{color:var(--on); border-color:var(--on-dim); background:rgba(63,185,80,.08)}
  .readouts{display:grid; grid-template-columns:1fr 1fr 1fr; gap:10px; margin-bottom:16px}
  .ro{background:var(--panel-2); border:1px solid var(--line); border-radius:9px; padding:11px 10px}
  .ro .k{font-size:10.5px; text-transform:uppercase; letter-spacing:1px; color:var(--muted)}
  .ro .v{font-family:ui-monospace,Consolas,monospace; font-size:22px; font-weight:600;
         letter-spacing:1px; color:var(--muted); text-align:right; transition:color .25s,text-shadow .25s}
  .ro .u{font-size:12px; margin-left:2px; letter-spacing:0}
  .card.live .ro .v{color:var(--text)}
  .card.live .ro.volt .v{color:var(--on); text-shadow:0 0 12px var(--on-glow)}
  .setline{font-size:12px; color:var(--muted); font-family:ui-monospace,Consolas,monospace;
           margin-bottom:16px; min-height:15px}
  .toggle{width:100%; border:none; border-radius:9px; padding:13px; font-size:14px;
          font-weight:650; cursor:pointer; transition:background .18s,opacity .18s}
  .toggle.turn-on{background:var(--on-dim); color:#fff}
  .toggle.turn-on:hover{background:var(--on)}
  .toggle.turn-off{background:transparent; color:var(--off); border:1px solid var(--off)}
  .toggle.turn-off:hover{background:rgba(248,81,73,.1)}
  .toggle:disabled{opacity:.5; cursor:wait}
  .banner{background:rgba(248,81,73,.08); border:1px solid var(--off); color:#ffb4ae;
          border-radius:10px; padding:14px 16px; font-size:13.5px; margin-bottom:20px}
  .empty{color:var(--muted); text-align:center; padding:50px 0; font-size:14px}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div>
      <h1>Power Supply</h1>
      <div class="idn" id="idn">Connecting…</div>
    </div>
    <div class="link"><span class="lamp" id="lamp"></span><span id="linktext">—</span></div>
  </header>
  <div id="banner"></div>
  <div class="grid" id="grid"><div class="empty">Reading…</div></div>
</div>

<script>
const grid=document.getElementById('grid'), idnEl=document.getElementById('idn'),
      lamp=document.getElementById('lamp'), linktext=document.getElementById('linktext'),
      banner=document.getElementById('banner');
let busy=false;
const fmt=(v,d)=>(v===null||v===undefined)?'––':Number(v).toFixed(d);

function render(s){
  idnEl.textContent = s.idn || 'Not connected';
  lamp.className = 'lamp' + (s.connected?' ok':'');
  linktext.textContent = s.connected?'Connected':'Offline';
  if(!s.connected){
    banner.textContent = s.error || 'Bridge not responding.';
    grid.innerHTML = '<div class="empty">No channels.</div>';
    return;
  }
  banner.textContent = '';
  grid.innerHTML = s.channels.map(c=>{
    const live=c.output;
    const setline=(c.vset!=null||c.iset!=null)?'Set '+fmt(c.vset,2)+' V · '+fmt(c.iset,3)+' A':'&nbsp;';
    return `<div class="card ${live?'live':''}"><div class="rail"></div>
      <div class="chead"><span class="name">CH${c.ch}</span>
        <span class="badge ${live?'on':''}">${live?'Output on':'Output off'}</span></div>
      <div class="readouts">
        <div class="ro volt"><div class="k">Volts</div><div class="v">${fmt(c.vmeas,3)}<span class="u">V</span></div></div>
        <div class="ro"><div class="k">Amps</div><div class="v">${fmt(c.imeas,3)}<span class="u">A</span></div></div>
        <div class="ro"><div class="k">Watts</div><div class="v">${fmt(c.pmeas,2)}<span class="u">W</span></div></div>
      </div>
      <div class="setline">${setline}</div>
      <button class="toggle ${live?'turn-off':'turn-on'}" onclick="toggle(${c.ch},${live?'false':'true'})">
        ${live?'Turn output off':'Turn output on'}</button></div>`;
  }).join('');
}

async function poll(){
  if(busy) return;
  try{ render(await (await fetch('/api/status')).json()); }
  catch(e){ lamp.className='lamp'; linktext.textContent='Offline'; }
}
async function toggle(ch,on){
  busy=true; document.querySelectorAll('.toggle').forEach(b=>b.disabled=true);
  try{ render(await (await fetch('/api/output',{method:'POST',
       headers:{'Content-Type':'application/json'},
       body:JSON.stringify({ch,state:on?'on':'off'})})).json()); }catch(e){}
  busy=false;
}
poll(); setInterval(poll,1500);
</script>
</body>
</html>"""


if __name__ == "__main__":
    srv = ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), Handler)
    print(f"PSU web UI on http://{LISTEN_HOST}:{LISTEN_PORT}  ->  bridge {BRIDGE_HOST}:{BRIDGE_PORT}")
    srv.serve_forever()
