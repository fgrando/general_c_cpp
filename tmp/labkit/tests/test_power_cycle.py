# -*- coding: utf-8 -*-
"""
Second test file -- shows how little a new test needs.

Two power cycles in a row: the target must boot cleanly both times, and go
quiet each time the power is removed.
"""

import unittest

from labharness import config
from labharness.testcase import LabTestCase


class PowerCycleTest(LabTestCase):

    CYCLES = 2

    def test_repeated_boot_is_clean(self):
        for i in range(1, self.CYCLES + 1):
            with self.step("power cycle %d/%d" % (i, self.CYCLES)):
                self.boot_target()
                self.power_off_and_settle()

    def test_boot_is_not_too_slow(self):
        import time
        t0 = time.monotonic()
        self.boot_target()
        elapsed = time.monotonic() - t0
        self.assertLess(elapsed, config.BOOT_TIMEOUT,
                        "boot took %.2fs" % elapsed)


if __name__ == "__main__":
    unittest.main()
