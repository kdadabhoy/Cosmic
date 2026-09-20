# GUIDE — the PendulumLab walkthrough chapter, executed

Status: done, 2026-09-20. Lane `ap/guide`, worktree `C:\dev\Cosmic-ap-guide`, base `8b4798a`
(main with AP-01..AP-04 + AP-03). Ran alone in its worktree; `main` untouched.

## 1. Deliverables

- `docs/guide/pendulumlab-walkthrough.md` — the chapter (listed in `docs/guide/README.md` under a
  new *Walkthroughs* row). Sections: What you are building · Before you start · Step 1 New Project
  (App) · Step 2 Build once and Play the template · Step 3 Screens (Lab, Stopped) · Step 4 Home ·
  Step 5 Lab (rig sprites, UI element table, anchor idioms) · Step 6 Settings · Step 7 Stopped ·
  Step 8 The flow · Step 9 The C++ (service, screen scripts, Module.cpp, Project Settings) · Step 10
  Play it, and the live loop (DataBus panel, Settings, the Stopped overlay, edit-while-playing,
  Build failed, source links) · Step 11 Export · What you should see · Troubleshooting · How this
  chapter was verified.
- `docs/guide/images/pendulumlab/01..18-*.png` (editor, 1440x810, 52-493 KB) and
  `19-exported-home.png`, `20-exported-lab.png`, `22-exported-home-again.png` (the exported exe,
  1280x720). The red box is Dear ImGui's own `DebugLocateItem` highlight for the pictured control
  (re-coloured), or a hand-placed rect (`annotations.json`) where the control is not an ImGui item
  (the status chip, the guard block, the DataBus table, the Done line).
- The driver `Projects/Starforge/src/GuideWalkthroughSelfTest.cpp` (env-gated, `COSMIC_GUIDE_SELFTEST`;
  hooks in `StarforgeApp.{h,cpp}` next to the AP-03 harness; one seam `FlowEditor::HarnessSelect`),
  the wrapper `tests/acceptance/fixtures/Run-GuideWalkthrough.ps1`, and `tools/guide_shots.py`
  (highlight re-colour, manual rects, downscale, <= 600 KB).

## 2. The run this evidence comes from (run 12, Release)

`Run-GuideWalkthrough.ps1 -Bin build\Runtime\Release -Output build\guide-run12`:

| Fact | Value |
| --- | --- |
| Editor verdict | **PASS**, 0 failed checks, 152 plan steps, 68.8 s of editor time (`guide-result.json`) |
| App template build (Ctrl+B) | ok - no auto-build runs after New Project; the chapter says so |
| PendulumLab2 module build (live loop after the src/ edits) | ok; `LabScreen`, `StoppedScreen`, `PendulumService` registered |
| Swing (Play -> Start, 3 s sampled) | `pendulum.angle_deg` min -5.000 max +4.999, 3 sign changes, 1692 samples; producer `PendulumService`; PhasePlot hosted panel drawn |
| Screens | Home -> Lab (click on Start) -> Settings -> Back -> Stopped (pushed at energy 0.00991 with damping 1.0; stack depth 2) -> Resume pops + resets -> key:Escape -> Home |
| Live loop | builds 2 / resumes 2 / failures 1: edit -> **Building...** -> resumed on the same state with the bus intact, chip **Live**; syntax error -> **Build failed**, Play stays stopped; fix -> resumes |
| Package (File > Package... > Package, Build Release first) | `C:/dev/Cosmic-ap-guide/dist/PendulumLab2`, **66 files** (`exported/files.txt`, `exported/package-sha256.txt`) |
| Exported exe from another directory | run from `build\guide-run12\exported\cwd-elsewhere`, exit 0, wrote 0 files into that directory; log `PlayerLayer: running project 'PendulumLab2' (flow 'flows/Main.cflow', 240 Hz)`; Home -> Start (click at the button found in the capture, rows 490-525) -> Lab (`20-exported-lab.png`) -> Escape -> Home showing `Last period estimate: 2.007 s` (`22-exported-home-again.png`) |

`project/` holds the authored project as the run left it (scenes, flow, src, manifest; `.bak`
files removed). The exported exe writes only under `dist/PendulumLab2/user/` (portable mode:
`imgui.ini`, `logs/`).

## 3. Defects found (not registered - AP-Q1 owns the KI register; proposed texts below)

