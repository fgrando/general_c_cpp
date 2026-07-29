#!/usr/bin/env python3
"""
report.py - build a CSP-safe HTML quality report for the Jenkins HTML Publisher.

Renders under HTML Publisher's DEFAULT Content Security Policy:
  * styling lives in an EXTERNAL report.css (style-src 'self' allows it)
  * CSS classes only -- no inline style="" (blocked by default CSP)
  * NO JavaScript (blocked by default CSP)
  * bars are Unicode block chars (plain text, always renders)

Inputs:
  --warnings FILE      CSV from warntable.py (built_file,offender_file,offender_line,warning_type,count)
  --analysis LABEL:FILE (repeatable) an extra 'metric,value' CSV, e.g. cppcheck.csv
  build info (all optional): --job --build --status --rev --url --timestamp
  --title, --outdir (default: reports)

Writes:  <outdir>/report.html  and  <outdir>/report.css

Publish with:
  publishHTML(target:[reportDir:'reports', reportFiles:'report.html',
                      reportName:'Quality Report', keepAll:true,
                      alwaysLinkToLastBuild:true, allowMissing:false])
"""

import argparse
import csv
import html
import os
import sys
from collections import Counter
from datetime import datetime

BAR_W = 24


def esc(x):
    return html.escape(str(x), quote=True)


def bar(n, nmax):
    if nmax <= 0:
        return ''
    filled = round(BAR_W * n / nmax)
    return '\u2587' * filled + '\u2591' * (BAR_W - filled)


def read_warnings(path):
    rows = []
    with open(path, newline='', encoding='utf-8') as fh:
        for r in csv.DictReader(fh):
            try:
                r['count'] = int(r.get('count', 1) or 1)
            except ValueError:
                r['count'] = 1
            rows.append(r)
    return rows


def read_metrics(path):
    out = []
    with open(path, newline='', encoding='utf-8') as fh:
        rd = csv.reader(fh)
        for row in rd:
            if not row or row[0].strip().startswith('#'):
                continue
            if row[0].strip().lower() in ('metric', 'name'):
                continue
            out.append((row[0].strip(), row[1].strip() if len(row) > 1 else ''))
    return out


def status_class(status):
    s = (status or '').upper()
    return {'SUCCESS': 'ok', 'UNSTABLE': 'warn', 'FAILURE': 'fail'}.get(s, 'info')


def table(headers, rows, cls='grid'):
    out = [f'<table class="{cls}"><thead><tr>']
    out += [f'<th>{esc(h)}</th>' for h in headers]
    out.append('</tr></thead><tbody>')
    for row in rows:
        out.append('<tr>' + ''.join(f'<td>{c}</td>' for c in row) + '</tr>')
    out.append('</tbody></table>')
    return ''.join(out)


def build_html(args, warns, analyses):
    total = sum(r['count'] for r in warns)
    by_type = Counter()
    by_built = Counter()
    for r in warns:
        by_type[r.get('warning_type', '')] += r['count']
        by_built[r.get('built_file', '')] += r['count']

    tmax = max(by_type.values(), default=0)
    bmax = max(by_built.values(), default=0)

    info = [
        ('Job', args.job), ('Build', args.build), ('SVN revision', args.rev),
        ('Status', args.status), ('Generated', args.timestamp),
    ]
    info = [(k, v) for k, v in info if v]

    # summary cards
    cards = [('Total warnings', total)]
    for label, metrics in analyses:
        for m, v in metrics:
            cards.append((f'{label}: {m}', v))

    P = []
    P.append('<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">')
    P.append('<link rel="stylesheet" href="report.css">')
    P.append(f'<title>{esc(args.title)}</title></head><body><main>')
    P.append(f'<h1>{esc(args.title)}</h1>')

    sc = status_class(args.status)
    if args.status:
        P.append(f'<p><span class="badge {sc}">{esc(args.status)}</span>'
                 + (f' <a href="{esc(args.url)}">build page</a>' if args.url else '') + '</p>')

    # cards row
    P.append('<section class="cards">')
    for label, val in cards:
        P.append(f'<div class="card"><div class="cval">{esc(val)}</div>'
                 f'<div class="clabel">{esc(label)}</div></div>')
    P.append('</section>')

    # build info
    if info:
        P.append('<h2>Build</h2>')
        P.append(table(['Field', 'Value'], [(esc(k), esc(v)) for k, v in info], 'kv'))

    # warnings
    P.append('<h2>Warnings</h2>')
    P.append(f'<p>{total} warning(s) after waivers, across {len(by_built)} built file(s).</p>')

    P.append('<h3>By warning type</h3>')
    rows = [(esc(t), n, f'<span class="bar">{bar(n, tmax)}</span>')
            for t, n in by_type.most_common()]
    P.append(table(['warning_type', 'count', ''], rows))

    P.append('<h3>Top built files</h3>')
    rows = [(esc(b), n, f'<span class="bar">{bar(n, bmax)}</span>')
            for b, n in by_built.most_common(15)]
    P.append(table(['built_file', 'count', ''], rows))

    P.append('<h3>Detail</h3>')
    rows = [(esc(r.get('built_file', '')), esc(r.get('offender_file', '')),
             esc(r.get('offender_line', '')), esc(r.get('warning_type', '')),
             r['count']) for r in warns]
    rows.sort(key=lambda x: (x[0], x[1], x[3]))
    P.append(table(['built_file', 'offender_file', 'line', 'warning_type', 'count'], rows))

    # other static analysis
    if analyses:
        P.append('<h2>Static analysis</h2>')
        for label, metrics in analyses:
            P.append(f'<h3>{esc(label)}</h3>')
            P.append(table(['metric', 'value'], [(esc(m), esc(v)) for m, v in metrics], 'kv'))

    P.append(f'<footer>Generated {esc(args.timestamp)}</footer>')
    P.append('</main></body></html>')
    return ''.join(P)


