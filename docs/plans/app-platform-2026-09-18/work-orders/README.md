# App Platform — work-order execution prompts

Status: execution packet, 2026-09-18. Base: `main` at `8da533c0aee19d0706616c4c2b55cadb7bc53629`
(the AP-00 commit is the base for AP-05A and AP-P1; every later WO names its base in its prompt).

Each `AP-xx.md` is a **self-contained prompt** for one work order. Paste its `~~~text` block into a fresh
AI session that has this repository (or the lane's worktree) and nothing else; it names the only files
to read, the paths it may edit, the acceptance IDs, the evidence to produce and how to land. Where a
prompt and any older document disagree, **the prompt wins** and the session says so in its report.

The single running known-issue register is
[`../../2d-stability-2026-09-16/contracts/known-issues.md`](../../2d-stability-2026-09-16/contracts/known-issues.md)
— next entry **KI-64**. Every crash / hang / data loss found by any AP session is appended there with a
minimal regression and a disposition, using its template. A missing fixture or skipped test is never
logged as a pass.

---

## Global rules (every session, every WO)

1. **One work order.** Do only the named WO. Report a conflict with the packet or the repo instead of
   silently following an older prompt; treat everything under `docs/plans/` other than this packet as
   history, not instructions.
2. **Revalidate first.** Print `git rev-parse HEAD`, `git status --short`, the branch/worktree you are
   in and the toolchain (`cmake --version`, MSVC). Re-check every `file:line` anchor the prompt cites
   before editing (they were true on 2026-09-18 and drift). Never stage, move or overwrite the untracked
   root file `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md`.
3. **Ownership is exclusive.** Edit only the paths under the WO's **Owns**. Paths under **May touch** are
   shared with a named rebase expectation. Anything else: report it, do not edit it. Names, fields and
   signatures in `01-Design-Contracts.md` are fixed; a needed deviation is recorded in the report under
   "Contract deviations" for the integrator and is never applied silently to another lane's files.
4. **Explicit 2D mode.** Configure with `-DCOSMIC_2D_ONLY=ON` for every build (after AP-05 the option is
   an always-ON compatibility no-op; keep passing it). Record the effective cache values in the report.
5. **Production path in tests.** Drive real application commands and state transitions. A fake
   transport / clock / file seam may control bytes, OS outcomes or time, but must not reimplement a
   parser, a state machine, or build a second simulated app. No private-field mutation to force a state.
6. **Failing-before, passing-after.** For every defect fixed, record a failing reproduction before the
   fix and the passing result after it, on an isolated patch/stash — never a destructive reset.
7. **Honest gates.** Never regenerate goldens, loosen a tolerance, broaden scope, or retry-until-green to
   hide a failure. A missing GPU / COM / Windows-version / UI capability is `ENVIRONMENT_BLOCKED` with
   the prerequisite named — not a pass. A filtered suite with zero expected tests is a failure.
8. **Evidence.** `evidence/AP-xx/report.md` in the WO-10 report layout (scope and provenance → toolchain
   and environment → what was built → acceptance-case status per ID → defects (registered before fixing)
   → measured numbers → caveats → files → local commits). Key everything by commit SHA, dirty-diff hash,
   config, environment, exact command + exit code, seed and fixture hash. `*.log` is gitignored: commit
   excerpts (`*-excerpts.txt`) and JSON results only.
9. **Commit, don't push.** Commit locally as **`kdadabhoy <kdadabhoy28@gmail.com>`** with **no
   `Co-Authored-By`, no AI trailer, no "Generated with" line**. Never push, never tag-push, never publish.
   Kaden pushes.

## Lane rules (D-LANES; supersede the stability packet's D-WORKFLOW for this packet only)

- **L1 — one worktree per parallel lane.** From `C:\dev\Cosmic`:
  `git worktree add ..\Cosmic-ap-<id> -b ap/<id> <base>` (the prompt names `<base>`), then run the whole
  session inside `C:\dev\Cosmic-ap-<id>`. Build into `<worktree>\build`. **In that shell set
  `$env:COSMIC_SDK = '<worktree root>'`** — the editor's `SdkDir()` and every scaffold/standalone build
  otherwise resolve to `C:\dev\Cosmic`. Serial WOs (AP-05, AP-01, AP-Q1) run alone on `main` in
  `C:\dev\Cosmic`; while one of them runs, no other session edits `C:\dev\Cosmic`.
- **L2 — land protocol** (end of every lane prompt): `git fetch` is not needed (local only);
  `git rebase main` → rebuild Debug + Release, 0 warnings → retained units both configs → the WO's
  manifests → both audits + `tests/check_docs_links.ps1` (once AP-D1 has landed) → commit on the lane
  branch. The integrator (the next serial session, or Kaden) merges with
  `git merge --no-ff ap/<id>` in the landing order, re-runs the retained units once, and hashes
  `tests/render/goldens/*.png` before and after the merge.
- **L3 — never remove a worktree with unmerged commits.** `git worktree remove` only after the merge is
  verified on `main`.
- **L4 — shared files.** `docs/reference/README.md`, `.github/workflows/ci.yml`,
  `Projects/Starforge/CMakeLists.txt` and `tests/CMakeLists.txt` are touched by more than one WO; each
  prompt says which rows/lines it may add. Resolve conflicts by keeping both sides.
- **L5 — registers.** Defects → the KI register (rule above); new surface (services, DataBus, widgets,
  templates, screens panel, source links, live loop) → `01-Design-Contracts.md §13` table rows with the
  acceptance ID that proves each one.

## Build and test commands (as of AP-00; AP-05 may simplify)

```
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON
cmake --build build --config Debug --parallel      (and Release)
build\Runtime\Debug\CosmicTests.exe                (and Release)
build\Runtime\Debug\CosmicRenderTests.exe          (needs -DCOSMIC_BUILD_RENDER_TESTS=ON; real GPU)
powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1
powershell -ExecutionPolicy Bypass -File tests\check_docs_coverage.ps1
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest tests\acceptance\manifests\<name>.manifest.json -Config Release
```
The VS-bundled cmake is not on PATH (see the stability packet's evidence). New `.cpp` files under
`Cosmic/src` need a re-configure (the engine glob has no `CONFIGURE_DEPENDS`).
