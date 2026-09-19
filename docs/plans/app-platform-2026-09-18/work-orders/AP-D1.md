# AP-D1 — Docs structure: archive, parked 3D, roadmap v5 skeleton, link checker

**Gate:** G2 · **Wave:** 2 (worktree, alongside AP-02 and AP-04) · **Runs:** worktree `ap/d1` ·
**Base:** `main` after AP-P1 has landed · **Depends on:** AP-P1 landed · **Acceptance:** DOC01, DOC03,
DOC04 · **Model:** Opus 5 · **Effort:** high · **Status:** not started

The structural half of the former WO-12, widened by D-DOCS and D-PURGE: everything completed or
superseded goes to an archive tier with a dated banner and a replacement link; every 3D chapter goes
to `docs/parked-3d/` with a PARKED banner; the coverage manifest is rewritten so the checker stays
green; a real Markdown link/anchor checker is added to CI; the roadmap becomes v5. Content writing
(the new app-authoring chapter, chapter updates) is AP-D2, after the code lanes land.

## Copy-paste prompt

~~~text
Execute only AP-D1 from the Cosmic App Platform packet (docs/plans/app-platform-2026-09-18/). Read
work-orders/README.md (all, lane rules L1-L5), then ONLY 01-Design-Contracts.md sections 10 (your row)
and 11, 00-Start-Here.md (decisions + the deferred list, for the roadmap), and 03-Acceptance-Catalog.md
rows DOC04 and the retained DOC01/DOC03 (../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md
lines 211-213). Do not read the docs you are moving beyond their first 10 lines unless a task needs it.

Lane setup: from C:\dev\Cosmic run  git worktree add ..\Cosmic-ap-d1 -b ap/d1 main  (main must already
contain AP-P1), work only inside C:\dev\Cosmic-ap-d1, set $env:COSMIC_SDK to it. You touch docs, the
root README, two PowerShell checkers and one CI step — never engine or editor source.

