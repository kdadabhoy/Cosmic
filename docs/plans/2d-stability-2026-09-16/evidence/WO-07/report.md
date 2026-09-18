# WO-07 execution report — 2026-09-17 (Part I: KI-1, L01, L03) + 2026-09-18 (Part II: L02, L04, L05, P01)

Only WO-07 was executed, directly on `main` (main-only campaign, D-WORKFLOW). **Part I**
(2026-09-17) covers the confirmed **KI-1** editor snap-chip crash (fixed, with real
failing-before/passing-after in Debug **and** Release, driven through the WO-04
acceptance runner), L01 and L03. **Part II** (2026-09-18, appended below) delivers the
remaining breadth — L02, L04, L05 and P01 — through the same runner in both configs, with
nine further defects found, registered and fixed along the way. Where a case's prerequisite is genuinely absent it is marked
`ENVIRONMENT_BLOCKED` with the exact prerequisite named; nothing missing is counted
as a pass.

## Scope and provenance (Part I)

- Initial + final local `HEAD`: `6845fecebb4c767913d0f1b03644e23109f57add`; local
  `origin/main` observed at the same SHA (the handoff's `0435d3c` was already
  superseded by a push). No branch, worktree, push, preservation-ref move, tag
  change, or golden regeneration was performed. `engine-3d` and the
  `cosmic-pre-2d-2026-09-16` tag were not touched.
- The only pre-existing untracked file, the protected root plan
  `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md`, was never staged, moved or
  overwritten.
- Every WO-07 commit uses author **and** committer `kdadabhoy <kdadabhoy28@gmail.com>`
  with no `Co-Authored-By`, AI, or "Generated with" trailer.

## Toolchain and environment (both parts)

- CMake: the VS-bundled `…/Microsoft/CMake/CMake/bin/cmake.exe` (4.3.x), VS18 2026 x64,
  configured with `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON` (render tests also on
  in the existing cache). Reconfigured after adding `Ki1SnapChipSelfTest.cpp` (the
  engine/test GLOBs lack `CONFIGURE_DEPENDS`); effective cache pins `COSMIC_2D_ONLY:BOOL=ON`.
- Acceptance runner driven through Windows PowerShell 5.1
  (`C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`) with absolute `OutDir`,
  repository-local `TempRoot`, `-KeepArtifacts`, and repository-local child `TEMP`/`TMP`.
- Reference machine: Windows 11 Education 26200, Ryzen 7 7800X3D (SSE4.2 floor),
  ~31 GiB RAM, RTX 5070 Ti (OpenGL 4.5). The KI-1 case is tier **I** (real editor UI on
  a real GPU); the runner records the full environment block per run in `results.json`.

## KI-1 — editor viewport snap-chip crash (FIXED)

**Defect.** `ViewportController::DrawViewportOverlays`'s `snapChip` lambda pushed a
style colour guarded on `on`, the button flipped `on`, then popped guarded on the
**changed** `on`. Every click that toggled a chip left Dear ImGui's colour stack off
by one: a Debug `abort()` and, in Release, a latent imbalance. The three chips render
**before** the `COSMIC_2D_ONLY` fence, so the bug shipped in the 2D editor.

**Fix.** Latch `const bool pushed = on;` before the button and guard both the
`PushStyleColor` and `PopStyleColor` on `pushed` — the exact pattern the sibling
`toggle` lambda already used. One file: `Projects/Starforge/src/ViewportController.cpp`.

**Regression — the real control, not a copy.** The `ViewportController.cpp` translation
unit cannot be linked into a standalone test (in Debug the whole editor is pulled in),
so a faithful regression must drive the **real** editor binary. `Ki1SnapChipSelfTest.cpp`
adds a harness to `StarforgeApp` that is inert unless `COSMIC_KI1_SELFTEST=<file>` is set.
When armed it:
1. opens a real, empty 2D edit scene so the real viewport strip renders;
2. waits for the strip layout to stabilise, then reads the three snap chips' real
   screen rects from a gated probe inside `DrawViewportOverlays`;
3. actuates the **real** Scale snap chip through Dear ImGui's input queue (main-viewport
   mouse events) for **both** toggle directions, twice — confirming via `SaveSnapPrefs`
   that each click truly toggled the chip, so a mis-aimed click can never pass;
4. asserts the colour-stack size **at the widget** (`colorDelta`, read inside the chip
   helper) is zero — the Release-safe oracle. A naive after-frame stack read nets to
   zero in Release because ImGui 1.92 **recovers** the stack at `EndChild`; measuring at
   the widget is what actually catches the silent case.

