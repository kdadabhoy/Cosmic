# UX-H1 — Carry-over hardening: crash dumps, ground-control dry run, scale profile, Live chip, dead-3D tidy

**Gate:** G3 · **Wave:** 3 (worktree; alongside UX-D1 and UX-05) · **Runs:** worktree `ux/h1` at `build\_lanes\ux-h1` ·
**Base:** `main` after UX-04 → UX-G0 → UX-D2 have landed (post-wave-2); lands last in wave 3 (D1 → 05 → H1) ·
**Depends on:** UX-03 (fixture marker), UX-G0 (§6 world contract), UX-D2 (API matrix + checker) · **Acceptance:** H1-A,
H1-B, H1-C, H1-D, H1-E, H1-F, B06 per tidy commit · **Model:** Opus 5.5 · **Effort:** high · **Status:** not started

The App Platform `FOLLOW-UP.md` items 5 (the never-written AP-H1: ground-control dry run, scale profile, the Live chip
after New Project, crash dumps for the unreproduced silent editor exit) and 3 (the dead-3D tidy remainder), carried into
this packet as H1-A…H1-D plus one B06-clean commit per tidy item. The soaks (item 4) are postponed to
`docs/plans/TESTING-PLAN.md` (D-SOAKS). Added 2026-09-25 (D-TOOLCHAIN): Starforge's compiler check — one clear message
with the winget one-liner when no MSVC toolset is installed, instead of a raw build failure (H1-E) — and the AP04
acceptance fixture's cmake path via vswhere (H1-F). No `01-Contracts.md` §10 row owns the files these need; they are assigned here
(wave 3, no concurrent lane touches the Starforge sources they need) and the §10 rows are an orchestrator TODO in
`RESUME.md`.

## Copy-paste prompt

~~~text
Execute only UX-H1 from the Cosmic UX & Shipping packet (docs/plans/ux-shipping-2026-09-24/). Read ONLY:
work-orders/README.md (all); 00-Start-Here.md decision D-TOOLCHAIN; 01-Contracts.md §6 (what the world contract keeps),
§9 + your §10 row; 03-Acceptance-Catalog.md rows H1-A..H1-F, B06, DG01, DG03; ../app-platform-2026-09-18/work-orders/
FOLLOW-UP.md items 3 and 5;
../app-platform-2026-09-18/evidence/AP-Q1/release-report.md §3 step 3 ("Pending"); ../app-platform-2026-09-18/evidence/
AP-05/report.md "Now-dead 3D-only surface" (:410-); tests/acceptance/fixtures/Verify-AP05Purge.ps1 (header :1-40).

Lane: from C:\dev\Cosmic  git worktree add build\_lanes\ux-h1 -b ux/h1 <post-wave-2 main SHA> ; work only there with
$env:COSMIC_SDK = that path. UX-D1 (docs/guides, docs/guide -> docs/developer, GuideWalkthroughSelfTest.cpp, README.md)
and UX-05 (CMakePresets.json, tools/, ci.yml consumer job, SF_Telem README) run concurrently: never touch their files
(one exception, at landing only, after UX-D1 is on main: the guide-00 picture of item 6).
Order: H1-D, H1-C, H1-E (item 6), H1-F (item 7), H1-A, H1-B, then the tidy. KI rule: a defect goes into docs/plans/2d-stability-2026-09-16/contracts/
known-issues.md under "the next free number" in work-orders/README.md (never hardcoded) BEFORE its fix, with
failing-before / passing-after on an isolated patch. Anchors below are from 0c2edd8: re-check first (README rule 2).

