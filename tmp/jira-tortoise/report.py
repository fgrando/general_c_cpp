#!/usr/bin/env python3
"""
report.py -- traceability report over commits.md and changelog.md.

    python report.py [COMMITS_MD] [CHANGELOG_MD]

Defaults to commits.md and changelog.md in the working directory. Prints
every changelog entry with the commits traced to it, then the commits that
reach no entry.

Also importable, for scripts that want the changelog as a dictionary:

    import report
    entries = report.changelog2dict('changelog.md')
    # {'AVX-1187': {'ref':..., 'jira': [...], 'type':..., 'text':...}, ...}

Standard library only, plus svntrace for commits.md. Python 3.6+.
"""

import io
import re
import sys
import textwrap

import svntrace

__all__ = ['changelog2dict', 'trace', 'CHANGELOG_TYPES']

CHANGELOG_TYPES = (
    'new', 'changed', 'bugfix', 'deprecation',
    'removed', 'security', 'ci', 'known-issue',
)

ENTRIES_HEADING = '## Entries'
CELL_SPLIT_RE = re.compile(r'(?<!\\)\|')
WIDTH = 78
INDENT = '    '


# --------------------------------------------------------------------------
# changelog.md
# --------------------------------------------------------------------------

def changelog2dict(source):
    """Parse the table under '## Entries'. Path or markdown text."""
    if '\n' not in source and source.strip():
        with io.open(source, 'r', encoding='utf-8') as handle:
            source = handle.read()

    entries = {}
    order = []
    in_entries = False

    for line in source.replace('\r\n', '\n').split('\n'):
        if line.startswith('## '):
            in_entries = line.strip() == ENTRIES_HEADING
            continue
        if not in_entries:
            continue

        stripped = line.strip()
        if not stripped.startswith('|'):
            continue

        cells = [cell.strip() for cell in CELL_SPLIT_RE.split(stripped)[1:-1]]
        if len(cells) != 4:
            continue

        ref, jira, kind, text = cells
        if ref.lower() == 'ref':                      # header row
            continue
        if set(ref) <= set('-: '):                    # separator row
            continue

        entries[ref] = {
            'ref': ref,
            'jira': [] if jira.lower() in ('none', '') else
                    [key.strip() for key in jira.split(',') if key.strip()],
            'type': kind,
            'text': text.replace('\\|', '|'),
        }
        order.append(ref)

    return dict(entries), order


# --------------------------------------------------------------------------
# tracing
# --------------------------------------------------------------------------

def trace(commits, entries):
    """Group commits by the changelog entry they point at.

    Returns (traced, untriaged, excluded, dangling) where traced maps an
    entry id to its commits, oldest first.
    """
    traced = dict((ref, []) for ref in entries)
    untriaged = []
    excluded = []
    dangling = []

    for commit in commits:
        refs = commit['changelog_ref']

        if refs is None:
            untriaged.append(commit)
            continue
        if not refs:
            excluded.append(commit)
            continue

        for ref in refs:
            if ref in traced:
                traced[ref].append(commit)
            else:
                dangling.append((commit, ref))

    for ref in traced:
        traced[ref].sort(key=lambda item: item['rev'])

    return traced, untriaged, excluded, dangling


# --------------------------------------------------------------------------
# printing
# --------------------------------------------------------------------------

def _subject(commit):
    first = commit['msg'].split('\n', 1)[0].strip()
    return first or '(empty message)'


def _commit_line(commit, width=WIDTH):
    prefix = '%s r%-7d %s  %-12s ' % (
        INDENT, commit['rev'], commit['date'][:10], commit['author'][:12])
    room = max(20, width - len(prefix))
    subject = _subject(commit)
    if len(subject) > room:
        # plain ASCII: a Windows console in cp850/cp1252 raises
        # UnicodeEncodeError on anything fancier
        subject = subject[:room - 3] + '...'
    return prefix + subject


def _print_commits(commits, empty_note=None):
    if not commits:
        if empty_note:
            print('%s(%s)' % (INDENT, empty_note))
        return
    for commit in commits:
        print(_commit_line(commit))


def print_report(commits, entries, order, out=None):
    traced, untriaged, excluded, dangling = trace(commits, entries)

    print('=' * WIDTH)
    print('CHANGELOG TRACEABILITY')
    print('=' * WIDTH)

    for ref in order:
        entry = entries[ref]
        jira = ', '.join(entry['jira']) if entry['jira'] else 'none'
        print('')
        print('%s  [%s]  jira: %s' % (ref, entry['type'], jira))
        for line in textwrap.wrap(entry['text'], WIDTH - len(INDENT)):
            print(INDENT + line)
        print('')
        _print_commits(traced[ref], 'no commits traced to this entry')

    print('')
    print('=' * WIDTH)
    print('UNTRACED COMMITS')
    print('=' * WIDTH)

    print('')
    print('Awaiting triage (changelog-ref: ?)')
    _print_commits(untriaged, 'none')

    print('')
    print('Deliberately excluded (changelog-ref: none)')
    _print_commits(excluded, 'none')

    if dangling:
        print('')
        print('Broken references (no such entry in changelog.md)')
        for commit, ref in dangling:
            print('%s r%-7d -> %s' % (INDENT, commit['rev'], ref))

    # entry types outside the closed vocabulary
    bad_types = [(ref, entries[ref]['type']) for ref in order
                 if entries[ref]['type'] not in CHANGELOG_TYPES]
    if bad_types:
        print('')
        print('Unknown types')
        for ref, kind in bad_types:
            print('%s%-10s %r' % (INDENT, ref, kind))

    # a JIRA-shaped ref must agree with the commit's own jira field
    mismatched = []
    for ref in order:
        if not entries[ref]['jira']:
            continue
        for commit in traced[ref]:
            if not set(entries[ref]['jira']) & set(commit['jira'] or []):
                mismatched.append((commit['rev'], ref))
    if mismatched:
        print('')
        print('Jira mismatch (commit jira does not include the entry jira)')
        for rev, ref in mismatched:
            print('%s r%-7d -> %s' % (INDENT, rev, ref))

    print('')
    print('-' * WIDTH)
    print('%d entries, %d commits: %d traced, %d untriaged, %d excluded'
          % (len(entries), len(commits),
             len(commits) - len(untriaged) - len(excluded),
             len(untriaged), len(excluded)))

    return 1 if (dangling or bad_types or mismatched) else 0


def main(argv):
    commits_path = argv[0] if len(argv) > 0 else 'commits.md'
    changelog_path = argv[1] if len(argv) > 1 else 'changelog.md'

    try:
        commits = svntrace.load(commits_path)['commits']
        entries, order = changelog2dict(changelog_path)
    except (svntrace.ParseError, IOError) as exc:
        print('error: %s' % exc, file=sys.stderr)
        return 2

    return print_report(commits, entries, order)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
