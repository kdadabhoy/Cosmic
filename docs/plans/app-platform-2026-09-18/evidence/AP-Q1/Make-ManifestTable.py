"""Render the manifest results of one driver run (summary.txt + per-manifest results.json) as a
Markdown table for the release report. Usage: py -3 Make-ManifestTable.py <run-dir> [<run-dir> ...]"""
import json, sys, os, glob

def rows(run_dir):
    out = []
    for res in sorted(glob.glob(os.path.join(run_dir, '*', 'results.json'))):
        name = os.path.basename(os.path.dirname(res))
        d = json.load(open(res, encoding='utf-8-sig'))
        for c in d['cases']:
            detail = (c.get('detail') or '').replace('|', '/').strip()
            if len(detail) > 90:
                detail = detail[:87] + '...'
            out.append((name, d.get('profile'), d.get('config'), c['id'], c['tier'], c['verdict'],
                        c.get('exit_code'), c.get('tests_passed'), round(c.get('duration_sec') or 0, 1),
                        (d.get('git') or {}).get('commit', '')[:7], detail))
    return out

for run_dir in sys.argv[1:]:
    print(f"\n**{os.path.basename(run_dir)}** (`{run_dir.replace(chr(92), '/')}`)\n")
    print("| Manifest | Profile | Cfg | Case | Tier | Verdict | Exit | Tests | s | Commit | Detail |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    counts = {}
    for r in rows(run_dir):
        counts[r[5]] = counts.get(r[5], 0) + 1
        print("| " + " | ".join(str(x) if x is not None else '' for x in r) + " |")
    print(f"\nCounts: {counts}")
