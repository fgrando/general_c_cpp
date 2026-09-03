# -*- coding: utf-8 -*-
"""
The lab interface: the single seam between the tests and the hardware.

Tests always call the module-level functions:

    from labharness import lab
    lab.psu_on()

Those delegate to whichever backend is active, so swapping the real bench
for a fake target (or for a different PSU driver) changes nothing in the
tests. `from labharness.lab import psu_on` is safe too -- the imported name
is the wrapper, and the wrapper resolves the backend at call time.

To wire in your real bench, fill in RealLab below (e.g. by calling into
lab_instr) -- that is the only place hardware knowledge belongs.
"""

from __future__ import annotations

import socket
import threading
import time

from .logsetup import IO, LOG, PSU


class Backend(object):
    """Interface every backend implements."""
    name = "abstract"

    def psu_on(self):
        raise NotImplementedError

    def psu_off(self):
        raise NotImplementedError

    def set_discrete1(self):
        raise NotImplementedError

    def set_discrete2(self):
        raise NotImplementedError


class RealLab(Backend):
    """
    The real bench. Replace each body with the call into your instrument
    layer; keep the logging so every action lands in the test log.
    """
    name = "real"

    def psu_on(self):
        PSU.info("power ON")
        # TODO: lab_instr.psu.output(True)

    def psu_off(self):
        PSU.info("power OFF")
        # TODO: lab_instr.psu.output(False)

    def set_discrete1(self):
        IO.info("discrete 1 -> set")
        # TODO: lab_instr.dio.set("D1", True)

    def set_discrete2(self):
        IO.info("discrete 2 -> set")
        # TODO: lab_instr.dio.set("D2", True)


class FakeTarget(Backend):
    """
    Stands in for target + serial daemon so tests can run with no hardware.

    It sends the serial traffic a real target would send, over UDP to the
    port the monitor listens on -- including the boot keyword split across
    two datagrams, which is the framing case worth rehearsing.

    `noisy=True` makes discrete 2 emit a stray line, which must turn a quiet
    window into a failure.
    """
    name = "fake"

    def __init__(self, port, host="127.0.0.1", noisy=False):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.dest = (host, port)
        self.noisy = noisy

    def _send(self, text, pause=0.2):
        self.sock.sendto(text.encode(), self.dest)
        time.sleep(pause)

    def _later(self, fn, delay):
        threading.Thread(target=lambda: (time.sleep(delay), fn()),
                         name="fake-target", daemon=True).start()

    def psu_on(self):
        PSU.info("power ON (fake target)")
        self._later(self._boot, 0.4)

    def psu_off(self):
        PSU.info("power OFF (fake target)")

    def set_discrete1(self):
        IO.info("discrete 1 -> set (fake target)")
        self._later(lambda: self._send("discrete_set: D1 asserted\r\n"), 0.3)

    def set_discrete2(self):
        IO.info("discrete 2 -> set (fake target)")
        if self.noisy:
            self._later(lambda: self._send("WARN: D2 glitch\r\n"), 2.0)

    def _boot(self):
        for chunk in ["\r\nRESET: power-on\r\n",
                      "init: clocks ok\r\ninit: ram ok\r\n",
                      "init: uart ok\r\nBOOT COM",     # <- keyword split here
                      "PLETE\r\n",
                      "app: idle\r\n"]:
            self._send(chunk)


# --------------------------------------------------------------------------
# Backend selection
# --------------------------------------------------------------------------

_backend = RealLab()


def use_backend(backend):
    global _backend
    _backend = backend
    LOG.info("lab backend: %s", backend.name)
    return backend


def use_fake_target(port, host="127.0.0.1", noisy=False):
    return use_backend(FakeTarget(port, host=host, noisy=noisy))


def use_real_lab():
    return use_backend(RealLab())


def backend():
    return _backend


# -- what the tests call ----------------------------------------------------

def psu_on():
    return _backend.psu_on()


def psu_off():
    return _backend.psu_off()


def set_discrete1():
    return _backend.set_discrete1()


def set_discrete2():
    return _backend.set_discrete2()
