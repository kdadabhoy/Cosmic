# UX & Shipping — design contracts

Status: fixed 2026-09-24 at `0c2edd8`. Names, keys, paths, signatures and rules the parallel lanes must
agree on. A lane reads only the sections its prompt names. A needed deviation is recorded in the lane's
report under "Contract deviations" and applied to this file by the integrator, never silently to another
lane's files. Anchors were true at `0c2edd8`; re-check before editing.

## §1 Flow editor and the Editors host (UX-01)

- **Trigger kinds.** The transition inspector (`Projects/Starforge/src/editors/FlowEditor.cpp:726-844`)
  replaces the lone `when (guard only)` radio (`:757-765`) with a **trigger-kind selector** with four
  values: **Event** (`On` = a signal name; the combo lists the scene's `UiButton`/`UiSlider`/`UiToggle`
  signals + existing `On` values), **Key** (`On` = `key:<Name>`, the key picker), **Timer** (`On` =
  `timer:<seconds>`), **When** (`On` = `"when"`). Switching *to* When keeps an existing guard or turns
  "Guard (if)" on with the previous guard restored if one existed, otherwise a channel guard skeleton;
  switching *away* restores the last non-when `On` value (remembered per transition for the document's
  lifetime; default `"signal"`) and removes a guard only if it is the auto-added empty one. `FlowAsset::
  Validate` reports **an empty guard on any transition** (new) in addition to `when` without a guard
  (existing, `Cosmic/src/scene/FlowMachine.cpp:388-389`). **On-disk `.cflow` bytes are unchanged** for
  every existing file: the v1/v2 format, `"on"` strings and `"if"` shapes (`FlowMachine.cpp:158-173`,
  `:278-295`) are not touched.
- **Link routing.** Output pins sit on the node's right edge, input pins on its left edge
  (`ed::PushStyleVar(StyleVar_PivotAlignment, {1,0.5})` / `{0,0.5}`; the `->` column right-aligned,
  `FlowEditor.cpp:365-369`). A pure helper
  `NodeCanvas::RouteLink(ImVec2 start, ImVec2 end, const ImRect& startNode, const ImRect& endNode, float strength, ImVec2& cp0, ImVec2& cp1)`
  (declared in `Projects/Starforge/src/widgets/NodeCanvas.h`, `ImVec2`/`ImRect` only, no ImGui state)
  computes the bezier control points: forward links as today; **backward links** (`dot(end − start,
  startDir) < 0`) and **self-loops** route below the union of both node rects so the curve never crosses
  the source rect. The vendored `Link::GetCurve`
  (`Cosmic/dependencies/imgui-node-editor/imgui_node_editor.cpp:955-982`) is patched to call the same
  logic; the patch is minimal and recorded in the existing
  `Cosmic/dependencies/imgui-node-editor/VENDOR-NOTES.md` under a new "Local patches" section (upstream
  commit, files, hunks, why). Input pins draw an arrowhead (`StyleVar_PinArrowSize` /
  `PinArrowWidth`). The helper is compiled into CosmicTests the way `UiRectGizmo.cpp` is
  (`tests/CMakeLists.txt:138-143`).
- **Editors host contract** (`Projects/Starforge/src/editors/AssetEditorHost.{h,cpp}`): `*open == false`
  hides the whole window (`AssetEditorHost.h:49-51` already says so; `StarforgeApp.cpp:1538-1542` must obey);
  the host **auto-shows only when `Open()` adds or focuses a document**; `CloseAll()` runs in
  `StarforgeApp::CloseProject`; first use sizes the window `SetNextWindowSize({1100, 680},
  ImGuiCond_FirstUseEver)` and `Open()` calls `SetNextWindowFocus()` for the next frame; every built-in
  layout preset docks `"Editors"` (`LayoutPresets.cpp:68-72, 113-139`; default port `DockPort::Center`,
  tabbed with the Viewport — a lane may pick a new "Flow" built-in instead and record it); `DockWindow` is
  never called at open time. Document tabs carry a **stable id** (a per-document counter assigned at
  `Open()`, not the index). A document created by `CreateDefaultAsset` (`AssetTypes.cpp:115-123`) is **not
  dirty on its first frame** (the auto-grid placement is applied before the first dirty comparison). The
  empty-state text (`AssetEditorHost.cpp:64-66`) names flow and story documents only. `FlowEditor` calls
  `m_Canvas.CenterOnContent()` once after `m_PlaceNodes` has laid the nodes out.

## §2 Viewport, Hierarchy, Inspector, Screens, Preferences (UX-02)

