#!/usr/bin/env python3
"""
udprecv.py -- receive UDP datagrams from serialudp.exe and print the
serial stream line by line.

The C bridge sends whatever bytes were in the serial buffer per datagram,
so datagram boundaries do NOT match line boundaries. This receiver
reassembles a per-sender byte buffer and emits a line only when a newline
(\\n, with optional preceding \\r) is seen.

Usage:
    python udprecv.py                       # listen 0.0.0.0:5000
    python udprecv.py --port 5000 --hex
    python udprecv.py --host 127.0.0.1 --port 5000 --logfile rx.log

Ctrl+C to quit.
"""

import argparse
import socket
import sys
from datetime import datetime


def stamp() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main() -> int:
    ap = argparse.ArgumentParser(description="UDP -> line-by-line printer")
    ap.add_argument("--host", default="0.0.0.0",
                    help="interface to bind (default 0.0.0.0)")
    ap.add_argument("--port", type=int, default=5000,
                    help="UDP port to listen on (default 5000)")
    ap.add_argument("--encoding", default="utf-8",
                    help="text decoding for lines (default utf-8)")
    ap.add_argument("--errors", default="replace",
                    help="decode error policy (default replace)")
    ap.add_argument("--hex", action="store_true",
                    help="also show each line as hex")
    ap.add_argument("--show-addr", action="store_true",
                    help="prefix each line with the sender ip:port")
    ap.add_argument("--logfile", default=None,
                    help="append printed lines to this file")
    ap.add_argument("--max-line", type=int, default=65536,
                    help="flush a buffer with no newline once it exceeds "
                         "this many bytes (default 65536)")
    args = ap.parse_args()

    logf = open(args.logfile, "a", encoding="utf-8") if args.logfile else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))
    # On Windows a blocking recvfrom() can't be interrupted by Ctrl+C until a
    # datagram arrives. A short timeout makes the call return periodically so
    # the pending KeyboardInterrupt can be delivered even when idle.
    sock.settimeout(0.5)
    print(f"{stamp()}  listening on udp {args.host}:{args.port}", file=sys.stderr)

    # one reassembly buffer per source address
    buffers: dict[tuple, bytes] = {}

    def emit(addr, raw: bytes) -> None:
        text = raw.decode(args.encoding, errors=args.errors)
        prefix = f"{addr[0]}:{addr[1]}  " if args.show_addr else ""
        line = f"{stamp()}  {prefix}{text}"
        if args.hex:
            line += "    | " + raw.hex(" ")
        print(line, flush=True)
        if logf:
            logf.write(line + "\n")
            logf.flush()

    try:
        while True:
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue  # idle tick: lets Ctrl+C be delivered on Windows
            buf = buffers.get(addr, b"") + data

            # split off every complete line; keep the trailing remainder
            *lines, rest = buf.split(b"\n")
            for chunk in lines:
                emit(addr, chunk.rstrip(b"\r"))

            # guard against an endless line (e.g. binary data, no framing)
            if len(rest) > args.max_line:
                emit(addr, rest)
                rest = b""

            buffers[addr] = rest
    except KeyboardInterrupt:
        print(f"\n{stamp()}  stopping", file=sys.stderr)
        # flush any partial trailing data so nothing is silently lost
        for addr, rest in buffers.items():
            if rest:
                emit(addr, rest)
    finally:
        sock.close()
        if logf:
            logf.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())