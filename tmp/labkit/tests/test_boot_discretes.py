# -*- coding: utf-8 -*-
"""
Boot, then the two discretes.

    power on         -> boot banner
    set_discrete1()  -> 'discrete_set' appears on the serial
    set_discrete2()  -> nothing at all for 5 s
    power off        -> target falls silent
"""

import unittest

from labharness import config, lab
from labharness.testcase import LabTestCase


class BootAndDiscretesTest(LabTestCase):

    transcript = "logs/%s.serial"        # byte-exact copy, one per test

    def test_boot_then_discretes(self):
        self.boot_target()

        self.stimulus("set discrete 1, expect %r" % config.DISCRETE_KEYWORD,
                      lab.set_discrete1,
                      config.DISCRETE_KEYWORD)

        # assert_quiet, not expect_silence: this must fail on the first byte,
        # not merely wait for chatter to stop.
        self.stimulus_quiet("set discrete 2, expect %.0fs of silence" % config.QUIET_WINDOW,
                            lab.set_discrete2)

        self.power_off_and_settle()


if __name__ == "__main__":
    unittest.main()
