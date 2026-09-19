# App Platform — acceptance catalog

Status: test specification, 2026-09-18. No case here has been executed. The execution model, tiers
(U/W/G/I/Q), profiles, evidence contract, isolation rules, deadlines and the provisional numeric bars are
those of [`../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md`](../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md)
and are not restated. Runner: `tests/acceptance/Run-Acceptance.ps1` with per-WO manifests named
`ap<wo>-<group>.manifest.json` (e.g. `ap01-units`, `ap02-gpu`, `ap03-editor`, `ap04-sample`).

Retained from the stability catalog unchanged: K01–K04, DOC01–DOC03, S01–S04, H01–H04, and every
retained-suite expectation (units Debug+Release, the 2D goldens byte-identical). K02 and Y02 add
PendulumLab to the packaged set; DOC01 is now backed by the real link checker.

## Fixtures (in addition to the stability packet's)

| Fixture | Definition |
| --- | --- |
| F-APP | A project scaffolded from `templates/app` by the real editor path, hashed after AP-04; the E-tier host re-scaffolds it fresh per run and compares the tree to the hash (token substitution excluded). |
| F-PENDULUM | Analytic reference for the damped pendulum: `θ(t)` for small angles with `L = 1 m`, `g = 9.80665`, damping `c ∈ {0, 0.05}`, `θ0 = 5°`, `t = i/240` for `i = 0..2400`, double precision, checked in as `tests/fixtures/ap04/pendulum_reference.csv` (`git add -f`; `*.csv` is gitignored). The service's RK4 at `dt = 1/240` must match within the stated bound; the period estimate must match `2π√(L/g)` within 1 %. |
| F-WIDGETS | Deterministic scenes for every widget kind at 320x180 with sentinel colours (AP-02), plus a DataBus pre-filled with known values/history so the same scene renders "preview" and "live" (E05 A/B). |

## Purge

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| B06 | U,W/pr | After AP-05: `grep -rn COSMIC_2D_ONLY Cosmic/src Projects tests` has zero preprocessor uses; none of the §9 Part-A paths exists; `grep -rniE "Renderer3D|Terrain|Voxel|NavMesh|assimp|Recast|EnvironmentMap|ShadowMap" Cosmic/src` finds no identifier; both configs build 0-warn; `CosmicTests --count` equals the recorded pre-purge count minus the deleted TUs' cases; every retained 2D golden byte-identical (hash list); both audits exit 0. The oracle is a script `tests/acceptance/fixtures/Verify-AP05Purge.ps1` and a manifest case. |

## Foundation (AP-01)

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| V01 | U/pr | DataBus: number/bool/string round-trips and coercions; `Set` creates; history ring wraps at capacity (assert oldest dropped, order preserved); `History(window)` boundary inclusive; `Age` before/after `Advance`; `Producer` follows `SetProducer`; `Subscribe`/`SubscribeAny` fire once per `Set` with the stored value; unsubscribe-during-dispatch prevents the call; nested `Set` allowed, depth-65 dropped with one warning; `Clear` keeps subscriptions; non-finite stored; 100,000 `Set` on 100 channels under 50 ms Release (record, not gating). |
| V02 | U,W/pr | ServiceHost with in-exe `CS_SERVICE` registrations: instantiate order (registration then `Order`), `OnAttach` after all constructed and before `BindScene`; `Tick` brackets `Producer`; `BindScene` unsubscribes/subscribes and calls `OnSceneChanged(old,new)`; 50 scene swaps keep the same instances and still deliver `OnSignal`; `Destroy` in reverse order; `Instantiate` re-entry destroys first; `UnregisterModule` strips services; **W**: the WO-07 F-LIFETIME-style DLL fixture registers a service, is loaded/unloaded 20 times through the real `GameModule`/`PlayerLayer` paths with a host-owned bus whose values persist across the reloads and no callback after unload. |
| V06 | U/pr | `Data()` proxy no-ops without a bus and reads/writes with one; flow channel guards for all six ops on number/bool/string, missing channel false with one warning; `"on":"when"` fires at most once per `OnUpdate`, after the signal drain, before timers, never without a guard (`Validate` reports it); `StartAt` enters the named state and falls back to `Start` for an unknown name; `FlowKeyBridge::KeyCodeFor` table, rising-edge per key with an injected probe, unknown names warn once; `KeySignals` distinct list. |

