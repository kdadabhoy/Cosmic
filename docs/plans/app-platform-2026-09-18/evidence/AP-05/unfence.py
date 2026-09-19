#!/usr/bin/env python3
"""
AP-05 part B - remove the COSMIC_2D_ONLY preprocessor fences from the 2D trunk.

For every C/C++ source file under Cosmic/src, Projects, tests and Cosmic/templates that mentions
COSMIC_2D_ONLY, walk the lines with a preprocessor-depth stack and rewrite the marked blocks:

  #ifndef COSMIC_2D_ONLY ... #endif                 -> dropped (the true branch was never compiled
  #if !defined(COSMIC_2D_ONLY) ... #endif              on this trunk)
  #ifndef COSMIC_2D_ONLY ... #else ... #endif       -> the #else branch, unfenced
  #ifdef COSMIC_2D_ONLY ... #endif                  -> the body, unfenced
  #if defined(COSMIC_2D_ONLY) ... #else ... #endif  -> the body (the #else branch is dropped)

Nesting-aware: an unrelated #if/#ifdef/#ifndef block inside a dropped branch goes with it; one inside
a kept branch is emitted verbatim; a marked block inside a kept branch is rewritten recursively.

Exceptions (left untouched in the file and printed for manual editing):
  * an #elif on a marked block (the whole block is emitted verbatim),
  * COSMIC_2D_ONLY inside a compound #if / #elif condition (e.g. "#if A && !defined(COSMIC_2D_ONLY)"),
  * a directive continued with a trailing backslash,
  * an unbalanced file (a stray #else/#elif/#endif or an unterminated #if) - the file is skipped.

Files that mention COSMIC_2D_ONLY but are not C/C++ sources (CMakeLists.txt, *.ps1, *.py, *.md,
*.json) are listed for hand editing and never rewritten.

Usage:
  py -3 docs/plans/app-platform-2026-09-18/evidence/AP-05/unfence.py            # dry run (default)
  py -3 docs/plans/app-platform-2026-09-18/evidence/AP-05/unfence.py --write    # rewrite in place
  options: --repo <root>  --roots a b c  --verbose (print every dropped range)

Bytes in, bytes out: line endings (CRLF/LF), the final-newline state and any non-ASCII text are
preserved exactly; only whole lines are removed, nothing is ever added.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

TOKEN = b"COSMIC_2D_ONLY"
DEFAULT_ROOTS = ["Cosmic/src", "Projects", "tests", "Cosmic/templates"]
SOURCE_EXTS = {".h", ".hpp", ".inl", ".cpp", ".cc", ".c", ".in"}
SKIP_DIRS = {"build", "_results", "_temp", ".git", "node_modules"}

# Directive shapes. A trailing "// comment" or "/* comment */" is allowed on every one.
_TRAIL = rb"(?:\s*(?://.*|/\*.*\*/\s*))?\s*$"
RE_DIRECTIVE = re.compile(rb"^\s*#\s*(\w+)(.*)$", re.S)
RE_IFNDEF_MARK = re.compile(rb"^\s*#\s*ifndef\s+COSMIC_2D_ONLY\b" + _TRAIL)
RE_IFDEF_MARK = re.compile(rb"^\s*#\s*ifdef\s+COSMIC_2D_ONLY\b" + _TRAIL)
RE_IF_NOTDEF_MARK = re.compile(rb"^\s*#\s*if\s+!\s*defined\s*(?:\(\s*COSMIC_2D_ONLY\s*\)|COSMIC_2D_ONLY\b)" + _TRAIL)
RE_IF_DEF_MARK = re.compile(rb"^\s*#\s*if\s+defined\s*(?:\(\s*COSMIC_2D_ONLY\s*\)|COSMIC_2D_ONLY\b)" + _TRAIL)

OPENERS = {b"if", b"ifdef", b"ifndef"}


def strip_eol(line: bytes) -> bytes:
    return line.rstrip(b"\r\n")


def split_lines(data: bytes) -> list[bytes]:
    """Split on LF keeping every byte (a CR stays glued to its line; a final partial line is kept)."""
    parts = data.split(b"\n")
    lines = [p + b"\n" for p in parts[:-1]]
    if parts[-1]:
        lines.append(parts[-1])
    return lines


class Block:
    __slots__ = ("open_idx", "kind", "else_idx", "elif_idx", "endif_idx", "depth", "note")

    def __init__(self, open_idx: int, kind: str, depth: int):
        self.open_idx = open_idx
        self.kind = kind            # NEUTRAL | MARK_NEG | MARK_POS | EXCEPTION
        self.else_idx = -1
        self.elif_idx: list[int] = []
        self.endif_idx = -1
        self.depth = depth
        self.note = ""


def classify_opener(text: bytes) -> tuple[str, str]:
    """Return (kind, note) for a #if/#ifdef/#ifndef line."""
    if text.rstrip().endswith(b"\\"):
        return "EXCEPTION", "directive continued with a trailing backslash"
    if RE_IFNDEF_MARK.match(text) or RE_IF_NOTDEF_MARK.match(text):
        return "MARK_NEG", ""
    if RE_IFDEF_MARK.match(text) or RE_IF_DEF_MARK.match(text):
        return "MARK_POS", ""
    if TOKEN in text:
        return "EXCEPTION", "COSMIC_2D_ONLY inside a compound condition"
    return "NEUTRAL", ""