1. H1-D crash dumps (§9). Cosmic/src/utils/CrashDump.{h,cpp}: CrashDump::Install(const std::string& dumpDir) sets an
   unhandled-exception filter that writes MiniDumpWriteDump(MiniDumpWithIndirectlyReferencedMemory) to
   <dumpDir>/<app>-<yyyymmdd-hhmmss>.dmp, logs the path, then lets the process die with its original code; plus
   CrashDump::WriteNow(path) for tests. Link dbghelp by #pragma comment(lib, "dbghelp.lib") (Cosmic/CMakeLists.txt stays
   untouched); a new Cosmic/src .cpp needs a re-configure. Install in Runtime/Main.cpp right after the identity decision
   (:100-101) with FileSystem::Resolve("user://logs"): Main.cpp serves both hosts (Runtime/CMakeLists.txt:8 CosmicApp,
   :50 StarforgeEditor). Add the docs/reference/README.md coverage row for utils/CrashDump.h (the audit fails without it).
   Proof: a CosmicTests case (WriteNow -> file starts "MDMP"); an env-gated deliberate fault (COSMIC_CRASHDUMP_SELFTEST=
   <seconds>; the least invasive seam, named in the report) in Starforge.exe and in CosmicApp.exe --project <a real
   project>, each child in a job object: exit 0xC0000005, exactly one new .dmp in that host's user://logs, the log line
   naming it, and cdb/dumpchk reading it if a debugger is installed (else say so). The silent exit (GUIDE report §3
   item 6) stays unreproduced; this makes the next one diagnosable.
