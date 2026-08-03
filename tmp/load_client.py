#!/usr/bin/env python3
"""
load_client.py  -  Search and download load packages from the SVN release
                   registry ("poor man's Artifactory").

Standard library only. Talks to SVN by shelling out to the `svn` binary
(works over svn:// and http(s)://). Verifies md5 + sha256 against the
downloaded manifest.

Registry layout assumed:
    <repo-url>/<component>/<track>/r<rev>-b<build>-<md5[:8]>.tar.gz
                                   r<rev>-b<build>-<md5[:8]>.json

Coordinate name grammar:
    r<digits>-b<digits>-<8+ hex>.tar.gz

Subcommands:
    list     list coordinates in a track (newest last)
    search   filter by rev/build/md5/version/status/substring; prints matches
    get      download a load (+ manifest) and verify hashes

Examples:
    load_client.py --repo-url svn://host/releases list
    load_client.py --repo-url svn://host/releases search --version 1.4.0 --status PASS
    load_client.py --repo-url svn://host/releases get --latest --out-dir ./dl
    load_client.py --repo-url svn://host/releases get \
        --coord r0002088-b000044-3c4d5e6f --out-dir ./dl
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

CHUNK = 1024 * 1024
COORD_RE = re.compile(r"^r(\d+)-b(\d+)-([0-9a-fA-F]{6,})\.tar\.gz$")


# --------------------------------------------------------------------------- #
# svn plumbing
# --------------------------------------------------------------------------- #
def svn(args, svn_extra, capture=True):
    """Run an svn command. Returns stdout (str) when capture=True."""
    cmd = ["svn"] + list(args) + list(svn_extra)
    try:
        res = subprocess.run(
            cmd,
            check=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError:
        sys.exit("ERROR: `svn` binary not found on PATH.")
    except subprocess.CalledProcessError as e:
        sys.stderr.write(e.stderr or "")
        sys.exit("ERROR: svn command failed: " + " ".join(cmd))
    return res.stdout if capture else None


def track_url(args):
    base = args.repo_url.rstrip("/")
    return "/".join([base, args.component, args.track])


def svn_ls(url, svn_extra):
    """Return list of entry names in an SVN dir URL."""
    out = svn(["ls", url], svn_extra)
    return [line.strip() for line in out.splitlines() if line.strip()]


def svn_cat(url, svn_extra):
    """Return file contents (text) at an SVN URL."""
    return svn(["cat", url], svn_extra)


def svn_export(url, dest, svn_extra):
    """Export a single file URL to a local path (overwrites)."""
    svn(["export", "--force", url, dest], svn_extra, capture=False)


# --------------------------------------------------------------------------- #
# coordinate handling
# --------------------------------------------------------------------------- #
class Coord:
    __slots__ = ("name", "rev", "build", "md5_8")

    def __init__(self, name, rev, build, md5_8):
        self.name = name          # full tar.gz filename
        self.rev = rev            # int
        self.build = build        # int
        self.md5_8 = md5_8.lower()

    @property
    def stem(self):
        return self.name[:-len(".tar.gz")]

    @property
    def json_name(self):
        return self.stem + ".json"

    def sort_key(self):
        return (self.rev, self.build)

    def __str__(self):
        return self.name


def parse_coords(names):
    coords = []
    for n in names:
        m = COORD_RE.match(n)
        if m:
            coords.append(Coord(n, int(m.group(1)), int(m.group(2)),
                                m.group(3)))
    coords.sort(key=Coord.sort_key)  # chronological (rev, then build)
    return coords


def fetch_manifest(url_dir, coord, svn_extra):
    """svn cat the coordinate's .json and parse it. Returns dict or None."""
    url = url_dir + "/" + coord.json_name
    try:
        return json.loads(svn_cat(url, svn_extra))
    except SystemExit:
        return None
    except (ValueError, json.JSONDecodeError):
        return None


