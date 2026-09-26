# UX-01 execution report — flow editor and the Editors host (FE01–FE05)

Status: **PHASE A** (VM, 2026-09-25) — builds, CosmicTests, the §11 check and the checkers are done on the VM;
the GL / editor legs (`ux01-editor`, the four retained manifests) are **pending the UX-V0 rebase** (Starforge
crashes on this VM until UX-V0's batch-shader fix, KI-82, is on `main`). Phase B fills them in.

## Scope and provenance

- Work order: `work-orders/UX-01.md` (Kaden's items 5–8), adapted by the orchestrator's VM notes (two phases).
- Branch `ux/01`, worktree `C:\dev\Cosmic\build\_lanes\ux-01`, base `2afe37c` (D-SOAKS). `main` has since moved
  by docs commits only (`d815252`, `353948b`); the rebase is Phase B.
- History: the first UX-01 session (old PC, 2026-09-24) wrote every fix, the tests, the self-test host and the
  ux01 evidence, then was cut off by the usage limit before this report (see `HANDOFF.md`). This Phase A
  session (VM) rebuilt from a fresh configure and re-ran the headless legs; it changed no code.
- HEAD at the start of Phase A: `d2aae20`; the Phase A commit is the one that adds this file (docs/evidence only).

## Toolchain and environment

- VMware Workstation VM, 8 vCPU / 16 GB, Windows 11 Education 10.0.26200; OpenGL = llvmpipe (Mesa software,
  no GPU). UX-V0 built concurrently in `build\_lanes\ux-v0`: `--parallel 4` used for every build.
- VS Community 2026 (Visual Studio 18), MSVC 19.51.36260.0, Windows SDK 10.0.26100.0, VS-bundled
  cmake 4.3.1-msvc1, generator "Visual Studio 18 2026", `-A x64`.
- `$env:COSMIC_SDK = C:\dev\Cosmic\build\_lanes\ux-01` for every build / run.
- Configure (fresh `build\`): `cmake -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`,
  exit 0, 7 s. Effective cache: `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`,
  `COSMIC_BUILD_RENDER_TESTS=OFF`, `COSMIC_BUILD_ENGINE_ONLY=OFF`, `COSMIC_WITH_JOLT=ON`,
  `COSMIC_SKIP_PROJECTS=AnalysisSample;PendulumLab`, `COSMIC_SDK_DIR=C:/dev/Cosmic/build/_lanes/ux-01`.

## What was built (commits on `ux/01`)

| Item | KI | Commit | Change |
| --- | --- | --- | --- |
| Register first | KI-66..71, KI-78 | `0ac96b4` | the six defects + the Screens-panel finding, before any fix |
| Size / dock / focus | KI-66 | `08b83f1` | first use 1100x680; `SetNextWindowFocus()` the frame after `Open()`; Assets / Telemetry / Level presets dock "Editors" at `DockPort::Center` (Animation keeps `BottomCenter`); `CenterOnContent()` once after the first layout; inspector toggle on the toolbar |
| Routing + vendored patch | KI-67 | `58bf7ad` | `NodeCanvas::RouteLink` (pure, `widgets/NodeCanvasRoute.cpp`, in CosmicTests); `g_CosmicLinkRouter` in `imgui_node_editor.cpp` (VENDOR-NOTES local patch 2, upstream `021aa0e`); pin pivots, input arrowheads, opaque node bg |
| Trigger kinds + Validate | KI-68 | `9c0b411` | Event / Key / Timer / When selector; ImGui-free `editors/FlowTrigger.{h,cpp}`; `FlowAsset::Validate` reports an empty guard; `FlowEditor::HarnessSelection` |
| First-frame dirty + tab ids | KI-69, KI-70 | `64ca25a` | placement written before the first comparison; per-document tab counter at `Open()` |
| The ✕ + Close Project | KI-71 | `fe3eddc` | `AssetEditorHost::ShouldDraw(flag)` = the flag; `AnyDirty`, `LogDirty`; `CloseProject` logs + `CloseAll()` |
| Self-test + tests | — | `59e972d` | `UX01EditorSelfTest.cpp` + 4 hooks, `tests/test_flow_editor.cpp` (FE01–FE05 headless), `Run-UX01Editor.ps1`, `ux01-editor.manifest.json` |
| Dispositions (WIP) | — | `d2aae20` | KI-66..71 → fixed, the last old-PC ux01 evidence, `HANDOFF.md` |
| Phase A | — | the Phase A commit (the one adding this file) | §11 rows checked and completed, KI-66 wording, this report |

## Acceptance-case status per ID

| ID | Leg | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| FE01 | `RouteLink` unit case (forward bit-exact, backward, above, self-loop, vertical stack; 64 samples) | PASS (VM) | PASS (VM) | `phaseA-vm-excerpts.txt` |
| FE02 | trigger kinds (Event -> When -> Event field for field), `Validate`, F-FLOWS hashes | PASS (VM) | PASS (VM) | `phaseA-vm-excerpts.txt`, `fflows-sha256.txt` unchanged |
| FE03-U | Editors host headless (flag, `ShouldDraw`, `CloseAll`) | PASS (VM) | PASS (VM) | `phaseA-vm-excerpts.txt` |
| FE05-U | tab ids stable across close / reopen | PASS (VM) | PASS (VM) | `phaseA-vm-excerpts.txt` |
| FE03 | self-test: Editors ✕ hides with the doc open, Open shows it, Close Project empties | **pending UX-V0 rebase** (old PC: PASS) | **pending UX-V0 rebase** (old PC: PASS) | `ux01-{Debug,Release}/` (old PC) |
| FE04 | self-test: Editors focused, canvas >= 800x400, every node inside, PNG | **pending UX-V0 rebase** (old PC: PASS) | **pending UX-V0 rebase** (old PC: PASS) | `ux01-*/fe04-flow-editor.png` (old PC) |
| FE05 | self-test: New ▸ Flow not dirty after 2 frames, a drag dirties | **pending UX-V0 rebase** (old PC: PASS) | **pending UX-V0 rebase** (old PC: PASS) | `ux01-*/ux01-result.json` (old PC) |

CosmicTests (VM, Phase A, HEAD `d2aae20` + Phase A docs): the six UX-01 cases pass in both configs (6/6, 162 assertions each). Full suite: **Debug 529 cases, 523 passed / 6 failed / 14 skipped** (877 s, alongside this lane's Release build and UX-V0); **Release 529, 527 passed / 2 failed / 14 skipped** (114 s, alongside UX-V0's llvmpipe render tests, CPU 66-100 %). Every failure is a timing case outside UX-01's code: E10 file watcher (1), WO-09 C05 per-case deadline (2; 2819 ms and 2013 ms against 2000 ms), WO-05 T03 serial matrix (Debug 3, Release 2). Re-run alone once: E10 and both C05 cases pass; **WO-05 T03 is intermittent on this VM** — Debug failed 3/4 and 4/4 on two re-runs, 3/4 from a fresh cwd, then passed 4/4 (401 s); Release failed 3 of the 4 WO-05 cases in the 7-case re-run (CPU 100 %) and 2/4 alone, then passed 4/4 (392 s); main's own `d815252` Release binary passed 4/4 (267 s) under similar load. UX-01 changes nothing on that path (engine: `FlowAsset::Validate` only; the new test include dirs shadow no SF_Telem header — checked). The land bar (>= 523/0/14 plus UX-01's 6) is therefore **not yet met on the VM**; Phase B re-runs both full suites with the VM as quiet as the other lane allows. Finding for the orchestrator (not registered by UX-01: no free KI number in its allocation, code outside its files): WO-05 T03's 2 s `WaitOpenEntered` / `Until(Open)` waits and its 250 ms / 2000 ms bars fail under an 8-vCPU VM at 100 % load — candidate KI (test-timing robustness on the VM).

Retained manifests (ap03-editor, wo07-l02, wo09-editor, wo10-sample): **pending UX-V0 rebase** (Phase B, both
configs; they launch Starforge / GL).

Checkers (Phase A): `check_gl_conformance` exit 0 (clean), `check_docs_coverage` exit 0 (123 public headers,
120 manifest rows), `check_docs_links` exit 0 (0 strict-tier breakages, 62 warn-only).

## Defects (registered before fixing, `0ac96b4`)

| KI | Defect | Failing-before (isolated before-state, Debug, old PC) | Passing-after |
| --- | --- | --- | --- |
| KI-66 | flow opens tiny, unsized, unfocused | FE04: canvas 4x5 px; window neither >= 1100x680 nor docked; nodes outside the canvas | FE04 (old PC both configs; VM: Phase B) |
| KI-67 | backward links / self-loops cross node boxes | FE01 unit cases fail on the stock curve | FE01 (VM both configs) |
| KI-68 | "when" is one-way, empty guard saved, never fires, unreported | FE02 unit cases fail | FE02 (VM both configs) |
| KI-69 | a new flow is dirty on its first frame | FE05: "a new flow is dirty on its first frames" | FE05 (old PC; VM: Phase B) |
| KI-70 | tabs keyed by index | FE05-U: ids re-key after a close | FE05-U (VM both configs) |
| KI-71 | Editors ✕ ignored while a doc is open; docs survive Close Project | FE03: window still drawn after its ✕; documents survived Close Project; no console line | FE03-U (VM), FE03 (old PC; VM: Phase B) |
| KI-78 | Screens panel docked by no built-in preset, floats over the Hierarchy / the central tab bar | observed by the self-test (it moves Screens aside before clicking the Editors ✕) | **open** (proposal: dock at `DockPort::LeftBottom`); not UX-01's to fix |

Failing-before record: `failing-before/failing-before-excerpts.txt` (before-state diff sha256
`9a06ced6…5a`, CosmicTests 5 of 6 UX-01 cases failed; self-test 10 failed checks), `ux01-before-Debug-result.json`,
`fe04-before-Debug.png`.

## Measured numbers

Old PC (2026-09-24, both configs identical; re-measured on the VM in Phase B): main window 1920x1080; Editors
window pos (366,116) size 1210x674, docked in the central node; flow canvas **852x551 px** at (376,229) after one
drawn frame (target >= 800x400); 4 of 4 state nodes inside the canvas; new flow `Dirty()` = 0 after 2 and 12
drawn frames, 1 after a drag; Close Project: 2 docs (1 dirty) → `AnyOpen()` false, 1 dirty line logged; the
self-test's own run time 1.9 s Debug / 1.4 s Release.
VM (Phase A): configure 7 s; Debug build 459 s, Release build 349 s (`--parallel 4`,
alongside UX-V0), 0 `: warning` and 0 `: error` lines in either build log; CosmicTests Debug 877 s / Release 114 s (full suite, under load; see above).
F-FLOWS: `fflows-sha256.txt` — 8 tracked `.cflow` round-trips hash as at the base `6021822`, 4 corrupt fixtures
refuse to load (unchanged); re-proven by the FE02 F-FLOWS case in both configs on the VM.

## Contract deviations (for the integrator)

1. `widgets/NodeCanvasRoute.cpp` — new TU (the prompt names it; §10's UX-01 row lists only `NodeCanvas.{h,cpp}`).
2. `UX01EditorSelfTest.cpp` + its four hook lines in `StarforgeApp.cpp` (Init / Shutdown / Tick / FrameEnd) and the
   declarations in `StarforgeApp.h` beside the AP03 / GUIDE hooks.
3. `CloseProject`: one physical line, `m_Editors.LogDirty(m_Ctx, "Close Project"); m_Editors.CloseAll();`
   (the prompt's "one line" plus the logging it asks for).
4. `Cosmic/src/scene/FlowMachine.cpp` — `FlowAsset::Validate` only (4 lines: the empty-guard error).
5. Vendored router pointer: declared + defined inside `imgui_node_editor.cpp` (no header change; `NodeCanvas.cpp`
   re-declares the `extern`), recorded as VENDOR-NOTES local patch 2.
6. `tests/CMakeLists.txt`: besides `NodeCanvasRoute.cpp` and `FlowTrigger.cpp`, `editors/AssetEditorHost.cpp` is
   compiled into CosmicTests (FE03 / FE05 headless halves), and **two** include dirs are added (imgui-node-editor
   headers, SYSTEM; `Projects/Starforge/src`) where the prompt said one.
7. `AssetTypes.cpp` untouched: KI-69 is fixed in `FlowEditor` (placement applied before the first comparison).
8. Trigger-kind skeleton: switching to When adds the empty guard, not a channel guard with an invented channel
   name (§11 row states it).
9. KI-78 registered, left open (the Screens panel is outside UX-01's files).

## Caveats

- Phase A is headless only. Every FE03 / FE04 / FE05 editor-half number above is from the old PC; the VM
  re-run is Phase B, after UX-V0 (KI-82) lands on `main`.
- The self-test moves the floating Screens panel (KI-78) aside before clicking the Editors ✕; it is recorded in
  the result JSON as `editors_close_button_covered_by`.
- CosmicTests ran while UX-V0 built and tested on the same VM (at most two lanes, as the README allows); load per run is recorded in `phaseA-vm-excerpts.txt`. The old-PC `ux01-*` / `runner-ux01-*` folders stay until Phase B replaces them with VM runs. The full-suite runs from the worktree root left `recordings/`, `logs/` and `n04-*` there; they were deleted before the commit.

## Files

Code: `Projects/Starforge/src/editors/{AssetEditorHost.h,AssetEditorHost.cpp,FlowEditor.h,FlowEditor.cpp,
FlowTrigger.h,FlowTrigger.cpp}`, `widgets/{NodeCanvas.h,NodeCanvas.cpp,NodeCanvasRoute.cpp}`, `LayoutPresets.cpp`,
`StarforgeApp.{h,cpp}` (hooks, `:1545` draw rule, `CloseProject`), `UX01EditorSelfTest.cpp`,
`Cosmic/dependencies/imgui-node-editor/{imgui_node_editor.cpp,VENDOR-NOTES.md}`, `Cosmic/src/scene/FlowMachine.cpp`.
Tests: `tests/test_flow_editor.cpp`, `tests/CMakeLists.txt`, `tests/acceptance/fixtures/Run-UX01Editor.ps1`,
`tests/acceptance/manifests/ux01-editor.manifest.json`.
Docs: `known-issues.md` (KI-66..71, KI-78), `01-Contracts.md` §11 (4 rows).
Evidence (`evidence/UX-01/`): `report.md`, `HANDOFF.md`, `fflows-sha256.txt`, `failing-before/`,
`ux01-{Debug,Release}/` + `runner-ux01-{Debug,Release}/` (old PC; replaced in Phase B), `phaseA-vm-excerpts.txt` (this phase: configure, builds, every CosmicTests run and re-run, checkers).

## Local commits (not pushed — Kaden pushes)

`0ac96b4`, `08b83f1`, `58bf7ad`, `9c0b411`, `64ca25a`, `fe3eddc`, `59e972d`, `d2aae20`, the Phase A commit (the one adding this file) — all as
kdadabhoy <kdadabhoy28@gmail.com>, no AI trailer.
