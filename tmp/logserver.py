#!/usr/bin/env python3
"""
logserver.py - Centralized UDP log collector for the serial-to-UDP bridges.

Each bridge just sends plain-text log lines over UDP (sendto). This server:
  - receives datagrams (one or more newline-separated lines each),
  - tags every line with the source bridge (UDP src ip:port, or a name map),
  - parses an optional leading level token (INFO if absent),
  - fans out to console + a rotating central file, and optionally to a
    separate rotating file per source bridge.

Wire format (all lenient, nothing required):
    "<message>"                 -> level INFO
    "WARNING link down"         -> level WARNING, msg "link down"
    "ERR crc mismatch 0x3f"     -> level ERROR,   msg "crc mismatch 0x3f"
Levels (case-insensitive): DEBUG, INFO, NOTICE, WARN/WARNING,
                           ERR/ERROR, CRIT/CRITICAL.

Requires Python 3.8+ (UDP asyncio endpoints on Windows need 3.8+).

Examples:
    python logserver.py
    python logserver.py --host 0.0.0.0 --port 9999 --logdir ./logs --per-source
    python logserver.py --name 192.168.1.50:*=uart2 --name 192.168.1.51:*=can0

Quick test (no bridge needed):
    printf 'INFO hello from test\n' | nc -u -w0 127.0.0.1 9999
    PowerShell:  (New-Object Net.Sockets.UdpClient).Send(
                   [Text.Encoding]::ASCII.GetBytes("WARN test`n"),12,"127.0.0.1",9999)
"""

import argparse
import asyncio
import logging
import os
import re
import sys
import time
from logging.handlers import RotatingFileHandler

# ---- level parsing -------------------------------------------------------

logging.addLevelName(25, "NOTICE")  # syslog-style notice, between INFO and WARNING

LEVELS = {
    "DEBUG": logging.DEBUG,
    "INFO": logging.INFO,
    "NOTICE": 25,
    "WARN": logging.WARNING,
    "WARNING": logging.WARNING,
    "ERR": logging.ERROR,
    "ERROR": logging.ERROR,
    "CRIT": logging.CRITICAL,
    "CRITICAL": logging.CRITICAL,
}


def parse_line(line):
    """Return (level:int, message:str) from a raw log line."""
    parts = line.split(None, 1)
    if parts and parts[0].upper() in LEVELS:
        level = LEVELS[parts[0].upper()]
        msg = parts[1] if len(parts) > 1 else ""
        return level, msg
    return logging.INFO, line


# ---- helpers -------------------------------------------------------------

def sanitize(text):
    """Make a string safe to use in a filename (Windows-safe)."""
    return re.sub(r"[^A-Za-z0-9._-]", "_", text)


class SourceDefaultFilter(logging.Filter):
    """Guarantee every record has a `source` attribute so the formatter
    never blows up on a stray internal log call."""
    def filter(self, record):
        if not hasattr(record, "source"):
            record.source = "-"
        return True