# --------------------------------------------------------------------------- #
# filtering
# --------------------------------------------------------------------------- #
def name_filter(coords, args):
    """Filters that need only the filename (cheap, no manifest fetch)."""
    out = coords
    if args.rev is not None:
        out = [c for c in out if c.rev == args.rev]
    if getattr(args, "rev_min", None) is not None:
        out = [c for c in out if c.rev >= args.rev_min]
    if getattr(args, "rev_max", None) is not None:
        out = [c for c in out if c.rev <= args.rev_max]
    if getattr(args, "rev_gt", None) is not None:
        out = [c for c in out if c.rev > args.rev_gt]
    if getattr(args, "rev_lt", None) is not None:
        out = [c for c in out if c.rev < args.rev_lt]
    if args.build is not None:
        out = [c for c in out if c.build == args.build]
    if args.md5:
        pref = args.md5.lower()
        out = [c for c in out if c.md5_8.startswith(pref[:8])]
    if args.contains:
        out = [c for c in out if args.contains in c.name]
    return out


def needs_manifest(args):
    return any([getattr(args, "version", None),
                getattr(args, "status", None),
                getattr(args, "author", None)])


def manifest_filter(url_dir, coords, args, svn_extra):
    """Filters that require reading each manifest (slower)."""
    if not needs_manifest(args):
        return coords
    kept = []
    for c in coords:
        man = fetch_manifest(url_dir, c, svn_extra)
        if man is None:
            continue
        if args.version and str(man.get("version")) != str(args.version):
            continue
        if args.status and str(man.get("build", {}).get("status")) != args.status:
            continue
        if args.author:
            authors = [a.lower() for a in man.get("authors", [])]
            if args.author.lower() not in authors:
                continue
        kept.append(c)
    return kept


def resolve(url_dir, args, svn_extra):
    """Return the filtered, sorted coordinate list for the request."""
    coords = parse_coords(svn_ls(url_dir, svn_extra))
    if getattr(args, "coord", None):
        want = args.coord
        if want.endswith(".tar.gz"):
            want = want[:-len(".tar.gz")]
        coords = [c for c in coords if c.stem == want]
        return coords
    coords = name_filter(coords, args)
    coords = manifest_filter(url_dir, coords, args, svn_extra)
    if getattr(args, "latest", False):
        coords = coords[-1:] if coords else []
    return coords


# --------------------------------------------------------------------------- #
# download + verify
# --------------------------------------------------------------------------- #
def hash_file(path):
    md5 = hashlib.md5()
    sha = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            block = fh.read(CHUNK)
            if not block:
                break
            md5.update(block)
            sha.update(block)
    return md5.hexdigest(), sha.hexdigest()


def download(url_dir, coord, out_dir, svn_extra, verify=True):
    os.makedirs(out_dir, exist_ok=True)
    tar_dest = os.path.join(out_dir, coord.name)
    json_dest = os.path.join(out_dir, coord.json_name)

    svn_export(url_dir + "/" + coord.name, tar_dest, svn_extra)
    # manifest is best-effort but expected; export if present
    try:
        svn_export(url_dir + "/" + coord.json_name, json_dest, svn_extra)
    except SystemExit:
        json_dest = None

    if not verify:
        return tar_dest, json_dest, True

    md5, sha = hash_file(tar_dest)
    ok = True

    # 1) name embeds first-8 of md5
    if not md5.startswith(coord.md5_8):
        sys.stderr.write(
            "WARN: md5 mismatch vs filename ({} != {}...)\n".format(
                md5[:8], coord.md5_8)
        )
        ok = False

    # 2) manifest hashes are authoritative
    if json_dest and os.path.exists(json_dest):
        with open(json_dest, "r", encoding="utf-8") as fh:
            man = json.load(fh)
        art = man.get("artifact", {})
        if art.get("md5") and art["md5"] != md5:
            sys.stderr.write("WARN: md5 mismatch vs manifest\n")
            ok = False
        if art.get("sha256") and art["sha256"] != sha:
            sys.stderr.write("WARN: sha256 mismatch vs manifest\n")
            ok = False
        if art.get("storage") == "svn" and art.get("jenkins_artifact_ref"):
            pass  # ref is informational only; SVN copy is authoritative
    else:
        sys.stderr.write("WARN: no manifest downloaded; hash unverified "
                         "beyond filename prefix\n")

    return tar_dest, json_dest, ok


