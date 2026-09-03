# -*- coding: utf-8 -*-
"""
SerialMonitor -- receives the target's serial stream from the daemon's UDP
port, logs it line by line, and lets a test wait for patterns in it.

    target --serial--> serial daemon --UDP datagrams--> SerialMonitor

The daemon relays bytes, so a datagram boundary has nothing to do with a
line boundary: "BOOT COMPLETE" can arrive as "...BOOT COM" + "PLETE\r\n".
Everything received is therefore appended to one growing buffer and matched
against the buffer, never against individual packets.
"""

from __future__ import annotations

import os
import re
import socket
import threading
import time

from . import config
from .logsetup import LOG, SERIAL, escape_ctrl


class ExpectTimeout(AssertionError):
    """A pattern did not show up in time.

    Deliberately an AssertionError so unittest reports a FAILURE (the target
    misbehaved) rather than an ERROR (the harness broke).
    """


class UnexpectedOutput(AssertionError):
    """The target talked during a window in which it had to stay quiet."""


class SerialMonitor(object):
    """
    A background thread keeps draining the socket the whole time, so output
    produced while the test is busy doing something else is never lost --
    which is exactly what a blocking recvfrom() in the test body would do.

        with SerialMonitor() as mon:
            lab.psu_on()
            mon.expect(r"BOOT COMPLETE", timeout=30)
    """

    def __init__(self, port=None, host=None, encoding=None,
                 transcript_path=None, bufsize=65535):
        self.addr = (config.UDP_HOST if host is None else host,
                     config.UDP_PORT if port is None else port)
        self.encoding = encoding or config.ENCODING
        self.bufsize = bufsize
        self.transcript_path = transcript_path

        self._sock = None
        self._thread = None
        self._stop = threading.Event()
        self._cond = threading.Condition()
        self._text = ""          # everything received, decoded
        self._pos = 0            # expect() searches from here
        self._line_buf = ""      # partial line not yet logged
        self._transcript = None
        self._packets = 0
        self._bytes = 0
        self._error = None       # exception from the reader thread, if any

    # -- lifecycle -------------------------------------------------------
    def start(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # Grow the kernel receive buffer: a chatty boot log can overrun the
        # default while the test is busy, and UDP drops silently.
        try:
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        except OSError:
            pass
        self._sock.bind(self.addr)
        self._sock.settimeout(0.2)          # so the thread can notice _stop

        if self.transcript_path:
            self._transcript = open(self.transcript_path, "w", encoding=self.encoding,
                                    errors="backslashreplace", newline="")
            LOG.info("raw transcript: %s", os.path.abspath(self.transcript_path))

        self._thread = threading.Thread(target=self._reader, name="serial-rx", daemon=True)
        self._thread.start()
        LOG.info("listening on udp %s:%d", self.addr[0] or "0.0.0.0", self.addr[1])
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        with self._cond:
            if self._line_buf:              # log whatever never got a newline
                SERIAL.info("%s  <partial line>", escape_ctrl(self._line_buf))
                self._line_buf = ""
        if self._sock:
            self._sock.close()
        if self._transcript:
            self._transcript.close()
        LOG.info("monitor stopped (%d packets, %d bytes)", self._packets, self._bytes)

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()
        return False

    # -- reader thread ---------------------------------------------------
    def _reader(self):
        while not self._stop.is_set():
            try:
                data, peer = self._sock.recvfrom(self.bufsize)
            except socket.timeout:
                continue
            except ConnectionResetError:
                # Windows raises this on a UDP socket after an ICMP
                # port-unreachable; not fatal for a receive-only socket.
                continue
            except OSError as exc:
                if not self._stop.is_set():
                    self._error = exc
                    LOG.error("socket error: %s", exc)
                return

            chunk = data.decode(self.encoding, "replace")
            with self._cond:
                self._text += chunk
                self._packets += 1
                self._bytes += len(data)
                if self._transcript:
                    self._transcript.write(chunk)
                    self._transcript.flush()
                # Log the received lines BEFORE waking the waiters, so the log
                # never shows "matched X" above the line that contained X.
                SERIAL.debug("packet from %s:%d, %d bytes", peer[0], peer[1], len(data))
                for line in self._take_lines(chunk):
                    SERIAL.info("%s", escape_ctrl(line))
                self._cond.notify_all()

    def _take_lines(self, chunk):
        """Re-assemble complete lines across datagram boundaries.

        Returns the lines that are now complete; the tail stays buffered
        until its terminator arrives. Handles \\r\\n, \\n and bare \\r.
        """
        self._line_buf += chunk
        if not re.search(r"[\r\n]", self._line_buf):
            return []
        parts = re.split(r"\r\n|\n|\r", self._line_buf)
        self._line_buf = parts.pop()        # trailing piece: incomplete
        return [p for p in parts if p != ""]

    # -- test API --------------------------------------------------------
    def expect(self, pattern, timeout=30.0, flags=re.MULTILINE):
        """
        Block until `pattern` (regex or plain string) appears, and return the
        re.Match. Only text received after the previous match is considered,
        so repeated calls walk forward through the log.

        The timeout is one absolute deadline for the whole wait, not a
        per-datagram timeout.
        """
        rx = pattern if hasattr(pattern, "search") else re.compile(pattern, flags)
        LOG.info("waiting for %r (timeout %.1fs)", rx.pattern, timeout)
        t0 = time.monotonic()
        deadline = t0 + timeout
        with self._cond:
            while True:
                if self._error:
                    raise self._error
                m = rx.search(self._text, self._pos)
                if m:
                    # Advance past the match, but never past a zero-width one.
                    self._pos = max(m.end(), m.start() + 1)
                    LOG.info("matched %r after %.2fs", escape_ctrl(m.group(0)),
                             time.monotonic() - t0)
                    return m
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    msg = ("timed out after %.1fs waiting for %r\n"
                           "--- last 800 chars received (%d packets total) ---\n%s"
                           % (timeout, rx.pattern, self._packets,
                              escape_ctrl(self._tail(800))))
                    LOG.error("%s", msg)
                    raise ExpectTimeout(msg)
                self._cond.wait(remaining)

    def expect_any(self, patterns, timeout=30.0):
        """Wait for whichever of several patterns appears first -> (index, match).

        Use it when the target may answer with either a success or an error
        banner: waiting for the success pattern alone burns the full timeout
        on every failure.
        """
        rxs = [p if hasattr(p, "search") else re.compile(p, re.MULTILINE) for p in patterns]
        LOG.info("waiting for any of %r (timeout %.1fs)", [r.pattern for r in rxs], timeout)
        t0 = time.monotonic()
        deadline = t0 + timeout
        with self._cond:
            while True:
                best = None
                for i, rx in enumerate(rxs):
                    m = rx.search(self._text, self._pos)
                    if m and (best is None or m.start() < best[1].start()):
                        best = (i, m)
                if best:
                    self._pos = max(best[1].end(), best[1].start() + 1)
                    LOG.info("matched pattern #%d %r after %.2fs", best[0],
                             escape_ctrl(best[1].group(0)), time.monotonic() - t0)
                    return best
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    msg = ("timed out after %.1fs waiting for any of %r\n--- tail ---\n%s"
                           % (timeout, [r.pattern for r in rxs], escape_ctrl(self._tail(800))))
                    LOG.error("%s", msg)
                    raise ExpectTimeout(msg)
                self._cond.wait(remaining)

    def expect_silence(self, quiet_for=2.0, timeout=15.0):
        """Wait until nothing has arrived for `quiet_for` seconds.

        For "the target has stopped talking" (e.g. after power off). For
        "the stimulus must produce no output at all", use assert_quiet().
        """
        LOG.info("waiting for %.1fs of silence (timeout %.1fs)", quiet_for, timeout)
        deadline = time.monotonic() + timeout
        with self._cond:
            last_len = len(self._text)
            quiet_since = time.monotonic()
            while True:
                if len(self._text) != last_len:
                    last_len = len(self._text)
                    quiet_since = time.monotonic()
                if time.monotonic() - quiet_since >= quiet_for:
                    LOG.info("target silent for %.1fs", quiet_for)
                    return True
                if time.monotonic() > deadline:
                    msg = "target still transmitting after %.1fs" % timeout
                    LOG.error("%s", msg)
                    raise ExpectTimeout(msg)
                self._cond.wait(0.2)

    def assert_quiet(self, window=5.0, settle=0.0):
        """
        Assert that NOTHING arrives for `window` seconds, failing as soon as
        something does.

        Not expect_silence(): that one restarts its clock on every byte, so
        it would pass on a target that chatters and then goes quiet.

        `settle` ignores output during the first N seconds after the
        stimulus -- useful if the target echoes the command itself.
        """
        if settle > 0:
            LOG.debug("settling for %.2fs before the quiet window", settle)
            time.sleep(settle)
        LOG.info("asserting no serial output for %.1fs", window)
        deadline = time.monotonic() + window
        with self._cond:
            self._pos = len(self._text)        # only judge what comes next
            start_len = len(self._text)
            while True:
                if len(self._text) != start_len:
                    unexpected = self._text[start_len:]
                    msg = ("target sent %d unexpected chars %.2fs into a %.1fs quiet "
                           "window: %r" % (len(unexpected),
                                           window - (deadline - time.monotonic()),
                                           window,
                                           escape_ctrl(unexpected[:400])))
                    LOG.error("%s", msg)
                    raise UnexpectedOutput(msg)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    LOG.info("quiet as expected for %.1fs", window)
                    self._pos = len(self._text)
                    return True
                self._cond.wait(remaining)

    def mark(self):
        """Ignore everything received so far.

        Call it right before a stimulus, so a keyword left over from an
        earlier step cannot satisfy the next expect().
        """
        with self._cond:
            skipped = len(self._text) - self._pos
            self._pos = len(self._text)
        LOG.debug("mark: ignoring %d buffered chars", skipped)

    def text(self):
        with self._cond:
            return self._text

    def _tail(self, n):
        return self._text[-n:]
