#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
svn_changelog.py -- turn an SVN log export into two cross-referenced TSV
tables: one row per commit, and one row per file.

This script never talks to the SVN server: you create the XML log yourself
and this only parses it. Python 3.6+, standard library only. Works on
Windows, Cygwin and Linux.


1) CREATE THE INPUT FILE
------------------------
`-v` is mandatory -- without it the XML contains no file paths at all.

    svn log -v --xml -r 12000:12500 https://svn.example.com/repo/trunk > svn_log.xml

    # or from a working copy:
    svn log -v --xml -r 12000:HEAD . > svn_log.xml

Useful extras:
    -g / --use-merge-history   also list revisions merged in from branches
    --stop-on-copy             stop at the branch point
    --username U --password P --non-interactive

Windows note: cmd.exe `>` passes bytes through unchanged (fine). PowerShell
`>` re-encodes to UTF-16 and breaks the XML -- use cmd.exe, a Cygwin shell,
or `svn log ... | Out-File -Encoding utf8 svn_log.xml`.


2) RUN
------
    python svn_changelog.py svn_log.xml -o out --tz-offset 2

Ad-hoc lookups (print to the console, nothing written):

    python svn_changelog.py svn_log.xml --file uart.c    # commits touching a file
    python svn_changelog.py svn_log.xml --rev 12003      # files in a commit


3) OUTPUT (in --out-dir)
------------------------
    commits.tsv   one row per revision:
                  revision, dates, author (login + resolved name), ticket(s),
                  file count, commit message, and the list of affected files
    files.tsv     one row per file:
                  path, how often it was touched, which authors, which
                  actions, where it was copied from, and the list of every
                  revision that touched it

Go forwards with commits.tsv (what did r12345 change?) and backwards with
files.tsv (which revisions touched uart.c?).

Tables are UTF-8 with BOM (Excel opens them cleanly) and tab-separated;
--format csv switches to commas. Values are flattened -- tabs become
spaces, newlines become the two characters '\\n' -- so no field ever spans
a line and `cut -f`, awk, pandas and Excel all agree.


4) AUTHOR NAMES
---------------
Login ids are translated through `authorlookup.py`, imported from the
current directory, from next to this script, or from --authorlookup PATH.
It only has to define a dict:

    AUTHORS = {"fgrando": "Fernando Zatt", "kmueller": "Klaus Mueller"}

The dict may be called AUTHORS, AUTHOR_LOOKUP, LOGIN_TO_NAME, NICKNAMES,
USERS, AUTHOR_MAP, LOOKUP (or lowercase); otherwise the largest
string-keyed dict in the module is used. Values may also be a dict
{"name":..,"team":..,"email":..} or a ("name","team","email") tuple.
Logins missing from the dict keep the login as their name and are listed
as "unmapped" in the console summary; a missing authorlookup.py is not an
error. `--write-authors authorlookup.py` emits a fillable stub containing
every login found in the XML.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, OrderedDict, defaultdict
from datetime import datetime, timedelta

# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------

# Ticket / issue ids recognised in commit messages: ABC-123 and #1234.
DEFAULT_TICKET_RE = r"(?:[A-Z][A-Z0-9]+-\d+|#\d{2,})"

# Excel refuses to display more than 32767 characters in one cell, so the
# list columns are cut at this length and marked. 0 = no limit.
DEFAULT_MAX_CELL = 32000

# Separator inside list columns.
SEP = "; "

# Names looked for, in order, when picking the dict out of authorlookup.py.
AUTHOR_DICT_NAMES = ["AUTHORS", "authors", "AUTHOR_LOOKUP", "author_lookup",
                     "LOGIN_TO_NAME", "login_to_name", "NICKNAMES", "nicknames",
                     "USERS", "users", "AUTHOR_MAP", "author_map", "LOOKUP", "lookup"]

ACTION_NAME = {"A": "added", "M": "modified", "D": "deleted", "R": "replaced"}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def one_line(text, limit=0):
    s = " ".join((text or "").split())
    if limit and len(s) > limit:
        s = s[:limit - 3] + "..."
    return s


def join_list(items, max_cell):
    """Join a list column, marking truncation so it is never silent."""
    items = list(items)
    if not max_cell:
        return SEP.join(items)
    out, used, shown = [], 0, 0
    for it in items:
        add = len(it) + (len(SEP) if out else 0)
        if used + add > max_cell:
            break
        out.append(it)
        used += add
        shown += 1
    if shown < len(items):
        return SEP.join(out) + SEP + "...(+%d more, see the count column)" % (len(items) - shown)
    return SEP.join(out)


