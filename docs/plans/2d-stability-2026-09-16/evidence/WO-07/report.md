# WO-07 execution report — 2026-09-17

Only WO-07 was executed, directly on `main` (main-only campaign, D-WORKFLOW). This
report covers the confirmed **KI-1** editor snap-chip crash (fixed, with real
failing-before/passing-after in Debug **and** Release, driven through the WO-04
acceptance runner) and the runtime/editor lifetime + UI stack-balance acceptance
scope (L01–L05, P01). Where a case's prerequisite is genuinely absent it is marked
`ENVIRONMENT_BLOCKED` with the exact prerequisite named; nothing missing is counted
as a pass.

## Scope and provenance

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

## Toolchain and environment

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

## Retained tests (regression safety)

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

## Acceptance-case status (L01–L05, P01)

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

**Remaining acceptance breadth (not executed this session — honest status, no phantom pass):**

| Case | Status | What remains |
|---|---|---|
| L02 | **Not executed** | 50 real Starforge game-module rebuild/reload cycles (≥10 reflected-field changes) through the actual `BuildRunner` + a scaffolded project, independently comparing preserved serialized fields. Heaviest case (50 real compiles). |
| L03 | **Not executed** | A host harness driving each failure (missing DLL/export, null `CreatePluginLayer`, failed compile, failed module load) and asserting recoverable host/UI + preserved edit-scene data. |
| L04 | **Not executed** | Teardown during live jobs/watcher notifications/selection-event-log callbacks/audio/hotkeys, with barriers + ownership counters. |
| P01 | **Not executed** | ImPlot known time-series/XY/scatter/shaded-band/legend data with adopted ImGui/ImPlot contexts over 50 reload/reopen cycles, asserting known samples/ranges. |

These require WO-05/06-scale host-fixture authoring and (L02) a real 50-compile loop;
they are a substantial follow-on and are recorded here as **planned / not-run**, distinct
from passed or env-blocked. `T04`-style physical/virtual serial and second-Windows-version
qualification remain `ENVIRONMENT_BLOCKED` as inherited from WO-05.

## Files

- `Projects/Starforge/src/ViewportController.cpp` / `.h` — KI-1 fix + gated regression probe.
- `Projects/Starforge/src/StarforgeApp.cpp` / `.h` — gated harness hooks (`Ki1SelfTest*`).
- `Projects/Starforge/src/Ki1SnapChipSelfTest.cpp` — the KI-1 in-editor regression harness.
- `tests/ImGuiStackGuard.h` — Release-safe ImGui stack-depth reader (shared helper).
- `tests/acceptance/fixtures/Run-Ki1SelfTest.ps1`, `tests/acceptance/manifests/wo07-ki1.manifest.json` — runner wiring.
- `docs/plans/2d-stability-2026-09-16/contracts/known-issues.md` — KI-1 → fix landed.
- `docs/plans/2d-stability-2026-09-16/evidence/WO-07/**` — this report + evidence.
