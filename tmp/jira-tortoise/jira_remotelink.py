#!/usr/bin/env python3
r"""
jira_remotelink.py - attach a WebSVN revision to a Jira issue as a remote link.

Two ways to call it.

1) By revision - the script asks SVN for the commit message, builds the WebSVN
   URL and a tidy title itself:

       python jira_remotelink.py SCRUM-5 --rev 212
       -> r0000212 [trunk] fix ring buffer wraparound

2) By explicit URL, exactly as before:

       python jira_remotelink.py SCRUM-5 ^
           "https://websvn.internal/websvn/revision.php?repname=myproject&rev=2"

NOTE: always put a URL in double quotes. In cmd.exe an unquoted '&' splits the
command line and Jira silently receives a truncated URL.

Standard library only. Re-running for the same revision updates the existing
link instead of creating a duplicate (deterministic globalId).

Environment:
    JIRA_BASE_URL   default https://demo123467890.atlassian.net
    JIRA_EMAIL      your Atlassian account email      (no quotes in `set`!)
    JIRA_API_TOKEN  id.atlassian.com/manage-profile/security/api-tokens
    SVN_URL         repository ROOT, used by --rev
    SVN_USERNAME / SVN_PASSWORD   optional, if svn has no cached credentials
    WEBSVN_BASE_URL default https://websvn.internal/websvn
    SVN_REPO_NAME   WebSVN 'repname', default myproject
"""

import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

DEFAULT_BASE_URL = "https://demo123467890.atlassian.net"
DEFAULT_WEBSVN_BASE = "https://websvn.internal/websvn"
DEFAULT_REPO_NAME = "myproject"

REV_PAD = 7          # r0000212 - matches the artifact registry convention
MSG_MAX = 60         # characters of commit message kept in the title


def issue_prefix_re(issue_key):
    """Strip a leading reference to THIS issue's project only, in any of the
    usual shapes: 'SCRUM-5: msg' '[SCRUM-5] msg' '(SCRUM-5) msg' '@SCRUM-5 msg'
    'SCRUM-5 - msg'.
    Scoping it to the project key keeps 'DO-178C ...' or 'ARINC-429 ...' intact."""
    project = re.escape(issue_key.split("-", 1)[0])
    return re.compile(
        rf"^\s*[\[\(@]?\s*{project}-\d+(?![\w-])\s*[\]\)]?\s*[:\-,]?\s*",
        re.IGNORECASE)


BRACKETED_KEY_RE = re.compile(r"\[([A-Z][A-Z0-9]{1,9}-\d+)\]")
LEADING_BRACKETS_RE = re.compile(r"^\s*(?:\[[A-Z][A-Z0-9]{1,9}-\d+\]\s*)+")


def find_issue_keys(message):
    """Every [PROJ-123] appearing anywhere in the message, in order, deduped.
    The brackets are what keep 'DO-178C' and 'ARINC-429' out."""
    seen, keys = set(), []
    for key in BRACKETED_KEY_RE.findall(message):
        key = key.upper()
        if key not in seen:
            seen.add(key)
            keys.append(key)
    return keys


def strip_issue_prefixes(text, issue_key):
    """Remove every leading issue reference, so '[SCRUM-5][SCRUM-9] msg' -> 'msg'."""
    rx = issue_prefix_re(issue_key)
    while True:
        stripped = rx.sub("", text, count=1)
        if stripped == text or not stripped.strip():
            return text if not stripped.strip() else stripped
        text = stripped


# --------------------------------------------------------------------------- #
# environment helpers
# --------------------------------------------------------------------------- #

def unquote_env(value, name):
    """cmd.exe keeps the quotes: `set X='abc'` stores  'abc'  including quotes.
    Strip a matching wrapping pair and say so, rather than failing with a 401."""
    if not value:
        return value
    stripped = value.strip()
    if len(stripped) >= 2 and stripped[0] == stripped[-1] and stripped[0] in "\"'":
        print(f"note: stripped wrapping {stripped[0]} characters from {name} "
              f"(in cmd.exe, use  set {name}=value  with no quotes)",
              file=sys.stderr)
        return stripped[1:-1]
    return stripped