# --------------------------------------------------------------------------
# Author lookup -- imported from authorlookup.py
# --------------------------------------------------------------------------

class Authors(object):
    """login -> {name, team, email}. Logins not in the dict map to themselves."""

    def __init__(self, mapping=None, source=""):
        self.map = mapping or {}
        self.source = source

    @classmethod
    def load(cls, hint=None):
        path = cls._locate(hint)
        if not path:
            if hint:
                raise RuntimeError("authorlookup module not found at: %s" % hint)
            log("  no authorlookup.py found -- using raw login ids")
            return cls()

        spec = importlib.util.spec_from_file_location("authorlookup", path)
        mod = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(mod)
        except Exception as exc:                      # noqa: BLE001 - user file
            raise RuntimeError("failed to import %s: %s" % (path, exc))

        raw = cls._pick_dict(mod)
        if raw is None:
            log("  ! %s defines no usable dict -- using raw login ids" % path)
            return cls(source=path)

        mapping = {}
        for login, val in raw.items():
            login = str(login).strip()
            if not login:
                continue
            if isinstance(val, dict):
                mapping[login] = {
                    "name": str(val.get("name") or val.get("nickname") or login).strip(),
                    "team": str(val.get("team") or val.get("group") or "").strip(),
                    "email": str(val.get("email") or val.get("mail") or "").strip(),
                }
            elif isinstance(val, (list, tuple)):
                vals = [str(x).strip() for x in val]
                mapping[login] = {
                    "name": vals[0] if vals else login,
                    "team": vals[1] if len(vals) > 1 else "",
                    "email": vals[2] if len(vals) > 2 else "",
                }
            else:
                mapping[login] = {"name": str(val).strip() or login, "team": "", "email": ""}
        log("  loaded %d author mappings from %s" % (len(mapping), path))
        return cls(mapping, path)

    @staticmethod
    def _locate(hint):
        candidates = []
        if hint:
            candidates.append(os.path.join(hint, "authorlookup.py") if os.path.isdir(hint) else hint)
        else:
            candidates.append(os.path.join(os.getcwd(), "authorlookup.py"))
            candidates.append(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "authorlookup.py"))
        for c in candidates:
            if os.path.isfile(c):
                return c
        return None

    @staticmethod
    def _pick_dict(mod):
        for name in AUTHOR_DICT_NAMES:
            val = getattr(mod, name, None)
            if isinstance(val, dict) and val:
                return val
        best = None
        for name in dir(mod):
            if name.startswith("_"):
                continue
            val = getattr(mod, name)
            if isinstance(val, dict) and val and all(isinstance(k, str) for k in val):
                if best is None or len(val) > len(best):
                    best = val
        return best

    def rec(self, login):
        return self.map.get(login) or {"name": login, "team": "", "email": ""}

    def name(self, login):
        return self.rec(login)["name"]

    def names(self, logins):
        return sorted(set(self.name(x) for x in logins))


def write_authors_template(path, commits, authors):
    """Write an authorlookup.py stub containing every login seen in the XML."""
    logins = sorted(set(c["author"] for c in commits))
    width = max((len(l) for l in logins), default=8) + 4
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("# -*- coding: utf-8 -*-\n")
        fh.write('"""SVN login -> real name, used by svn_changelog.py."""\n\n')
        fh.write("AUTHORS = {\n")
        for login in logins:
            fh.write("    %s %s,\n" % (('"%s":' % login).ljust(width),
                                       '"%s"' % authors.rec(login)["name"]))
        fh.write("}\n")
    log("  wrote %s (%d logins)" % (path, len(logins)))


# --------------------------------------------------------------------------
# Table writer
# --------------------------------------------------------------------------

def flatten(value):
    """Make a value safe for a single-line tab-separated field."""
    if value is None:
        return ""
    s = value if isinstance(value, str) else str(value)
    if "\t" in s:
        s = s.replace("\t", " ")
    if "\r" in s or "\n" in s:
        s = s.replace("\r\n", "\\n").replace("\r", "\\n").replace("\n", "\\n")
    return s


