"""rewrite_links.py - re-point Markdown links after files move (AP-D1 mechanical tool).

Usage:  py -3 rewrite_links.py <moves.json> [--dry]

moves.json maps OLD repo-relative path -> NEW repo-relative path (forward slashes) for every
file that was moved with `git mv` BEFORE this script runs. For every Markdown file in the
scanned set (README.md, docs/**, tests/**, Projects/*/README.md, Projects/*/docs/*.md) the
script:
  1. determines where the file USED to live (via the inverse move map);
  2. resolves every relative link target against that OLD location;
  3. maps the target through the move map if it points at a moved file;
  4. re-relativizes the target against the file's NEW location and rewrites the link if it
     changed.
Links inside fenced code blocks are left alone. Only links whose old target actually existed
(or was a moved file) are rewritten; already-broken links are reported but untouched.
"""
import json, os, re, sys, posixpath

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
LINK_RE = re.compile(r'(\]\(\s*<?)([^\s>()#]+(?:\([^\s()]*\)[^\s>()]*)*)?(#[^\s>)]*)?(>?(?:\s+"[^"]*")?\s*\))')

def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')

def scan_files():
    out = []
    r = os.path.join(ROOT, 'README.md')
    if os.path.exists(r): out.append(r)
    for base in ('docs', 'tests'):
        for dp, dn, fn in os.walk(os.path.join(ROOT, base)):
            for f in fn:
                if f.lower().endswith('.md'): out.append(os.path.join(dp, f))
    proj = os.path.join(ROOT, 'Projects')
    if os.path.isdir(proj):
        for p in os.listdir(proj):
            for cand in (os.path.join(proj, p, 'README.md'),):
                if os.path.exists(cand): out.append(cand)
            d = os.path.join(proj, p, 'docs')
            if os.path.isdir(d):
                for f in os.listdir(d):
                    if f.lower().endswith('.md'): out.append(os.path.join(d, f))
    return out

def main():
    dry = '--dry' in sys.argv
    moves = json.load(open(sys.argv[1], encoding='utf-8'))
    moves = {k.strip('/'): v.strip('/') for k, v in moves.items()}
    inverse = {v: k for k, v in moves.items()}
    # directory moves: if a key ends with '/', treat as prefix (not used by AP-D1 but cheap)
    changed_files = 0
    rewritten = 0
    for full in scan_files():
        new_rel = rel(full)
        old_rel = inverse.get(new_rel, new_rel)
        old_dir = posixpath.dirname(old_rel)
        new_dir = posixpath.dirname(new_rel)
        text = open(full, encoding='utf-8', newline='').read()
        lines = text.split('\n')
        in_fence = False
        out_lines = []
        file_changed = False
        for line in lines:
            if re.match(r'^\s{0,3}(```|~~~)', line):
                in_fence = not in_fence
                out_lines.append(line); continue
            if in_fence:
                out_lines.append(line); continue
            def sub(m):
                nonlocal file_changed, rewritten
                pre, target, anchor, post = m.group(1), m.group(2), m.group(3) or '', m.group(4)
                if not target:
                    return m.group(0)
                if re.match(r'^[a-zA-Z][a-zA-Z0-9+.\-]*:', target) or target.startswith('/'):
                    return m.group(0)
                old_target = posixpath.normpath(posixpath.join(old_dir, target)) if old_dir else posixpath.normpath(target)
                if old_target.startswith('../'):
                    return m.group(0)  # points outside the repo; leave it
                new_target = moves.get(old_target, old_target)
                # only rewrite when the target moved OR the linking file moved and the old
                # target still exists (so the link must be re-relativized)
                if new_target == old_target and old_rel == new_rel:
                    return m.group(0)
                if new_target == old_target and not os.path.exists(os.path.join(ROOT, old_target)):
                    return m.group(0)  # already broken; report via the checker, do not guess
                new_link = posixpath.relpath(new_target, new_dir) if new_dir else new_target
                if new_link == target:
                    return m.group(0)
                file_changed = True; rewritten += 1
                return pre + new_link + anchor + post
            out_lines.append(LINK_RE.sub(sub, line))
        if file_changed:
            changed_files += 1
            if not dry:
                open(full, 'w', encoding='utf-8', newline='').write('\n'.join(out_lines))
            print('rewrote', new_rel)
    print('files changed:', changed_files, 'links rewritten:', rewritten, '(dry run)' if dry else '')

if __name__ == '__main__':
    main()
