# AP-01 execution report — 2026-09-19 (DataBus, app services, flow channels, host wiring, template move; V01 + V02 + V06)

Only AP-01 was executed, directly on `main` on top of the AP-05B commit `84a8075` (nobody else on
the branch; the `ap/p1` worktree `C:\dev\Cosmic-ap-p1` was not touched). The engine foundation
tier of the App Platform packet is in: the host-owned `DataBus` (§1), the `AppService` /
`PanelRegistry` / `ServiceHost` / `CS_SERVICE` / `CS_PANEL` tier with `ModuleRegistry` service
registration and stripping (§2), the `Data()` script proxy, the flow additions (`FlowGuard::Channel`,
`when`, `StartAt`, `KeySignals`, `SetDataBus`, the `lookupChannel` parameter) with the
`FlowKeyBridge` (§5), the `UiSystem` signature changes with a no-op `CollectHostedPanels` (§3 end),
both hosts wired to the §2 frame order (`PlayerLayer` fully, `StarforgeApp` Play without the editor
hosted-panel draw, which is AP-03), and the template tree moved to `assets/templates/game/` (§8).
**V01, V02 (U and both W halves) and V06 PASS in Debug and Release**, through the runner and in the
direct suite; the direct suite is **487/487 in both configs** (454 at `84a8075` + 33 run + 1
skipped-by-default = **34 new cases**); both configs **0 warnings**; both audits **exit 0**; the
retained `wo09-units` (8/8), `wo09-editor` C05-I and `wo07-l02` L02 (real scaffold + Ctrl+B
`BuildScripts` through the moved template) verdicts are in the status table. This session
continued a previous AP-01 session that was cut off by a usage limit with the implementation on
disk and uncommitted; nothing was reset or re-implemented — the tree was reconciled against
`git status`/`git diff`, rebuilt, verified, reported and committed.

## Scope and provenance

- Initial `HEAD`: `84a8075e816f438422e2999618d6740669428f47` ("Remove the COSMIC_2D_ONLY fences;
  the trunk is 2D-only source (AP-05 part B)"), pushed by Kaden. Working tree dirty with the previous
  session's AP-01 implementation (11 new files, 19 modified, 17 staged `git mv` renames of the
  template tree). No branch, worktree, push, stash drop, reset or checkout was made.
- Left untracked and out of every commit: the root `Cosmic - 2D Trunk Consolidation & Acceptance
  Plan.md`, `recordings/`, `docs/plans/2d-stability-2026-09-16/evidence/WO-07/l01-runner-Release/`
  and `docs/plans/app-platform-2026-09-18/work-orders/ORCHESTRATOR.md`.
- Commits are authored and committed as `kdadabhoy <kdadabhoy28@gmail.com>` with no
  `Co-Authored-By`, AI or "Generated with" trailer; only explicit AP-01 paths were staged.
- The runner recorded `dirty=True` with `commit=84a8075` in every `results.json` because the runs
  happened on the uncommitted tree; the commit SHAs are at the end.

## Toolchain and environment

- CMake `C:\Program Files\Microsoft Visual Studio\18\Community\…\CMake\bin\cmake.exe` (VS18 2026
  x64, MSBuild 18.7.8), configured `-A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`
  (`configure.log`; re-configured because `Cosmic/src/data/DataBus.cpp`, `ServiceHost.cpp` and
  `FlowKeyBridge.cpp` are new engine TUs and the engine glob has no `CONFIGURE_DEPENDS`).
- Full builds of every target, Debug and Release: **exit 0, 0 warnings, 0 errors**
  (`build-debug.log`/`.exit`, `build-release.log`/`.exit`; the logs are short because the previous
  session's objects were up to date — only `test_wo05_kiss.cpp` recompiled).
- Reference machine: Windows 11 Education 10.0.26200, real GL context for the W/I cases.

## Baseline facts recorded (and respected)

- `CosmicTests` at `84a8075`: 454 run / 454 passed / 13 skipped in both configs
  (`evidence/AP-05/test-counts.txt`).
- **KI-57 reproduced twice before the clean runs.** The first direct Debug and Release runs both
  failed exactly one case, `WO-06 D01` at `test_wo06.cpp(208)`, because `%TEMP%\wo06\fallback\scene.bin`
  was stale from the previous session's direct run (and the two configs were also running at the
  same time against the same fixed scratch path). Clearing `%TEMP%\wo06` and running the configs
  one after the other gave 487/487 in both. No AP-01 file is involved; KI-57 stays open as written
  (the fix is the one-liner it names).