def write_table(out_dir, stem, fieldnames, rows, fmt="tsv"):
    ext = ".tsv" if fmt == "tsv" else ".csv"
    path = os.path.join(out_dir, stem + ext)
    with open(path, "w", newline="", encoding="utf-8-sig") as fh:
        w = csv.DictWriter(fh, fieldnames=fieldnames, extrasaction="ignore",
                           delimiter="\t" if fmt == "tsv" else ",",
                           lineterminator="\n", quoting=csv.QUOTE_MINIMAL)
        w.writeheader()
        for r in rows:
            w.writerow({k: flatten(v) for k, v in r.items()})
    log("  wrote %s (%d rows)" % (path, len(rows)))
    return path


# --------------------------------------------------------------------------
# Reading and parsing the log
# --------------------------------------------------------------------------

def read_log_xml(path):
    if not os.path.isfile(path):
        raise RuntimeError("input XML not found: %s" % path)
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        raise RuntimeError(
            "%s looks like UTF-16 (PowerShell '>' does that). Re-create it from "
            "cmd.exe / a Cygwin shell, or with: svn log ... | Out-File -Encoding utf8 %s"
            % (path, path))
    try:
        root = ET.fromstring(raw.decode("utf-8-sig", "replace"))
    except ET.ParseError as exc:
        raise RuntimeError("%s is not valid XML (%s). Was it produced with "
                           "`svn log -v --xml`?" % (path, exc))
    if root.tag != "log":
        raise RuntimeError("%s: expected a <log> root element, got <%s>" % (path, root.tag))
    return root


def parse_svn_date(text):
    """'2026-09-03T10:11:12.123456Z' -> naive UTC datetime (or None)."""
    if not text:
        return None
    text = text.strip().rstrip("Z")
    for fmt in ("%Y-%m-%dT%H:%M:%S.%f", "%Y-%m-%dT%H:%M:%S"):
        try:
            return datetime.strptime(text, fmt)
        except ValueError:
            continue
    return None


def parse_log(root, args, authors):
    """
    Return (commits, changes).

    commits -- one dict per revision (with its file list already built)
    changes -- one dict per (revision, path), the raw pairs both tables and
               the console lookups are built from
    """
    ticket_re = re.compile(args.ticket_regex)
    tz = timedelta(hours=args.tz_offset)

    commits, changes = [], []
    no_paths = 0

    for entry in root.findall("logentry"):
        rev = int(entry.get("revision"))
        author = (entry.findtext("author") or "(none)").strip()
        dt_utc = parse_svn_date(entry.findtext("date"))
        dt_loc = (dt_utc + tz) if dt_utc else None
        msg = (entry.findtext("msg") or "").replace("\r\n", "\n").replace("\r", "\n")

        paths_el = entry.find("paths")
        path_nodes = list(paths_el.findall("path")) if paths_el is not None else []
        if not path_nodes:
            no_paths += 1

        rows = []
        for pn in path_nodes:
            path = (pn.text or "").strip()
            action = pn.get("action", "")
            rows.append({
                "revision": rev,
                "author": author,
                "author_name": authors.name(author),
                "date_utc": dt_utc.strftime("%Y-%m-%d %H:%M:%S") if dt_utc else "",
                "date_local": dt_loc.strftime("%Y-%m-%d %H:%M:%S") if dt_loc else "",
                "action": action,
                "action_name": ACTION_NAME.get(action, action),
                "kind": pn.get("kind", "") or "",
                "path": path,
                "filename": path.rsplit("/", 1)[-1],
                "copyfrom_path": pn.get("copyfrom-path", "") or "",
                "copyfrom_rev": pn.get("copyfrom-rev", "") or "",
                "text_mods": pn.get("text-mods", "") or "",
                "prop_mods": pn.get("prop-mods", "") or "",
                "summary": one_line(msg, 120),
            })
        rows.sort(key=lambda r: r["path"])

        tickets = sorted(set(ticket_re.findall(msg)))
        commits.append({
            "revision": rev,
            "date_local": dt_loc.strftime("%Y-%m-%d %H:%M:%S") if dt_loc else "",
            "date_utc": dt_utc.strftime("%Y-%m-%d %H:%M:%S") if dt_utc else "",
            "author": author,
            "author_name": authors.name(author),
            "tickets": ";".join(tickets),
            "files_count": len(rows),
            "message": msg.strip(),
            "files": join_list(("%s:%s" % (r["action"], r["path"]) for r in rows),
                               args.max_cell),
        })
        changes.extend(rows)

    if no_paths:
        log("  ! %d log entries carry no <paths> element -- was the log exported "
            "without -v?" % no_paths)

    commits.sort(key=lambda c: c["revision"])
    changes.sort(key=lambda r: (r["revision"], r["path"]))
    return commits, changes