# --------------------------------------------------------------------------- #
# commands
# --------------------------------------------------------------------------- #
def cmd_list(args, svn_extra):
    url_dir = track_url(args)
    for c in parse_coords(svn_ls(url_dir, svn_extra)):
        print(c.name)
    return 0


def cmd_search(args, svn_extra):
    url_dir = track_url(args)
    matches = resolve(url_dir, args, svn_extra)
    if not matches:
        sys.stderr.write("no matches\n")
        return 1
    for c in matches:
        print(c.name)
    return 0


def cmd_get(args, svn_extra):
    url_dir = track_url(args)
    matches = resolve(url_dir, args, svn_extra)
    if not matches:
        sys.exit("no load matched the given criteria")
    if len(matches) > 1 and not args.latest:
        sys.stderr.write("multiple matches; refine or use --latest:\n")
        for c in matches:
            sys.stderr.write("  " + c.name + "\n")
        return 1
    coord = matches[-1]
    tar, jsn, ok = download(url_dir, coord, args.out_dir, svn_extra,
                            verify=not args.no_verify)
    print(tar)
    if jsn:
        print(jsn)
    if not ok and not args.no_verify:
        sys.stderr.write("VERIFICATION FAILED\n")
        return 3
    return 0


# --------------------------------------------------------------------------- #
# cli
# --------------------------------------------------------------------------- #
def add_common(sp):
    sp.add_argument("--rev", type=int, help="exact source revision")
    sp.add_argument("--rev-min", type=int,
                    help="revision >= this (inclusive)")
    sp.add_argument("--rev-max", type=int,
                    help="revision <= this (inclusive)")
    sp.add_argument("--rev-gt", type=int,
                    help="revision > this (exclusive)")
    sp.add_argument("--rev-lt", type=int,
                    help="revision < this (exclusive)")
    sp.add_argument("--build", type=int, help="exact Jenkins build number")
    sp.add_argument("--md5", help="md5 prefix (name embeds first 8)")
    sp.add_argument("--contains", help="substring match on filename")
    sp.add_argument("--version", help="manifest version (fetches manifests)")
    sp.add_argument("--status", help="manifest build.status, e.g. PASS")
    sp.add_argument("--author", help="manifest authors contains (fetches)")


def build_parser():
    p = argparse.ArgumentParser(description="SVN release-registry client.")
    p.add_argument("--repo-url", required=True,
                   help="base URL of the releases repo, e.g. svn://host/releases")
    p.add_argument("--component", default="myapp")
    p.add_argument("--track", default="trunk")
    # svn passthrough / auth
    p.add_argument("--username")
    p.add_argument("--password")
    p.add_argument("--non-interactive", action="store_true", default=True)
    p.add_argument("--trust-server-cert", action="store_true",
                   help="for https with self-signed certs")

    sub = p.add_subparsers(dest="cmd", required=True)

    sp_list = sub.add_parser("list", help="list coordinates in a track")
    sp_list.set_defaults(func=cmd_list)

    sp_search = sub.add_parser("search", help="filter and print matches")
    add_common(sp_search)
    sp_search.add_argument("--latest", action="store_true")
    sp_search.set_defaults(func=cmd_search)

    sp_get = sub.add_parser("get", help="download a load + manifest, verify")
    add_common(sp_get)
    sp_get.add_argument("--coord",
                        help="exact coordinate (with or without .tar.gz)")
    sp_get.add_argument("--latest", action="store_true",
                        help="pick newest match / newest in track")
    sp_get.add_argument("--out-dir", default=".")
    sp_get.add_argument("--no-verify", action="store_true")
    sp_get.set_defaults(func=cmd_get)

    return p


def svn_extra_args(args):
    extra = []
    if args.non_interactive:
        extra.append("--non-interactive")
    if args.username:
        extra += ["--username", args.username]
    if args.password:
        extra += ["--password", args.password]
    if args.trust_server_cert:
        extra += ["--trust-server-cert-failures", "unknown-ca,cn-mismatch"]
    return extra


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.func(args, svn_extra_args(args))


if __name__ == "__main__":
    raise SystemExit(main())
