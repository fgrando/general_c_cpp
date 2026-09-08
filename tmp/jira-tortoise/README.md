# SVN ↔ Jira commit traceability — client-side only

Three pieces, none of which need server access or admin rights:

| File | What it does |
|---|---|
| `set-bugtraq-props.bat` | Adds a **Jira issue** box to the TortoiseSVN commit dialog and prefixes the message with `[SCRUM-5]` |
| `check_commit_hook.py` + `.bat` | Rejects a commit whose message doesn't start with `[PROJ-123]` |
| `jira_remotelink.py` | Attaches the WebSVN revision to the Jira issue as a remote link |

---

## 1. Repository properties (do this once, for everyone)

The properties live in the repository, so they follow the checkout. Folder
name and machine don't matter — this is what makes it path-independent.

```bat
svn checkout --depth empty https://svn.internal/svn/myproject C:\tmp\rootwc
cd /d C:\tmp\rootwc
C:\svn-tools\set-bugtraq-props.bat
svn commit -m "[NOJIRA] add TortoiseSVN issue tracker integration" .
```

A `--depth empty` checkout pulls no files, only the root folder, so this takes
seconds even on a large repository.

Properties set — **regex mode**, so you type the keys into the message text
itself and TortoiseSVN turns them into links as you type:

| Property | Value | Why |
|---|---|---|
| `bugtraq:url` | `https://demo123467890.atlassian.net/browse/%BUGID%` | What each detected key links to |
| `bugtraq:logregex` | two lines, from `bugtraq-logregex.txt` | Finds every `[KEY-n]` in the message |

```
\[[A-Z][A-Z0-9]+-\d+\](?:\s*\[[A-Z][A-Z0-9]+-\d+\])*
([A-Z][A-Z0-9]+-\d+)
```

Line 1 locates a block of one or more bracketed references; line 2 pulls each
bare key out of it. Requiring the brackets is what keeps `DO-178C` and
`ARINC-429` from being mistaken for issue keys.

`logregex` takes precedence over `bugtraq:message`, so the batch file deletes
the input-field properties (`bugtraq:message`, `append`, `label`, `number`,
`warnifnoissue`) to avoid confusion later.

Since TortoiseSVN 1.8 these are *inherited* properties: set on the root, they
apply implicitly to every subfolder.

To set them by hand instead: right-click the root working copy folder →
**TortoiseSVN → Properties → New → Bugtraq**. Type `%BUGID%` literally there —
the `%%` doubling in the batch file is a batch-escaping artefact only.

### If you prefer the separate input box

Set `bugtraq:message` = `[%BUGID%]`, `bugtraq:append` = `false`,
`bugtraq:number` = `false`, `bugtraq:warnifnoissue` = `true`, and delete
`bugtraq:logregex`. You then get one box, filled before the message — simpler,
but no in-message links and multiple issues are clumsier.

---

## 2. The check-commit hook (per machine)

Copy `check_commit_hook.py` and `check_commit_hook.bat` to a fixed folder,
e.g. `C:\svn-tools\`. Then:

**TortoiseSVN → Settings → Hook Scripts → Add…**

| Field | Value |
|---|---|
| Hook Type | **Check Commit Hook** |
| Working Copy Path | `C:\dev` — the *parent* of all your checkouts |
| Command Line | `C:\svn-tools\check_commit_hook.bat` |
| Wait for the script to finish | ☑ checked |
| Hide the script while running | ☑ checked |
| Force the script to run | ☐ unchecked |

**Working Copy Path is the answer to "the project folder is always
different".** TortoiseSVN searches *upwards* from wherever you commit until it
finds a matching hook path, so one entry at a common parent covers every
project under it, whatever it's called.

**Both checkboxes matter.** Without *Wait for the script to finish* the exit
code is never read. Without *Hide the script while running* the documentation
notes a hook that returns an error may not stop the operation.

### Why Check Commit and not Pre Commit

`check-commit` fires after you press OK but **before the dialog closes**, so a
rejection returns you to the dialog with your typed message intact.
`pre-commit` fires after the dialog is gone — a rejection there loses the
message.

### Accepted message formats

```
[SCRUM-5] some description of the change i did
[SCRUM-5][SCRUM-9] shared fix across both issues
[NOJIRA] bump build number
```

Rejected: `SCRUM-5: desc`, `@SCRUM-5 desc`, `fixed stuff for [SCRUM-5]`,
`[OTHER-9] desc` (unknown project), `[SCRUM-5]` alone (no description).

Tunables at the top of `check_commit_hook.py`:

```python
PROJECT_KEYS = ["SCRUM"]      # [] accepts any well-formed key
ALLOW_NOJIRA = True           # the escape hatch for ticketless commits
REQUIRE_KEY_AT_START = True   # False = a key anywhere in the message is enough
REQUIRE_DESCRIPTION = True
MIN_DESCRIPTION_CHARS = 5
```

`REQUIRE_KEY_AT_START = True` still allows extra references later in the
message — `[SCRUM-5] desc, also refs [SCRUM-12]` is accepted. It only insists
that the *first* thing in the message is a key, which is what keeps the
generated Jira link titles readable.

---

## 3. Pushing the link into Jira

```bat
set JIRA_EMAIL=fernandozatt@gmail.com
set JIRA_API_TOKEN=your_token_no_quotes
set SVN_URL=https://svn.internal/svn/myproject

rem one named issue
python jira_remotelink.py SCRUM-5 --rev 212

rem every [KEY-n] the commit message mentions
python jira_remotelink.py auto --rev 212
```

`auto` reads the commit message, collects every bracketed key, and posts the
same link to each issue. One bad key is reported and skipped rather than
aborting the rest; the exit code is non-zero if any failed.

`SVN_URL` must be the repository **root**, so `svn log -r N` can find any
revision regardless of which branch it touched.

---

## Limits worth knowing

Client-side hooks and `bugtraq:` properties are **advisory**. They are bypassed
by the command-line client, by any other SVN client, and by anyone who clears
the setting. They make the convention easy to follow; they do not enforce it.

For DO-178C traceability evidence, only a server-side `pre-commit` hook is
authoritative. When server access becomes possible, the same regex moves there
unchanged:

```python
r"^\[[A-Z][A-Z0-9]+-\d+\](\[[A-Z][A-Z0-9]+-\d+\])*\s+\S"
```