def build_files(changes, authors, max_cell):
    """One row per file path, listing every revision that touched it."""
    per_path = OrderedDict()
    for r in sorted(changes, key=lambda r: (r["path"], r["revision"])):
        per_path.setdefault(r["path"], []).append(r)

    rows = []
    for path, rs in per_path.items():
        revs = sorted(set(r["revision"] for r in rs))
        acts = Counter(r["action"] for r in rs)
        logins = set(r["author"] for r in rs)
        copied = [r for r in rs if r["copyfrom_path"]]
        rows.append({
            "path": path,
            "filename": rs[0]["filename"],
            "ext": ("." + rs[0]["filename"].rsplit(".", 1)[-1].lower())
                   if "." in rs[0]["filename"][1:] else "",
            "kind": rs[-1]["kind"],
            "revision_count": len(revs),
            "authors_count": len(logins),
            "authors": ";".join(authors.names(logins)),
            "actions": ";".join("%s=%d" % (ACTION_NAME.get(a, a), n)
                                for a, n in sorted(acts.items())),
            "first_revision": revs[0],
            "last_revision": revs[-1],
            "first_date": rs[0]["date_local"],
            "last_date": rs[-1]["date_local"],
            "copied_from": ";".join(sorted(set("%s@%s" % (r["copyfrom_path"], r["copyfrom_rev"])
                                               for r in copied))),
            "revisions": join_list((str(x) for x in revs), max_cell),
        })
    rows.sort(key=lambda r: (-r["revision_count"], r["path"]))
    return rows


# --------------------------------------------------------------------------
# Ad-hoc console lookups
# --------------------------------------------------------------------------

def query_file(changes, commits, needle, exact=False):
    by_rev = {c["revision"]: c for c in commits}
    if exact:
        hits = [r for r in changes if r["path"] == needle]
    else:
        low = needle.lower()
        hits = [r for r in changes if low in r["path"].lower()]
    if not hits:
        print("No file matches %r in this log export." % needle)
        return
    paths = sorted(set(r["path"] for r in hits))
    print("\nFiles matching %r: %d" % (needle, len(paths)))
    for path in paths:
        rows = sorted([r for r in hits if r["path"] == path], key=lambda r: r["revision"])
        print("\n" + "=" * 78)
        print("%s  (%d commits)" % (path, len(rows)))
        print("=" * 78)
        for r in rows:
            c = by_rev.get(r["revision"], {})
            print("  r%-8d %-9s %-12s %-20s %s" % (
                r["revision"], r["action_name"], r["date_local"][:10],
                r["author_name"][:20], one_line(c.get("message", ""), 60)))
            if r["copyfrom_path"]:
                print("            (copied from %s@%s)" % (r["copyfrom_path"], r["copyfrom_rev"]))
    print("")


def query_rev(changes, commits, revs):
    by_rev = {c["revision"]: c for c in commits}
    for rev in revs:
        c = by_rev.get(rev)
        rows = sorted([r for r in changes if r["revision"] == rev], key=lambda r: r["path"])
        print("\n" + "=" * 78)
        if not c:
            print("r%d is not in this log export." % rev)
            print("=" * 78)
            continue
        print("r%d  %s  <%s>  %s" % (rev, c["date_local"], c["author_name"], c["author"]))
        if c["tickets"]:
            print("ticket(s): %s" % c["tickets"])
        print("=" * 78)
        for line in (c["message"] or "(no message)").splitlines() or ["(no message)"]:
            print("  | " + line)
        print("  ---- %d files ----" % len(rows))
        for r in rows:
            print("  %-9s %s" % (r["action_name"], r["path"]))
            if r["copyfrom_path"]:
                print("            (copied from %s@%s)" % (r["copyfrom_path"], r["copyfrom_rev"]))
    print("")


# --------------------------------------------------------------------------
# Console overview
# --------------------------------------------------------------------------

