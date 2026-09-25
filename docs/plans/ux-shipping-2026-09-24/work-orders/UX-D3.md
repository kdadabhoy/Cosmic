# UX-D3 — Developer-tier updates (the AP-D2 remainder) + the `systems/app-platform.md` explainer

**Gate:** G4 · **Wave:** 4 (alone) · **Runs:** worktree `ux/d3` at `build\_lanes\ux-d3` · **Base:** `main` after
wave 3 has landed (UX-D1 → UX-05 → UX-H1) · **Depends on:** UX-D1 (its files are the renamed
`docs/developer/**`), UX-01..UX-05 and UX-G0 (the behaviour it documents), UX-D2 (`app-services.md` and
`API-MATRIX.md`, which it links instead of duplicating) · **Acceptance:** DOC01, DOC03 · **Model:** Opus 5.5 ·
**Effort:** high · **Status:** not started

The developer half of the old AP-D2 (its items 2 and 4, plus the stale-mention sweep AP-D1 left in its report
§7): the seven chapters the App Platform and this campaign changed are brought up to the code, the app
platform gets its systems explainer, and the live tiers stop citing projects that were deleted from `main`.
There is no separate `app-authoring.md`: the user side is guides 02–04 (UX-D1), the developer side is these
chapter updates, and the per-call side is `app-services.md` (UX-D2).

## Copy-paste prompt

~~~text
Execute only UX-D3 from the Cosmic "UX & Shipping" packet (docs/plans/ux-shipping-2026-09-24/). Read, and
only: work-orders/README.md (global rules, lane rules L1-L5, build commands, next KI); 01-Contracts.md §7
(documentation tiers) and §10 (your ownership row); docs/plans/2d-stability-2026-09-16/
03-Acceptance-Test-Catalog.md rows DOC01 and DOC03 (lines 211 and 213); docs/developer/README.md (the
authoring contract, the five rules, the verification bar, the mandatory chapter format - keep all of it);
docs/systems/README.md (the explainer format and the index); docs/plans/app-platform-2026-09-18/evidence/
AP-D1/report.md §4 and §7 (the sweep AP-D1 did and what it left); docs/reference/app-services.md and
docs/reference/API-MATRIX.md (link them, never duplicate them). Then each chapter you edit, in full, and
the header / .cpp / test each claim rests on.

Lane: from C:\dev\Cosmic run  git worktree add build\_lanes\ux-d3 -b ux/d3 main  (main after UX-D1, UX-05
and UX-H1 have landed; docs/developer/ must exist - if it does not, stop and report). Work only in
C:\dev\Cosmic\build\_lanes\ux-d3 with $env:COSMIC_SDK set to it; configure -DCOSMIC_2D_ONLY=ON
-DCOSMIC_BUILD_TESTS=ON and build Release, so you can check behaviour in the editor and compile every
example you write in a scratch App project under build\_temp\ux-d3\ (never committed). You edit docs only.

The five rules (docs/developer/README.md "The five rules"; docs/guide/README.md:26-44 at 0c2edd8):
(1) the headers are the source of truth - header, then .cpp, then tests; (2) older text is a quarry, not a
source - verify every borrowed line, the contracts and the plan included; (3) chapters derive from the
engine on this base; (4) retire what you replace - text that described the pre-campaign behaviour becomes a
dated History note or goes; (5) say what you found. Verification bar: every example compiles, every
failure mode is stated, one trunk, link the reference / explainer / user guide rather than repeat them.

