"""archive_plans.py - AP-D1 step 2: git mv docs/plans/12..29-*.md (+ the v4 roadmap) into
docs/plans/archive/, insert the section-11 ARCHIVED banner at line 3 of each, and write the
move map consumed by rewrite_links.py.  Run from anywhere; operates on the repo root.
"""
import json, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
DATE = '2026-09-20'

# doc -> (origin, landed-by, replacement-link-relative-to-docs/plans/archive/)
META = {
    '12-documentation-plan.md': ('Docs Plan v2, 2026-07-03 (restructured 2026-07-25), work orders D5-D61', 'see git log (D5-D61, 2026-07-03 .. 2026-07-26)', '[`../../guide/README.md`](../../guide/README.md) + [`../../reference/README.md`](../../reference/README.md) (the living tiers) and `tests/check_docs_coverage.ps1` / `tests/check_docs_links.ps1`'),
    '13-phase14-starforge-hardening-plan.md': ('Phase 14, created 2026-07-04', 'see git log (H1-H10, 2026-07-04)', 'none (shipped)'),
    '14-phase15-physics-plan.md': ('Phase 15, created 2026-07-04', 'see git log (J1-J9, 2026-07-04)', '[`../../guide/physics.md`](../../guide/physics.md) (2D trunk; terrain colliders parked)'),
    '15-phase16-app-platform-plan.md': ('Phase 16, created 2026-07-04', 'see git log (S1-S8, 2026-07-05)', '[`../app-platform-2026-09-18/00-Start-Here.md`](../app-platform-2026-09-18/00-Start-Here.md) (the 2026-09 App Platform campaign)'),
    '16-phase17-ui-flow-2d-plan.md': ('Phase 17, created 2026-07-04', 'see git log (U1-U8, 2026-07-08 .. 2026-07-11)', '[`../../guide/game-ui.md`](../../guide/game-ui.md), [`../../guide/flow-and-story.md`](../../guide/flow-and-story.md)'),
    '17-phase18-voxel-plan.md': ('Phase 18 (3D), created 2026-07-04', 'see git log (V1-V7, 2026-07-08)', '[`../../parked-3d/README.md`](../../parked-3d/README.md) (parked 3D; source on `engine-3d`)'),
    '18-phase19-rendering-quality-plan.md': ('Phase 19 menu, created 2026-07-04', 'R8 2026-07-11; the rest never fired', '[`../FEATURE-MATRIX.md`](../FEATURE-MATRIX.md) "Parked (engine-3d)" for the 3D items'),
    '19-phase20-asset-animation-plan.md': ('Phase 20 (3D), created 2026-07-04', 'see git log (A1/A2/A4, 2026-07-12)', '[`../../parked-3d/README.md`](../../parked-3d/README.md)'),
    '20-phase21-scripting-connectivity-plan.md': ('Phase 21, created 2026-07-04', 'never started (unlock-driven)', '[`../00-MASTER-ROADMAP.md`](../00-MASTER-ROADMAP.md) deferred list (UDP / link abstraction)'),
    '21-phase22-editor-shell-plan.md': ('Phase 22, created 2026-07-11', 'see git log (K1-K13, 2026-07-11)', '[`../../guide/editor-ui-and-theming.md`](../../guide/editor-ui-and-theming.md)'),
    '22-phase23-asset-workflows-plan.md': ('Phase 23, created 2026-07-11', 'see git log (T1-T18, 2026-07-12)', 'none (shipped)'),
    '23-phase24-animation-editors-plan.md': ('Phase 24 (3D), created 2026-07-11', 'see git log (M1-M6, 2026-07-12)', '[`../../parked-3d/README.md`](../../parked-3d/README.md)'),
    '24-phase25-graphs-story-plan.md': ('Phase 25, created 2026-07-11', 'see git log (Q1-Q6, 2026-07-12)', '[`../../guide/flow-and-story.md`](../../guide/flow-and-story.md)'),
    '25-phase26-navigation-ai-plan.md': ('Phase 26 (3D), created 2026-07-11', 'see git log (N1-N5, 2026-07-14)', '[`../../parked-3d/README.md`](../../parked-3d/README.md)'),
    '26-phase27-world-2d-plan.md': ('Phase 27, created 2026-07-11', 'see git log (X1-X7, 2026-07-14)', '[`../../guide/lighting-2d.md`](../../guide/lighting-2d.md) (2D lights); the sky/particle items are parked 3D'),
    '27-phase28-flagship-sample-plan.md': ('Phase 28 (3D flagship Forge Isle), created 2026-07-11', 'Z1 greybox only; project deleted from `main` 2026-09 (preserved on `engine-3d`)', '[`../../showcase/README.md`](../../showcase/README.md) (PendulumLab is the 2D showcase)'),
    '28-phase29-engine-split-plan.md': ('Phase 29, 2026-07-25', 'see git log (W0-W10, 2026-07-25)', 'superseded by the trunk policy: `main` is 2D-only (D-PURGE, [`../app-platform-2026-09-18/00-Start-Here.md`](../app-platform-2026-09-18/00-Start-Here.md))'),
    '29-phase30-2d-hardening-plan.md': ('Phase 30, planned 2026-07-25, never executed as written', 'superseded', '**superseded by the 2D stability packet** [`../2d-stability-2026-09-16/00-Start-Here.md`](../2d-stability-2026-09-16/00-Start-Here.md) (WO-00..WO-10 done) and the App Platform packet'),
    '00-MASTER-ROADMAP-v4.md': ('Master Roadmap v4, 2026-07-04 / 2026-07-11', 'phases 14-29 landed; see each archived plan', '[`../00-MASTER-ROADMAP.md`](../00-MASTER-ROADMAP.md) (v5)'),
}

