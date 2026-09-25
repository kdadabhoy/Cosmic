# UX-D1 — Guides tier: `docs/guides/` with eight captured guides; `docs/guide/` → `docs/developer/`

**Gate:** G3 · **Wave:** 3 (worktree, alongside UX-05 and UX-H1; lands **first**: D1 → 05 → H1) · **Runs:**
worktree `ux/d1` at `build\_lanes\ux-d1` · **Base:** `main` after wave 2 has landed (UX-04 → UX-G0 → UX-D2) ·
**Depends on:** UX-01, UX-02, UX-03 (the pictures show the fixed editor), UX-04 (the SDK zip and installer
guide 00 describes), UX-D2 (guides 02/03 link `API-MATRIX.md` and `app-services.md`) · **Acceptance:** DOC01,
DOC03, DG01, DG02 (made executable here; executed by UX-Q1 from the SDK zip) · **Model:** Opus 5.5 ·
**Effort:** high (Sonnet 5 acceptable for guide prose only, with Opus verifying every code claim and every
quoted label — the AP-D2 rule) · **Status:** not started

D-GUIDES made real (items 1, 2, 9, 10, 12, 13, 15 and the doc halves of 18 and 21): a user tier that starts
at "get Starforge", follows the editor's own labels step by step, and shows every step in a picture of the
real, fixed editor produced by the capture pipeline; the 24 developer chapters keep their authoring contract
under a new name. It lands first in wave 3 because UX-05 appends a section to guide 06 and UX-H1 rebases onto
the rename.

## Copy-paste prompt

~~~text
Execute only UX-D1 from the Cosmic "UX & Shipping" packet (docs/plans/ux-shipping-2026-09-24/). Read, and
only: work-orders/README.md (global rules 1-10, lane rules L1-L5, build commands, next KI);
00-Start-Here.md decision D-GUIDES; 01-Contracts.md §7 (guide format, image rule, images manifest, rename
map), §10 (your ownership row) and §11 (you add the "guides tier + images manifest" row);
03-Acceptance-Catalog.md rows DG01, DG02 and fixture F-GUIDESHOTS; docs/guide/README.md (the authoring
contract the developer tier keeps); docs/guide/pendulumlab-walkthrough.md;
docs/plans/app-platform-2026-09-18/evidence/AP-D1/report.md §3 with rewrite_links.py beside it (the scripted
move you copy). Then each file you edit, in full, and for every guide the editor source whose labels it quotes.

Lane: from C:\dev\Cosmic run  git worktree add build\_lanes\ux-d1 -b ux/d1 main  (main must already hold
UX-01..UX-04, UX-G0 and UX-D2 - check git log --oneline). Work only in C:\dev\Cosmic\build\_lanes\ux-d1 with
$env:COSMIC_SDK set to that folder; configure -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON and build Release
(Debug too before landing - you edit a .cpp). Every picture comes from THIS build's
build\Runtime\Release\Starforge.exe on the 2560x1440 display at 100 % scaling (the driver sizes the editor
window to 1600x900, GuideWalkthroughSelfTest.cpp:322 at 0c2edd8). Stage the SDK zip with UX-04's
installer\Stage-Sdk.ps1 and unzip it to build\_temp\ux-d1\sdk\ for guide 00 and the DG02 rehearsal.

