# -*- coding: utf-8 -*-
"""
Bench configuration -- the only file you should need to edit per setup.

Every value can also be overridden with an environment variable, so Jenkins
can point a job at a different bench without touching the code:

    set LAB_UDP_PORT=5006 && python run_tests.py
"""

import os


def _int(name, default):
    return int(os.environ.get(name, default))


def _float(name, default):
    return float(os.environ.get(name, default))


def _str(name, default):
    return os.environ.get(name, default)


# -- where the serial daemon sends the forwarded serial stream --------------
UDP_HOST = _str("LAB_UDP_HOST", "")          # "" = all interfaces
UDP_PORT = _int("LAB_UDP_PORT", 5005)
ENCODING = _str("LAB_SERIAL_ENCODING", "utf-8")

# -- what the target says ---------------------------------------------------
BOOT_KEYWORD = _str("LAB_BOOT_KEYWORD", r"BOOT COMPLETE")
DISCRETE_KEYWORD = _str("LAB_DISCRETE_KEYWORD", r"discrete_set")

# Patterns that mean "the target is unhappy"; raced against the expected
# keyword so a failure is reported in seconds instead of at the timeout.
ERROR_PATTERN = _str("LAB_ERROR_PATTERN", r"(?i)\b(panic|fatal|assert|exception)\b")

# -- timing -----------------------------------------------------------------
BOOT_TIMEOUT = _float("LAB_BOOT_TIMEOUT", 30.0)
DISCRETE_TIMEOUT = _float("LAB_DISCRETE_TIMEOUT", 10.0)
QUIET_WINDOW = _float("LAB_QUIET_WINDOW", 5.0)
SHUTDOWN_QUIET = _float("LAB_SHUTDOWN_QUIET", 1.0)

# -- logging ----------------------------------------------------------------
LOG_DIR = _str("LAB_LOG_DIR", "logs")