2. H1-C Live chip (GUIDE report §3 item 5; StarforgeApp::LiveChipText StarforgeAppPlatform.cpp:540-548, DrawLiveChip
   :551-560 show "Live" whenever auto-build is on). KI first (UX: "Live" on a freshly scaffolded, never-built app). Fix:
   while the open project has no module built this session the chip reads "Not built - Ctrl+B", tooltip: the watcher only
   rebuilds on edits after open; no automatic build after New Project (it would race the guide driver's own Ctrl+B).
   Failing-before/after: a step in AP03AuthoringSelfTest's E01 reading LiveChipText after NewProjectAt and after
   BuildScripts; ap03-editor manifest. Tell the orchestrator which guide images (UX-D1) show "Live" on an unbuilt app.
3. H1-A ground-control dry run (FOLLOW-UP 5a). New fixture tests/UXH1GroundControlFixture.cpp: a real CS_MODULE with a
   CS_SERVICE (scripting/AppService.h) owning a SerialLink (serial/SerialLink.h:48) over FakeSerialTransport
   (tests/FakeSerialTransport.h:34) fed a deterministic 8-channel frame stream at 50 Hz and publishing to the DataBus; a
   canvas built through the public API binds UiPlot (UiComponents.h:300-313), UiGauge, UiValueText to the channels;
   CS_TEST_FIXTURE() (UX-03's marker); one fixture block in tests/CMakeLists.txt in the :177-254 pattern (L4). Host:
   CosmicApp.exe --project UXH1GroundControlFixture, Release, 60 min, in a job object. The fixture writes per-frame time,
   Renderer2D::GetStats().LineCount (Renderer2D.h:178-201) and bus counters to a result JSON. Oracles: after a 60 s warmup
   no frame > 100 ms; memory plateau by the WO-02 method (../2d-stability-2026-09-16/evidence/WO-02/runtime-baselines.txt
   §(e): 5 s samples of private bytes, working set, handles, threads; trend over the last window); plot segments <= 512
   per channel (UiSystem.cpp:949-951 decimates to 513 points) from LineCount minus the count with the plots empty;
   samples received = sent. Manifest ux-h1-ground.manifest.json: a 2-min pr/nightly case + the 60-min release case.
4. H1-B scale profile (5b): the same fixture with 200 channels at 50 Hz and 6 UiPlots x 4 channels at full WindowSeconds,
   5 min per config (Debug, Release): p50/p95/p99/max frame time, private bytes, bus memory. A measurement: PASS = ran and
   recorded; a Release frame > 100 ms is a finding for Kaden; a crash or hang is a KI. Manifest ux-h1-scale.manifest.json.
5. Tidy (FOLLOW-UP 3), one commit per item in this order, each only when a grep of Cosmic/src, Projects, Runtime, tests
   finds no consumer outside the item:
   (1) PhysicsWorld::DebugDraw (PhysicsWorld.h:138) + IPhysicsBackend::DebugDraw (PhysicsBackend.h:134) and the
       JoltBackend/NullBackend/test overrides - unless §6 names it (IPhysicsBackend is the world-mode physics seam);
   (2) PostProcessStack 3D effects (SSAO :96-99, fog :124-125, underwater, lens flare, heat haze), their host
       assignments, and shaders nothing else loads;
   (3) SceneRenderer 3D settings: ScenePass ShadowDepth/Reflection/TopDownDepth (SceneRenderer.h:79; Main keeps value 2),
       Skybox/IBL/Shadows/WaterReflections (:106-108), Underwater (:121-127), lens-flare/outline, every host assignment;
   (4) Mesh (Mesh.h:128; MeshVertex/SkinVertex/Submesh/MeshData :56-98), Material's 3D hints (Material.h:88-127) and the
       .cmat pipeline (MaterialAsset.h, AssetLibrary.cpp:101-156 + PBR.glsl/PBRSkinned.glsl, TypeRegistry.cpp:333,
       MaterialEditorPanel.{h,cpp}, AssetTypes.cpp:32/95/113, ContentBrowserPanel.cpp:61). Material itself STAYS
       (SpriteRendererComponent::ActiveMaterial, Components.h:153); C05's opaque MeshRenderer-block preservation stays;
   (5) last, only with a reachability proof: OrbitCameraController (:51), FlyCameraController (:44), EditorCameraRig's
       Orbit/Fly and their viewport UI (ViewportController.cpp:863-912). The rig drives the view only when m_Mode2D is
       false (StarforgeApp.cpp:1067-1084; :890/:907 merely keep its pose) and shipping code never sets it false
       (StarforgeApp.h:263 default true; only Ki1SnapChipSelfTest.cpp:141, L05EditorSelfTest.cpp:151 assign it, to
       true); if anything else makes the rig visible, keep it and report.
   KEEP: PerspectiveCamera (tests/render/render_2d.cpp:514, render_wo08_rtt.cpp:188 render goldens with it; D-WORLD's
   perspective layering) and CameraComponent's Perspective projection and default (a serialized default = format change).
   Per commit: extend B06 through tests/acceptance/fixtures/ux-h1-deleted-paths.txt (Verify-AP05Purge.ps1
   -DeletedPathsFile) in ux-h1-b06.manifest.json; reconfigure; Release then Debug 0-warn; CosmicTests both; ux-h1-b06;
   CosmicRenderTests for (2)-(5) with all 15 goldens byte-identical (hash before/after; a changed golden stops the item,
   never regenerate); remove docs/reference/README.md rows of deleted headers (coverage 0) and API-MATRIX.md rows naming
   deleted symbols (check_api_matrix 0); a doc link your deletion breaks loses the link, keeps the text; list touched
   docs/developer files for UX-D3 instead of editing their prose.
6. H1-E compiler check (D-TOOLCHAIN: MSVC is the only supported toolchain). KI first: "a PC without MSVC gets a raw build
   failure - nothing checks for a toolset; FindCMake falls back to a bare `cmake` on PATH" (BuildRunner.cpp:20-45, the
   fallback :43-44). Probe beside FindCMake in BuildRunner.{h,cpp} (no new TU, so Projects/Starforge/CMakeLists.txt stays
   untouched): MSVC present = vswhere (%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe -latest -products *
   -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath - the package.bat:59 query;
   spawned with no console window) returns an installation AND a cmake exists (FindCMake's VS-bundled hit, or cmake
   resolvable on PATH); no vswhere.exe = no toolset. Cached per session; "Check again" re-probes. Seam:
   COSMIC_SIMULATE_NO_TOOLSET=1, read ONLY inside the probe, makes vswhere's answer and FindCMake's VS search come back
   empty; everything after it is the production path (README rule 5). Wire it wherever a build would start:
   StarforgeApp::BuildScripts (StarforgeApp.cpp:456-478, before m_Builder.Start), BeginPackage's cmake steps (:3028-3048 at
   60a0536; UX-04 changed that region - re-check), New Project (after the scaffold; H1-C: no automatic build) and the
   homescreen's first frame (DrawHomescreen; a non-blocking notice). On "missing": no BuildRunner start, one Console line
   naming the probe result, and one message titled "C++ compiler not found" showing the two winget one-liners of the
   catalog's H1-E row VERBATIM (Build Tools first - the smaller install - then Community) with Copy buttons, the pointer
   https://github.com/kdadabhoy/Cosmic/blob/main/docs/guides/00-get-starforge.md plus README-SDK.md (the zip has no docs/),
   Check again and Close. Verify, don't trust: that --includeRecommended on the VCTools workload installs the VS-bundled
   CMake FindCMake looks for; if not, add --add Microsoft.VisualStudio.Component.VC.CMake.Project to BOTH strings and
   record a contract deviation (guide 00 and the catalog follow your source). Proof: AP03AuthoringSelfTest gains a second
   plan selected by COSMIC_AP03_PLAN=toolchain (E01-E08 unchanged when unset; window sized 1600x900 as
   GuideWalkthroughSelfTest.cpp does), run twice by a new tests/acceptance/fixtures/Run-UXH1Toolchain.ps1 +
   ux-h1-toolchain.manifest.json per the catalog row: (a) with the override - the message on the homescreen; New Project,
   Ctrl+B and Package start no build (BuildRunner Idle, no cmake.exe child, nothing new under dist/), one Console line
   each; the strings byte-equal; a screenshot saved (guide 00's source shot); (b) without it - probe paths recorded, no
   message, a build succeeds. Failing-before: plan (a) on a binary without the check (a build starts, no message) - kept.
7. H1-F: tests/acceptance/fixtures/Run-AP04Sample.ps1:21's default -CMake (a hard-coded ...\18\Community\... path) resolves
   through the same vswhere query + Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe, then PATH (its :30
   fallback). Oracle: git grep -n -F "18\Community" -- "*.ps1" "*.psm1" "*.bat" "*.cpp" "*.h" "*.cmake" "*CMakeLists.txt"
   "*.yml" "*.json" prints nothing (Markdown mentions are UX-D1's); ap04-sample re-run green with no -CMake argument.

Acceptance: H1-A..H1-F and B06 as above; the manifests ux-h1-ground, ux-h1-scale, ux-h1-b06, ux-h1-toolchain (+ the H1-D
wrapper's) and the ap04-sample re-run.
Evidence: evidence/UX-H1/report.md (WO-10 layout): dump paths/sizes + the analysis excerpt, frame-time and memory
tables, the tidy table (item, files, grep proof, B06, golden hashes, SHA), the H1-E probe results (a)/(b) + failing-before
and the verified winget strings, KI entries; §11 rows for crash dumps and the compiler check.
Land (L2) after UX-D1 and UX-05: git rebase main; Debug + Release 0-warn; CosmicTests both; your manifests +
ap03-editor + ap05-purge + ap04-sample; both audits + check_docs_links.ps1 + check_api_matrix.ps1. Then, only now that
UX-D1 is on main, the guide-00 picture: the H1-E (a) screenshot annotated by tools/guide_shots.py (a rect in
annotations.json around the command field) into docs/guides/images/00-get-starforge/, one docs/guides/images/manifest.json
entry (method "driver", source_shot, annotated_by), its sha256 appended to evidence/UX-D1/shots-sha256.txt, and in the
"Install the C++ compiler" step one sentence quoting the title (copied from your source) that replaces UX-D1's named
exception; evidence/UX-D1/check_guide_images.py (DG01) and check_guide00_toolchain.py (DG03) exit 0. If UX-D1 is not on
main, skip it and tell the orchestrator. Commit on ux/h1 as kdadabhoy <kdadabhoy28@gmail.com>, no Co-Authored-By / AI
trailer; never push. Report <= 40 lines: H1 verdicts and numbers (H1-E/H1-F included), the tidy items removed / kept
(why), KIs, deviations, SHAs, git status --short.
~~~

## Files to read first (and nothing else)

The eight in the prompt: README; D-TOOLCHAIN; §6, §9 + §10 row; H1/B06/DG01/DG03 rows; FOLLOW-UP items 3 + 5; AP-Q1 §3
step 3; AP-05 dead list; `Verify-AP05Purge.ps1` header.

## Owns / May touch

- **Owns:** `Cosmic/src/utils/CrashDump.{h,cpp}`; the install lines in `Runtime/Main.cpp`; `tests/UXH1GroundControlFixture.cpp`;
  the H1 fixtures + `ux-h1-*.json` manifests + `ux-h1-deleted-paths.txt`; the tidy removals (engine, editor, tests)
  listed above, each in its own commit; `evidence/UX-H1/**`. D-TOOLCHAIN (not yet in `01-Contracts.md` §10 — see
  `RESUME.md`): the toolset probe in `Projects/Starforge/src/BuildRunner.{h,cpp}`; `tests/acceptance/fixtures/Run-UXH1Toolchain.ps1`
  (new); the cmake-discovery lines of `tests/acceptance/fixtures/Run-AP04Sample.ps1`.
- **May touch:** `StarforgeAppPlatform.cpp` (`LiveChipText`/`DrawLiveChip`); `AP03AuthoringSelfTest.cpp` (one step + the
  `toolchain` plan); `StarforgeApp.cpp` (the H1-E check calls in `BuildScripts`, the `BeginPackage` cmake steps, the New
  Project path and a homescreen notice in `DrawHomescreen`, plus matching `StarforgeApp.h` declarations);
  `tests/CMakeLists.txt` (one fixture block, L4); `docs/reference/README.md` + `API-MATRIX.md` (rows only); at landing
  only, after UX-D1 is on `main`: `docs/guides/00-get-starforge.md` (the compiler-check sentence + picture in the
  "Install the C++ compiler" step), one PNG under `docs/guides/images/00-get-starforge/`, one
  `docs/guides/images/manifest.json` entry and one line of `evidence/UX-D1/shots-sha256.txt`; the KI register;
  `01-Contracts.md` §11 rows.

## Scope

- **In:** crash dumps in both hosts, the 60-min dry run, the scale profile, the Live chip, the compiler check + its guide-00
  picture, the AP04 fixture's cmake discovery, tidy items (1)–(5) by evidence.
- **Out:** the soaks (postponed to `docs/plans/TESTING-PLAN.md`, D-SOAKS); crash dumps inside packaged apps beyond what
  `Main.cpp` gives; developer-doc prose (UX-D3); guide text beyond the one compiler-check sentence (UX-D1); any toolchain
  other than MSVC (`docs/plans/TOOLCHAIN-PLAN.md`).

## Deliverables

`CrashDump`, the fixture + four manifests, the chip fix + KI, the toolset probe + message + KI, `Run-AP04Sample.ps1` via
vswhere, the guide-00 picture, one commit per removed tidy item, `evidence/UX-H1/report.md`.

## Done when (DoD)

A deliberate fault leaves a readable dump in both hosts; H1-A oracles pass over 60 min; H1-B numbers recorded both
configs; the chip KI closed; H1-E (a) and (b) pass and the compiler-check KI is closed; H1-F's grep prints nothing; every
tidy commit B06-clean, 0-warn, goldens byte-identical; kept items justified.

## Rollback

Every item is its own commit and reverts alone; tidy commits revert in reverse order.
