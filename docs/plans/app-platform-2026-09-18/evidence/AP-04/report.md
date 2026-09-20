# AP-04 execution report — 2026-09-19 (template kinds, samples on disk, PendulumLab, headless tests)

Only AP-04 was executed, in the worktree `C:\dev\Cosmic-ap-04` on branch `ap/04` (lane rule L1),
with `$env:COSMIC_SDK = 'C:\dev\Cosmic-ap-04'` in every shell. The three template kinds
(`game`, `blank`, `app`), the two captured samples (`FlowDemo`, `ForgePong`) and the PendulumLab
showcase project are on disk; every template scene loads through `SceneSerializer` with the
contract's §3/§5 reflected names; every kind scaffolds through the real `ScaffoldProjectTo`, opens
through `OpenProjectPath` and its scaffolded module builds standalone; PendulumLab builds from a
clean SDK path (**Y01 PASS**, Debug + Release) and its physics/flow units (**Y01-U, F02-U PASS**,
both configs) run through the real `ServiceHost` / `FlowMachine` / `ScriptHost`. The Y02 self-test
host exists and was smoke-run through the real player: navigation, bus sampling and the producer
tag pass; the two AP-02-dependent checks (hosted-panel draw count, plot ROI) fail **as expected
before AP-02 lands** and are left for AP-Q1. `CosmicTests` **501 passed / 14 skipped** in both
configs (487 + the 14 new cases); both configs **0 warnings**; both audits **exit 0**. No engine
defect was found; no KI was registered (next free stays **KI-59**).

## Scope and provenance

- Base: `main` at `e01f0a0ef0497f322307f77988f9de6bdfd8efb6` (AP-05 + AP-01 + AP-P1 merged);
  `git worktree add ..\Cosmic-ap-04 -b ap/04 main`. `main` was still at `e01f0a0` at land time, so
  the L2 rebase was a no-op (**rebased onto `e01f0a0`**; the integrator re-rebases after AP-02).
- `C:\dev\Cosmic` (main), `engine-3d`, `cosmic-pre-2d-2026-09-16`, the untracked root plan file and
  `recordings/` were never touched. Nothing pushed. The worktree is left in place (L3).
- Files edited are exactly the AP-04 **Owns** set (§10) plus the two **May touch** files
  (`tests/CMakeLists.txt`: TUs + fixture defines; root `CMakeLists.txt`: skip list only) and the
  §13 rows in `01-Design-Contracts.md` (L5). `Projects/Starforge/src/StarforgeApp.cpp` carried two
  **temporary env-gated hooks** (`COSMIC_AP04_CAPTURE`, `COSMIC_AP04_SCAFFOLD`) that were reverted
  with `git checkout --` before any commit — `git status` shows it clean.
- Commits are authored and committed as `kdadabhoy <kdadabhoy28@gmail.com>`, no trailer.

## Toolchain and environment

- CMake 4.3.1-msvc1 (VS-bundled), VS 18 2026 x64, MSVC 19.51.36248; configured
  `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`; effective cache: `COSMIC_2D_ONLY:BOOL=ON`,
  `COSMIC_BUILD_TESTS:BOOL=ON`, `COSMIC_SKIP_PROJECTS:STRING=AnalysisSample;PendulumLab`
  (`--> Skipping game projects: AnalysisSample;PendulumLab`). Re-configured after adding the test
  TUs and the skip-list change.
- Windows 11 Education 10.0.26200, real GPU (the editor and the player ran windowed for the
  capture, scaffold and Y02 smoke steps). `py -3` = Python 3.14 (used for the F-PENDULUM generator).

## What was built

