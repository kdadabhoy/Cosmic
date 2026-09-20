# AP-03 — Editor UX: rect gizmo, Screens/DataBus panels, source links, live loop, template picker

Status: **landed on `ap/03`** (2026-09-19). Acceptance: E01–E08, F01, V05 (editor half) — all PASS
in Debug and Release through the `ap03-editor` manifest; E05 A/B re-run green through `ap02-gpu`.

## 1. Scope and provenance

- Work order: `work-orders/AP-03.md` (the nine items; executed verbatim, deviations in §7).
- Base: `main` at `e34bdd8` (AP-05 + AP-01 + AP-P1 + AP-02 + AP-04). Worktree `C:\dev\Cosmic-ap-03`,
  branch `ap/03`, `$env:COSMIC_SDK = C:\dev\Cosmic-ap-03`. Ran alone.
- AP-D1 has NOT landed: there is no `tests/check_docs_links.ps1`; the link-checker gate was skipped.
- Local commits (kdadabhoy, no trailers): §9. Not pushed. Not merged.

## 2. Toolchain and environment

- cmake 4.3.1-msvc1 (VS-bundled), MSVC v18 (VS 2026 Community), Windows 11 Education 10.0.26200.
- Configure: `-A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON`
  (COSMIC_2D_ONLY is the post-AP-05 no-op; effective cache: ON).
- Real GPU (GL), real window; the editor self-test runs from a junction-linked child CWD (L02/C05
  pattern) with the project under the runner's `{RUNTEMP}`.

## 3. What was built

New Starforge TUs (all under `Projects/Starforge/src/`, picked up by the CMake glob — the TU list
needed no edit):

| Item | Files | Notes |
| --- | --- | --- |
| 1 rect gizmo | `UiRectGizmo.{h,cpp}`, chips + pick gating in `ViewportController.{h,cpp}` | `UiRectGizmoMath` pure (unit-tested); one `RectOffsetsCommand` per gesture; snap chips 1/8 px + 16 px (KI-1 latched pattern, unprobed) |
| 2 screens | `panels/ScreensPanel.{h,cpp}`, `ScreenScaffold.{h,cpp}`, `assets/editor/stubs/ScreenScript.h.in`, `ProjectManifest.h` (writes `kind`) | refusal text `Module.cpp has no CS_SCREENS markers`; `OpenProject` now opens the flow's start scene (`OpenStartupScene`) |
| 3 databus | `panels/DataBusPanel.{h,cpp}`, `m_PreviewBus` passed with `preview=true` | live table (channel/value/age/producer/Open), preview table |
| 4 source links | `SourceLocator.{h,cpp}`; rows in `panels/InspectorPanel.{h,cpp}`; viewport ctx menu + `ResolveLogicSource` in `StarforgeAppPlatform.cpp` | `COSMIC_AP03_RECORD_SHELL` records instead of launching |
| 5 widgets + hosted | `DrawUiWidgetMenu`, `DrawHostedPanels` (§4 block, letterbox-mapped, stack depths checked), `DrawnThisFrame` written in editor + `PlayerLayer.cpp` | |
| 6 picker + cache | `ListTemplates/ListSamples/OpenSample/DrawTemplatePicker/DrawSampleButtons/CachedProjects`; `BuildFlowDemo`/`BuildForgePong` + helpers deleted (about 1,300 lines) | first-run popup scaffolds ForgePong from disk |
| 7 live loop | `LiveLoopTick/LiveBeforeBuild/LiveAfterBuild/DrawLiveChip`, `m_PlayStartAt`, `m_PlayKeepBus`, `EditorPrefs::AutoResumePlay` | Ctrl+B in Play = stop-build-resume; hammer enabled in Play |
| 8 flow inspector | `editors/FlowEditor.cpp`, `widgets/VariablesPanel.cpp` | Channel guard source, `when` trigger, key picker over `FlowKeyBridge::KeyCodeFor`, widget Signals in the On picker |
| 9 self-test | `AP03AuthoringSelfTest.cpp`, `tests/acceptance/fixtures/Run-AP03Authoring.ps1`, `tests/acceptance/manifests/ap03-editor.manifest.json`, `tests/test_ap03_editor.cpp` (+ `tests/CMakeLists.txt` rows) | 42 plan steps, oracle judged per step |

Both configs build with 0 warnings (`cmake --build build --config {Debug,Release} --parallel`, exit 0).

## 4. Acceptance per ID

Runner: `Run-Acceptance.ps1 -Manifest <abs>\ap03-editor.manifest.json -Config {Release,Debug}`
(`ap03-editor-runner-{Release,Debug}/results.json`, per-config artifacts `ap03-{Release,Debug}/`).
Self-test totals: Release 161 s, Debug 92 s; oracle `recovered_errors=0 end_frame_leaks=0
context_drift=0 hosted_imbalance=0` in both; `live = builds 2 / resumes 2 / failures 1` in both.

