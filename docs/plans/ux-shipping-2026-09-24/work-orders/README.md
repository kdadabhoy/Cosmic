# UX & Shipping — work-order execution prompts

Status: execution packet, 2026-09-24. Base: `main` at `0c2edd8` (the packet commit UX-00 is the base for wave 1;
every later WO names its base in its prompt).

Each `UX-xx.md` is a **self-contained prompt** for one work order. Paste its `~~~text` block into a fresh
AI session that has this repository (or the lane's worktree) and nothing else; it names the only files
to read, the paths it may edit, the acceptance IDs, the evidence to produce and how to land. Where a
prompt and any older document disagree, **the prompt wins** and the session says so in its report.

The single running known-issue register is
[`../../2d-stability-2026-09-16/contracts/known-issues.md`](../../2d-stability-2026-09-16/contracts/known-issues.md)
— next entry **KI-66**. Every crash / hang / data loss / wrong-behaviour defect found by any UX session is
appended there with a minimal regression and a disposition, using its template, **before** it is fixed. A
missing fixture or skipped test is never logged as a pass.

The orchestrator prompt is [`ORCHESTRATOR.md`](ORCHESTRATOR.md).

---

## Global rules (every session, every WO)

1. **One work order.** Do only the named WO. Report a conflict with the packet or the repo instead of
   silently following an older prompt; treat everything under `docs/plans/` other than this packet as
   history, not instructions (the App Platform packet's `FOLLOW-UP.md` is superseded by UX-H1 / UX-Q1).
2. **Revalidate first.** Print `git rev-parse HEAD`, `git status --short`, the branch/worktree you are
   in and the toolchain (`cmake --version`, MSVC). Re-check every `file:line` anchor the prompt cites
   before editing (they were true on 2026-09-24 at `0c2edd8` and drift).
3. **Ownership is exclusive.** Edit only the paths under the WO's **Owns**. Paths under **May touch** are
   shared with a named rebase expectation. Anything else: report it, do not edit it. Names, fields and
   signatures in `01-Contracts.md` are fixed; a needed deviation is recorded in the report under
   "Contract deviations" for the integrator and is never applied silently to another lane's files.
4. **Explicit 2D mode.** Configure with `-DCOSMIC_2D_ONLY=ON` for every build (an always-ON compatibility
   flag; keep passing it). Record the effective cache values in the report.
5. **Production path in tests.** Drive real application commands and state transitions. A fake
   transport / clock / file seam may control bytes, OS outcomes or time, but must not reimplement a
   parser, a state machine, or build a second simulated app. No private-field mutation to force a state.
   Editor behaviour is proven through a self-test host step (`AP03AuthoringSelfTest` /
   `GuideWalkthroughSelfTest` pattern: an env-gated driver inside Starforge writing a result JSON, run by
   a `Run-*.ps1` wrapper and a manifest), never by a screenshot alone.
6. **Failing-before, passing-after.** For every defect fixed, record a failing reproduction before the
   fix and the passing result after it, on an isolated patch/stash — never a destructive reset.
7. **Honest gates.** Never regenerate goldens, loosen a tolerance, broaden scope, or retry-until-green to
   hide a failure. A missing GPU / COM / Windows-version / VM / UI capability is `ENVIRONMENT_BLOCKED` with
   the prerequisite named — not a pass. A filtered suite with zero expected tests is a failure.
8. **Evidence.** `evidence/UX-xx/report.md` in the WO-10 report layout (scope and provenance → toolchain
   and environment → what was built → acceptance-case status per ID → defects (registered before fixing)
   → measured numbers → caveats → files → local commits). Key everything by commit SHA, dirty-diff hash,
   config, environment, exact command + exit code, seed and fixture hash. `*.log` is gitignored: commit
   excerpts (`*-excerpts.txt`) and JSON results only.
9. **Commit, don't push.** Commit locally as **`kdadabhoy <kdadabhoy28@gmail.com>`** with **no
   `Co-Authored-By`, no AI trailer, no "Generated with" line**. Never push, never tag-push, never publish
   a release. Kaden pushes.
10. **Pictures.** Every image that reaches a guide is a capture of the real editor (the lane's Release
    build, 2560x1440 at 100 %) with the control boxed in red by `tools/guide_shots.py` — from the capture
    driver, or from a computer-use screenshot annotated through `annotations.json`. Pictures that are not of
    the editor (Windows Explorer showing a project tree, the installer, a terminal running a build) are
    computer-use screenshots taken by the orchestrator and annotated the same way. No hand-drawn boxes,
    no mock-ups, no images of an unfixed editor in a guide that describes the fixed one.

## Lane rules (D-LANES)

- **L1 — one worktree per parallel lane.** From `C:\dev\Cosmic`:
  `git worktree add build\_lanes\ux-<id> -b ux/<id> <base>` (the prompt names `<base>`), then run the whole
  session inside `C:\dev\Cosmic\build\_lanes\ux-<id>` (`build\` is gitignored, so the lanes never show up in
  `git status`). Build into `<worktree>\build`. **In that shell set `$env:COSMIC_SDK = '<worktree root>'`** —
  the editor's `SdkDir()` and every scaffold/standalone build otherwise resolve to `C:\dev\Cosmic`. Serial
  WOs (UX-Q1) run alone on `main` in `C:\dev\Cosmic`; while one runs, no other session edits that tree.
- **L2 — land protocol** (end of every lane prompt): `git rebase main` → rebuild Debug + Release, 0
  warnings → retained units both configs → the WO's manifests → both audits + `tests\check_docs_links.ps1`
  (+ `tests\check_api_matrix.ps1` once UX-D2 has landed) → commit on the lane branch. The integrator (the
  orchestrator) merges with `git merge --no-ff ux/<id>` in the landing order, re-runs the retained units
  once, and hashes `tests/render/goldens/*.png` before and after the merge.
- **L3 — never remove a worktree with unmerged commits.** `git worktree remove` only after the merge is
  verified on `main`.
- **L4 — shared files.** `Projects/Starforge/src/StarforgeApp.cpp` (three wave-1 lanes, disjoint line
  ranges named in each prompt), `docs/reference/README.md`, `.github/workflows/ci.yml`,
  `.github/workflows/release.yml`, `Projects/Starforge/CMakeLists.txt`, `tests/CMakeLists.txt`,
  `docs/guides/06-package-and-ship.md` and `docs/guides/00-get-starforge.md` (UX-D1 writes it; UX-H1 adds the
  compiler-check picture at its landing, D-TOOLCHAIN) are touched by more than one WO; each prompt says which
  lines/rows it may add. Resolve conflicts by keeping both sides.
- **L5 — registers.** Defects → the KI register (rule above); new surface (prefs keys, marker export,
  presets, SDK layout, guides manifest) → `01-Contracts.md` §11 table rows with the acceptance ID that
  proves each one. **KI numbers under concurrency:** three lanes of one wave would all take the next free
  number; a lane registers with the next number it sees and, at rebase, renumbers its entries above any the
  other lanes landed first (the integrator checks the register is gap-free and updates the number in this
  file after each landing).

## Build and test commands

```
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON
cmake --build build --config Debug --parallel      (and Release)
build\Runtime\Debug\CosmicTests.exe                (and Release)
build\Runtime\Debug\CosmicRenderTests.exe          (needs -DCOSMIC_BUILD_RENDER_TESTS=ON; real GPU)
powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1
powershell -ExecutionPolicy Bypass -File tests\check_docs_coverage.ps1
powershell -ExecutionPolicy Bypass -File tests\check_docs_links.ps1
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest <ABSOLUTE path to tests\acceptance\manifests\<name>.manifest.json> -Config Release -TempRoot <repo>\build\_temp\<name>
```
The VS-bundled cmake is not on PATH. New `.cpp` files under `Cosmic/src` need a re-configure (the engine
glob has no `CONFIGURE_DEPENDS`). The acceptance runner's `-Manifest` resolves relative to
`tests/acceptance/` — pass an absolute path; fixtures need `-TempRoot` or they report spurious failures.
Retained manifests rewrite tracked evidence files under the App Platform packet: `git checkout --` them and
delete `n04-*.bin` / `recordings/` from the tree root before committing. Bash heredocs over ~12 k characters
fail: use the Write/Edit tools. Python is `py -3`.

## Baseline at `0c2edd8`

CosmicTests **523 passed / 0 failed / 14 skipped** both configs; CosmicRenderTests 45 (2 skipped), 15
goldens; all three checkers exit 0; KI-1..65 registered (63 open). Qualified App Platform SHA `fa1223a`
(tag `cosmic-app-platform-g5-2026-09-20`).
