# UX-D2 — API matrix by feature, the `app-services.md` reference chapter, `check_api_matrix.ps1`

**Gate:** G2 · **Wave:** 2 (worktree, alongside UX-04 and UX-G0; lands **last**: 04 → G0 → D2) · **Runs:**
worktree `ux/d2` at `build\_lanes\ux-d2` · **Base:** `main` after wave 1 has landed (UX-01 → UX-02 → UX-03) ·
**Depends on:** wave 1 landed; rebases onto UX-04 and UX-G0 before landing · **Acceptance:** DM01, DM02,
DOC01 · **Model:** Opus 5.5 · **Effort:** high · **Status:** not started

Item 14 plus the old AP-D2 item 3. Today `docs/reference/README.md` is a per-header manifest (15 chapters,
five still skeletons) and nothing answers "which call do I use for X, and where is one being used". This WO
adds that lookup — one table per feature area, every row tied to its header by a checker so it cannot rot,
every example compiled — and writes the reference chapter the App Platform headers never got (at `0c2edd8`
their manifest rows, `docs/reference/README.md:206-209`, point at developer-guide chapters instead).

## Copy-paste prompt

~~~text
Execute only UX-D2 from the Cosmic "UX & Shipping" packet (docs/plans/ux-shipping-2026-09-24/). Read, and
only: work-orders/README.md (global rules, lane rules L1-L5, build commands); 01-Contracts.md §8 (matrix
format and checker contract), §10 (your ownership row) and §11 (you add the "API matrix + checker" row);
03-Acceptance-Catalog.md rows DM01, DM02 and the retained DOC01; docs/reference/README.md (the chapter
table, the entry format at :33-74, the coverage manifest); tests/check_docs_coverage.ps1 (lines 1-60 and
the strict-mode loop at :290-354 - the style you copy and the rule your chapter must pass);
.github/workflows/ci.yml lines 20-50; the five headers app-services.md documents (data/DataBus.h,
scripting/AppService.h, scripting/ServiceHost.h, scene/FlowKeyBridge.h, scene/FlowMachine.h). Then each
header a matrix row cites and each reference chapter you link.

Lane: from C:\dev\Cosmic run  git worktree add build\_lanes\ux-d2 -b ux/d2 main  (main after UX-01..03).
Work only in C:\dev\Cosmic\build\_lanes\ux-d2 with $env:COSMIC_SDK set to it; configure
-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON and build Debug + Release (DM02 compiles into CosmicTests).

Authoring rules (docs/guide/README.md:26-44 and the reference rules at docs/reference/README.md:65-74):
(1) the header is the source of truth - signatures are copied, never paraphrased; (2) verify every
borrowed line (the contracts, older chapters, the showcase feature list); (3) rows derive from the headers
and from real use in Projects/, not from a wish list; (4) retire what you replace - the four rows you
re-point stop sending readers to guide chapters; (5) say what you found (a comment that disagrees with its
code, a mis-routed manifest row, a public call nothing uses).

