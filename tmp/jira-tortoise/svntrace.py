#!/usr/bin/env python3
"""
svntrace.py -- import SVN commits into commits.md, and parse it back.

Command line:

    python svntrace.py SVN_URL COMMITS_MD

Reads the machine-owned header of COMMITS_MD, fetches every revision from
last-imported+1 to HEAD, appends them as untriaged records and rewrites the
file. Existing records are never modified.

As a module:

    import svntrace

    data = svntrace.commits2dict('commits.md')   # path or markdown text
    for commit in data['commits']:
        ...
    svntrace.dict2commits(data)                  # -> markdown text
    svntrace.save('commits.md', data)            # write it back

Records are kept newest first, in the file and in the dictionary.

The round-trip is lossless: parsing a file and writing it back yields the
same bytes, so a caller can load the dictionary, edit it and save it.

Standard library only. Python 3.6+.

Dictionary shape
----------------
{
  'preamble': [str],            # everything above the first record, verbatim
  'header': {                   # the '- key: value' fields found in it
      'repo': str,
      'last_imported': int,
      'path_limit': int,
      'imported_at': str,
  },
  'commits': [                  # newest revision first, JSON-serialisable
      {
        'rev':             int,
        'jira':            None | [str],   # None = untriaged '?';  [] = 'none'
        'changelog_ref':   None | [str],   # None = untriaged '?';  [] = 'none'
        'date':            str,            # UTC, e.g. '2026-08-03T06:41:18Z'
        'author':          str,
        'file_count':      int,            # authoritative count from svn
        'note':            str,            # human free text
        'msg':             str,            # verbatim commit message
        'files':           [{'action': 'M', 'path': '/trunk/...'}],
        'files_truncated': int,            # paths omitted by path-limit
      },
  ],
}

None vs [] is load-bearing: None means nobody has looked at this revision
yet, [] means someone looked and decided nothing applies.
"""

import io
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone

__all__ = [
    'commits2dict', 'dict2commits', 'load', 'save',
    'by_rev', 'update_from_svn', 'svn_log',
    'parse_date', 'format_date', 'to_utc', 'ParseError',
]

REV_HEAD_RE = re.compile(r'^##\s+r(\d+)\s*$')
FIELD_RE = re.compile(r'^-\s+([A-Za-z][A-Za-z0-9_-]*):\s?(.*)$')
FENCE_RE = re.compile(r'^(~{3,})\s*([A-Za-z0-9_-]*)\s*$')
PATH_RE = re.compile(r'^([AMDR])\s+(.+?)\s*$')
TRUNC_RE = re.compile(r'^\.\.\.\s+(\d+)\s+more paths not listed\s*$')
JIRA_KEY_RE = re.compile(r'\b([A-Z][A-Z0-9]+-\d+)\b')
DATE_RE = re.compile(
    r'^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(Z|[+-]\d{2}:\d{2})$')

INT_HEADER_FIELDS = ('last_imported', 'path_limit')
LIST_FIELDS = ('jira', 'changelog_ref')
UNTRIAGED = '?'
NONE = 'none'
DEFAULT_PATH_LIMIT = 10


class ParseError(Exception):
    pass


# --------------------------------------------------------------------------
# dates
# --------------------------------------------------------------------------

def parse_date(value):
    """'2026-08-03T06:41:18Z' -> aware datetime.

    Hand-rolled rather than strptime('%z'): Python 3.6 rejects the colon in
    the offset.
    """
    match = DATE_RE.match((value or '').strip())
    if not match:
        raise ParseError('not an ISO-8601 timestamp: %r' % value)

    year, month, day, hour, minute, second, suffix = match.groups()
    if suffix == 'Z':
        tzinfo = timezone.utc
    else:
        sign = -1 if suffix[0] == '-' else 1
        tzinfo = timezone(sign * timedelta(hours=int(suffix[1:3]),
                                           minutes=int(suffix[4:6])))
    return datetime(int(year), int(month), int(day),
                    int(hour), int(minute), int(second), tzinfo=tzinfo)