**1. Samples captured (before anything else).** Starforge (Debug) was built in the worktree, then
run once with the temporary `COSMIC_AP04_CAPTURE` hook calling the real `BuildFlowDemo()` and
`BuildForgePong()` (`AP04_CAPTURE flow=1 pong=1`). The generated trees under
`build/Runtime/Debug/assets/projects/{FlowDemo,ForgePong}` were copied to
`Projects/Starforge/assets/templates/samples/{FlowDemo,ForgePong}` with the project name replaced by
`@PROJECT_NAME@` in text files only (`hit.png` byte-identical). Every captured file's SHA-256 and
whether it was kept is in `sample-capture-provenance.json` (39 files). Deliberate trims, recorded
here: both builders scaffold from the *game* template first, so the captured trees carried the full
game-template script set and the 3D `scenes/Main.cscene` (referenced by neither flow) — FlowDemo is
the zero-code sample and keeps no scripts (markers-only `Module.cpp`); ForgePong keeps exactly the two
scripts its `Game.cscene` names (`PaddleController`, `PongBall`); the unreferenced `Main.cscene` was
dropped from both. `kind = "game"` and the `CS_SCREENS` markers were added.

**2. Templates.** `game/`: `scenes/Main.cscene` is now one flat-colour sprite + an orthographic
primary camera (JSON); `HoverController.{h,cpp}` (the 3D Hover-Cube script) and `WalkController.h`
(XZ character walker) removed; `BouncingBall`, `PidController`, `PhysicsBall`, `PaddleController`,
`PongBall` kept; `StoryUiBinding` registered in `Module.cpp` with its include; `kind = "game"`;
markers. `blank/`: `kind = "blank"`, canvas + camera scene, markers-only `Module.cpp`, the game
`CMakeLists.txt`. `app/`: exactly §8 — manifest (`kind = "app"`, `startup_flow`, `fixed_dt_hz = 60`),
`flows/Main.cflow` (Home → Dashboard → Settings, `key:Escape` back, `@quit` from Home),
`scenes/{Home,Dashboard,Settings}.cscene` using `UiValueText`/`UiGauge`/`UiIndicator`/`UiPlot`/
`UiSlider`/`UiToggle`/`UiButton`/`UiImage`/`UiHostedPanel` with the §3 names and fields and the
game-ui anchor idioms, `src/services/AppService.{h,cpp}` (publishes `app.uptime`/`app.sine`/
`app.counter` each tick, handles `counter.increment`/`counter.reset`/`settings.defaults`,
`CS_PANEL("Diagnostics")` drawing three `ImGui::Text` lines), `src/screens/{Home,Dashboard,Settings}
Screen.h` from the §5 stub shape, `Module.cpp` with `CS_SERVICE(AppService)` + three `CS_SCRIPT`s
between the markers, `assets/ui/{panel,logo}.png` (generated, 186/302 bytes, no token), `README.md`.

**3. PendulumLab** (`Projects/PendulumLab`, external consumer, in `COSMIC_SKIP_PROJECTS`, its
CMakeLists refuses `add_subdirectory`): `PendulumService` (RK4 via `IntegrateRK4` at the host's fixed
step — `fixed_dt_hz = 240`; `settings.length/gravity/damping` from the bus plus `settings.theta0_deg`
and `settings.small_angle`, seeded when missing; publishes `pendulum.angle_deg/omega/energy/
period_est/running` + `pendulum.phaseplot_draws`; handles `pendulum.start/stop/reset/nudge`,
`startstop_clicked`, `settings.defaults`; `CS_PANEL("PhasePlot")` ImPlot scatter over the bus
history), screens Home/Lab/Settings + the `Stopped` overlay, `LabScreen.h` placing pivot/rod/bob
sprites from the bus every frame, `HomeScreen.h` animating the title, `flows/Main.cflow` with the
channel-guarded `when` (`pendulum.energy < 0.01` → push `Stopped`; `resume_clicked` → `@pop`) and
`key:Escape`, `project.cproj` `kind = "app"`, `README.md`, and `Y02SelfTestService`
(`src/Y02SelfTest.{h,cpp}`, env `COSMIC_Y02_SELFTEST=<result.json>`, `COSMIC_Y02_OUTPUT`).

