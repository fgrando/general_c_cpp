#!/usr/bin/env python3
"""
powercontrol2udp.py -- UDP bridge between the web UI and a Rigol power supply.

The Python counterpart of powercontrol2udp.c: a UDP server speaking the same
protocol as the mock, driving the instrument over pyvisa/SCPI instead of
faking it. Drop-in replacement for mock_powercontrol2udp.py.

Protocol (request datagram -> JSON reply):
    "status"            -> status of all channels
    "output <ch> on"    -> set output, reply status
    "output <ch> off"   -> set output, reply status
  Status JSON:
    {"connected":true,"idn":"...","channels":[
        {"ch":1,"output":true,"vmeas":5.0,"imeas":0.42,"pmeas":2.1,"vset":5.0,"iset":1.0}, ...]}

Dependencies:
    pip install pyvisa
    # Plus ONE VISA backend:
    #   Native (recommended on the lab PC): NI-VISA or Rigol UltraSigma; VISA_BACKEND = ""
    #   Pure-Python: pip install pyvisa-py pyusb   and set VISA_BACKEND = "@py"

Run:
    python powercontrol2udp.py
    # then run psu_web.py (points at 127.0.0.1:5005 by default)
"""

import json
import socket

import pyvisa

# ------------------------- configuration ---------------------------------
BIND_HOST = "127.0.0.1"    # use "0.0.0.0" to reach it from other hosts
BIND_PORT = 5005
VISA_BACKEND = ""          # "" = native visa32.dll; "@py" = pyvisa-py
RESOURCE = None            # None = auto-detect; or "USB0::0x1AB1::...::INSTR"
MAX_CHANNELS = 3           # probe CH1..CH<MAX> at connect
RIGOL_VID = "0x1AB1"       # Rigol USB vendor ID (verified)
IO_TIMEOUT_MS = 3000
# -------------------------------------------------------------------------


class Psu:
    """Owns one VISA session and the list of active channels."""

    def __init__(self):
        self.rm = None
        self.dev = None
        self.idn = ""
        self.resource = ""
        self.channels = []
        self.error = ""

    # -- connection -------------------------------------------------------
    def _find_resource(self):
        if RESOURCE:
            return RESOURCE
        for res in self.rm.list_resources():
            if res.upper().startswith("USB") and RIGOL_VID.upper() in res.upper():
                return res
        for res in self.rm.list_resources():          # fallback: probe *IDN?
            if not res.upper().startswith("USB"):
                continue
            try:
                d = self.rm.open_resource(res)
                idn = d.query("*IDN?")
                d.close()
                if "RIGOL" in idn.upper():
                    return res
            except Exception:
                pass
        return None

    def connect(self):
        self.close()
        self.error = ""
        try:
            self.rm = pyvisa.ResourceManager(VISA_BACKEND)
            self.resource = self._find_resource()
            if not self.resource:
                self.error = "No Rigol PSU found on USB."
                return False
            self.dev = self.rm.open_resource(self.resource)
            self.dev.timeout = IO_TIMEOUT_MS
            self.idn = self.dev.query("*IDN?").strip()
            self.channels = []
            for n in range(1, MAX_CHANNELS + 1):
                try:
                    if self.dev.query(f":OUTP? CH{n}").strip():
                        self.channels.append(n)
                except Exception:
                    break
            if not self.channels:
                self.channels = [1]     # assume single-channel
            return True
        except Exception as e:
            self.error = str(e)
            return False

    def close(self):
        for obj in (self.dev, self.rm):
            try:
                if obj:
                    obj.close()
            except Exception:
                pass
        self.dev = self.rm = None

    def _ensure(self):
        return True if self.dev is not None else self.connect()

    # -- helpers ----------------------------------------------------------
    def _q(self, cmd):
        try:
            return self.dev.query(cmd).strip()
        except Exception:
            return None

    @staticmethod
    def _num(s):
        try:
            return round(float(s), 4)
        except (TypeError, ValueError):
            return None

    # -- public API -------------------------------------------------------
    def status(self):
        if not self._ensure():
            return {"connected": False, "error": self.error, "channels": []}
        chans = []
        for n in self.channels:
            out = self._q(f":OUTP? CH{n}")
            if out is None:                 # link dropped
                self.close()
                return {"connected": False,
                        "error": "Lost connection to instrument.",
                        "channels": []}
            chans.append({
                "ch": n,
                "output": out.upper().startswith("ON") or out == "1",
                "vmeas": self._num(self._q(f":MEAS:VOLT? CH{n}")),
                "imeas": self._num(self._q(f":MEAS:CURR? CH{n}")),
                "pmeas": self._num(self._q(f":MEAS:POWE? CH{n}")),
                "vset": self._num(self._q(f":SOUR{n}:VOLT?")),
                "iset": self._num(self._q(f":SOUR{n}:CURR?")),
            })
        return {"connected": True, "idn": self.idn,
                "resource": self.resource, "channels": chans}

    def set_output(self, ch, on):
        if not self._ensure():
            return
        try:
            # DP800/DP900/DP2000 syntax. Adjust if your model differs.
            self.dev.write(f":OUTP CH{ch},{'ON' if on else 'OFF'}")
        except Exception:
            self.close()


def handle(psu, req):
    """Parse a request datagram, act, and return JSON reply bytes."""
    parts = req.strip().lower().split()
    if parts and parts[0] == "output" and len(parts) == 3:
        try:
            psu.set_output(int(parts[1]), parts[2] == "on")
        except ValueError:
            pass
    # every command replies with fresh status
    return json.dumps(psu.status()).encode()


def main():
    psu = Psu()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((BIND_HOST, BIND_PORT))

    if psu.connect():
        print(f"powercontrol2udp on {BIND_HOST}:{BIND_PORT}  ->  {psu.idn}")
    else:
        print(f"powercontrol2udp on {BIND_HOST}:{BIND_PORT}  "
              f"(no PSU yet: {psu.error}; will retry on request)")

    while True:
        data, addr = s.recvfrom(65535)
        reply = handle(psu, data.decode(errors="replace"))
        s.sendto(reply, addr)


if __name__ == "__main__":
    main()
#pip install pyvisa        # + NI-VISA/UltraSigma, or pyvisa-py+pyusb with VISA_BACKEND="@py"
#python powercontrol2udp.py
#python psu_web.py         # unchanged