- **One gizmo per selection.** `ViewportController::DrawGizmo` (`ViewportController.cpp:572-629`) does not
  draw the transform gizmo for an entity that has `RectTransformComponent` or `CanvasComponent`; the UI rect
  gizmo (`UiRectGizmo.cpp`) owns those. The rect gizmo's **Move surface always captures the selected
  element** (the centre square, or the whole selected rect when the pointer is inside it), not only when the
  element is topmost (`UiRectGizmo.cpp:262-273`). In 2D mode `Gizmo::Manipulate` (`Cosmic/src/graphics/
  Gizmo.cpp:84-92`) writes `Transform.Rotation.z` (angle from the rotation matrix) and leaves
  `UseQuatRotation` false, so Rotate is visible on sprites (`Scene.cpp:716` draws `Rotation.z`). When the
  current selection is a world entity (`SpriteRenderer`/`Transform` without a canvas ancestor), the viewport
  press test tries the selected entity's sprite bounds **before** the UI hit-test
  (`ViewportController.cpp:255-275`), so a sprite under an opaque `UiImage` can still be dragged. Toolbar
  chips that do not apply to the selection are drawn disabled with the tooltip `UI elements: use the rect
  gizmo`.
- **Active semantics for UI.** `UiSystem::VisitUi` (`Cosmic/src/scene/ui/UiSystem.cpp:310-401`) skips a
  node **and its subtree** when its `TagComponent::Active` is false (canvas roots keep the existing check at
  `:415`), so `Render`, `Update`, `HitTest` and `CollectHostedPanels` all ignore inactive elements. No new
  field; `Components.h:90-94` is the contract being honoured.
- **What a button does.** The Inspector's `Signal` row for `UiButton`, `UiSlider`, `UiToggle`
  (`panels/InspectorPanel.cpp:569-591`) and the viewport `Open logic source` menu
  (`StarforgeAppPlatform.cpp:315-349`) list, in addition to the `src/**` hits, every transition of the
  project's startup flow whose `On` equals the signal, as a read-only line
  `Flow: <State> —<signal>→ <Target>` (`@quit`/`@pop`/`push` shown as written) with **Open in flow editor**
  (opens the flow document and selects that transition). Implemented as a new hit kind in
  `SourceLocator::ForSignal` (`SourceLocator.cpp:210-224`; kind `Flow`, path = the `.cflow`, line = 0,
  state/target in the description) so E08's existing cases keep passing and UX-D1's guide can show it.
- **Scenes list.** The Screens panel (`panels/ScreensPanel.cpp`) gains a **Scenes** section under the flow
  states: every `project://scenes/**/*.cscene` (recursive, sorted), the flow's start scene marked, the open
  scene highlighted, double-click → `host.OpenScene(vfs)`, right-click → Reveal in Explorer. File ▸ Open
  Scene (`StarforgeApp.cpp:2015-2031`) becomes recursive with the same list.
- **Preferences.** `EditorPrefs` (`Projects/Starforge/src/EditorPrefs.h`) gains `autosave_enabled` (bool,
  default `true`) and `prompt_unsaved` (bool, default `true`) beside the existing autosave interval
  (`EditorPrefs.h:35`, a `float` read by `GetFloat` at `:213`; keep its key, treat it as whole minutes, accept old
  float values; default 5; range 1–60), persisted in `user://starforge/editor.toml`.
  **Edit ▸ Preferences…** is a modal with those three controls and the autosave folder path (`user://
  starforge/autosave/`) with a Reveal button. `StarforgeApp::Autosave` (`StarforgeApp.cpp:1296-1314`) obeys
  the flag; the status bar shows `autosaved HH:MM` after each autosave. When `Dirty` and `prompt_unsaved`,
  `OpenScene`, `NewScene`, `OpenProject` and `CloseProject` show **Save / Discard / Cancel** (the same shape as
  `AssetEditorHost.cpp:132-179`); Cancel aborts the action. The window's close button cannot prompt without an
  engine change (`Application.cpp:471, 684-688` handles `WindowCloseEvent` before any layer; the title-bar ✕
  calls `Application::Close`, `WorkspaceLayer.cpp:294`): on that path the editor writes an autosave copy in
  `OnDetach` and the gap is recorded for a later engine WO.

## §3 Launcher and samples (UX-03)

- **Fixture marker.** `Cosmic/src/scripting/ModuleMacros.h` gains
  `#define CS_TEST_FIXTURE() extern "C" __declspec(dllexport) int CosmicTestFixture() { return 1; }`.
  Every fixture in `tests/*Fixture.cpp` (nine files; the DLL targets are at `tests/CMakeLists.txt:176-254` —
  `WO07NoExport` has no entry point and stays as is) invokes it.
  `LauncherLayer::ScanForProjects` (`Cosmic/src/layers/LauncherLayer.cpp:582-634`) skips a DLL whose
  `GetProcAddress(hMod, "CosmicTestFixture")` is non-null, logging one debug line per skipped DLL. No name
  matching. Fixture output directories and the tests that load them are untouched.
