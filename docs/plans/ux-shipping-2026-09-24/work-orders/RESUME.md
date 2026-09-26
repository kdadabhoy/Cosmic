# UX & Shipping — resume point (2026-09-24, moving to a new PC)

Kaden stopped the campaign on the evening of 2026-09-24 (~21:50), about 1.5 h into wave 1, to move the repository to a
new PC. **Nothing from wave 1 has landed on `main`.** The three wave-1 lanes are committed on their branches (WIP
where unfinished), each with an `evidence/UX-xx/HANDOFF.md`. The orchestrator on the new PC starts here, then
follows [`ORCHESTRATOR.md`](ORCHESTRATOR.md) as before.

## State

`main` = `6021822` (UX-00 packet) → `2afe37c` (D-SOAKS: soaks postponed to
[`../../TESTING-PLAN.md`](../../TESTING-PLAN.md)) → the commit that adds this file. Tags unchanged
(`cosmic-pre-2d-2026-09-16`, `cosmic-app-platform-g5-2026-09-20`, both already on origin).

| Lane | Branch | Tip | Base | State | Handoff |
| --- | --- | --- | --- | --- | --- |
| UX-01 | `ux/01` | `d2aae20` | `2afe37c` (already rebased) | all six fixes committed; `ux01-editor` 5/5 PASSED Debug + Release; report.md, retained-run verdicts and CosmicTests counts missing; agent cut off (usage limit) | `evidence/UX-01/HANDOFF.md` (on `ux/01` only) |
| UX-02 | `ux/02` | `9667c84` | `6021822` | Phase A nearly done: 530/0/14 both configs; ED01/02/03/05 PASS both configs, ED04 fix committed but not re-run; retained runs partly done; report.md missing; Phase B pending | `evidence/UX-02/HANDOFF.md` (on `ux/02` only) |
| UX-03 | `ux/03` | `1aabb8f` | `6021822` | one orchestrator WIP commit of the agent's whole tree (agent cut off before its first commit); `ux03-launcher` 3/3 PASSED Debug + Release; 3 retained Release failures under load to re-run; report.md missing; Phase B pending | `evidence/UX-03/HANDOFF.md` (on `ux/03` only) |

KI numbers were **pre-allocated by the orchestrator** so the lanes would not collide (this replaced "next free
number" for wave 1): UX-01 **KI-66..71**, UX-02 **KI-72..76**, UX-03 **KI-77**, extras "KI-78 or above". All three lanes
then registered an extra as **KI-78** (a collision to fix at landing, lowest landing first):

| On branch | Title | Final number |
| --- | --- | --- |
| ux/01 KI-78 | the Screens panel is docked by no built-in preset (open) | **KI-78** (UX-01 lands first) |
| ux/02 KI-78 | a hot-reload build finishing after a project switch loads the old module and clears the new dirty flag (open) | **KI-79** |
| ux/02 KI-79 | `SaveScene` returns true when the write fails (open) | **KI-80** |
| ux/03 KI-77 | the Launcher lists the test fixtures | KI-77 |
| ux/03 KI-78 | the homescreen project card draws `thumb.png` upside down | **KI-81** |

UX-02 renumbers its two at its rebase onto UX-01, UX-03 its KI-78 at its rebase onto UX-02 (grep the evidence and
commit messages of the lane for the old number too). After wave 1 the register runs gap-free KI-66..81 and
`README.md`'s "next entry" line becomes **KI-82** (it still says KI-66 on `main`).

Gotcha found tonight: sub-agents were **refused writing `evidence/UX-xx/report.md` with the Write tool** (UX-02 hit it;
`HANDOFF.md` was allowed). Tell each lane agent to write its report through Bash, or write it as the orchestrator
from the agent's final message. Retained manifests run while three lanes build can fail on load (MSB6003, a scene
save rename race) — re-run such a failure alone before registering a KI.

Open orchestrator TODOs: `01-Contracts.md` §12 acceptance index (≈ line 324) still lists the soaks under UX-Q1 — fix
it to point at TESTING-PLAN.md after wave 1 lands (UX-02/03 edit that file); README next-free KI as above.

**UX-V0 contract rows (added by UX-V0 on 2026-09-25; it left `01-Contracts.md` alone because the wave-1 lanes edit
it).** UX-V0 lands before UX-01 and registered KI-82/83 and, for the orchestrator, KI-84 (README's next entry is now **KI-85**; KI-66..81 stay with the
wave-1 lanes). After wave 1 lands, in the same `01-Contracts.md` commit as the D-TOOLCHAIN rows below:

1. **§10 ownership** — a UX-V0 row (landed; recorded so later lanes know who changed what). **Owns:**
   `Cosmic/assets/shaders/Texture.glsl`, `Cosmic/assets/shaders/QuadInstance.glsl`; `Cosmic/src/graphics/Shader.{h,cpp}`
   (failure line, `GetLastCreateError`, `COSMIC_SHADER_OVERRIDE`); `Cosmic/src/platform/OpenGL/OpenGLShader.{h,cpp}`
   (`m_RendererID = 0`, `GetFailureReason`, `DescribeContext`); `Cosmic/src/renderer/Renderer2D.{h,cpp}` (`Init` → bool,
   `GetInitError`, the three `Flush` guards); `Cosmic/src/renderer/Renderer.{h,cpp}` (`Init` → bool);
   `tests/render/render_ux_v0_shader_failure.cpp` + `tests/render/fixtures/ux_v0/**`; the sampler-index pass of
   `tests/check_gl_conformance.ps1`; `evidence/UX-V0/**`. **May touch (shared, landed):**
   `Cosmic/src/core/Application.{h,cpp}` (the start-up-failure lines after `Renderer::Init`, the members at the end of
   the class, the `m_Framebuffer` guard in `OnWindowResize`) and `Runtime/Main.cpp` (`ReportStartupFailure` + its call)
   — UX-H1 later adds its crash-dump install lines to the same two files (different lines); `tests/render/render_main.cpp`
   (the `Init` check); `tests/render/CMakeLists.txt` (one TU, one define, `add_dependencies(... CosmicApp)`);
   `tests/render/fixtures/wo08/wo08_tint_quad{,_instance}.glsl` (the same KI-82 fetch); docs text for the changed code:
   `docs/guide/materials-and-shaders.md` (the batch-shader example + "Index u_Textures with literals only"),
   `docs/reference/graphics-resources.md` (`Shader::Create`), `docs/reference/rendering-2d.md` (`Renderer2D::Init`),
   `docs/reference/core.md` (Application start-up failure) — UX-D1's rename and UX-D3's sweep carry these along.
2. **§11 new surface** rows: `Shader::GetLastCreateError()` + the one-line failure message (VM02); the
   `COSMIC_SHADER_OVERRIDE` environment variable (VM02); `Renderer2D::Init()` → `bool` + `Renderer2D::GetInitError()`
   and `Renderer::Init()` → `bool` (VM02); `Application::StartedSuccessfully()` / `GetStartupError()` / `GetExitCode()`
   / `StartupFailureExitCode = 2` and the host's `COSMIC_NO_FATAL_DIALOG=1` (VM02); the GLSL sampler-index pass of
   `check_gl_conformance.ps1` (VM01).
3. **§12 acceptance index** — a `VM01–VM03 | UX-V0` cell.

**D-TOOLCHAIN contract changes (added 2026-09-25; `01-Contracts.md` was left alone because the wave-1 lanes edit it).**
Kaden kept MSVC as the only supported toolchain and added SD05 (UX-04), H1-E/H1-F (UX-H1) and DG03 (UX-D1) to the
not-yet-started WOs; the prompts, `00-Start-Here.md`, `02-Work-Orders.md` and `03-Acceptance-Catalog.md` already carry
them. After wave 1 lands and **before spawning wave 2** (UX-04 reads §4 and §10), apply these to `01-Contracts.md` in one
commit on `main`:

