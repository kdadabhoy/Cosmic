# AP-P1 — One package identity, writable user data, acceptance runner in CI

**Gate:** G2 · **Wave:** 1 (worktree; may run alongside AP-05/AP-01) · **Runs:** worktree `ap/p1` ·
**Base:** the AP-00 commit; rebase onto AP-01 before landing · **Depends on:** AP-00 · **Acceptance:**
K01, K02, K04, H05 (K03 honestly blocked without a Windows 10 machine) · **Model:** Opus 5 ·
**Status:** not started

The former WO-11, plus wiring the acceptance runner into CI. Three packaging paths exist today with
two layouts (editor `Packager` → renamed exe + `boot.cfg`; `package.bat`/`cmake --install` →
`CosmicApp.exe --project` + `projects/`; `release.yml` → hand-staged). One layout wins: the editor's.
SF_Telem must write only under `user://`. The runner's `pr` profile must run on every push.

## Copy-paste prompt

~~~text
Execute only AP-P1 from the Cosmic App Platform packet (docs/plans/app-platform-2026-09-18/). Read
work-orders/README.md (all, especially lane rules L1-L5), then ONLY 01-Design-Contracts.md sections
10 (your row) and 12, 03-Acceptance-Catalog.md rows H05 and the retained K01-K04 (in the stability
catalog, ../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md lines 205-215), and the old WO-11
prompt at ../2d-stability-2026-09-16/work-orders/WO-11.md (history; this prompt wins).

Lane setup: from C:\dev\Cosmic run  git worktree add ..\Cosmic-ap-p1 -b ap/p1 <AP-00 commit sha>  ,
work only inside C:\dev\Cosmic-ap-p1, set $env:COSMIC_SDK to that path in your shell, build into its
own build\ directory. AP-05/AP-01 may be running on main at the same time; you never touch their files.

Do, in order:
1. Reconcile the three packaging paths to ONE layout: <App>.exe (renamed CosmicApp.exe), Cosmic.dll,
   <App>.dll, assets/ (engine assets minus projects/), assets/projects/<App>/, boot.cfg, user/ placeholder,
   licenses. The editor Packager (Projects/Starforge/src/Packager.cpp) is the reference; make
   package.bat <App> and .github/workflows/release.yml produce the identical file list (compare sorted
   relative-path lists and record them). cmake --install keeps working but is no longer a shipping
   path; say so in package.bat's header. Enforce the 2D identity through all of them (the configure
   passes -DCOSMIC_2D_ONLY=ON; after AP-05 it is a no-op but stays). No dev-only target (CosmicTests,
   fixtures) may be staged. Add option(COSMIC_2D_ONLY "..." ON) to Projects/SF_Telem/CMakeLists.txt like
   AnalysisSample's.
2. Writable user data (section 12): change only the path lines in Projects/SF_Telem/src/SF_Telem.cpp
   so logs go to user://logs and recordings to user://recordings/SF_Telem; do not touch anything else in
   SF_Telem. Prove from a read-only install location (icacls a staged copy read-only, run as a normal
   user): record, autosave, export, replay, config write, screenshot all succeed and land under
   %LOCALAPPDATA%\SF_Telem; opening the window is NOT sufficient. Write the final policy text into
   01-Design-Contracts.md section 12.
3. K01: build an external consumer (Projects/AnalysisSample, standalone with -DCOSMIC_SDK_DIR) from a
   CLEAN SDK copy (a fresh clone or an export with build/Runtime/<cfg> only) — no source-relative hidden
   dependency; PUBLIC 2D mode and DLL/CRT identity agree (dumpbin the CRT imports). K02: package
   SF_Telem and AnalysisSample through package.bat AND through the editor path (drive the editor with
   the existing X01 package harness, Run-WO10Sample.ps1 pattern); inspect the payloads. K04:
   install/reinstall/uninstall on a disposable location; older v1 recording fixtures stay readable. K03
   needs a clean Windows 10 AND 11 machine without a compiler: run what you can on this Windows 11 box
   and mark the Windows 10 legs ENVIRONMENT_BLOCKED with the prerequisite named — never as passed.
4. H05: add a job "acceptance-pr" to .github/workflows/ci.yml that, after the units, runs
   tests/acceptance/Run-Acceptance.ps1 -Profile pr over the manifests that carry the pr profile (write
   tests/acceptance/manifests/pr-*.manifest.json aggregating the U/W cases the stability campaign left
   green plus ap05-purge when it exists), fails on any FAILED, uploads results.json/JUnit, and reports
   G/I/Q cases as ENVIRONMENT_BLOCKED (hosted runners have no GPU/editor). Keep the 300-case discovery
   floor. Drop stale 3D mentions from the workflows (release.yml's Frontier example etc.). Run the
   profile locally exactly as CI would.

Evidence in evidence/AP-P1/report.md (WO-10 layout): the three file lists, the read-only-install proof
(paths written), K01-K04 results with blocked legs named, the CI profile run. Land per L2: rebase onto
main after AP-01 has landed, rebuild Debug+Release 0-warn, retained units both configs, your manifests,
both audits, commit on ap/p1 as kdadabhoy with no AI trailer. Do not push, do not merge into main
yourself unless the packet's integrator step is assigned to you; leave the branch ready. Return: the
unified layout, the writable-path proof, K/H results, and the exact merge command for the integrator.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `01-Design-Contracts.md` §10 (row), §12; the K01–K04/H05 rows;
`Projects/Starforge/src/Packager.{h,cpp}`; `package.bat`; `.github/workflows/{ci,release}.yml`;
`Runtime/CMakeLists.txt`; `Projects/SF_Telem/src/SF_Telem.cpp` around `:62`.

## Owns / May touch

See §10 (AP-P1 row). Nothing else; `Projects/SF_Telem/src/SF_Telem.cpp` only at the path lines.

## Scope

- **In:** layout unification, writable user data, external-consumer build, K01–K04 evidence, CI
  acceptance job, workflow cleanup.
- **Out:** installer feature changes; publishing; any SF_Telem behaviour beyond paths; PendulumLab
  packaging (AP-Q1 runs K02 for it later).

## Deliverables

Reconciled `Packager`/`package.bat`/`release.yml`; SF_Telem path change + option; `pr-*` manifests;
CI job; §12 policy text; `evidence/AP-P1/report.md`.

## Done when (DoD)

Identical file lists from all three paths; read-only-install writes succeed under `%LOCALAPPDATA%`;
K01/K02/K04 pass, K03 blocked legs named; CI `pr` profile runs green locally with blocked tiers reported.

## Rollback

Revert the lane's commits; the previous packaging paths are unchanged in behaviour otherwise.
