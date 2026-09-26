# UX-01 execution report — flow editor and the Editors host (FE01–FE05)

Status: **Phase B done — ready to land** (VM, 2026-09-26). FE01, FE02, FE03 and FE05 pass in both configs on the VM.
**FE04 is ENVIRONMENT_BLOCKED on the VM.** The display is 1718x920, so Windows limits the editor window to 1738x940,
and the case needs a 1920x1080 window. The runner reports that leg as FAILED. FE04 last passed at 1920x1080 on the
pre-VM PC; re-checking it at 1920x1080 is HOST-VERIFY. CosmicTests 529/0/14 in both configs. The four retained
manifests pass in both configs, and the three checkers exit 0. KI-85 (a harness defect found in Phase B) is
registered and fixed.

## Scope and provenance

- Work order: `work-orders/UX-01.md` (Kaden's items 5–8), adapted by the orchestrator's VM notes (two phases).
- Branch `ux/01` in worktree `C:\dev\Cosmic\build\_lanes\ux-01`, **rebased onto `main` `690d642`** (the UX-V0 merge).
  The only conflict was `known-issues.md`; both sides were kept, with KI-66..71 and KI-78 before KI-82..84.
- History: the first session (pre-VM PC, 2026-09-24) wrote every fix, the tests and the self-test host, then ran out
  of usage before writing a report (`HANDOFF.md`). Phase A (VM, 2026-09-25) rebuilt everything and ran the headless
  legs. Phase B (VM, 2026-09-26) rebased, rebuilt, and ran every leg below.
- Evidence keys: every runner `results.json` carries `git.commit` / `dirty` and the environment. The `ux01-editor`
  VM runs were made at `067cd45` plus the uncommitted KI-85 fix, which was then committed unchanged as `e7f99a8`.
  The retained runs were made at `e7f99a8`; the only uncommitted changes were evidence and docs.

## Toolchain and environment

- VMware Workstation VM: 8 vCPU (AMD Ryzen 7 7800X3D host), 16 GB, Windows 11 Education 10.0.26200.9457.
  GPU "VMware SVGA 3D", OpenGL 4.5 llvmpipe, display 1718x920 (primary, 100 %).
- VS Community 2026 (Visual Studio 18), MSVC 19.51.36260.0, Windows SDK 10.0.26100.0, VS-bundled cmake 4.3.1-msvc1,
  generator "Visual Studio 18 2026", `-A x64`, `--parallel 4`. `$env:COSMIC_SDK` = the worktree for every build and
  every run. Load: UX-03 and the orchestrator's landing tests shared the VM; the CPU load of each run is recorded.
- Configure: `cmake -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`, exit 0. Effective cache:
  `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=OFF`, `COSMIC_BUILD_ENGINE_ONLY=OFF`,
  `COSMIC_WITH_JOLT=ON`, `COSMIC_SKIP_PROJECTS=AnalysisSample;PendulumLab`, `COSMIC_SDK_DIR=<worktree>`.

## What was built (commits on `ux/01`, rebased)

| Item | KI | Commit | Change |
| --- | --- | --- | --- |
| Register first | KI-66..71, KI-78 | `3f717f0` | the six defects and the Screens-panel finding, registered before any fix |
| Size / dock / focus | KI-66 | `1fb0310` | first use 1100x680; `SetNextWindowFocus()` the frame after `Open()`; the Assets / Telemetry / Level presets dock "Editors" at `DockPort::Center` (Animation keeps `BottomCenter`); `CenterOnContent()` once after the first layout; an Inspector toggle on the toolbar |
| Routing + vendored patch | KI-67 | `16263fb` | `NodeCanvas::RouteLink` (pure, `widgets/NodeCanvasRoute.cpp`, also compiled into CosmicTests); `g_CosmicLinkRouter` in `imgui_node_editor.cpp` (VENDOR-NOTES local patch 2, upstream `021aa0e`); pin pivots, input arrowheads, opaque node background |
| Trigger kinds + Validate | KI-68 | `7fec696` | Event / Key / Timer / When selector; ImGui-free `editors/FlowTrigger.{h,cpp}`; `FlowAsset::Validate` reports an empty guard; `FlowEditor::HarnessSelection` |
| First-frame dirty + tab ids | KI-69, KI-70 | `7fca363` | the placement is written before the first comparison; a per-document tab counter assigned at `Open()` |
| The ✕ + Close Project | KI-71 | `7d0ec1c` | `AssetEditorHost::ShouldDraw(flag)` returns the flag alone; `AnyDirty`, `LogDirty`; `CloseProject` logs the dirty documents, then calls `CloseAll()` |
| Self-test + tests | — | `318b981` | `UX01EditorSelfTest.cpp` + 4 hooks, `tests/test_flow_editor.cpp` (the headless halves of FE01–FE05), `Run-UX01Editor.ps1`, `ux01-editor.manifest.json` |
| Dispositions (WIP) | — | `1bf560c` | KI-66..71 marked fixed; the last pre-VM ux01 evidence; `HANDOFF.md` |
| Phase A | — | `24c0fba` | the §11 rows checked and completed (self-test host row added), KI-66 wording, `phaseA-vm-excerpts.txt` |
| KI-85 register | KI-85 | `067cd45` | the FAIL-path truncation registered before the fix; the KI dispositions now name the rebased SHAs |
| KI-85 fix | KI-85 | `e7f99a8` | `f.close()` before the FAIL exit, which is now `TerminateProcess(GetCurrentProcess(), 1)` (5 lines, `UX01EditorSelfTest.cpp`) |
| Phase B | — | this commit | VM evidence (`vm/`, `retained/`, `phaseB-vm-excerpts.txt`), KI-66/85 dispositions, README next entry **KI-86**, this report |

## Acceptance-case status per ID (VM, Phase B)

| ID | Leg | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| FE01 | `RouteLink` unit case (forward links bit-exact; backward same row, backward above, self-loop, vertical stack; 64 samples) | **PASS** | **PASS** | `vm/runner-ux01-*/results.json` |
| FE02 | trigger kinds (Event -> When -> Event, field for field), `Validate`, F-FLOWS hashes (3 cases) | **PASS** | **PASS** | same; `fflows-sha256.txt` unchanged |
| FE03-U | Editors host, headless (flag, `ShouldDraw`, `CloseAll`) | **PASS** | **PASS** | same |
| FE05-U | tab ids stable across close / reopen | **PASS** | **PASS** | same |
| FE03 | self-test: the Editors ✕ hides the window with the document still open (`show=0 open_docs=1`); `Open` shows it again; Close Project leaves `AnyOpen()` false with 1 dirty line logged | **PASS** | **PASS** | `vm/ux01-*/ux01-result.json` |
| FE04 | self-test: Editors focused, canvas >= 800x400 px at a 1920x1080 window, every node inside the canvas, PNG | **ENVIRONMENT_BLOCKED** (runner: FAILED) | **ENVIRONMENT_BLOCKED** (runner: FAILED) | `vm/ux01-*/`: the window came up 1738x940, not 1920x1080; canvas 737x454; docked, focused, 4/4 nodes inside. Pre-VM PC at 1920x1080: PASS, 852x551 (`ux01-*/`) |
| FE05 | self-test: New ▸ Flow is not dirty after 2 and 12 drawn frames; a node drag makes it dirty | **PASS** | **PASS** | `vm/ux01-*/ux01-result.json` |
| UX01-EDITOR runner row | FE03 + FE04 + FE05 + the independent oracle | FAILED (2 of 4: FE04 and the PNG-size oracle) | FAILED (same) | the FE04 size prerequisite above; FE03, FE05 and the `Main.cflow` no-write oracle pass |

**Why ENVIRONMENT_BLOCKED and not FAIL:** the prompt sets FE04's target at a 1920x1080 window. On a 1718x920 display
Windows limits a top-level window's tracking size to the screen plus its frame, so the window came up 1738x940.
Every FE04 check that does not depend on size passed in both configs: the window is docked in the central node and
focused, all 4 nodes are inside the canvas, and `CenterOnContent` was applied. Only the two size checks failed:
canvas 737x454 against 800x400, and PNG 1738x940 against 1920x1080. Scaled by the window ratio, the measurements
match the pre-VM 1920x1080 run (Editors window 1095x577 vs 1210x674, canvas 737x454 vs 852x551). This is not
counted as a pass. The display resolution was not changed (a system setting).

**HOST-VERIFY (Kaden, a display that can hold 1920x1080):** from the worktree or `main` after landing, with
`$env:COSMIC_SDK` set to that root, run
`powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest <ABS>\tests\acceptance\manifests\ux01-editor.manifest.json -Config Debug -TempRoot <repo>\build\_temp\ux01d`
and the same with `-Config Release`. Expected: 5/5 PASSED, FE04 canvas >= 800x400.

CosmicTests (full suite, cwd = worktree root, TEMP isolated):
- **Debug: 529 passed / 0 failed / 14 skipped** (exit 0, 510 s, with another lane's CosmicTests running).
- **Release: 529 / 0 / 14** (exit 0, 252 s).
- The baseline is 523 + UX-01's 6 cases, so the land bar is met. No KI-84 timing failure occurred in Phase B;
  Phase A's failures are recorded in `phaseA-vm-excerpts.txt` and in KI-84.

Retained manifests (both configs, run one after another; the fixtures rewrote tracked App-Platform / 2D-stability
evidence, which was restored with `git checkout --` after copying the verdict JSONs into `retained/`):

| Manifest | Debug | Release | Detail |
| --- | --- | --- | --- |
| `ap03-editor` | **PASSED** (189 s) | **PASSED** (451 s, CPU avg 83 %) | AP03 self-test PASS (E01–E04, E06–E08, F01 and V05 all PASS), `failed_checks=0`, independent oracle PASS. No MSB6003 in either run, and E03 passed in both (UX-V0 had seen E03 fail in Release). |
| `wo07-l02` | **PASSED** (695 s) | **PASSED** (833 s) | 53 builds, 50 successful rebuild/reloads, 12 reflected-field changes, compile and module-load failures recovered, scene oracle PASS |
| `wo09-editor` | **PASSED** (5 s) | **PASSED** (5 s) | C05 lifecycle, 9 steps, scene oracle PASS (8 entities) |
| `wo10-sample` | **PASSED** (96 s) | **PASSED** (94 s) | package via the editor path, packaged run, numeric oracle (worst 4.15e-5 m) and image oracle PASS |

Checkers after the rebase: `check_gl_conformance` exits 0 (GLSL pass: 89 files clean; GL tokens clean).
`check_docs_coverage` exits 0 (123 public headers, 120 manifest rows). `check_docs_links` exits 0 (0 strict-tier
breakages, 62 warn-only).

## Defects (registered before fixing)

| KI | Defect | Failing-before | Passing-after |
| --- | --- | --- | --- |
| KI-66 | a flow opens tiny, unsized and unfocused | FE04 before-state: canvas 4x5 px, window neither sized nor docked, nodes outside the canvas | FE04 at 1920x1080 (pre-VM PC); VM: blocked at the size check, everything else passes |
| KI-67 | backward links and self-loops cross the node boxes | FE01 on the stock curve (`failing-before/`) | FE01 PASS in both configs (VM) |
| KI-68 | "when" is one-way; an empty guard is saved, never fires and is not reported | FE02 before-state | FE02 PASS in both configs (VM) |
| KI-69 | a new flow is dirty on its first frame | FE05 before-state | FE05 PASS in both configs (VM) |
| KI-70 | tabs are keyed by index | FE05-U before-state | FE05-U PASS in both configs (VM) |
| KI-71 | the Editors ✕ is ignored while a document is open; documents survive Close Project | FE03 before-state | FE03-U and FE03 PASS in both configs (VM) |
| KI-78 | the Screens panel is docked by no built-in preset; it floats over the Hierarchy and the central tab bar | seen on every run: the self-test moves it aside before clicking the Editors ✕ (`editors_close_button_covered_by`) | **open**, proposal: dock it at `DockPort::LeftBottom`. Not UX-01's to fix. |
| KI-85 | the UX-01 self-test truncates its own result JSON on the FAIL path: an open `ofstream` plus `quick_exit`, and the process ended with 0xC0000409 | the first VM Debug run: 4157-byte JSON cut off mid-string, so the wrapper scored 0 of 4 although FE03 and FE05 had passed (`phaseB-vm-excerpts.txt`) | `e7f99a8`: both VM runs exit 1 with valid JSON, FE03 / FE05 PASS, FE04 FAIL reported alone |

Registered in `3f717f0` (KI-66..71, KI-78) and `067cd45` (KI-85). The failing-before record for KI-66..71 is
`failing-before/failing-before-excerpts.txt` (pre-VM PC, isolated before-state).

## Measured numbers

| | pre-VM PC (1920x1080) | VM (1718x920 display) |
| --- | --- | --- |
| main window | 1920x1080 | 1738x940 (OS-limited) |
| Editors window | 1210x674 at (366,116), docked in the central node | 1095x577 at (331,107), docked in the central node |
| flow canvas (first drawn frame) | **852x551** at (376,229) | **737x454** at (341,220) |
| nodes inside the canvas | 4 of 4 | 4 of 4 |
| New Flow `Dirty()` after 2 / 12 frames / a drag | 0 / 0 / 1 | 0 / 0 / 1 |
| Close Project | 2 docs (1 dirty) -> `AnyOpen` 0, 1 line logged | same |
| self-test time Debug / Release | 1.9 s / 1.4 s | 4.6 s / 4.1 s |

VM builds after the rebase (incremental): Debug 150 s, Release 217 s, 0 warnings and 0 errors in both.
After the KI-85 fix, the Starforge-target rebuilds also had 0 warnings. F-FLOWS: 8 tracked `.cflow` round-trips
hash as at `6021822`, and the 4 corrupt fixtures refuse to load (FE02 in both configs).

## Contract deviations (for the integrator)

1. `widgets/NodeCanvasRoute.cpp` is a new TU (the prompt names it; §10's UX-01 row lists only `NodeCanvas.{h,cpp}`).
2. `UX01EditorSelfTest.cpp`, its four hook lines in `StarforgeApp.cpp`, and the declarations in `StarforgeApp.h`.
3. `CloseProject` gets one physical line: `m_Editors.LogDirty(m_Ctx, "Close Project"); m_Editors.CloseAll();`.
4. `Cosmic/src/scene/FlowMachine.cpp`: `FlowAsset::Validate` only (4 lines).
5. The vendored router pointer is declared and defined inside `imgui_node_editor.cpp` (no header change;
   `NodeCanvas.cpp` re-declares the `extern`). It is VENDOR-NOTES local patch 2.
6. `tests/CMakeLists.txt` also compiles `editors/AssetEditorHost.cpp` into CosmicTests, and adds **two** include
   directories where the prompt said one: the imgui-node-editor headers (SYSTEM) and `Projects/Starforge/src`.
7. `AssetTypes.cpp` is untouched: KI-69 is fixed in `FlowEditor`.
8. Switching a transition to When adds an empty guard, not a channel guard with an invented name (§11 row).
9. KI-78 is registered and left open.
10. `work-orders/README.md` (not an owned file): the next-entry line now says **KI-86** and notes KI-85, at the
    orchestrator's request.
11. Evidence layout: `ux01-*/` and `runner-ux01-*/` keep the pre-VM 1920x1080 PASS runs. The VM runs are under
    `vm/`, and the retained verdicts are under `retained/`.

## Caveats

- FE04's DoD ("pass on Debug and Release") is **not met on the VM**. It is blocked at the size prerequisite, and
  HOST-VERIFY is required before FE04 counts as passing on the landed code. The pre-VM pass predates the rebase.
- `AP03AuthoringSelfTest.cpp` and `GuideWalkthroughSelfTest.cpp` open their result stream the same way KI-85 did.
  They are not UX-01's files, so they were left unchanged (noted in KI-85).
- The retained runs left `scratch-*` directories (with asset junctions) and `dist/` behind. Junctions were deleted
  as links first, then the directories were removed. `recordings/`, `logs/` and `n04-*` in the root were deleted.

## Files

Code: `Projects/Starforge/src/editors/{AssetEditorHost,FlowEditor,FlowTrigger}.{h,cpp}`,
`widgets/{NodeCanvas.h,NodeCanvas.cpp,NodeCanvasRoute.cpp}`, `LayoutPresets.cpp`, `StarforgeApp.{h,cpp}`,
`UX01EditorSelfTest.cpp`, `Cosmic/dependencies/imgui-node-editor/{imgui_node_editor.cpp,VENDOR-NOTES.md}`,
`Cosmic/src/scene/FlowMachine.cpp`. Tests: `tests/test_flow_editor.cpp`, `tests/CMakeLists.txt`,
`tests/acceptance/fixtures/Run-UX01Editor.ps1`, `tests/acceptance/manifests/ux01-editor.manifest.json`.
Docs: `known-issues.md` (KI-66..71, KI-78, KI-85), `01-Contracts.md` §11 (4 rows), `work-orders/README.md`
(next KI). Evidence (`evidence/UX-01/`): this report, `HANDOFF.md`, `fflows-sha256.txt`, `failing-before/`,
`ux01-*/` and `runner-ux01-*/` (pre-VM), `vm/` (VM ux01 runs + FE04 PNGs), `retained/` (runner results + verdict
JSONs), `phaseA-vm-excerpts.txt`, `phaseB-vm-excerpts.txt`.

## Local commits (not pushed — Kaden pushes)

`3f717f0`, `1fb0310`, `16263fb`, `7fec696`, `7fca363`, `7d0ec1c`, `318b981`, `1bf560c`, `24c0fba`, `067cd45`,
`e7f99a8`, and the Phase B evidence commit. All are by kdadabhoy <kdadabhoy28@gmail.com>, with no AI trailer.