def banner(origin, landed, replacement):
    return ('> **ARCHIVED %s** — completed/superseded; kept as the record of what was built and why. '
            'Do not execute. Origin: %s. Landed by: %s. Replacement: %s.' % (DATE, origin, landed, replacement))

def insert_banner(path, text_banner):
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    if any(l.startswith('> **ARCHIVED') for l in lines[:6]):
        return False
    # line 3 (1-based): title, blank, banner, blank, rest
    if len(lines) < 2 or lines[1].strip() != '':
        lines.insert(1, '')
    lines.insert(2, text_banner)
    lines.insert(3, '')
    open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    return True

def main():
    plans = os.path.join(ROOT, 'docs', 'plans')
    archive = os.path.join(plans, 'archive')
    moves = {}
    for name in sorted(os.listdir(plans)):
        if not name.endswith('.md'): continue
        if name[:2].isdigit() and 12 <= int(name[:2]) <= 29:
            src = 'docs/plans/' + name
            dst = 'docs/plans/archive/' + name
            if os.path.exists(os.path.join(ROOT, src)):
                subprocess.check_call(['git', 'mv', src, dst], cwd=ROOT)
            moves[src] = dst
    # v4 roadmap
    if os.path.exists(os.path.join(plans, '00-MASTER-ROADMAP.md')) and not os.path.exists(os.path.join(archive, '00-MASTER-ROADMAP-v4.md')):
        subprocess.check_call(['git', 'mv', 'docs/plans/00-MASTER-ROADMAP.md', 'docs/plans/archive/00-MASTER-ROADMAP-v4.md'], cwd=ROOT)
    # NOTE: the v4 roadmap is NOT put in the move map: links to 00-MASTER-ROADMAP.md must keep
    # pointing at the live (v5) roadmap that step 2 writes at the same path.
    for name, (origin, landed, repl) in META.items():
        p = os.path.join(archive, name)
        if os.path.exists(p):
            if insert_banner(p, banner(origin, landed, repl)):
                print('banner ->', name)
    json.dump(moves, open(os.path.join(os.path.dirname(__file__), 'moves-plans.json'), 'w'), indent=1)
    print('moved', len(moves), 'plan docs; move map written to moves-plans.json')

if __name__ == '__main__':
    main()