- Retained manifests that drive the real editor (`wo09-editor`, `wo07-l02`) require `-TempRoot`
  inside the repository (their wrappers throw "fixtures and artifacts must remain inside Cosmic"
  otherwise) — this session used `build/ap01-accept-temp`, as WO-09/WO-10 used `build/wo09-accept-temp`.
  The runner also resolves a relative `-Manifest` against `tests/acceptance/`, so absolute paths were
  passed. Neither is a defect; both are recorded so the next session does not lose the same minutes.
- The retained manifests write their outputs into the WO-07 / WO-09 evidence directories (that is
  where their `-Output` arguments point). After the runs, the 37 tracked files they rewrote under
  `docs/plans/2d-stability-2026-09-16/evidence/{WO-07/l02-runner-Release,WO-09/*}` were restored to
  `HEAD` (`git checkout --` on exactly those paths) and the 7 MB `scratch-*` copy L02 left there was
  removed; the verdicts and result JSONs live here as `retained-*-Release.txt`,
  `retained-l02-result.json` and `retained-c05i-result.json`. No AP-01 working-tree file was reset.

## What was built

All of it was on disk from the previous session; this list is the reconciled, verified content.

1. **`Cosmic/src/data/DataBus.{h,cpp}`** (§1) — `DataValue` (Number/Bool/String, `MakeX`,
   `AsNumber`/`AsString`/**`AsBool`**), `DataSample`, `DataBus` (Set/SetBool/SetString/Set(DataValue),
   Has/Get/GetNumber/GetBool/GetString, Age/LastWriteTime, per-channel ring history with
   `kDefaultHistory = 1024` and `SetHistoryCapacity`/`HistoryCapacity`/`History(window)`,
   `Advance`/`Now`, `SetProducer`/`Producer`, `Subscribe`/`SubscribeAny`/`Unsubscribe` with
   dispatch-safe unsubscribe/subscribe, nested Set with the depth-65 dispatch dropped under one
   warning, `Channels` (sorted)/`Remove`/`Clear` (keeps subscriptions)/`ChannelCount`).
2. **`scripting/AppService.h`** (§2) — `PanelRegistry` (Register replaces; Draw returns false when
   unknown; `SourceOf`; sorted `Names`; `Clear`), `AppContext` (Bus, Panels, ActiveScene, Flow,
   ProjectName, InEditor), `AppService` with the seven virtuals and the protected `Panels()`,
   `CS_PANEL(name, fn)`, `ServiceBuilder<T>::Order`. **`scripting/ServiceHost.{h,cpp}`** —
   `Instantiate(module, ctx)` (registration order stable-sorted by Order; OnAttach after all are
   constructed, before the first BindScene; re-entry destroys first), `BindScene` (unsubscribe old
   bus, subscribe new, `OnSceneChanged(old, new)`), `Tick`/`FixedTick` inside a nesting-safe
   `SetProducer(name)…SetProducer("")` bracket, `DispatchEvent`, `DispatchSignal`, `Destroy`
   (OnDetach in reverse order, delete, unsubscribe, `Panels.Clear()`), `IsInstantiated`, `Names`,
   `DescriptorOf`. **`ModuleRegistry`** — `ServiceDescriptor`, `AddService<T>` (returns the
   builder), `FindService`, `ServiceNames()` / `ServiceNames(module)`, and `UnregisterModule`
   strips the module's services in the same pass as its scripts/components (KI-29 ordering).
   **`ModuleMacros.h`** — `CS_SERVICE(T) … CS_END`.
3. **Data() proxy** — `ScriptHost::SetDataBus(DataBus*)` injected into each instance at Instantiate;
   `DataProxy` on `ScriptableEntity` and `SystemScript` (no-ops/defaults without a bus).
4. **FlowMachine** (§5) — `FlowGuard::Channel` (highest precedence; number/bool/string by the value's
   kind; missing channel or no bus ⇒ false with one warning per guard), `SetDataBus`, `"on": "when"`
   (`TryFireWhen` once per `OnUpdate`, after the signal drain, before timers; at most one per update;
   `Validate` reports a `when` without an `if`), `StartAt` (unknown name warns and falls back to
   Start), static `KeySignals`; `EvaluateFlowGuard(..., lookupChannel)`; JSON save writes `channel`
   only when non-empty and `when` verbatim — v1 and v2 files save byte-identical to the AP-05B
   binary's output (pinned in the V06 JSON case). **`scene/FlowKeyBridge.{h,cpp}`** — `Probe`
   injection (default `Input::IsKeyPressed`), `Bind` (unknown names warn once), `Poll` (one rising
   edge per bound key ⇒ `FeedSignal("key:<Name>")`), `KeyCodeFor` table, `Clear`.
5. **UiSystem signatures** (§3 end) — `DataBus* bus` on `Update`, `const DataBus* bus, bool preview`
   on both `Render` overloads, `UiHostedPanelDraw{Handle, Name, Rect, Scale}`,
   `CollectHostedPanels` with a no-op body. Every existing call site compiles through the defaults;
   the existing UI suites pass unchanged.
6. **Hosts** (§2 frame order, §4 block). `PlayerLayer`: `m_Bus`, `m_Panels`, `m_Services`,
   `m_KeyBridge`; services instantiated after the manifest read and before the flow starts;
   `BindScene` inside `RebindScripts` while the old scene is still alive; `m_Bus.Advance` with the
   unscaled frame delta; `Tick` before the UI/flow/scripts, `FixedTick` before `ScriptHost::FixedTick`;
   hosted panels drawn in `OnImGuiRender` before the pause check with the §4 window flags;
   `m_KeyBridge.Poll(m_Flow)` replaces the hand-rolled Escape edge; `OnDetach` order flow →
   scripts → services → physics/renderer → `UnregisterModule`. `StarforgeApp`: `m_PlayBus`,
   `m_PlayPanels`, `m_PlayServices`, `m_PlayKeyBridge`; the new TU
   `Projects/Starforge/src/StarforgeAppServices.cpp` holds `PlayServicesStart` (InEditor = true,
   before `m_Scripts.Instantiate`, binds the scene), `PlayServicesBindScene`, `PlayServicesStop`
   (scripts, then services; `m_PlayBus` left intact), `PlayServicesAdvanceBus`, `PlayFlowBindKeys`;
   `StarforgeApp.cpp` carries hook lines only (it is at the `/bigobj` limit); `ReloadModule` stops
   Play (existing behaviour) so services die in `StopScene`; `RenderViewport` passes `&m_PlayBus`
   while playing and `nullptr` (preview) otherwise; hosted panels are NOT drawn in the editor (AP-03).
7. **Templates** (§8) — `Projects/Starforge/assets/templates/*` → `templates/game/*` by `git mv`
   (17 files, verbatim); `ScaffoldProjectTo(name, destRoot, kind = "game")` reads
   `templates/<kind>/`; the POST_BUILD sync in `Projects/Starforge/CMakeLists.txt` syncs the keyed
   tree; `ProjectManifest::Kind` is read from `project.cproj` (`"kind"`, default `game`; write and
   picker are AP-03); `tests/test_template_scripts.cpp` includes the moved headers.
8. **`Cosmic.h`** includes `data/DataBus.h`, `scripting/AppService.h`, `scripting/ServiceHost.h`,
   `scene/FlowKeyBridge.h`; four rows added to `docs/reference/README.md` (see deviation D4).
9. **Tests** — `tests/test_databus.cpp` (13 cases, suite `AP-01 V01 DataBus`),
   `tests/test_servicehost.cpp` (12 cases: 9 `V02` U cases, `V02 W` 20 GameModule reloads in-exe,
   `V02 W host` 20 PlayerLayer reloads through a real `Application` — skipped by default, run by the
   manifest with `-NoSkip` — and the `V06 Data proxy` case), `tests/test_flow_channels.cpp` (9 cases,
   suite `AP-01 V06 Flow channels`), the fixture DLL `tests/AP01ServiceFixture.cpp` +
   `tests/AP01ServiceReport.h` (a real `CS_MODULE` registering a `CS_SERVICE` that registers a
   `CS_PANEL` from `OnAttach`, plus a `CS_SCRIPT`), `tests/CMakeLists.txt` (three TUs, the real
   `Projects/Starforge/src/GameModule.cpp` compiled into the exe, the `AP01ServiceFixture` target on
   the WO-07 fixture pattern), and `tests/acceptance/manifests/ap01-units.manifest.json`
   (V01, V02-U, V02-W-PLAYER, V06 filtered by test-suite / test-case name through `Run-WO10Case.ps1`).

## Final-pass method

1. Reconcile the dirty tree against the hand-off (`git status --short`, `git diff --stat`); nothing
   missing, nothing extra.
2. Re-configure; full Debug and Release builds; assert 0 warnings from the logs.
3. `CosmicTests.exe` Debug, then Release, one at a time with `%TEMP%\wo06` cleared (KI-57).
4. `ap01-units` through `Run-Acceptance.ps1` in Release and Debug (the W-PLAYER case needs the GPU).
5. Retained: `wo09-units`, `wo09-editor` (C05-I: `NewProjectAt` scaffold from `templates/game/` +
   Play/Stop inside the real editor) and `wo07-l02` (L02: scaffold + the REAL Ctrl+B path
   `BuildScripts → BuildRunner → cmake configure + build` + reload cycles) in Release.
6. Both audits; refresh the evidence txt files.
7. Report; three commits.

## Acceptance-case status (final pass, final binaries)

| Case | Debug | Release | Evidence |
|---|---|---|---|
| V01 DataBus (13 cases) | PASS | PASS | `v01-{Debug,Release}/`, `ap01-units-runner-*.txt` |
| V02-U ServiceHost + 20 GameModule reloads (10 cases) | PASS | PASS | `v02u-*/` |
| V02-W-PLAYER 20 real-PlayerLayer reloads (W, `-NoSkip`, isolated CWD) | PASS | PASS | `v02w-*/V02-W-PLAYER.out.log` |
| V06 flow channels + Data proxy (10 cases) | PASS | PASS | `v06-*/` |
| Direct suite `CosmicTests.exe` | 487/487, 14 skipped | 487/487, 14 skipped | `test-counts.txt` (the 25 MB full logs stay local as `units-*.log`) |
| Retained `wo09-units` (8 cases) | — | 8/8 PASS | `retained-wo09-units-Release.txt` |
| Retained `wo09-editor` C05-I (scaffold + Play in the real editor) | — | PASS | `retained-wo09-editor-Release.txt`, `…/WO-09/c05i-Release/` |
| Retained `wo07-l02` L02 (scaffold + Ctrl+B `BuildScripts` + reloads) | — | PASS (52 cycles, 53 builds, 50 rebuild+reload) | `retained-wo07-l02-Release.txt` |
| `check_gl_conformance.ps1` / `check_docs_coverage.ps1` | exit 0 | exit 0 | `audit-*.txt` |

Runner summaries: `ap01-units` `4 passed / 0 failed / 0 env-blocked (of 4)` in both configs.

### V01 — DataBus

Every §1 bullet is a `CHECK` (see the case list in `test_databus.cpp`): coercions, Set-creates,
ring wrap at capacity with order preserved and the capacity rules, `History(window)` inclusive
boundary, `Age` before/after `Advance` (negative and non-finite deltas ignored), `Producer` follows
`SetProducer`, one callback per Set with the stored value, unsubscribe-during-dispatch prevents the
call and subscribe-during-dispatch misses the in-flight value, nested Set allowed with the depth-65
dispatch dropped under ONE warning, `Clear` keeps subscriptions, non-finite stored as written, every
method safe on an empty bus. Timing (MESSAGE, not asserted): 100,000 Set on 100 channels =
**65.4 ms Debug / 2.78 ms Release** (the contract's 50 ms Release bar is met with room).

### V02 — services

U part: instantiation order is registration order stable-sorted by `Order`; `OnAttach` after all are
constructed and before `BindScene`; the producer bracket is nesting-safe; `BindScene` unsubscribes
the old bus and calls `OnSceneChanged(old, new)`; 50 scene swaps keep the same instances and still
deliver `OnSignal`; `Destroy` reverses; `Instantiate` re-entry destroys first; `UnregisterModule`
strips services; `PanelRegistry` and the `CS_PANEL` call site; `DispatchEvent` in order.

W part (the reload proof), two halves:
- **GameModule half** (in the ordinary suite): `AP01ServiceFixture.dll` loaded/unloaded 20 times
  through the real `Projects/Starforge/src/GameModule.cpp` (`LoadLibrary` + `CosmicModule_Register`,
  `UnregisterModule` + `FreeLibrary`) with a test-owned `DataBus` whose values, history and
  producers persist across every reload. Tally: `reloads=20 updates=60 producerSeen=60
  pingAfterUnload=0 signalsAfterUnload=0`; `OnDetach` is stamped before the DLL's static destructor;
  no callback after unload while the same bus/scene paths still reach exe listeners.
- **PlayerLayer half** (`V02 W host`, real `Application`, `TransitionToLauncher` /
  `TransitionFromLauncherToWorkspace`, GL context): 20 cycles; every cycle the real `PlayerLayer`
  instantiates the module's service after the manifest read, ticks it inside the producer bracket,
  destroys it in `OnDetach` before `UnregisterModule` and before `FreeLibrary` (one shared sequence
  checked by the fixture's report), no callback after unload, the `ModuleRegistry` clean after the
  last unload. Tally: `cycles=20 updates=80 producerSeen=80 orderFailures=0`. The fixture has no
  scene, so every cycle logs `could not load startup scene` — expected; the case is about the service
  lifecycle and the layer runs on without a scene as the runtime does.

### V06 — flow channels, key bridge, Data proxy

All six ops on number/bool/string channels; missing channel and bus-less machine false with one
warning per guard; `when` fires at most once per `OnUpdate`, after the drain, before timers, never
without a guard (`Validate` reports it); `StartAt` + fallback; `KeyCodeFor` table; `KeySignals`
distinct first-occurrence; the bridge feeds one signal per rising edge per key over an injected probe
and warns once for unknown names; v1 and v2 files save byte-identical to the pre-AP-01 binary,
`channel` only when non-empty, `when` verbatim; `Data()` no-ops without a bus and reads/writes with one
on both `ScriptableEntity` and `SystemScript`.

## Non-vacuity (failing-before)

Recorded by the previous session in `failing-before/` (Debug; mutated implementation → observed
failure → restored; not redone here, per the hand-off). What each file shows:

| File | Mutation observed through the failures | Result |
|---|---|---|
| `v01-databus-mutated-Debug.txt` | coercion / history storage broken (`GetNumber("s") == 3.75`, `AsNumber()` of `"  12.5xyz"`, `h[0].Value == 3.0` fail) | 8 of 10 selected cases failed, 39 assertions |
| `v01-databus-mutated-clear-nonfinite-Debug.txt` | `Clear` dropping subscriptions and non-finite values not stored (`hits == 4`, `isnan(GetNumber("x"))` fail) | 2/2 cases failed, 9 assertions |
| `v02-servicehost-mutated-Debug.txt` | `Order` sort removed (`names[0] == "SvcB"`, `g_Log[0] == "ctor:B"` fail) plus bracket/stripping | 11/11 cases failed, 192 assertions |
| `v02w-playerlayer-mutated-Debug.txt` | producer bracket and `UnregisterModule` stripping removed on the PlayerLayer path (`producerSeen == updates` with `producerSeen=0`, `FindService("AP01Service") == nullptr` fail) | 1/1 failed, 2 assertions |
| `v06-flow-mutated-Debug.txt` | channel comparison ops broken (`eval("n", "!=", N(6))`, `"<"`, `">"` fail) | 9/9 cases failed, 38 assertions |

## Contract deviations

The exact differences between §1/§2/§5 as written and what landed (also carried as notes in the §13
rows); AP-Q1 updates the contract:

- **D1 (§1)** `DataValue::AsBool() const` added (Number → finite and non-zero; String → `"true"` /
  `"1"`). The contract lists only `AsNumber`/`AsString`, but `GetBool` needs a coercion and every
  consumer (AP-02 widgets, guards) would otherwise re-implement it.
- **D2 (§2)** `ServiceDescriptor` is declared in `scripting/ModuleRegistry.h`, not `AppService.h`:
  `ModuleRegistry::AddService<T>` has to store it and `AppService.h` includes `ModuleRegistry.h`, so
  the other direction would be circular. `ServiceBuilder<T>` stays in `AppService.h` as specified;
  `ModuleRegistry.h` forward-declares it and the template body only needs the complete type where
  `CS_SERVICE` expands (a module `.cpp` via `<Cosmic.h>`).
- **D3 (§5)** `EvaluateFlowGuard`'s new parameter is
  `const std::function<bool(const std::string&, DataValue&)>& lookupChannel = {}` (out-parameter
  form, so a missing channel and a present-but-non-finite value are distinguishable); the contract
  named the parameter and its default but not its type.
- **D4 (WO prompt step 8)** the four `docs/reference/README.md` rows point at
  `../guide/scripting.md` (DataBus, AppService, ServiceHost) and `../guide/flow-and-story.md`
  (FlowKeyBridge), not `../guide/app-authoring.md`: `check_docs_coverage.ps1` rule 4 fails a row
  whose chapter file does not exist, and `app-authoring.md` is written by AP-D2. AP-D2 should
  re-point the four rows when the chapter lands (one line each).
- **No deviation** from the §2 frame order, the §4 block, the `UiSystem` signatures, the §8 move
  (`kind` is read only, as allowed) or the `FlowMachine` / `FlowKeyBridge` public surface.

## Defects found

- None in AP-01 code. KI-57 (pre-existing `test_wo06.cpp` D01 scratch hygiene) reproduced as
  described under baseline facts; no new KI registered (the next free number stays KI-59).

## Notes, limits and honest caveats

- The hosted-panel block in `PlayerLayer::OnImGuiRender` is exercised only by compilation and the
  20-cycle runs (no scene registers a `UiHostedPanel` yet); its behaviour is proven by V03–V05 in
  AP-02/AP-03, and the editor does not draw hosted panels until AP-03.
- `UiSystem::CollectHostedPanels` is a no-op by contract in AP-01; `bus`/`preview` are accepted and
  unused until AP-02 gives the bound widgets bodies.
- `ProjectManifest::Kind` is read only; `ScaffoldProjectTo` takes `kind` but every caller passes the
  default `"game"` until AP-03's picker.
- Running the direct suite for two configs at the same time is not safe (KI-57's fixed scratch
  path); the acceptance runner is unaffected because it redirects `TEMP` per run.
- `test_servicehost.cpp` compiles `Projects/Starforge/src/GameModule.cpp` into the test exe: the
  thing under test is the loader the editor ships, not a copy.
- The failing-before proofs are the previous session's recorded runs; this session did not
  re-mutate the implementation (the hand-off said not to redo them).

## Files

- Engine: `Cosmic/src/Cosmic.h`, `Cosmic/src/data/{DataBus.h,DataBus.cpp}`,
  `Cosmic/src/scripting/{AppService.h,ServiceHost.h,ServiceHost.cpp,ModuleMacros.h,ModuleRegistry.h,
  ModuleRegistry.cpp,ScriptHost.h,ScriptHost.cpp,ScriptableEntity.h}`,
  `Cosmic/src/scene/{FlowMachine.h,FlowMachine.cpp,FlowKeyBridge.h,FlowKeyBridge.cpp}`,
  `Cosmic/src/scene/ui/{UiSystem.h,UiSystem.cpp}`.
- Hosts + templates: `Cosmic/src/layers/{PlayerLayer.h,PlayerLayer.cpp}`,
  `Projects/Starforge/{CMakeLists.txt,src/ProjectManifest.h,src/StarforgeApp.h,src/StarforgeApp.cpp,
  src/StarforgeAppServices.cpp}`, `Projects/Starforge/assets/templates/game/**` (17 renames),
  `tests/test_template_scripts.cpp` (include paths).
- Tests + docs: `tests/{test_databus,test_servicehost,test_flow_channels,AP01ServiceFixture}.cpp`,
  `tests/AP01ServiceReport.h`, `tests/CMakeLists.txt`,
  `tests/acceptance/manifests/ap01-units.manifest.json`, `docs/reference/README.md`,
  `docs/plans/app-platform-2026-09-18/01-Design-Contracts.md` (§13 rows), this directory
  (`*.log` files are gitignored; the runner output directories `v01-*`, `v02u-*`, `v02w-*`, `v06-*`
  carry `children.json` and the golden hashes; their `.out.log`/`.err.log` are gitignored and stay local).

## Local commits (not pushed — Kaden pushes)

1. `a12cf27` — engine: DataBus, AppService/ServiceHost/CS_SERVICE, ModuleRegistry services,
   Data() proxy, flow channels/when/StartAt/KeySignals, FlowKeyBridge, UiSystem signatures, Cosmic.h.
2. `6904112` — hosts + templates: PlayerLayer and StarforgeApp wiring, StarforgeAppServices.cpp,
   the `templates/game/` move, ScaffoldProjectTo/POST_BUILD/ProjectManifest::Kind, the template
   test include.
3. The evidence commit that carries this report, the three test TUs, the fixture, the CMake list,
   the manifest, the reference rows and the §13 rows.
