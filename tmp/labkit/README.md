# labharness

Shared plumbing for serial-over-UDP hardware tests.

```
target --serial--> serial daemon --UDP--> SerialMonitor --> your test
```

## Layout

```
labharness/            <- the common part, included by every test
    config.py          bench settings: ports, keywords, timeouts (env-overridable)
    logsetup.py        console + file logging, control-char escaping, step()
    monitor.py         SerialMonitor: expect / expect_any / expect_silence / assert_quiet
    lab.py             psu_on, psu_off, set_discrete1/2 + the backend behind them
    testcase.py        LabTestCase: the base class every test inherits
tests/                 <- one file per test
    test_boot_discretes.py
    test_power_cycle.py
run_tests.py           discovery + logging + backend selection
```

## Running

```
python run_tests.py                 # every test, real bench
python run_tests.py --fake          # no hardware: fake target
python run_tests.py --fake --noisy  # fake target that breaks the quiet window (must FAIL)
python run_tests.py -k discrete     # only matching tests
python run_tests.py -v              # DEBUG on the console (per-packet detail)

python -m unittest tests.test_boot_discretes -v     # a single file
```

Logs go to the console and to `logs/test-<timestamp>.log` at once. A test
that sets `transcript = "logs/%s.serial"` also gets a byte-exact copy of
its serial stream.

## Adding a test

Create `tests/test_<something>.py`:

```python
from labharness import config, lab
from labharness.testcase import LabTestCase


class MyTest(LabTestCase):

    def test_something(self):
        self.boot_target()

        self.stimulus("set discrete 1, expect ack",
                      lab.set_discrete1,
                      config.DISCRETE_KEYWORD)

        self.stimulus_quiet("set discrete 2, expect silence",
                            lab.set_discrete2)

        self.power_off_and_settle()
```

`LabTestCase.setUp` starts the monitor **before** anything powers the
target, and registers `psu_off` as a cleanup, so a crashed test never
leaves the target energised.

## Wiring in the real bench

`labharness/lab.py` is the only file that knows about hardware. Fill in the
four methods of `RealLab` (e.g. calls into `lab_instr`), keep the logging
lines, and every test picks it up. Tests never import the driver.

## The two silence checks are not the same

- `expect_silence(quiet_for)` waits for the target to **stop** talking, and
  restarts its clock on every byte. Use it after power-off.
- `assert_quiet(window)` asserts **nothing at all** arrives during the
  window, and fails on the first byte. Use it when a stimulus must produce
  no output.

Using the first where you meant the second gives you a test that passes on
a target that chatters and then goes quiet.

## Settings

Everything in `config.py` reads an environment variable first, so Jenkins
can retarget a job without code changes:

```
LAB_UDP_PORT, LAB_UDP_HOST, LAB_SERIAL_ENCODING,
LAB_BOOT_KEYWORD, LAB_DISCRETE_KEYWORD, LAB_ERROR_PATTERN,
LAB_BOOT_TIMEOUT, LAB_DISCRETE_TIMEOUT, LAB_QUIET_WINDOW, LAB_SHUTDOWN_QUIET,
LAB_LOG_DIR
```
