"""stale_sweep.py - AP-D1 step 5, the scripted part of the stale sweep over the LIVE tiers.

  A. every "**Configuration:**" paragraph in docs/guide, docs/reference, docs/systems becomes the
     one-line trunk policy (the two-configuration build it described no longer exists);
  B. every live chapter that still cites a deleted project (Frontier, Engine3DDemo, ForgeIsle,
     ViperSim, ForgePlayground, ForgeBlocks) or the old two-configuration build (engine-2d,
     byte-identical, COSMIC_2D_ONLY=OFF, #ifndef COSMIC_2D_ONLY, "2D build"/"3D build", worktree
     advice) gets ONE dated "History" note at line 3 that tells the reader how to read those
     mentions; the inline text is not rewritten line by line (AP-D2 owns chapter prose);
  C. every live-tier link into docs/parked-3d/ gets the visible "(parked 3D)" label (DOC04).
The hand edits (guide README exemplar list, root README doc map / 1.6, docs/README.md, the
stability packet's closed line, getting-started / building-and-shipping build sections) are
separate, in this session's commits.
"""
import os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
LIVE_DIRS = ['docs/guide', 'docs/reference', 'docs/systems', 'docs/design', 'docs/engineering-notes']
LIVE_FILES = ['docs/README.md', 'README.md', 'tests/README.md', 'tests/acceptance/README.md']
WARN_ONLY = ('docs/archive/', 'docs/plans/archive/', 'docs/parked-3d/', 'docs/plans/2d-stability-2026-09-16/evidence/')
STALE_RE = re.compile(r'Frontier|Engine3DDemo|ForgeIsle|Forge Isle|ViperSim|ForgePlayground|ForgeBlocks|engine-2d|byte-identical|COSMIC_2D_ONLY=OFF|#ifndef COSMIC_2D_ONLY|\b2D build\b|\b3D build\b|\b2D configuration\b|\b3D configuration\b|two engine|both engine|git worktree', re.I)
CONFIG_LINE = ('**Configuration:** 2D trunk. `main` builds one engine, 2D-only (since 2026-09-18, D-PURGE); '
               '`-DCOSMIC_2D_ONLY=ON` is an always-on compatibility flag and `OFF` is rejected at configure. '
               'History: the two-configuration build this line used to describe is '
               '[parked](%s) (parked 3D).')
HISTORY = ('> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine '
           '(Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` '
           'or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those '
           'projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` '
           '(`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as '
           'historical. The current exemplars are the template projects, `Projects/PendulumLab`, '
           '`Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and '
           '[`%s`](%s) (parked 3D) records what the split was.')

def rel(p): return os.path.relpath(p, ROOT).replace('\\', '/')

def live_files():
    out = []
    for d in LIVE_DIRS:
        full = os.path.join(ROOT, d)
        if not os.path.isdir(full): continue
        for dp, dn, fn in os.walk(full):
            out += [os.path.join(dp, f) for f in fn if f.endswith('.md')]
    for f in LIVE_FILES:
        p = os.path.join(ROOT, f)
        if os.path.exists(p): out.append(p)
    proj = os.path.join(ROOT, 'Projects')
    for p in os.listdir(proj):
        r = os.path.join(proj, p, 'README.md')
        if os.path.exists(r): out.append(r)
    return sorted(set(out))

def relto(from_file, target_rel):
    import posixpath
    return posixpath.relpath(target_rel, posixpath.dirname(rel(from_file)))

def sweep(path):
    r = rel(path)
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    L = raw.split(nl)
    changed = False
    parked_split = relto(path, 'docs/parked-3d/systems/build-2d-3d-split.md')

    # A. Configuration paragraphs (guide/reference/systems only)
    if r.startswith(('docs/guide/', 'docs/reference/', 'docs/systems/')) and not r.endswith('/README.md'):
        i = 0
        while i < len(L):
            if L[i].startswith('**Configuration:**'):
                j = i + 1
                while j < len(L) and L[j].strip() != '' and not L[j].startswith(('**', '#', '>', '---', '|', '- ', '```')):
                    j += 1
                L[i:j] = [CONFIG_LINE % parked_split]
                changed = True
            i += 1

    # B. History note where stale mentions remain
    text = nl.join(L)
    body_wo_notes = nl.join(l for l in L if not l.startswith('> **History (2026-09-20'))
    if STALE_RE.search(body_wo_notes) and not any(l.startswith('> **History (2026-09-20') for l in L[:8]) and r not in ('README.md', 'docs/README.md', 'docs/guide/README.md', 'docs/reference/README.md', 'docs/systems/README.md'):
        if L[1].strip() != '': L.insert(1, '')
        L.insert(2, HISTORY % (parked_split, parked_split)); L.insert(3, '')
        changed = True

    # C. label parked links
    if not r.startswith(WARN_ONLY):
        in_fence = False
        for k, line in enumerate(L):
            if re.match(r'^\s{0,3}(```|~~~)', line): in_fence = not in_fence; continue
            if in_fence or '(parked 3D)' in line: continue
            new = re.sub(r'(\]\([^)\s]*parked-3d/[^)\s]*\))(?!\s*\(parked 3D\))', r'\1 (parked 3D)', line)
            if new != line:
                L[k] = new; changed = True
    if changed:
        open(path, 'w', encoding='utf-8', newline='').write(nl.join(L))
    return changed

def main():
    n = 0
    for f in live_files():
        if sweep(f):
            n += 1; print('swept', rel(f))
    print('files changed:', n)

if __name__ == '__main__':
    main()