# --------------------------------------------------------------------------- #
# subversion
# --------------------------------------------------------------------------- #

def svn_log_xml(svn_url, revision, username=None, password=None):
    cmd = ["svn", "log", "--xml", "-v", "--non-interactive",
           "-r", str(revision), svn_url]
    if username:
        cmd += ["--username", username]
    if password:
        cmd += ["--password", password, "--no-auth-cache"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        raise SystemExit("'svn' was not found on PATH - needed for --rev")
    if proc.returncode != 0:
        raise SystemExit(f"svn log r{revision} failed: {proc.stderr.strip()}")
    return proc.stdout


def parse_single_logentry(xml_text, revision):
    entry = ET.fromstring(xml_text).find("logentry")
    if entry is None:
        raise SystemExit(
            f"r{revision} returned no log entry. Point --svn-url at the "
            f"repository ROOT so every revision is visible from it.")
    return {
        "revision": int(entry.get("revision")),
        "author": (entry.findtext("author") or "unknown").strip(),
        "date": (entry.findtext("date") or "")[:10],
        "message": entry.findtext("msg") or "",
        "paths": [p.text for p in entry.findall("./paths/path") if p.text],
    }


def branch_label(paths):
    """Best-effort branch name from the changed paths of one revision."""
    if not paths:
        return "?"
    split = [p.strip("/").split("/") for p in paths]
    if len(split) == 1 and len(split[0]) > 1:
        split[0] = split[0][:-1]   # a lone path ends in the file name
    common = []
    for parts in zip(*split):
        if len(set(parts)) == 1:
            common.append(parts[0])
        else:
            break
    if not common:
        return "mixed"
    if common[0] == "trunk":
        return "trunk"
    if common[0] in ("branches", "tags"):
        rest = common[1:3]
        return "/".join(rest) if rest else common[0]
    return "/".join(common[:2])


BULLET_RE = re.compile(r"^\s*([*+\-]|\d+[.)])\s")


def first_paragraph(message):
    """Lines up to the first blank line or first bullet, joined into one line.

    A wrapped description ('...change i did\nsometimes with list') is one
    sentence, so taking only the first line would lose half of it. Bullet
    lists below it are detail, not summary, so they stop the scan."""
    lines = []
    for line in message.splitlines():
        if not line.strip():
            if lines:
                break
            continue          # skip leading blank lines
        if BULLET_RE.match(line):
            break
        lines.append(line.strip())
    return " ".join(lines)


def truncate(text, limit):
    """Cut at a word boundary, never mid-word."""
    if len(text) <= limit:
        return text
    cut = text[:limit - 3]
    if " " in cut:
        cut = cut[:cut.rfind(" ")]
    return cut.rstrip(" ,;:.-") + "..."


def short_message(message, issue_key, limit=None):
    """First paragraph, issue-key prefix removed, truncated at a word break."""
    text = first_paragraph(message)
    if not text:
        return "(no commit message)"
    if issue_key:
        text = strip_issue_prefixes(text, issue_key)
    else:
        stripped = LEADING_BRACKETS_RE.sub("", text)
        text = stripped if stripped.strip() else text
    return truncate(text, limit or MSG_MAX)


def build_title(entry, issue_key, limit=None):
    return (f"r{entry['revision']:0{REV_PAD}d} "
            f"[{branch_label(entry['paths'])}] "
            f"{short_message(entry['message'], issue_key, limit)}")


def websvn_revision_url(websvn_base, repo_name, revision):
    query = urllib.parse.urlencode({"repname": repo_name, "rev": revision,
                                    "isdir": "0"})
    return f"{websvn_base.rstrip('/')}/revision.php?{query}"


# --------------------------------------------------------------------------- #
# jira
# --------------------------------------------------------------------------- #

def request(base_url, email, token, method, path, payload=None):
    """Return (status, body_text). Never raises on HTTP error status."""
    url = f"{base_url.rstrip('/')}{path}"
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Basic " + base64.b64encode(
        f"{email}:{token}".encode()).decode("ascii"))
    req.add_header("Accept", "application/json")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", "replace")
    except urllib.error.URLError as exc:
        raise SystemExit(f"cannot reach {url}: {exc.reason}")