Do, in order, scripting every mechanical move (PowerShell or Python kept in evidence/AP-D1/):
1. Link checker first: tests/check_docs_links.ps1 per section 11 (relative file links + #anchors with
   GitHub slug rules; strict for live tiers, warn-only under docs/archive/**, docs/plans/archive/**,
   docs/parked-3d/**, the stability evidence dir; ASCII-only, PowerShell 5.1). Run it on the tree as it
   is and record the baseline breakages; add its step to .github/workflows/ci.yml after the two audits
   (this is the one line you may add there).
2. Archive plans: git mv docs/plans/12..29-*.md to docs/plans/archive/ (the 18 files), add the section-11
   banner at line 3 of each (origin phase/date, landed-by, replacement), extend docs/plans/archive/
   README.md's table with a row per doc and a Commit column, mark 29-phase30 superseded by the stability
   packet. 12-documentation-plan.md is linked from line 4 of every docs/reference/*.md and docs/systems/
   *.md (39 links): rewrite those links by script to the archived path. Archive the v4 roadmap as
   docs/plans/archive/00-MASTER-ROADMAP-v4.md and write the v5 skeleton at docs/plans/00-MASTER-ROADMAP.md
   (shipped foundation Phases 1-29 archived; stability campaign done with WO-11/12/13 absorbed; the App
   Platform table with statuses; the deferred list; the v3 rule). Prune FEATURE-MATRIX.md: 3D rows move
   under a "Parked (engine-3d)" heading with no phase home; the rest stays.
3. Archive design docs: git mv docs/design/{forge-isle,example-images-gap-analysis,
   starforge-acceptance-demo,app-platform-acceptance,ui-flow-2d-acceptance,water-rendering-notes,
   starforge-ui}.md to docs/archive/design/ with the banner and a README index (| Doc | What it is |
   Superseded by |). Re-check frame-lifecycle.md, modularity-audit.md, responsive-rendering-and-pause.md:
   keep them live, add a dated note where they describe 3D paths that no longer exist on main.
4. Park 3D: create docs/parked-3d/{guide,reference,systems}/ and git mv docs/guide/{rendering-3d,
   voxels,navigation-and-ai,animation,world-systems}.md, docs/reference/{rendering-3d,world-systems}.md,
   docs/systems/{rendering-3d,terrain,water,particles,build-2d-3d-split}.md there. Park a chapter ONLY
   when every header it documents was deleted by AP-05; MIXED chapters stay live and lose their 3D
   sections to a parked twin: docs/reference/rendering-pipeline.md and docs/systems/rendering-pipeline.md
   (SceneRenderer/PostProcessStack are the live 2D spine; EnvironmentMap/ShadowMap/CoverageCapture parts
   move out), docs/systems/cameras-navigation.md (Camera2D stays; CAD orbit/fly/nav-cube parts move
   out). A moved or trimmed chapter keeps its STATUS: SKELETON banner exactly as it was, or the coverage
   checker's strict mode engages at the new path. Split docs/guide/lighting-and-environment.md
   into the 2D-lights part (stays, renamed lighting-2d.md, links fixed) and the parked rest; move the
   root README's Part II sections that describe 3D systems (sections 30-43 where they are 3D) into
   docs/parked-3d/README-part2-3d-systems.md verbatim, leaving a one-paragraph pointer in README.md.
   Every parked file gets the section-11 PARKED banner at line 3. Write docs/parked-3d/README.md (index,
   the engine-3d SHA and tag, how to resume 3D). Rewrite every docs/reference/README.md row that pointed
   at a moved chapter to its new relative path (the checker allows rows outside docs/reference/);
   check_docs_coverage.ps1 must exit 0 — if a rule in it blocks a parked path, add the smallest allowance
   and say so. The manifest must not gain or lose rows for existing headers.
5. Stale sweep in the LIVE tiers (docs/guide, docs/reference, docs/systems, docs/design (remaining),
   docs/engineering-notes, docs/README.md, README.md, Projects/*/README.md, tests/*.md): every mention of
   Frontier, Engine3DDemo, ForgeIsle, ViperSim, ForgePlayground, ForgeBlocks, engine-2d-as-a-branch,
   "byte-identical branches", COSMIC_2D_ONLY=OFF as a supported mode, and the 3D/2D worktree advice is
   rewritten to the trunk policy or moved into a dated "History:" note. docs/guide/README.md line 50
   becomes "the template projects, PendulumLab, AnalysisSample, SF_Telem". README.md section 1.6 and
   the doc-map tree at lines 9-20 are rewritten (add archive/, parked-3d/, engineering-notes/, the two
   packets). docs/README.md links both packets; ../2d-stability-2026-09-16/00-Start-Here.md gets a
   "closed; WO-11/12/13 absorbed by app-platform" line at the top.
6. Run tests/check_docs_links.ps1 (live tiers must be clean; warn-only tiers reported), both audits,
   and a script that asserts the PARKED banner on every docs/parked-3d/** file and that no live-tier
   link into parked-3d/ lacks the "(parked 3D)" label (DOC04). Record DOC01/DOC03/DOC04 in
   evidence/AP-D1/report.md with before/after file counts per tier.

Land per L2 (rebase onto main; rebuild is not needed for docs but run both audits + the link checker
on the rebased tree; commit on ap/d1 as kdadabhoy, no AI trailer, in logical commits: checker, plans
archive, design archive, parked-3d, stale sweep). Do not push. Return: the move tables (from -> to per
file), the manifest rows rewritten, the checker results, and anything you left for AP-D2.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `01-Design-Contracts.md` §10 (row), §11; `00-Start-Here.md`;
`docs/plans/archive/README.md`; `docs/archive/README.md`; `docs/reference/README.md` (the table);
`tests/check_docs_coverage.ps1` (only to find the chapter-link resolution); `README.md:1-60` and
`:300-350`.

## Owns / May touch

See §10 (AP-D1 row). May touch `.github/workflows/ci.yml` (one step) and `docs/reference/README.md`
(link rewrites only).

## Scope

- **In:** the six steps above.
- **Out:** writing the app-authoring chapter or updating chapters for the new APIs (AP-D2); any
  source change; rewriting parked 3D content.

## Deliverables

`tests/check_docs_links.ps1` + CI step; the archive tiers with banners and README tables; `docs/parked-3d/`
with index and banners; roadmap v5 skeleton + archived v4; pruned FEATURE-MATRIX; rewritten manifest
links; stale sweep; `evidence/AP-D1/report.md`.

## Done when (DoD)

Both audits and the link checker exit 0 on the rebased tree; DOC04's banner/label script passes; every
moved file is reachable from an index; no live doc mentions a deleted project as current.

## Rollback

Revert the lane's commits (git mv is fully reversible; no content was deleted).
