# -*- coding: utf-8 -*-
"""
Logging for the whole harness: everything goes to the console and to a file.

Loggers used across the package:

    test    test steps and expect() results
    serial  the target's serial output, one line per line
    psu     power supply actions
    io      discretes and other lab I/O
"""

from __future__ import annotations

import logging
import os
import re
import sys
import time
from contextlib import contextmanager
from datetime import datetime

from . import config

LOG = logging.getLogger("test")
SERIAL = logging.getLogger("serial")
PSU = logging.getLogger("psu")
IO = logging.getLogger("io")

# Printable characters are kept as-is; everything else is escaped so a stray
# 0x00 or an ANSI sequence cannot mangle the log file.
_CTRL = re.compile(r"[^\x20-\x7e\t]")

_log_file = None


def escape_ctrl(text):
    def esc(m):
        cp = ord(m.group(0))
        # A byte the decoder could not map shows up as U+FFFD; keep it
        # distinguishable from a real 0xFD byte.
        return "\\x%02x" % cp if cp < 0x100 else "\\u%04x" % cp
    return _CTRL.sub(esc, text)


def setup_logging(log_file=None, log_dir=None, console_level=logging.INFO,
                  file_level=logging.DEBUG):
    """
    Send every log record to both the console and a file.

    Returns the log file path. Safe to call again: handlers are replaced,
    not duplicated.
    """
    global _log_file

    if log_file is None:
        log_dir = log_dir or config.LOG_DIR
        os.makedirs(log_dir, exist_ok=True)
        log_file = os.path.join(
            log_dir, "test-%s.log" % datetime.now().strftime("%Y%m%d-%H%M%S"))
    else:
        parent = os.path.dirname(os.path.abspath(log_file))
        if parent:
            os.makedirs(parent, exist_ok=True)

    root = logging.getLogger()
    root.setLevel(min(console_level, file_level))
    for h in list(root.handlers):
        root.removeHandler(h)
        h.close()

    console = logging.StreamHandler(sys.stdout)
    console.setLevel(console_level)
    console.setFormatter(logging.Formatter(
        "%(asctime)s.%(msecs)03d %(levelname)-7s %(name)-6s %(message)s",
        datefmt="%H:%M:%S"))
    root.addHandler(console)

    # errors="backslashreplace": a corrupted serial byte must never kill the
    # test run with a UnicodeEncodeError.
    fileh = logging.FileHandler(log_file, mode="w", encoding="utf-8",
                                errors="backslashreplace")
    fileh.setLevel(file_level)
    fileh.setFormatter(logging.Formatter(
        "%(asctime)s.%(msecs)03d %(levelname)-7s %(name)-6s %(threadName)-12s %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S"))
    root.addHandler(fileh)

    # The Windows console is often cp1252 and chokes on non-ASCII bytes.
    try:
        sys.stdout.reconfigure(errors="backslashreplace")
    except (AttributeError, ValueError):
        pass

    _log_file = log_file
    LOG.info("logging to %s", os.path.abspath(log_file))
    return log_file


def ensure_logging():
    """Set logging up unless someone already did (e.g. the runner, or Jenkins)."""
    if not logging.getLogger().handlers:
        return setup_logging()
    return _log_file


def log_file():
    return _log_file


@contextmanager
def step(title):
    """Bracket a test step in the log, with its duration."""
    LOG.info("---- STEP: %s ----", title)
    t0 = time.monotonic()
    try:
        yield
    except Exception as exc:
        LOG.error("STEP FAILED after %.2fs: %s -- %s",
                  time.monotonic() - t0, title, exc.__class__.__name__)
        raise
    LOG.info("---- OK (%.2fs): %s ----", time.monotonic() - t0, title)