1. **§4 SDK release.** (a) The `sdk.toml` line of the tree: `version, sha, date, layout = "checkout-mirror",
   toolchain = "msvc", msvc_version` (reserve-now for [`../../TOOLCHAIN-PLAN.md`](../../TOOLCHAIN-PLAN.md)). (b) A new bullet
   "**VC++ runtime (new KI, D-TOOLCHAIN)**": every app package (editor Packager; `Stage-AppPackage.ps1`, hence
   `package.bat <App>` and `release.yml`'s `package` job; the Packager-generated `.iss`, `AppSetup.iss`, `CosmicSetup.iss`),
   the zip's `build/Runtime/Release` and `Starforge-Setup` carry the Release VC++ runtime — app-local Distributable Code
   DLLs from the building machine's `VC\Redist`, or `vc_redist.x64.exe` run by the installer (UX-04 records its choice) —
   at least as new as the newest MSVC toolset that built any binary in the package; Debug trees carry none; proven by SD05
   (`tests/acceptance/fixtures/Test-PackageImports.ps1`). (c) The Installer bullet: "carries the VC++ runtime (SD05)".
2. **§7 Documentation tiers.** Guide `00-get-starforge` includes the "Install the C++ compiler" step (Visual Studio
   Community 2026 or the Build Tools for Visual Studio 2026, "Desktop development with C++", the two winget strings of
   catalog row H1-E; DG03); the step's compiler-check picture is added by UX-H1 at its landing.
3. **§9 Hardening carry-over.** A paragraph "**Compiler check (UX-H1, D-TOOLCHAIN)**": the probe beside
   `BuildRunner::FindCMake` (the `package.bat:59` vswhere query + a cmake), cached per session; wired into `BuildScripts`,
   `BeginPackage`'s cmake steps, New Project after the scaffold and the homescreen's first frame; on "missing": no build,
   one Console line, the message titled `C++ compiler not found` with the two winget strings verbatim, the guide-00 URL and
   `README-SDK.md`; the probe-internal seam `COSMIC_SIMULATE_NO_TOOLSET=1`; the `COSMIC_AP03_PLAN=toolchain` self-test
   plan; `Run-AP04Sample.ps1` resolves cmake through vswhere (H1-F).
4. **§10 ownership.** UX-04 **Owns** + `tests/acceptance/fixtures/Test-PackageImports.ps1` (new, SD05), and the
   `Packager.cpp` entry widened to "+ the VC++ runtime copy" (`Packager.h` if its interface changes). UX-H1 **Owns** +
   `Projects/Starforge/src/BuildRunner.{h,cpp}` (the toolset probe), `tests/acceptance/fixtures/Run-UXH1Toolchain.ps1`
   (new), `tests/acceptance/fixtures/Run-AP04Sample.ps1` (cmake-discovery lines only); UX-H1 **May touch** +
   `Projects/Starforge/src/StarforgeApp.{h,cpp}` (the H1-E calls in `BuildScripts`, the `BeginPackage` cmake steps, the New
   Project path, a `DrawHomescreen` notice), `Projects/Starforge/src/AP03AuthoringSelfTest.cpp` (one step + the `toolchain`
   plan — the WO prompt already lists the H1-C step, §10 does not), and at landing only, after UX-D1:
   `docs/guides/00-get-starforge.md` (the compiler-check sentence + picture), one PNG under
   `docs/guides/images/00-get-starforge/`, one `docs/guides/images/manifest.json` entry, one line of
   `evidence/UX-D1/shots-sha256.txt`. UX-D1: no new paths (`docs/guides/**`, `README.md` and `evidence/UX-D1/**` cover
   DG03); `docs/guides/00-get-starforge.md` is now shared with UX-H1 (L4 in [`README.md`](README.md) already says so).
5. **§11 New-surface register** rows: the VC++ runtime in every package + the import oracle (SD05); `sdk.toml`
   `toolchain` / `msvc_version` (SD05, SD03); the toolset probe, the `C++ compiler not found` message and the
   `COSMIC_SIMULATE_NO_TOOLSET` seam (H1-E); `Run-AP04Sample.ps1` cmake via vswhere (H1-F); guide 00's "Install the C++
   compiler" step (DG03).
6. **§12 Acceptance index.** `SD01–SD04, K02-bak` → `SD01–SD05, K02-bak` (UX-04); `H1-A–H1-D, B06 (per tidy commit)` →
   `H1-A–H1-F, B06 (per tidy commit)` (UX-H1); a new `DG03` cell → `UX-D1 (UX-H1 adds its picture; UX-Q1 re-runs it with
   the source-equality leg)`.

## How wave 1 was run (keep these adaptations when resuming)

- The orchestrator creates each lane worktree itself (`git worktree add build\_lanes\ux-<id> ux/<id>`), and the
  agent's prompt says to skip the `git worktree add` step.
- Agent prompt = the ORCHESTRATOR.md preamble + an "Orchestrator notes" block + the WO's `~~~text` block verbatim.
  Notes used: work only in your worktree; set `COSMIC_SDK` to the worktree in every command (shell state does not
  persist); the pre-allocated KI numbers above; `--parallel 4` per build when three lanes compile at once (16
  threads / 31 GB on the old PC); isolate temp/prefs/projects dirs under the worktree's `build\_temp` and kill only
  your own processes; never merge or remove worktrees.
- UX-02 and UX-03 run **two-phase**: Phase A = the whole WO committed against the base; if their predecessors are
  not on `main` yet they stop "ready for rebase"; the orchestrator messages them (or, on the new PC, prompts a fresh
  agent) for Phase B = the prompt's Land (L2) step (`git rebase main`, post-rebase items, rebuild, rerun, commit).
  UX-02's post-rebase items: `m_Editors.AnyDirty()` in the unsaved-changes test; UX-01's
  `FlowEditor::HarnessSelection` confirming the transition in ED03.

## New-PC prerequisites

- Windows 10/11 x64; **Visual Studio 2026 (18) Community** with "Desktop development with C++" (the bundled
  `cmake.exe` path in [`README.md`](README.md) must exist — adjust the path there if the edition differs); Git for
  Windows (Git Bash); Python 3 via the `py` launcher; Windows PowerShell 5.1.
- A GPU with OpenGL 4.x drivers (render tests, the editor self-tests).
- Wave 2 (UX-04): **Inno Setup 6** (`ISCC.exe`) to build `Starforge-Setup-<ver>.exe`, otherwise SD02 is
  `ENVIRONMENT_BLOCKED`.
- Wave 3 (UX-D1): guide pictures are taken at **2560x1440, 100 % scaling**; the Claude desktop app with
  computer-use for the shots the capture driver cannot reach.
- Optional: the Claude memory folder copied from the old PC (below).

## Steps on the new PC (Kaden)

1. On the **old** PC, before deleting anything: push (`git push origin main ux/01 ux/02 ux/03`) and check
   `git ls-remote origin main ux/01 ux/02 ux/03` shows the same SHAs as the table above.
2. Optional: copy the folder `C:\Users\Kaden\.claude\projects\C--dev-Cosmic\memory\` (every file, including
   `MEMORY.md`) to the same path on the new PC. Clone to `C:\dev\Cosmic` so the `C--dev-Cosmic` key matches.
3. `git clone https://github.com/kdadabhoy/Cosmic.git C:\dev\Cosmic`, then in `C:\dev\Cosmic`:
   `git branch ux/01 origin/ux/01`, `git branch ux/02 origin/ux/02`, `git branch ux/03 origin/ux/03`.
4. Start the orchestrator session in `C:\dev\Cosmic` with the restart prompt Kaden was given (it points here).

## What the resuming orchestrator does

1. Re-check: `git log --oneline -3`, the lane branch tips against the table, `git status --short` clean.
2. Recreate the worktrees: `git worktree add build\_lanes\ux-0N ux/0N` (N = 1, 2, 3). `build\` is gitignored, so every
   lane needs a fresh configure + build (the old PC's builds are gone).
3. For each lane read its `HANDOFF.md` (≤ 40 lines) and spawn a **fresh** agent (the old agents do not exist on the
   new PC): its prompt = the preamble + the orchestrator notes above + the WO `~~~text` block verbatim + a paragraph
   "already on disk: <the HANDOFF summary>; continue from that state, do not reset; the base is still `6021822`,
   `main` has moved only by docs commits".
4. Land 01 → 02 → 03 per ORCHESTRATOR.md (verify, rebase, rebuild, `git merge --no-ff`, CosmicTests both configs,
   golden hashes into `evidence/UX-Q1/golden-hashes.txt`), then waves 2–5 as planned. Soaks are **not** in the
   campaign (D-SOAKS); UX-Q1 runs the quick legs only.

## Orchestrator TODOs added at the UX-01 landing (2026-09-26)

- UX-D3: `docs/guide/sprites-and-tilemaps.md:29-32` still calls ForgePong the first-run offer and cites the removed
  `BuildForgePong` (found by UX-03; D-SAMPLES made PendulumLab the welcome offer). Fix after the `docs/developer/` rename.
- UX-H1: KI-84 (timing-bound tests incl. wo06 D03-nightly's 60 s deadline — measure on a quiet machine first) and
  KI-86 (AP03 / guide self-tests truncate their result JSON on FAIL) are its to fix.
- FE04 (UX-01) is ENVIRONMENT_BLOCKED at the VM's 1718x920 display: re-run `ux01-editor` on `main` once the VM display
  is ≥ 1920x1080 (Kaden changes the resolution), and record it in `evidence/UX-01/`.
- The quiet (no other lane) re-run of `ap03-editor` Release on `main` for UX-V0's VM03 is covered by UX-01's retained
  run (PASS both configs on the VM, E03 passed, no MSB6003) — note it in UX-Q1.
