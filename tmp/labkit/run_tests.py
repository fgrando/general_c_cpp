#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Run the lab tests.

    python run_tests.py                     # every test in tests/, real bench
    python run_tests.py --fake              # no hardware: fake target
    python run_tests.py --fake --noisy      # fake target that breaks the
                                            #   quiet window (must FAIL)
    python run_tests.py -k discrete         # only tests whose name matches
    python run_tests.py -v                  # DEBUG on the console too

Individual files also run on their own, which is handy while writing one:

    python -m unittest tests.test_boot_discretes -v
"""

from __future__ import annotations

import argparse
import logging
import os
import sys
import unittest

from labharness import config, lab
from labharness.logsetup import LOG, setup_logging

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-k", "--pattern", default=None,
                    help="only run tests whose name contains this")
    ap.add_argument("-s", "--start-dir", default=os.path.join(HERE, "tests"),
                    help="directory to discover tests in (default: tests/)")
    ap.add_argument("--port", type=int, default=None, help="override the UDP port")
    ap.add_argument("--log-file", default=None,
                    help="log file path (default: %s/test-<timestamp>.log)" % config.LOG_DIR)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show DEBUG on the console too (per-packet detail)")
    ap.add_argument("--fake", action="store_true",
                    help="use the fake target instead of the real bench")
    ap.add_argument("--noisy", action="store_true",
                    help="with --fake: break the silence after discrete 2, so the "
                         "run is expected to FAIL")
    args = ap.parse_args()

    if args.port:
        config.UDP_PORT = args.port

    setup_logging(args.log_file,
                  console_level=logging.DEBUG if args.verbose else logging.INFO)

    if args.fake:
        lab.use_fake_target(config.UDP_PORT, noisy=args.noisy)
    else:
        lab.use_real_lab()

    loader = unittest.TestLoader()
    if args.pattern:
        loader.testNamePatterns = ["*%s*" % args.pattern]
    suite = loader.discover(args.start_dir, pattern="test_*.py", top_level_dir=HERE)

    LOG.info("running %d test(s) against udp port %d",
             suite.countTestCases(), config.UDP_PORT)
    result = unittest.TextTestRunner(verbosity=2, stream=sys.stdout).run(suite)

    LOG.info("result: %d run, %d failures, %d errors, %d skipped",
             result.testsRun, len(result.failures), len(result.errors),
             len(result.skipped))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