| ID | Result (Debug / Release) | Evidence |
| --- | --- | --- |
| E01 | PASS / PASS | F-APP tree byte-equal to `templates/app` (token replaced; FNV-1a `c3fc974676d7ab73`, identical both configs) in-process AND by the wrapper; kind read as `app`, AutoBuild ON; open scene = flow start `Home`; viewport 99.9 % non-clear pixels; real `BuildRunner` build ok; Play → flow `Home`, `m_PlayServices.Count() >= 1`, `app.uptime` published; Stop |
| E02 | PASS / PASS | `scenes/Telemetry.cscene` (Canvas + primary ortho Camera + `NativeScript{TelemetryScreen}`), flow gains the state (removing it restores the canonical bytes exactly), `Validate()` empty, header from the stub (no `@` left), include + `CS_SCRIPT` between the markers, rebuild ok and `TelemetryScreen` registered; marker-less module refused with the documented message, no file touched. Wrapper re-checks the files + flow JSON out of process |
| E03 | PASS / PASS | U: 4 doctest cases bit-exact (`test_ap03_editor.cpp`). I: injected pointer drag of (40, −20) px on `QuitButton` (canvas scale 0.357) → offsets moved by exactly (40,−20)/scale, ONE `CommandStack` entry (`Move UI Rect`), undo restores, redo re-applies, anchors/pivot unchanged; SE resize changes `OffsetMax` only (`Resize UI Rect`, one entry) |
| E04 | PASS / PASS | sentinel PNG imported to `project://textures`, `UiImage.TexturePath` set through `Commands::SetField` (the asset-slot / drop commit), saved, scene reopened → 76,330 sentinel pixels in the viewport readback |
| E05 | PASS (Release, `ap02-gpu`) | `retained-ap02-gpu-Release/results.json`: V03 + E05 PASSED (preview vs live byte-identical A/B unchanged by this lane) |
| E06 | PASS / PASS | picker lists `App, Game, Blank` with descriptions; samples `FlowDemo, ForgePong`; `game`, `blank`, `sample:FlowDemo`, `sample:ForgePong` each scaffold + open + build + play (app proven by E01); `Prefs::LoadProjects` called **1** time over 600 homescreen frames (probe `m_LoadProjectsCalls`) |
| E07 | PASS / PASS | `AppService.cpp` rewritten on disk while playing on `Dashboard` → auto-build (debounced) stops Play, rebuilds, resumes on `Dashboard` with services re-instantiated, `app.sine` |max| = 2.000 (was 1.0), `app.uptime` history keeps samples older than the rebuild, Console has `[Build] Building`; compile error → stays stopped, chip `Build failed`, Console error; fix → resumes on `Dashboard`, chip `Live` |
| E08 | PASS / PASS | U: 7 cases (service/panel/channel/signal/screen/unresolved reasons/record seam). I: 11 recorded invocations, every path equal to the expected absolute file (table in §5); nothing launched |
| F01 | PASS / PASS | rename + set-start inverse ops restore the `.cflow` bytes; set-start changes only `start`; a `when` + channel-guard transition round-trips, validates, and `Validate()` rejects `when` without a guard |
| V05 (editor half) | PASS / PASS | `Diagnostics` CS_PANEL drawn at `vpPos + rect` (recorded), also under the 16:9 letterbox preset inside the band (`band uv 0.074,0,0.851,1`); `UiHostedPanel.DrawnThisFrame` true for it and false for the unknown `Ap03Nope` (placeholder); nothing drawn in edit mode; `hosted_imbalance = 0` and the WO-07 oracle clean |

## 5. Recorded shell invocations (E08, Release run; `<root>` = the scaffolded `Ap03App`)

| Placement | Kind | Recorded path:line | Expected |
| --- | --- | --- | --- |
| Screens panel Open script (Home) | open | `<root>/src/screens/HomeScreen.h:9` | OK |
| Screens panel Reveal (Home) | reveal | `<root>/src/screens/HomeScreen.h:9` | OK |
| Inspector NativeScript Open source | open | `<root>/src/screens/HomeScreen.h:9` | OK |
| Viewport ctx Open logic source (edit mode → script) | open | `<root>/src/screens/HomeScreen.h:9` | OK |
| DataBus panel Open producer `app.uptime` | open | `<root>/src/Module.cpp:25` (CS_SERVICE site) | OK |
| Inspector Channel row Open producer `app.sine` | open | `<root>/src/Module.cpp:25` | OK |
| Viewport ctx Open logic source (hosted panel) | open | `<root>/src/services/AppService.cpp:33` (CS_PANEL site) | OK |
| Viewport ctx Reveal (hosted panel) | reveal | `<root>/src/services/AppService.cpp:33` | OK |
| Inspector UiHostedPanel Open source | open | `<root>/src/services/AppService.cpp:33` | OK |
| Viewport ctx Open logic source (bound widget channel) | open | `<root>/src/Module.cpp:25` | OK |
| Inspector Find handlers `counter.increment` | open | `<root>/src/services/AppService.cpp:62` | OK |

