#!/usr/bin/env python3
"""
make_manifest.py  -  Generate the JSON manifest for a load package in the
                     "poor man's Artifactory" SVN release registry.

Standard library only. Deterministic output. UTC timestamps.

The manifest name/coordinate is:  r<rev:07d>-b<build:06d>-<first8-md5>
computed from the actual load tar.gz, so run this AFTER the load is built.

Two Jenkins-specific inputs cannot be read by Python and are passed in:
  * release notes : the multiline free-text field from the job config,
                    supplied via --release-notes-file (or --release-notes).
  * changes       : the commit/author list Jenkins shows as "Changes",
                    supplied via --changes-file as JSON (see FORMAT below).

CHANGES FILE FORMAT (a JSON list; keys are matched leniently):
  [
    {"revision": 2088, "author": "fbecker",
     "message": "PR#312: fix telemetry frame alignment on wrap",
     "timestamp": 1754212462000, "paths": ["trunk/telemetry/frame.c"]},
    ...
  ]
Accepted key aliases:
  revision  <- revision | rev | commitId | commit_id | id
  author    <- author | authorName | fullName | user
  message   <- message | msg | comment
  timestamp <- timestamp | time            (epoch ms, epoch s, or ISO-8601)
  paths     <- paths | affectedPaths | files
"""

import argparse
import hashlib
import json
import os
import sys
from datetime import datetime, timezone

CHUNK = 1024 * 1024  # 1 MiB streaming read for large tarballs


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def hash_file(path):
    """Return (md5_hex, sha256_hex, size_bytes) computed in a single pass."""
    md5 = hashlib.md5()
    sha = hashlib.sha256()
    size = 0
    with open(path, "rb") as fh:
        while True:
            block = fh.read(CHUNK)
            if not block:
                break
            size += len(block)
            md5.update(block)
            sha.update(block)
    return md5.hexdigest(), sha.hexdigest(), size


def utc_now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def epoch_or_iso_to_utc(value):
    """Best-effort convert a Jenkins timestamp to UTC ISO-8601, else pass through."""
    if value is None:
        return None
    # numeric -> epoch (ms if it looks like ms, else s)
    if isinstance(value, (int, float)) or (isinstance(value, str) and value.isdigit()):
        n = int(value)
        if n > 10_000_000_000:  # clearly milliseconds
            n //= 1000
        try:
            return datetime.fromtimestamp(n, tz=timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"
            )
        except (OverflowError, OSError, ValueError):
            return str(value)
    return str(value)  # assume already a date/ISO string


def pick(d, *keys, default=None):
    for k in keys:
        if k in d and d[k] not in (None, ""):
            return d[k]
    return default


def load_changes(path):
    """Read Jenkins changeSets JSON and normalize to a stable schema."""
    if not path:
        return []
    with open(path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)
    if not isinstance(raw, list):
        raise ValueError("changes file must contain a JSON list")
    out = []
    for item in raw:
        if not isinstance(item, dict):
            continue
        out.append(
            {
                "revision": pick(item, "revision", "rev", "commitId",
                                 "commit_id", "id"),
                "author": pick(item, "author", "authorName", "fullName",
                               "user", default="unknown"),
                "message": pick(item, "message", "msg", "comment", default=""),
                "timestamp_utc": epoch_or_iso_to_utc(
                    pick(item, "timestamp", "time")
                ),
                "paths": pick(item, "paths", "affectedPaths", "files",
                              default=[]),
            }
        )
    return out


def parse_content(items):
    """--content name:path  ->  {name, md5, sha256, size_bytes, built_from_rev?}"""
    contents = []
    for spec in items or []:
        if ":" not in spec:
            raise ValueError(f"--content must be name:path, got: {spec!r}")
        name, path = spec.split(":", 1)
        md5, sha, size = hash_file(path)
        contents.append(
            {"name": name, "md5": md5, "sha256": sha, "size_bytes": size}
        )
    return contents


def read_notes(args):
    if args.release_notes_file:
        with open(args.release_notes_file, "r", encoding="utf-8") as fh:
            return fh.read().rstrip("\n")
    if args.release_notes is not None:
        return args.release_notes
    return ""


