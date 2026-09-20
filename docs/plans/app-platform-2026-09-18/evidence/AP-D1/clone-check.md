# Clone check — the README Quickstart, run literally from a fresh clone (2026-09-20)

Purpose: prove that what `git push` will put on GitHub builds, tests and starts the editor by
following the root README's Quickstart, with nothing from the working tree, no `COSMIC_SDK`, and the
VS-bundled CMake called by its full path. Everything below was run by this session on the dev
machine (Windows 11 Education 10.0.26200, VS 2026 Community, CMake 4.3.1-msvc1, NVIDIA GPU).

## Commands and results

| # | Command (from the clone root) | Exit | Result |
| --- | --- | --- | --- |
| 1 | `git clone C:\dev\Cosmic C:\dev\Cosmic\build\_scratch\clone-check` (local clone of the committed state = what GitHub serves) | 0 | HEAD `1aa7b70` (the docs commits below add only evidence/README text on top); 5542 tracked files, 109 MB checkout, `git status` clean |
| 2 | `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON` | 0 | 4.4 s. Cache: `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_ENGINE_ONLY=OFF`, `COSMIC_WITH_JOLT=ON`, `COSMIC_SKIP_PROJECTS=AnalysisSample;PendulumLab` (the two standalone specimens, by design). Auto-detected projects: SF_Telem, Starforge. |
| 3 | `& $cmake --build build --config Release --parallel` | 0 | **178 s**, 0 warnings. `build\Runtime\Release\`: `Starforge.exe`, `CosmicApp.exe`, `CosmicTests.exe`, `Cosmic.dll`, `Starforge.dll`, `SF_Telem.dll` + 10 test-fixture DLLs, `assets\`. Project DLLs land **flat** beside the exes (no `projects\` subfolder). |
| 4 | `build\Runtime\Release\CosmicTests.exe` | 0 | **519 test cases passed, 0 failed, 14 skipped**; 23,445,172 assertions; 172 s. |
| 5 | `powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1` | 0 | clean |
| 6 | `powershell -ExecutionPolicy Bypass -File tests\check_docs_coverage.ps1` | 0 | clean (123 public headers, 120 manifest rows, 5 skeletons) |
| 7 | `powershell -ExecutionPolicy Bypass -File tests\check_docs_links.ps1` | 1 → 0 | in the clone the only strict breakage was the README's link to *this file*, which did not exist yet; exit 0 on the tree that contains it (§ below) |
| 8 | `py -3 docs\plans\app-platform-2026-09-18\evidence\AP-D1\check_parked.py` | 0 | 18/18 banners, 207 labelled links, 0 violations |
| 9 | `Remove-Item Env:COSMIC_SDK; Start-Process build\Runtime\Release\Starforge.exe -WorkingDirectory <clone root>`; wait 10 s; `CloseMainWindow()` | 0 | **Alive after 10 s**, homescreen up (`Successfully loaded and mounted project DLL Layer`, dockspace built), clean shutdown on close (`Application Subsystems safely terminated`), exit code 0, no `error` in the logs. Logs landed in `build\Runtime\Release\logs\` (next to the exe). |
| 10 | `Remove-Item -Recurse -Force build\_scratch\clone-check` | 0 | clone deleted at the end of the session |

## What this proves and what it does not

- A fresh clone **configures, builds Release, passes the full unit suite and starts the editor** with
  nothing but VS 2026 + git installed and `COSMIC_SDK` unset. Nothing the build or tests need is
  untracked or ignored (the two force-added CSV fixtures are now explicit `.gitignore` negations).
- `COSMIC_SDK` is **not** needed to start the editor. It **is** needed the moment the editor builds a
  project's C++ (New Project → Build, or Build on `Projects\PendulumLab`), because the project's
  `CMakeLists.txt` resolves the engine through it; the README Quickstart §6 says so and gives both
  ways to set it (`setup.bat` once, or `$env:COSMIC_SDK` in the launching shell). Without it the
  editor's fallback is "three directories up from the current directory", which is only right when
  launched from `build\Runtime\<Config>`.
- Not exercised here: the editor's Build/Play of PendulumLab from the clone (a GPU/UI interaction —
  the AP-04 `Run-AP04Sample.ps1` standalone build and the K02 packaged run are the scripted proofs of
  that path, in `evidence/AP-04` and `evidence/AP-Q1`), and a machine without Visual Studio (K03).

## Corrections the run forced into the README

- `build\Runtime\Release\` layout: project DLLs are flat, there is no `projects\` folder.
- `PendulumLab` and `AnalysisSample` are skipped by the SDK build on purpose (`COSMIC_SKIP_PROJECTS`
  default) and build standalone against the SDK; the Quickstart says to press **Build** before
  **Play** after opening `Projects\PendulumLab`.
- Clean Release build time: ~3 minutes on this machine, not the 10–15 first written.
- Dev-boot logs go to `logs\` next to the executable.
