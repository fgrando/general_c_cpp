#!/usr/bin/env python3
"""
warntable.py - translate a GCC/Clang build log into a warning table.

Serial builds, relative paths. Stdlib only. This script ONLY parses the log into
a table; comparison / scoring live in other scripts.

Columns:
  built_file     the source GCC was compiling (the 'trigger' file; the root shown
                 in 'In file included from'). For a warning in the compiled .c
                 itself, built_file == offender_file.
  offender_file  the file the warning line points at (often a header).
  offender_line  the line number in the offender file.
  warning_type   the -Wflag (e.g. -Wunused-variable); '(no-flag)' if none.
  count          how many such warnings (aggregated mode; sum = overall total).
  sample         the full log line the warning was reported on. In aggregated
                 mode (count > 1) this is the last matching line seen.

Waivers (--waivers FILE): CSV rows  file,line,warning  matched against
(offender_file, offender_line, warning_type). A matching warning is dropped.
  * line blank or '*'    -> any line (use for third-party code that drifts)
  * warning blank or '*' -> any warning type on that file[:line]
  * file is matched as a path suffix, so 'bad.h' matches 'include/bad.h'
  * lines starting with '#' and blank lines are ignored; an optional
    'file,line,warning' header row is skipped.

Usage:
  python warntable.py build.log
  python warntable.py build.log -o warnings.csv
  python warntable.py build.log --waivers waivers.csv
  python warntable.py build.log --raw          # one row per warning, no count
  python warntable.py build.log --count-errors
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


def norm(p):
    return p.replace('\\', '/').lstrip('./')


def is_source(path):
    return norm(path).lower().endswith(_SRC_EXT)


def parse_location(loc):
    """Peel up to two trailing numeric fields off the right (col, then line), so a
    Windows drive-letter colon in the path survives. Returns (file, line) or (None, None)."""
    nums = []
    while len(nums) < 2:
        head, sep, tail = loc.rpartition(':')
        if sep and tail.isdigit():
            nums.append(tail)   # nums[0]=col (if two present), last=line
            loc = head
        else:
            break
    if not nums:
        return None, None
    line = nums[1] if len(nums) == 2 else nums[0]
    return loc, line


def compile_target(line):
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


def load_waivers(path):
    """Return list of (file_norm, line_or_None, wtype_or_None)."""
    waivers = []
    with open(path, encoding='utf-8') as fh:
        for raw in fh:
            s = raw.strip()
            if not s or s.startswith('#'):
                continue
            parts = [p.strip() for p in s.split(',')]
            f = parts[0]
            if f.lower() == 'file':          # header row
                continue
            ln = parts[1] if len(parts) > 1 else ''
            wt = parts[2] if len(parts) > 2 else ''
            waivers.append((
                norm(f),
                None if ln in ('', '*') else ln,
                None if wt in ('', '*') else wt,
            ))
    return waivers


def path_suffix_match(offender, wfile):
    o, w = norm(offender), norm(wfile)
    return o == w or o.endswith('/' + w) or w.endswith('/' + o)


def is_waived(offender, line, wtype, waivers):
    for wf, wl, wt in waivers:
        if not path_suffix_match(offender, wf):
            continue
        if wl is not None and wl != line:
            continue
        if wt is not None and wt != wtype:
            continue
        return True
    return False


def parse_log(path, waivers=None, count_errors=False):
    """Yield (built_file, offender_file, offender_line, warning_type, sample) per warning."""
    waivers = waivers or []
    tu = None          # translation unit currently being compiled; never cleared
    rows = []
    waived = 0

    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        for raw in fh:
            line = raw.rstrip('\n')

            tgt = compile_target(line)
            if tgt is not None:
                tu = norm(tgt)                    # signal 1: compile command echo
                continue

            inc = _INCFROM_RE.match(line)
            if inc:
                tu = norm(inc.group('file'))      # signal 2: include-chain root is the TU
                continue

            m = _DIAG_RE.search(line)
            if not m:
                continue
            kind = m.group('kind')
            if kind == 'note':
                continue
            if kind == 'error' and not count_errors:
                continue

            offender, oline = parse_location(line[:m.start()])
            if offender is None:
                continue
            offender = norm(offender)

            if is_source(offender):
                tu = offender                     # signal 3: a direct .c warning => that .c is the TU
            built = tu or offender

            wtype = warning_type(m.group('msg'))
            if is_waived(offender, oline, wtype, waivers):
                waived += 1
                continue

            rows.append((built, offender, oline, wtype, line))

    return rows, waived


def main(argv=None):
    ap = argparse.ArgumentParser(description='Translate a GCC/Clang build log into a warning table.')
    ap.add_argument('log')
    ap.add_argument('-o', '--out', help='write CSV here (default: stdout)')
    ap.add_argument('--waivers', help='CSV waiver list: file,line,warning')
    ap.add_argument('--raw', action='store_true',
                    help='one row per warning (no count column)')
    ap.add_argument('--count-errors', action='store_true', help='also include error: lines')
    args = ap.parse_args(argv)

    waivers = load_waivers(args.waivers) if args.waivers else []
    rows, waived = parse_log(args.log, waivers=waivers, count_errors=args.count_errors)

    fh = open(args.out, 'w', newline='', encoding='utf-8') if args.out else sys.stdout
    try:
        w = csv.writer(fh)
        if args.raw:
            w.writerow(['built_file', 'offender_file', 'offender_line', 'warning_type', 'sample'])
            for r in rows:
                w.writerow(r)
        else:
            w.writerow(['built_file', 'offender_file', 'offender_line', 'warning_type', 'count', 'sample'])
            counts = Counter()
            last_sample = {}
            for built, off, oln, wt, sample in rows:
                key = (built, off, oln, wt)
                counts[key] += 1
                last_sample[key] = sample     # last occurrence wins
            for (built, off, oln, wt), n in sorted(counts.items()):
                w.writerow([built, off, oln, wt, n, last_sample[(built, off, oln, wt)]])
    finally:
        if fh is not sys.stdout:
            fh.close()

    if waivers:
        print(f'{waived} warning(s) waived.', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