Deliverables:
1. docs/reference/API-MATRIX.md. An intro (what it is, how rows are checked, how to add one), then one
   "## <area>" section per §8 area in this order: Application & layers · Time & clocks · Events & input ·
   2D rendering · Sprites, tilemaps, lights · UI widgets · DataBus & services · Flow & story · Scenes,
   ECS, serialization · Scripting proxies · Physics · Audio · Assets, VFS, Config · Serial & telemetry ·
   Jobs · Math & sim toolkit · Editor hosting. Each section is one table:
     | Call | Header | What it does | Use it when | Example | Reference | Used by |
   Call: one backticked name copied from the header - `Class::Member` (a method; for a component, the
   field you set), a free function, or a macro. Header: the path under Cosmic/src/. Example: EITHER a
   snippet of at most 6 statements in one inline code span (a table cell cannot hold a fenced block) that
   compiles on this base, OR a backticked path:line into Projects/PendulumLab, Projects/SF_Telem,
   Projects/AnalysisSample or Projects/Starforge/assets/templates/** whose line shows the call (inline
   code, not a link - the link checker would read ":57" as part of the file name). Reference: a link to
   the entry (anchor style #classnamemethodname, docs/reference/README.md:74); "— (skeleton:
   <chapter>.md)" when the chapter is a skeleton; "— (no reference chapter; developer chapter:
   [x](../guide/x.md))" when the manifest routes the header to the guide tier - UX-D1's rename script
   rewrites those links later. Used by: PendulumLab / SF_Telem / AnalysisSample / the templates, from a
   grep, or "—".
   Must-haves (verified at 0c2edd8): UI widgets = CanvasComponent (scene/ui/UiComponents.h:68) and the
   seven bound widgets UiValueText / UiGauge / UiIndicator / UiPlot / UiSlider / UiToggle /
   UiHostedPanelComponent (:237-398). DataBus & services = DataBus (data/DataBus.h:73), the AppService
   virtuals (scripting/AppService.h:87-108), CS_SERVICE (scripting/ModuleMacros.h:82), CS_PANEL
   (AppService.h:112). Scripting proxies = the six on ScriptableEntity: Telemetry, Physics, Character,
   Signals, Flow, Data (scripting/ScriptableEntity.h:142-280) - Nav/Animator/Voxels are parked 3D, no
   rows. Serial & telemetry includes the transport seam: SerialPort(std::unique_ptr<ISerialTransport>)
   (serial/SerialPort.h:87) and SerialLink(std::unique_ptr<ISerialTransport>) (serial/SerialLink.h:48),
   plus one FakeSerialTransport row whose Header is tests/FakeSerialTransport.h (:34) - the fake never
   ships (serial/ISerialTransport.h:19-21), so it is the one row outside Cosmic/src/; record that as a
   §8 contract deviation. Coverage: every header in the reference manifest has at least one row or a line
   in a closing "Headers without a call row" table giving the reason (types only, engine plumbing).
2. docs/reference/app-services.md in the entry format: class intros plus one entry per public call of
   data/DataBus.h, scripting/AppService.h, scripting/ServiceHost.h, scene/FlowKeyBridge.h, and the
   FlowMachine additions - FlowGuard::Channel (scene/FlowMachine.h:93), the "when" transition kind
   (:115-120), StartAt (:205), SetDataBus (:210), KeySignals (:214). Signatures verbatim; every entry
   states its failure behaviour; every example compiles (the examples also go through DM02). No
   STATUS: SKELETON banner, so strict mode applies: the chapter must name every class|struct COSMIC_API
   its four headers declare - at 0c2edd8 DataValue, DataBus, PanelRegistry, AppService, ServiceHost,
   FlowKeyBridge (the regex at check_docs_coverage.ps1:323). In docs/reference/README.md re-point the four
   rows at :206-209 to app-services.md (the scene/FlowMachine.h row at :196 stays on flow-and-story; link
   the additions from the new chapter's See also), add an "App Services" row to the chapter table, and add
   one line above that table pointing at API-MATRIX.md.
3. tests/check_api_matrix.ps1 - PowerShell 5.1, pure ASCII (the existing checkers' header comment says
   why), the same header / usage / exit-code style. It parses every table row under a "## " area of
   API-MATRIX.md and exits 1, one line per miss, when: a row does not have 7 cells; Call is not one
   backticked name; the Header file does not exist (Cosmic/src/<h>, or tests/<h> for the one fake row);
   the header lacks the identifier (Class::Member: both names as whole words; a function: the name
   followed by "("; a macro: "#define NAME"); a path:line example's file is missing or shorter than the
   line, or the call's last name is not within 3 lines of it; a §8 area is missing or empty; a manifest
   header has neither a row nor a "without a call row" entry; tests/test_api_matrix_examples.cpp is out of
   date with the matrix. -EmitExamples regenerates that file: one doctest TEST_CASE per area, each inline
   snippet verbatim inside a wrapper (a free function, or a member of an AppService / ScriptableEntity
   subclass when the snippet calls a protected member), compile-only plus a smoke call where cheap.
   -Matrix <path> checks another file (for the negative runs). Clean output:
   "API matrix: clean (<rows> rows, <areas> areas, <n> path:line examples, <m> compiled snippets)."
4. tests/test_api_matrix_examples.cpp (generated, committed) and its line in tests/CMakeLists.txt's
   COSMIC_TEST_SOURCES (:9). .github/workflows/ci.yml: one step "API matrix audit" right after "Markdown
   link audit" (ci.yml:44-46 at 0c2edd8), shell pwsh, run ./tests/check_api_matrix.ps1, with a comment in
   its neighbours' style. Nothing else in ci.yml (UX-05 adds its job in wave 3).

Acceptance. DM01: check_api_matrix.ps1 exits 0 on the rebased tree, plus one negative run per failure rule
(a scratch copy with one broken row each -> exit 1 naming that row), outputs recorded. DM02: CosmicTests
builds Debug + Release with 0 warnings with the examples TU in and passes in both configs (count =
baseline + the new cases); every path:line resolves through the checker. DOC01: check_docs_coverage.ps1
exits 0 with app-services.md in strict mode; check_docs_links.ps1 strict 0 (every Reference anchor
resolves); check_gl_conformance.ps1 exits 0.

Evidence: evidence/UX-D2/report.md in the WO-10 layout (README rule 8): rows per area, manifest headers
covered vs listed without a row, inline vs path:line examples, the negative runs, the contract deviation,
what you found. A header that does not do what its comment says is a finding; if it is a defect, register
a KI (next number in work-orders/README.md) and report it - do not fix code here.

Land per L2 after UX-04 and UX-G0 have landed: git rebase main (UX-G0 changes ProjectManifest.h,
SceneSerializer.cpp and PlayerLayer.cpp - re-run the checker and the examples build after the rebase;
tests/CMakeLists.txt and docs/reference/README.md conflicts keep both sides); rebuild Debug + Release with
0 warnings; CosmicTests both configs; the four checkers exit 0; commit on ux/d2 as kdadabhoy
<kdadabhoy28@gmail.com> with no Co-Authored-By or AI trailer; never push. Commits: app-services.md + the
manifest rows; API-MATRIX.md (two or three commits by area group); the checker + generated examples +
CMake line + CI step; evidence + the §11 row. Report in at most 40 lines: rows per area, headers without a
row and why, the deviation, what you found.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `01-Contracts.md` §8, §10 (UX-D2 row), §11; `03-Acceptance-Catalog.md` (DM01, DM02,
DOC01); `docs/reference/README.md`; `tests/check_docs_coverage.ps1` (1–60, 290–354); `.github/workflows/ci.yml`
(20–50); the five headers `app-services.md` documents; then each header a row cites and each chapter linked.

## Owns / May touch

See `01-Contracts.md` §10 (UX-D2 row). Also needed and **not** in that row (recorded as a contract deviation
for the integrator): `tests/test_api_matrix_examples.cpp` (new, generated) and one line in
`tests/CMakeLists.txt` — DM02 in `03-Acceptance-Catalog.md` requires both; UX-G0 may also add lines to
`tests/CMakeLists.txt` in the same wave (L4: keep both sides).

## Scope

- **In:** deliverables 1–4, DM01, DM02, DOC01.
- **Out:** guides and the `docs/guide/` rename (UX-D1); developer-chapter prose (UX-D3); writing the skeleton
  reference chapters (a row may say "— (skeleton)"); any engine or editor source change (report it).

## Deliverables

`docs/reference/API-MATRIX.md`; `docs/reference/app-services.md`; the re-pointed manifest rows, the chapter-
table row and the matrix pointer in `docs/reference/README.md`; `tests/check_api_matrix.ps1`;
`tests/test_api_matrix_examples.cpp` + its CMake line; the CI step; `evidence/UX-D2/report.md`; the
`01-Contracts.md` §11 row.

## Done when (DoD)

`check_api_matrix.ps1` exits 0 and fails on each negative case; CosmicTests builds and passes in both
configs with the examples TU; `check_docs_coverage.ps1` passes with `app-services.md` strict;
`check_docs_links.ps1` strict 0; every §8 area has a table; every manifest header has a row or a stated
reason.

## Rollback

Revert the lane's commits. The CI step and the examples TU go with them; the manifest rows fall back to their
`../guide/` targets, which still exist until UX-D1's rename lands.