1. **ScreenScaffold put the screen include above the header comment.** `InsertIntoModule` located
   `CS_MODULE_BEGIN` with a plain `find`, and every template's `Module.cpp` mentions it in its `//`
   header (`// CS_MODULE_BEGIN/END expand to...`), so "the last #include before CS_MODULE_BEGIN" was
   none and the include landed at line 3, inside the comment block. Failing-before:
   `ki-scaffold-include/failing-before-Module.cpp` (run 1). Fix: the first occurrence that starts
   its line (`ScreenScaffold.cpp`). Passing-after: `ki-scaffold-include/passing-after-Module.cpp`.
   Compiled either way (the headers include Cosmic.h), which is why AP-03's E02 did not see it.
   *Proposed KI:* "Screens > New Screen / Create script inserted `#include "screens/<Name>Screen.h"`
   at the top of Module.cpp because the template's header comment contains the words
   CS_MODULE_BEGIN; fixed on ap/guide (`ScreenScaffold::InsertIntoModule` matches the macro at line start)."
2. **The packaged app's UI hit-testing was offset by the workspace chrome.** `PlayerLayer::UpdateUI`
   fed `Input::GetMousePosition()` (window-client) against a framebuffer-local viewport rect, so
   every button in a shipped app answered clicks ~54 px above it (the menu bar + Viewport tab).
   Failing-before: `ki-player-ui-pointer/failing-before-*` (a click on Start's pixels opened
   Settings; run 7). Fix: pointer = `GetMouseScreenPosition() - GetViewportPos()`, scaled to the
   framebuffer (`Cosmic/src/layers/PlayerLayer.cpp`). Passing-after: `ki-player-ui-pointer/passing-after-*`.
   SF_Telem's shipped exe has the same player; nothing in the retained suite clicks a game-view
   button in the standalone host.
   *Proposed KI:* "Standalone PlayerLayer: UI pointer used window coordinates against the framebuffer
   rect - clicks on in-game buttons landed one chrome-height too low; fixed on ap/guide."
3. **New Project picker showed `@PROJECT_NAME@` as the App template's description.** `ListTemplates`
   takes a template README's first line as the description; the app template's README opens with
   `# @PROJECT_NAME@`. Failing-before / passing-after: `ki-template-picker-description/`. Fix: ignore a
   first line carrying the token (`StarforgeAppPlatform.cpp`).
   *Proposed KI (cosmetic):* "Template picker: README title token shown instead of the App
   description; fixed on ap/guide."
4. **Not fixed - the packager stages the editor's `*.cscene.bak` backups** (`Packager.cpp`'s
   `SkipContentEntry` and `installer/Stage-AppPackage.ps1`'s `$skip` are AP-P1's hand-synced pair; both
   would need the rule). 66 files instead of 62 here; harmless. Documented in the chapter's
   troubleshooting. *Proposed KI:* "Package Project stages `scenes/*.cscene.bak`; add `*.bak` to both skip lists."
5. **Not fixed - no auto-build after New Project.** By design of the watcher (only edits after the
   project opened), but the App template's `Live` chip suggests otherwise; the chapter tells the reader
   to press Ctrl+B. *Proposed KI (UX):* "kind = app: consider building once after scaffold, or say
   `press Ctrl+B` in the chip tooltip."
6. **Observed once, not reproduced - the editor exited silently in Play while the `when` guard pushed
   the Stopped overlay** (run 10: last engine log line 02:03:34, no result JSON, no WER record, no dump
   facility in the tree). Runs 4-9, 11 and 12 passed the same step. Logged for the record; nothing to
   register without a reproduction.
7. Cosmetic: the Package dialog's `Icon: none set - see File > Project Settings...` renders the
   triangle glyph (U+25B8) as the missing-glyph box (visible in `18-package-done.png`).

## 4. Caveats

- The New Project / New Screen dialogs are opened by the injected click but filled programmatically
  (the same `NewProjectAt` / `ScreensPanel::NewScreen` the **Create** buttons run); the flow edits go
  through `FlowAsset` Load/Save, not typed into the inspector; the C++ is copied from
  `Projects/PendulumLab/src`. Stated in the chapter's last section.
- Game-view clicks in Play are real: OS cursor position (the engine polls it) plus button messages
  posted to the editor's own HWND (so no other window can receive them). A second Starforge instance
  (AP-Q1's) was on the desktop during the runs; the exported-exe captures therefore use
  `PrintWindow(PW_RENDERFULLCONTENT)` - the app's own pixels even when overlapped.
- Editor screenshots are GL back-buffer readbacks (the L05 pattern), immune to overlap.
- The docs coverage checker passed without a manifest change (guide chapters are exempt from strict mode).

## 5. Gates (end of lane, Release) - see `gates.txt`.