## Widgets (AP-02)

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| V03 | G/gpu | F-WIDGETS: one 320x180 golden per component (value text with/without stale, bar and arc gauge at 0/50/100 %, indicator on/off with textures, plot with 2 channels + a non-finite gap, slider H/V at 0/0.5/1, toggle on/off, hosted-panel frame + placeholder), compared with the existing tolerance plus sentinel ROIs (fill edge pixel, knob centre, plot line pixel); every pre-existing golden byte-identical. |
| V04 | U/pr | Pure helpers: `FormatValue` for one conversion / no conversion / two conversions (literal) / bool / string / missing / stale; `GaugeFill` clamp and non-finite; `SliderValueAt` H/V, `Step` snapping, out-of-rect clamps. `Update` state machine: slider press-drag-release writes the bus each frame and emits `Signal` only when the value changed; toggle release-inside flips and emits; a `UiImage` over a slider does not block it; `Interactable=false` removes it; `Update` returns true over any interactive element; two `Update` calls with the same edges do not double-emit. Serialization: a scene with every widget round-trips through `SceneSerializer` field-exact. |
| V05 | I,G/gpu | Engine half (AP-02): `CollectHostedPanels` returns the resolved rects in canvas order; `Render` in preview mode draws the placeholder. Editor half (AP-03 self-test): in Play a registered `CS_PANEL` draw fn is called with the element's rect (recorded), the ImGui window sits at viewport-offset coordinates (letterboxed presets included), not called in edit mode, unknown name shows the placeholder, and the WO-07 ImGui stack oracle is balanced after each frame. Player half (AP-Q1 via Y02): the same in the packaged PendulumLab. |

## Editor authoring (AP-03)

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| E01 | W,I/release | `AP03AuthoringSelfTest` (env `COSMIC_AP03_SELFTEST=<result.json>`, `COSMIC_AP03_ROOT=<temp>`): New Project with kind `app` → the tree equals F-APP → the viewport renders non-blank (readback: ≥ 1 % of pixels differ from the clear colour) → `BuildScripts` through the real `BuildRunner` succeeds → Play → `m_PlayFlow.CurrentState() == "Home"` → services instantiated (`Count() ≥ 1`) → Stop. Both configs. |
| E02 | W,I/release | Screens ▸ New Screen "Telemetry" with "Create script": `scenes/Telemetry.cscene` exists with canvas + camera + `NativeScript{TelemetryScreen}`; `flows/Main.cflow` gains the state (round-trip byte-stable except the addition); `src/screens/TelemetryScreen.h` from the stub; `Module.cpp` has the include and `CS_SCRIPT(TelemetryScreen)` between the markers; rebuild succeeds; `FlowAsset::Validate()` empty; a project whose `Module.cpp` lacks markers is refused with the documented message and no file changes. |
| E03 | U,I/pr,release | `UiRectGizmo` pure math (handle hit-test, drag → new `OffsetMin/OffsetMax`, snap to 1/8 px and to a 16 px grid, min size 1 px) unit-tested bit-exact; self-test: injected pointer drag moves a selected element by (40, −20) px → exactly one `CommandStack` entry, undo restores the previous offsets, redo re-applies; anchors and pivot untouched; a resize from the bottom-right handle changes only `OffsetMax`. |
| E04 | I,G/release | Replace `UiImage.TexturePath` through the Inspector asset slot (and by drop from the Content Browser) with an imported PNG → the scene saves the new path → after reopen the sentinel pixel of the new texture is present in the viewport readback. |
| E05 | G/gpu | With the F-WIDGETS scene: `Render(bus=nullptr)` (preview) vs `Render(bus)` (live) with the bus holding the preview values produce byte-identical images; with different bus values the diff is confined to the widget ROIs. |
| E06 | U,W,I/release | Homescreen: the template combo lists `App`, `Game`, `Blank` and the Samples buttons list `FlowDemo`, `ForgePong`; each scaffolds, opens, builds and plays (self-test); `Prefs::LoadProjects()` call count over 600 homescreen frames ≤ 3 (probe counter, gated). |
| E07 | W,I/release | Live loop: with Play running on F-APP, the self-test rewrites `src/services/AppService.cpp` on disk (change the published `app.sine` amplitude) → within the build deadline the editor is back in Play on the same flow state with `m_Services` re-instantiated, `app.sine` amplitude changed, the bus history for `app.uptime` still containing samples older than the rebuild, and the Console holding the build line; then an intentional compile error → Play stays stopped, Console shows the error, chip state `Build failed`; then the fix → resumes. No manual action in the sequence. |
| E08 | U,I/pr,release | `SourceLocator` unit: `ForScriptClass` finds the stub file; `ForService` resolves the `CS_SERVICE` file:line; `ForPanel` the `CS_PANEL` file:line; `ForChannel` through `Producer`; `ForSignal` lists every file with the quoted string; unresolved hits carry a reason. Self-test: with `COSMIC_AP03_RECORD_SHELL=1` the Open/Reveal calls are recorded (not launched) and equal the expected absolute paths for a screen script, a service, a panel, a bound widget and a button signal, from the Inspector, the Screens panel, the DataBus panel and the viewport context menu. |
| F01 | W,I/release | Screens panel add/rename/set-start round-trips `.cflow` byte-stable for a v1 file apart from the edited keys; the flow editor's "On" picker lists the new widgets' `Signal` strings; the transition inspector writes `channel` guards and `when` correctly. |

