# AP-P1 — one package identity, writable user data, acceptance runner in CI

Status: complete, 2026-09-19. Lane `ap/p1`, worktree `C:\dev\Cosmic-ap-p1`, base `7479927`
(the AP-00 follow-up commit). Not rebased onto `main` and not merged: AP-01 has not landed.

## 1. Scope and provenance

Executes AP-P1 from `docs/plans/app-platform-2026-09-18/work-orders/AP-P1.md`: reconcile the three
packaging paths to one layout, move SF_Telem's writable data under `user://`, prove K01-K04 (K03's
Windows 10 legs honestly blocked), and add the `acceptance-pr` job that runs the acceptance runner's
`pr` profile in CI (H05).

Read for this work order: `work-orders/README.md` (all, lane rules L1-L5); `01-Design-Contracts.md`
§10 (AP-P1 row) and §12; `03-Acceptance-Catalog.md` row H05; the retained K01-K04 rows in
`../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md:205-215`; the superseded WO-11 prompt (as
history). Where the prompt and an older document disagree, the prompt won.

## 2. Toolchain and environment

| Item | Value |
| --- | --- |
| Machine | `DESKTOP-SEOA4BT`, Windows 11 Education build 26200 |
| CPU / RAM | AMD Ryzen 7 7800X3D (8C/16T), 31.2 GB |
| GPU | NVIDIA GeForce RTX 5070 Ti, driver 32.0.16.1692 |
| Toolchain | VS 18 2026 Community, MSVC 14.51.36231, Windows SDK 10.0.26100.0, cmake 4.3.1-msvc1 |
| Shell | Windows PowerShell 5.1.26100.9444 |
| Cache | `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=OFF`, `COSMIC_WITH_JOLT=ON`, `COSMIC_SKIP_PROJECTS=AnalysisSample` |

Configure: `cmake -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`.

## 3. What was built

### 3.1 One package layout

`Projects/Starforge/src/Packager.cpp` stays the reference and now also stages `licenses/` and the
`user/` placeholder. `installer/Stage-AppPackage.ps1` (new) is the CLI implementation of the same
layout, and **both** `package.bat <App>` and `.github/workflows/release.yml` call it, so the two CLI
paths cannot drift from each other by construction and only one hand-synced pair (C++ to PowerShell)
remains - checked by the file-list diffs in §5.

```
<App>.exe                  renamed CosmicApp.exe
<App>.dll                  only this app's plugin DLL
Cosmic.dll
boot.cfg                   names <App>; also sets the per-app user:// identity
assets/**                  engine assets, minus assets/projects/**
assets/projects/<App>/**   only this app's content (no src/, build/, .git/, CMakeLists.txt)
licenses/**                from installer/licenses/MANIFEST.txt (one source of truth)
user/README.txt            portable-mode writable-root placeholder
```

The stager refuses to finish if a dev-only target (`CosmicTests`, `CosmicRenderTests`, a
`WO0*Fixture.dll`) or a `.pdb`/`.lib`/`.exp`/`.ilk` reaches the payload. `cmake --install` still
works and still produces the developer SDK bundle (`package.bat` with no argument); it is no longer
a shipping path, stated in `package.bat`'s header, `Runtime/CMakeLists.txt` and the root
`CMakeLists.txt` at the install rules.

`installer/CosmicSetup.iss` had a real defect: its shortcuts launched `CosmicApp.exe --project <App>`,
and an explicit `--project` makes `Runtime/Main.cpp` skip the `boot.cfg` branch - the only code that
calls `FileSystem::SetAppIdentity`. The installed app therefore fell back to the shared user root and,
with a writable install directory, wrote its data inside the install folder. Shortcuts now launch
`{app}\<App>.exe` with no flags. (`installer/AppSetup.iss` was already correct.)

### 3.2 Writable user data (contract §12, rewritten in `01-Design-Contracts.md`)

`Projects/SF_Telem/CMakeLists.txt` gained `option(COSMIC_2D_ONLY ... ON)` plus the matching
`target_compile_definitions`, like AnalysisSample's, so a standalone SF_Telem build agrees with the
2D SDK it loads into. The path changes themselves are listed under "Contract deviations" (§10).

### 3.3 CI (H05)

`.github/workflows/ci.yml` gained the `acceptance-pr` job (after the units, restoring the same build
cache entry, failing loudly if the binaries are absent) and three manifests
(`tests/acceptance/manifests/pr-{units,windows,blocked}.manifest.json`) aggregating the U/W cases the
stability campaign left green, plus the G/I/Q cases that must show as `ENVIRONMENT_BLOCKED`. The job
picks up `ap05-purge.manifest.json` automatically once AP-05 lands it. The 300-case discovery floor
is untouched. The stale `docs/plans/07-installer-packaging-plan.md` comment at the end of `ci.yml`
is gone; the workflows carry no 3D project mentions (the `Frontier` example the prompt cites was
already replaced in `release.yml` before this lane started - nothing to remove).

