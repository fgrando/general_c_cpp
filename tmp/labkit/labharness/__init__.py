# -*- coding: utf-8 -*-
"""
labharness -- shared plumbing for serial-over-UDP hardware tests.

    target --serial--> serial daemon --UDP--> SerialMonitor --> your test

Modules:
    config     bench settings (ports, keywords, timeouts), env-overridable
    logsetup   console + file logging, escaping, the step() helper
    monitor    SerialMonitor and its expect/assert primitives
    lab        psu_on/psu_off/set_discrete1/2 and the backend that runs them
    testcase   LabTestCase, the base class for every test file

A new test file needs only:

    from labharness import lab
    from labharness.testcase import LabTestCase
"""

from .logsetup import (LOG, SERIAL, PSU, IO, escape_ctrl, ensure_logging,
                       setup_logging, step)
from .monitor import ExpectTimeout, SerialMonitor, UnexpectedOutput
from .testcase import LabTestCase

__all__ = [
    "LOG", "SERIAL", "PSU", "IO",
    "escape_ctrl", "ensure_logging", "setup_logging", "step",
    "SerialMonitor", "ExpectTimeout", "UnexpectedOutput",
    "LabTestCase",
]

__version__ = "1.0"