## Templates and sample (AP-04, executed partly by AP-Q1)

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| F02 | U,W/pr | PendulumLab flow driven headlessly (fake loader) and through the player: Home →(start_clicked) Lab →(key:Escape) Home; Lab →(settings_clicked) Settings →(back) Lab; Lab: `when energy < 0.01` pushes `Stopped`, `resume_clicked` pops; a scripted 200-step sequence gives the same state trace on two runs. |
| Y01 | U,W/pr,release | PendulumLab configures and builds standalone from a **clean SDK path** (`-DCOSMIC_SDK_DIR`, no source-relative dependency; the K01 procedure); `test_pendulumlab.cpp` in-exe: RK4 at 1/240 vs F-PENDULUM within `1e-4` rad over 10 s undamped, monotone energy decrease with `c = 0.05`, period estimate within 1 % of `2π√(L/g)`, two runs bit-identical (hash of the published series). |
| Y02 | W,G,I/release | The editor packages PendulumLab through the real File ▸ Package path (`X01` pattern, env `COSMIC_Y02_PACKAGE`); the staged `PendulumLab.exe` runs from a different cwd with `COSMIC_Y02_SELFTEST` and navigates Home → Lab → Settings → Lab, reads `pendulum.angle_deg` changing, verifies the plot ROI and the hosted panel's draw count > 0, writes a result JSON; exit 0. |
| Y03 | Q/release | 2-h PendulumLab soak in the packaged exe with a screen switch every 30 s and a Reset every 5 min: memory plateau per the WO-02 oracle, 0 fixed-step drift over the injected clock (WO-10 method), no hang; a Windows job object bounds the child. |

## Packaging, CI, docs, showcase

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| H05 | W/pr | `ci.yml` job `acceptance-pr` runs `Run-Acceptance.ps1 -Profile pr` over the U/W manifests after the units; the job fails on any FAILED case; G/I/Q cases appear as `ENVIRONMENT_BLOCKED` with the named capability in the uploaded `results.json`, never as passed; the 300-case discovery floor stays. |
| DOC04 | U/pr | `check_docs_links.ps1` green for live tiers; every file under `docs/parked-3d/**` has the §11 banner at line 3 (a script asserts it); no live-tier link into `parked-3d/` without the "(parked 3D)" label; the manifest has no row pointing at a deleted header. |
| DOC05 | Review/release | `docs/showcase/README.md` lists 12 PNG captures (each ≤ 1 MB, 1920x1080 or the editor's window size) with captions: homescreen + template picker; a screen being arranged with the rect gizmo; the Screens panel with a linked script; the Inspector's Open source item; the flow graph of PendulumLab; the DataBus panel live; the Lab screen live (plot, gauge, slider); the hosted ImPlot phase plot; the live-loop status chip after a rebuild; the packaged PendulumLab window; SF_Telem's main screen; the acceptance run summary. Plus the one-line feature list and the 150-word blurb; `README.md` top strip links them. |

## Retained expectations restated for AP-Q1

The full retained suites (units Debug+Release, the 2D goldens, both audits, the link checker, every
`wo*` manifest the stability campaign left green) must be green at the qualified SHA, with the
post-purge counts recorded as the new baseline. Every `ap*` manifest green or `ENVIRONMENT_BLOCKED` with
the prerequisite named. No ID passes by inference.