A new capability `render-tests` (`AcceptanceRunner.psm1`) reports whether `CosmicRenderTests.exe`
exists in the bin dir, so a G case on a tree built without `-DCOSMIC_BUILD_RENDER_TESTS=ON` is
`ENVIRONMENT_BLOCKED` naming that prerequisite instead of a MISSING-executable failure.

## 4. Acceptance-case status

| ID | Verdict | Evidence |
| --- | --- | --- |
| K01 external consumer from a clean SDK | **PASS** | `k01-excerpts.txt` |
| K02 package through CLI + editor paths | **PASS** | `k02-k04-excerpts.txt`, `k02-layout-excerpts.txt`, `layout/*.files.txt` |
| K03 Windows 11 read-only install | **PASS** (installed launch, arbitrary CWD, spaces + non-ASCII path) | `k02-k04-excerpts.txt` |
| K03 Windows 10 legs | **ENVIRONMENT_BLOCKED** - prerequisite: a clean Windows 10 x64 machine with no compiler/SDK/dev PATH. None exists in this environment; not run, not passed. | - |
| K03 record/export driven *inside* a read-only dir | **ENVIRONMENT_BLOCKED** - prerequisite: a test host that starts from a non-writable working directory (**KI-58**), or an SF_Telem UI-automation harness not hosted by the dev-only `CosmicTests.exe` | `readonly-writes-excerpts.txt`, `ki58/repro-excerpts.txt` |
| K04 update / reinstall / uninstall + v1 fixtures | **PASS** | `k02-k04-excerpts.txt` |
| H05 `pr` profile, run locally exactly as CI | **PASS** - 21 passed / 0 failed / 4 env-blocked (of 25), exit 0 | `acceptance/pr-profile-excerpts.txt`, `acceptance/*/results.json` |

Retained gates at the end of the lane: Debug and Release builds **0 warnings**; `CosmicTests`
**454/454 in both configs** (Debug 23,405,298 assertions, Release 23,465,503), run back to back on
one machine without clearing `%TEMP%\wo06`; `tests/check_gl_conformance.ps1` clean;
`tests/check_docs_coverage.ps1` clean (151 public headers, 148 manifest rows, 7 skeletons).

## 5. The three file lists

`layout/SF_Telem.packagebat.files.txt` (what `package.bat SF_Telem` stages) and
`layout/SF_Telem.releaseyml.files.txt` (the `release.yml` step's own invocation, relative paths and
all) are **identical, 75 paths**. `layout/SF_Telem.cli.files.txt` is the same list produced by the
K02 harness.

Top-level shape (SF_Telem, 75 paths): `assets` x60, `licenses` x10, `SF_Telem.exe`, `SF_Telem.dll`,
`Cosmic.dll`, `boot.cfg`, `user/README.txt`. `assets/projects/` contains only `SF_Telem/` (6 files).

For the **editor** path the comparison uses an EXTERNAL app, because that is the path the editor
drives end to end: `layout/AnalysisSample.editor.files.txt` (Starforge > File > Package via the
existing X01 harness) vs `layout/AnalysisSample.cli.files.txt` (`Stage-AppPackage.ps1` with
`-AppDllPath`) - **identical payload, 72 paths**. The editor dist additionally holds 6 files under
`user/` that the X01 harness's *run* of the packaged app wrote (portable mode: `user://` is
`<exe>/user/`); they are runtime output, not staged payload, and are listed explicitly in
`k02-layout-excerpts.txt`. That the run produced them is itself evidence that portable mode works.

## 6. The writable-path proof (paths actually written)

The real packaged `SF_Telem.exe` was installed to `...\install root Unicode\SF_Telem app` (spaces
plus a non-ASCII character in the real path), the tree was denied write access for the running user,
and the app was launched with **no flags** from an arbitrary working directory (`C:\Windows`), then
closed with `CloseMainWindow()`:

- exit **0**;
- the install tree is **byte-identical** afterwards (SHA-256 manifest before/after: 0 added,
  0 changed, 0 removed) - nothing was written into it;
- `%LOCALAPPDATA%\SF_Telem\imgui.ini` - config write;
- `%LOCALAPPDATA%\SF_Telem\logs\App_*.log` and `...\logs\Cosmic_*.log` - log writes, non-empty.