**4. Tests.** `tests/fixtures/ap04/pendulum_reference.csv` (F-PENDULUM, 2401 rows, generated by the
checked-in `generate_pendulum_reference.py` via `py -3`; `Generate-PendulumReference.ps1` is the
entry point with a PowerShell-5.1 fallback; `git add -f`; SHA-256
`19395fa1baa19efeb1347dd0624740d777a14f6edfba20d4a36d4515609526fc`). `tests/test_pendulumlab.cpp`
(5 Y01 + 3 F02 cases) compiles the sample's real service TU in-exe. `tests/test_template_scripts.cpp`
extended (6 cases: game + app template scripts, the app `AppService` on a bus, the scene-validation
suite, the flow validation, kind/markers/token). `tests/CMakeLists.txt`: TUs + fixture defines.

**5. Y01 wrapper + manifests.** `tests/acceptance/fixtures/Run-AP04Sample.ps1` copies the sample
outside the tree, configures/builds it against `-DCOSMIC_SDK_DIR`, checks the DLL + exports, writes
`commands.txt` and `result.json`; manifests `ap04-units` (Y01-U, F02-U, E01-U) and `ap04-sample` (Y01).

## Acceptance-case status

| ID | Config | Result | Evidence |
| --- | --- | --- | --- |
| Y01-U (physics units) | Debug, Release | **PASS** 5/5 cases, 42 assertions | `y01u-{cfg}/children.json`, `units-runner-excerpts.txt` |
| F02-U (flow headless) | Debug, Release | **PASS** 3/3 cases, 50 assertions | `f02u-{cfg}/children.json` |
| E01-U (template content) | Debug, Release | **PASS** 6/6 cases, 855 assertions; 15 scenes, 35 widget blocks | `e01u-{cfg}/children.json` |
| Y01 (standalone build, clean SDK path) | Release | **PASS** configure 0, build 0, 0 warnings outside imgui/implot, DLL `3E67E97D…`, 3 exports, 31.8 s | `y01-Release/result.json`, `commands.txt` |
| Y01 | Debug | **PASS** DLL `038CD138…`, 18.8 s | `y01-Debug/result.json` |
| Scaffold + open, all five kinds | Debug editor | **PASS** `scaffold=1 open=1` for game/blank/app/samples/FlowDemo/samples/ForgePong | `build-test-excerpts.txt` |
| Scaffolded modules build standalone | Debug | **PASS** 5/5 DLLs, 0 warnings | `build-test-excerpts.txt` (`SCAFFOLD_BUILD …`) |
| Y02 (self-test host through the player) | Release smoke | **host works; 2 expected FAILs pre-AP-02** (see below) | `y02-smoke-Release/result.json`, `y02-plot-roi.png` |
| Y03 | — | not run (AP-Q1) | — |
| Retained: CosmicTests | Debug, Release | **PASS** 501 / 0 failed / 14 skipped (both) | `build-test-excerpts.txt` |
| Audits | — | `check_gl_conformance` exit 0, `check_docs_coverage` exit 0 ("clean") | `audit-*.txt` |

**Y02 smoke** (`CosmicApp.exe --project PendulumLab`, Release DLL from the Y01 build, run from the
runtime dir): the flow starts at `Home`; `start_clicked` → `Lab` in 1 frame; 3637 frames sampled,
`pendulum.angle_deg` swept −5.000..5.000°, `pendulum.running = 1`, energy 0.0373, producer
`PendulumService`; `Settings` → `Lab` → `key:Escape` → `Home`, trace `Home,Lab,Settings,Lab,Home`.
FAIL 1: `pendulum.phaseplot_draws = 0` — `UiSystem::CollectHostedPanels` is AP-01's no-op body until
AP-02. FAIL 2: the `Plot` element is not returned by `CollectElements` (UiPlot unregistered), so the
host resolved its rect from the `RectTransform` ([666 113 1265 345] in 1280×664) and found 0
line-colour pixels — no UiPlot renderer yet. The captured ROI PNG shows the clear colour. Both
checks become live once AP-02 lands; AP-Q1 runs Y02 against the packaged exe.