def authors_from_changes(changes):
    """De-duplicated, order-preserving author list."""
    seen = {}
    for c in changes:
        a = c.get("author")
        if a and a not in seen:
            seen[a] = None
    return list(seen.keys())


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #
def build_manifest(args):
    art_md5, art_sha, art_size = hash_file(args.artifact)
    coord = "r{:07d}-b{:06d}-{}".format(args.revision, args.build_number,
                                        art_md5[:8])
    filename = coord + ".tar.gz"

    changes = load_changes(args.changes_file)

    manifest = {
        "schema_version": "1.0",
        "component": args.component,
        "part_number": args.part_number,
        "version": args.version,
        "track": args.track,
        "coordinate": coord,
        "source": {
            "repo_url": args.repo_url,
            "revision": args.revision,
            "clean_checkout": args.clean,
            "wc_state": args.wc_state,
        },
        "build": {
            "jenkins_job": args.jenkins_job,
            "build_number": args.build_number,
            "build_url": args.build_url,
            "agent": args.agent,
            "timestamp_utc": args.build_timestamp or utc_now_iso(),
            "toolchain": {
                "compiler": args.compiler,
                "version": args.compiler_version,
                "flags": args.compiler_flags,
            },
            "status": args.status,
        },
        "artifact": {
            "filename": filename,
            "size_bytes": art_size,
            "md5": art_md5,
            "sha256": art_sha,
            # Authoritative payload lives in SVN alongside this manifest.
            "storage": "svn",
            # Best-effort reference to the Jenkins build artifact. NOT a
            # guarantee of availability - Jenkins retention may reap it. Kept
            # only as a provenance pointer; verify against md5/sha256 above.
            "jenkins_artifact_ref": args.jenkins_artifact_url,
        },
        "contents": parse_content(args.content),
        "dependencies": json.loads(args.dependencies) if args.dependencies else [],
        "release_notes": read_notes(args),
        "changes": changes,
        "authors": authors_from_changes(changes),
    }
    return coord, manifest


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Generate the load-package manifest JSON."
    )

    # identity / source
    p.add_argument("--artifact", required=True,
                   help="path to the built load .tar.gz (hashed here)")
    p.add_argument("--component", default="myapp-load")
    p.add_argument("--part-number", default=None)
    p.add_argument("--version", default=None,
                   help="human/part version; kept in JSON, not in the name")
    p.add_argument("--track", default="trunk")
    p.add_argument("--repo-url", required=True)
    p.add_argument("--revision", type=int, required=True,
                   help="SOURCE revision the load was built from")
    p.add_argument("--wc-state", default=None,
                   help="svnversion output, e.g. 2088 or 2051:2088M")
    p.add_argument("--clean", dest="clean", action="store_true",
                   help="working copy was a clean single revision")
    p.add_argument("--dirty", dest="clean", action="store_false")
    p.set_defaults(clean=True)

    # build info
    p.add_argument("--jenkins-job", default=None)
    p.add_argument("--build-number", type=int, required=True)
    p.add_argument("--build-url", default=None)
    p.add_argument("--jenkins-artifact-url", default=None,
                   help="best-effort reference to the Jenkins build artifact; "
                        "non-authoritative, may 404 after retention reaps it")
    p.add_argument("--agent", default=None)
    p.add_argument("--build-timestamp", default=None,
                   help="UTC ISO-8601; defaults to now")
    p.add_argument("--status", default="PASS")

    # toolchain
    p.add_argument("--compiler", default=None)
    p.add_argument("--compiler-version", default=None)
    p.add_argument("--compiler-flags", default=None)

    # contents / deps
    p.add_argument("--content", action="append", metavar="NAME:PATH",
                   help="component inside the load; repeatable")
    p.add_argument("--dependencies", default=None,
                   help="JSON list string of external dependency coordinates")

    # jenkins free-text + changes
    p.add_argument("--release-notes", default=None,
                   help="release notes text (prefer --release-notes-file)")
    p.add_argument("--release-notes-file", default=None,
                   help="file holding the multiline job-config text")
    p.add_argument("--changes-file", default=None,
                   help="JSON list dumped from currentBuild.changeSets")

    # output
    p.add_argument("--out-dir", default=".",
                   help="directory to write <coord>.json into")
    p.add_argument("--print", dest="to_stdout", action="store_true",
                   help="also print the manifest to stdout")
    p.add_argument("--strict-clean", action="store_true",
                   help="exit non-zero if wc-state is dirty/mixed")

    args = p.parse_args(argv)

    # Optional guard: refuse to manifest a non-clean build.
    if args.strict_clean:
        dirty = (not args.clean) or (
            args.wc_state and (
                args.wc_state.endswith(("M", "S", "P")) or ":" in args.wc_state
            )
        )
        if dirty:
            sys.stderr.write(
                "ERROR: working copy is mixed/modified "
                "(wc_state={}) - refusing to publish.\n".format(args.wc_state)
            )
            return 2

    coord, manifest = build_manifest(args)

    out_path = os.path.join(args.out_dir, coord + ".json")
    text = json.dumps(manifest, indent=2, ensure_ascii=False)  # keeps umlauts
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text + "\n")

    if args.to_stdout:
        print(text)
    # emit the coordinate on stderr so the pipeline can capture it cleanly
    sys.stderr.write(coord + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