Raw file: `ap03-Release/ap03-shell-invocations.txt` (and the Debug twin).

## 6. Defects found (registered in the KI register before fixing)

- **KI-59** — `FileSystem::SetActiveProjectPath` keyed the project layout on the mere existence of an
  `assets/` folder; the AP-04 app template (contract §8 `assets/ui/*.png`) therefore mounted
  `project://` one level too deep: manifest defaults, no flow, empty scene. Failing-before: the first
  Debug run (`ap03-Debug-dev`, 51 cascading failures, `manifest kind not read as app`). Fixed: the
  manifest's location decides (`assets/project.cproj`, or `assets/` without a root `project.cproj`).
- **KI-60** — the editor's `GameModule::Load` never called `InitializePluginContexts`, so the first
  `CS_PANEL` draw inside the module hit a null ImGui context (`0xC0000005` right after
  `E07 play + navigate to Dashboard`, second Debug run). Fixed in `GameModule.cpp` (resolves the export
  like `Application::LoadPlugin`).
- Live-loop interaction found by the retained L02: an auto-build that started BEFORE Play and finished
  during Play must follow the WO-07/KI-28 reload contract (stop, no resume). An intermediate version
  resumed it and broke L02; the landed code resumes only builds the live loop started during Play.

## 7. Contract deviations / caveats (for the integrator)

- Engine May-touch beyond the prompt's one-liner: `Cosmic/src/utils/FileSystem.cpp` (KI-59),
  `Cosmic/src/layers/PlayerLayer.cpp` (`DrawnThisFrame`, the announced fix).
- AP-01's TU `StarforgeAppServices.cpp`: one guarded line (`if (!m_PlayKeepBus) m_PlayBus.Clear()`) —
  the skip AP-01's own comment reserved for the D-LIVE resume.
- `tests/CMakeLists.txt`: 4 rows (test TU + the three Starforge TUs compiled from the tree).
- E02/F01 "byte-stable": the template's hand-authored `Main.cflow` is not in the serializer's canonical
  form; stability is asserted against the canonical Load→Save text (every editor write from then on).
- E04 drives the field command the Inspector asset slot / Content Browser drop commit, not the ImGui
  drag itself. E08's I half drives each placement's resolution API + `Open`/`Reveal`, not the ImGui
  buttons; the buttons call the same functions (`InspectorPanel::DrawSourceLinkRow`,
  `ScreensPanel::OpenScript`, `DataBusPanel::ProducerHit`, `StarforgeApp::ResolveLogicSource`).
- F01's "On picker lists the widgets' Signal strings" is implemented (`FlowEditor::ScanKnownSignals`
  reads `UiSlider`/`UiToggle`) but not self-tested (private to the editor document).
- `ScaffoldProjectTo(name, dest, kind)` already took the kind (AP-01); `NewProjectAt` gained a kind
  overload; the two-arg form keeps `game` so C05/L02 are unchanged.
- The rect gizmo's Move surface captures only when the primary element is the topmost hit, so
  overlapping elements stay selectable.
- The link checker (AP-D1) does not exist yet: skipped, not passed. `wo10-sample` exists and was run.

## 8. Retained gates

| Gate | Result |
| --- | --- |
| CosmicTests Debug | 519 passed / 0 failed / 14 skipped (512 + 7 AP-03 units) — `units-Debug-excerpts.txt` |
| CosmicTests Release | 519 passed / 0 failed / 14 skipped — `units-Release-excerpts.txt` |
| `wo07-l02` Release | PASS (`retained-wo07-l02-Release/results.json`, after the KI-28 restore) |
| `wo09-editor` Release | PASS (C05-I) |
| `wo10-sample` Release | PASS (X01) |
| `ap02-gpu` Release | PASS (V03, E05) |
| `check_gl_conformance.ps1` | exit 0 — `audit-gl-conformance.txt` |
| `check_docs_coverage.ps1` | exit 0 — `audit-docs-coverage.txt` |
| `check_docs_links.ps1` | not present (AP-D1 deferred) |

Retained runs rewrote tracked evidence under `docs/plans/2d-stability-2026-09-16/evidence/**` and
`evidence/AP-02/e05-*`; those were reverted (`git checkout --`) so the lane commits carry only AP-03 files.

## 9. Files

Evidence: `ap03-{Debug,Release}/` (result JSON, console, shell invocations, the E02 files),
`ap03-editor-runner-{Debug,Release}/`, `retained-*-Release/`, `units-*-excerpts.txt`, `audit-*.txt`,
`ap03-Debug-dev/` (the iteration runs incl. the failing-before logs for KI-59/KI-60).
Commits on `ap/03` (kdadabhoy, no trailers): gizmo · source locator · screens+scaffold · databus panel ·
source-link rows/flow inspector/KI fixes · platform wiring+self-test · evidence+§13.