## Scene-validation table (E01-U; loader = `SceneSerializer::Load`, both configs)

| Scene | Entities checked | Widget blocks | Scripts named |
| --- | --- | --- | --- |
| game/Main | Camera(ortho), Sprite(SpriteRenderer) | 0 | — |
| blank/Main | Camera, Canvas | 0 | — |
| app/Home | Canvas(+UiImage, NativeScript), Logo, Title, Uptime(UiText+UiValueText), 3 buttons | 1 | HomeScreen |
| app/Dashboard | HeaderUptime, PlotFrame, Plot(UiPlot), SineGauge, CounterGauge, CounterValue, CounterHigh(UiImage+UiIndicator), 2 counter buttons, Diagnostics(UiHostedPanel), Back | 7 | DashboardScreen |
| app/Settings | 3 sliders (+ value texts), AutoToggle(UiImage+UiToggle), Defaults, Back | 8 | SettingsScreen |
| samples/FlowDemo/{MainMenu,Game,Pause} | as generated (perspective camera on the menu screens, canvas "HUD" in Game) | 0 | — |
| samples/ForgePong/{Menu,Game,Win} | as generated; Game names PaddleController ×2, PongBall; HitFx SpriteAnimation | 0 | PaddleController, PongBall |
| PendulumLab/Home | Title, Period(UiValueText), 3 buttons | 1 | HomeScreen |
| PendulumLab/Lab | Pivot/Rod/Bob sprites, Running(UiIndicator), Angle/Omega values, EnergyGauge, Plot(UiPlot), PhasePlot(UiHostedPanel), 5 buttons | 6 | LabScreen |
| PendulumLab/Settings | 4 sliders (+ value texts), SmallAngleToggle, Note, Defaults, Back | 10 | SettingsScreen |
| PendulumLab/Stopped | Canvas(+UiImage), Energy(UiValueText), Resume | 1 | StoppedScreen |

Every widget field name in every block is in the contract's §3 list; every `UiValueText` has a
sibling `UiText`, every `UiToggle`/`UiIndicator` a sibling `UiImage`; every `UiButton.Signal` is
non-empty. **How the serializer treats the unregistered §3 components in this worktree:** AP-02's
structs are not registered here, so `SceneSerializer::LoadEntityComponents` stores each block
verbatim in `OpaqueComponentsComponent::Blocks` (name + JSON) and re-emits it on save — the scenes
load without error (35 blocks "via verbatim preservation pending AP-02"). The test checks presence
through that path when a name is unregistered and through the live component (`TypeDescriptor::Has`)
plus `FindField` for every JSON key once it is registered, so after AP-02 lands the same test becomes
the field-name gate. No other opaque block exists in any scene (no 3D leftovers).

## Non-vacuity

`PendulumService::Step` was replaced once by an explicit Euler step (`s + k1 * dt`); Debug
`AP-04 Y01*`: max |θ − ref| = **1.966e-2 rad** (bound 1e-4) and |ω − ref| = 5.88e-2, **1055 / 1054**
energy increases under damping (bound 0), both damped-reference checks failed → 2 of 5 cases
failed, 7 assertions. Restored (diff empty), rebuilt, all green. The measured passing numbers: max
|θ − ref| = **6.246e-8 rad**, max |ω − ref| = 2.043e-7 rad/s (both configs).

## Measured numbers