def build_formatter(utc):
    fmt = logging.Formatter(
        "%(asctime)s %(levelname)-7s [%(source)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    if utc:
        fmt.converter = time.gmtime
    return fmt


def make_rotating_handler(path, max_bytes, backups, formatter):
    h = RotatingFileHandler(
        path, maxBytes=max_bytes, backupCount=backups, encoding="utf-8"
    )
    h.setFormatter(formatter)
    h.addFilter(SourceDefaultFilter())
    return h


# ---- name mapping (src -> friendly bridge name) --------------------------

def build_name_map(entries):
    """
    entries: list of "PATTERN=NAME" where PATTERN is host or host:port,
    and '*' is allowed for the port (or host). Returns a matcher function
    src(ip,port) -> name|None.
    """
    rules = []  # (host_or_None, port_or_None, name)
    for e in entries or []:
        if "=" not in e:
            raise ValueError(f"--name needs PATTERN=NAME, got: {e!r}")
        pat, name = e.split("=", 1)
        host, _, port = pat.partition(":")
        host = None if host in ("", "*") else host
        port = None if port in ("", "*") else int(port)
        rules.append((host, port, name.strip()))

    def match(ip, port):
        for host, rport, name in rules:
            if (host is None or host == ip) and (rport is None or rport == port):
                return name
        return None

    return match


# ---- the UDP protocol ----------------------------------------------------

class LogProtocol(asyncio.DatagramProtocol):
    def __init__(self, central, args, formatter, name_match):
        self.central = central
        self.args = args
        self.formatter = formatter
        self.name_match = name_match
        self._per_source = {}   # source-key -> Logger
        self._internal = logging.getLogger("logserver")

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        ip, port = addr[0], addr[1]
        name = self.name_match(ip, port)
        source = name if name else f"{ip}:{port}"

        try:
            text = data.decode("utf-8", errors="replace")
        except Exception:  # extremely defensive; decode with replace won't raise
            text = repr(data)

        # A datagram may carry several lines, or a trailing newline.
        for raw in text.replace("\r", "").split("\n"):
            if raw == "":
                continue
            level, msg = parse_line(raw)
            self.central.log(level, msg, extra={"source": source})
            if self.args.per_source:
                self._source_logger(source).log(
                    level, msg, extra={"source": source}
                )

    def _source_logger(self, source):
        lg = self._per_source.get(source)
        if lg is None:
            lg = logging.getLogger(f"src.{source}")
            lg.setLevel(logging.DEBUG)
            lg.propagate = False
            path = os.path.join(
                self.args.logdir, f"{sanitize(source)}.log"
            )
            lg.addHandler(make_rotating_handler(
                path, self.args.max_bytes, self.args.backups, self.formatter
            ))
            self._per_source[source] = lg
        return lg

    def error_received(self, exc):
        # On Windows an ICMP port-unreachable can surface here; log and continue.
        self._internal.warning("socket error_received: %s", exc)


# ---- setup / main --------------------------------------------------------

def setup_central(args, formatter):
    logger = logging.getLogger("central")
    logger.setLevel(logging.DEBUG)
    logger.propagate = False
    logger.addFilter(SourceDefaultFilter())

    logger.addHandler(make_rotating_handler(
        os.path.join(args.logdir, "central.log"),
        args.max_bytes, args.backups, formatter,
    ))

    if not args.no_console:
        con = logging.StreamHandler(sys.stdout)
        con.setFormatter(formatter)
        con.addFilter(SourceDefaultFilter())
        logger.addHandler(con)

    return logger


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Centralized UDP log collector.")
    p.add_argument("--host", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    p.add_argument("--port", type=int, default=9999, help="bind port (default 9999)")
    p.add_argument("--logdir", default="./logs", help="directory for log files")
    p.add_argument("--max-bytes", type=int, default=5 * 1024 * 1024,
                   dest="max_bytes", help="rotate after N bytes (default 5 MiB)")
    p.add_argument("--backups", type=int, default=5,
                   help="rotated files to keep (default 5)")
    p.add_argument("--per-source", action="store_true",
                   help="also write a separate file per bridge")
    p.add_argument("--no-console", action="store_true",
                   help="do not echo to stdout")
    p.add_argument("--utc", action="store_true", help="timestamp in UTC")
    p.add_argument("--name", action="append", metavar="PATTERN=NAME",
                   help="map a source to a friendly name, e.g. "
                        "192.168.1.50:*=uart2 (repeatable)")
    return p.parse_args(argv)


async def run(args):
    formatter = build_formatter(args.utc)
    central = setup_central(args, formatter)
    name_match = build_name_map(args.name)

    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: LogProtocol(central, args, formatter, name_match),
        local_addr=(args.host, args.port),
    )
    central.info("log collector listening on %s:%d (logdir=%s)",
                 args.host, args.port, os.path.abspath(args.logdir),
                 extra={"source": "logserver"})
    try:
        await asyncio.Event().wait()   # run until interrupted
    finally:
        transport.close()


def main():
    args = parse_args()
    os.makedirs(args.logdir, exist_ok=True)
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        logging.getLogger("central").info(
            "shutting down", extra={"source": "logserver"})


if __name__ == "__main__":
    main()