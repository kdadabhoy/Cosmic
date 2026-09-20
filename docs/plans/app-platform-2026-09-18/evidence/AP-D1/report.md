# AP-D1 — docs structure: archive, parked 3D, roadmap v5, link checker — report

Status: done, 2026-09-20. Acceptance **DOC01, DOC03, DOC04** (below). Executed directly on `main` in
`C:\dev\Cosmic` (no worktree: nothing else was editing the tree), base `ff41ea5` (the GUIDE lane
merge; AP-P1 and everything through AP-Q1 already on `main`). The same session then did the
"clone and run" repository cleanup Kaden asked for (README Quickstart, script fold, hygiene, the
clone check) — see [`clone-check.md`](clone-check.md).

## 1. Scope and provenance

| Item | Value |
| --- | --- |
| Base | `ff41ea5` (`main`) |
| Commits | see §9 — checker · plans archive · design archive · parked-3d + scripted sweep · hand sweep + Quickstart · gitignore ×2 · junk removal · evidence |
| Prompt deviations | (a) no worktree, per Kaden's instruction; (b) banners are dated **2026-09-20** (the day the moves happened), not the contract's literal `2026-09-18`; (c) `graphics/Mesh.h` is live on the trunk, so the 3D Rendering reference chapter was parked and the `Mesh.h` manifest row re-routed to `graphics-resources.md#mesh` with a short resource-surface section (§3d); (d) the scripted stale sweep replaces every `**Configuration:**` paragraph and adds one dated History note per affected chapter instead of rewriting ~200 inline worked-example mentions of the deleted projects line by line (AP-D2 owns chapter prose); (e) `build_2d.bat` / `build_all_2d.bat` deleted (Part B.1) — byte-for-byte the same configure as `build.bat` / `build_all.bat`. |
| Tools | PowerShell 5.1 (`check_docs_links.ps1`), Python 3.14 (`py -3`), git |

## 2. Link checker (`tests/check_docs_links.ps1`, step 1)

Scans `README.md`, `docs/**/*.md`, `tests/**/*.md`, `Projects/*/README.md`, `Projects/*/docs/*.md`;
resolves relative file links and `#anchors` with GitHub slug rules (lowercase, spaces → `-`, keep
letters/digits/`-`/`_`, strip other punctuation, `-N` suffix for repeated headings, `<a name>`/`{#id}`
honoured); skips URI schemes and everything inside fenced code / inline code. Strict (exit 1) for
live tiers; warn-only for `docs/archive/**`, `docs/plans/archive/**`, `docs/parked-3d/**`,
`docs/plans/2d-stability-2026-09-16/evidence/**`. ASCII-only, PowerShell 5.1. CI step "Markdown link
audit" added to `.github/workflows/ci.yml` after the two audits.

**Baseline on the tree as it was** ([`links-baseline.txt`](links-baseline.txt)): 210 files, 2471 local
links (1008 with anchors). Strict breakages **6** → after two checker fixes (underscores are kept in
slugs; `<T>` inside a code span is text, not an HTML tag) **3 real**: the three `navigation-and-ai.md`
links to deleted `tests/test_nav_*.cpp` (chapter parked → warn-only) plus one real broken anchor in
`time-and-ticks.md:471` (fixed: `#the-global-scale-policy-no-rewind`). Warn-only breakages: 19.

**Final** (HEAD): strict **0**, warn-only 62 (all inside archived/parked material whose relative links
predate the moves — recorded, deliberately not rewritten).

## 3. Move tables

### 3a. Plans → `docs/plans/archive/` (step 2; [`archive_plans.py`](archive_plans.py), [`moves-plans.json`](moves-plans.json))

| From | To |
| --- | --- |
| `docs/plans/12-documentation-plan.md` … `29-phase30-2d-hardening-plan.md` (18 files, same names) | `docs/plans/archive/<same name>` — ARCHIVED banner at line 3 with origin / landed-by / replacement; `29-phase30` marked superseded by the stability packet |
| `docs/plans/00-MASTER-ROADMAP.md` (v4) | `docs/plans/archive/00-MASTER-ROADMAP-v4.md`; **v5 written** at `docs/plans/00-MASTER-ROADMAP.md` |
| root `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` (untracked) | `docs/plans/archive/2d-trunk-consolidation-brief-2026-09.md` with the banner (an identical copy already existed as `2d-stability-2026-09-16/Original-Consolidation-Plan.md`; both kept, the banner says so) |
| `work-orders/ORCHESTRATOR.md` (untracked) | committed in place with a "Historical" note at the top |
| `2d-stability-2026-09-16/evidence/WO-07/l01-runner-Release/` (untracked runner output) | deleted |