def diagnose_404(base_url, email, token, issue_key):
    """Jira answers 404 for 'no such issue' AND for 'no Link Issues permission'.
    Probe two cheap endpoints to tell the user which one it actually is."""
    status, body = request(base_url, email, token, "GET", "/rest/api/3/myself")
    if status == 401:
        return ("credentials rejected. Check JIRA_EMAIL is your Atlassian account "
                "email and JIRA_API_TOKEN is a current API token.")
    if status != 200:
        return f"could not identify the account ({status}): {body[:200]}"
    try:
        parsed = json.loads(body)
        who = parsed.get("emailAddress") or parsed.get("displayName") or "(unknown)"
    except ValueError:
        who = "(unknown)"

    status, _ = request(base_url, email, token, "GET",
                        f"/rest/api/3/issue/{issue_key}?fields=summary")
    if status == 200:
        return (f"authenticated as {who}, and {issue_key} is visible, but the "
                f"account lacks the 'Link Issues' project permission.")
    if status == 404:
        return (f"authenticated as {who}, but {issue_key} does not exist or is "
                f"not visible to that account. Check the project key and number.")
    return f"authenticated as {who}; unexpected {status} when reading {issue_key}."


def add_remote_link(base_url, email, token, issue_key, url,
                    title=None, summary=None, relationship="SVN commit",
                    global_id=None, dry_run=False):
    payload = {
        "globalId": global_id or "url:" + hashlib.sha1(url.encode()).hexdigest(),
        "application": {"type": "org.tigris.subversion", "name": "WebSVN"},
        "relationship": relationship,
        "object": {"url": url, "title": title or url},
    }
    if summary:
        payload["object"]["summary"] = summary[:250]

    path = f"/rest/api/3/issue/{issue_key}/remotelink"

    if dry_run:
        print(f"POST {base_url.rstrip('/')}{path}")
        print(json.dumps(payload, indent=2))
        return "dry-run"

    status, body = request(base_url, email, token, "POST", path, payload)
    if status == 201:
        return "created"
    if status == 200:
        return "updated"
    if status == 404:
        raise SystemExit("Jira returned 404 -> " +
                         diagnose_404(base_url, email, token, issue_key))
    if status in (401, 403):
        raise SystemExit(f"Jira auth/permission failure ({status}): {body[:300]}")
    raise SystemExit(f"Jira returned {status}: {body[:300]}")


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #

