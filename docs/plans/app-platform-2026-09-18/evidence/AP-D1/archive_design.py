"""archive_design.py - AP-D1 step 3: git mv seven superseded design docs into docs/archive/design/,
insert the ARCHIVED banner at line 3 of each, write docs/archive/design/README.md and the move map.
"""
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
DATE = '2026-09-20'

# name -> (origin, landed-by, replacement (relative to docs/archive/design/), what-it-is)
META = {
    'forge-isle.md': ('Phase 28 / Z1 content spec, 2026-07-14', 'Z1 greybox on the pre-split tree; project deleted from `main` 2026-09', '3D flagship parked — the 2D showcase is [`../../showcase/README.md`](../../showcase/README.md)', 'Forge Isle flagship design (3D island adventure)'),
    'example-images-gap-analysis.md': ('editor-vision spec of record, 2026-07-11', 'Phases 22–28 (archived plans 21–27)', '[`../../plans/archive/00-MASTER-ROADMAP-v4.md`](../../plans/archive/00-MASTER-ROADMAP-v4.md) records what shipped; the 2D editor direction is the [App Platform packet](../../plans/app-platform-2026-09-18/00-Start-Here.md)', 'Reference-editor screenshot gap analysis that became roadmap v4 Phases 22–28'),
    'starforge-acceptance-demo.md': ('Phase 13 acceptance script, 2026-07-04', 'E21 (archived plan 11)', 'the scripted acceptance runner [`../../../tests/acceptance/README.md`](../../../tests/acceptance/README.md) and the PendulumLab walkthrough [`../../guide/pendulumlab-walkthrough.md`](../../guide/pendulumlab-walkthrough.md)', 'Recorded Phase 13 editor acceptance demo script'),
    'app-platform-acceptance.md': ('Phase 16 S8 acceptance, 2026-07-05', 'S8 (archived plan 15)', 'AP-P1 packaging acceptance K01–K04 in [`../../plans/app-platform-2026-09-18/03-Acceptance-Catalog.md`](../../plans/app-platform-2026-09-18/03-Acceptance-Catalog.md)', 'Phase 16 recorded "packaged app on a clean machine" acceptance'),
    'ui-flow-2d-acceptance.md': ('Phase 17 U8 acceptance, 2026-07-11', 'U8 (archived plan 16); FlowDemo/ForgePong samples replaced by the template projects', 'the retained C-series manifests under `tests/acceptance/manifests/` and [`../../guide/pendulumlab-walkthrough.md`](../../guide/pendulumlab-walkthrough.md)', 'Phase 17 recorded UI / flow / 2D acceptance'),
    'water-rendering-notes.md': ('water system engineering note, 2026-07-03', 'Phase 11 (archived plan 05/10)', '3D water is parked — [`../../parked-3d/systems/water.md`](../../parked-3d/systems/water.md)', 'Gerstner water rendering notes, fixes and limits (3D)'),
    'starforge-ui.md': ('Stage-D editor quick guide, 2026-07-03', 'absorbed by the guide tier (D39 and later)', '[`../../guide/editor-ui-and-theming.md`](../../guide/editor-ui-and-theming.md), [`../../guide/getting-started.md`](../../guide/getting-started.md)', 'Early Starforge editor user guide'),
}

def banner(origin, landed, repl):
    return ('> **ARCHIVED %s** — completed/superseded; kept as the record of what was built and why. '
            'Do not execute. Origin: %s. Landed by: %s. Replacement: %s.' % (DATE, origin, landed, repl))

def insert_banner(path, b):
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    if any(l.startswith('> **ARCHIVED') for l in lines[:6]): return
    if lines[1].strip() != '': lines.insert(1, '')
    lines.insert(2, b); lines.insert(3, '')
    open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))

def main():
    dst_dir = os.path.join(ROOT, 'docs', 'archive', 'design')
    os.makedirs(dst_dir, exist_ok=True)
    moves = {}
    for name, (origin, landed, repl, what) in META.items():
        src = 'docs/design/' + name; dst = 'docs/archive/design/' + name
        if os.path.exists(os.path.join(ROOT, src)):
            subprocess.check_call(['git', 'mv', src, dst], cwd=ROOT)
        moves[src] = dst
        insert_banner(os.path.join(ROOT, dst), banner(origin, landed, repl))
    rows = '\n'.join('| [`%s`](%s) | %s | %s |' % (n, n, META[n][3], META[n][2]) for n in META)
    readme = ('# Archived design docs — superseded proposals and acceptance scripts\n\n'
              '> **ARCHIVED tier (2026-09-20, App Platform AP-D1).** Design proposals and recorded-acceptance scripts whose\n'
              '> work shipped, was superseded, or describes 3D systems that no longer exist on `main`. Each file carries an\n'
              '> `ARCHIVED` banner at line 3 with its origin, what landed it and its replacement. Relative links inside them\n'
              '> were written from `docs/design/` and may be stale (the link checker treats this tier as warn-only).\n'
              '> Live proposals stay in [`../../design/`](../../design/README.md); historical analyses are one level up in\n'
              '> [`../`](../README.md).\n\n'
              '| Doc | What it is | Superseded by |\n| --- | --- | --- |\n' + rows + '\n')
    open(os.path.join(dst_dir, 'README.md'), 'w', encoding='utf-8', newline='\n').write(readme)
    json.dump(moves, open(os.path.join(os.path.dirname(__file__), 'moves-design.json'), 'w'), indent=1)
    print('moved', len(moves), 'design docs')

if __name__ == '__main__':
    main()
