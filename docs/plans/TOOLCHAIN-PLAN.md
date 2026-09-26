# Cosmic — Bundled toolchain plan (llvm-mingw): designed now, built later

Status: plan, 2026-09-25. Nothing here is scheduled; it is the brief for a future packet, the way UX-G0's world-mode
design doc is the brief for the world features. The master roadmap links here
([`00-MASTER-ROADMAP.md`](00-MASTER-ROADMAP.md)); the feature row is in [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md).

**Decision D-TOOLCHAIN (Kaden, 2026-09-25)**, recorded in
[`ux-shipping-2026-09-24/00-Start-Here.md`](ux-shipping-2026-09-24/00-Start-Here.md): MSVC stays the **only supported
toolchain** for the engine, the editor and user projects; a bundled llvm-mingw "zero-install" flavour is **designed here
and built later**; the UX & Shipping campaign only adds the friction fixes (the VC++ runtime in every package — SD05,
UX-04; the Starforge compiler check — H1-E/H1-F, UX-H1; the "Install the C++ compiler" step in guide 00 — DG03, UX-D1).
Nothing below is a pass until it has run.

## Goal

**A user can build their project DLL with nothing but Starforge installed** — no Visual Studio, no Build Tools, no
winget step.

Today (D-SHIP) the engine and the editor ship prebuilt in the SDK zip and `Starforge-Setup-<ver>.exe`, but the
project DLL is compiled on the user's machine: Starforge's `BuildRunner` runs cmake with the Visual Studio generator
and MSVC. So every user installs Visual Studio Community 2026 or the Build Tools with "Desktop development with C++" —
several GB — and meets a licence question: VS Community is free for individuals, classroom/academic use, open source
and non-enterprise organisations up to 5 users, and the Build Tools may be used by anyone licensed for Community
([VS Community](https://visualstudio.microsoft.com/vs/community/)). Anyone else needs a paid Visual Studio licence
just to compile a Cosmic app.

## Where the compiler coupling really is (three read-only audits, 2026-09-25, at `60a0536`)

| Layer | Coupling found | What it means |
| --- | --- | --- |
| First-party source | clang-cl compiles it with 0–1 changes. MinGW needs ~5–8 files to **build**: `Core.h:73-74` (`__debugbreak` without `<intrin.h>`), the `#pragma comment(lib)` in `FileDialog.cpp:22-23` and `ContentBrowserPanel.cpp:38` (must move to CMake), `BuildRunner.cpp`, `Packager.cpp:66` (hard-codes `Cosmic.dll`); and ~50 for a **clean** port: 34 files use MS secure-CRT calls (`localtime_s`, `strncpy_s(_TRUNCATE)`, `_dupenv_s`, `_vsnprintf_s`, `_strtoui64` — mostly declared by the mingw-w64 headers). `std::from_chars/to_chars(double)` in `DataExport.cpp:60,110` need a recent libc++. | Small. Changing the compiler is not the hard part. |
| Platform (Win32) | 42 files include `windows.h` (34 unguarded; `JobSystem.h:65` includes it with no Win32 use); DWM borderless chrome in `Window.cpp`; COM file dialogs in `FileDialog.cpp`; Win32 serial; `LoadLibrary` plugin loading; GLFW + glad windowing. | Stays with any Windows compiler; the mingw-w64 headers cover all of it. |
| Build | `-A x64` hard-coded in `BuildRunner.cpp:61`, `StarforgeApp.cpp:3028/3038/3048`, `build*.bat`, `package.bat:88`, `ci.yml:77`, `release.yml:38`, `gpu-qualification.yml:29`, `Run-AP04Sample.ps1:52` (Ninja rejects `-A`); `CMakePresets.json:12-13` pins "Visual Studio 18 2026"; the `Cosmic.lib` import name in 10 CMakeLists (Starforge, SF_Telem, PendulumLab, AnalysisSample, ExampleProject, the 5 templates under `Projects/Starforge/assets/templates`); root `CMakeLists.txt:42`'s non-MSVC branch adds `-std=c++20` to C sources too (clang errors); Jolt `CMakeLists.txt:43-45` sets `JPH_USE_SSE4_x` without `-msse4.2`; `Runtime/CMakeLists.txt:34-37` `/SUBSYSTEM:WINDOWS` (MinGW needs `-mwindows`), manifest embedded only on MSVC; single-config generators ignore `--config` (`kHotConfig`, `BuildRunner.h:79-83`) and the templates set only `IMPORTED_*_DEBUG/_RELEASE`. No prebuilt binaries are committed; every dependency builds from source, and glfw, Jolt and imgui support MinGW. | Mechanical; a toolchain file plus a dozen guarded lines. |
| **The binary boundary — the real lock** | `COSMIC_API` (`Core.h:45-56`) exports **122 whole C++ classes**; `std::string`, `std::function` and `shared_ptr` cross `Cosmic.dll` ↔ project DLL (`Layer.h`, `ModuleRegistry.h`, `DataBus.h`, `EventBus.h`, `AppService.h`); one CRT heap is required (`BuildRunner.h:72-78`, `README.md:497-502`); ImGui/ImPlot are compiled per module and adopt the host's `ImGuiContext` (`ModuleMacros.h:115-119`); entt type ids hash `__FUNCSIG__` on MSVC but `__PRETTY_FUNCTION__` on clang (entt `config.h:93-100`) for user components registered by `CS_COMPONENT` (`ModuleMacros.h:94`). | A user project **must** be built with the engine's exact toolchain + C++ standard library + C runtime. Mixing is impossible without a redesign. |

## The answer: a second SDK flavour — never mixing

- **Two flavours of one release.** `Cosmic-SDK-<ver>-win64.zip` (MSVC — the primary, qualified flavour, unchanged) and
  `Cosmic-SDK-<ver>-win64-llvm-mingw.zip`, in which **every** binary (`Cosmic.dll`, `CosmicApp.exe`, `Starforge.exe`,
  `Starforge.dll`) is built by the bundled llvm-mingw and the toolchain itself ships under `toolchain/`. Everything else
  is the same checkout-mirror layout (D-SHIP), so `COSMIC_SDK` and the templates work unchanged.
- **Pinned toolchain.** llvm-mingw `20260922` = LLVM 23.1.2, the `ucrt-x86_64` build (UCRT — the same C runtime family
  MSVC uses; x86_64-hosted), a 190.7 MB zip before stripping
  ([releases](https://github.com/mstorsjo/llvm-mingw/releases)). Pinned by URL + SHA-256 in the repo; an upgrade is a
  requalification.
- **`sdk.toml` records the flavour**: `toolchain = "msvc" | "llvm-mingw"`, plus `msvc_version` or
  `llvm_mingw_release` + `llvm_version`. UX-04 reserves `toolchain = "msvc"` and `msvc_version` now (SD05), so the key
  is additive when this plan runs.
- **Starforge picks the toolchain from the SDK it runs from** (its `sdk.toml`), never from what happens to be
  installed: an MSVC-flavour Starforge never uses a bundled clang; a llvm-mingw-flavour Starforge never uses Visual
  Studio, even when present.
- **A module from the other flavour never loads.** The C++ mangling differs (MSVC ABI vs the Itanium ABI llvm-mingw
  uses), so `LoadLibrary` fails at import binding. The host turns that into one clear message ("`<module>` was built
  with `<other toolchain>`; rebuild it in this Starforge") instead of a raw `ERROR_PROC_NOT_FOUND`; a project's build
  directory is stamped with its flavour and a flavour change regenerates it.
- **Projects move by rebuilding; binaries never move.** Sources, `.cscene`, `.cflow` and `project.cproj` are text. T5
  proves nothing serialized carries a flavour-specific id (entt ids are hashed per flavour).
- **MSVC stays primary.** CI's full matrix, every stability/App Platform/UX manifest, the goldens and each campaign's
  qualification run on MSVC; the llvm-mingw flavour runs the TC set below plus `pr-units`.

## Rejected alternatives

| Alternative | Why not |
| --- | --- |
| Swap MSVC out entirely (llvm-mingw only) | Loses first-class Visual Studio debugging (STL natvis, Edit and Continue, the debugger every VS user already has), throws away the qualified base of three campaigns and changes every build and CI path at once. A second flavour gives the same zero-install benefit and takes nothing away. |
| A C-API module boundary, like Godot's GDExtension | The only design that would let a user pick *any* compiler — Godot's official Windows binaries are built with MinGW precisely because its extension boundary is a C API. Cosmic's boundary is 122 exported C++ classes with std types crossing it: a redesign of every public header and every project, a campaign of its own, not a toolchain task. Engines whose boundary is C++ (Unreal, Unity IL2CPP, O3DE) all require Visual Studio C++. |
| clang-cl | MSVC-ABI compatible (it could even mix with MSVC builds), but it compiles against the MSVC STL, the CRT headers/libs and the Windows SDK from a Visual Studio install. Those are not redistributable — only the runtime DLLs under `VC\Redist` are "Distributable Code" ([redistributing Visual C++ files](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)) — so it removes nothing the user must install. Kept as T0's portability check. |
| Bundle the MSVC Build Tools | Not redistributable (same reason). |
| w64devkit 2.10.0 (GCC 16.2, 67 MB) | Smaller, but links MSVCRT, not the UCRT, and GCC's CodeView/PDB support is weaker than LLVM's. llvm-mingw's UCRT build + lld's `--pdb` is the better fit. |

## The work — ordered steps T0..T10 (acceptance IDs TC01..TC11)

Anchors are the 2026-09-25 audit's (at `60a0536`); the executing session re-checks each first, as every packet
requires. T0 can run on its own at any time; T1→T3 are serial (same CMake files); T4/T5 follow T3; T6→T7 are serial
(same Starforge files); T8 can run beside T7; T9 is the qualification; T10 the docs.

| Step | Scope | Files | Acceptance |
| --- | --- | --- | --- |
| **T0** Portability CI job | A `portability` job in `ci.yml` on `windows-latest`. Leg A: configure with `-T ClangCL` (VS generator + "C++ Clang tools for Windows") and compile every first-party target — **gating** from day one (0–1 source changes today). Leg B: the llvm-mingw toolchain file, compile-only, **informational** until T3 lands, then gating. Worth doing even if nothing else here is ever built: it stops new MSVC-only code from creeping in. | `.github/workflows/ci.yml` (new job); `cmake/toolchains/llvm-mingw.cmake` (new, minimal); `tools/Get-LlvmMingw.ps1` (new: pinned URL + SHA-256, `actions/cache`) | TC01 |
| **T1** Toolchain file + CMake fixes | The full toolchain file (clang/clang++ `--target=x86_64-w64-mingw32`, lld, `-ffp-contract=off` (T4), `-gcodeview` (T8)); a `llvm-mingw` configure preset (Ninja Multi-Config) beside the pinned VS one; C++ flags only on C++ sources; Jolt's SSE4.x defines matched by `-msse4.1 -msse4.2` (D-CPU's SSE4.2 floor); `-mwindows`/`WIN32_EXECUTABLE` and the manifest embedded through a `.rc` for non-MSVC; the two `#pragma comment(lib)` sites become `target_link_libraries`; `-A x64` passed only to Visual Studio generators in the scripts and workflows. | `cmake/toolchains/llvm-mingw.cmake`; `CMakePresets.json:12-13`; root `CMakeLists.txt:42`; Jolt `CMakeLists.txt:43-45`; `Runtime/CMakeLists.txt:34-37`; `FileDialog.cpp:22-23`; `ContentBrowserPanel.cpp:38`; `build*.bat`; `package.bat:88`; `ci.yml:77`; `release.yml:38`; `gpu-qualification.yml:29`; `Run-AP04Sample.ps1:52` | TC02 |
| **T2** Source fixes | `__debugbreak` portable (`<intrin.h>` or `__builtin_debugtrap()`); the secure-CRT calls the mingw-w64 headers do not declare wrapped in one small compat header; `JobSystem.h:65` drops `<windows.h>`; the `windows.h` includes normalised through one internal header; `from_chars/to_chars(double)` verified with the pinned libc++ (fallback kept if not); a grep for `std::chrono` time-zone use (libc++'s is partial); the inline-static audit of `COSMIC_API` headers (Risks). | `Core.h:73-74`; the 34 secure-CRT files; `JobSystem.h:65`; the 42 `windows.h` includers; `DataExport.cpp:60,110` | TC03 |
| **T3** Import-library naming | MinGW names the import library `libCosmic.dll.a`; 10 CMakeLists import `Cosmic.lib`. Preferred: the `Cosmic` target sets `PREFIX ""`, `IMPORT_PREFIX ""`, `IMPORT_SUFFIX ".lib"` so the file is `Cosmic.lib` in both flavours and **no consumer changes** (D-LIBRARY's "App template unchanged" holds). Fallback: a `COSMIC_IMPORT_LIB` variable in the templates (a template change every existing project would need — avoid). The templates' `IMPORTED_*_DEBUG/_RELEASE` work under Ninja Multi-Config's Debug/Release. | `Cosmic/CMakeLists.txt` (target properties); read-only check of the 10 consumer CMakeLists | TC04 |
| **T4** FP determinism | Clang's default `-ffp-contract=on` may fuse `a*b+c`; MSVC as configured does not contract. The flavour compiles with `-ffp-contract=off`, never `-ffast-math`, and runs the determinism/drift checks (S03, the N02 `pr` leg, the fixed-step and telemetry units). Cross-flavour bit-identity is **not** a goal (mingw-w64 supplies some libm functions itself, so transcendental results may differ in the last ulp); each flavour passes the same oracles. A test that needs a flavour-specific tolerance is a finding for Kaden, never loosened silently. | the toolchain file; the named tests (read-only unless a KI) | TC05 |
| **T5** CosmicTests parity | CosmicTests and CosmicRenderTests build and run in the flavour: same case count, 0 failed, the same skipped set as MSVC; the 15 goldens byte-identical (a mismatch is a finding, never a regenerated golden); `pr-units` green; a host/module test that every engine singleton has one address across `Cosmic.dll` and a loaded module; a scene saved by one flavour loads in the other. | `tests/` (new cases only) | TC06 |
| **T6** Starforge toolchain selection | `BuildRunner` reads the flavour from `sdk.toml`. MSVC: today's command (VS generator, `-A x64`, `FindCMake` `BuildRunner.cpp:20-45`, UX-H1's toolset probe). llvm-mingw: the SDK's `toolchain/cmake/bin/cmake.exe` + `toolchain/ninja/ninja.exe`, `-G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=<sdk>/toolchain/llvm-mingw.cmake`; `--config` is then honoured, so `kHotConfig` holds; hot reload's `_hotN` suffix (`GAME_HOT_SUFFIX`) unchanged and the PDB-lock handling re-checked for lld's `--pdb`; the packaging builds take the same branch; UX-H1's compiler check becomes flavour-aware (the bundled folder, never vswhere); the mismatched-module message; the build dir stamped with its flavour. | `BuildRunner.{h,cpp}` (`:20-45`, `:61`, `.h:79-83`); `StarforgeApp.cpp:3028/3038/3048`; the H1-E probe | TC07 |
| **T7** Packaging | The flavour's runtime DLLs (`libc++.dll`, `libunwind.dll`, and `libwinpthread-1.dll` if linked) in every package — SD05's import oracle applies unchanged; the licence notices: LLVM's Apache-2.0 WITH LLVM-exception and mingw-w64's `COPYING.MinGW-w64-runtime.txt` (programs that ship the mingw-w64 runtime must include it) in `installer/licenses/MANIFEST.txt` and in every package of the flavour; the toolchain stripped to x86_64 only (the `ucrt-x86_64` zip also carries i686/armv7/aarch64 targets) with the measured size recorded; cmake + ninja bundled with their notices; `Starforge-Setup-<ver>-llvm-mingw.exe` or an installer component — decided by the measured size. | `Packager.cpp:66`, `:106-111`, `:300-301`; `installer/Stage-AppPackage.ps1:96-99`; `installer/AppSetup.iss`, `CosmicSetup.iss`, `StarforgeSetup.iss`; `installer/Stage-Sdk.ps1`; `installer/licenses/**`; `release.yml` | TC08 |
| **T8** Debugging story | Debug project DLLs with `-gcodeview -Wl,--pdb=` so Visual Studio (if present), WinDbg and UX-H1's crash dumps resolve symbols; caveats written down (the MAME precedent, [mamedev/mame#14352](https://github.com/mamedev/mame/pull/14352): attach and step work; no natvis for libc++ types; no Edit and Continue); optional: llvm-mingw's `lldb` / `lldb-dap` bundled for a debugger with no Visual Studio at all. | the toolchain file; `docs/developer/` (T10) | TC09 |
| **T9** Qualification without Visual Studio | A Windows 11 VM snapshot with **no** Visual Studio, Build Tools or VC++ redistributable: install the flavour's Starforge-Setup → New Project (App) → Build → Play → edit + hot reload → Package → the packaged exe runs on a second fresh snapshot. The MSVC flavour's SD04 on the same snapshot shows H1-E's message (the contrast). `ENVIRONMENT_BLOCKED` without a VM, never a pass. | `tests/acceptance/fixtures/` (a driver); `evidence/` | TC10 |
| **T10** Docs and guides | Guide 00 gains "no Visual Studio (bundled toolchain)" beside the MSVC install step, pictures by the guide rules; README prerequisites; `docs/installer-guide.md`; the developer building chapter ("two flavours, never mix"); `README-SDK.md` per flavour; the FEATURE-MATRIX row flipped. | `docs/guides/00-get-starforge.md`; `README.md`; `docs/installer-guide.md`; `docs/developer/` | TC11 |

### Acceptance IDs

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| TC01 | CI/pr | The `portability` job's clang-cl leg compiles every first-party target with 0 errors on every push; the llvm-mingw leg's error list is recorded (informational until T3, gating after). |
| TC02 | W/pr | `cmake --preset llvm-mingw` + Debug and Release builds produce `Cosmic.dll`, `Cosmic.lib`, `CosmicApp.exe`, `Starforge.exe`, `Starforge.dll`, `CosmicTests.exe`; the MSVC build afterwards is unchanged (0 warnings, same runtime-dir file list). |
| TC03 | U/pr | The flavour builds with 0 warnings at its agreed warning set; `git grep` finds no `#pragma comment(lib` in first-party sources and no `<windows.h>` in `JobSystem.h`; the inline-static audit table is in the evidence. |
| TC04 | W/pr | The App template, copied from each flavour's SDK tree, configures and builds against it with **zero** template diffs; `Cosmic.lib` is the import library's name in both. |
| TC05 | U,W/pr | With `-ffp-contract=off`, S03 ×5, the N02 `pr` leg and the fixed-step/telemetry units pass unchanged in the flavour; any tolerance question is a recorded finding. |
| TC06 | U,G/release | CosmicTests count, failures (0) and skipped set equal MSVC's in both configs; the 15 goldens byte-identical; `pr-units` green; the one-address singleton test and the cross-flavour scene load pass. |
| TC07 | I/release | In the unzipped llvm-mingw SDK with no Visual Studio on PATH: New Project (App) → Build → Play → edit + Ctrl+B hot reload (L02 pattern) → Package; a module built by the MSVC flavour is refused with the one-line message; switching flavour regenerates the project's build dir. |
| TC08 | W/pr | SD05's import oracle over every package of the flavour (every import is a Windows system DLL or in the package); every licence path `MANIFEST.txt` names is present; the stripped toolchain's size recorded. |
| TC09 | I/release | A breakpoint in a flavour-built project DLL is hit in the bundled lldb (and in Visual Studio attach where VS exists); an H1-D minidump of a flavour-built host opens with symbols. |
| TC10 | Q/release | T9's run on a VM snapshot without Visual Studio; `ENVIRONMENT_BLOCKED` with the prerequisite named when no VM exists — never a pass. |
| TC11 | U/pr | DOC01 (the three checkers + `check_api_matrix.ps1`) exit 0; guide pictures in the images manifest per the guide rules. |

## Risks

| Risk | Mitigation |
| --- | --- |
| **dllimport semantics differ under MinGW**: inline member functions of `COSMIC_API` classes are compiled into each module instead of imported, so a function-local static in one — or a static data member of an exported class template — can exist once per DLL: a split singleton. The Phase 20 per-DLL `FileSystem` VFS state bug (fixed by moving the state into `FileSystem.cpp`) is the precedent. RTTI and exception type matching across DLLs also differ. | T2's inline-static audit; T5's one-address test; `dynamic_cast` and a thrown engine exception across the boundary in T5. |
| entt type ids are consistent only within a flavour (`__FUNCSIG__` vs `__PRETTY_FUNCTION__`). | Never mixing is the design; T5 proves nothing persisted uses them. |
| `from_chars(double)` in libc++ (`DataExport.cpp:60,110`); libc++'s chrono time zones are partial. | Verified against the pinned release in T2; a fallback kept. |
| **The CI and qualification matrix doubles.** | MSVC keeps the full matrix; the flavour runs the TC set + `pr-units`; a release needs both green. |
| **SDK size**: a 190.7 MB toolchain zip before stripping, plus cmake, ninja and a second engine build. | A separate download/installer, so the MSVC flavour does not grow; x86_64-only strip (T7). |
| Debugging is weaker than MSVC's (T8 caveats). | Users who have Visual Studio stay on the MSVC flavour; guide 00 says so. |
| Codegen and performance differ. | UX-H1's H1-B scale profile run in both flavours (recorded, not gating). |
| A third-party toolchain inside a release (supply chain). | Pinned URL + SHA-256, verified by CI; Kaden approves every upgrade. |
| **Licence.** The repo has no LICENSE file; shipping the SDK (D-SHIP) already raises it, and bundling a toolchain with its notices makes Cosmic's own licence a prerequisite. | Kaden's decision before T7. |

## Effort (rough)

T0 ½ session · T1–T3 2 sessions · T4–T5 1–2 · T6 1 · T7 1 · T8 ½–1 · T9 1 (needs a VM) · T10 1 — **about 8–10 work-order
sessions**, one packet of ~7 work orders, mostly serial (T1–T3 share the CMake files, T6–T7 the Starforge files):
roughly the size of UX & Shipping's waves 2 and 3. After that, every campaign's qualification covers two flavours.

## What unlocks it

- The UX & Shipping campaign has closed (UX-Q1) and the App Platform is quiet — no toolchain work while the SDK layout,
  the packager and `BuildRunner` are still moving.
- **Kaden decides whether zero-install is worth a second flavour**; the doubled matrix is the price. Signals that it
  is: the SDK zip in real users' hands with the Visual Studio install as the top support question; users on
  locked-down or classroom machines; a team not eligible for VS Community.
- T0 alone may be picked up earlier: it adds one CI job and keeps the code portable whatever is decided.

## Sources

- Visual Studio Community licensing: https://visualstudio.microsoft.com/vs/community/
- Redistributing Visual C++ files (Distributable Code, app-local vs `vc_redist.x64.exe`):
  https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
- llvm-mingw releases (20260922 = LLVM 23.1.2; `ucrt-x86_64` zip 190.7 MB; Apache-2.0 WITH LLVM-exception + the mingw-w64
  runtime notices): https://github.com/mstorsjo/llvm-mingw/releases
- CodeView/PDB from a MinGW-target clang with Visual Studio attach (MAME): https://github.com/mamedev/mame/pull/14352
- winget (2026-09-25): `Microsoft.VisualStudio.Community` / `Microsoft.VisualStudio.BuildTools` (18.10.2); workloads
  `Microsoft.VisualStudio.Workload.NativeDesktop` (Community) / `Microsoft.VisualStudio.Workload.VCTools` (Build Tools).