The five authoring rules (docs/guide/README.md:26-44 at 0c2edd8; docs/developer/README.md after step 1):
(1) the source is the truth - every label a step quotes is copied from the string the editor draws (grep
it) and is visible in that step's picture; (2) verify every borrowed line - the old walkthrough, the
contracts and the plan's wording included; (3) guides derive from the editor on this base, not from the
plan; (4) retire what you replace - the walkthrough's "Before you start" cmake block moves to guide 00, its
troubleshooting for defects UX-01..04 fixed goes (the *.cscene.bak entry and the "66 files" count - KI-63;
the "when (guard only)" radio and its picture - replaced by UX-01's picker); (5) say what you found.

Do, in order (scripts and evidence in docs/plans/ux-shipping-2026-09-24/evidence/UX-D1/):
1. The rename, as its own first commit. rename_developer.py (modelled on AP-D1's rewrite_links.py; with a
   --reverse mode for rollback): git mv docs/guide -> docs/developer, except pendulumlab-walkthrough.md ->
   docs/guides/01-pendulumlab-walkthrough.md and images/pendulumlab/ (21 PNGs) ->
   docs/guides/images/pendulumlab/; writes moves-developer.json (from -> to per file = the §7 rename map);
   rewrites every Markdown link that resolves into a moved file in README.md, docs/** (live tiers,
   packets and evidence alike - the AP-D1 precedent), tests/**/*.md, Projects/*/README.md,
   Projects/*/docs/*.md and the docs/reference/README.md manifest rows (../guide/x.md -> ../developer/x.md;
   API-MATRIX.md included). Plain-text path mentions: rewrite them in live docs, in the comments of
   tests/check_docs_coverage.ps1, Run-GuideWalkthrough.ps1:1, GuideWalkthroughSelfTest.cpp:2 and - path
   text only - Cosmic/src/core/Application.h:122, tests/render/wo08_common.h:14 and :281,
   Projects/PendulumLab/src/Y02SelfTest.cpp:205; historic prose under docs/plans/** stays as written.
   Never rewrite parked-3d/guide/ (the parked tier keeps its own folder) and never touch docs/guides/;
   the walkthrough's links to sibling chapters become ../developer/<x>.md. No prose edits inside
   docs/developer/ (UX-D3 owns them): docs/developer/README.md changes only its intro paragraph (the user
   tier is ../guides/) and the walkthrough row's link. Gate after the commit: check_docs_links.ps1 strict
   0; check_docs_coverage.ps1 exit 0 with the same row count; docs/plans/app-platform-2026-09-18/evidence/
   AP-D1/check_parked.py exit 0; git grep -n "docs/guide/" lists only history under docs/plans/** and the
   parked tier.
2. The capture pipeline (one commit). GuideWalkthroughSelfTest.cpp: a second, screenshot-only plan
   selected by a new env var COSMIC_GUIDE_PLAN (walkthrough = today's plan and the default; guides = the
   new one), built from addShot/addLocate (:556-591): DebugLocateItem's green highlight (:349) becomes the
   red box in guide_shots.py; a control that is not an ImGui item gets its rect in the result JSON's
   shot_rects (:1140). The guides plan works on a copy of Projects/PendulumLab under the project root,
   never the tracked tree; shot names are <NN>-<step>-<slug>. Run-GuideWalkthrough.ps1 gains -SdkRoot
   (default: three levels above -Bin, the SdkDir() rule of StarforgeApp.cpp:437-447) from which
   COSMIC_SDK and the default COSMIC_GUIDE_REF derive, and -NoSdkEnv (leaves COSMIC_SDK unset, as
   DG02 requires). The wrapper no longer hard-codes <repo>\dist\PendulumLab2 (line 27 today): after UX-04 an
   external project packages to <project root>\dist\<App>, so it reads the dist path the self-test result
   JSON reports (the `dist` fact, GuideWalkthroughSelfTest.cpp:1087) and fails if that path is missing. New sibling Run-GuideShots.ps1 runs the guides plan, then tools\guide_shots.py per
   guide into docs\guides\images\<NN-slug>\. guide_shots.py keeps its defaults (--max-width 1440,
   --max-kb 600, lines 55-56) and gains only --report <json> (per image: green px, manual rects, size,
   sha256). tests/acceptance/manifests/ux-d1-guides.manifest.json in the ap03-editor.manifest.json shape:
   case DG01 (Run-GuideShots.ps1) and case DG02 (Run-GuideWalkthrough.ps1 -Bin {BIN} -NoSdkEnv), so UX-Q1
   runs it with -BinDir <unzipped SDK>\build\Runtime\Release.
3. The guides, one commit each with its images and manifest entries, in the §7 format: "# <Title> -
   Guide" (em dash); header block **What you will do** / **You will need** / **Time** / **Where this lives
   in the code** (a link to the developer chapter); numbered steps naming the menu, panel, button or field
   exactly as the editor labels it; one picture per step with that control boxed in red (the
   walkthrough's long steps keep several); ## What you should see; ## Troubleshooting (symptom first);
   ## Where this lives in the code.
   - 00-get-starforge.md: the SDK zip or Starforge-Setup-<ver>.exe (UX-04), or clone + ONE table of the
     root scripts vs raw cmake (build.bat = incremental, Debug unless given Release; build_all.bat = clean;
     build_all_release.bat; build_engine.bat = engine only; setup.bat = setx COSMIC_SDK); where
     Starforge.exe lives; COSMIC_SDK and the three-up fallback; first launch.
   - 01-pendulumlab-walkthrough.md: the moved chapter, re-captured on this build. New prologue pointing at
     00 instead of the cmake block. A "Where your code lives" box after Step 1: the tree under
     %USERPROFILE%\Documents\Starforge Projects\<Name>\ shown in the Content Browser (it roots at
     project://, ContentBrowserPanel.cpp:229 - check that it lists src/; if not, picture the Screens
     panel's Open script instead), Inspector ▸ Open source / Reveal (the button says Reveal,
     InspectorPanel.cpp:438-442), and the rebuild-safety statement: a rebuild never rewrites src/ -
     ScreenScaffold::CreateScript writes src/screens/<Name>Screen.h only if it is absent
     (ScreenScaffold.cpp:229) and inserts the CS_SCRIPT block idempotently (:74-92); BuildRunner only runs
     cmake; caveat: the Screens panel's Relink script (panels/ScreensPanel.cpp:274) rewrites the .cscene
     and clears that script's field overrides when the class name differs (ScreenScaffold.cpp:163-193,
     the clear at :186). Step 9 names every file by its full path. Steps 2, 4, 5.1, 6, 7 and 9 have no
     picture at 0c2edd8 - add them. Export stays as it is, re-verified.
   - 02-your-code-in-the-engine.md: Module.cpp, CS_SERVICE / CS_SCRIPT / CS_SYSTEM, Ctrl+B and the live
     loop, what a build produces, project.cproj, .cscene and .cflow in plain words.
   - 03-services-and-the-databus.md: what a service is; the bare minimum (a default-constructible class
     plus CS_SERVICE(T).Order(n) CS_END; every virtual optional, AppService.h:87-108); the lifecycle in
     one picture; channels; binding a plot, gauge, value text and slider; a button's Signal -> OnSignal;
     CS_PANEL hosted panels; the DataBus panel; links to app-services.md and API-MATRIX.md.
   - 04-screens-and-the-flow.md: the Screens panel with UX-02's Scenes list; the flow editor as UX-01 left
     it (states, transitions, the Event/Key/Timer/When picker, guards, overlays); how a button's Signal
     reaches the flow and where the Inspector now shows it (UX-02's "Flow:" line).
   - 05-arranging-a-screen.md: rect gizmo vs transform gizmo and which selection gets which, the eyeball =
     Active, anchors, snapping, the toolbar chips - as UX-02 left them.
   - 06-package-and-ship.md: File ▸ Package…, the installer script, where user data goes; end with the
     heading "## Your app in its own repo" and one line saying UX-05 writes it (UX-05 owns that section).
   - 07-editor-preferences.md: Edit ▸ Preferences… (UX-02) - autosave on/off and interval, where autosaves
     go (user://starforge/autosave/<Project>/), the .bak beside every save, the unsaved-changes prompt;
     layouts.
   - docs/guides/README.md: the index and the start-here page (order, one line and a time per guide).
   - docs/guides/images/manifest.json: one entry per image {image, guide, step, method:
     "driver"|"computer-use", source_shot (the raw file under build\_temp\ux-d1\shots\, F-GUIDESHOTS),
     annotated_by: "tools/guide_shots.py"}.
4. README.md: a "Start here" strip pointing at docs/guides/README.md above "## 📚 Documentation map"
   (README.md:149 at 0c2edd8); the doc-map tree, the tier table and the most-asked list name guides/ and
   developer/. docs/README.md leads with Guides and lists five tiers (Guides, Developer, Reference,
   Systems, Plans); its stale "AP-D2 adds the app-authoring chapter" status (docs/README.md:44) goes.

Pictures the driver cannot reach (Windows Explorer, the installer wizard, a terminal, the IDE that Open
source launches): list each in evidence/UX-D1/computer-use-requests.md (guide, step, window, state, the
control to box) and name them in your report. The orchestrator captures them into
build\_temp\ux-d1\shots\ (ORCHESTRATOR.md grants it Starforge only - name any other window you need) and
hands the paths back by SendMessage; you add the rect to annotations.json and run guide_shots.py. A shot
that cannot be taken leaves its step without a picture, named in the report - never a mock-up.

Acceptance. DOC01: check_docs_coverage.ps1, check_docs_links.ps1 (strict 0), check_api_matrix.ps1 and
check_gl_conformance.ps1 exit 0 on the rebased tree. DOC03: no live tier (docs/guides/ included) names
Frontier, Engine3DDemo, ForgeIsle, ViperSim, "2D build", engine-2d, byte-identical or COSMIC_2D_ONLY=OFF
outside a dated History note (grep, counts recorded). DG01: evidence/UX-D1/check_guide_images.py exits 0
on the catalog oracle - manifest.json lists every PNG under docs/guides/images/** and nothing else; each
source_shot's sha256 is in shots-sha256.txt; annotated_by is guide_shots.py and its --report shows green
px > 0 or a rect for that image; <= 600 KB and <= 1440 px wide; every ![...](images/...) in
docs/guides/*.md is in the manifest; no image serves two steps; every numbered step has a picture or a
named exception. DG02: made executable, not claimed - run ux-d1-guides on the lane build (-BinDir the
lane's Release) and record PASS, then rehearse the DG02 case once with -BinDir
build\_temp\ux-d1\sdk\build\Runtime\Release; UX-Q1 owns the verdict.

Evidence: evidence/UX-D1/report.md in the WO-10 layout (README rule 8) plus rename_developer.py,
moves-developer.json, check_guide_images.py, shots-sha256.txt, guide-result-*.json, annotations.json, the
guide_shots reports and computer-use-requests.md; raw shots stay in build\_temp\ux-d1\shots\. A defect you
meet in the editor while capturing goes into the KI register (next number in work-orders/README.md) and
the report; it is not fixed here.

Land per L2: git rebase main; rebuild Debug + Release with 0 warnings; CosmicTests both configs; the
ux-d1-guides manifest; the four checkers exit 0; commit on ux/d1 as kdadabhoy <kdadabhoy28@gmail.com>
with no Co-Authored-By or AI trailer; never push. Commits: the rename; the capture pipeline; one per guide
(00..07; the index and manifest.json with the last); README.md + docs/README.md; evidence + the §11 row.
Report in at most 40 lines: the rename table summary, each guide with its step and picture counts, the
shots the driver could not take, what you found (rule 5), and what UX-D3 and UX-Q1 must re-check.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `00-Start-Here.md` (D-GUIDES); `01-Contracts.md` §7, §10 (UX-D1 row), §11;
`03-Acceptance-Catalog.md` (DG01, DG02, F-GUIDESHOTS); `docs/guide/README.md`;
`docs/guide/pendulumlab-walkthrough.md`; `../app-platform-2026-09-18/evidence/AP-D1/report.md` §3 +
`rewrite_links.py`; then every file edited, in full, and the editor sources each guide quotes.

## Owns / May touch

See `01-Contracts.md` §10 (UX-D1 row). The four source files whose comments name `docs/guide/`
(`Cosmic/src/core/Application.h:122`, `tests/render/wo08_common.h:14,281`,
`Projects/PendulumLab/src/Y02SelfTest.cpp:205`) fall under "every file whose links the rename rewrites" —
path text only. `docs/guides/06-package-and-ship.md` is shared with UX-05 (L4): UX-D1 writes the file and
the placeholder heading; UX-05 fills that section after rebasing.

## Scope

- **In:** the rename (scripted, checker-enforced); the capture pipeline (driver plan, wrappers,
  `guide_shots.py --report`, the `ux-d1-guides` manifest); guides 00–07, the index and the images
  manifest; the README / `docs/README.md` front door; DG01; DG02 made runnable against the zip.
- **Out:** prose inside `docs/developer/` (UX-D3); `API-MATRIX.md` and `app-services.md` (UX-D2); the
  "Your app in its own repo" section (UX-05); editor or engine code beyond the driver (a needed seam is
  reported, not added); `docs/showcase/` and the roadmap (UX-Q1); the DG02 verdict (UX-Q1).

## Deliverables

- `docs/developer/**` (renamed); `docs/guides/01-pendulumlab-walkthrough.md` + `docs/guides/images/pendulumlab/`.
- `docs/guides/README.md`, `00-get-starforge.md`, `02-your-code-in-the-engine.md`,
  `03-services-and-the-databus.md`, `04-screens-and-the-flow.md`, `05-arranging-a-screen.md`,
  `06-package-and-ship.md` (with the UX-05 placeholder heading), `07-editor-preferences.md`.
- `docs/guides/images/<NN-slug>/*.png` + `docs/guides/images/manifest.json`.
- The `COSMIC_GUIDE_PLAN=guides` plan in `GuideWalkthroughSelfTest.cpp`; `Run-GuideWalkthrough.ps1`
  (`-SdkRoot`, `-NoSdkEnv`); `Run-GuideShots.ps1`; `tools/guide_shots.py --report`;
  `tests/acceptance/manifests/ux-d1-guides.manifest.json`.
- The `README.md` / `docs/README.md` front door; `evidence/UX-D1/**`; the `01-Contracts.md` §11 row.

## Done when (DoD)

The four checkers and `check_parked.py` exit 0 on the rebased tree; DG01's check passes; the
`ux-d1-guides` manifest passes on the lane build; every numbered step of every guide has a real-editor
picture or a named exception; Debug + Release build with 0 warnings and CosmicTests is at or above the
baseline in both configs; `git grep "docs/guide/"` finds only history.

## Rollback

Revert the lane's commits in reverse order. The rename is one commit and `git mv` is reversible; once UX-05
or UX-H1 has landed on top of it, reverse it with `rename_developer.py --reverse` (the inverse of
`moves-developer.json`) instead of a plain revert. The driver's new plan runs only when
`COSMIC_GUIDE_PLAN=guides` is set, so reverting it changes no shipped behaviour.