Record / autosave / export / replay / screenshot were driven through the **real** app on the
production path (the L05 host fixture: real `Workspace::SF_Telem`, real serial chain via the WO-04
fake transport, real navigation buttons) from a **writable staged package**: 12 cycles, 124 judged
actions, 0 failures, replay load 12 / unload 12, 1 native dialog opened and cancelled, 3 screenshots,
recording 5,144 bytes under the package's own user root, ImGui oracle 0 errors / 0 leaks / 0 drift.
Repeating that inside the **read-only** copy is blocked by KI-58 (§7) and is reported as blocked, not
as a pass; the same run did confirm that nothing was written into the read-only directory.

Section 12 of `01-Design-Contracts.md` now carries the final policy text (the rule, the `user://`
mapping table, SF_Telem's paths, the one layout, and the uninstall policy).

## 7. Defects (registered before fixing)

**KI-57 - `CosmicTests` is not idempotent (stale `%TEMP%\wo06` scratch).** Found independently by
this lane and by AP-05A (which registered it and did not fix it); the register entry says so. `WO-06
D01` ends by copying `bad-version.bin` over `%TEMP%\wo06\fallback\scene.bin`, and `Scratch()` never
cleared it, so the next process to run D01 loaded the leftover and failed `REQUIRE(p.Load(...))` at
`tests/test_wo06.cpp:208`.
*Failing-before* (`ki57/failing-before-excerpts.txt`): run 1 SUCCESS 145 assertions, run 2 FAILURE at
`:208`; a full Debug-then-Release pass reported 454/454 then **453/454**.
*Fix*: `Scratch()` clears each named directory once per process - not per call: several cases call
`Scratch("x")` again to read back what they just wrote, and clearing per call deleted the data under
the test (it broke D04 on the first attempt, which is how the per-name form was arrived at).
*Passing-after*: `WO-06 *` 23/23 twice back to back, and the full suite **454/454 in both configs**
consecutively without clearing the scratch. The `pr` profile also carries `D01-IDEMPOTENT`, which
runs D01 three times in three processes.

**KI-58 - `CosmicTests.exe` fail-fasts at startup when its working directory is not writable.**
Found while building the read-only proof. With an **empty** directory as the working directory and
write access denied, `CosmicTests.exe --count` exits `-1073740791` (`0xC0000409`, what the release
CRT's `abort()` raises) with zero bytes on stdout and stderr; with a writable working directory the
same command exits 0 and reports 454 cases (`ki58/repro-excerpts.txt`). It is a **dev-only** binary -
the packaged `SF_Telem.exe` runs fine from exactly such a tree (§6) - so it is registered open, not
fixed: the startup code is outside AP-P1's ownership.

## 8. Measured numbers

- Package payload: SF_Telem **75 files**, AnalysisSample **72 files**.
- Clean SDK export for K01: **3,836 files, 74.1 MB** (`Cosmic/src`, `Cosmic/dependencies`,
  `build/Runtime/Release`, `installer/licenses`; no `.git`, no `Projects/`, no `tests/`, no root
  `CMakeLists.txt`).
- K01 CRT identity: `Cosmic.dll` and `AnalysisSample.dll` import the **same** release CRT set
  (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, 11 x `api-ms-win-crt-*`); no debug CRT;
  consumer machine `8664 (x64)`; `COSMIC_2D_ONLY` defined on the consumer's TUs (8 occurrences in the
  generated vcxproj); the consumer's `CMakeCache.txt` contains **no** path into `C:\dev\Cosmic-ap-p1`.
- X01 editor package (K02): build 37.4 s, packaged run exit 0, 1,201 rows, 12 scrubs, worst
  marker-vs-oracle 4.15e-5 m, worst plot-vs-equations 8.78e-5 m, staged `trajectory.csv` SHA-256
  equals the committed fixture.
- `pr` profile: 25 cases - 21 passed, 0 failed, 4 env-blocked (`render-tests`, `editor-ui`,
  `serial-device`, `sf-stable-v1-capture`).

## 9. Caveats

- **Windows 10 is not covered.** Every Windows 10 leg of K03 is `ENVIRONMENT_BLOCKED`; only Windows 11
  build 26200 was exercised. The installer itself (`iscc`) was not compiled either - Inno Setup is not
  installed here - so K04 exercises install/reinstall/uninstall as file operations against the staged
  payload plus the documented `%LOCALAPPDATA%` policy, not the compiled setup executable.
- The one-layout gate between the **editor** and the CLI is proved for an external app
  (AnalysisSample) and between the two CLI paths for an in-tree app (SF_Telem). The editor path for
  an in-tree app was not separately driven; it is the same `Packager::Stage` call with a different
  `ProjectContentDir`.
- `Cosmic.dll` in the 2D build still exports 39 symbols containing `SceneRenderer` and 5 containing
  `Skeleton` (0 for `Scene3D` / `NavWorld`). That is AP-05's purge scope - observed and reported here,
  not touched.
- `AnalysisSample` is a compatibility specimen (synthetic F-TRAJECTORY / F-SERIES-LARGE fixtures), not
  a qualified downstream consumer; the D-9km caveat from the stability catalog still stands.

## 10. Contract deviations

1. **`Projects/SF_Telem/src/TelemHub.{h,cpp}` and `DrivetrainLayer.cpp` were edited**, although §10
   lists only `Projects/SF_Telem/src/SF_Telem.cpp` (path lines). §12 assumed both paths lived in
   `SF_Telem.cpp:62`; they do not. Every edit is a path line or its comment, nothing else:
   - `SF_Telem.cpp` `OnAttach` - `Resolve("project://logs")` to `Resolve("user://logs")`;
     `OnDetach` - `SetLogDirectory("logs")` to `SetLogDirectory(Resolve("user://logs"))`.
   - `TelemHub.h:279,283` - `k_RecordDir` / `k_AutoSaveDir` `"recordings/SF_Telem"` to
     `"user://recordings/SF_Telem"` (+ `/_autosave`). The recordings path lives here, not in
     `SF_Telem.cpp`.
   - `TelemHub.cpp` - the 7 USES of those two constants wrapped in
     `Cosmic::FileSystem::Resolve(...)` (`:131` replay path, `:186`, `:635`, `:651` `Flush`, `:610`
     `SetAutosave`, `:638`, `:654` the status string). Required because `DataRecorder` writes raw
     paths and does not resolve the VFS prefix; without it the app would create a literal `user:` folder.
   - `DrivetrainLayer.cpp:1129` - the CSV **export** target `Resolve("project://logs")` to
     `Resolve("user://logs")`. It is a write into the app's read-only content root that fails
     silently from an installed location (`create_directories` takes an `error_code`).
   No behaviour outside these path lines was changed; in a dev tree they all resolve to the same
   `./logs` and `./recordings/SF_Telem` as before, which is why the 454-case suite is unchanged.
2. **`tests/test_wo06.cpp` was edited** (not in AP-P1's Owns): the KI-57 fix, `Scratch()` only. It is
   the passing-after for a registered defect that otherwise makes `ci.yml`'s own Debug-then-Release
   unit pass a false red - i.e. it blocks this lane's own deliverable.
3. **`docs/plans/2d-stability-2026-09-16/contracts/known-issues.md`** gained KI-57 and KI-58, per the
   packet's global rule 2. AP-05A registered KI-57 on `main` independently; the integrator keeps one
   entry (this one carries the fix and the failing-before/passing-after evidence).
4. `tests/acceptance/AcceptanceRunner.psm1` and `Run-Acceptance.ps1` (AP-P1's Owns) gained the
   `render-tests` capability and an absolute-`OutDir` fix - the child is launched through `cmd.exe`
   with its streams redirected while its working directory is the isolated run-temp, so a relative
   `-OutDir` made every case come back "exit 1, no output". Found by running the profile locally
   exactly as CI does.
5. `01-Design-Contracts.md` §13 was **not** touched (it is AP-Q1's). The rows
   "Packaging identity + writable user data" and "Acceptance in CI" are now proven and can move off
   "planned" when AP-Q1 finalizes.

## 11. Files

New: `installer/Stage-AppPackage.ps1`, `installer/licenses/{MANIFEST.txt,THIRD-PARTY.txt}`,
`tests/acceptance/fixtures/{Run-RepeatCase.ps1,Run-PackageInstall.ps1,Run-ReadOnlyWrites.ps1}`,
`tests/acceptance/manifests/pr-{units,windows,blocked}.manifest.json`, this evidence directory.

Changed: `.github/workflows/{ci,release}.yml`; `package.bat`; `package_installer.bat`;
`installer/CosmicSetup.iss`; `Projects/Starforge/src/Packager.{h,cpp}`; `Runtime/CMakeLists.txt`;
root `CMakeLists.txt` (install-rule comments only); `Projects/SF_Telem/CMakeLists.txt`;
`Projects/SF_Telem/src/{SF_Telem.cpp,TelemHub.h,TelemHub.cpp,DrivetrainLayer.cpp}`;
`tests/acceptance/{AcceptanceRunner.psm1,Run-Acceptance.ps1}`; `tests/test_wo06.cpp`;
`docs/plans/app-platform-2026-09-18/01-Design-Contracts.md` (§12);
`docs/plans/2d-stability-2026-09-16/contracts/known-issues.md` (KI-57, KI-58).

## 12. Local commits

On `ap/p1` only; nothing pushed, nothing merged, no rebase onto `main` yet (AP-01 has not landed).
See `git log --oneline main..ap/p1`.

**Integrator merge command**, after AP-01 is on `main` and this lane has been rebased onto it:

```
git checkout main
git merge --no-ff ap/p1
```