CSS = """\
:root{--fg:#1c2530;--mut:#5b6b7a;--line:#d9e0e7;--bg:#fff;--head:#f2f5f8;}
*{box-sizing:border-box;} body{margin:0;background:var(--bg);color:var(--fg);
 font:14px/1.5 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;}
main{max-width:1100px;margin:0 auto;padding:24px;}
h1{font-size:22px;margin:0 0 4px;} h2{font-size:17px;margin:28px 0 8px;
 border-bottom:1px solid var(--line);padding-bottom:4px;} h3{font-size:14px;margin:18px 0 6px;color:var(--mut);}
.badge{display:inline-block;padding:2px 10px;border-radius:3px;color:#fff;font-weight:600;font-size:12px;}
.badge.ok{background:#2e7d32;} .badge.warn{background:#c47f00;}
.badge.fail{background:#c62828;} .badge.info{background:#546e7a;}
a{color:#1565c0;} 
.cards{display:flex;flex-wrap:wrap;gap:12px;margin:12px 0;}
.card{border:1px solid var(--line);border-radius:6px;padding:10px 14px;min-width:120px;}
.cval{font-size:22px;font-weight:700;} .clabel{font-size:12px;color:var(--mut);}
table{border-collapse:collapse;width:100%;margin:6px 0 4px;font-size:13px;}
th,td{border:1px solid var(--line);padding:5px 8px;text-align:left;vertical-align:top;}
th{background:var(--head);font-weight:600;}
tr:nth-child(even) td{background:#fafbfc;}
table.kv{width:auto;} table.kv td:first-child,table.kv th:first-child{color:var(--mut);white-space:nowrap;}
.bar{font-family:monospace;letter-spacing:-1px;color:#3a6ea5;white-space:nowrap;}
footer{margin-top:28px;color:var(--mut);font-size:12px;}
"""


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--warnings', required=True)
    ap.add_argument('--analysis', action='append', default=[], metavar='LABEL:FILE')
    ap.add_argument('--title', default='Quality Report')
    ap.add_argument('--job', default='')
    ap.add_argument('--build', default='')
    ap.add_argument('--status', default='')
    ap.add_argument('--rev', default='')
    ap.add_argument('--url', default='')
    ap.add_argument('--timestamp', default=datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    ap.add_argument('--outdir', default='reports')
    args = ap.parse_args(argv)

    warns = read_warnings(args.warnings)
    analyses = []
    for spec in args.analysis:
        if ':' not in spec:
            print(f'ignoring --analysis {spec!r} (need LABEL:FILE)', file=sys.stderr)
            continue
        label, path = spec.split(':', 1)
        analyses.append((label, read_metrics(path)))

    os.makedirs(args.outdir, exist_ok=True)
    with open(os.path.join(args.outdir, 'report.css'), 'w', encoding='utf-8') as fh:
        fh.write(CSS)
    with open(os.path.join(args.outdir, 'report.html'), 'w', encoding='utf-8') as fh:
        fh.write(build_html(args, warns, analyses))
    print(f'wrote {args.outdir}/report.html and report.css')
    return 0


if __name__ == '__main__':
    sys.exit(main())