def parse_blocks(lines: list[bytes]) -> tuple[list[Block], list[str]]:
    """Pass 1: block structure + exceptions. Returns (blocks, errors)."""
    blocks: list[Block] = []
    stack: list[Block] = []
    errors: list[str] = []
    for i, raw in enumerate(lines):
        text = strip_eol(raw)
        m = RE_DIRECTIVE.match(text)
        if not m:
            continue
        word = m.group(1)
        if word in OPENERS:
            kind, note = classify_opener(text)
            b = Block(i, kind, len(stack))
            b.note = note
            blocks.append(b)
            stack.append(b)
        elif word == b"elif":
            if not stack:
                errors.append(f"line {i + 1}: #elif without an open #if")
                continue
            top = stack[-1]
            top.elif_idx.append(i)
            if TOKEN in text and top.kind != "EXCEPTION":
                top.kind = "EXCEPTION"
                top.note = "COSMIC_2D_ONLY on an #elif"
            elif top.kind in ("MARK_NEG", "MARK_POS"):
                top.kind = "EXCEPTION"
                top.note = "#elif on a marked block"
        elif word == b"else":
            if not stack:
                errors.append(f"line {i + 1}: #else without an open #if")
                continue
            top = stack[-1]
            if top.else_idx != -1:
                errors.append(f"line {i + 1}: second #else for the #if at line {top.open_idx + 1}")
            top.else_idx = i
        elif word == b"endif":
            if not stack:
                errors.append(f"line {i + 1}: #endif without an open #if")
                continue
            top = stack.pop()
            top.endif_idx = i
    for b in stack:
        errors.append(f"line {b.open_idx + 1}: #if never closed")
    return blocks, errors


