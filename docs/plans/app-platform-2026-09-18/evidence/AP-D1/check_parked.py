"""check_parked.py - DOC04 assertions (App Platform AP-D1).

  1. every *.md under docs/parked-3d/** carries the section-11 PARKED banner at line 3;
  2. no live-tier Markdown link into docs/parked-3d/ lacks the visible label "(parked 3D)" on the same
     line (live tiers = every scanned file outside docs/archive/**, docs/plans/archive/**,
     docs/parked-3d/**, docs/plans/2d-stability-2026-09-16/evidence/**).
Exit 1 on any violation.  Run:  py -3 docs/plans/app-platform-2026-09-18/evidence/AP-D1/check_parked.py
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
BANNER_PREFIX = '> **PARKED 3D — not on the trunk.**'
WARN_ONLY = ('docs/archive/', 'docs/plans/archive/', 'docs/parked-3d/', 'docs/plans/2d-stability-2026-09-16/evidence/')

def rel(p): return os.path.relpath(p, ROOT).replace('\\', '/')

def scan():
    out = [os.path.join(ROOT, 'README.md')]
    for base in ('docs', 'tests'):
        for dp, dn, fn in os.walk(os.path.join(ROOT, base)):
            out += [os.path.join(dp, f) for f in fn if f.lower().endswith('.md')]
    proj = os.path.join(ROOT, 'Projects')
    if os.path.isdir(proj):
        for p in os.listdir(proj):
            r = os.path.join(proj, p, 'README.md')
            if os.path.exists(r): out.append(r)
            d = os.path.join(proj, p, 'docs')
            if os.path.isdir(d): out += [os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith('.md')]
    return out

fails = []
parked_files = 0
for dp, dn, fn in os.walk(os.path.join(ROOT, 'docs', 'parked-3d')):
    for f in fn:
        if not f.lower().endswith('.md'): continue
        p = os.path.join(dp, f); parked_files += 1
        lines = open(p, encoding='utf-8').read().split('\n')
        if len(lines) < 3 or not lines[2].startswith(BANNER_PREFIX):
            fails.append('%s: line 3 is not the PARKED banner' % rel(p))

link_re = re.compile(r'\]\(([^)\s]*parked-3d/[^)\s]*)\)')
labelled = 0
for p in scan():
    r = rel(p)
    if r.startswith(WARN_ONLY): continue
    in_fence = False
    for n, line in enumerate(open(p, encoding='utf-8').read().split('\n'), 1):
        if re.match(r'^\s{0,3}(```|~~~)', line): in_fence = not in_fence; continue
        if in_fence: continue
        for m in link_re.finditer(line):
            if '(parked 3D)' in line: labelled += 1
            else: fails.append('%s:%d: link into parked-3d/ without the "(parked 3D)" label: %s' % (r, n, m.group(1)))

print('parked files with banner checked: %d; labelled live links into parked-3d/: %d; violations: %d' % (parked_files, labelled, len(fails)))
for f in fails: print('  ' + f)
sys.exit(1 if fails else 0)
