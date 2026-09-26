# UX-V0 — HOST-VERIFY (real GPU)

The UX-V0 lane ran on the campaign VM, whose only OpenGL is `OpenGL 4.5 — llvmpipe (LLVM 13.0.1, 256 bits)`
(Mesa 24.1.0). There the golden comparisons are **not authoritative**: llvmpipe rasterises two goldens just over
budget (`instancing2d` 0.118 %, `wo08_text` 0.128 % vs 0.1 %), with or without UX-V0's shader change. The claim
that the rewritten batch shaders (`Texture.glsl`, `QuadInstance.glsl`) produce the same pictures as before on a real
GPU is therefore checked on Kaden's host. Nothing here regenerates a golden or changes a tolerance.

## Commands (PowerShell, on the real-GPU host)

After the orchestrator has merged `ux/v0` into `main` (or, before that, on the lane branch: `git fetch` then
`git checkout ux/v0`):

```powershell
cd C:\dev\Cosmic
git pull                                  # main with UX-V0 merged
git rev-parse HEAD
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON
& $cmake --build build --config Debug   --parallel
& $cmake --build build --config Release --parallel
Get-FileHash tests\render\goldens\*.png -Algorithm SHA256 | Format-Table Hash, Path -AutoSize > golden-hashes-before.txt
build\Runtime\Debug\CosmicRenderTests.exe   --reporters=console --no-intro *> render-debug.txt;   "exit=$LASTEXITCODE" >> render-debug.txt
build\Runtime\Release\CosmicRenderTests.exe --reporters=console --no-intro *> render-release.txt; "exit=$LASTEXITCODE" >> render-release.txt
Get-FileHash tests\render\goldens\*.png -Algorithm SHA256 | Format-Table Hash, Path -AutoSize > golden-hashes-after.txt
git status --short tests\render\goldens
Select-String -Path render-debug.txt, render-release.txt -Pattern '\[doctest\]|ERROR|OpenGL 4|Shader compilation failure|Shader link failure'
```

(Use the cmake path of the installed edition if it is not Community.)

## Expected

- Both configs: `[doctest] test cases: 48 | 46 passed | 0 failed | 2 skipped` and `exit=0`. That is the real-GPU
  baseline of **45 cases, 0 failed, 2 skipped** plus UX-V0's three new VM02 cases (suite `UX-V0 VM02`), which are not
  golden comparisons and also pass on the VM.
- All 15 goldens pass, including `instancing2d` and `wo08_text` (the two llvmpipe misses), and so do the `wo08_*`
  cases whose custom-shader legs load the rewritten `wo08_tint_quad*` fixtures (`render_wo08_batches.cpp`,
  `render_wo08_instancing.cpp`).
- `Shader compilation failure` appears only for the deliberately broken VM02 fixture: 4 times (every nearby error
  line names `ux_v0_undeclared`); no `Shader link failure`; no `[error] Shader::Create:` line without `ux_v0_` in it.
- `golden-hashes-before.txt` = `golden-hashes-after.txt`, with the same 15 hashes as
  `evidence/UX-V0/golden-hashes-before.txt` (that file is `sha256sum` output: lower case), and
  `git status --short tests\render\goldens` prints nothing (`*.actual.png` / `*.diff.png` are gitignored).
- Optional: `build\Runtime\Release\Starforge.exe` opens the homescreen as before.

## What to send back

1. `git rev-parse HEAD` and the GPU line from either file (`OpenGL 4.5 — <renderer>`).
2. The two `[doctest] test cases:` / `assertions:` lines and the `exit=` lines.
3. Every `ERROR` line, if any (with the `*.actual.png` / `*.diff.png` of a failing golden from `tests\render\goldens\`).
4. Whether the two golden-hash files are identical to each other and to `evidence/UX-V0/golden-hashes-before.txt`.

A failing golden here is a real regression of the shader rewrite (the maths is meant to be identical: same sampler,
same `uv * tiling`, same colour multiply) and goes back to the orchestrator as a KI — not a golden update.
