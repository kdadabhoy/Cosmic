# UX-V0: VM enablement — conformant batch shaders, no crash on a failed shader

**Gate:** G1 · **Wave:** 1a (before UX-01 lands; ∥ UX-01's non-GL steps) · **Runs:** worktree
`build\_lanes\ux-v0`, branch `ux/v0` · **Base:** `main` at the commit that adds this file · **Depends on:** UX-00
· **Acceptance:** VM01–VM03 · **Model:** Opus 5.5 · **Effort:** high · **Status:** not started

Added by the orchestrator on 2026-09-25 after the move to the new PC, a VMware VM whose only OpenGL is
`OpenGL 4.5 — llvmpipe (LLVM 13.0.1)` (VMware's Mesa software renderer). Kaden wants as much of the campaign
as possible tested in the VM. On `main` at `d815252` (Release, this VM): the build is clean and CosmicTests is
523/0/14, but **Starforge crashes at start-up (exit -1073741819, 0xC0000005)** and CosmicRenderTests aborts,
because Mesa refuses `Cosmic/assets/shaders/Texture.glsl`: the Renderer2D batch shader is `#version 330 core`
and indexes `uniform sampler2D u_Textures[32]` with `int(v_TexIndex)` — illegal in GLSL 1.30–3.30 (NVIDIA/AMD
tolerate it). `Shader::Create` then returns nullptr (`OpenGLShader`/`Shader::Create` log "Returning nullptr")
and the engine dereferences it. An orchestrator experiment that bumped only the build-output copy to
`#version 450 core` let Starforge run and draw correctly and CosmicRenderTests reach 43/45 (the two misses,
`instancing2d` 0.118 % and `wo08_text` 0.128 % vs a 0.1 % budget, are llvmpipe rasterisation). The experiment
is not the fix: in GLSL 4.50 a sampler-array index must be dynamically uniform, otherwise behaviour is
undefined. `QuadInstance.glsl:56` declares the same `u_Textures[32]` array.

Orchestrator evidence already on disk (main's gitignored build dir, same VM):
`C:\dev\Cosmic\build\_vm_probe\baseline-render-accelON.log` (the Mesa error + preprocessed dump),
`C:\dev\Cosmic\build\_vm_probe\baseline-render-patched.log` (43/45 with the experiment),
`C:\dev\Cosmic\build\Runtime\Release\logs\Cosmic_2026-09-25_21-43-12.log` (Starforge's failing start).

## Copy-paste prompt

~~~text
Execute only UX-V0 from the Cosmic "UX & Shipping" packet (docs/plans/ux-shipping-2026-09-24/).
Nobody answers questions: decide, proceed, report honestly (blocked/failed is reported as such).
Read first, and nothing else until you edit a file named below:
 1. work-orders/README.md (global rules, lane rules L1-L5, build commands, the VM environment section,
    the next free KI number) and work-orders/UX-V0.md above this block (the problem statement)
 2. 03-Acceptance-Catalog.md (format of a row) and 02-Work-Orders.md (landing order table)
 3. Cosmic/assets/shaders/Texture.glsl, Cosmic/assets/shaders/QuadInstance.glsl
 4. Cosmic/src/renderer/Renderer2D.cpp (Init, :220-260) and every other `Shader::Create(` call site under
    Cosmic/src (18 today: grep them) — only the lines that use the result
 5. Cosmic/src/platform/OpenGL/OpenGLShader.cpp (compile/link error path) and the Shader::Create factory
 6. tests/check_gl_conformance.ps1, tests/render/ (how render tests and goldens run), tests/CMakeLists.txt
 7. docs/plans/2d-stability-2026-09-16/contracts/known-issues.md template :14-22

Lane: the worktree C:\dev\Cosmic\build\_lanes\ux-v0 (branch ux/v0) already exists — do not run git worktree
add. Work only inside it; set $env:COSMIC_SDK to that folder in every command (shell state does not
persist). Configure with the README command (+ -DCOSMIC_BUILD_RENDER_TESTS=ON), build into <worktree>\build,
`--parallel 4` (another lane builds at the same time). UX-01 runs concurrently in build\_lanes\ux-01 and
waits for you: you land first. Revalidate: print git rev-parse HEAD, git status --short, cmake --version.

KIs first (append to known-issues.md with the template; numbers: KI-82 and KI-83 — KI-66..81 are
pre-allocated to the wave-1 lanes, see work-orders/RESUME.md; set README's next-entry line to KI-84):
 KI-82 — the Renderer2D batch shader Texture.glsl (and QuadInstance.glsl if it has the same defect)
   indexes a sampler array with a non-constant / non-dynamically-uniform expression; a conformant GLSL
   compiler (Mesa: VMware/VirtualBox VMs, Intel and AMD on Linux) rejects or may mis-sample it.
 KI-83 — a failed engine shader (Shader::Create returns nullptr) crashes the process (0xC0000005)
   instead of failing with a clear message.
Failing-before for both: build the unfixed base in your worktree and reproduce (Starforge exit code and
log, CosmicRenderTests output, the extended checker from change 3 exiting 1 on the unfixed shaders); keep
excerpts in evidence/UX-V0/failing-before-excerpts.txt. Then fix, then passing-after.

Changes:
1. Conformant batch shaders (VM01). Rewrite the texture fetch in Texture.glsl (and QuadInstance.glsl if
   affected) so no sampler array is indexed by anything but a constant expression — e.g. a `flat`
   texture-index varying and a switch over the 32 slots, or an equivalent spec-defined technique — and use
   `#version 450 core` like the engine's other 46 shaders (the engine requires GL 4.5 core, Window.cpp:
   327-328). Output must be mathematically identical to today's on a real GPU: same sampler, same UVs,
   same tiling, same colour multiply. Keep the 32-slot contract with Renderer2D. Grep every other .glsl in
   the repo (Cosmic/assets, Projects/**/assets, templates) for the same pattern and fix any hit under KI-82.
2. No crash on a failed shader (VM02). Every engine-owned Shader::Create call site handles nullptr: log
   one clear error that names the shader path, the GL_RENDERER / GL_VERSION strings and the first compiler
   error line, then either fail start-up cleanly (a readable message and a non-zero exit, no access
   violation) or continue without the dependent feature — decide per call site and record the choice.
   Prove it through the production load path with a test (rule 5: a real broken shader file fed through
   the normal loader, e.g. a test asset or a documented env-var override of an asset path; no private-field
   mutation); a render test (tests/render, GL context) or a CosmicTests case is fine.
3. Keep it fixed (VM01). Extend tests/check_gl_conformance.ps1 with a second pass over every *.glsl in
   the repo (vendored dependencies exempt): exit 1 when a `uniform sampler*` array is indexed by a
   non-literal expression, naming file:line. Keep the existing GL-token pass unchanged. It must exit 1 on
   the unfixed shaders (failing-before) and 0 after.
4. Bookkeeping (you own these lines): 03-Acceptance-Catalog.md rows VM01-VM03 (procedure + oracle);
   02-Work-Orders.md: UX-V0 in the dependency graph, the landing order (UX-00 → UX-V0 → UX-01 → …) and
   the ID cross-check; 00-Start-Here.md Waves block: a "Wave 1a UX-V0" line; append to RESUME.md's
   orchestrator TODOs the 01-Contracts.md rows UX-V0 needs after wave 1 lands (§10 ownership of the files
   you touched, §12 index rows VM01-VM03). Do NOT edit 01-Contracts.md.

Acceptance:
 VM01 conformant shaders: extended checker exit 1 before / 0 after; no shader compile error in the
      Starforge, CosmicApp and CosmicRenderTests logs under llvmpipe, Debug and Release.
 VM02 no crash: the new test passes both configs; failing-before shows the access violation.
 VM03 the editor runs in the VM: Starforge starts under llvmpipe, and the retained editor manifests
      tests\acceptance\manifests\wo07-l05.manifest.json and ap03-editor.manifest.json PASS in Debug and
      Release (Run-Acceptance.ps1 with an absolute -Manifest and -TempRoot <worktree>\build\_temp\<name>;
      a failure under load is re-run alone once before it counts; `git checkout --` the tracked evidence
      files the retained manifests rewrite, delete recordings/ and n04-*.bin before committing).
 Also: CosmicTests 523+ / 0 failed / 14 skipped both configs (new cases counted); CosmicRenderTests run
 both configs and the counts recorded — in this VM they are NOT authoritative (llvmpipe): never loosen a
 tolerance or regenerate a golden; hash tests/render/goldens/*.png before and after (must be unchanged).
 Write evidence/UX-V0/HOST-VERIFY.md: the exact commands Kaden runs on his real-GPU host after this lands
 (clone/pull, configure with render tests, build Debug+Release, CosmicRenderTests both configs; expected:
 45 cases, 0 failed, 2 skipped, as at the real-GPU baseline) and what to send back.

Evidence and report: evidence/UX-V0/ (excerpts, result JSONs, golden hashes before/after, HOST-VERIFY.md)
and evidence/UX-V0/report.md in the WO-10 layout. Sub-agents have been refused writing report.md with the
Write tool: create it through Bash (write it in chunks under ~10k characters). Land (L2) is the
orchestrator's: finish with the three checkers exit 0, a clean `git status --short` (recordings/ and *.log
untracked), and local commits (KI registration first, then the fixes, then evidence) as kdadabhoy
<kdadabhoy28@gmail.com>, no AI trailer. Never push. Final chat report ≤ 40 lines + SHAs + git status.
~~~
