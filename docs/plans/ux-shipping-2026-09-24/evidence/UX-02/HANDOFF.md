# UX-02 hand-off (hard stop, 2026-09-24 ~21:45) — branch `ux/02`, base `6021822`, NOT rebased

**Verified (Debug + Release, full builds 0 warnings; CosmicTests 530 passed / 0 failed / 14 skipped both, baseline 523):**
- ED02 (KI-76 `VisitUi` Active): PASS / PASS (`ed02-*`). ED-UNITS (suite `UX-02 editor units`: ED01 ApplyModel + CapturesMove + Owns, ED03 ForSignal Flow hits, ED04 ProjectScenes, ED05 prefs round trip): PASS / PASS (`ed-units-*`).
- UX02-EDITOR self-test (`ux02-*/ux02-result.json`): ED01 PASS/PASS (KI-72: 0 transform-gizmo calls with Plot; KI-73 centre-square drag under an overlay; KI-75 OS press-drag moves the sprite by the world delta, 1 entry, undo), ED03 PASS/PASS, ED05 PASS/PASS (+ the wrapper's OnDetach autosave-copy oracle PASS), **ED04 FAIL/FAIL** in the runner: only the File ▸ Open Scene leg (the pure hover never opened the submenu; the Scenes section, its double-click and the lister checks passed). It passed in the earlier direct wrapper runs.
- Failing-before on isolated patches: `failing-before-excerpts.txt` (KI-72/73/75 self-test; KI-73/74/76 units).
- Retained: ap03-editor Release PASS; Debug FAIL on V05 only, caused by `Save FAILED`/atomic rename (KI-79, environmental race, not UX-02 code) — rerun needed. wo07-ki1 PASS/PASS, wo07-l02 Debug PASS (Release killed by the stop, exit 127), wo09-editor PASS/PASS, wo10-sample NOT RUN. Checkers gl_conformance / docs_coverage / docs_links: exit 0.

**Written but unverified:** `UX02EditorSelfTest.cpp` step "ED04 click Open Scene (the submenu)" (a click replaces the hover; not rebuilt, not rerun). `evidence/UX-02/report.md` not written (the agent harness refused .md report files); the evidence is the excerpts + result JSONs.

**Left:** rebuild + rerun ux02-editor both configs (expect ED04 green); rerun ap03-editor Debug, wo07-l02 Release, wo10-sample both; write report.md (WO-10 layout). Phase B: `git rebase main` (UX-01 in; keep both sides in StarforgeApp.cpp/.h, tests/CMakeLists.txt, the KI register), add `|| m_Editors.AnyDirty()` to `StarforgeApp::UnsavedWork` (StarforgeAppPrefs.cpp) and let Save also save dirty documents, check UX-01's `HarnessSelection()` == (2,0) in ED03's verify step, rebuild, rerun, commit.

**Next commands** (in `C:\dev\Cosmic\build\_lanes\ux-02`, `$env:COSMIC_SDK` = that folder, VS-bundled cmake):
```
cmake --build build --config Debug --parallel 4 ; cmake --build build --config Release --parallel 4
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\build\_lanes\ux-02\tests\acceptance\manifests\ux02-editor.manifest.json -Config Debug -TempRoot C:\dev\Cosmic\build\_lanes\ux-02\build\_temp\ux02 -OutDir C:\dev\Cosmic\build\_lanes\ux-02\build\_temp\acc-ux02-Debug   (then -Config Release)
same for ap03-editor (Debug), wo07-l02 (Release), wo10-sample (both); then git checkout -- docs/plans/app-platform-2026-09-18/evidence docs/plans/2d-stability-2026-09-16/evidence
```
Remove a runner's `scratch-*` dirs by deleting their junctions first (they link build\Runtime assets).

**KIs used:** KI-72..76 (a..e, fixed), KI-78 (stale build reloads into the next project and clears Dirty; open), KI-79 (`SaveScene` returns true on a failed write; open). KI-77 = UX-03.
**Move-capture choice:** the rect gizmo's 10x10 px centre square always captures the selection; the rest of the rect only when topmost (whole-rect capture would make elements drawn over the selection unclickable).
**Gotchas:** the OS cursor is shared by lanes (ED01's sprite leg retries a disturbed input ≤2x, logged); a pure ImGui hover can be overridden by the GLFW poll of a real cursor outside the window — use clicks; opening PendulumLab queues an auto-build, the self-test waits it out (KI-78); `Edit ▸ Preferences…` changes write the child CWD's editor.toml only.