`docs/plans/archive/README.md`: Commit column added, 20 new rows. `FEATURE-MATRIX.md`: 35 3D rows moved
under **"Parked (engine-3d)"** with phase home `— (parked; was …)`; the three editor-viewport rows that
still exist on the trunk (orbit camera, wireframe modes, viewport instrument) stayed live; the
"Pure-2D build configuration" row marked superseded by D-PURGE. 89 links rewritten by
[`rewrite_links.py`](rewrite_links.py) (39 of them the `12-documentation-plan.md` banner links in
`docs/reference/*.md` and `docs/systems/*.md`).

### 3b. Design docs → `docs/archive/design/` (step 3; [`archive_design.py`](archive_design.py))

`forge-isle`, `example-images-gap-analysis`, `starforge-acceptance-demo`, `app-platform-acceptance`,
`ui-flow-2d-acceptance`, `water-rendering-notes`, `starforge-ui` → `docs/archive/design/<same>.md`
with banners; `docs/archive/design/README.md` index (`| Doc | What it is | Superseded by |`).
`frame-lifecycle.md` and `modularity-audit.md` stay live with a dated note at line 3;
`responsive-rendering-and-pause.md` mentions no 3D path (no note). 43 links rewritten.

### 3c. Parked 3D → `docs/parked-3d/` (step 4; [`park_3d.py`](park_3d.py), [`moves-parked.json`](moves-parked.json))

| From | To |
| --- | --- |
| `docs/guide/{rendering-3d,voxels,navigation-and-ai,animation,world-systems}.md` | `docs/parked-3d/guide/<same>` |
| `docs/guide/lighting-and-environment.md` | **split**: original verbatim → `docs/parked-3d/guide/lighting-and-environment.md`; the 2D part (spine, post chain, composite order, `ApplyEnvironment`, `RenderToTexture`) → **`docs/guide/lighting-2d.md`** (new live chapter; its History note explains the split) |
| `docs/reference/{rendering-3d,world-systems}.md` | `docs/parked-3d/reference/<same>` |
| `docs/reference/rendering-pipeline.md` (mixed) | stays live (SceneRenderer/PostProcessStack); 3D scope → `docs/parked-3d/reference/rendering-pipeline-3d.md` |
| `docs/systems/{rendering-3d,terrain,water,particles,build-2d-3d-split}.md` | `docs/parked-3d/systems/<same>` |
| `docs/systems/rendering-pipeline.md` (mixed) | stays live; 3D scope → `docs/parked-3d/systems/rendering-pipeline-3d.md` |
| `docs/systems/cameras-navigation.md` (mixed) | stays live (Camera2D, orthographic, editor rig); CAD orbit/fly/ViewCube/picking → `docs/parked-3d/systems/cameras-navigation-3d.md` |
| root `README.md` Part II §30 "The 2D partition", the 3D lines of the source map, DG-6's `Renderer3D` nodes/edges, §40's build-flag paragraph | `docs/parked-3d/README-part2-3d-systems.md` (verbatim); one pointer paragraph "Where the 3D half went" left in §30 |
| — | `docs/parked-3d/README.md` (index, `engine-3d` `0e8894b` / tag `cosmic-pre-2d-2026-09-16`, how to resume 3D) |

Every parked file (18) has the §11 PARKED banner at line 3; every moved/trimmed skeleton kept its
`STATUS: SKELETON` banner. 226 links rewritten. `check_docs_coverage.ps1` needed **no allowance**
(rows may already point outside `docs/reference/`).

### 3d. Manifest rows rewritten (`docs/reference/README.md`)

| Header | Row now points at |
| --- | --- |
| `graphics/Mesh.h` | `graphics-resources.md#mesh` (was `rendering-3d.md`; new "Mesh" section documents the resource surface — the header is live on the trunk: `AssetLibrary`, `MeshRendererComponent`, `SceneRenderer.cpp`) |
| Chapters table: 3D Rendering, World Systems | `../parked-3d/reference/…` labelled "(parked 3D)", status `PARKED (was SKELETON — D10/D12)` |
| Chapters table: Frame Pipeline, Cameras & Navigation, Graphics Resources | scope text trimmed of the deleted classes |
| The ³ᴰ / ³ᴰ⁺ / gap-list blockquote | replaced by a dated History note |

The manifest neither gained nor lost rows for existing headers (120 rows before and after; the
checker reports 123 public headers = 120 rows + 2 footnote rows + `Cosmic.h`, clean).

## 4. Stale sweep (step 5; [`stale_sweep.py`](stale_sweep.py) + hand edits)