def rewrite(lines: list[bytes], blocks: list[Block]) -> tuple[list[bytes], dict]:
    """Pass 2: emit the unfenced file."""
    by_open = {b.open_idx: b for b in blocks}
    by_end = {b.endif_idx: b for b in blocks}
    by_else = {b.else_idx: b for b in blocks if b.else_idx != -1}
    stats = {"neg_dropped": 0, "neg_else_kept": 0, "pos_kept": 0, "lines_removed": 0, "ranges": []}
    out: list[bytes] = []
    # frame: [block, emit_this_branch]
    frames: list[list] = []
    squash_blank = False

    def parent_emit() -> bool:
        return all(f[1] for f in frames)

    i = 0
    n = len(lines)
    while i < n:
        raw = lines[i]
        text = strip_eol(raw)
        if i in by_open:
            b = by_open[i]
            emit_here = parent_emit()
            if b.kind in ("MARK_NEG", "MARK_POS"):
                if not emit_here:
                    frames.append([b, False])       # already inside a dropped region
                else:
                    first_keep = (b.kind == "MARK_POS")
                    frames.append([b, first_keep])
                    if b.kind == "MARK_NEG":
                        if b.else_idx == -1:
                            stats["neg_dropped"] += 1
                        else:
                            stats["neg_else_kept"] += 1
                    else:
                        stats["pos_kept"] += 1
                # the marked directive itself is never emitted
                i += 1
                continue
            # NEUTRAL or EXCEPTION: verbatim, follows the enclosing region
            frames.append([b, True])
            if emit_here:
                out.append(raw)
            i += 1
            continue
        if i in by_else:
            b = by_else[i]
            f = frames[-1]
            assert f[0] is b, "frame/else mismatch"
            if b.kind in ("MARK_NEG", "MARK_POS") and parent_emit_excluding_top(frames):
                f[1] = not f[1]
                i += 1
                continue
            # neutral/exception #else (or a marked one inside a dropped region): verbatim
            if b.kind in ("MARK_NEG", "MARK_POS"):
                i += 1
                continue
            if parent_emit():
                out.append(raw)
            i += 1
            continue
        if i in by_end:
            b = by_end[i]
            f = frames.pop()
            assert f[0] is b, "frame/endif mismatch"
            if b.kind in ("MARK_NEG", "MARK_POS"):
                # blank-line hygiene: a fully dropped block sitting between two blank lines
                # would leave a doubled blank; drop the following blank once.
                if parent_emit() and b.kind == "MARK_NEG" and b.else_idx == -1:
                    squash_blank = True
                i += 1
                continue
            if parent_emit():
                out.append(raw)
            i += 1
            continue
        # ordinary line (or a directive that is not part of a block: #include, #define, ...)
        if parent_emit():
            if squash_blank and strip_eol(raw).strip() == b"" and out and strip_eol(out[-1]).strip() == b"":
                squash_blank = False
                i += 1
                continue
            squash_blank = False
            out.append(raw)
        i += 1
    stats["lines_removed"] = len(lines) - len(out)
    return out, stats


def parent_emit_excluding_top(frames: list[list]) -> bool:
    return all(f[1] for f in frames[:-1])


def count_spellings(lines: list[bytes]) -> dict:
    c = {"ifndef": 0, "ifdef": 0, "if_defined": 0, "if_not_defined": 0, "elif": 0, "compound": 0,
         "else_endif_comment": 0, "other_mentions": 0}
    for raw in lines:
        text = strip_eol(raw)
        if TOKEN not in text:
            continue
        m = RE_DIRECTIVE.match(text)
        if not m:
            c["other_mentions"] += 1
            continue
        word = m.group(1)
        if word == b"ifndef" and RE_IFNDEF_MARK.match(text):
            c["ifndef"] += 1
        elif word == b"ifdef" and RE_IFDEF_MARK.match(text):
            c["ifdef"] += 1
        elif word == b"if" and RE_IF_DEF_MARK.match(text):
            c["if_defined"] += 1
        elif word == b"if" and RE_IF_NOTDEF_MARK.match(text):
            c["if_not_defined"] += 1
        elif word == b"elif":
            c["elif"] += 1
        elif word in OPENERS:
            c["compound"] += 1
        elif word in (b"else", b"endif"):
            c["else_endif_comment"] += 1
        else:
            c["other_mentions"] += 1
    return c


