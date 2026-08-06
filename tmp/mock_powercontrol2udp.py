#!/usr/bin/env python3
"""
mock_powercontrol2udp.py -- Fake UDP power-control bridge for testing psu_web.py.

Speaks the same UDP protocol the real powercontrol2udp will speak, but keeps
fake channel state instead of talking to a PSU. Lets you click the web page
end-to-end with no hardware.

Protocol (request datagram -> JSON reply):
    "status"            -> status of all channels
    "output <ch> on"    -> set channel output, reply status
    "output <ch> off"   -> set channel output, reply status

Run (in a second terminal, before/with psu_web.py):
    python mock_powercontrol2udp.py

To build the REAL bridge: keep this request/reply shape, and replace
`state`/measurements with pyvisa queries to the instrument.
"""

import json
import random
import socket

BIND_HOST = "127.0.0.1"
BIND_PORT = 5005
IDN = "RIGOL TECHNOLOGIES,DP832,MOCK0001,00.01 (mock)"

# Fake channels: setpoints + current output state.
state = {
    1: {"output": False, "vset": 5.00, "iset": 1.000},
    2: {"output": False, "vset": 12.00, "iset": 0.500},
    3: {"output": False, "vset": 3.30, "iset": 2.000},
}


def measure(ch):
    """Return a plausible (vmeas, imeas, pmeas) with a little jitter."""
    cfg = state[ch]
    if not cfg["output"]:
        return 0.0, 0.0, 0.0
    v = cfg["vset"] - random.uniform(0.0, 0.03)              # tiny regulation drop
    i = min(cfg["iset"], cfg["iset"] * random.uniform(0.40, 0.75))  # fake load
    return round(v, 3), round(i, 3), round(v * i, 2)


def status_json():
    chans = []
    for ch in sorted(state):
        v, i, p = measure(ch)
        chans.append({
            "ch": ch,
            "output": state[ch]["output"],
            "vmeas": v, "imeas": i, "pmeas": p,
            "vset": state[ch]["vset"], "iset": state[ch]["iset"],
        })
    return json.dumps({"connected": True, "idn": IDN, "channels": chans}).encode()


def handle(cmd):
    parts = cmd.strip().lower().split()
    if not parts:
        return json.dumps({"connected": True, "idn": IDN, "channels": []}).encode()
    if parts[0] == "status":
        return status_json()
    if parts[0] == "output" and len(parts) == 3:
        try:
            ch = int(parts[1])
            if ch in state:
                state[ch]["output"] = (parts[2] == "on")
        except ValueError:
            pass
        return status_json()
    return json.dumps({"connected": True, "idn": IDN,
                       "error": f"unknown command: {cmd!r}", "channels": []}).encode()


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((BIND_HOST, BIND_PORT))
    print(f"mock powercontrol2udp listening on {BIND_HOST}:{BIND_PORT}")
    while True:
        data, addr = s.recvfrom(65535)
        s.sendto(handle(data.decode(errors="replace")), addr)


if __name__ == "__main__":
    main()
