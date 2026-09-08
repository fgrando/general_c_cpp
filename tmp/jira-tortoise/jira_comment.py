#!/usr/bin/env python3
r"""
jira_comment.py - add a comment to a Jira issue.

    python jira_comment.py --task SCRUM-5 --comment "this is a comment"

    python jira_comment.py --task SCRUM-5 --file notes.txt
    svn log -r 212 | python jira_comment.py --task SCRUM-5 --file -

Standard library only. Companion to jira_remotelink.py and uses the same
environment:

    JIRA_BASE_URL   default https://demo123467890.atlassian.net
    JIRA_EMAIL      your Atlassian account email      (no quotes in `set`!)
    JIRA_API_TOKEN  id.atlassian.com/manage-profile/security/api-tokens

Notes
-----
* Jira Cloud's v3 API wants the comment body in Atlassian Document Format,
  not a plain string. This script builds the ADF for you: blank lines start a
  new paragraph, single newlines become line breaks.
* --once skips posting when an identical comment already exists on the issue,
  which makes the script safe to re-run from a Jenkins job.
"""

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_BASE_URL = "https://demo123467890.atlassian.net"
HTTP_TIMEOUT = 30


# --------------------------------------------------------------------------- #
# environment
# --------------------------------------------------------------------------- #

def unquote_env(value, name):
    """cmd.exe keeps the quotes: `set X='abc'` stores  'abc'  including quotes."""
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
# http
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
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", "replace")
    except urllib.error.URLError as exc:
        raise SystemExit(f"cannot reach {url}: {exc.reason}")


def diagnose_404(base_url, email, token, issue_key):
    """Jira answers 404 for 'no such issue' AND for 'not allowed to comment'."""
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
                f"account lacks the 'Add Comments' project permission.")
    if status == 404:
        return (f"authenticated as {who}, but {issue_key} does not exist or is "
                f"not visible to that account. Check the project key and number.")
    return f"authenticated as {who}; unexpected {status} when reading {issue_key}."


# --------------------------------------------------------------------------- #
# Atlassian Document Format
# --------------------------------------------------------------------------- #

def text_to_adf(text):
    """Plain text -> ADF doc. Blank line = new paragraph, newline = line break."""
    paragraphs = [p for p in text.replace("\r\n", "\n").split("\n\n")]
    content = []
    for para in paragraphs:
        lines = para.split("\n")
        while lines and not lines[-1].strip():
            lines.pop()          # no trailing hardBreak from a final newline
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines:
            continue
        nodes = []
        for i, line in enumerate(lines):
            if i:
                nodes.append({"type": "hardBreak"})
            if line:
                nodes.append({"type": "text", "text": line})
        content.append({"type": "paragraph", "content": nodes})
    if not content:
        content = [{"type": "paragraph", "content": []}]
    return {"type": "doc", "version": 1, "content": content}


def adf_to_text(node):
    """Flatten an ADF body back to plain text, for --once comparison."""
    if isinstance(node, str):
        return node
    if isinstance(node, list):
        return "".join(adf_to_text(n) for n in node)
    if not isinstance(node, dict):
        return ""
    kind = node.get("type")
    if kind == "text":
        return node.get("text", "")
    if kind == "hardBreak":
        return "\n"
    inner = adf_to_text(node.get("content", []))
    if kind == "paragraph":
        return inner + "\n\n"
    return inner


def normalise(text):
    return " ".join(text.split())


# --------------------------------------------------------------------------- #
# jira operations
# --------------------------------------------------------------------------- #

def existing_comment_texts(base_url, email, token, issue_key):
    """All comment bodies on the issue, flattened to plain text."""
    texts, start_at = [], 0
    while True:
        status, body = request(
            base_url, email, token, "GET",
            f"/rest/api/3/issue/{issue_key}/comment"
            f"?startAt={start_at}&maxResults=100")
        if status == 404:
            raise SystemExit("Jira returned 404 -> " +
                             diagnose_404(base_url, email, token, issue_key))
        if status != 200:
            raise SystemExit(f"could not read comments ({status}): {body[:300]}")
        data = json.loads(body)
        comments = data.get("comments", [])
        for comment in comments:
            texts.append(adf_to_text(comment.get("body", "")))
        start_at += len(comments)
        if not comments or start_at >= data.get("total", 0):
            return texts


def add_comment(base_url, email, token, issue_key, text,
                visibility=None, dry_run=False):
    payload = {"body": text_to_adf(text)}
    if visibility:
        kind, _, value = visibility.partition(":")
        payload["visibility"] = {"type": kind.strip() or "role",
                                 "value": value.strip() or kind.strip()}

    path = f"/rest/api/3/issue/{issue_key}/comment"

    if dry_run:
        print(f"POST {base_url.rstrip('/')}{path}")
        print(json.dumps(payload, indent=2))
        return None

    status, body = request(base_url, email, token, "POST", path, payload)
    if status == 201:
        try:
            return json.loads(body).get("id")
        except ValueError:
            return None
    if status == 404:
        raise SystemExit("Jira returned 404 -> " +
                         diagnose_404(base_url, email, token, issue_key))
    if status in (401, 403):
        raise SystemExit(f"Jira auth/permission failure ({status}): {body[:300]}")
    if status == 400:
        raise SystemExit(f"Jira rejected the comment body (400): {body[:300]}")
    raise SystemExit(f"Jira returned {status}: {body[:300]}")


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #

def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--task", "-t", required=True,
                   help="Jira issue key, e.g. SCRUM-5")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--comment", "-c", help="comment text, in double quotes")
    src.add_argument("--file", "-f",
                     help="read the comment from a UTF-8 file, or '-' for stdin")
    p.add_argument("--once", action="store_true",
                   help="skip if an identical comment is already on the issue "
                        "(makes Jenkins re-runs safe)")
    p.add_argument("--visibility",
                   help="restrict the comment, e.g. 'role:Developers' or "
                        "'group:jira-developers'")
    p.add_argument("--base-url",
                   default=os.environ.get("JIRA_BASE_URL", DEFAULT_BASE_URL))
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()

    if args.file:
        if args.file == "-":
            text = sys.stdin.read()
        else:
            with open(args.file, "rb") as fh:
                raw = fh.read()
            for encoding in ("utf-8-sig", "utf-8", "cp1252", "latin-1"):
                try:
                    text = raw.decode(encoding)
                    break
                except UnicodeDecodeError:
                    continue
            else:
                text = raw.decode("utf-8", "replace")
    else:
        text = args.comment

    if not text or not text.strip():
        sys.exit("refusing to post an empty comment")

    email = unquote_env(os.environ.get("JIRA_EMAIL"), "JIRA_EMAIL")
    token = unquote_env(os.environ.get("JIRA_API_TOKEN"), "JIRA_API_TOKEN")
    if not args.dry_run and not (email and token):
        sys.exit("set JIRA_EMAIL and JIRA_API_TOKEN (or use --dry-run)")

    if args.once and not args.dry_run:
        wanted = normalise(text)
        for existing in existing_comment_texts(args.base_url, email, token,
                                               args.task):
            if normalise(existing) == wanted:
                print(f"{args.task}: identical comment already present, skipped")
                return

    comment_id = add_comment(args.base_url, email, token, args.task, text,
                             visibility=args.visibility, dry_run=args.dry_run)

    if args.dry_run:
        print(f"{args.task}: dry-run")
        return

    print(f"{args.task}: comment added")
    if comment_id:
        print(f"  {args.base_url.rstrip('/')}/browse/{args.task}"
              f"?focusedCommentId={comment_id}")


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    main()