def iter_files(repo: str, roots: list[str]):
    for root in roots:
        top = os.path.join(repo, root)
        if not os.path.isdir(top):
            continue
        for dirpath, dirnames, filenames in os.walk(top):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                yield os.path.join(dirpath, name)


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    default_repo = os.path.abspath(os.path.join(here, "..", "..", "..", "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=default_repo)
    ap.add_argument("--roots", nargs="*", default=DEFAULT_ROOTS)
    ap.add_argument("--write", action="store_true", help="rewrite the files (default: dry run)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)
    if not os.path.isfile(os.path.join(repo, "Cosmic", "src", "Cosmic.h")):
        print(f"[FATAL] {repo} is not the Cosmic repository (no Cosmic/src/Cosmic.h)")
        return 3

    totals = {"ifndef": 0, "ifdef": 0, "if_defined": 0, "if_not_defined": 0, "elif": 0, "compound": 0,
              "else_endif_comment": 0, "other_mentions": 0}
    per_file = []          # (rel, counts, stats, new_lines or None)
    exceptions = []        # "rel:line: text (note)"
    manual_files = []      # non-source files that mention the token
    skipped = []           # unbalanced files
    files_touched = 0

    for path in sorted(iter_files(repo, args.roots)):
        with open(path, "rb") as fh:
            data = fh.read()
        if TOKEN not in data:
            continue
        rel = os.path.relpath(path, repo).replace("\\", "/")
        ext = os.path.splitext(path)[1].lower()
        lines = split_lines(data)
        counts = count_spellings(lines)
        if ext not in SOURCE_EXTS:
            manual_files.append((rel, sum(counts.values())))
            continue
        for k in totals:
            totals[k] += counts[k]
        blocks, errors = parse_blocks(lines)
        if errors:
            skipped.append((rel, errors))
            continue
        for b in blocks:
            if b.kind == "EXCEPTION":
                exceptions.append(f"{rel}:{b.open_idx + 1}: {strip_eol(lines[b.open_idx]).decode('utf-8', 'replace').strip()}  ({b.note})")
                for j in b.elif_idx:
                    exceptions.append(f"{rel}:{j + 1}: {strip_eol(lines[j]).decode('utf-8', 'replace').strip()}  (#elif of the block above)")
        new_lines, stats = rewrite(lines, blocks)
        changed = new_lines != lines
        if changed:
            files_touched += 1
        per_file.append((rel, counts, stats, new_lines if changed else None))

    # ---- report --------------------------------------------------------------------
    print("== COSMIC_2D_ONLY spellings in C/C++ sources under", ", ".join(args.roots), "==")
    print(f"  #ifndef COSMIC_2D_ONLY            : {totals['ifndef']}")
    print(f"  #ifdef COSMIC_2D_ONLY             : {totals['ifdef']}")
    print(f"  #if defined(COSMIC_2D_ONLY)       : {totals['if_defined']}")
    print(f"  #if !defined(COSMIC_2D_ONLY)      : {totals['if_not_defined']}")
    print(f"  #elif mentioning it               : {totals['elif']}")
    print(f"  compound #if conditions           : {totals['compound']}")
    print(f"  #else/#endif trailing comments    : {totals['else_endif_comment']}")
    print(f"  other (non-directive) mentions    : {totals['other_mentions']}")
    marked = totals['ifndef'] + totals['ifdef'] + totals['if_defined'] + totals['if_not_defined']
    print(f"  marked blocks                     : {marked}  (+ {totals['compound'] + totals['elif']} exception directive(s))")
    print()
    print("== per file: blocks (neg dropped / neg->else kept / pos body kept) and lines removed ==")
    w = max((len(r) for r, _, _, _ in per_file), default=20)
    for rel, counts, stats, new_lines in per_file:
        print(f"  {rel.ljust(w)}  marks={counts['ifndef'] + counts['ifdef'] + counts['if_defined'] + counts['if_not_defined']:3d}"
              f"  dropped={stats['neg_dropped']:3d}  else-kept={stats['neg_else_kept']:2d}  pos-kept={stats['pos_kept']:2d}"
              f"  -{stats['lines_removed']} lines")
    print()
    print("== dry-run diff stat ==")
    removed_total = 0
    for rel, counts, stats, new_lines in per_file:
        if new_lines is None:
            continue
        removed_total += stats["lines_removed"]
        print(f"  {rel.ljust(w)} | {stats['lines_removed']:5d} " + "-" * min(stats['lines_removed'], 60))
    print(f"  {files_touched} file(s) changed, 0 insertions(+), {removed_total} deletions(-)")
    print()
    if exceptions:
        print("== EXCEPTIONS left untouched - edit by hand ==")
        for e in exceptions:
            print("  " + e)
    else:
        print("== EXCEPTIONS: none ==")
    if skipped:
        print("== SKIPPED (unbalanced preprocessor structure) ==")
        for rel, errs in skipped:
            print(f"  {rel}: " + "; ".join(errs))
    if manual_files:
        print("== non-C/C++ files mentioning COSMIC_2D_ONLY (hand-edit; never rewritten) ==")
        for rel, n in manual_files:
            print(f"  {rel}  ({n} line(s))")
    print()

    if not args.write:
        print("dry run - nothing written (re-run with --write)")
        return 0
    written = 0
    for rel, counts, stats, new_lines in per_file:
        if new_lines is None:
            continue
        with open(os.path.join(repo, rel), "wb") as fh:
            fh.write(b"".join(new_lines))
        written += 1
    print(f"wrote {written} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