Evidence in `ki1/` and driven through the runner as case `KI-1`
(`manifests/wo07-ki1.manifest.json` + `fixtures/Run-Ki1SelfTest.ps1`):

| Run | Config | Result | Evidence |
|---|---|---|---|
| Failing-before | Debug | `abort()`, exit 3 — ImGui `IM_ASSERT` "Calling PopStyleColor() too many times!" in window `Untitled###Viewport` | `ki1/failing-before-Debug.stdout.log` |
| Failing-before | Release | verdict FAIL exit 1 — `colorDelta=-1` (OFF→ON spurious pop) **and** `+1` (ON→OFF leaked push), 4/4 clicks landed on `Untitled###Viewport/##k6strip` | `ki1/failing-before-Release.result.json` |
| Passing-after | Debug | verdict PASS exit 0 — 4/4 registered, imbalance 0, no abort | `ki1/passing-after-Debug.result.json` |
| Passing-after | Release | verdict PASS exit 0 — 4/4 registered, imbalance 0 | `ki1/passing-after-Release.result.json` |
| Runner (fixed) | Debug | `KI-1 PASSED — exit 0 as expected` (1 passed / 0 failed / 0 env-blocked) | `runner-Debug/results/results.json` |
| Runner (fixed) | Release | `KI-1 PASSED — exit 0 as expected` (1 passed / 0 failed / 0 env-blocked) | `runner-Release/results/results.json` |

The failing-before Debug and Release runs above were captured against the **unfixed**
`snapChip` (the fix applied only after); the harness itself was unchanged between the
before and after runs.

## L05 — Release-safe ImGui stack-balance on scripted editor UI