def print_overview(commits, changes, file_rows, authors):
    if not commits:
        print("No commits found in the log export.")
        return
    revs = [c["revision"] for c in commits]
    print("")
    print("=" * 62)
    print("Range           : r%d .. r%d  (%d commits)" % (min(revs), max(revs), len(commits)))
    print("Dates           : %s .. %s" % (commits[0]["date_local"], commits[-1]["date_local"]))
    print("Distinct files  : %d   (file-level changes: %d)" % (len(file_rows), len(changes)))
    print("Commits w/o ticket : %d of %d" % (sum(1 for c in commits if not c["tickets"]),
                                             len(commits)))
    print("Empty messages  : %d" % sum(1 for c in commits if not c["message"]))
    dirs = sum(1 for r in changes if r["kind"] == "dir")
    if dirs:
        print("Directory entries  : %d (a directory row can stand for many files)" % dirs)
    unmapped = sorted(set(c["author"] for c in commits) - set(authors.map))
    if unmapped:
        print("Unmapped logins : %s" % ", ".join(unmapped))
    print("-" * 62)
    print("Top authors     :")
    for a, n in Counter(c["author"] for c in commits).most_common(5):
        print("    %-28s %4d commits" % (authors.name(a)[:28], n))
    print("Most-touched files :")
    for r in file_rows[:5]:
        print("    %-4d x %s" % (r["revision_count"], r["path"]))
    print("=" * 62)
    print("")


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(
        description="Turn an `svn log -v --xml` export into commits.tsv and files.tsv. "
                    "Does not contact the SVN server.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    p.add_argument("input_xml", nargs="?", metavar="SVN_LOG_XML",
                   help="the file you created with: svn log -v --xml -r A:B TARGET > svn_log.xml")
    p.add_argument("-o", "--out-dir", default="svn_changelog_out",
                   help="output directory (default: svn_changelog_out)")
    p.add_argument("--format", choices=("tsv", "csv"), default="tsv",
                   help="output format (default: tsv)")
    p.add_argument("--tz-offset", type=float, default=0.0,
                   help="hours to add to the UTC commit time for date_local (e.g. 2)")
    p.add_argument("--ticket-regex", default=DEFAULT_TICKET_RE,
                   help="regex for issue ids in commit messages (default: ABC-123 and #1234)")
    p.add_argument("--max-cell", type=int, default=DEFAULT_MAX_CELL,
                   help="max characters in the 'files'/'revisions' list columns; "
                        "0 = unlimited (default: %(default)s, Excel's cell limit is 32767)")

    g = p.add_argument_group("author names")
    g.add_argument("--authorlookup", default=None, metavar="PATH",
                   help="path to authorlookup.py (or the directory holding it). "
                        "Default: ./authorlookup.py, then next to this script.")
    g.add_argument("--write-authors", default=None, metavar="FILE",
                   help="write an authorlookup.py stub with every login found, then continue")

    g = p.add_argument_group("ad-hoc lookups (print only, no tables written)")
    g.add_argument("--file", default=None, metavar="PATH_OR_SUBSTRING",
                   help="show every commit that affected the matching file(s)")
    g.add_argument("--exact-path", action="store_true",
                   help="with --file: match the full path instead of a substring")
    g.add_argument("--rev", type=int, action="append", default=[], metavar="N",
                   help="show every file affected by revision N (repeatable)")
    return p


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    if not args.input_xml:
        parser.error(
            "no input file.\n\n"
            "Create it first, then pass it in:\n"
            "    svn log -v --xml -r 12000:12500 https://svn/repo/trunk > svn_log.xml\n"
            "    python %s svn_log.xml -o out\n\n"
            "(-v is mandatory: without it the XML contains no file paths.)"
            % os.path.basename(sys.argv[0]))

    authors = Authors.load(args.authorlookup)

    log("Reading %s" % args.input_xml)
    root = read_log_xml(args.input_xml)

    log("Parsing log entries")
    commits, changes = parse_log(root, args, authors)
    log("  %d commits, %d file-level changes" % (len(commits), len(changes)))

    if args.write_authors:
        write_authors_template(args.write_authors, commits, authors)

    if args.file or args.rev:
        if args.file:
            query_file(changes, commits, args.file, args.exact_path)
        if args.rev:
            query_rev(changes, commits, args.rev)
        return 0

    file_rows = build_files(changes, authors, args.max_cell)

    os.makedirs(args.out_dir, exist_ok=True)
    log("Writing tables to %s" % args.out_dir)
    commit_fields = ["revision", "date_local", "date_utc", "author", "author_name",
                     "tickets", "files_count", "message", "files"]
    file_fields = ["path", "filename", "ext", "kind", "revision_count", "authors_count",
                   "authors", "actions", "first_revision", "last_revision",
                   "first_date", "last_date", "copied_from", "revisions"]
    write_table(args.out_dir, "commits", commit_fields, commits, args.format)
    write_table(args.out_dir, "files", file_fields, file_rows, args.format)

    print_overview(commits, changes, file_rows, authors)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except RuntimeError as exc:
        log("ERROR: %s" % exc)
        sys.exit(1)