Deliverables:
1. Chapter updates in docs/developer/, each in the mandatory format (header block, Quick start, task
   sections, Common patterns, Pitfalls, See also), each linking the user guide that walks the same ground
   (docs/guides/NN-*.md) instead of repeating its steps:
   - scripting.md: services vs scripts vs systems - which to use when (a table); AppService with every
     virtual optional (scripting/AppService.h:87-108), the minimum CS_SERVICE(T).Order(n) CS_END;
     (scripting/ModuleMacros.h:82, ServiceBuilder AppService.h:122), AppContext (:77), CS_PANEL (:112);
     the bus from a script through Data() (scripting/ScriptableEntity.h:280; SystemScript's at :332); the
     frame order (scripting/ServiceHost.h). The chapter and the developer README's row still say "all
     eight" proxies incl. Nav()/Animator()/Voxels(); at 0c2edd8 there are six - Telemetry, Physics,
     Character, Signals, Flow, Data (ScriptableEntity.h:142-280).
   - game-ui.md: the canvas and the seven bound widgets (scene/ui/UiComponents.h:68, :237-398), the
     sibling and anchor rules (verify them in scene/ui/UiSystem.cpp), the Active flag as UX-02 fixed it
     (an inactive element and its subtree are not drawn, updated, hit-tested or hosting panels), and which
     gizmo edits which selection (UX-02).
   - flow-and-story.md: channel guards (FlowGuard::Channel, scene/FlowMachine.h:93, highest precedence);
     "when" transitions (:115-120: evaluated after the signal drain and before timers, first match wins, a
     when without an if never fires); StartAt (:205); the key bridge (scene/FlowKeyBridge.h,
     FlowMachine::KeySignals :214) with the table of key names; the Event / Key / Timer / When picker as
     UX-01 left it in editors/FlowEditor.cpp. The "key:Escape only" text becomes history.
   - getting-started.md: the template kinds (App / Game / Blank; New Project defaults to App - UX-03),
     the samples on disk (templates/samples/* plus Projects/* with a project.cproj, PendulumLab featured -
     UX-03), the SDK zip and the installer (UX-04), the root build scripts; link guides 00 and 01.
   - project-anatomy.md: services in the lifecycle tables; both hosts' frame order (ServiceHost.h,
     layers/PlayerLayer.cpp, Starforge's Play path), including UX-G0's physics-after-services init.
   - building-and-shipping.md: the one package layout (Projects/Starforge/src/Packager.cpp and
     installer/Stage-AppPackage.ps1, *.bak skipped since KI-63 / UX-04); the SDK zip and Starforge-Setup
     (installer/Stage-Sdk.ps1, StarforgeSetup.iss, release.yml's sdk job - UX-04); the consumer path (the
     sdk preset in CMakePresets.json, tools/Build-Sdk.ps1, tools/New-AppRepo.ps1, ci.yml's consumer job -
     UX-05); link guide 06.
   - editor-ui-and-theming.md: the Screens panel with UX-02's Scenes list, the DataBus panel, the rect
     gizmo, source links with UX-02's "Flow:" line, the live chip, Edit ▸ Preferences… (UX-02).
   In docs/developer/README.md: the chapter-table rows you touched (Covers + Status "✅ <date> (UX-D3)"),
   the proxy list, the "Not covered by any reference chapter yet" paragraph (the four app headers now
   route to ../reference/app-services.md - say so as history), and lighting-2d's "(AP-D2 rewrites)" note
   (not scheduled in this packet - say that instead).
2. docs/systems/app-platform.md. It does NOT exist at 0c2edd8 (the plan's "remove its SKELETON banner"
   assumed a skeleton that was never written): create it in the explainer format (docs/systems/README.md
   "Document format": One-liner / Source / API Reference / Guide, then §1-§6). Content: why the bus lives
   in the host, why services are recreated on every reload, the frame order and its reasons, how hosted
   panels coexist with the canvas, and the design rule - logic is C++ plus the flow; the Inspector shows
   where logic lives but never holds it (UX-02's item-18 rationale). No SKELETON banner; add its row to
   docs/systems/README.md's table as WRITTEN (UX-D3). check_docs_coverage.ps1 cannot see this file (strict
   mode only reads docs/reference/ chapters the manifest routes to, check_docs_coverage.ps1:307-313), so
   prove the same bar yourself: evidence/UX-D3/check_explainer.ps1 applies that checker's regex (:323) to
   data/DataBus.h, scripting/AppService.h, scripting/ServiceHost.h and scene/FlowKeyBridge.h and exits 1
   unless app-platform.md names every class it finds.
3. The stale 3D mentions (AP-D1 report §7, estimated there at ~200): every worked-example reference to a
   deleted project (ViperSim, Frontier, Engine3DDemo, ForgeIsle, ForgePlayground, ForgeBlocks) in
   docs/developer/**, docs/reference/*.md and docs/systems/*.md is rewritten to a current exemplar (the
   templates, PendulumLab, AnalysisSample, SF_Telem) with a verified path:line, or the sentence goes; the
   dated History notes stay. A grep at 0c2edd8 finds 318 occurrences in 44 files of those three tiers,
   History notes included (heaviest: reference/math.md 25, guide/sim-math-toolkit.md 21,
   reference/cameras.md 20); re-grep on your base - UX-H1's tidy may have removed some.
   reference/cameras.md still documents NavigationCube and ScenePicker, deleted by AP-05: remove those
   entries. Record a before/after count per file.
4. docs/reference/ecs.md - the component count. It says 34 = 19 in Components.h + 15 in Components3D.h
   (ecs.md:1058, :1494, :1952, :2045-2051; docs/reference/README.md:23), but scene/Components3D.h does not
   exist at 0c2edd8. Count the CS_REGISTER_COMPONENT lines on your base (at 0c2edd8: Components.h:689-707
   = 19, SelectableComponent.h:28 = 1; UiComponents.h:415-428 = 13 more, documented in
   developer/game-ui.md) and state what you counted. Move the Components3D section (:1494-1888) and the
   ScenePicker section (:2022-2044) to a parked twin docs/parked-3d/reference/ecs-3d.md with the PARKED
   banner at line 3 (AP-D1's mixed-chapter rule); fix the scope line (:10), the contents (:55-56), the
   configuration summary and the chapter-table cell in docs/reference/README.md (Status / Covers cells
   only - the one part of that file you may touch).

Acceptance. DOC01: check_docs_coverage.ps1 exits 0 (strict mode passes for every written reference chapter
you touched), check_docs_links.ps1 strict 0, check_api_matrix.ps1 exits 0 (a chapter edit must not break
an anchor a matrix row links), check_gl_conformance.ps1 exits 0, docs/plans/app-platform-2026-09-18/
evidence/AP-D1/check_parked.py exits 0, check_explainer.ps1 exits 0, and no skeleton is labelled complete
(a script compares every index Status cell with the file's banner). DOC03: outside dated History notes no
live tier names a deleted project or describes a 3D path as current - the before/after table proves it.
Every example you add compiles in the scratch project (command and exit code recorded).

Evidence: evidence/UX-D3/report.md in the WO-10 layout (README rule 8) plus check_explainer.ps1, the
status-vs-banner script, the stale-mention before/after table and the example build log excerpt. Per rule
5, list per chapter what the older text got wrong. A defect found while documenting is registered in the
KI register (next number in work-orders/README.md) and reported, not fixed here.

Land per L2: git rebase main; rebuild Release (docs only, so the retained units need not re-run unless
the rebase brought code); every check above exits 0; commit on ux/d3 as kdadabhoy
<kdadabhoy28@gmail.com> with no Co-Authored-By or AI trailer; never push. One commit per chapter group:
scripting + project-anatomy; game-ui + editor-ui-and-theming; flow-and-story; getting-started +
building-and-shipping; systems/app-platform.md + the systems index; the stale sweep (one per tier); ecs.md +
its parked twin + the reference statuses; the developer README + evidence. Report in at most 40 lines: the
chapters changed, what each got wrong before, the stale-mention counts, the ecs count and how it was
derived, and anything UX-Q1 must re-check.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `01-Contracts.md` §7, §10 (UX-D3 row); the stability catalog's DOC01/DOC03 rows;
`docs/developer/README.md`; `docs/systems/README.md`; `../app-platform-2026-09-18/evidence/AP-D1/report.md`
§4, §7; `docs/reference/app-services.md`; `docs/reference/API-MATRIX.md`; then every chapter edited, in full.

## Owns / May touch

See `01-Contracts.md` §10 (UX-D3 row). Also needed and **not** in that row (recorded as contract deviations
for the integrator; UX-D3 runs alone, so no lane shares them): `docs/reference/*.md` and `docs/systems/*.md`
for the stale-mention rewrite only; `docs/systems/README.md` (one index row);
`docs/parked-3d/reference/ecs-3d.md` (new parked twin of the removed `ecs.md` sections).

## Scope

- **In:** deliverables 1–4, DOC01, DOC03.
- **Out:** the user guides and their pictures (UX-D1); `API-MATRIX.md` / `app-services.md` content (UX-D2 —
  a broken link into them is fixed in the linking chapter); a full rewrite of `lighting-2d.md` (AP-D1 §7;
  unscheduled — its stale mentions are in scope, the rewrite is not); writing skeleton reference or systems
  chapters other than `app-platform.md`; DOC02 and the showcase (UX-Q1); any source change.

## Deliverables

The seven updated chapters + `docs/developer/README.md`; `docs/systems/app-platform.md` + its index row; the
stale-mention sweep across the three tiers; `docs/reference/ecs.md` with the true count and its parked twin;
the reference chapter-table cells; `evidence/UX-D3/report.md` with the scripts and tables.

## Done when (DoD)

The four checkers, `check_parked.py`, `check_explainer.ps1` and the status-vs-banner script exit 0 on the
rebased tree; every example added compiles; the before/after table shows no deleted-project mention outside a
History note; `app-platform.md` has no banner and names every `COSMIC_API` class of the four app headers;
every index Status agrees with its file.

## Rollback

Docs only: revert the lane's commits (the `ecs.md` move is a cut-and-paste into `docs/parked-3d/`, so a revert
restores both files exactly).