The requirement that L05 assert ImGui stack balance after each scripted UI action — not
merely "no crash" — is realised by the KI-1 harness's per-widget `colorDelta` oracle,
which is what distinguishes the fixed from the buggy chip in **Release** (where a
whole-frame check is fooled by ImGui's stack recovery). This is the mechanism KI-1 proves
necessary. The harness drives the real editor viewport strip on a real GPU; the broader
L05 surface (200 mixed cycles across the SF_Telem Main/Testing/Analysis/Replay screens,
native file-dialog cancel, replay open/close, minimize/restore, resize, dock/undock,
fullscreen, and closed/open/lost serial states) is tracked below.

## L03 — broken runtime-plugin load is recoverable

The same `test_wo07_host` binary drives three broken-load cases in fresh isolated
children (`COSMIC_WO07_L03_CASE`), each verifying `Application::LoadProjectDLL` rejects
the plugin and the app falls back to a **live launcher** that serves frames and closes
cleanly (no crash, no hang, no stale handle):

| Case | Plugin | Logged rejection (non-vacuous) |
|---|---|---|
| 0 | a missing DLL path | `Project DLL not found: '…wo07-does-not-exist.dll'` |
| 1 | `WO07NoExport.dll` (loads, exports none of the engine signatures) | `Plugin is missing required engine export signatures!` |
| 2 | `WO07LifetimeFixture.dll` with its report env unset → `CreatePluginLayer` returns null | `Plugin's CreatePluginLayer() returned nullptr — aborting load.` |

Each child then constructs the `Application` without throwing, runs ≥5 live launcher
frames (proving the host stayed functional), and closes cleanly. Driven through the
runner as case `L03` (5 children per case = 15 per config); evidence under
`l03-Debug/` and `l03-Release/`. The editor game-module compile/load-failure paths
(`GameModule::Load` / `BuildRunner`) are the L02 surface and are not covered here.

## Retained tests (regression safety) — Part I

WO-07 changes are confined to `Projects/Starforge/**` (editor DLL) plus new test
fixtures/docs; **no** file compiled into `CosmicTests` was modified. The full retained
headless suite was rebuilt and run in both configurations from **isolated** `TEMP` roots:

- Debug: **385 passed / 0 failed / 4 skipped** (native host cases skipped by default).
- Release: **385 passed / 0 failed / 4 skipped**.

Note (pre-existing, out of scope): running `CosmicTests` Debug then Release from a
**shared** `TEMP` reproducibly fails one WO-06 case (`test_wo06.cpp:208`) because
`Scratch()` (`test_wo06.cpp:28`) never clears `%TEMP%/wo06/<name>`, so the deliberately
bad `scene.bin` written at `:213` in the first run poisons the second. The acceptance
runner isolates `TEMP` per case, so this never affects an official run; it is filed as a
separate follow-up and is unrelated to the WO-07 changes.

## Acceptance-case status after Part I (2026-09-17)

*Superseded by the final table in Part II below; kept as the record of the first session.*


| Case | Status | Notes |
|---|---|---|
| KI-1 | **PASS (Debug + Release, via runner)** | Real editor control; failing-before/passing-after both configs. |
| L01 | **PASS (Debug + Release, via runner)** | F-LIFETIME runtime-plugin teardown, 100 reload + 10 fresh cycles per config; see below. |
| L03 | **PASS (Debug + Release, via runner)** | Broken runtime-plugin load is recoverable (missing DLL / missing exports / null CreatePluginLayer); see below. |
| L05 (stack-balance oracle) | **Mechanism delivered + demonstrated** | Per-widget colour-stack oracle on the real editor viewport strip, Release-safe; see above. |

## L01 — runtime-plugin (F-LIFETIME) teardown

`WO07LifetimeFixture.dll` is a real project DLL loaded by the real `Application` (adopting
the host's ImGui/ImPlot contexts). It owns one of every leakable resource class — a GPU
**texture**, a GPU **framebuffer**, a module-owned **component** object, an
**EntitySelection listener**, a **log sink**, a **JobSystem job**, and a **file watcher** —
and records each create/release into an **exe-owned** report (so the counts survive
`FreeLibrary`). `test_wo07_host.cpp` drives two runtime load/unload shapes:

- **reload** — after warmup the fixture calls `Application::TransitionToLauncher()` (the real
  `UnloadProjectDLL` path); the host then runs quiescence frames in the launcher and fires a
  **post-unload** `EntitySelection` change.
- **fresh launch/close** — after warmup it closes the window; the full `Application::Shutdown`
  unloads the plugin.

After `Run()` the host asserts, from the surviving report:
- **destruction order** — `seqDetach < seqDestroy` (OnDetach ran, then the module-owned
  destructors ran) and both completed before `Run()` returned, i.e. before `FreeLibrary`;
- **owned resources balanced** — `texCreated==texFreed`, `fboCreated==fboFreed`,
  `componentDestroyed==1`, `sinkAdded==sinkRemoved`, `listenerSubscribed==listenerUnsubscribed`,
  `jobSubmitted==jobRan`, `watcherStarted==watcherStopped`;
- **no callback after unload** — the fixture listener fired twice while live and **zero** times
  after unload; the host's own listener DID see the post-unload emit (so the probe is
  non-vacuous, and the freed fixture callback was never invoked — no crash);
- **scenario threads back to the warmed baseline** — the file-watcher worker appears at warmup
  (`warmedThreads > baselineThreads`) and is gone after quiescence (`postThreads == baselineThreads`);
  process handle count did not grow across the cycle (finite GL/driver caches are logged, not gated).

Driven through the runner as case `L01`
(`manifests/wo07-l01.manifest.json` + `fixtures/Run-WO07Lifetime.ps1`): **100 reload + 10 fresh**
isolated child processes per config, evidence under `l01-Debug/` and `l01-Release/`.

Teardown-contract note surfaced while building this (not a defect, a caveat worth recording):
`Application::Shutdown` tears down the **JobSystem before it unloads the project DLL**
(`Application.cpp:385` then `:407`), so a plugin that submits work must not `WaitIdle()` in
`OnDetach` on the full-shutdown path — the fixture settles its job during warmup instead. A
plugin file watcher must also watch a **quiet** directory, not a volatile one the app writes to
during shutdown, or its change events race the watcher's own teardown.

**Contracts reviewed (destruction-order / reload semantics confirmed by reading current control flow):**

- **Runtime plugin lifecycle (L01 target).** `Application::UnloadProjectDLL`
  (`Cosmic/src/core/Application.cpp:778-811`): `WorkspaceLayer::ClearViewportLayer` →
  `OnDetach`, then `delete m_ActivePluginLayer` (module-owned destructors), then
  `FreeLibrary`, then clears the handle — i.e. OnDetach + destructors run **before**
  `FreeLibrary` and GL-context loss, and the plugin's ImGui/ImPlot contexts are the
  host's (adopted at `:716-717` before `CreatePluginLayer` at `:703`).
- **Editor game-module lifecycle (L02 target).** `StarforgeApp::ReloadModule`
  (`Projects/Starforge/src/StarforgeApp.cpp:485`): serialize the edit scene → `StopScene`
  → `ClearSelection` → `Commands.Clear()` (undo cleared) → `Scene.reset()` (module-typed
  component destructors run against the **still-loaded** old module) → `GameModule::Unload`
  (`FreeLibrary`) → load the new module → rebuild the scene from the snapshot →
  `ClearDirty`. It preserves serialized scene + reflected/custom fields and does **not**
  promise arbitrary running C++ state — matching the ratified contract.
- **Failure paths (L03 target).** `Application::LoadProjectDLL` already handles missing
  DLL (`LoadLibraryA` fails → logs, returns), missing export / null `CreatePluginLayer`
  (`GetProcAddress`/return-null → logs, `FreeLibrary`, clears handle) recoverably, with no
  stale module pointer left set.

**Remaining acceptance breadth after Part I:** L02, L04, L05 (breadth) and P01 were recorded
here as *planned / not-run* on 2026-09-17. They were executed on 2026-09-18 — see Part II.

## Files

- `Projects/Starforge/src/ViewportController.cpp` / `.h` — KI-1 fix + gated regression probe.
- `Projects/Starforge/src/StarforgeApp.cpp` / `.h` — gated harness hooks (`Ki1SelfTest*`).
- `Projects/Starforge/src/Ki1SnapChipSelfTest.cpp` — the KI-1 in-editor regression harness.
- `tests/ImGuiStackGuard.h` — Release-safe ImGui stack-depth reader (shared helper).
- `tests/acceptance/fixtures/Run-Ki1SelfTest.ps1`, `tests/acceptance/manifests/wo07-ki1.manifest.json` — runner wiring.
- `docs/plans/2d-stability-2026-09-16/contracts/known-issues.md` — KI-1 → fix landed.
- `docs/plans/2d-stability-2026-09-16/evidence/WO-07/**` — this report + evidence.


## Part II — the remaining breadth: L02, L04, L05, P01 (2026-09-18)

Executed directly on `main` from `HEAD fe3d807299f12444e27f79f175d4c5229aa63b63`
(the three 2026-09-17 WO-07 commits, which `origin/main` had meanwhile received; no push
was made from this session). Same rules and toolchain as Part I (VS-bundled CMake,
`-DCOSMIC_2D_ONLY=ON`, reconfigured after every new `.cpp`; Windows PowerShell 5.1 runner
with absolute `OutDir`, repository-local `TempRoot`, `-KeepArtifacts`, repository-local
child `TEMP`/`TMP`; DESKTOP-SEOA4BT, Win11 26200, 7800X3D, RTX 5070 Ti / OpenGL 4.5).
A pre-existing registered worktree `.claude/worktrees/epic-clarke-338e7f` (branch
`claude/epic-clarke-338e7f`, from an earlier session) was found and left untouched; no
worktree or branch was created. Every case below was delivered **through the WO-04 runner
in Debug and Release**; the runner `results.json` / `results.junit.xml` and a
`*-runner-excerpts.txt` per case are committed, the raw `.out.log`/`.err.log` files are
gitignored, and the junction/asset scratch dirs were removed from the evidence tree before
committing. Each `results.json` records `dirty=true` with the working tree's diff hash:
the runs were made against the uncommitted fixes, exactly as the Part I runs were.

Local commits (author and committer `kdadabhoy <kdadabhoy28@gmail.com>`, no trailers):
`3e1c2be` — editor module-reload defects (KI-26…31, KI-34) + the L02 acceptance + the
widened strip probe and the editor L05 harness; `6d2e741` — the L04, L05 (SF_Telem host)
and P01 hosts with the KI-32/KI-33 engine fixes; and the commit carrying this report,
the revalidation evidence and the final known-issue dispositions. Nothing was pushed.

Nothing in this session is `ENVIRONMENT_BLOCKED`: every prerequisite (GPU, visible
window, native dialogs, the real cmake toolchain for the 50-compile loop) was present.
Physical-driver serial qualification (T04) remains `ENVIRONMENT_BLOCKED` as inherited
from WO-05; L05 exercises closed/open/lost serial states over the WO-04 fake transport
and says so.

### Final acceptance-case status (WO-07)

| Case | Debug | Release | Cycle / build counts | Evidence |
|---|---|---|---|---|
| KI-1 | PASS | PASS | 4/4 real chip actuations, imbalance 0 (revalidated 2026-09-18) | `runner-*`, `ki1/`, `revalidation-*/ki1` |
| L01 | PASS | PASS | 100 reload + 10 fresh children per config (revalidated) | `l01-*`, `revalidation-*/l01` |
| L02 | PASS | PASS | **53 real builds = 50 successful rebuild/reload cycles + 1 compile failure + 1 module-load failure; 12 reflected-field changes** | `l02-*`, `l02/` |
| L03 | PASS | PASS | 15 children per config (revalidated); the editor compile/load-failure halves live in L02 | `l03-*`, `revalidation-*/l03` |
| L04 | PASS | PASS | 20 reload + 10 close + 10 careless + 10 real-module children per config | `l04-*`, `l04/` |
| L05 | PASS | PASS | SF_Telem host: 200 cycles / 2080 judged actions; editor host: 200 cycles / 2240 judged actions, 1200/1200 real chip clicks | `l05-*`, `l05-editor-runner-*` |
| P01 | PASS | PASS | 3 launches × 50 in-process reload/reopen cycles, 700 frames plotted / 550 inspected / 500 presented-pixel probes each | `p01-*` |
| Retained headless | 388/388 | 388/388 | 385 retained + 3 new headless regressions (KI-27, KI-30, KI-32); 10 skipped native hosts (WO-05/06 + WO-07 L01/L03/L04/P01/L05) | `retained` below |

### Defects found while executing L02/L04 (all fixed, failing-before → passing-after)

Nine new confirmed defects, every one registered in `contracts/known-issues.md` **before**
its fix with the failing-before evidence named there. Seven were found by the L02 harness
on the first real rebuild cycles, two by the L04 fixture:

| KI | One line | Fix (file) | Failing-before | Passing-after |
|---|---|---|---|---|
| KI-26 | A project scaffolded by the 2D editor cannot build its game module (template never consumes `COSMIC_2D_ONLY`; 3D-only samples registered): 13 unresolved externals on every Ctrl+B | `Projects/Starforge/assets/templates/CMakeLists.txt` (WO-03 option+define block), `templates/src/Module.cpp` + `NavCritter.h`/`VoxelDigger.h` fenced | `l02/scaffold-template-failing-before-Debug.txt` | L02 cycle 0 of every run |
| KI-27 | Re-registering a reflected component accumulates its fields (5→10→15→20 per reload) | `Cosmic/src/reflect/TypeRegistry.h` `ClassIn` starts from a fresh field list | `l02/failing-before-Debug-registry-accumulation-play-leak.result.json` | L02 registry checks; `test_reflect.cpp` KI-27 case |
| KI-28 | A reload during Play bakes runtime state into the edit scene (snapshot taken before `StopScene`) | `Projects/Starforge/src/StarforgeApp.cpp` `ReloadModule` stops Play first | same file (`Position [2732,8,9] != [7,8,9]`) | L02 cycles with Play during the build |
| KI-29 | Registry entries of an unloaded module stay live and are invoked after `FreeLibrary` (editor: ACCESS VIOLATION after a failed load; runtime: `PlayerLayer` never unregisters) | `TypeRegistry::Remove`, `ModuleRegistry::UnregisterModule` drops the module's Reflect descriptors, `PlayerLayer::OnDetach` unregisters | `l02/failing-before-Debug-load-failure-access-violation.txt`, `l04/failing-before-Debug-runtime-module-registry-stale.txt` (counterfactual, with binary hashes) | L02 load-failure cycle (blocks preserved opaquely), L04 module-registry children |
| KI-30 | Script field overrides are dropped when a scene is loaded without its script class (open-before-build, failed load) | `Cosmic/src/scene/Components.h` `PendingFields` + `SceneSerializer.cpp` keeps/merges them | `l02/failing-before-Debug-script-overrides-lost.result.json` | L02 after the failed-load cycle; `test_scene_serializer.cpp` KI-30 case |
| KI-31 | Inspector backfill of one newly added script field resets every existing override | `Projects/Starforge/src/panels/InspectorPanel.cpp` seeds only the missing fields | `l02/failing-before-Debug-inspector-backfill-resets-overrides.result.json` | L02 cycles with the probe selected across a field addition |
| KI-32 | `EntitySelection` invokes a listener unsubscribed during the dispatch | `Cosmic/src/telemetry/EntitySelection.cpp` `Notify` re-checks liveness per callback (the EventBus rule) | `l04/failing-before-Debug-entityselection-removed-listener-fires.txt` | L04 `esRemovedMidDispatchFired == 0`; `test_events.cpp` KI-32 case |
| KI-33 | `UnloadProjectDLL` frees the plugin while its job is still queued/in flight (worker resumes in unmapped code) | `Cosmic/src/core/Application.cpp` drains the JobSystem before `FreeLibrary` | `l04/failing-before-Debug-job-runs-into-unmapped-plugin.txt` | L04 careless-plugin children |
| KI-34 | The **Release** editor hot-loads a `/MDd` Debug-CRT module (`kHotConfig = "Debug"`), crashing at the first load | `Projects/Starforge/src/BuildRunner.h` `kHotConfig` follows the editor's own configuration | `l02/failing-before-Release-debug-module-in-release-editor.txt` | L02 Release |

Two test-side consequences of the new contracts are recorded honestly: `test_reflect.cpp`
had one bare `Reflect::Class<WidgetComponent>("Widget","Test")` "ensure registered" call
that relied on the old accumulation (it crashed under the KI-27 fix); it now declares its
full chain through a shared helper. And the L02 harness itself went through three
iterations of *harness* fixes (float-literal generation, stopping Play after a deliberate
compile failure, not expecting a destruction-order proof on the reload that follows a
failed load, judging the reload contract before the harness's own edit) — none of those
weakened a check; each is visible in the failing-before result files.

### L02 — editor game-module rebuild/reload (the second, distinct DLL lifecycle)

`Projects/Starforge/src/L02ModuleReloadSelfTest.cpp` is a gated in-editor harness
(`COSMIC_L02_SELFTEST`, `COSMIC_L02_PROJECT_ROOT`, `COSMIC_L02_CYCLES`; wired at
`OnAttach`/`OnUpdate`/`OnDetach` and three probes inside `ReloadModule`). Armed, the real
`Starforge.exe`:

1. scaffolds a **real project through `NewProjectAt`** (the shipped template: CMakeLists,
   `src/`, `scenes/Main.cscene`) into a short repository-local root (`{RUNTEMP}\l02` —
   MSBuild's TryCompile paths exceed MAX_PATH under the deep evidence directory, which the
   first attempt proved), and adds a custom **reflected** component `L02Component`
   (`CS_COMPONENT`, 5 fields) plus a script `L02Script` whose `OnUpdate` drifts the
   entity's `Position.x` every frame while playing;
2. runs **52 numbered build cycles** through the real `BuildScripts() → BuildRunner
   (cmake configure + build) → Poll → ReloadModule(...) → GameModule` path. Every cycle
   rewrites `Module.cpp` with a new exported **build stamp** that the harness reads back
   from the DLL that is actually mapped (`GetModuleHandleA(stem)` + `GetProcAddress`), so
   an unchanged binary can never pass as a rebuild. Cycles are grouped into 13 variants
   with **12 reflected-field changes**: add field, retype int→float, rename, remove, add
   vec4, change defaults, add int, remove, retype float→int (+ script field
   add/remove), add a second component type `L02Extra`, change a string default + remove a
   field, remove the component type. Cycle 25 is a deliberate **compile error** and cycle
   41 a DLL that builds but **exports no `CosmicModule_Register`**;
3. after every successful reload asserts, **through the reflection registry** (never the
   serializer under test): the probe instance's kept fields equal the harness's own
   expectation table (values it set through the real `Commands::SetField`), new fields
   read the new module's default (a fresh instance is created and read), retyped values
   convert, the descriptor field list equals the variant **exactly** (no accumulation, no
   loss), `L02Extra` is present/absent and its block survives **opaquely** (C05) once the
   type is removed, the script descriptor matches, the probe's UUID/Tag/`Transform`
   survive (Position exactly `[7,8,9]` even when Play was started during the build), Play
   is stopped, selection and undo/redo are cleared and the scene is clean, the module-typed
   **destructor ran while the old DLL was still mapped** (the module's own destructor
   stamps an exe-owned sequence; `ReloadModule` probes stamp scene-dropped / unloaded /
   loaded) and the old stem is unmapped afterwards;
4. on the compile-failure cycle asserts the builder reported `Failed`, the current module
   and scene were kept and `[Build] Failed` was logged (Play, if running, is kept — the
   harness stops it); on the load-failure cycle asserts `[Module] Load failed`, no module
   loaded, the `L02Component` block preserved opaquely and **no stale descriptor left**;
5. saves through the real Save path; the runner wrapper (`Run-WO07ModuleReload.ps1`)
   then parses `scenes/Main.cscene` **out of process** (PowerShell's JSON reader) against
   the expected-final table the harness wrote — the independent oracle.

Result (both configs, `l02-runner-excerpts.txt`): **53 builds, 50 successful
rebuild/reload cycles, 12 reflected-field changes, compile failure recovered, module-load
failure recovered, 0 failed checks, independent scene oracle PASS** — Debug 316 s, Release
380 s (the 30-min bar is not approached). What L02 does **not** claim: preservation of
arbitrary running C++ state (the contract explicitly does not promise it, and the harness
proves the edit scene — not the play state — is what survives).

### L04 — teardown during live background activity

`tests/WO07TeardownFixture.cpp` (+ `WO07TeardownReport.h`) is a runtime plugin that, at the
moment of unload, owns all of: a JobSystem job that is **genuinely in flight** (it signals
"entered", then blocks on an exe-owned barrier), a file watcher whose directory the host
writes a file into every frame, a connected `SerialLink` over the WO-04
`FakeSerialTransport` with an exe thread pushing lines in a tight loop (130–234 lines
landed *inside* `OnDetach` per run), an `EntitySelection` listener, a log sink, a window
hotkey override (F9 posted as a real key message every frame), a scene `EventBus`
listener, a texture, a framebuffer and a module-owned object. The transition is requested
from **inside** the selection dispatch (a deferred transition). Three modes
(`tests/test_wo07_l04.cpp`): 0 reload (`TransitionToLauncher`), 1
`WM_CLOSE` while the job is blocked (the exe releases it 200 ms later; whole close
234–250 ms, inside the 2 s bar), 2 the **careless plugin** whose `OnDetach` never joins
its job (released 300 ms after detach). A fourth host loads a real `CS_MODULE` module
(`WO07ModuleFixture.dll`, hosted by the real `PlayerLayer`) twice and checks the registry
is clean after each unload.

Asserted per child from the exe-owned report: requested-inside-dispatch < dispatch
returned < `OnDetach` < destructor (ordering by sequence stamps); the job finished before
the code went away (mode 0: in flight at `OnDetach`, joined there; mode 1: drained before
unload; mode 2: `seqJobDone < seqAfterUnload`, `jobActiveAfterUnload == 0`); every
channel fired ≥ N times while live and **zero** times after unload while the same paths
still reach exe listeners (non-vacuous); a listener removed mid-dispatch did not fire
(EntitySelection **and** EventBus); every owned resource balanced (texture, framebuffer,
component, sink, listeners, watcher, bus connections, serial closed with fake handles/reads
0, `detachMs`/`serialCloseMs` ≤ 2 s); scenario threads back to the warmed baseline
(`warm 32–33 → post 29 == base 29`) and no handle growth. Result: **50/50 children per
config** (`l04-runner-excerpts.txt`).

### L05 — 200 scripted UI cycles with per-action stack balance

The oracle is `tests/WO07UiOracle.h`: (1) ImGui's recovered-error callback (`g.ErrorCallback`,
fires in Debug and Release for every "Missing PopStyleColor()", "PopID() too many
times!", "Missing End()"…, naming the window), (2) an `EndFramePre` context hook reading
the colour / style-var / font (the frame font = exactly 1 in ImGui 1.92) / popup / group /
ID / window-stack depths **before** EndFrame's own recovery pass, (3) the depths read at
the widget (the KI-1 chip probe, widened to all six strip chips, and around each layer's
render call), plus context/font pins. Every scripted action is judged on the diff of those
counters after 2–8 frames and written as one line of an exact action log.

- **SF_Telem host** (`tests/WO07UiCyclesFixture.cpp`, the real `Workspace::SF_Telem` over
  the fake transport; 200 cycles): serial closed / open (+bytes) / lost (`SignalDrop` →
  `Failed`) through the production chain; the **real** homescreen tile and Navigation
  buttons pressed through Dear ImGui's own item activation (`ActivateItemByID` on the real
  ids — 1000 screen switches, 0 mismatches); replay open of a recording the fixture itself
  produced through the real recorder + the **real** Unload button (200/200); the **real**
  Browse button opening the native `IFileDialog` (modal on the UI thread), cancelled by an
  exe helper thread — **20 dialogs opened and cancelled**; minimize/restore (40), resize
  (40), F11 fullscreen as a real key message both ways (16), undocking a docked window
  through ImGui's own undock path (40, re-docked by the next layout). **2080 judged
  actions, 0 failed; 0 recovered errors, 0 leaks, 0 drift, 0 layer imbalance**, six
  screenshots of the presented image per run.
- **Editor host** (`Projects/Starforge/src/L05EditorSelfTest.cpp`, gated by
  `COSMIC_L05_SELFTEST`; 200 cycles): all six real viewport-strip chips clicked with real
  mouse events at the rects the strip reports and **each confirmed to have toggled its
  state** (1200/1200), the four built-in layout presets in rotation (200, every panel
  re-docks), viewport hide/show (400), Play start/stop (20), minimize/restore, resize, F11
  both ways. **2240 judged actions, 0 failed; 0 errors / leaks / drift / strip / chip
  deltas**, six screenshots per run.

Both hosts, both configs: PASSED (`l05-runner-excerpts.txt`). Physical-driver serial
states stay `ENVIRONMENT_BLOCKED` (T04); the closed/open/lost states here are the
production ownership chain over the fake transport, as the catalog allows.

### P01 — ImPlot lifetime + known data

`tests/WO07PlotFixture.cpp` is a runtime plugin adopting the host contexts (the one
supported model — ImGui/ImPlot are static libraries with per-module globals, so the plugin
records the pointers `InitializePluginContexts` hands over and every frame checks its
current contexts are exactly those; 50 adoptions per process, identical and non-null).
Under a different registered theme every cycle it plots F-TRAJECTORY (`t=i/120`,
`y=50t−½gt²`, 1201 samples) as a time series and XY, 25 known scatter points, a shaded
band `y±5`, a three-series legend, a log10 Y axis (`10^(t/2.5)`), the nonfinite policy
(every 7th sample NaN + one +Inf: ImPlot skips them) and empty series. From the 4th frame
on it asserts fitted limits == the known ranges from the closed-form equations in double
(`X=[0,10] Y=[0,127.464516]`), nonfinite samples never moved them, equal pixel spacing per
decade (log) / per step (linear), legend count 3 with distinct colours, **drawn vertices
with the series colours at the known samples' pixels** (line, band and marker), stack
balance, zero recovered ImGui errors — and a **front-buffer pixel probe** of the presented
image at a known sample (counted only when the window is visible and owns that pixel):
**500/500 probes read the line colour (255,89,26)** in every launch. The plot window is
closed and reopened every cycle; the host reloads the plugin 50 times per process (3
launches per config). Multiple contexts/hosts are not exercised because the engine does not
support them (one adopted context), which the report states rather than fakes.

### Retained tests (regression safety) — this session

The engine itself changed this time (`TypeRegistry.h`, `ModuleRegistry`, `SceneSerializer`,
`Components.h`, `EntitySelection.cpp`, `Application.cpp`, `PlayerLayer.cpp`), so the full
headless suite was rebuilt and rerun from isolated `TEMP` roots after the last fix:

- Debug: **388 passed / 0 failed / 10 skipped** (23,112,685 assertions).
- Release: **388 passed / 0 failed / 10 skipped** (23,165,300 assertions).

385 retained cases + 3 new headless regressions (KI-27 `test_reflect.cpp`, KI-30
`test_scene_serializer.cpp`, KI-32 `test_events.cpp`); the 10 skipped are the native host
cases the runner launches (WO-05/06 hosts, WO-07 L01/L03/L04 ×2/P01/L05). The earlier
host cases were **revalidated through the runner** after the engine changes: KI-1, L01
(110 children) and L03 (15 children), Debug and Release — all PASSED
(`revalidation-runner-excerpts.txt`, `revalidation-<cfg>/`).

### Files added/changed in Part II

- Engine: `Cosmic/src/reflect/TypeRegistry.h` (fresh field list, `Remove`),
  `Cosmic/src/scripting/ModuleRegistry.{h,cpp}` (drops Reflect descriptors on unregister),
  `Cosmic/src/layers/PlayerLayer.cpp` (unregisters on detach), `Cosmic/src/core/Application.cpp`
  (JobSystem drain before `FreeLibrary`), `Cosmic/src/telemetry/EntitySelection.cpp`
  (liveness re-check per callback), `Cosmic/src/scene/Components.h` + `SceneSerializer.cpp`
  (`PendingFields`).
- Editor: `Projects/Starforge/src/StarforgeApp.{h,cpp}` (reload order + gated hooks),
  `BuildRunner.h` (`kHotConfig`), `panels/InspectorPanel.cpp` (backfill only missing),
  `ViewportController.{h,cpp}` (probe widened to all chips + read-only strip state),
  `L02ModuleReloadSelfTest.cpp`, `L05EditorSelfTest.cpp`; the scaffold template
  (`assets/templates/CMakeLists.txt`, `src/Module.cpp`, `src/scripts/{NavCritter,VoxelDigger}.h`).
- SF_Telem: `Projects/SF_Telem/src/SF_Telem.h` (`CurrentScreen()` read-only accessor).
- Tests: `tests/WO07TeardownFixture.cpp` + `WO07TeardownReport.h`, `tests/WO07ModuleFixture.cpp`,
  `tests/WO07PlotFixture.cpp` + `WO07PlotReport.h`, `tests/WO07UiCyclesFixture.cpp` +
  `WO07UiCyclesReport.h`, `tests/WO07UiOracle.h`, `tests/test_wo07_l04.cpp`,
  `tests/test_wo07_p01.cpp`, `tests/test_wo07_l05.cpp`, `tests/CMakeLists.txt`,
  `tests/test_reflect.cpp`, `tests/test_scene_serializer.cpp`, `tests/test_events.cpp`.
- Runner: `tests/acceptance/fixtures/Run-WO07{ModuleReload,Teardown,Plots,UiCycles}.ps1`,
  `Run-L05EditorSelfTest.ps1`; `tests/acceptance/manifests/wo07-{l02,l04,l05,p01}.manifest.json`.
- Docs/evidence: `contracts/known-issues.md` (KI-26…KI-34), `evidence/WO-07/**` (this report,
  `l02/`, `l04/`, `l02-*`, `l04-*`, `l05-*`, `l05-editor-runner-*`, `p01-*`,
  `revalidation-*`, the five `*-runner-excerpts.txt`).
