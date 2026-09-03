# -*- coding: utf-8 -*-
"""
LabTestCase -- the base class every test file inherits from.

It owns the boring, repeated part: logging exists, the serial monitor is
running before the target is powered, and the target is de-energised at the
end whatever happened. A new test file is then just:

    from labharness import lab
    from labharness.testcase import LabTestCase

    class MyTest(LabTestCase):
        def test_something(self):
            self.boot_target()
            with self.step("do the thing"):
                self.mon.mark()
                lab.set_discrete1()
                self.mon.expect(r"discrete_set", timeout=10)
"""

from __future__ import annotations

import unittest

from . import config, lab
from .logsetup import LOG, ensure_logging, step
from .monitor import SerialMonitor


class LabTestCase(unittest.TestCase):
    """Serial monitor + guaranteed power-down, shared by every test."""

    # Override in a subclass if one test needs its own bench settings.
    udp_port = None
    udp_host = None
    transcript = None            # e.g. "logs/%s.serial" % test name

    #: set False if a test wants to open the monitor itself
    auto_monitor = True

    step = staticmethod(step)

    @classmethod
    def setUpClass(cls):
        ensure_logging()         # no-op if the runner already configured it

    def setUp(self):
        LOG.info("=== %s.%s ===", self.__class__.__name__, self._testMethodName)
        if not self.auto_monitor:
            self.mon = None
            return
        transcript = self.transcript
        if transcript and "%s" in transcript:
            transcript = transcript % self._testMethodName
        # Listen BEFORE powering the target: UDP has no replay, so a banner
        # emitted before the socket exists is gone for good.
        self.mon = SerialMonitor(port=self.udp_port, host=self.udp_host,
                                 transcript_path=transcript)
        self.mon.start()
        self.addCleanup(self.mon.stop)
        # Registered after the monitor so it runs BEFORE the monitor stops:
        # the power-down is still visible in the serial log.
        self.addCleanup(lab.psu_off)

    # -- helpers every test tends to need --------------------------------
    def boot_target(self, timeout=None):
        """Power on and wait for the boot banner, failing fast on a panic."""
        timeout = config.BOOT_TIMEOUT if timeout is None else timeout
        with step("power on, wait for boot"):
            self.mon.mark()
            lab.psu_on()
            idx, m = self.mon.expect_any([config.BOOT_KEYWORD, config.ERROR_PATTERN],
                                         timeout=timeout)
            self.assertEqual(idx, 0, "target reported an error during boot: %r"
                             % m.group(0))
            return m

    def power_off_and_settle(self, quiet_for=None, timeout=10.0):
        """Power down and confirm the target actually stopped talking."""
        quiet_for = config.SHUTDOWN_QUIET if quiet_for is None else quiet_for
        with step("power off"):
            lab.psu_off()
            self.mon.expect_silence(quiet_for=quiet_for, timeout=timeout)

    def stimulus(self, title, action, pattern, timeout=None):
        """mark -> act -> expect, the pattern most steps follow.

        The mark() is what stops a keyword left over from an earlier step
        satisfying this one instantly.
        """
        timeout = config.DISCRETE_TIMEOUT if timeout is None else timeout
        with step(title):
            self.mon.mark()
            action()
            return self.mon.expect(pattern, timeout=timeout)

    def stimulus_quiet(self, title, action, window=None, settle=0.0):
        """act -> assert nothing comes back for `window` seconds."""
        window = config.QUIET_WINDOW if window is None else window
        with step(title):
            action()
            return self.mon.assert_quiet(window, settle=settle)
