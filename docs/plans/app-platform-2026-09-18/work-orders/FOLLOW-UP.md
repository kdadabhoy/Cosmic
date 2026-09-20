# Cosmic — post-campaign close-out prompt (written 2026-09-20 04:00; paste into a fresh Claude Code session on a clean clone, Opus 5, effort high)

~~~text
You are the orchestrator for the Cosmic engine's post-campaign close-out in <repo root> (Windows; C++/CMake;
PowerShell 5.1 + Git Bash; VS-bundled cmake path and standard commands in
docs/plans/app-platform-2026-09-18/work-orders/README.md). You do not implement work yourself: spawn one
background subagent per item (Agent tool, general-purpose, model "opus", run_in_background), verify on disk,
then spawn the next. Serial items run alone on main; parallel items get a worktree INSIDE <repo>\build\_lanes\
(gitignored), never in the parent folder. Rules: commit as kdadabhoy <kdadabhoy28@gmail.com> with no
Co-Authored-By / AI trailer; never push (Kaden pushes); never touch branch engine-3d or the tags
cosmic-pre-2d-2026-09-16 / cosmic-app-platform-g5-2026-09-20; a "failed" agent notification says nothing about
the disk (inspect git status/log first); every defect is registered in
docs/plans/2d-stability-2026-09-16/contracts/known-issues.md BEFORE fixing (next free number is in
work-orders/README.md line 13) with failing-before / passing-after evidence; both audits
(tests\check_gl_conformance.ps1, tests\check_docs_coverage.ps1) and tests\check_docs_links.ps1 exit 0 after
every landed item; retained manifests rewrite tracked evidence files — `git checkout --` them and delete
n04-*.bin / recordings/ from the tree root before committing; the acceptance runner's -Manifest needs an
absolute path and fixtures need -TempRoot <repo>\build\_temp\<name>. Read first, and only: the memory file
notes below, docs/plans/00-MASTER-ROADMAP.md, docs/plans/app-platform-2026-09-18/evidence/AP-Q1/release-report.md
sections "pending" and "deferred", and the work-order prompt for each item when you spawn it.

Items, in order (each gets its own agent; report to Kaden in one paragraph after each lands):

1. AP-D2 docs content — prompt: docs/plans/app-platform-2026-09-18/work-orders/AP-D2.md (~~~text block).
   Adapt: AP-D1 has landed (archive/, parked-3d/, link checker exist); the four AP-01 rows in
   docs/reference/README.md must be re-pointed at the new ../guide/app-authoring.md; the DOC02 walkthrough
   is executed literally — reuse docs/guide/pendulumlab-walkthrough.md and its driver
   tests/acceptance/fixtures/Run-GuideWalkthrough.ps1 as the executed proof rather than writing a second one.

2. KI-63 — the packager stages scenes/*.cscene.bak: add the *.bak rule to BOTH
   Projects/Starforge/src/Packager.cpp (SkipContentEntry) and installer/Stage-AppPackage.ps1 ($skip), add a
   .bak fixture to the K02 comparison, re-run the pr-units and apq1-y02 manifests, close the KI.

3. Dead-3D tidy, remainder — evidence/AP-05/report.md "now-dead 3D-only surface" minus what AP-Q1 removed
   (RenderQueue, Frustum, GpuCameraBlock, TextureCube, UniformBuffer, StorageBuffer, 8 shaders): cameras
   (CAD orbit/fly), Mesh/Material/.cmat loaders, SceneRenderer 3D settings, PostProcess 3D effects, DebugDraw
   3D verbs. Grep + build per removal, each its own commit, B06 oracle and both configs 0-warn after each;
   docs/reference/README.md rows for deleted headers removed (coverage checker must stay 0).

4. Soaks (run overnight; one agent, sequential, Windows job objects, never a manual kill counted as pass):
   Y03 PendulumLab 2 h, S01 SF_Telem 2 h, S02 AnalysisSample 2 h, N02-drift-2h, T05 — exact commands in
   release-report.md section 9. Results appended to evidence/AP-Q1/soaks.md; any plateau/drift failure → KI.

5. Hardening WO (write the one-page prompt first as docs/plans/app-platform-2026-09-18/work-orders/AP-H1.md,
   then run it): (a) a ground-control dry run — an AppService on SerialLink over the FakeSerialTransport
   seam feeding 8 channels at 50 Hz into UiPlot/UiGauge/UiValueText for 60 min, oracles: no dropped frames
   >100 ms, memory plateau, plot decimation ≤512 segments; (b) scale profile — 200 channels, 6 plots at full
   WindowSeconds, record frame time Debug/Release; (c) the App template's "Live" chip / no auto-build after New
   Project UX (AP-03 nit): build once after scaffold for kind=app or fix the chip tooltip; (d) the
   unreproduced silent editor exit during the `when` push (GUIDE report §3 item 6): add a crash-dump facility
   (MiniDumpWriteDump on unhandled exception, dumps under user://logs) so the next occurrence is diagnosable.

6. Close-out: update docs/plans/00-MASTER-ROADMAP.md statuses, regenerate the release report's matrix with
   the soak results, stage the push + tag commands for Kaden (unexecuted).

Memory notes (state at hand-off, 2026-09-20): main = 918add9 (AP-D1 docs restructure + clone-and-run cleanup + KI-64/65 pointer lane) pushed; qualified
SHA fa1223a (tag cosmic-app-platform-g5-2026-09-20); CosmicTests 523 passed / 14 skipped both configs; tests/check_docs_links.ps1 exists and runs in CI;
CosmicRenderTests 45/45, 15 goldens; KI-1..65 registered (61/62/64/65 fixed, 63 open). Deferred by decision, not
by failure: AP-D2, the soaks, KI-63, the tidy remainder, hardening.
~~~
