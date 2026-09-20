# AP-Q1 — release report: integrate, qualify, showcase, staged push

Status: qualification record, 2026-09-20 (session started 2026-09-19 late evening). Gate **G5** for the
App Platform packet under Kaden's **lite scope** (2026-09-19): steps 1, 2, 3, 4, 7, 8 in full; step 5 only
Y02, S03 and K02 (PendulumLab); the 2-hour soaks Y03, S01, S02 **pending, not run (budget)** — exact
commands in §9. Everything below was produced by one session working alone on `main` in `C:\dev\Cosmic`.

## 1. Pinned SHA, provenance, clean status

| Item | Value |
| --- | --- |
| **Pinned (qualified) SHA** | **`fa1223a88629319a927304d7c6dbb04401c3604a`** (`main`) |
| Session base | `8b4798a` = merge of `ap/03` (AP-03) — the first build/test of that merge happened here |
| Commits added by AP-Q1 (in order) | `a80cdd3` contract reconciliation · `a6524a7` tidy: dead GPU-resource/math classes · `62098a2` tidy: unreferenced shaders/texture · `54b0beb` Y02 wrapper + manifest · `c35e6bd` manifest JSON escape fix · `06116f3` Y02/X01 harness fixes · `fa1223a` Y02 readback after the scene render · (this report's commit, docs/evidence only) |
| Landing order verified (`git log --first-parent`) | `726ae35`/`7479927` AP-00 → `1bedfa4` AP-05A → `84a8075` AP-05B → `a12cf27`/`6904112`/`190b54d` AP-01 → `e01f0a0` AP-P1 (`ap/p1` tip `0b8c4bf`) → `ef795c1` AP-02 (`8dc0346`) → `e34bdd8` AP-04 (`28d6516`) → `8b4798a` AP-03 (`b1e3c46`). **AP-D1 and AP-D2 deferred** (Kaden's decision, not missing by mistake). |
| Lane branches | `ap/02`, `ap/03`, `ap/04`, `ap/p1` all `--merged main`; every worktree (`C:\dev\Cosmic-ap-{02,03,04,p1}`) is clean and its HEAD is an ancestor of `main`. Worktrees left in place (L3). |
| `engine-3d` / tag `cosmic-pre-2d-2026-09-16` | both at `0e8894b8540029ac57e68540aa9774cf5cf77ebe` (tag object `76ca58b6…`); untouched (reflog shows only the branch creation). |
| Status at start | clean apart from the four untracked paths Kaden asked to leave alone (`Cosmic - 2D Trunk Consolidation & Acceptance Plan.md`, `recordings/`, `…/WO-07/l01-runner-Release/`, `work-orders/ORCHESTRATOR.md`). |
| Remote observation | `origin/main` moved to `a80cdd3` at 2026-09-20 00:05:47 −0700 ("update by push" in the reflog) **between this session's commits**. This session ran **no** `git push`; the push was made by someone/something else (Kaden pushes). Recorded, not acted on. |
| Runner `dirty=True` | every `results.json` says `dirty=True` with `dirty_diff_sha256=null`: the flag comes from **untracked** files (the AP-Q1 evidence directory being written plus Kaden's four paths), never from modified tracked files. |

Compiled-code deltas between the SHAs the runs are keyed to: `54b0beb → c35e6bd` manifest JSON only;
`c35e6bd → 06116f3` `Projects/Starforge/src/X01PackageSelfTest.cpp` (editor test-harness TU) and PendulumLab
sources; `06116f3 → fa1223a` PendulumLab sources only (`Y02SelfTest.cpp`, `PendulumService.{h,cpp}`, which
`CosmicTests` also compiles). **Every retained suite and every manifest in §4 was (re-)run at `fa1223a`**
after the last code commit; the earlier runs (`54b0beb`, `c35e6bd`) are kept under
`manifests-Release/` and `manifests-Release-rerun/` as the first-pass record.

## 2. Environment (from the runner's `results.json` + probes)

| Item | Value |
| --- | --- |
| OS | Microsoft Windows 11 Education 10.0.26200, build 26200.9457 (25H2) |
| CPU | AMD Ryzen 7 7800X3D, 8C/16T; ISA probe SSE2/SSE3/SSSE3/SSE4.1/**SSE4.2**/AVX/FMA/AVX2/AVX512F (floor SSE4.2 met) |
| RAM | 31.2 GiB |
| GPU / driver / GL | NVIDIA GeForce RTX 5070 Ti, driver 32.0.16.1692; `OpenGL 4.5 — NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2` (render-test log). Second adapter: AMD Radeon(TM) Graphics 32.0.21045.5002 (unused) |
| Toolchain | Visual Studio 18 2026 generator, MSVC 14.51.36231, VS-bundled `cmake 4.3.1-msvc1`, Windows PowerShell 5.1.26100.9444, `py -3` = Python 3.14 |
| Configure | `-A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON`; cache `COSMIC_2D_ONLY:BOOL=ON`, `COSMIC_BUILD_TESTS:BOOL=ON`, `COSMIC_BUILD_RENDER_TESTS:BOOL=ON` |
| Capabilities probed | windows ✓, cpu-sse42 ✓, gpu-gl ✓, render-tests ✓, audio-device ✓, serial-device ✗ (no COM hardware, ever), editor-ui ✗ (runner capability), sf-stable-v1-capture ✗ |
| Runner invocation | `Run-Acceptance.ps1 -Manifest <abs> -Profile <p> -Config <c> -OutDir <abs> -TempRoot C:\dev\Cosmic\build\_temp\apq1` via `Run-Manifests.ps1` (this folder). The repository-local `-TempRoot` is **required** by the WO-07/WO-09/AP-03 fixtures (they refuse a run-temp outside the repo); the first pass without it produced 4 spurious FAILs (§4.4). |

## 3. What was done, per step

1. **Verify** — §1 above.
2. **Contract reconciliation** — `a80cdd3`: every "Contract deviations" section (AP-01 D1–D4, AP-02 D1–D6,
   AP-03 D1–D3, AP-04 D1–D3, AP-05 A1–A3/B1–B4, AP-P1 D1/D2/D4) applied inline to
   `01-Design-Contracts.md` §1/§2/§3/§4/§5/§6/§8/§9/§10 as `[AP-xx Dn]` notes; §13 rewritten as the final
   register (17 landed rows + purge, packaging, CI, docs-deferred, showcase and tidy rows), the 15
   "planned" placeholder rows dropped. AP-04's evidence report `Local commits` corrected to the real
   `main` SHAs (`0fa2dd7`/`3894819`/`b4a0dff`/`28d6516`; the pre-rebase ones are noted) — in this commit.
3. **Tidy** (time-boxed ~20 min, as instructed) —
   - `a6524a7` removed `renderer/RenderQueue.h`, `math/Frustum.h`, `renderer/CameraUniforms.h`
     (`GpuCameraBlock`), `graphics/TextureCube.{h,cpp}` + `platform/OpenGL/OpenGLTextureCube.{h,cpp}` +
     the `BindTextureCubeSlot` verb in `RendererAPI.h`/`RenderCommand.h`/`OpenGLRendererAPI.{h,cpp}`,
     `graphics/UniformBuffer.{h,cpp}`, `graphics/StorageBuffer.{h,cpp}` + their OpenGL implementations;
     `Cosmic.h` includes and 5 `docs/reference/README.md` manifest rows dropped; `BindingPoints.h` comment
     re-worded under `History:`. Grep oracle before deleting: zero consumers under `Cosmic/src`,
     `Projects`, `tests`, `Runtime` other than `Cosmic.h` and the files themselves.
   - `62098a2` deleted `Cosmic/assets/shaders/{ComputeParticles,FlatColor,FlowEmissive,MeshLit,Outline,
     ParticlePoints,WaterFlow}.glsl` and `Cosmic/assets/textures/Galaxy.png` (no load site).
   - Both commits were verified on one build (Release 0 warnings, then Debug 0 warnings) and the B06
     oracle (`ap05-purge`, `b06-tidy1-Release/`: PASS) — one build for the two commits, stated honestly.
   - **Pending (not removed, listed for a later tidy):** `OrbitCameraController`/`FlyCameraController`/
     `PerspectiveCamera` (+ editor `EditorCameraRig`, `ViewportController` Orbit/Fly), `Mesh`/`MeshVertex`/
     `MeshData`/`SkinVertex`/`Submesh`, `Material` queue hints + the `.cmat` pipeline (`MaterialAsset`,
     `AssetLibrary::GetMaterial/BuildMaterial`, `MaterialEditorPanel`, `PBR.glsl`/`PBRSkinned.glsl`),
     `SceneRenderer` `ScenePass::{ShadowDepth,Reflection,TopDownDepth}` + the Skybox/IBL/Shadows/
     Underwater/LensFlare/Outline settings and their host assignments, `EnvironmentComponent` sun/sky
     fields, `PostProcessStack` SSAO/fog/underwater/lens-flare/heat-haze, `PhysicsWorld::DebugDraw`/
     `IPhysicsBackend::DebugDraw`, `CameraComponent::Projection::Perspective` default, `EditorSnapshot.h`/
     `PreviewRig.h` prose, the `rendering-3d.md` chapter name (AP-D1).
4. **Retained suites at the pinned SHA** — §4.
5. **Long runs (lite scope)** — Y02 PASS, S03 PASS ×5 both configs, K02 PendulumLab PASS; Y03/S01/S02
   pending (§9).
6. **Defects** — no crash / hang / data-loss defect was found: **no new KI registered** (KI-1..60
   unchanged, §6). Two **test-harness** defects were found and fixed with failing-before / passing-after
   evidence (§5) — they are harness code, outside the register's scope.
7. **Showcase kit** — §7.
8. This report.

## 4. Retained suites and manifests at `fa1223a`

### 4.1 Unit and render suites (new baseline)

| Suite | Config | Result | Assertions | Evidence |
| --- | --- | --- | --- | --- |
| `CosmicTests.exe` | Release | **519 passed / 0 failed / 14 skipped** (`--count` 519) | 23,428,650 | `units-Release-excerpts.txt` |
| `CosmicTests.exe` | Debug | **519 / 0 / 14** (`--count` 519) | 23,407,130 | `units-Debug-excerpts.txt` |
| `CosmicRenderTests.exe` | Release | **45 passed / 0 failed / 2 skipped** (`--count` 45) | 23,516,339 | `render-Release-excerpts.txt` |
| `CosmicRenderTests.exe` | Debug | **45 / 0 / 2** | 23,516,339 | `render-Debug-excerpts.txt` |
| Goldens `tests/render/goldens/*.png` | — | **15 files byte-identical** to HEAD before and after every run (`git diff --quiet` clean; the runner's before/after hash lists agree), the 7 `ap02_*` goldens included | — | `golden-hashes.txt` |
| `tests/check_gl_conformance.ps1` | — | exit 0, "clean" | — | `audit-gl-conformance.txt` |
| `tests/check_docs_coverage.ps1` | — | exit 0, "clean (123 public headers, 120 manifest rows, 6 skeleton chapters)" — was 127/125 before the tidy | — | `audit-docs-coverage.txt` |
| `tests/check_docs_links.ps1` | — | **does not exist — deferred (AP-D1)**; not a failure | — | — |

Baseline change vs AP-03's lane: unit case count unchanged (519 / 519; the tidy deleted no test), render
cases unchanged (45). The pre-purge → post-purge count history is in `evidence/AP-05/test-counts.txt`.

### 4.2 Manifests, Release, final run (`manifests-Release-final/`)

All 33 manifests the stability campaign left green plus every `ap*` manifest and the new `apq1-y02`,
profile `release` (or `pr` for `pr-*`, `wo05`, `wo06`, `selftest`). Full per-case table:
[`manifest-table-Release-final.md`](manifest-table-Release-final.md).

| Result | Cases | Notes |
| --- | --- | --- |
| **PASSED** | **117** | every retained wo05/06/07/08/09/10 case in scope, every ap01/02/03/04/05 case, Y02, X01, R07, the three retained-suite wrappers |
| ENVIRONMENT_BLOCKED | 3 | `pr-windows/D01-genuine-SF-Stable` (capability `sf-stable-v1-capture`: a genuine SF-Stable v1 capture fixture), `pr-blocked/EDITOR-I` (`editor-ui`), `pr-blocked/SOAK-Q` (`serial-device`) — the last two are the designed blocked-branch proofs of H05 |
| FAILED / TIMEOUT / MISSING | 3 / 1 / 1 | **all inside `selftest` run in `-SelfTest` mode**, where they are the designed classifications; the runner printed `[SELF-TEST OK] every case was classified exactly as expected`, exit 0 (`selftest-selftestmode/`) |
| Real failures at `fa1223a` | **0** | — |

`wo07-p01` (P01) is in the table from a **stand-alone** run after the chain: inside the chain it failed
with `occluded=500 (otherwin=500)` because the Debug unit run I started concurrently opened GL windows over
it — my scheduling, not the product; alone (and in the `c35e6bd` re-run) it passes with
`checked=500 match=500 mismatch=0 occluded=0`.

### 4.3 Manifests, Debug (`manifests-Debug/`)

U/W manifests plus `ap02-gpu`, `wo07-p01` and `ap03-editor` in Debug (the I/G-tier manifests whose
authors ran both configs). Table: [`manifest-table-Debug.md`](manifest-table-Debug.md). Result:  **81 PASSED, 1 ENVIRONMENT_BLOCKED** (`pr-windows/D01-genuine-SF-Stable`, the `sf-stable-v1-capture`
fixture), 0 failed. `wo07-p01` Debug is the stand-alone re-run: inside the Debug chain it was occluded by the
S03 Release processes I ran concurrently (`otherwin=500`), alone it passes (`checked=500 match=500`).

### 4.4 First pass (`54b0beb`/`c35e6bd`) and what it taught

`manifests-Release/` (first chain) had 6 non-passing manifests, none of them product defects:
`wo07-l02`, `wo09-editor`, `ap03-editor` — the fixtures throw "artifacts must remain inside Cosmic" when
the runner's run-temp is `%TEMP%` (fixed by `-TempRoot <repo>\build\_temp\apq1`, as the lanes did);
`wo07-l05/L05-editor` — 3 of 1,200 synthetic clicks not registered while CosmicTests Debug was running
concurrently (passes alone, twice); `wo07-p01` — occluded window (passes when foreground); `apq1-y02` —
my manifest's JSON escapes (fixed in `c35e6bd`), then the two harness defects of §5; `selftest` — needs
`-SelfTest`. `manifests-Release-rerun/` holds the `c35e6bd` re-runs (all PASS except Y02, pre-fix).

### 4.5 Retained-suite wrappers rewrite tracked evidence

The wo* manifests write into `docs/plans/2d-stability-2026-09-16/evidence/**` and `evidence/AP-0x/**`
(children.json, golden hash lists, captures — 178 tracked files). They were reverted with `git checkout --`
before the evidence commit, as the lanes did; the tree-root `n04-*.bin`/`n04-hashes.txt` were deleted.

## 5. Defects and fixes (failing-before / passing-after)

No engine, editor or app defect of the register's kind (crash, hang, data loss, stale callback, silent
corruption) was found. **KI-57.. not extended; KI-61 stays the next free number.** Two test-harness
defects, both in AP-04's Y02 host / the shared X01 package harness, both blocking Y02 from ever passing:

| # | Defect | Failing-before | Fix (commit) | Passing-after |
| --- | --- | --- | --- | --- |
| H-1 | `X01PackageSelfTest` required `assets/projects/<App>/data/trajectory.csv` — an AnalysisSample-only file — so the editor-path packaging of any other project (PendulumLab) reported `staged payload missing` | `manifests-Release-rerun/apq1-y02` → package step FAIL (`y02-package-result.json` at that run) | the check now iterates the SOURCE project's `data/` directory (`06116f3`) | `manifests-Release-final/apq1-y02` package PASS, `build_seconds` 34.8, 63-path payload |
| H-2 | `Y02SelfTestService` read the viewport framebuffer in its `OnUpdate`, i.e. after `WorkspaceLayer` had cleared the target (0.1 grey) and before `PlayerLayer::RenderScene` — the ROI was always the clear colour (AP-04's own smoke recorded the same flat ROI and attributed it to the missing UiPlot renderer) | `y02-Release/y02-plot-roi.png` from the `06116f3` run: 140,033 px, 0 line-colour px (flat 25,25,25) | one-shot `PendulumService::SetAfterPhasePlotDrawOnce` seam runs the readback at the end of the frame's PhasePlot hosted-panel draw (host ImGui pass, after the render), `FrameBuffer::Bind()` before `ReadPixels` (it reads the bound FBO), whole frame written as `y02-frame.png` (`fa1223a`) | `y02-Release/`: **904** line-colour px in the Plot rect `[666 113 1265 345]` of 1280×664, `y02-plot-roi.png` shows the two traces + grid, `y02-frame.png` the whole Lab screen |

Also corrected in the same commits: my Y02 wrapper's oracle no longer asserts the app's cwd equals the
launch cwd — the runtime re-roots itself to the exe directory on purpose (`Runtime/Main.cpp`
`SetCurrentDirectoryA(exeDir)`), and portable mode puts `user://` at `<exe>/user` when the exe dir is
writable (§12); both are recorded in the log line `launch cwd=… app cwd=… user_root=./user`.

Observed nit for the packaging owner (not a defect): `user/README.txt` is staged with CRLF + trailing
newline by the editor `Packager` (text-mode `std::ofstream`) and with LF by `Stage-AppPackage.ps1`;
same words. K02's script records it as a note.

## 6. KI dispositions

KI-1..60 **unchanged by AP-Q1**; the register (`docs/plans/2d-stability-2026-09-16/contracts/known-issues.md`)
was not edited. Summary of the state at `fa1223a`:

- Fixed and regression-covered (fix landed in the WO named in each entry): KI-1, KI-4–KI-7, KI-8, KI-9,
  KI-10–KI-54 (WO-05/06/07/08/09/10), KI-57 (AP-P1 `Scratch()`), KI-59, KI-60 (AP-03).
- Open by disposition: **KI-2** (SerialLink connected-state coverage gap — only real COM hardware closes
  it; never available here), **KI-3** (registered as an enforcement gap; the 2D-mode enforcement landed in
  WO-03 and B06/`Verify-AP05Purge.ps1` now guards the trunk, entry text not updated), **KI-55** (huge
  finite `TimeScale` — proposal for Kaden: clamp to a documented maximum), **KI-56** (`Layer::m_LocalTime`
  float quantisation — documented as a local animation phase), **KI-58** (`CosmicTests.exe` fail-fasts
  when its working directory is read-only — open, suggested owner: next test-host change).
- Duplicate entry note: the second "KI-57 — registered here by AP-05 part A" heading is the merge marker
  AP-P1 left; one entry carries the fix.
- New in AP-Q1: **none** (the two harness defects of §5 are outside the register's scope).

## 7. Showcase kit (DOC05)

`docs/showcase/README.md` — the 150-word blurb, the one-line feature list, the caption table — and the
root `README.md` top strip. Captures and their status are listed in `docs/showcase/README.md`; the
capture procedure (real editor / packaged app on screen, Windows screenshot of the window via
`Capture-Window.ps1`, input via `Send-Click.ps1`, every PNG ≤ 1 MB) and any missing capture with its reason
are in §7.1 below (taken after the Debug chain, which needed the editor undisturbed).

### 7.1 Capture status — 12 of 12 taken, none missing

| # | File | Size | Taken as |
| --- | --- | --- | --- |
| 1 | `01-homescreen-template-picker.png` | 2576x1408, 120 KB | Starforge (Release, `fa1223a`) maximised, New Project modal open (App / Game / Blank radio list, Samples buttons) |
| 2 | `02-rect-gizmo-arranging-screen.png` | 2576x1408, 331 KB | PendulumLab `Lab.cscene` in edit mode, the Plot element selected: move surface + 8 handles, 1/8 and 16 snap chips in the toolbar, Inspector with the `Open producer` source links |
| 3 | `03-screens-panel-linked-script.png` | 2576x1408, 200 KB | Screens panel (4 screens, start marker on Home, scripts HomeScreen/LabScreen/SettingsScreen/StoppedScreen) over the Home scene |
| 4 | `04-inspector-open-source.png` | 2576x1408, 270 KB | Inspector on the Lab `Canvas` entity: Native Script `LabScreen` with `Open source` / `Reveal` |
| 5 | `05-pendulumlab-flow-graph.png` | 2576x1408, 339 KB | Editors ▸ Main flow: Home / Lab / Settings / Stopped (overlay) / @quit nodes with the transitions, `on when [+] [push]` on Lab |
| 6 | `06-databus-panel-live.png` | 2576x1408, 310 KB | DataBus panel during editor Play: 11 live channels (`pendulum.*`, `settings.*`), value, age, producer `PendulumService`, Open links |
| 7 | `07-lab-screen-live.png` | 2576x1408, 242 KB | Lab screen live in editor Play: angle/omega value texts, energy gauge, the two-channel plot, the hosted phase plot, PLAYING + Live chips |
| 8 | `08-hosted-implot-phase-plot.png` | 780x370 crop of #7, 32 KB | the `CS_PANEL("PhasePlot")` ImPlot scatter drawn inside its UI element during editor Play (the packaged-player rendering of the same panel is in #10) |
| 9 | `09-live-loop-chip-after-rebuild.png` | 2576x1408, 292 KB | after touching `src/screens/LabScreen.h` during Play: Console `[build] SUCCESS … [Live] Resumed Play on 'Lab'`, status bar `PLAYING · module ok · Live` (the intermediate `building… / Building…` state is in `build/_temp` only) |
| 10 | `10-packaged-pendulumlab.png` | 1280x720, 55 KB | `dist\PendulumLab\PendulumLab.exe` (the Y02 package) on its Lab screen, launched from `build\_temp\apq1` |
| 11 | `11-sf-telem-main-screen.png` | 1280x720, 205 KB | `CosmicApp.exe --project SF_Telem`, Main Telemetry workspace (no COM port opened) |
| 12 | `12-acceptance-run-summary.png` | 1115x628, 120 KB | Windows Terminal showing `Run-Acceptance.ps1` for `ap01-units` followed by the qualification chain summary (`manifests-Release-final/summary.txt`) |

Method: real windows, Windows screenshots (`Capture-Window.ps1`, DWM frame bounds, lossless PNG, auto-downscale
if > 1 MB — never needed), input through `Send-Click.ps1` (SetCursorPos + `mouse_event`/`keybd_event`, i.e. ordinary
WM_ input to GLFW/ImGui). PendulumLab was pre-registered in the dev editor's `user://starforge/projects.toml`
(`build/Runtime/Release/starforge/`) so the homescreen card opened it. The `LabScreen.h` touch for #9 was reverted
(`git checkout --`). No engine PNG capture was needed: every viewport is inside a captured window.

Nits seen while capturing (not defects): the New Project modal shows the `app` template's description as the raw
token `@PROJECT_NAME@` (the template README's first line is used verbatim); the hosted ImPlot axis labels are
faint over the scene because the hosted window is `NoBackground`.

**Concurrent session on the same desktop.** While capturing, a second `Starforge.exe` from
`C:\dev\Cosmic-ap-guide\build\Runtime\Release` (another session's docs walkthrough, "guide-run11", project
`PendulumLab2`) was running. Two of my inputs reached it before I restricted targeting to my editor's PID
(`-OwnerPid`): I stopped its process 39304 by mistake (it was re-launched by its own driver within a minute as
PID 23316) and one menu-bar click may have landed on it. Nothing in `C:\dev\Cosmic` was affected; the other
session's owner should know its run 10/11 may have been disturbed.

## 8. Requirement → case → evidence matrix

Legend: **P** passed at `fa1223a` · **B** ENVIRONMENT_BLOCKED (prerequisite named) · **N** not run in the
lite scope (command in §9) · **D** deferred by Kaden's decision (AP-D1/AP-D2) · **E** evidence of the
owning WO stands, not re-run here (reason given).

### 8.1 App Platform catalog

| ID | Requirement | Case / manifest | Result | Evidence |
| --- | --- | --- | --- | --- |
| B06 | 3D purge oracle | `ap05-purge` Release + Debug; re-run after each tidy commit | **P** | `manifests-Release-final/ap05-purge`, `manifests-Debug/ap05-purge`, `b06-tidy1-Release/` |
| V01 | DataBus | `ap01-units/V01` R+D; S03 ×5 both configs | **P** | `manifests-*/ap01-units`, `s03-*/` |
| V02 | ServiceHost U + W (20 DLL reloads, PlayerLayer path) | `ap01-units/V02-U`, `V02-W-PLAYER` R+D | **P** | `manifests-*/ap01-units` |
| V06 | Data() proxy, flow guards, key bridge | `ap01-units/V06` R+D | **P** | same |
| V03 | Widget goldens + sentinel ROIs | `ap02-gpu/V03` R+D; goldens byte-identical | **P** | `manifests-*/ap02-gpu`, `golden-hashes.txt` |
| V04 | Widget helpers / Update / serialization | `ap02-units/V04` R+D | **P** | `manifests-*/ap02-units` |
| V05 | Hosted panels: engine half / editor half / player half | `ap02-gpu` (engine) · `ap03-editor` (editor, R+D) · **Y02** player half: PhasePlot draws 4,442 in the packaged exe | **P** | `manifests-*/ap02-gpu`, `manifests-*/ap03-editor`, `y02-Release/y02-result.json` |
| E01–E08, F01 | Editor authoring self-test (New Project app → F-APP tree → viewport non-blank → build → Play → Home; Screens panel; gizmo undo/redo; texture slot; E05 A/B; template picker; live loop incl. compile error; source links recorded) | `ap03-editor/AP03-EDITOR` Release (176 s) + Debug | **P** | `manifests-Release-final/ap03-editor`, `manifests-Debug/ap03-editor`, AP-03's `ap03-*/result.json` |
| F02 | PendulumLab flow headless + 200-step determinism | `ap04-units/F02-U` R+D; S03 `F02-200-step-trace` ×5 identical | **P** | `manifests-*/ap04-units`, `s03-*/summary.json` |
| Y01 | PendulumLab standalone from a clean SDK path; RK4 vs F-PENDULUM; bit-identical runs | `ap04-sample/Y01` Release; `ap04-units/Y01-U` R+D; S03 `F-PENDULUM-*` ×5 (series FNV-1a `fcb4b3e0…` identical across 5 processes and both configs) | **P** | `manifests-*/ap04-*`, `s03-*/` |
| Y02 | Editor packages PendulumLab through File ▸ Package; staged exe from another cwd with the self-test | `apq1-y02/Y02` Release: package PASS (34.8 s build), run PASS (trace Home,Lab,Settings,Lab,Home; angle −5.000..4.998°; draws 4,442; plot 904 line px), oracle PASS | **P** | `manifests-Release-final/apq1-y02`, `y02-Release/` (result JSON, package payload+hashes, ROI + frame PNG) |
| Y03 | 2-h PendulumLab soak | — | **N** | §9 |
| H05 | Acceptance in CI (`acceptance-pr`) | `pr-units` (19 P), `pr-windows` (2 P + 1 B), `pr-blocked` (1 P + 2 B, the designed blocked branches) both configs; the CI job itself runs on Kaden's push | **P** (local), CI pending push | `manifests-*/pr-*` |
| DOC04 | Link checker, parked-3d banners | — | **D** (AP-D1) | — |
| DOC05 | Showcase kit | `docs/showcase/` + README strip | see §7 | `docs/showcase/README.md` |

### 8.2 Stability catalog (retained expectations)

| ID | Case / manifest | Result | Evidence / reason |
| --- | --- | --- | --- |
| B01–B05 | WO-03 build/CI enforcement | **E** | WO-03 evidence; the 2D option is a compatibility no-op since AP-05B (cache values in §2); the trunk oracle is now B06 (P) |
| H01–H04 | runner self-test | **P** | `selftest-selftestmode/` — `[SELF-TEST OK]`, exit 0 |
| T01, T02, T03 (write-close, late-open, owner-cancel, root-matrix), T06 | `pr-units`, `wo05:pr` R+D | **P** | `manifests-*/pr-units`, `manifests-*/wo05` |
| T04 | virtual/USB COM on two Windows versions | **B** | serial hardware — never available (locked decision) |
| T05 | 30-min controlled soak (`nightly`) | **N** | `Run-Acceptance.ps1 -Manifest …\wo05.manifest.json -Profile nightly -Config Release -TempRoot …` |
| D01–D06 (+ D05-two-hour fake-clock) | `wo06:pr`, `pr-units`, `pr-windows` R+D | **P** (D01-genuine-SF-Stable **B**: needs the `sf-stable-v1-capture` fixture) | `manifests-*/wo06`, `manifests-*/pr-*` |
| L01, L02, L03, L04, L05 (sftelem + editor), P01, KI-1 | `wo07-*` Release | **P** | `manifests-Release-final/wo07-*` (`wo07-p01` stand-alone, §4.2) |
| R01–R06 (G) + R04-U/R06-U + R07 (11-min Q) | `wo08-gpu`, `wo08-units` R+D, `wo08-r07`, `wo08-retained` | **P** | `manifests-*/wo08-*` |
| C01–C06 (U+G) + C05-I + C06-AUDIO | `wo09-units` R+D, `wo09-gpu`, `wo09-editor`, `wo09-retained` | **P** | `manifests-*/wo09-*` |
| N01 (11 host cases), N02 (U + 7 policy/origin), N03, N04 (+FILTERS/LOOKUP/SCENE), X01 | `wo10-units` R+D, `wo10-host` R+D, `wo10-sample`, `wo10-retained` | **P** | `manifests-*/wo10-*` |
| N02-drift-2h | `wo10-drift` (55-min W-tier drift run) | **N** | `Run-Acceptance.ps1 -Manifest …\wo10-drift.manifest.json -Profile release -Config Release -TempRoot …` |
| K01 | clean-SDK external consumer | **P** | `ap04-sample/Y01` is the K01 procedure for PendulumLab; AP-P1 evidence for the minimal consumer |
| K02 | package SF_Telem + AnalysisSample (AP-P1) + **PendulumLab** via CLI and editor paths | **P** | `k02-PendulumLab/report.json`: CLI (`Stage-AppPackage.ps1`) and editor (Y02 dist) payloads **identical, 60 paths**; exe/app DLL/Cosmic.dll byte-identical; boot.cfg names `PendulumLab`; 10 licence files per `MANIFEST.txt`; 25 shaders, 4 font files; no test exe / PDB / other app DLL / src / build; note: `user/README.txt` line endings differ (§5) |
| K03 | clean Win10/Win11 targets | **B** | Windows 10 VM (as AP-P1 recorded) |
| K04 | update/reinstall/uninstall | **E** | AP-P1 evidence (`k02-k04-excerpts.txt`); not re-run |
| DOC01 | coverage + link checks | **P** (coverage half, exit 0) / **D** (link half, AP-D1) | `audit-docs-coverage.txt` |
| DOC02, DOC03 | walkthrough, policy review | **D** (AP-D2 / AP-D1) | — |
| S01 | SF_Telem 2-h soak | **N** | §9 |
| S02 | AnalysisSample 2-h | **N** | §9 |
| S03 | determinism ×5 | **P** both configs, 9 fixtures, checksums identical across 5 processes and across Debug/Release | `s03-Release/summary.json` (`fa1223a`), `s03-Debug/summary.json` |
| S04 | this matrix, candidate hash, environment, dispositions, goldens | **P** (this report) | — |

### 8.3 Counts

App Platform catalog: 29 IDs — **25 P**, 0 B, 1 N (Y03), 1 D (DOC04), DOC05 = showcase (§7), V05 counted
once. Stability catalog rows: **P** for every retained U/W/G/I row that was run (T01–T03, T06, D01–D06,
L01–L05, P01, R01–R07, C01–C06, N01–N04, X01, K01, K02, S03, S04, H01–H04, DOC01 coverage half);
**B**: T04, K03, D01-genuine-SF-Stable; **N**: T05, N02-drift-2h, S01, S02; **E**: B01–B05, K04;
**D**: DOC02, DOC03, DOC01 link half. **Failed: 0.**

## 9. Pending long runs — exact commands (not run: budget, Kaden 2026-09-19)

All from `C:\dev\Cosmic`, Release binaries at `fa1223a`, nothing else running on the desktop:

```
# Y03 — 2-h PendulumLab soak in the packaged exe (screen switch every 30 s, Reset every 5 min, memory plateau,
# fixed-step drift, Windows job object). No manifest exists yet: AP-04 built the exe + self-test host, the soak
# driver is still to be written (WO-02 memory oracle + WO-10 drift method over the dist\PendulumLab\PendulumLab.exe
# produced by:  powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\apq1-y02.manifest.json -Config Release -TempRoot C:\dev\Cosmic\build\_temp\apq1 )
# S01 — SF_Telem 2-h soak exactly as the stability catalog row states (record/autosave/export, screen switches,
# disconnect/reconnect, final close; separate non-recording segment for the memory plateau):
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo06.manifest.json -Profile pr -Config Release -TempRoot C:\dev\Cosmic\build\_temp\apq1      # D05-two-hour (fake clock, minutes)
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo06.manifest.json -Profile native -Config Release -TempRoot C:\dev\Cosmic\build\_temp\apq1  # D05-native-two-hour (real clock, 2 h)
# S02 — AnalysisSample 2-h (animation/scrub/resize/capture loop + reopen): no 2-h driver exists; X01 is the
# functional half (PASS). Start from:  Run-Acceptance.ps1 -Manifest …\wo10-sample.manifest.json -Config Release -TempRoot …
# N02-drift-2h (WO-10, 55 min):
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo10-drift.manifest.json -Profile release -Config Release -TempRoot C:\dev\Cosmic\build\_temp\apq1
# T05 (30-min controlled, nightly profile):
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo05.manifest.json -Profile nightly -Config Release -TempRoot C:\dev\Cosmic\build\_temp\apq1
```

## 10. Package hashes (Y02 editor path = K02 CLI path, byte-identical)

| File | Size | SHA-256 |
| --- | --- | --- |
| `dist/PendulumLab/PendulumLab.exe` (renamed `CosmicApp.exe`) | 187,392 | `44962cf069b1686540408a96f128b6d4afa4b72eef47374db2ccc51a9bfddeff` |
| `dist/PendulumLab/PendulumLab.dll` (external Release build) | 664,576 | `16d5ed7aba9abe88d9b1bd05ddd6064fe32ac7e8451238dfd41f13d5eb50097c` |
| `dist/PendulumLab/Cosmic.dll` (= `build/Runtime/Release/Cosmic.dll` at `fa1223a`) | 5,238,784 | `3633c4cd73ab20ffffd9b1a7ec7495e5d99c2cb17adae388f193c0d34008e4b2` |

Full 63-path list with sizes and hashes: `y02-Release/y02-package-payload.txt`; CLI list:
`k02-PendulumLab/cli.files.txt`. Goldens: `golden-hashes.txt` (15 files).

## 11. Deferred list

- **AP-D1 / AP-D2** (docs): `tests/check_docs_links.ps1`, `docs/parked-3d/`, `docs/guide/app-authoring.md`,
  roadmap v5, DOC02 walkthrough, DOC03 review, DOC04; the four AP-01 rows in `docs/reference/README.md`
  point at existing chapters on purpose — Kaden's decision, a later week.
- **Y03, S01, S02, N02-drift-2h, T05** — §9 (budget).
- **Tidy remainder** — §3 step 3 list.
- **KI-3 entry text**, **KI-55 clamp decision**, **KI-58** — open per §6.
- `user/README.txt` line-ending parity between `Packager.cpp` and `Stage-AppPackage.ps1` (nit).
- `PhysicsWorld::DebugDraw` no-op and the 3D `SceneRendererSettings` fields still assigned by hosts.

## 12. Staged promotion (for Kaden — NOT executed by this session)

Pre-flight Kaden can run locally first:

```
git -C C:\dev\Cosmic status --short          # only the four untracked paths
git -C C:\dev\Cosmic log --oneline -12       # ends at the AP-Q1 evidence commit on top of fa1223a
git -C C:\dev\Cosmic rev-parse HEAD
```

Push (fast-forward from `origin/main` = `a80cdd3`, which someone pushed at 00:05 −0700; everything after it
is this session's work):

```
git -C C:\dev\Cosmic push origin main
git -C C:\dev\Cosmic tag -a cosmic-app-platform-g5-2026-09-20 fa1223a -m "App Platform G5 qualified SHA (AP-Q1 release report: docs/plans/app-platform-2026-09-18/evidence/AP-Q1/release-report.md)"
git -C C:\dev\Cosmic push origin cosmic-app-platform-g5-2026-09-20
```

CI to expect on the push (`.github/workflows/ci.yml`, `windows-latest`): job **build-and-test** — GL
conformance audit, API reference coverage audit, configure (2D-only), cache-key toolchain id, Debug build,
Release build, test-discovery check (≥ 300 cases; 519 here), unit tests Debug, unit tests Release, upload
`cosmic-2d-test-reports`; then job **acceptance-pr** — restores the build, asserts the 2D tree,
`Run-Acceptance.ps1 -Profile pr` over the U/W manifests (G/I/Q cases appear as `ENVIRONMENT_BLOCKED` with the
capability named), uploads `cosmic-acceptance-pr`. Expect green; the one thing CI cannot reproduce from
here is the render-tests/GPU half, which is blocked there by design. `release.yml` is `workflow_dispatch`
only (`app_name` input; `PendulumLab` is an external consumer, so it ships through the editor/CLI path shown
by Y02/K02, not through that workflow).

## 13. Files in this directory

`release-report.md` (this) · `manifest-table-Release-final.md`, `manifest-table-Debug.md` (rendered by
`Make-ManifestTable.py`) · `manifests-Release/`, `manifests-Release-rerun/`, `manifests-Release-final/`,
`manifests-Debug/` (per-manifest `results.json`, `results.junit.xml`, per-case `*.out.log`/`*.err.log`,
`summary.txt`, driver console logs) · `b06-tidy1-Release/` · `units-*-excerpts.txt`, `render-*-excerpts.txt`,
`golden-hashes.txt`, `audit-*.txt` · `s03-Release/`, `s03-Debug/` (`summary.json` + per-run checksum
material and doctest logs) · `y02-Release/` (package result + payload hashes, run result, `y02-plot-roi.png`,
`y02-frame.png`, editor console) · `k02-PendulumLab/` (report, both file lists, runtime-output list, stage
log) · scripts: `Run-Manifests.ps1`, `Run-S03Determinism.ps1`, `Run-K02PendulumLab.ps1`, `Capture-Window.ps1`,
`Send-Click.ps1`, `Bring-ToFront.ps1`, `Make-ManifestTable.py`.
