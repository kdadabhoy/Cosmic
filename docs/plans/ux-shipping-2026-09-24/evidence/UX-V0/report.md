# UX-V0 — VM enablement: conformant batch shaders, no crash on a failed shader — report

## 1. Scope and provenance

- **Work order:** UX-V0 (`work-orders/UX-V0.md`, the `~~~text` block), plus two orchestrator additions received during
  the session (register **KI-84** — timing-bound CosmicTests under load — registration only, then add WO-09 C02 to it;
  README next entry → **KI-85**). Where the orchestrator's notes and the prompt disagree the notes won: the prompt says
  "next-entry line to KI-84", the final line says KI-85.
- **Lane:** worktree `C:\dev\Cosmic\build\_lanes\ux-v0`, branch `ux/v0`, base `353948b` (= `main` at the start;
  `git rev-parse HEAD` printed `353948b8f49706b70914135897facc25d8164c1d`, `git status --short` empty). Built into
  `<worktree>\build`, `COSMIC_SDK` = the worktree in every command, `--parallel 4`; temp/prefs/projects under
  `<worktree>\build\_temp`; only processes started by this lane were stopped (by PID).
- **Not done here (the orchestrator's, L2):** `git rebase main`, the merge, the landing re-runs. Nothing was pushed.
- **Prompt facts that did not hold:** the repo has 28 tracked `*.glsl` files (not "46 other shaders"); none outside
  the four named below declares a sampler array. `QuadInstance.glsl` was already `#version 450 core` (legal syntax,
  undefined behaviour — see KI-82). The unfixed CosmicRenderTests did not "abort": Release exited with the same
  `0xC0000005` as Starforge; Debug ran against an invalid program (KI-83's uninitialised program id).
- **Scope notes (files outside the prompt's list, all recorded for §10 in `RESUME.md`):** the developer guide's batch
  shader example (`docs/guide/materials-and-shaders.md`) taught the KI-82 pattern and said `Texture.glsl` was
  `#version 330` — rewritten; three reference chapters described the old failure behaviour (`graphics-resources.md`
  `Shader::Create`, `rendering-2d.md` `Renderer2D::Init`, `core.md` Application boot) — updated; `00-Start-Here.md`'s
  landing-order sentence got `UX-V0` next to the requested "Wave 1a" line. `01-Contracts.md` untouched.

## 2. Toolchain and environment

| Item | Value |
| --- | --- |
| Machine | VMware VM, 8 vCPU, Windows 11 Education 10.0.26200; other lanes (UX-01, then UX-02) building/testing concurrently |
| OpenGL | `OpenGL 4.5 — llvmpipe (LLVM 13.0.1, 256 bits)`, `GL_VERSION` `4.5 (Core Profile) Mesa 24.1.0` |
| Compiler | MSVC 14.51.36231 (`cl` 19.51.36260), Visual Studio 18 2026 generator |
| CMake | 4.3.1-msvc1 (VS-bundled) |
| Configure | `cmake -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON` |
| Effective cache | `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=ON`, `COSMIC_BUILD_ENGINE_ONLY=OFF`, `COSMIC_WITH_JOLT=ON`, `COSMIC_SKIP_PROJECTS=AnalysisSample;PendulumLab`, `COSMIC_SDK_DIR=C:/dev/Cosmic/build/_lanes/ux-v0` |
| Python | 3.13 via `py -3` (evidence helpers only) |

## 3. What was built

**KI-82 — conformant batch shaders (VM01).**
- `Cosmic/assets/shaders/Texture.glsl`: `#version 450 core` (both stages); the slot is `flat out int v_TexIndex =
  int(a_TexIndex)` (the old `int()` truncation, now exact per quad instead of applied to an interpolated float); the
  fragment fetches through `SampleSlot(slot, uv)`, a `switch` over the literal indices 0-31, result `* v_Color` as
  before. Out of range (Renderer2D never emits one) gives `vec4(1.0)`, i.e. colour only. Vertex layout,
  `u_Textures[32]` and the 32-slot contract with `Renderer2D` unchanged; `u_Textures` stays an active uniform array,
  so the engine's automatic sampler upload is unchanged.
- `Cosmic/assets/shaders/QuadInstance.glsl`: same scheme; its `0 <= index < 32` guard, the `v_Color *` order and the
  alpha discard are kept.
- `tests/render/fixtures/wo08/wo08_tint_quad.glsl`, `wo08_tint_quad_instance.glsl`: same fix (they copy the two
  contracts; the checker flagged them).
- `tests/check_gl_conformance.ps1`: a second pass over every `*.glsl` under the repo root (skipping directories named
  `.git`, `build`, `out`, `dependencies`, `vendor`, `extern`, `third_party`, `node_modules`; comments blanked with line
  numbers preserved) that exits 1 when a `uniform sampler*` array is indexed by anything but an integer literal,
  naming `file:line`, the array, the index and the source line; zero `*.glsl` found is also a failure. The GL-token
  pass is unchanged (same code and messages; only the final `exit 0` now also depends on pass 2). PS 5.1-safe, ASCII.
  A synthetic check (literal, hex and `2u` indices, `//` and `/* */` comments, a multi-line block comment,
  `u_Tex [i]`, nested `u_Tex[b[0]]`, an `isampler2DArray` with `layout(binding=...)`, copies under `build/` and
  `dependencies/`) reported exactly the three real violations.

**KI-83 — a failed engine shader never crashes (VM02).** Decision per call site (the 17 engine `Shader::Create` calls):

| Call site | Decision |
| --- | --- |
| `Renderer2D.cpp` `Texture.glsl` (batch quad shader) | **fatal, clean**: `Init` returns false with `GetInitError()`; `Application` stops the boot after `Renderer::Init` (one CRITICAL line), `Run()` returns at once, `Runtime/Main.cpp` deletes the app (normal `Shutdown`), prints `Fatal: <app> could not start: ...` + the log folder to stderr (a message box when a GUI build has no stderr, unless `COSMIC_NO_FATAL_DIALOG=1`) and exits `Application::StartupFailureExitCode` = **2** |
| `Renderer2D.cpp` `Line.glsl`, `Circle.glsl` | continue without the feature: `Flush` now skips a batch whose shader is null (both were null dereferences) |
| `Renderer2D.cpp` `Text.glsl`, `CircleInstance.glsl`, `QuadInstance.glsl` | continue (already guarded: `Flush` / the instanced verbs return early) |
| `Light2DRenderer.cpp` `Light2D.glsl`, `BlitCopy.glsl` | continue without 2D lights (already: `Ensure` returns false) |
| `PostProcessStack.cpp` `Tonemap.glsl` + 6 effect shaders | continue without that pass (already guarded per pass) |
| `LauncherLayer.cpp` `Launcher.glsl` | continue with the plain clear colour (already guarded; UX-03's file, untouched) |
| `AssetLibrary.cpp` `GetShader` | returns nullptr to its caller (already; `BuildMaterial` returns nullptr) |

Also: `Shader::Create` logs ONE error line - `Shader::Create: shader '<path>' [(overridden to '<path>')] failed to
build on renderer '<GL_RENDERER>', OpenGL '<GL_VERSION>': <STAGE> stage: <first compiler error line>. Returning
nullptr.` - and keeps the text for `Shader::GetLastCreateError()` (thread-local); `OpenGLShader::m_RendererID = 0`
(it was uninitialised: in Debug a failed shader passed `IsValid()` with `0xCDCDCDCD`); an unreadable file or a file
with no stages fails before any GL work; the info-log buffers hold at least one zeroed char.
`COSMIC_SHADER_OVERRIDE` (`"<file name>=<path>[;...]"`, read in `Shader::Create`, logged as a warning) is the
documented production seam the VM02 test uses. `Application::OnWindowResize` tolerates the missing framebuffer -
found by the VM02 start-up case itself: the first cut exited `0xC000041D` (an access violation inside the WndProc when
the Window's teardown reported a resize). `tests/render/render_main.cpp` stops with a message if `Renderer2D::Init`
fails. Commit `1538b2d` reports the failure after the app is deleted, so the message box never sits over a frozen
engine window (`vm02-release-startup-dialog.png`: a Release `Starforge.exe` started from the shell with the override).

**The VM02 test** - `tests/render/render_ux_v0_shader_failure.cpp` (suite `UX-V0 VM02`, 3 cases, in
CosmicRenderTests; `add_dependencies(CosmicRenderTests CosmicApp)`) with the real broken fixture
`tests/render/fixtures/ux_v0/ux_v0_broken.glsl` (reads an undeclared `ux_v0_undeclared`): (1) `Shader::Create` on it
returns nullptr and exactly one captured error line names the path, a non-empty renderer and version and the first
compiler error; a missing file says `could not read the file`; (2) `Renderer2D::Init` with the override on
`Texture.glsl` returns false with the reason and a quad/line/circle frame runs (the quad's pixel keeps the clear
colour); with `Line.glsl` + `Circle.glsl` overridden `Init` succeeds, the quad draws, the line and the disc are
skipped; the harness renderer is rebuilt and a control frame draws all three; (3) `CosmicApp.exe` started with the
override (stdout/stderr to a file, 120 s deadline) exits 2, never `0xC0000005`, and says `could not start` with
`Texture.glsl`, `ux_v0_undeclared` and the renderer. No private state is touched.

## 4. Acceptance-case status

| ID | Status | Evidence |
| --- | --- | --- |
| **VM01** conformant shaders | **PASS** | Checker: exit 1 on the unfixed tree listing `QuadInstance.glsl:65`, `Texture.glsl:40`, `wo08_tint_quad.glsl:45`, `wo08_tint_quad_instance.glsl:52` (`failing-before-excerpts.txt` §5), exit 0 after (29 files incl. the VM02 fixture). Logs under llvmpipe, clean rebuilds, both configs: `Starforge.exe` and `CosmicApp.exe` normal starts - 0 compile / link / `Shader::Create` errors; full `CosmicRenderTests` - the only shader errors are the VM02 fixture's own (4 compile failures + 1 missing-file, every `Shader::Create` error line names `ux_v0_`), counted by `scan_shader_errors.py` (`passing-after-excerpts.txt`). Output identity on a real GPU: HOST-VERIFY. |
| **VM02** no crash on a failed shader | **PASS** Debug + Release | `--test-suite="UX-V0 VM02"`: 3/3 passed both configs; also inside every full render run. Start-up with the override, clean rebuilds: `Starforge.exe` and `CosmicApp.exe` exit **2** in both configs with `Fatal: <app> could not start: ...` on stderr and `Application Subsystems safely terminated.` as the last log line. Failing-before: Release `Starforge.exe` and `CosmicRenderTests.exe` exit `0xC0000005`; Debug returns the failed shader as valid (`failing-before-excerpts.txt` §1-§4). |
| **VM03** the editor runs in the VM | **PARTIAL - Release ap03-editor NOT MET** | `Starforge.exe` starts under llvmpipe and runs (stopped after 45-90 s) in both configs, logs clean (VM01). `wo07-l05`: **PASS** Release (2/2) and Debug (2/2). `ap03-editor`: **PASS** Debug (AP03 PASS, oracle PASS, 768 s). Release **FAILED** in all three runs: (1) E03 + two E06 project builds; (2) the one allowed re-run: 36 checks, every one downstream of scaffolded-project build failures; (3) a diagnostic run with a shorter `-TempRoot` (`build\_temp\a3`): only E03. Every project-build failure is MSBuild's file tracker (`MSB6003 ... DirectoryNotFoundException ... *.tlog`, with `MSB8029`: the build dirs sit under the per-run TEMP the runner sets) - the "MSB6003 under load" RESUME.md already records; run 3 had none. E03 (`OffsetMax after SE resize (-127.202,-25.2021), expected (185.959,61.5751)`) failed in runs 1 and 3 with identical numbers and passed in run 2; it is the injected rect-gizmo drag (UX-02's area), untouched by this lane. Other lanes were building/testing throughout. `vm03-excerpts.txt`, `manifests/*.results.json`. |
| CosmicTests | **PASS** | 523 passed / 0 failed / 14 skipped, Release (427 s) and Debug (571 s), clean rebuilds; also 523/0/14 on the incremental builds (300 s / 437 s). No new CosmicTests case (the VM02 cases live in CosmicRenderTests). |
| CosmicRenderTests (counted, not authoritative here) | recorded | 48 cases (45 + 3 VM02): **46 passed / 2 failed / 2 skipped** in both configs; the 2 are the llvmpipe goldens `instancing2d` (68 px, 0.118 %) and `wo08_text` (74 px, 0.128 %) - the same numbers as the orchestrator's pre-UX-V0 43/45 experiment. No tolerance changed, no golden regenerated. |
| Goldens | **unchanged** | `golden-hashes-before.txt` = `golden-hashes-after.txt` (15 PNGs, sha256); the llvmpipe `*.actual.png` / `*.diff.png` (gitignored) were deleted. |
| Checkers | **exit 0** | `check_gl_conformance` (both passes), `check_docs_coverage`, `check_docs_links`. |
| Builds | **0 warnings** | Clean rebuilds (`--clean-first`, `--parallel 4`): Release 280 s, Debug 189 s, 0 warnings, 0 errors. |

## 5. Defects (registered before fixing)

- **KI-82** (registered in `33aff09`, fixed in `05f118c`) - the batch shaders index `u_Textures` with a
  non-constant, non-dynamically-uniform expression; Mesa rejects `Texture.glsl`.
- **KI-83** (registered in `33aff09`, fixed in `05f118c` + `1538b2d`) - a failed engine shader crashed the process
  (Release `0xC0000005`), `m_RendererID` was uninitialised (Debug: failed shader returned as valid), and the failure
  line named neither driver nor error. The VM02 start-up case caught a second crash on the new failure path during
  development (`Application::OnWindowResize` on a null framebuffer, exit `0xC000041D`), fixed under KI-83 before the
  fix commit.
- **KI-84** (registered for the orchestrator in `445b303`, extended in `6bd88cc`; no fix, owner UX-H1) -
  timing-bound CosmicTests (WO-05 T03, WO-09 C05, WO-09 C02 maximum map, E10) fail under CPU load on the VM; README's
  VM section now says to re-run such a failure alone. This lane's own four full CosmicTests runs had no such failure.
- **Seen, not registered (for the orchestrator to judge):** (a) the AP03 Release E03 flake above - identical wrong
  numbers in two runs, pass in a third; worth a KI if it reproduces on an idle VM; (b) the AP03 self-test process
  ends with `0xC0000409` (fail-fast) on its FAIL path in all three Release runs (the wrapper still reports the
  verdict), whereas the PASS run exits 0; (c) suspected, not reproduced: when the Window cannot create a GL 4.5
  context (`glfwCreateWindow` fails, `Window.cpp:355-361`), `Application::Initialize` still calls `Renderer::Init`
  with no GL function pointers - the same class of start-up crash as KI-83, on machines without GL 4.5.

## 6. Measured numbers

| What | Before (unfixed `353948b`) | After |
| --- | --- | --- |
| Release `Starforge.exe` start | exit `0xC0000005` ~4 s after launch | runs (stopped after 45-90 s); log clean |
| Debug `Starforge.exe` start | runs, failed shader returned as valid (no `Returning nullptr`) | runs; log clean |
| Release `CosmicRenderTests` | exit `0xC0000005` in `Renderer2D::Init`, 0 cases run | 48: 46 / 2 (llvmpipe goldens) / 2 skipped, 54 s |
| Debug `CosmicRenderTests` | 611 failed-CHECK lines (9 test files) against the invalid program, stopped by PID after ~26 min | 48: 46 / 2 / 2, 140 s |
| Start-up with `Texture.glsl` -> broken fixture | (the same `0xC0000005`) | exit 2, `Fatal: ... could not start` (both hosts, both configs) |
| `check_gl_conformance.ps1` | exit 1, 4 violations | exit 0, 29 files scanned |
| CosmicTests | 523/0/14 (VM baseline, orchestrator) | 523/0/14 both configs |
| Builds (`--parallel 4`, VM shared with another lane) | configure 8 s; Release 8 min 03 s, Debug 4 min 26 s (full) | clean rebuild Release 280 s, Debug 189 s, 0 warnings |
| `wo07-l05` | not runnable (editor crashed) | Release 139 s, Debug 153 s, 2/2 each |
| `ap03-editor` | not runnable | Debug PASS 777 s; Release 3 x FAIL (321 s, 295 s, 661 s) |

## 7. Caveats

- **Golden comparisons are not authoritative on llvmpipe.** The claim that the rewritten batch shaders draw exactly
  what the old ones drew on a real GPU (same sampler, UV * tiling, colour multiply; `flat` slot equal to the old
  per-quad constant) rests on the maths and on the 13 goldens that pass here; the real-GPU run is HOST-VERIFY
  (`HOST-VERIFY.md`: 48 cases, 46 passed, 0 failed, 2 skipped expected).
- **Derivatives inside the `switch`:** `texture()` with implicit LOD now runs inside a `switch` on a `flat` slot. The
  slot is constant per primitive and 2x2 derivative quads never span primitives, so the control flow is uniform
  within every quad - the standard technique for batch renderers; any driver disagreeing would show up in HOST-VERIFY.
- `Renderer2D::Init` and `Renderer::Init` now return `bool` - an exported-signature change of `Cosmic.dll`; every
  in-tree project is rebuilt with it (out-of-tree app DLLs rebuild against the SDK anyway).
- `COSMIC_SHADER_OVERRIDE` is a production environment variable (logged as a warning when it applies);
  `COSMIC_NO_FATAL_DIALOG=1` only suppresses the start-up message box.
- VM03's Release `ap03-editor` is not met on this VM under concurrent lane load (§4); the landing re-run on `main`
  with no other lane active is the orchestrator's (L2; README now says landing runs are done with no other lane
  building).
- The retained manifests rewrote tracked evidence under the 2D-stability and App-Platform packets and left
  `scratch-*` directories (with an asset junction) under `evidence/WO-07/`: the tracked files were restored with
  `git checkout --`, the scratch directories removed with `cmd /c rmdir /s /q` (junction-safe); no `recordings/` or
  `n04-*.bin` appeared in the tree root.

## 8. Files

Engine and host: `Cosmic/assets/shaders/Texture.glsl`, `Cosmic/assets/shaders/QuadInstance.glsl`,
`Cosmic/src/graphics/Shader.{h,cpp}`, `Cosmic/src/platform/OpenGL/OpenGLShader.{h,cpp}`,
`Cosmic/src/renderer/Renderer2D.{h,cpp}`, `Cosmic/src/renderer/Renderer.{h,cpp}`, `Cosmic/src/core/Application.{h,cpp}`,
`Runtime/Main.cpp`.
Tests: `tests/check_gl_conformance.ps1`, `tests/render/render_ux_v0_shader_failure.cpp` (new),
`tests/render/fixtures/ux_v0/ux_v0_broken.glsl` (new), `tests/render/fixtures/wo08/wo08_tint_quad{,_instance}.glsl`,
`tests/render/render_main.cpp`, `tests/render/CMakeLists.txt`.
Docs: `docs/guide/materials-and-shaders.md`, `docs/reference/{graphics-resources,rendering-2d,core}.md`,
`docs/plans/2d-stability-2026-09-16/contracts/known-issues.md` (KI-82..84), and in this packet `00-Start-Here.md`,
`02-Work-Orders.md`, `03-Acceptance-Catalog.md` (VM01-VM03), `work-orders/README.md` (next KI-85, VM bullet),
`work-orders/RESUME.md` (the `01-Contracts.md` §10/§11/§12 TODO for UX-V0).
Evidence (`evidence/UX-V0/`): `report.md`, `failing-before-excerpts.txt`, `passing-after-excerpts.txt`,
`vm03-excerpts.txt`, `ki84-excerpts.txt`, `manifests/*.results.json` (6 runs), `golden-hashes-{before,after}.txt`,
`vm02-release-startup-dialog.png`, `scan_shader_errors.py`, `HOST-VERIFY.md`.

## 9. Local commits (branch `ux/v0`, author kdadabhoy <kdadabhoy28@gmail.com>, no AI trailer, not pushed)

1. `33aff09` Register KI-82 and KI-83; next KI-84.
2. `05f118c` Fix KI-82 and KI-83 (shaders, checker pass 2, failure paths, VM02 test, docs).
3. `1538b2d` Report a start-up failure after the teardown (no message box over a frozen window).
4. `445b303` Register KI-84 (orchestrator request); next KI-85; VM note.
5. `6bd88cc` KI-84: add WO-09 C02.
6. The evidence + bookkeeping commit that carries this report (VM01-VM03 rows, work-order map, Wave 1a, RESUME TODO).