Scripted (52 live chapters): every `**Configuration:**` paragraph → the one-line trunk policy; one
dated **History (2026-09-20)** note per chapter that still cites Frontier / Engine3DDemo / ForgeIsle /
ViperSim / `#ifndef COSMIC_2D_ONLY` / "2D build" etc., saying how to read those mentions and naming the
current exemplars; every live link into `parked-3d/` labelled "(parked 3D)".
By hand: root `README.md` (Quickstart, doc map with archive/ parked-3d/ engineering-notes/ showcase/
and both packets, §1 "What is Cosmic", §1.5 script/option tables, §1.6 rewritten as "The Engine
Configuration" with a History block, §30 pointer, §35 DG-6, §40); `docs/README.md` (both packets,
parked-3d, showcase, status rows); `docs/guide/README.md` (exemplar list → "the template projects,
PendulumLab, AnalysisSample, SF_Telem", 3D table → "3D (parked)", `lighting-2d` row, Configuration
coverage section); `docs/systems/README.md`; `docs/design/README.md`;
`docs/guide/getting-started.md` and `building-and-shipping.md` (build sections, script tables, tree
listings, "The engine configuration" section); `2d-stability-2026-09-16/00-Start-Here.md` gets the
"Closed; WO-11/12/13 absorbed" line.

## 5. Acceptance

| ID | Result | Evidence |
| --- | --- | --- |
| DOC01 | **PASS** — coverage audit clean (123 headers / 120 rows / 5 skeletons); link checker strict 0; every archived doc has a replacement link in its banner; no skeleton lost its banner | `check_docs_coverage.ps1` exit 0, `check_docs_links.ps1` exit 0 at HEAD |
| DOC03 | **PASS** — one policy everywhere: `main` = 2D trunk, `engine-3d` = the 3D tree; every remaining "2D build / engine-2d / byte-identical / worktree" mention sits under a dated History note or in an archived/parked file | grep of the live tiers + the History notes |
| DOC04 | **PASS** — 18/18 parked files carry the banner at line 3; 207 labelled live links into `parked-3d/`, 0 unlabelled; the manifest has no row for a deleted header | [`check_parked.py`](check_parked.py) exit 0 |

## 6. File counts per tier (top-level `.md`, `git ls-tree`)

| Tier | Before (`ff41ea5`) | After (HEAD) |
| --- | --- | --- |
| `docs/guide/` | 31 | 26 (−6 parked, +`lighting-2d.md`) |
| `docs/reference/` | 17 | 15 |
| `docs/systems/` | 22 | 17 |
| `docs/design/` | 11 | 4 |
| `docs/plans/` (top level) | 20 | 2 (`00-MASTER-ROADMAP.md` v5, `FEATURE-MATRIX.md`) |
| `docs/plans/archive/` | 13 | 33 |
| `docs/archive/design/` | 0 | 8 |
| `docs/parked-3d/**` | 0 | 18 |

## 7. Left for AP-D2

- Chapter prose: the ~200 inline worked-example references to `Projects/ViperSim`, `Frontier`,
  `Engine3DDemo` in `math.md`, `sim-math-toolkit.md`, `cameras.md` (reference), `audio.md`,
  `serial-and-telemetry.md`, `jobs-and-parallelism.md`, `entities-and-components.md` etc. are covered
  by a chapter-level History note, not rewritten. `docs/reference/cameras.md` still documents
  `NavigationCube` / `ScenePicker` entries (deleted classes) under that note.
- `docs/guide/lighting-2d.md` is the verbatim 2D subset of the D55 chapter with a new intro; it needs
  a real rewrite (and the `SceneRendererSettings` 3D-era toggles need an honest entry).
- The app-authoring chapter, reference rows/entries for `DataBus`, `AppService`, `ServiceHost`,
  `FlowKeyBridge`, bound widgets; DOC02 (a person walks the Quickstart); roadmap v5 → final.
- `docs/reference/ecs.md` "What a 2D build sees" section and the 34-component count predate the purge.

## 8. Repository cleanup (Part B) — summary

See [`clone-check.md`](clone-check.md) for the literal clone → configure → build → test → launch run.
Deleted scripts: `build_2d.bat`, `build_all_2d.bat` (folded). `.gitignore`: `recordings/` added (it
was never ignored; a `git add -A` in this session briefly committed 66 MB of recorder output — the
offending commit was rewritten before anything left the machine); explicit negations for the two
force-added CSV fixtures. Removed 100 test-output `.bin` files that had been committed inside
evidence directories (their hashes live in the `checksum-material.txt` files). Verified tracked:
`docs/guide/images/**` (21), `docs/showcase/*.png` (12), `tests/render/goldens/*.png` (15),
`tests/fixtures/**` + `tests/acceptance/fixtures/**` (64) — all match the disk.

## 9. Local commits

`git log --oneline ff41ea5..HEAD` — listed in the session report (kdadabhoy, no trailer, not pushed).