- RK4 (small-angle) vs F-PENDULUM over 10 s at 1/240: 6.246e-8 rad; damped (c = 0.05) small-angle vs
  the damped reference: < 1e-4 (same bound, passes); energy after 10 s at c = 0.05 within
  (0.55, 0.65)·E₀ (e^{−0.5} = 0.607); period estimate within 1 % of 2π√(L/g) for both models; L = 4
  doubles the estimate within 1 %. Two runs FNV-1a-identical and `memcmp`-identical.
- Builds: Debug from scratch 0 warnings; Release from scratch 0 warnings; final incremental full
  builds 0 warnings. CosmicTests: 23,402,723 (Debug) / 23,461,708 (Release) assertions.
- Y01 standalone: Release 31.8 s, Debug 18.8 s (imgui/implot compiled into the module).

## Caveats and notes for the integrator

- **Contract additions (not deviations):** PendulumLab reads two extra bus channels,
  `settings.theta0_deg` (5) and `settings.small_angle` (false). The latter exists because the
  F-PENDULUM reference is the *linearised* solution: at θ₀ = 5° the full `sin θ` model drifts
  ~1.3e-3 rad from it over 10 s (period ratio 1 + θ₀²/16), so the 1e-4 bound is only meaningful for
  the same ODE. The default in-app model is the full one; the tests set the toggle. PendulumLab also
  publishes `pendulum.phaseplot_draws` (the Y02 host reads it) and handles `startstop_clicked`.
- **Widget field names needed and not found:** none — every field the scenes use is in §3.
- The `app` template's service is named `AppService` per §8; inside a TU that opens
  `using namespace Cosmic` the name is ambiguous with `Cosmic::AppService` (the template's
  `Module.cpp` does not open the namespace; the test qualifies).
- `FlowMachine::Enter` runs `onEnter` emits on the *new* scene's bus before the host has re-bound
  services/scripts to it, so a flow `onEnter: emit` cannot start the pendulum; `LabScreen::OnStart`
  emits `pendulum.start` instead. Engine behaviour, not changed (AP-01's lane).
- `StarforgeApp::OpenProject` opens `scenes/Main.cscene` when present, else a new empty scene, so an
  `app`-kind project (or a sample) opens with an empty scene until AP-03's Screens/startup handling —
  the scaffold+open evidence shows `entities=0` for app/FlowDemo/ForgePong for that reason.
- The samples are the code builders' output as of `e01f0a0` (perspective camera on FlowDemo's menu
  screens, `Environment` component). The homescreen buttons still call the builders until AP-03.
- `pendulum.period_est` reads 0 until the second upward zero crossing (3T/4 + T ≈ 3.5 s at L = 1).
- Y02 through the packaged exe, and Y03, were not run (AP-Q1).

## Files

Templates: `Projects/Starforge/assets/templates/{game,blank,app,samples/FlowDemo,samples/ForgePong}/**`.
PendulumLab: `Projects/PendulumLab/**`, root `CMakeLists.txt` (skip list). Tests:
`tests/test_pendulumlab.cpp`, `tests/test_template_scripts.cpp`, `tests/CMakeLists.txt`,
`tests/fixtures/ap04/{generate_pendulum_reference.py,Generate-PendulumReference.ps1,pendulum_reference.csv}`,
`tests/acceptance/fixtures/Run-AP04Sample.ps1`, `tests/acceptance/manifests/ap04-{units,sample}.manifest.json`.
Evidence: this folder (`*.log` gitignored; excerpts and JSON committed). Register: §13 rows in
`01-Design-Contracts.md`.

## Local commits

Appended below after committing (`ap/04`, base `e01f0a0`).

- `2f633cd` Fill the template kinds and put the samples on disk (AP-04)
- `2d1b6b2` Add PendulumLab, the App Platform showcase project (AP-04)
- `3bb1d76` Test the templates and PendulumLab headlessly; F-PENDULUM fixture; AP-04 evidence (AP-04)
- (this line) the report amendment recording the three SHAs above; `git rebase main` at land time was a no-op (`main` = `e01f0a0`).
