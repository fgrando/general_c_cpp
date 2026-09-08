#!/usr/bin/env python3
r"""
check_commit_hook.py - TortoiseSVN client-side check-commit hook.

Rejects a commit whose log message does not start with a Jira issue key in
brackets:

    [SCRUM-5] some description of the change i did
    [SCRUM-5][SCRUM-9] shared fix          <- multiple issues allowed
    [NOJIRA] bump build number             <- escape hatch, see ALLOW_NOJIRA

TortoiseSVN calls a check-commit hook as:

    <command>  PATH  MESSAGEFILE  CWD

    PATH         temp file, one affected path per line, UTF-8
    MESSAGEFILE  temp file holding the log message, UTF-8
    CWD          common root of the affected paths

Exit 0 accepts the commit. Any non-zero exit keeps the commit dialog open and
shows whatever this script printed, so the message text is not lost.

Configure it in TortoiseSVN -> Settings -> Hook Scripts (see README).
"""

import io
import os
import re
import sys

# --------------------------------------------------------------------------- #
# configuration
# --------------------------------------------------------------------------- #

# Accepted Jira project keys. Empty list = accept any well-formed key.
PROJECT_KEYS = ["SCRUM"]

# Allow [NOJIRA] for commits that genuinely have no ticket (build fixes etc).
ALLOW_NOJIRA = True
NOJIRA_TAG = "NOJIRA"

# True  = at least one key must be at the START of the message (keeps the
#          generated Jira link titles tidy).
# False = a key anywhere in the message is enough.
REQUIRE_KEY_AT_START = True

# Require some description after the key(s), not just the bare tag.
REQUIRE_DESCRIPTION = True
MIN_DESCRIPTION_CHARS = 5

# --------------------------------------------------------------------------- #

KEY = r"[A-Z][A-Z0-9]{1,9}-\d+"
# One or more bracketed tags at the very start, then the description.
PREFIX_RE = re.compile(rf"^\s*((?:\[(?:{KEY}|{NOJIRA_TAG})\]\s*)+)(.*)$",
                       re.DOTALL)
TAG_RE = re.compile(rf"\[({KEY}|{NOJIRA_TAG})\]")
# Any bracketed reference, anywhere in the message.
ANYWHERE_RE = re.compile(rf"\[(?:{KEY}|{NOJIRA_TAG})\]")


def read_text(path):
    """TortoiseSVN writes these temp files as UTF-8; tolerate a BOM and,
    defensively, a legacy code page."""
    with open(path, "rb") as fh:
        raw = fh.read()
    for encoding in ("utf-8-sig", "utf-8", "cp1252", "latin-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def fail(*lines):
    for line in lines:
        print(line)
    sys.exit(1)


def main():
    if len(sys.argv) < 3:
        fail("check_commit_hook: expected TortoiseSVN to pass "
             "PATH MESSAGEFILE CWD.",
             f"Got {len(sys.argv) - 1} argument(s): {sys.argv[1:]}",
             "",
             "Check the hook command line in Settings -> Hook Scripts.")

    message_file = sys.argv[2]
    if not os.path.isfile(message_file):
        fail(f"check_commit_hook: message file not found: {message_file}")

    message = read_text(message_file)

    if not message.strip():
        fail("Empty commit message.",
             "",
             "Start it with the Jira issue in brackets, e.g.:",
             "    [SCRUM-5] short description of the change")

    if not REQUIRE_KEY_AT_START and ANYWHERE_RE.search(message):
        sys.exit(0)

    match = PREFIX_RE.match(message)
    if not match:
        first_line = message.strip().splitlines()[0][:70]
        fail("The commit message must start with a Jira issue key in brackets.",
             "",
             f"  yours:    {first_line}",
             "  expected: [SCRUM-5] short description of the change",
             "",
             "Multiple issues are allowed: [SCRUM-5][SCRUM-9] shared fix"
             + ("\nUse [NOJIRA] for commits with no ticket." if ALLOW_NOJIRA else ""))

    tags = TAG_RE.findall(match.group(1))
    description = match.group(2).strip()

    if not ALLOW_NOJIRA and NOJIRA_TAG in tags:
        fail(f"[{NOJIRA_TAG}] is not accepted in this repository.",
             "Reference the Jira issue this change belongs to.")

    issue_tags = [t for t in tags if t != NOJIRA_TAG]

    if PROJECT_KEYS:
        wrong = [t for t in issue_tags
                 if t.split("-", 1)[0].upper() not in
                 {k.upper() for k in PROJECT_KEYS}]
        if wrong:
            fail(f"Unknown project key: {', '.join(wrong)}",
                 f"Accepted project keys: {', '.join(PROJECT_KEYS)}")

    if not issue_tags and NOJIRA_TAG not in tags:
        fail("No Jira issue key found in the commit message.")

    if REQUIRE_DESCRIPTION and len(description) < MIN_DESCRIPTION_CHARS:
        fail("The commit message needs a description after the issue key.",
             "",
             "  expected: [SCRUM-5] short description of the change")

    sys.exit(0)


if __name__ == "__main__":
    # TortoiseSVN reads the hook's output; make sure non-ASCII survives.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, io.UnsupportedOperation):
        pass
    main()