#!/usr/bin/env python3
"""
warntable.py - translate a GCC/Clang build log into a warning table.

Serial builds, relative paths. Stdlib only. Line numbers are ignored on purpose.
This script ONLY parses the log into a table; comparison / waivers / scoring are
handled by other scripts.

Columns:
  built_file     the source GCC was compiling (the 'trigger' file; what gcc shows
                 in 'In file included from'). For a warning in the compiled .c
                 itself, built_file == offender_file.
  offender_file  the file the warning line actually points at (often a header).
  warning_type   the -Wflag (e.g. -Wunused-variable); '(no-flag)' if none.
  count          how many such warnings (default, aggregated mode).

Usage:
  python warntable.py build.log                 # CSV to stdout (aggregated)
  python warntable.py build.log -o warnings.csv # CSV to a file
  python warntable.py build.log --raw           # one row per warning, no count
  python warntable.py build.log --count-errors  # also include error: lines
"""

import argparse
import csv
import re
import sys
from collections import Counter

_DIAG_RE = re.compile(r':\s*(?P<kind>warning|error|note):\s*(?P<msg>.*?)\s*$')
_FLAG_RE = re.compile(r'\[(?P<flag>-[WR][^\]]+)\]\s*$')
_INCFROM_RE = re.compile(r'^\s*In file included from\s+(?P<file>.+?):\d+', re.IGNORECASE)
_HAS_C_FLAG = re.compile(r'(?:^|\s)-c(?:\s|$)')
_SRC_EXT = ('.c', '.cc', '.cpp', '.cxx', '.c++', '.cp', '.m', '.mm')
_HDR_EXT = ('.h', '.hpp', '.hh', '.hxx', '.hp', '.inc', '.ipp', '.tcc', '.def')


def norm(p):
    return p.replace('\\', '/').lstrip('./')


def is_source(path):
    return norm(path).lower().endswith(_SRC_EXT)


def is_header(path):
    return norm(path).lower().endswith(_HDR_EXT)


def parse_location(loc):
    """Peel up to two trailing numeric fields (col, line) off the right so a
    Windows drive-letter colon in the path is preserved. Returns file or None."""
    peeled = 0
    while peeled < 2:
        head, sep, tail = loc.rpartition(':')
        if sep and tail.isdigit():
            loc, peeled = head, peeled + 1
        else:
            break
    return loc if peeled else None


def compile_target(line):
    """If line is a compile command (-c ...), return its source file, else None."""
    if not _HAS_C_FLAG.search(line):
        return None
    toks = line.split()
    for i, t in enumerate(toks):
        if i > 0 and toks[i - 1] == '-o':
            continue
        if is_source(t):
            return t
    return None


def warning_type(msg):
    m = _FLAG_RE.search(msg)
    if not m:
        return '(no-flag)'
    return re.sub(r'^-Werror=', '-W', m.group('flag'))


def parse_log(path, count_errors=False):
    """Yield (built_file, offender_file, warning_type) per warning, in log order."""
    current_tu = None   # from compile-command echo
    chain_root = None   # from 'In file included from' (the TU for header warnings)
    rows = []

    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        for raw in fh:
            line = raw.rstrip('\n')

            tgt = compile_target(line)
            if tgt is not None:
                current_tu, chain_root = norm(tgt), None
                continue

            inc = _INCFROM_RE.match(line)
            if inc:
                chain_root = norm(inc.group('file'))
                continue

            m = _DIAG_RE.search(line)
            if not m:
                continue
            kind = m.group('kind')
            if kind == 'note':
                continue
            if kind == 'error' and not count_errors:
                continue

            offender = parse_location(line[:m.start()])
            if offender is None:
                continue
            offender = norm(offender)

            if is_header(offender):
                built = chain_root or current_tu or offender
            else:
                built = offender          # direct warning: the .c IS the trigger
                chain_root = None

            rows.append((built, offender, warning_type(m.group('msg'))))

    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description='Translate a GCC/Clang build log into a warning table.')
    ap.add_argument('log')
    ap.add_argument('-o', '--out', help='write CSV here (default: stdout)')
    ap.add_argument('--raw', action='store_true',
                    help='one row per warning (no count column) instead of aggregating')
    ap.add_argument('--count-errors', action='store_true', help='also include error: lines')
    args = ap.parse_args(argv)

    rows = parse_log(args.log, count_errors=args.count_errors)
    fh = open(args.out, 'w', newline='', encoding='utf-8') if args.out else sys.stdout
    try:
        w = csv.writer(fh)
        if args.raw:
            w.writerow(['built_file', 'offender_file', 'warning_type'])
            for r in rows:
                w.writerow(r)
        else:
            w.writerow(['built_file', 'offender_file', 'warning_type', 'count'])
            for (built, offender, wtype), n in sorted(Counter(rows).items()):
                w.writerow([built, offender, wtype, n])
    finally:
        if fh is not sys.stdout:
            fh.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())