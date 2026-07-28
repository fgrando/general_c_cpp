#!/usr/bin/env python3
"""
loaddb_query.py -- query the load registry (one JSON object per load package,
components as a list).

Repo layout:
    loaddb/packages/<load_package_md5>.json

Each file:
    { schema_version, load_package_md5, jenkins_build_url, svn_url,
      svn_revision, notes, components: [ {component, crc32}, ... ] }

Lookups:
    --crc32 / --crc-of  identify a dumped component, trace to its package
    --package           list a package's components
    --search TEXT       list all packages whose notes contain TEXT
                        (case-insensitive; for when you don't know the exact
                        version and want every possible match)
"""

import argparse
import json
import zlib
import sys
from pathlib import Path


def load_records(db: Path):
    """Return (by_crc32, by_package). by_crc32 maps crc -> flattened record."""
    by_crc32, by_package = {}, {}
    for f in (db / "packages").glob("*.json"):
        pkg = json.loads(f.read_text())
        by_package[pkg["load_package_md5"]] = pkg
        shared = {k: v for k, v in pkg.items() if k != "components"}
        for comp in pkg["components"]:
            by_crc32[comp["crc32"].lower()] = {**shared, **comp}
    return by_crc32, by_package


def _norm_crc(v: str) -> str:
    return f"0x{int(v, 16):08x}"


def _print_component(rec):
    print(f"  component        : {rec['component']}")
    print(f"  crc32            : {rec['crc32']}")
    print(f"  load_package_md5 : {rec['load_package_md5']}")
    print(f"  svn              : {rec['svn_url']} @ {rec['svn_revision']}")
    print(f"  jenkins          : {rec['jenkins_build_url']}")
    print(f"  notes            : {rec['notes']}")


def _print_package(pkg):
    comps = ", ".join(f"{c['component']}={c['crc32']}" for c in pkg["components"])
    print(f"[{pkg['load_package_md5']}]  svn {pkg['svn_revision']}")
    print(f"  notes      : {pkg['notes']}")
    print(f"  jenkins    : {pkg['jenkins_build_url']}")
    print(f"  components : {comps}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="query the load registry")
    ap.add_argument("--db", default="loaddb")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--crc32", help="identify a component by embedded crc32")
    g.add_argument("--crc-of", help="crc32 a dumped component blob, then identify")
    g.add_argument("--package", help="list components of a load_package_md5")
    g.add_argument("--search", help="list packages whose notes contain TEXT")
    args = ap.parse_args(argv)

    by_crc32, by_package = load_records(Path(args.db))

    # free-text search over notes -----------------------------------------
    if args.search is not None:
        q = args.search.lower()
        hits = [p for p in by_package.values() if q in p.get("notes", "").lower()]
        if not hits:
            print(f"no packages match '{args.search}'"); return 1
        hits.sort(key=lambda p: p["svn_revision"])
        print(f"{len(hits)} package(s) matching '{args.search}':\n")
        for pkg in hits:
            _print_package(pkg)
            print()
        return 0

    # list a package's components -----------------------------------------
    if args.package:
        pkg = by_package.get(args.package)
        if not pkg:
            print("no such load package"); return 1
        print(f"load package {args.package}  (svn {pkg['svn_revision']})")
        for c in pkg["components"]:
            print(f"  - {c['component']:12} crc32={c['crc32']}")
        return 0

    # identify a component by crc -----------------------------------------
    crc = _norm_crc(args.crc32) if args.crc32 else \
          f"0x{zlib.crc32(Path(args.crc_of).read_bytes()) & 0xffffffff:08x}"
    rec = by_crc32.get(crc)
    if not rec:
        print(f"no match for {crc}"); return 1
    print(f"MATCH {crc}")
    _print_component(rec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