- **Samples.** `ListSamples()` (`StarforgeAppPlatform.cpp:379-390`) returns `{name, kind, description,
  sourcePath, featured}` from two sources: `assets/projects/Starforge/templates/samples/*` and
  `<SdkDir()>/Projects/*` that contain a `project.cproj` (never `Starforge`); `kind` from the manifest;
  `description` from the first non-title line of the sample's `README.md` (ignoring a `@PROJECT_NAME@` line).
  `DrawSampleButtons()` groups them **App samples** (kind `app`) then **Game samples** (kind `game`), with
  **PendulumLab first and marked featured**; a project without a `kind` (AnalysisSample) is listed last under
  **Other samples** and is not part of LH02's build/play loop. `OpenSample` keeps copying into `Documents/Starforge Projects/
  <Name>` on first use for both sources (the SDK's copy is never edited in place). The one-time welcome
  popup (`StarforgeApp.cpp:2439-2477`, flag `playground_offered`) offers **PendulumLab**; the New Project
  dialog preselects **App**. E06's oracle is extended, not replaced.

## §4 SDK release (UX-04)

- **Zip** `Cosmic-SDK-<ver>-win64.zip`, `<ver>` = `COSMIC_VERSION_STRING` (`Cosmic/src/core/Version.h:20`).
  Tree (checkout-mirror; nothing else):
  ```
  sdk.toml                      version, sha, date, layout = "checkout-mirror"
  VERSION                       the version string
  README-SDK.md                 unzip, set nothing, run build\Runtime\Release\Starforge.exe; the consumer CMake recipe
  Cosmic/src/**/*.h
  Cosmic/dependencies/imgui/    headers + imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp
  Cosmic/dependencies/implot/   headers + implot.cpp implot_items.cpp
  Cosmic/dependencies/glm/      headers
  Cosmic/dependencies/entt/src/ headers
  Cosmic/dependencies/spdlog/include/  headers
  build/Runtime/Release/        everything the Release runtime dir holds (Cosmic.dll Cosmic.lib CosmicApp.exe Starforge.exe
                                Starforge.dll assets/** branding/** and the redistributable CRT DLLs from
                                CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS) MINUS the exclusion list below
  build/Runtime/Debug/          the same for Debug
  Projects/PendulumLab/**       the featured sample's source tree (no build/), so the homescreen lists it (D-SAMPLES)
  installer/licenses/**  installer/Stage-AppPackage.ps1  installer/AppSetup.iss
  <every LICENSE file installer/licenses/MANIFEST.txt names, at its manifest path>   so File > Package works from the zip
  ```
  Exclusions (never in the zip): PDBs, `.exp`, `.lib` other than `Cosmic.lib`, `CosmicTests*`, `CosmicRenderTests*`,
  every `*Fixture*.dll`, `WO07NoExport.dll`, `SF_Telem.dll` and `assets/projects/SF_Telem/**`, `assets/cache/**`,
  sample build output, `CMakeCache.txt` / CMake files, `starforge/` user data. The runtime dirs are mirrored
  minus that list (not an allow-list), so a new engine asset never goes missing from the zip. `Stage-Sdk.ps1`
  verifies afterwards that every source path `installer/licenses/MANIFEST.txt` names exists in the staged tree.
  Staged by `installer/Stage-Sdk.ps1` (PowerShell 5.1, ASCII; `-SdkRoot`, `-OutDir`, `-Configs`, `-ListOut`,
  `-Zip`), called by `package.bat sdk` and by the `sdk` job of `.github/workflows/release.yml`
  (`workflow_dispatch` + `push: tags: cosmic-sdk-v*`; uploads the zip and the installer as artifacts and,
  for a tag, attaches both to the GitHub Release for that tag). Kaden triggers releases by pushing the tag.
- **Installer** `Starforge-Setup-<ver>.exe` from `installer/StarforgeSetup.iss` (derived from
  `AppSetup.iss`): installs the zip tree to `{autopf}\Cosmic SDK` (per-user, `PrivilegesRequired=lowest`),
  Start-menu + optional desktop shortcut to `build\Runtime\Release\Starforge.exe` with `WorkingDir` = that
  exe's folder (so `StarforgeApp::SdkDir()`'s "three up" fallback, `StarforgeApp.cpp:437-447`, resolves the
  install root), an optional task that writes `HKCU\Environment\COSMIC_SDK` = the install root. Because the
  install folder is per-user and writable, the editor's `user://` resolves to the exe folder
  (`FileSystem.cpp:100-102`), so its prefs, layouts, autosaves and project registry live under
  `{app}\build\Runtime\Release\starforge\`; uninstall removes them with the folder and the installer's final
  page says so. Inno Setup 6 is not installed on the dev machine: locally the installer step is
  `ENVIRONMENT_BLOCKED` until Kaden installs ISCC; the CI `sdk` job installs Inno Setup 6 itself
  (`choco install innosetup`) and builds it.
- `Cosmic/CMakeLists.txt`: `set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)` before GLFW's `add_subdirectory` (the
  leak documented in `docs/guide/building-and-shipping.md` "Which install() rules exist").
- **KI-63**: the `*.bak` rule in both `Projects/Starforge/src/Packager.cpp SkipContentEntry` (`:21-27`)
  and `installer/Stage-AppPackage.ps1 $skip` — both filters match top-level names only today and the `.bak`
  files sit in `scenes/`, so the rule is applied **recursively** on both sides; a `.bak` fixture in the K02
  comparison; KI closed.
- **Packaging an app repo (new KI, fixed here with KI-63):** the packager takes the project root as content
  and nothing skips `extern/`, `dist/`, `.gitmodules` or `build.bat`, so an app repo laid out per §5 would
  stage its whole `extern/Cosmic` SDK and write into `extern/Cosmic/dist`. Both skip lists gain `extern`,
  `dist`, `.gitmodules`, `build.bat` (recursive where it matters) and the editor packager writes `dist/`
  under the **project** root, not `<sdk>/dist` (`StarforgeApp::BeginPackage`, `StarforgeApp.cpp:2953-3060`,
  output dir `:2970`). UX-05 only verifies this.

## §5 Cosmic as an SDK — the consumer path (UX-05; no engine source change)

- `CMakePresets.json`: configure preset **`sdk`** (inherits `2d`; `COSMIC_BUILD_TESTS=OFF`,
  `COSMIC_SKIP_PROJECTS="AnalysisSample;PendulumLab;SF_Telem"`) and build presets `sdk-release`, `sdk-debug`.
  Result: `Cosmic`, `CosmicApp`, `Starforge` under `<checkout>/build/Runtime/<cfg>` — the §4 tree in place.
- `tools/Build-Sdk.ps1 [-Root <checkout>] [-Config Release|Debug|Both]` wraps the presets; it falls back to
  the newest installed Visual Studio generator when the preset's pinned one (`CMakePresets.json:12`,
  "Visual Studio 18 2026") is absent (GitHub runners), and it refuses to reconfigure a cache that was made
  by the dev configure (the skip list and tests-off values are sticky, root `CMakeLists.txt:161-170`)
  unless `-Force`. `installer/Stage-Sdk.ps1 -Build` calls it once it exists (UX-04 lands first and uses its
  own raw configure until then, marked for UX-05 to replace).
- `tools/New-AppRepo.ps1 -Name <App> -Dest <dir> [-CosmicTag <tag>] [-Sdk submodule|zip]` scaffolds an app
  repo: `extern/Cosmic` as a submodule pinned to `<tag>` (or a README instruction to unzip the SDK there),
  `CMakeLists.txt`, `src/`, `assets/`, `flows/`, `scenes/`, `project.cproj` copied from
  `Projects/Starforge/assets/templates/app` with `@PROJECT_NAME@` substituted, `build.bat` (sets
  `COSMIC_SDK=%~dp0extern\Cosmic`, runs `Build-Sdk.ps1`, then configures/builds the app with
  `-DCOSMIC_SDK_DIR=… -DGAME_OUTPUT_DIR=%~dp0build`), `.gitignore`, `README.md` (open the folder in
  `extern\Cosmic\build\Runtime\Release\Starforge.exe`, Ctrl+B, Play, File ▸ Package).
- `.github/workflows/ci.yml` job **`consumer`**: generates such a repo in a temp dir from the checkout
  (path pin in place of a real submodule clone), builds the SDK with the preset, builds the app both configs,
  packages it with `Stage-AppPackage.ps1 -SdkRoot <extern/Cosmic> -RuntimeDir <its build/Runtime/Release>`,
  compares the file list with K02's shape and runs the **packaged exe** (`CosmicApp.exe --project` cannot run
  an external App-template app: it looks for content under the runtime's `assets/projects/<name>`,
  `Application.cpp:778-786`); on a GPU-less CI runner the run step is `ENVIRONMENT_BLOCKED` and an
  export-table check of the DLL stands in.
- Dry run in `evidence/UX-05/`: SF_Telem's `src/ assets/ firmware/ docs/` in a temp git repo with Cosmic as a
  submodule at the lane's SHA → `build.bat` → run → package (K01), then the same repo against the UX-04 zip.
- `Projects/SF_Telem/README.md` banner (D-SPECIMEN). `Packager.cpp` is UX-04's (the app-repo packaging KI
  above); UX-05 verifies the fix in the dry run and reports, it does not edit the packager.

## §6 The 2D world contract (UX-G0) — reserve now, so the later world work is additive

1. **`ProjectManifest::Save` preserves unknown keys and tables** (`Projects/Starforge/src/ProjectManifest.h:
   52-73` rewrites from the known key set today and drops `capture_cursor`, which `Cosmic/src/layers/
   PlayerLayer.cpp:87` reads — a registered KI). `Save` **edits the file in place**: a known key is updated on
   its own line, unknown keys, tables and comments are preserved verbatim, a missing known key is appended; a
   load → save of an unchanged manifest is byte-identical (WM02). A second defect is fixed with it: File ▸
   Project Settings ▸ Save builds a fresh `ProjectManifest` (`StarforgeApp.cpp:3265-3272`), which rewrites
   `kind` as `game` and drops `startup_flow` / `pixel_art` — an app project loses its flow; it loads first,
   then saves (registered KI).
2. **Sprite sort key** (`Cosmic/src/scene/Scene.cpp:560-598`): ascending `ZOrder`, then `YSort ? −Position.y
   : Position.z`, then entity id. A future leading `SortingLayer` (int, default 0) sorts *before* `ZOrder`;
   default 0 reproduces today's order byte-for-byte.
3. **Camera conventions**: `CameraComponent` orthographic `OrthoSize` = half-height; the camera sits at +Z
   looking −Z; Near/Far are honoured as authored (`Components.h:397-420`); both hosts pick the first
   `Primary` camera (`PlayerLayer.cpp:343-351`, `StarforgeApp.cpp:1120-1163`). A perspective camera is a
   supported way to layer sprites by Z.
4. **"No RigidBody ⇒ the engine never writes your Transform."** Engine physics writes only entities in
   `ScenePhysics::m_Bodies` (`physics/ScenePhysics.cpp:195-227`); user-coded simulation drives transforms
   from `AppService::OnFixedUpdate` → `SystemScript::OnFixedUpdateAll` → `ScriptableEntity::OnFixedUpdate`,
   before `ScenePhysics::Step` (`PlayerLayer.cpp:324-337`). This sentence goes into the contracts, the
   guides and the design doc.
5. **`kind` is editor-only.** The engine never reads `project.cproj kind` (`PlayerLayer.cpp:68-90`); a future
   `world` value changes editor UX only.
6. **Physics-backend selection seam**: `PlayerLayer::OnAttach` initialises physics **after** services are
   instantiated (today `:114` before `:122`) so a service's `OnAttach` can `PhysicsBackendRegistry::
   SetDefault`; the editor's Play path already has that order (`StarforgeApp.cpp:683` then `:696`) and is
   left alone. Existing behaviour for projects without a service that
   touches physics is unchanged (WM02).
7. **Clear colour**: one constant for the 2D clear colour used by the player (`PlayerLayer.cpp:381,390`) and
   the editor viewport (`StarforgeApp.cpp:1181,1195`) — the player's value wins (the shipped app is the
   truth); editor 2D **Play** runs `ApplyEnvironment` (`StarforgeApp.cpp:1197` skips it in both 2D edit and
   Play today; edit mode stays as is) so bloom/vignette preview matches the shipped player. Goldens are rendered by the render tests, not the editor, and must stay byte-identical.
8. **Scene version**: `SceneSerializer::LoadFromString` warns once when `cosmic_scene > 1`
   (`SceneSerializer.cpp:479-503`); the writer keeps `1`.
9. **Reserved component names**, never reused for something else: `Particle2D`, `ParallaxLayer`,
   `Camera2DFollow`, `SortingLayer` (field). `ParticleEmitter` blocks from `engine-3d` scenes keep
   round-tripping opaquely.
10. Registered KIs (fix optional in UX-G0, mandatory before the world packet): float
    `SpriteAnimationComponent::Elapsed` (`Components.h:232`, `Scene.cpp:537`; a looping clip stalls after
    ~3 days); one `CreateRef<SubTexture2D>` per textured sprite per frame (`Scene.cpp:729`); the editor
    `Camera2DController` zoom cap of 10 000 units half-height (`Camera2DController.h:118-119`).

## §7 Documentation tiers (UX-D1, UX-D3)

- Tiers after this campaign: **Guides** (`docs/guides/`, user, pictures) · **Developer** (`docs/developer/`,
  the former `docs/guide/`, unchanged content and format) · Reference · Systems · Plans. `docs/README.md` and
  the root README lead with Guides.
- **Rename map**: `docs/guide/<x>.md` → `docs/developer/<x>.md` for every chapter except
  `pendulumlab-walkthrough.md` → `docs/guides/01-pendulumlab-walkthrough.md`; `docs/guide/images/pendulumlab/`
  → `docs/guides/images/pendulumlab/`; every inbound link (README.md, `docs/**`, `tests/**`,
  `Projects/*/README.md`, `docs/reference/README.md` manifest rows, `tests/check_docs_coverage.ps1` if it
  names the tier) rewritten by script; `tests/check_docs_links.ps1` strict 0 afterwards.
- **Guide format** (mandatory): `# <Title> — Guide`; header block **What you will do** / **You will need** /
  **Time** / **Where this lives in the code** (link to the developer chapter); numbered steps, each naming
  the menu, panel, button or field exactly as the editor labels it, one picture per step with the control
  boxed in red; `## What you should see`; `## Troubleshooting` (symptom first); `## Where this lives in the
  code`. Guides: `00-get-starforge`, `01-pendulumlab-walkthrough`, `02-your-code-in-the-engine`,
  `03-services-and-the-databus`, `04-screens-and-the-flow`, `05-arranging-a-screen`, `06-package-and-ship`,
  `07-editor-preferences`; `docs/guides/README.md` is the index.
- **Image rule**: pictures are captures of the real editor (the lane's Release build; the driver sizes the
  window to 1600x900 so driver images are 1440x810 after `guide_shots.py`, computer-use captures come from the
  2560x1440 display at 100 %) from the capture driver (`Projects/Starforge/src/GuideWalkthroughSelfTest.cpp` screenshot steps →
  `tests/acceptance/fixtures/Run-GuideWalkthrough.ps1` / a sibling `Run-GuideShots.ps1` →
  `tools/guide_shots.py`) or computer-use screenshots annotated through `guide_shots.py` with
  `annotations.json`. `docs/guides/images/manifest.json`: one entry per image `{image, guide, step,
  method: "driver"|"computer-use", source_shot, annotated_by}`. ≤ 600 KB, max width 1440.

## §8 API matrix (UX-D2)

`docs/reference/API-MATRIX.md`: one table per feature area (Application & layers · Time & clocks · Events &
input · 2D rendering · Sprites, tilemaps, lights · UI widgets · DataBus & services · Flow & story · Scenes,
ECS, serialization · Scripting proxies · Physics · Audio · Assets, VFS, Config · Serial & telemetry · Jobs ·
Math & sim toolkit · Editor hosting); columns `| Call | Header | What it does | Use it when | Example |
Reference | Used by |`; **Call** is a backticked `Class::Method`, free function or macro copied from the
header; **Header** the path under `Cosmic/src/` (one exception: the test seam `tests/FakeSerialTransport.h` is
listed with its `tests/` path and the checker accepts it); **Example** a compiling snippet (≤ 6 lines) or a
`path:line` into `Projects/PendulumLab`, `Projects/SF_Telem`, `Projects/AnalysisSample` or the templates;
**Reference** the entry link or `—` for a skeleton chapter. `tests/check_api_matrix.ps1` (PowerShell 5.1,
ASCII): for every row the header exists and contains the call's identifier, every `path:line` example
exists; exit 1 on any miss; one CI step after the link audit. `docs/reference/app-services.md`: entries for
`data/DataBus.h`, `scripting/AppService.h`, `scripting/ServiceHost.h`, `scene/FlowKeyBridge.h` and the
`FlowMachine` additions, signatures verbatim; the manifest rows re-pointed.

## §9 Hardening carry-over (UX-H1)

`Cosmic/src/utils/CrashDump.{h,cpp}`: `CrashDump::Install(const std::string& dumpDir)` sets an unhandled-
exception filter that writes a `MiniDumpWriteDump` (`MiniDumpWithIndirectlyReferencedMemory`) to
`user://logs/<app>-<timestamp>.dmp` and logs the path; installed by both hosts after `FileSystem` is up.
The ground-control dry run, the scale profile and the auto-build-after-scaffold UX are AP-H1's (a)–(c) as
written in `../app-platform-2026-09-18/work-orders/FOLLOW-UP.md` item 5. The dead-3D tidy list is
`../app-platform-2026-09-18/evidence/AP-Q1/release-report.md` §3 step 3 "pending".

## §10 Parallel lanes (D-LANES) — file ownership matrix

"Owns" is exclusive. "May touch" is shared with a rebase expectation (keep both sides). Anything else is
reported, not edited. Concurrent lanes (same wave) never share an "Owns" path.

| WO | Owns | May touch |
| --- | --- | --- |
| UX-01 | `Projects/Starforge/src/editors/**`; `Projects/Starforge/src/widgets/NodeCanvas.{h,cpp}`; `Projects/Starforge/src/LayoutPresets.{h,cpp}`; `Projects/Starforge/src/AssetTypes.cpp` (default flow asset); `Cosmic/dependencies/imgui-node-editor/imgui_node_editor.cpp` + `VENDOR-NOTES.md` (patch record); `tests/test_flow_editor.cpp` (new); `tests/acceptance/manifests/ux01-*.json`; a `Run-UX01Editor.ps1` fixture; `evidence/UX-01/**` | `Projects/Starforge/src/StarforgeApp.cpp` lines 1538-1542, one `CloseAll()` line in `CloseProject`, self-test hook lines; `Cosmic/src/scene/FlowMachine.cpp` (`Validate`: the empty-guard report only); `Projects/Starforge/src/*SelfTest.cpp` (add steps); `tests/CMakeLists.txt` (add TUs); `Projects/Starforge/CMakeLists.txt` (TU list) |
| UX-02 | `Projects/Starforge/src/ViewportController.{h,cpp}`; `UiRectGizmo.{h,cpp}`; `panels/{HierarchyPanel,InspectorPanel,ScreensPanel}.{h,cpp}`; `EditorPrefs.h`; `SourceLocator.{h,cpp}`; `Cosmic/src/graphics/Gizmo.{h,cpp}` (2D rotation write-back); `Cosmic/src/scene/ui/UiSystem.cpp` (`VisitUi` active check only); `tests/test_ui_widgets.cpp` (add cases); `tests/test_ap03_editor.cpp` (add cases); `tests/acceptance/manifests/ux02-*.json`; `evidence/UX-02/**` | `Projects/Starforge/src/StarforgeApp.cpp` (the Autosave block `:1296-1314`, the Open/New/Close/Exit handlers for the prompt, a new Edit ▸ Preferences menu item + modal, File ▸ Open Scene `:2015-2031`); `StarforgeAppPlatform.cpp:315-349` (viewport menu flow hits); `*SelfTest.cpp` (add steps); `tests/CMakeLists.txt`; `Projects/Starforge/CMakeLists.txt` |
| UX-03 | `Cosmic/src/layers/LauncherLayer.{h,cpp}`; `Cosmic/src/scripting/ModuleMacros.h` (the marker macro only); `tests/*Fixture.cpp` (the marker line only); `tests/test_launcher_scan.cpp` (new); `Projects/Starforge/src/StarforgeAppPlatform.cpp` (`ListSamples`/`DrawSampleButtons`/`OpenSample`/template default); `tests/acceptance/manifests/ux03-*.json`; `evidence/UX-03/**` | `Projects/Starforge/src/StarforgeApp.{h,cpp}` (the welcome popup `:2439-2477` and its offer at `:114-115`, the homescreen sample section in `DrawHomescreen`, matching declarations); `tests/CMakeLists.txt`; `*SelfTest.cpp` (E06 extension) |
| UX-04 | `installer/**` (new `Stage-Sdk.ps1`, `StarforgeSetup.iss`); `package.bat`; `.github/workflows/release.yml`; `Projects/Starforge/src/Packager.cpp` + `StarforgeApp.cpp:2953-3060` (KI-63 and the app-repo packaging KI: recursive skips, `dist/` under the project); `Cosmic/CMakeLists.txt` (`GLFW_INSTALL` line); `docs/installer-guide.md`; `tests/acceptance/fixtures/Run-UX04Sdk.ps1`; `tests/acceptance/manifests/ux04-*.json`; the K02 `.bak` fixture; `evidence/UX-04/**` | `tests/acceptance/manifests/pr-*.json` (re-run only); the KI register (KI-63 disposition) |
| UX-G0 | `04-World-Mode-Design.md` (this packet); this file §6 (fill in evidence); `docs/plans/FEATURE-MATRIX.md` (rows); `Projects/Starforge/src/ProjectManifest.h`; `Cosmic/src/scene/SceneSerializer.cpp` (version warning); `Cosmic/src/layers/PlayerLayer.cpp` (init order, clear colour); `Projects/Starforge/src/StarforgeApp.cpp:1181-1197` (clear colour, `ApplyEnvironment`) and `:3262-3275` (Project Settings save); a small shared header for the 2D clear colour constant (e.g. `Cosmic/src/renderer/ClearColor2D.h`, added to the reference manifest); `tests/test_project_manifest.cpp` (new), `tests/test_world_contract.cpp` (new); `tests/acceptance/manifests/uxg0-*.json`; `evidence/UX-G0/**` | `tests/CMakeLists.txt`; `Projects/Starforge/src/StarforgeAppServices.cpp` (mirror of the init order); the KI register |
| UX-D2 | `docs/reference/API-MATRIX.md` (new); `docs/reference/app-services.md` (new); `tests/check_api_matrix.ps1` (new); `tests/test_api_matrix_examples.cpp` (generated, DM02) + its generator script; `evidence/UX-D2/**` | `docs/reference/README.md` (rows + chapter table); `.github/workflows/ci.yml` (one step); `tests/CMakeLists.txt` (one TU line) |
| UX-D1 | `docs/guides/**` (new); the `docs/guide/` → `docs/developer/` rename (paths only, no prose edits); `docs/developer/README.md` (tier intro paragraph only); `README.md`; `docs/README.md`; `Projects/Starforge/src/GuideWalkthroughSelfTest.cpp` (screenshot steps); `tools/guide_shots.py`; `tests/acceptance/fixtures/Run-GuideWalkthrough.ps1` (+ `-SdkRoot` / `-NoSdkEnv` for DG02) + `Run-GuideShots.ps1` (new); `tests/acceptance/manifests/ux-d1-*.json`; `evidence/UX-D1/**` | every file whose links or path comments the rename rewrites (link/comment text only — includes `Cosmic/src/core/Application.h:122`, `tests/render/wo08_common.h:14,281`, `Projects/PendulumLab/src/Y02SelfTest.cpp:205`); `docs/reference/README.md` (manifest paths); `tests/check_docs_coverage.ps1` (tier path only if it names it) |
| UX-05 | `CMakePresets.json`; `tools/Build-Sdk.ps1` (new); `tools/New-AppRepo.ps1` (new); `.github/workflows/ci.yml` (the `consumer` job); `Projects/SF_Telem/README.md` (banner); `tests/acceptance/fixtures/Run-UX05Consumer.ps1`; `tests/acceptance/manifests/ux05-*.json`; `evidence/UX-05/**` | `docs/guides/06-package-and-ship.md` (append the "Your app in its own repo" section under the placeholder heading UX-D1 leaves); `installer/Stage-Sdk.ps1` (the `-Build` hook) |
| UX-H1 | `Cosmic/src/utils/CrashDump.{h,cpp}` (new); the host install lines in `Runtime/Main.cpp` / `Cosmic/src/core/Application.cpp`; the AP-H1 fixtures under `tests/acceptance/fixtures/` + manifests `ux-h1-*.json`; the tidy removals (cameras, Mesh/Material/.cmat, SceneRenderer 3D settings, PostProcess 3D effects, DebugDraw 3D verbs) each in its own commit; `docs/reference/README.md` rows for deleted headers; `evidence/UX-H1/**` | `Projects/Starforge/src/StarforgeAppPlatform.cpp` (the Live chip tooltip / build-once-after-scaffold); the KI register |
| UX-D3 | `docs/developer/**` (post-rename prose, incl. the `lighting-2d.md` rewrite AP-D1 left); `docs/systems/app-platform.md` (new — it does not exist at `0c2edd8`) + `docs/systems/README.md` row; the stale-3D-mention sweep across `docs/reference/*.md` and `docs/systems/*.md`; `docs/reference/ecs.md` (component count; the `Components3D.h` material moves to a parked twin under `docs/parked-3d/reference/`); `evidence/UX-D3/**` | `docs/reference/README.md` (statuses) |
| UX-Q1 | `evidence/UX-Q1/**`; `docs/showcase/**`; `README.md` top strip; this file §11 and recorded deviations; `docs/plans/00-MASTER-ROADMAP.md` statuses; code only for registered KI fixes, each in its own commit | anything a KI fix needs, with the file named in the report |

Worktree protocol and land protocol: `work-orders/README.md` L1–L5.

## §11 New-surface register (filled by the lanes, finalized by UX-Q1)

| Surface | Contract | Class | Proven by | Status |
| --- | --- | --- | --- | --- |
| (rows added by each landing lane: trigger-kind selector, `RouteLink`, Editors host contract, one-gizmo rule, UI Active semantics, flow-usage hits, Scenes list, prefs keys, fixture marker, sample discovery, SDK zip layout, installer, `sdk` preset, app-repo kit, CI consumer job, manifest key preservation, world contract items, guides tier + images manifest, API matrix + checker, crash dumps) | | | | |
| Trigger-kind selector | The transition inspector's Event / Key / Timer / When selector; the rules live in the ImGui-free `Starforge::FlowTrigger` (`Projects/Starforge/src/editors/FlowTrigger.{h,cpp}`: `Kind`, `KindOf(on)`, `SetKind(tr, kind, memory)`, `IsEmptyGuard`, `TimerSeconds` / `TimerOn`): to When turns the guard on (previous fields, else the empty skeleton); away from When restores the transition's last value for the target kind (defaults `signal` / `key:Escape` / `timer:1`) and drops only the guard SetKind(When) added while it is still empty. `FlowAsset::Validate` also reports an empty guard (no channel / variable / entity) on any transition. `.cflow` bytes unchanged (F-FLOWS). Deviation: the skeleton is the empty guard, not a channel guard with a made-up channel name (the shared guard editor infers Channel mode from a non-empty channel; the When hint names Compare > Channel). | Editor behaviour + Starforge-internal API; engine `Validate` message | FE02 | UX-01 (ux/01) |
| `NodeCanvas::RouteLink` | `static void NodeCanvas::RouteLink(ImVec2 start, ImVec2 end, const ImRect& startNode, const ImRect& endNode, float strength, ImVec2& cp0, ImVec2& cp1)` in `widgets/NodeCanvas.h`, defined in `widgets/NodeCanvasRoute.cpp` (pure; compiled into CosmicTests): forward links = imgui-node-editor's stock points bit for bit; backward links / self-loops = start + (s, s), end + (-s, s) with the smallest reach s (24-step bisection) that clears the union of both rects by 12 px at its sides and bottom. Installed by `NodeCanvas::Begin` as the vendored router (`imgui_node_editor.cpp` local patch 2, VENDOR-NOTES.md) for every NodeCanvas (Flow, Story, PostChain). Flow nodes: outputs on the right edge (pivot {1,0.5}, right-aligned `->` column), inputs on the left edge (pivot {0,0.5}) with an 8x8 arrowhead, opaque node background. | Starforge API + vendored-library patch | FE01 (+ FE04 PNG) | UX-01 (ux/01) |
| Editors host contract | `AssetEditorHost` (`editors/AssetEditorHost.h`): `ShouldDraw(showFlag)` = the flag alone (StarforgeApp draws the host only then; its ✕ hides the dock with documents open), `Open()` raises the flag and focuses the window on the next frame, first use 1100x680, every built-in preset docks "Editors" at `DockPort::Center` (no `DockWindow` at open time), stable per-document tab ids (`TabId(path)`, counter at `Open()`), `Find`, `Close`, `AnyDirty`, `LogDirty(ctx, why)`; `CloseProject` runs `LogDirty` + `CloseAll()`; a `CreateDefaultAsset` flow is not dirty on its first frames; `FlowEditor` centres once after the first layout and has an Inspector toggle; harness seams `FlowEditor::HarnessSelection(int&, int&) const` and `HarnessCanvas()`. | Editor behaviour + Starforge-internal API | FE03, FE04, FE05 | UX-01 (ux/01) |

## §12 Acceptance index (every ID has exactly one owner; procedures in `03-Acceptance-Catalog.md`)

| ID | Owner | ID | Owner | ID | Owner |
| --- | --- | --- | --- | --- | --- |
| FE01–FE05 | UX-01 | SD01–SD04, K02-bak | UX-04 | DG01 | UX-D1 |
| ED01–ED05 | UX-02 | EX01–EX05 | UX-05 | DG02 | UX-D1 (made executable) · UX-Q1 (executed from the zip) |
| LH01–LH02 | UX-03 | WM01–WM03 | UX-G0 | DM01–DM02 | UX-D2 |
| H1-A–H1-D, B06 (per tidy commit) | UX-H1 | DOC01, DOC03 | every docs lane (rerun) | Y03, S01, S02, N02-drift-2h, T05, S03, K02 | UX-Q1 |