def format_date(moment):
    """Aware datetime -> ISO-8601. UTC is written with a trailing Z."""
    offset = moment.utcoffset()
    if offset is None or int(offset.total_seconds()) == 0:
        return moment.strftime('%Y-%m-%dT%H:%M:%S') + 'Z'
    stamp = moment.strftime('%Y-%m-%dT%H:%M:%S')
    total = int(offset.total_seconds())
    sign = '-' if total < 0 else '+'
    total = abs(total)
    return '%s%s%02d:%02d' % (stamp, sign, total // 3600, (total % 3600) // 60)


def to_utc(value):
    """Any ISO-8601 timestamp this module accepts -> UTC, trailing Z."""
    return format_date(parse_date(value).astimezone(timezone.utc))


def _svn_date(raw):
    """svn's timestamp, microseconds dropped.

    svn already reports UTC, so this is a truncation and not a conversion --
    stored times are UTC precisely so that DST never enters the picture.
    """
    raw = (raw or '').strip()
    if '.' in raw:
        raw = raw.split('.', 1)[0] + 'Z'
    if not raw:
        return ''
    return to_utc(raw)


# --------------------------------------------------------------------------
# parsing
# --------------------------------------------------------------------------

def _parse_refs(value):
    value = value.strip()
    if value in ('', UNTRIAGED):
        return None
    if value.lower() == NONE:
        return []
    return [part.strip() for part in value.split(',') if part.strip()]


def _format_refs(value):
    if value is None:
        return UNTRIAGED
    if not value:
        return NONE
    return ', '.join(value)


def _read_fence(lines, index, fence):
    """Consume a fenced block. A closing fence must be at least as long as
    the opening one, so '~~~' inside a '~~~~msg' block does not end it."""
    open_len = len(fence)
    i = index + 1
    body = []
    while i < len(lines):
        stripped = lines[i].rstrip()
        if stripped and stripped == '~' * len(stripped) and len(stripped) >= open_len:
            return '\n'.join(body), i + 1
        body.append(lines[i])
        i += 1
    raise ParseError('unterminated fence opened at line %d' % (index + 1))


def _fence_for(text):
    """Shortest tilde fence that cannot be closed by content inside text."""
    longest = 0
    for line in text.split('\n'):
        stripped = line.rstrip()
        if stripped and stripped == '~' * len(stripped):
            longest = max(longest, len(stripped))
    return '~' * max(3, longest + 1)


def _parse_files(body):
    files = []
    truncated = 0
    for line in body.split('\n'):
        if not line.strip():
            continue
        trunc = TRUNC_RE.match(line)
        if trunc:
            truncated = int(trunc.group(1))
            continue
        path = PATH_RE.match(line)
        if path:
            files.append({'action': path.group(1), 'path': path.group(2)})
    return files, truncated


def _blank_commit(rev):
    return {
        'rev': rev, 'jira': None, 'changelog_ref': None, 'date': '',
        'author': '', 'file_count': 0, 'note': '', 'msg': '',
        'files': [], 'files_truncated': 0,
    }


def commits2dict(source):
    """Parse commits.md. `source` is a file path or the markdown text itself."""
    if '\n' not in source and source.strip():
        source = _read_text(source)
    lines = source.replace('\r\n', '\n').split('\n')

    data = {'preamble': [], 'header': {}, 'commits': []}
    seen = set()
    i, total = 0, len(lines)

    while i < total and not REV_HEAD_RE.match(lines[i]):
        data['preamble'].append(lines[i])
        field = FIELD_RE.match(lines[i])
        if field:
            key = field.group(1).replace('-', '_')
            value = field.group(2).strip()
            if key in INT_HEADER_FIELDS:
                try:
                    value = int(value)
                except ValueError:
                    raise ParseError('header %s is not an integer: %r'
                                     % (field.group(1), value))
            data['header'][key] = value
        i += 1

    while i < total:
        head = REV_HEAD_RE.match(lines[i])
        if not head:
            i += 1
            continue

        rev = int(head.group(1))
        if rev in seen:
            raise ParseError('revision r%d appears more than once' % rev)
        seen.add(rev)

        commit = _blank_commit(rev)
        i += 1
        pending = None

        while i < total and not REV_HEAD_RE.match(lines[i]):
            line = lines[i]

            fence = FENCE_RE.match(line)
            if fence:
                body, i = _read_fence(lines, i, fence.group(1))
                if fence.group(2) == 'msg':
                    commit['msg'] = body.strip('\n')
                elif fence.group(2) == 'files':
                    commit['files'], commit['files_truncated'] = _parse_files(body)
                pending = None
                continue

            field = FIELD_RE.match(line)
            if field:
                pending = field.group(1).replace('-', '_')
                value = field.group(2).strip()
                if pending in LIST_FIELDS:
                    commit[pending] = _parse_refs(value)
                elif pending == 'files':
                    commit['file_count'] = int(value) if value.isdigit() else 0
                elif pending in ('date', 'author', 'note'):
                    commit[pending] = value
                i += 1
                continue

            if pending == 'note' and line[:1] in (' ', '\t') and line.strip():
                commit['note'] = (commit['note'] + ' ' + line.strip()).strip()

            i += 1

        data['commits'].append(commit)

    data['commits'].sort(key=lambda item: item['rev'], reverse=True)
    return data


# --------------------------------------------------------------------------
# writing
# --------------------------------------------------------------------------

def _field(key, value):
    """'- key: value', with no trailing space when the value is empty."""
    value = '' if value is None else str(value)
    return ('- %s: %s' % (key, value)) if value else ('- %s:' % key)


def _format_commit(commit):
    out = [
        '## r%d' % commit['rev'],
        _field('jira', _format_refs(commit['jira'])),
        _field('changelog-ref', _format_refs(commit['changelog_ref'])),
        _field('date', commit['date']),
        _field('author', commit['author']),
        _field('files', commit['file_count']),
        _field('note', commit['note']),
        '',
    ]

    fence = _fence_for(commit['msg'])
    out += [fence + 'msg', commit['msg'], fence, '']

    if commit['files'] or commit['files_truncated']:
        out.append('~~~files')
        out += ['%s %s' % (item['action'], item['path']) for item in commit['files']]
        if commit['files_truncated']:
            out.append('... %d more paths not listed' % commit['files_truncated'])
        out += ['~~~', '']

    return '\n'.join(out)


def dict2commits(data):
    """Render the dictionary back to markdown.

    The preamble is re-emitted verbatim, except that any '- key: value' line
    it contains is refreshed from data['header'] -- so comments and notes
    written by hand at the top of the file survive.
    """
    preamble = []
    for line in data.get('preamble', []):
        field = FIELD_RE.match(line)
        if field:
            key = field.group(1).replace('-', '_')
            if key in data.get('header', {}):
                line = _field(field.group(1), data['header'][key])
        preamble.append(line)

    while preamble and not preamble[-1].strip():
        preamble.pop()

    body = [_format_commit(commit)
            for commit in sorted(data['commits'],
                                 key=lambda item: item['rev'], reverse=True)]
    return '\n'.join(preamble) + '\n\n' + '\n'.join(body)


def by_rev(data):
    """{revision: commit} view, for callers that want lookup by number."""
    return dict((commit['rev'], commit) for commit in data['commits'])


def _read_text(path):
    with io.open(path, 'r', encoding='utf-8') as handle:
        return handle.read().replace('\r\n', '\n')


def load(path):
    return commits2dict(_read_text(path))


def save(path, data):
    with io.open(path, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(dict2commits(data))


# --------------------------------------------------------------------------
# svn
# --------------------------------------------------------------------------

def svn_log(url, start, end='HEAD'):
    """Run `svn log --xml -v` and return a list of raw commit dicts."""
    cmd = ['svn', 'log', '--xml', '-v', '-r', '%d:%s' % (start, end), url]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()

    if proc.returncode != 0:
        text = stderr.decode('utf-8', 'replace')
        if 'No such revision' in text:
            return []
        raise RuntimeError('svn log failed: %s' % text.strip())

    # ElementTree must be given bytes: svn emits an encoding declaration and
    # passing a decoded str raises ValueError.
    root = ET.fromstring(stdout)

    entries = []
    for node in root.findall('logentry'):
        msg = node.find('msg')
        date = node.find('date')
        author = node.find('author')

        files = []
        paths = node.find('paths')
        if paths is not None:
            for path in paths.findall('path'):
                files.append({'action': path.get('action') or 'M',
                              'path': (path.text or '').strip()})
        files.sort(key=lambda item: item['path'])

        entries.append({
            'rev': int(node.get('revision')),
            'author': (author.text or '').strip() if author is not None else '',
            'date': _svn_date(date.text if date is not None else ''),
            'msg': (msg.text or '').strip() if msg is not None else '',
            'files': files,
        })

    entries.sort(key=lambda item: item['rev'])
    return entries


def update_from_svn(data, url=None):
    """Append every revision after last-imported. Returns the added commits.

    New records go to the front of the list -- the file is kept newest
    first, so triage starts at the top.

    Existing records are left untouched. last-imported is set to the highest
    revision the log reported, NOT the highest one stored: a path-filtered
    log skips revisions that touched other parts of the repository, and
    storing the latter would rescan the same range forever.
    """
    url = url or data['header'].get('repo')
    if not url:
        raise RuntimeError('no SVN url given and none in the header')

    limit = int(data['header'].get('path_limit', DEFAULT_PATH_LIMIT))
    known = set(commit['rev'] for commit in data['commits'])
    added = []

    entries = svn_log(url, int(data['header'].get('last_imported', 0)) + 1)

    for entry in entries:
        if entry['rev'] in known:
            continue

        commit = _blank_commit(entry['rev'])
        commit['author'] = entry['author']
        commit['date'] = entry['date']
        commit['msg'] = entry['msg']
        commit['file_count'] = len(entry['files'])
        commit['files'] = entry['files'][:limit]
        commit['files_truncated'] = max(0, len(entry['files']) - limit)

        keys = []
        for key in JIRA_KEY_RE.findall(entry['msg']):
            if key not in keys:
                keys.append(key)
        commit['jira'] = keys or None

        data['commits'].append(commit)
        added.append(commit)

    data['commits'].sort(key=lambda item: item['rev'], reverse=True)
    data['header']['repo'] = url
    if entries:
        data['header']['last_imported'] = max(item['rev'] for item in entries)
    data['header']['imported_at'] = format_date(datetime.now(timezone.utc))

    return added


# --------------------------------------------------------------------------
# command line
# --------------------------------------------------------------------------

def main(argv):
    if len(argv) != 2 or argv[0] in ('-h', '--help'):
        print('usage: svntrace.py SVN_URL COMMITS_MD\n\n'
              '  Appends every revision from last-imported+1 to HEAD.\n'
              '  Existing records in COMMITS_MD are never modified.',
              file=sys.stderr)
        return 2

    url, path = argv
    try:
        data = load(path)
        added = update_from_svn(data, url)
        save(path, data)
    except (ParseError, RuntimeError, IOError) as exc:
        print('error: %s' % exc, file=sys.stderr)
        return 1

    for commit in reversed(added):
        first = (commit['msg'].split('\n', 1)[0] or '(empty message)')[:60]
        print('r%-7d %s  %-12s %s'
              % (commit['rev'], commit['date'][:10], commit['author'], first))

    untriaged = sum(1 for commit in data['commits']
                    if commit['jira'] is None or commit['changelog_ref'] is None)
    print('\nadded %d revision(s); last-imported now %s; %d awaiting triage'
          % (len(added), data['header'].get('last_imported'), untriaged))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))