def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("issue",
                   help="Jira issue key, e.g. SCRUM-5. Use 'auto' with --rev to "
                        "link every [KEY-n] found in the commit message.")
    p.add_argument("url", nargs="?",
                   help='URL to attach, in double quotes. Omit when using --rev.')

    g = p.add_argument_group("revision mode")
    g.add_argument("--rev", type=int,
                   help="SVN revision; fetches the commit message and builds "
                        "both the WebSVN URL and the title")
    g.add_argument("--svn-url", default=os.environ.get("SVN_URL"),
                   help="SVN repository ROOT for --rev (default: $SVN_URL)")
    g.add_argument("--repo-name",
                   default=os.environ.get("SVN_REPO_NAME", DEFAULT_REPO_NAME),
                   help=f"WebSVN 'repname' (default: {DEFAULT_REPO_NAME})")
    g.add_argument("--websvn-base",
                   default=os.environ.get("WEBSVN_BASE_URL", DEFAULT_WEBSVN_BASE),
                   help=f"WebSVN base URL (default: {DEFAULT_WEBSVN_BASE})")
    g.add_argument("--svn-log-file",
                   help="read 'svn log --xml -v' output from a file instead of "
                        "calling svn (offline testing)")

    p.add_argument("--title", help="override the generated link text")
    p.add_argument("--msg-max", type=int, default=MSG_MAX,
                   help=f"characters of commit message kept in the title "
                        f"(default: {MSG_MAX})")
    p.add_argument("--summary", help="override the generated second line")
    p.add_argument("--relationship", default="SVN commit",
                   help="group heading in the Links panel (default: 'SVN commit')")
    p.add_argument("--global-id", help="override the dedup id")
    p.add_argument("--base-url",
                   default=os.environ.get("JIRA_BASE_URL", DEFAULT_BASE_URL))
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()

    if not args.rev and not args.url:
        p.error("give a URL, or --rev N")
    if args.rev and args.url:
        p.error("give either a URL or --rev, not both")

    auto = args.issue.strip().lower() == "auto"
    if auto and not args.rev:
        p.error("'auto' needs --rev, so the commit message can be read")

    url, title, summary = args.url, args.title, args.summary
    global_id = args.global_id
    issues = [args.issue]

    if args.rev:
        if args.svn_log_file:
            with open(args.svn_log_file, "r", encoding="utf-8") as fh:
                xml_text = fh.read()
        else:
            if not args.svn_url:
                p.error("--rev needs --svn-url (or $SVN_URL)")
            xml_text = svn_log_xml(
                args.svn_url, args.rev,
                unquote_env(os.environ.get("SVN_USERNAME"), "SVN_USERNAME"),
                unquote_env(os.environ.get("SVN_PASSWORD"), "SVN_PASSWORD"))

        entry = parse_single_logentry(xml_text, args.rev)

        if auto:
            issues = find_issue_keys(entry["message"])
            if not issues:
                raise SystemExit(
                    f"r{entry['revision']} has no [PROJ-123] reference in its "
                    f"commit message:\n  {first_paragraph(entry['message'])[:100]}")
            print(f"r{entry['revision']} references: {', '.join(issues)}")

        url = websvn_revision_url(args.websvn_base, args.repo_name,
                                  entry["revision"])
        title = title or build_title(entry, None if auto else args.issue,
                                     args.msg_max)
        summary = summary or f"{entry['author']}, {entry['date']}"
        # Revision-based id: stays stable even if the WebSVN URL scheme changes.
        global_id = global_id or f"svn:{args.repo_name}:r{entry['revision']}"
    else:
        query = url.split("?", 1)[1] if "?" in url else ""
        if query and "&" not in query:
            print('warning: the URL has only one query parameter. If the real '
                  'URL had more, cmd.exe truncated it at the "&" - re-run with '
                  'the URL in double quotes.', file=sys.stderr)

    email = unquote_env(os.environ.get("JIRA_EMAIL"), "JIRA_EMAIL")
    token = unquote_env(os.environ.get("JIRA_API_TOKEN"), "JIRA_API_TOKEN")
    if not args.dry_run and not (email and token):
        sys.exit("set JIRA_EMAIL and JIRA_API_TOKEN (or use --dry-run)")

    # The same globalId on different issues is fine - it is scoped per issue,
    # so each gets its own link and a re-run updates rather than duplicates.
    failures = 0
    for issue in issues:
        try:
            result = add_remote_link(
                args.base_url, email, token, issue, url,
                title=title, summary=summary, relationship=args.relationship,
                global_id=global_id, dry_run=args.dry_run)
        except SystemExit as exc:
            # One bad key must not stop the others from being linked.
            if len(issues) == 1:
                raise
            print(f"{issue}: FAILED - {exc}", file=sys.stderr)
            failures += 1
            continue
        print(f"{issue}: {result}")

    print(f"  title: {title}")
    print(f"  url:   {url}")
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()