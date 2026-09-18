# WO-08 execution report — 2026-09-18 (renderer / camera / capture safety net, R01–R07)

Only WO-08 was executed, directly on `main` (main-only campaign, D-WORKFLOW). All seven
acceptance cases were implemented, driven through the WO-04 acceptance runner in **Debug and
Release** (R07 Release only — it is a named-machine qualification on the `release` profile), and
every one **PASSED**. Four engine defects were found on the way, each registered in the
known-issue register **before** its fix with failing-before / passing-after evidence
(KI-35..KI-38), plus one pre-existing enforcement gap recorded but deliberately not fixed
(KI-39, other work orders' files). Nothing is `ENVIRONMENT_BLOCKED`: every prerequisite (GPU,
OpenGL 4.5, Windows, the pinned font, the WO-04 runner) was present on the reference machine.

## Scope and provenance

- Initial `HEAD`: `2024748a37fbcfbbae0b4e185fcaa9c3bfcf5863` ("Report WO-07 breadth results …").
  **Handoff deviation:** `origin/main` was already at `2024748` (Kaden had pushed WO-07), so
  `main` started 0 ahead, not 3. No branch, worktree, push, preservation-ref move or tag change
  was made; `engine-3d` and `cosmic-pre-2d-2026-09-16` were not touched; the registered worktree
  `.claude/worktrees/epic-clarke-338e7f` was left alone.
- The untracked root plan `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` and the untracked
  `recordings/` directory were never staged, moved or overwritten.
- Commits are authored **and** committed as `kdadabhoy <kdadabhoy28@gmail.com>` with no
  `Co-Authored-By`, AI or "Generated with" trailer; only explicit WO-08 paths were staged.
- The runner recorded `dirty=True` with `commit=2024748` in every `results.json` because the runs
  happened on the uncommitted WO-08 tree; the commit SHA(s) are given at the end of this report.

## Toolchain and environment

- CMake `C:\Program Files\Microsoft Visual Studio\18\Community\…\CMake\bin\cmake.exe`, VS18 2026
  x64, configured `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON`
  (`configure.log`; cache pins `COSMIC_2D_ONLY:BOOL=ON`). Reconfigured after adding the new
  `.cpp` files (the test lists are explicit; the engine GLOB lacks `CONFIGURE_DEPENDS`).
- Full builds of every target: Release and Debug both **0 warnings / 0 errors**
  (`build-release.log`/`.exit`, `build-debug.log`/`.exit`).
- Acceptance runner: Windows PowerShell 5.1 (`C:\Windows\System32\WindowsPowerShell\v1.0\
  powershell.exe`), absolute `-OutDir` under this directory, repository-local
  `-TempRoot C:\dev\Cosmic\build\wo08-accept-temp`, `-KeepArtifacts`, repository-local child
  `TEMP`/`TMP`, and `-GoldenDir C:\dev\Cosmic\tests\render\goldens` so the runner's own
  before/after SHA-256 covers the real goldens (18 entries: 17 PNGs + `.gitignore`).
- Reference machine (from `results.json`): **DESKTOP-SEOA4BT**, Windows 11 Education
  26200.9457, AMD Ryzen 7 7800X3D (8C/16T, SSE4.2 floor), 31.2 GiB, **NVIDIA GeForce RTX 5070 Ti**
  driver 32.0.16.1692, OpenGL 4.5 (the harness logs "OpenGL 4.5 — NVIDIA GeForce RTX 5070
  Ti/PCIe/SSE2").

## Golden baseline (stated as required)

`tests/render/GoldenImage.h`: goldens are **320×180** (`kGoldenWidth`/`kGoldenHeight`), the
comparator passes a frame when every pixel is within **2/255 per channel** (`kChannelTolerance`)
or at most **0.1 % of pixels** exceed it (`kPixelBudget = 0.001`). The comparator was **not
replaced**; WO-08 extends it with sentinel/ROI probes in `tests/render/wo08_common.h`. The
comparator's missing-golden path (it writes the capture, then fails) is treated as a mutation by
the new wrapper (H03), which hashes the golden dir before/after every child, copies any written or
`.actual`/`.diff` file into `<case>/diagnostics/` and fails the case.

Golden hashes: `goldens-sha256-before.txt` (14 originals, identical to the WO-02 baseline
prefixes) vs `goldens-sha256-after.txt` — the 14 originals are **byte-unchanged**; exactly three
files were added (see "New goldens").

## What was built

**Tests (render suite, `CosmicRenderTests`, tier G):** `tests/render/wo08_common.h` (pixel
probes, index↔colour sentinels, pixel-exact camera, F-2D procedural textures, the documented
`FboTexture` adapter, `StatsScope`), `render_wo08_primitives.cpp` (R01), `render_wo08_batches.cpp`
(R02), `render_wo08_instancing.cpp` (R03 + the new golden), `render_wo08_camera.cpp` (R04 GPU),
`render_wo08_rtt.cpp` (R05), `render_wo08_capture.cpp` (R06 GPU), `render_wo08_perf.cpp` (R07,
`doctest::skip(true)`, run only with `--no-skip=true`). Custom-shader fixtures (tint variants of
the batch quad / SDF circle / both instanced shaders) under `tests/render/fixtures/wo08/`, baked as
`COSMIC_WO08_FIXTURE_DIR`. `render_main.cpp` now switches the hidden window's vsync off (nothing
is ever presented; this makes the R07 "vsync off" statement a fact of the harness).

**Tests (headless, `CosmicTests`, tier U):** `tests/test_wo08_camera.cpp` (R04) and
`tests/test_wo08_png.cpp` (R06), listed explicitly in `tests/CMakeLists.txt`.

**Runner:** `tests/acceptance/fixtures/Run-WO08Render.ps1` (wrapper: H03 golden gate, evidence
captures, diagnostics, perf JSON) and manifests `wo08-gpu` (R01–R06 G), `wo08-units` (R04-U,
R06-U), `wo08-r07` (Q, `release` profile) and `wo08-retained` (388 units + 6 golden cases).

**Engine — observation-only additions (no behaviour change):**
- `Renderer2D::Statistics` gains `Flushes`, `InstanceDrawCalls`, `InstanceCount`, `GlyphCount`
  (the historical four keep their meaning; glyphs still count into `QuadCount`).
- `RendererAPI::FinishGpu()` / `RenderCommand::FinishGpu()` (OpenGL: `glFinish`) — the R07
  timing fence so "complete frame" includes GPU completion without a read-back as the workload.
- `graphics/GpuObjectStats.{h,cpp}` — live engine-owned GPU object counts (framebuffers,
  framebuffer attachments, textures, buffers, VAOs, programs), maintained at the platform layer's
  create/delete sites; R05's leak oracle ("count engine GPU objects, not driver caches").

**Engine — defect fixes (see the register entries):** `Circle.glsl` + `CircleInstance.glsl`
(KI-35), `Renderer2D.cpp` non-material quad paths (KI-36), `UiSystem.cpp` `DrawImageQuad`
(KI-37), `Camera2DController.{h,cpp}` (KI-38).

**Docs:** `docs/reference/rendering-2d.md` (new `Statistics` fields), `cameras.md` (the input
policy), `graphics-resources.md` (`FinishGpu`, `GpuObjectStats`), `docs/reference/README.md`
(manifest row for the new header), `contracts/known-issues.md` (KI-35..39).

## Acceptance-case status (final pass, final binaries)

| Case | Tier | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| R01 primitives/text | G | PASSED (2 cases, 130,915 assertions) | PASSED (2 / 130,915) | `r01-<cfg>/`, `gpu-runner-excerpts.txt` |
| R02 batch boundaries | G | PASSED (7 / 6,835,380) | PASSED (7 / 6,835,380) | `r02-<cfg>/` |
| R03 instancing + new golden | G | PASSED (5 / 2,761,622) | PASSED (5 / 2,761,622) | `r03-<cfg>/` |
| R04-U camera (headless) | U | PASSED (8 / 61,824) | PASSED (8 / 61,824) | `units-runner-excerpts.txt` |
| R04-G camera (GPU) | G | PASSED (4 / 11,390,985) | PASSED (4 / 11,390,985) | `r04g-<cfg>/` |
| R05 RTT / nesting / restoration / leaks | G | PASSED (2 / 2,804) | PASSED (2 / 2,804) | `r05-<cfg>/` |
| R06-U PNG (headless) | U | PASSED (4 / 223) | PASSED (4 / 223) | `units-runner-excerpts.txt` |
| R06-G read-back (GPU) | G | PASSED (2 / 48) | PASSED (2 / 48) | `r06g-<cfg>/` |
| R07 performance | Q | **not run** (not a qualified configuration; `release` profile only) | PASSED (2 / 8,294,418) | `r07-Release/perf-*.json`, `r07-runner-excerpts.txt` |
| retained-units (388) | U | PASSED (388 / 23,116,787) | PASSED (388 / 23,166,677) | `retained-runner-excerpts.txt` |
| retained-goldens (6 cases, 14 goldens) | G | PASSED, byte-exact | PASSED, byte-exact | `retained-goldens-<cfg>/` |

`golden_mutated=False` in every `results.json`; no `.actual`/`.diff` diagnostics were produced in
any final run; every golden (14 old + 3 new) reproduced **byte-exactly** in both configurations
(the comparator's "matched within tolerance but NOT byte-exact" note never appeared).

### R01 — primitives and text

`wo08_primitives` (golden + 33 sentinels): red/green/blue flat quads; a 50 %-white quad over the
blue and over the clear (both bands equal the independently computed alpha-over value, the blue
outside the overlap untouched); a 45° rotated quad (centre yellow, unrotated corner empty, axis
tip filled); a 4×4 checker quad (cells (1,1)/(2,1) exact, UV orientation pinned); an atlas
sub-texture (`CreateFromCoords({2,1})` → tile 6 everywhere); a ×2-tiled checker (four cell
centres incl. two wrapped cells); SDF disc (centre filled, r=1.2 clear), **ring** (centre clear,
r=0.9 filled — KI-35), ellipse (inside on the long axis, outside the normalised radius); a dashed
diagonal `DrawLine` and a `DrawRect` (ink counts along known edges — `Line.glsl` dashes at 0.05
world units, documented in the test); the per-flush grouping sentinel (a circle submitted BEFORE a
quad at the same spot is drawn over it; a line submitted before a quad stays over it — quads →
lines → circles → text within a flush); a Material quad reading `u_Color`. Stats:
`Flushes=2`, `DrawCalls=4`, `QuadCount=11`, `CircleCount=4`, `LineCount=6`, `GlyphCount=0`.

`wo08_text` (golden + per-glyph ROIs): "Cosmic 2D" at 1.6 units, "Batch\nText" (two lines, one
`LineHeight` = 1.0 em for Roboto apart), "AV" with +0.25 em kerning, and "A\xC3\xA9B" (UTF-8 "é")
which renders as **"A??B"** — every visible glyph box has ink of its colour, every space has none,
and the fallback frame is **byte-identical** to drawing "A??B" directly. `GlyphCount=23`,
`QuadCount=23`, one text draw. Empty strings, `\r\n \t` and a null font draw nothing except the
`'?'` fallback for `\t` (documented: any byte without a glyph becomes `'?'`).

### R02 — batch boundaries (exact counts)

Oracle: item *i* at grid cell *i* in colour `EncodeIndex(i)`; after the frame cell *i* must decode
to *i* for *i < N* and to "clear" beyond — dropped, duplicated and shifted items are all visible.

| Kind | N = 0 | 1 | 9,999 | 10,000 | 10,001 | 20,001 |
| --- | --- | --- | --- | --- | --- | --- |
| flat quads: draws / flushes | 0 / 0 | 1 / 1 | 1 / 1 | 1 / 1 | **2 / 2** | **3 / 3** |
| SDF circles: draws / flushes | 0 / 0 | 1 / 1 | 1 / 1 | 1 / 1 | 2 / 2 | 3 / 3 |
| lines (dash-safe camera): draws / flushes | 0 / 0 | 1 / 1 | 1 / 1 | 1 / 1 | 2 / 2 | 3 / 3 |
| glyphs, one `DrawString` per glyph: draws / flushes | 0 / 0 | 1 / 1 | 1 / 1 | 1 / 1 | 2 / 2 | 3 / 3 |

All 20,001-cell grids verified with 0 dropped / 0 wrong / 0 extra at every N. One 10,001-glyph
string crossing `MaxTextQuads` mid-string: 2 draws, all 10,001 cells inked, none beyond.
Texture slots (2×2 flat textures, slot 0 reserved): **30 → 1, 31 → 1, 32 → 2, 33 → 2, 62 → 2,
63 → 3, 64 → 3** draws, each quad sampling its own texture; a texture reused in a batch costs no
slot (31 distinct + 31 repeats + 2 flat = 1 draw); after a rollover the next batch's fresh slot
table shows texture 0 again. Material / shader: material→5 flat quads = **2 draws** (3 flushes: the
first material switch flushes an empty default bucket), material→textured/sub-textured/rotated =
2, alternating flat/material ×2 = 4, two materials on one shader = 3 (identity breaks, uniforms do
not leak), default/custom/default circle shader = 3 circle draws, a fresh pass always starts on
the default material and circle shader. Mixed: 100 quads + 100 lines + 100 circles + 100 glyphs
interleaved = **1 flush, 4 draws**, `QuadCount=200` (glyphs counted), every cell correct; 10 quads
+ 5 instanced quads + 10 quads = 3 draws, 1 instance draw, 2 flushes.

### R03 — real 2D instancing

| N | 0 | 1 | 10,000 | 19,999 | 20,000 | 20,001 | 40,001 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| instance draws (circles and quads alike) | 0 | 1 | **1** | 1 | 1 | **2** | **3** |

`InstanceCount == N`, `DrawCalls == InstanceDrawCalls` for the isolated submissions, every
instance in its index-coded cell (0 dropped / wrong / extra across 40,201 cells), a null pointer
with a nonzero count is a no-op. Custom shader paths (tint fixtures) at 1 / 20,000 / 20,001: same
chunking, tinted output, and afterwards the default instanced and batched paths are untinted
(magenta/white/cyan/white/white/white sequence, 4 instance draws + 2 batches = 6 draws). Normal
draws before/after (50 quads + 50 circles each side of 1 / 10,000 / 20,001 instances): 2 flushes,
`ceil(N/20,000) + 4` draws, all 100+N+... cells correct.

### R04 — camera

Headless (independent pinhole oracle, bar 0.5 px inside the **declared envelope** |focus| ≤
1000·zoom, |world−focus| ≤ 4·zoom·max(aspect,1)): ScreenToWorld round trips over 10 viewports
(1280×720/1600×900/1920×1080/2560×1440 with and without offsets = 100/125/150/200 % DPI, 641×359
with and without offset) × 8 zooms (0.01 … 10,000) × 4 focus points — **51,840 samples, worst
0.043 px**; the controller's own projection vs the model **worst 0.0011 px** and its matrix
inverse round trip ≤ 0.5 px; zoom-about-cursor **1,550 pairs asserted** inside the envelope (370
pairs outside it only measured: worst 127.8 px, which is float ulps at 10,000→0.01 about a corner
18,000 units away — hence the declared envelope); PanBy under every viewport ≤ 0.5 px; the zoom
range clamps at 0.01 / 10,000 with zero and negative → 0.01, and at both ends the visible edge
projects to the viewport edge; the **nonfinite policy** (KI-38) — NaN/±inf zoom, focus, resize,
rect, bounds, constructor aspect and scroll amount are rejected and the projection never moves;
FrameBounds on zero-size / inverted / tiny / huge boxes; a viewport changed through 1×1, 641×359,
1920×1080, 3×3, 1×1080, 0×0 (ignored), 1080×1, 641×359 keeps a finite projection with the focus
at the centre. The outside-envelope sample (zoom 0.01, focus 1,000) measures 1.47 px and is
reported, not asserted.

GPU: an offset pass viewport (40,60,320,180 inside 400×300) fills exactly its rectangle and puts
three markers on the predicted target pixels; 100/125/150/200 % target sizes scale the marker's
position to 1e-3 px and its footprint to 9²/11²/14²/18² (±1 px per edge); **641×359** (deliberately
odd on both axes so the centre column/row are single pixels, not a multiple of 8 so no alignment
helps, aspect 1.7855 ≠ 16:9) puts a one-pixel marker exactly on (320,179) with all four neighbours
clear and 3×3 corner markers on (0,0)/(640,0)/(0,358)/(640,358); the changing-viewport sequence
(1×1 → 641×359 → 320×180 → 3×3 → 1920×1080 → 1×1 → 2×1080 → 640×360) with `Camera2DController`
matrices hits the centre pixel every time (exactly for odd sizes), and `Resize(0,0)` is refused.

### R05 — RTT, nesting, restoration, leaks

Two targets (64×48, 48×64) captured top-left-origin with their corner markers and top bar in
place; shown 1:1 through `UiImageComponent::RuntimeTexture` with the documented `FboTexture`
adapter — **upright after KI-37** on both the plain and the 9-slice paths (a GL-native 4×4
red-over-blue texture shows red at the rect top through both). Passes nested three deep (world →
pixel camera on an offset 60×40 viewport → a 20×10 camera inside it): each level fills exactly its
own viewport, the level-1 probe after the level-2 pop lands on level-1 pixels, and outer geometry
after the nesting lands on the outer camera's pixels. `RenderCommand::GetBoundFramebuffer()` is
identical before and after `SceneRenderer::RenderToTexture` into a third target (which did render:
green sprite, black corner). After `UiSystem::Render`, a half-alpha quad blends alpha-over
(blend restored) and a farther quad drawn second stays hidden (depth test + write restored). A and
B are **byte-identical** to their first captures after everything that sampled them (no
source→target feedback); re-rendering A shows the new content through the adapter. 200 cycles of
create → render → resize → render → resize → render (sizes cycling through 64×48, 641×359, 1×1,
320×180, 3×3, 256×256, 1920×1080, 2×2, each shown through the UI) → destroy: `GpuObjectStats`
after == before (`fbo=3 att=6 tex=2 buf=11 vao=6 prog=6` in both configs), and one probe target
accounts for exactly +1 framebuffer / +2 attachments, a resize replaces rather than accumulates.

### R06 — capture

Headless: the 7×5 four-corner fixture (alpha 255/128/64/0, RGB kept under alpha 0) round-trips
byte for byte; the PNG's signature, IHDR (7×5, depth 8, colour type 6) and IHDR CRC are checked by
parsing the bytes; **decode orientation is proven against a PNG the test builds by hand**
(stored-deflate zlib + adler32 + crc32) — row 0 comes back as the top row; RGB (3-channel) writes
decode with alpha 255; null data / 0, −1 dims / 0, 5 channels, a missing directory, an empty path,
a missing file, a non-image file and a 33-byte truncated PNG all fail and leave the outputs
untouched; `ResizeRgba` copy / box average / bilinear corners / degenerate no-ops.

GPU: a 641×359 RGBA8 target reads back 641×359×4 with the top-left marker at index 0, the other
three corners at their indices, the row stride exact (row 0's last pixel vs row 1's first), a
half-alpha white quad over an alpha-zero clear = (128,128,128,64), cleared pixels (0,0,0,0), an
out-of-range attachment failing without touching outputs, the capture round-tripping through the
CPU PNG path byte for byte and equal to the harness `Capture()`. HDR-to-byte on RGBA16F with
blending off: **2.0 → 255, 0.25 → 64 (±1), −1.0 → 0, 0.5 → 128 (±1), 4.0 → 255, alpha 0.75 → 191
(±1)** — "convert + clamp", as documented on `ReadPixels`.

### R07 — performance (correctness kept separate)

Method: per frame clear + `PushRenderPass` + ONE `DrawInstanced*` of exactly 10,000 instances +
`PopRenderPass` + **`RenderCommand::FinishGpu()`** (full GPU completion fence) → clock stops; no
read-back in the timed region; an offscreen 1920×1080 RGBA8+D24S8 target, so there is no
swap-chain present and vsync cannot throttle (the harness window's vsync is off regardless).
Deterministic LCG layouts (seeds 12345 / 54321), 10-s warmup then 60-s sample, Release, machine
**DESKTOP-SEOA4BT** (Ryzen 7 7800X3D, RTX 5070 Ti, driver 32.0.16.1692, Windows 11 26200.9457).

| Workload | frames | complete-frame ms mean / p50 / **p95** / **p99** / max | CPU-submit p95 | GPU-zone p95 / p99 | draws/frame | bar | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 10,000 instanced circles | 411,535 | 0.145 / 0.093 / **0.366** / **0.442** / 2.12 | 0.017 | 0.330 / 0.380 | 1 | p95 ≤ 16.67, p99 ≤ 33.33 | PASSED |
| 10,000 instanced quads | 317,850 | 0.188 / 0.120 / **0.405** / **0.491** / 3.15 | 0.022 | 0.360 / 0.419 | 1 | same | PASSED |

Correctness inside the same run (asserted separately from the timing): exactly 1 instance draw
per frame on every frame, `InstanceCount == 10,000`, the last frame is not blank. GPU-zone samples
equal the frame count because the fence makes each frame's timestamp query resolvable at the next
`GpuFrameMark`. These numbers qualify the named machine only; they are not a claim for every
supported GPU. Debug was not run for R07 (not a qualified configuration).

## New goldens — provenance and review

Generated once, deliberately, **outside** the runner (`golden-generation/command.txt`:
`CosmicRenderTests.exe --update-goldens --test-case="R01 primitives*,R01 text*,R03 golden*"`,
Release, on the reference machine, at `HEAD 2024748` + the WO-08 working tree — the dirty diff at
that moment is what the first WO-08 commit contains), with the golden dir hashed before (14 files)
and after (17): only the three new files differ (`golden-generation/goldens-sha256-*.txt`). The
comparator-written copies from the discovery runs were deleted before generation; nothing was
blessed from a failing run. Both configurations then reproduced all three byte-exactly.

- **`wo08_primitives.png`** (`3A91FCD4…DC95`) — dark navy clear. Row 1: red, green and blue
  quads; a 50 %-white quad straddling the blue quad's right edge (three bands: blue, lavender
  overlap, grey over the clear); a yellow diamond (45°); a 4×4 light/dark checker; an orange
  atlas-tile quad. Row 2: an 8×8 tiled checker; a cyan filled disc; an **orange ring** (wall 0.2 of
  the radius, hollow centre); a purple ellipse; a dashed cyan diagonal line inside a dashed white
  rectangle. Row 3: a magenta material quad; a dashed yellow line crossing a dark blue-grey quad;
  a magenta disc covering a small dark quad. Reviewed: this is the intended scene; before KI-35 the
  ring was a small dot (see `ki35-circle-thickness/failing-before/r01-primitives.png`).
- **`wo08_text.png`** (`6E7220E4…5C5A`) — "Cosmic 2D" large in cream at the upper left, "Batch" /
  "Text" on two lines in light blue below it, "A V" with a widened gap lower right, and "A??B"
  (the fallback for the two UTF-8 bytes of "é") upper right. Reviewed: crisp SDF glyphs, no missing
  glyph, no stray ink.
- **`instancing2d.png`** (`95F18D75…1B01`) — a 24×12 grid of instanced quads sweeping the hue
  (red → yellow → green → cyan → blue → magenta, top-left to bottom-right) over a dark backdrop quad
  (batched, drawn before), a 12×6 field of instanced SDF circles in warm colours alternating per
  column full disc / thin ring (0.3) / thick ring (0.6) and per row crisp (fade 0.005) / soft-edged
  (fade 0.3), a teal batched disc at the bottom-left and a white rotated batched quad at the
  bottom-right (both drawn after the instanced calls). Reviewed: pins both instanced shaders
  (post-KI-35) and the batch↔instanced interleave. It is a **2D** golden; `instancing.png` (the 3D
  `InstanceSet` golden from `render_3d.cpp`) is untouched and not built in this configuration.

## Defects found (all registered before the fix; failing-before → passing-after)

| KI | What | Failing-before | Passing-after | Fix |
| --- | --- | --- | --- | --- |
| **KI-35** | `DrawCircle` thickness inverted — a ring rendered as a small disc (both circle shaders); the documented contract and every shipping caller (SF_Telem wheel tyres, launcher rings) expect rings | `ki35-circle-thickness/failing-before/` (R01 ring sentinels fail; frame shows the dot) | `ki35-circle-thickness/passing-after/` (R01 clean except the then-missing goldens) | `Circle.glsl`, `CircleInstance.glsl`: wall factor evaluated on `distance` (1−r) |
| **KI-36** | After a Material quad, every later non-material quad became its own draw under the material's shader/uniforms (6 draws for 1+5 quads; flat white quads rendered magenta); reachable from `Scene::OnRenderSprites` | `ki36-material-restore/failing-before/` (13 failed assertions, capture shows six magenta cells) | `ki36-material-restore/passing-after/` (R02 all 7 cases) | `Renderer2D.cpp`: the six non-material paths rejoin `DefaultMaterial` after the flush |
| **KI-37** | `UiImage` drew every texture vertically flipped (plain and 9-slice paths) — authored UI images and RTT feeds upside-down | `ki37-uiimage-flipped/failing-before/` (rect top-left reads the target's bottom-left; bar at the bottom) | `ki37-uiimage-flipped/passing-after/` (R05 2,804 assertions) | `UiSystem.cpp` `DrawImageQuad`: V runs 1→0 from the screen top on both paths |
| **KI-38** | `Camera2DController` accepted NaN/inf zoom/focus/size/bounds/scroll → non-finite (blank, sticky) projection | `ki38-camera-nonfinite/failing-before/` (107 failed assertions) | `ki38-camera-nonfinite/passing-after/` (all 12 WO-08 headless cases) | policy: non-finite inputs rejected, documented in the header + `cameras.md` |
| KI-39 | Pre-existing: both trunk audits are red at HEAD for WO-04/06/07 reasons (8 inline `/*GL_…*/` comment tokens in two WO-07 fixtures; 2 unlisted headers from WO-04/WO-06) | `audit-gl-conformance.txt`, `audit-docs-coverage.txt` | — | **open**, out of WO-08 scope; recipe in the register. WO-08's own header is listed and no WO-08 file adds a GL token |

Every counterfactual was captured on the working tree before the fix and re-run after it with a
rebuilt binary; no destructive reset was used and no golden was regenerated to absorb a
regression. Existing goldens: unchanged (none draws a circle or a textured UI image).

## Retained tests (regression safety)

Through the runner, both configurations, after the last engine change:
- `retained-units` — `CosmicTests --test-case-exclude="WO-08 *"`: **388 passed / 0 failed** in
  Debug (23,116,787 assertions) and Release (23,166,677); the 22 skipped are the 10 native host
  cases the other manifests launch plus the 12 WO-08 cases the filter excludes — WO-08 adds 12
  headless cases, so the unfiltered suite is 400.
- `retained-goldens` — the six pre-WO-08 2D golden cases (29 assertions) with the wrapper's
  before/after hash: all 17 PNGs unchanged in both configs; the 14 originals match the WO-02
  baseline hashes exactly.
- Source audits: **not clean at HEAD for pre-existing reasons** (KI-39); the WO-08 files scan
  clean (`\bgl[A-Z]…\(|\bGL_…` finds nothing in them outside full-line comments) and the WO-08
  header is covered.

## Notes, limits and honest caveats

- The R04 "scroll with a cursor anchor" path (`OnMouseScrolled` → `Input::GetMouseScreenPosition`
  → `Application::Get()`) cannot run without an `Application`; it is covered through its pure
  statics (`ScreenToWorld`/`ZoomAboutPoint`, 1,550 pairs) and the scroll rejection of NaN/inf
  through the real event with a zero viewport. Not `ENVIRONMENT_BLOCKED` — it is a design
  boundary of the rig, and the anchor math is what the scroll handler calls.
- The overlap sentinels draw with **depth-write OFF** (`Wo08::BeginFrame`), the convention the
  engine's own sprite pass uses (`Scene.cpp:620-622`); with the harness default (write ON) a later
  primitive at the same z fails `GL_LESS`. World-space SDF text under depth-write ON shows edge
  artefacts where adjacent glyph boxes overlap (their SDF padding writes depth at the AA edge) —
  inherent to SDF text with depth writes, not a defect; `UiSystem` draws text with depth off.
- `Statistics::Flushes` counts empty flushes (e.g. entering a Material bucket from an empty
  default bucket); the draw-count assertions are the meaningful ones and `Flushes` is documented
  accordingly.
- 31, not 32, distinct non-white textures fit one batch (slot 0 is the white texture) — as the
  catalog says; recorded, not a defect.
- The per-case wrapper deletes any comparator-written golden and copies it to `diagnostics/`; in
  the final runs nothing was written.
- Evidence hygiene: the wrapper's scratch dirs are removed per child; no junctions were created;
  the runner's `_temp` lived under `build/`; discovery-run captures and the duplicated large Debug
  grid PNGs (identical frames; Release copies kept) were deleted to keep the evidence at ~1.4 MB of
  PNGs; the gitignored `*.log` files stay uncommitted (`results.json` / `results.junit.xml` /
  `children.json` / excerpts are committed).
- Not done, by scope: WO-09 authored content, WO-10 clocks, any renderer ordering redesign, 3D
  paths, new visual features, and the KI-39 comment/manifest fixes in other work orders' files.

## Files

- Engine: `Cosmic/src/renderer/Renderer2D.{h,cpp}`, `RendererAPI.h`, `RenderCommand.h`,
  `Cosmic/src/graphics/GpuObjectStats.{h,cpp}` (new), `Cosmic/src/platform/OpenGL/
  {OpenGLRendererAPI.{h,cpp},OpenGLFrameBuffer.cpp,OpenGLTexture.cpp,OpenGLBuffer.cpp,
  OpenGLVertexArray.cpp,OpenGLShader.cpp}`, `Cosmic/src/camera/Camera2DController.{h,cpp}`,
  `Cosmic/src/scene/ui/UiSystem.cpp`, `Cosmic/assets/shaders/{Circle,CircleInstance}.glsl`.
- Tests: `tests/render/{wo08_common.h,render_wo08_*.cpp,render_main.cpp,CMakeLists.txt}`,
  `tests/render/fixtures/wo08/*.glsl`, `tests/render/goldens/{wo08_primitives,wo08_text,
  instancing2d}.png`, `tests/{test_wo08_camera.cpp,test_wo08_png.cpp,CMakeLists.txt}`.
- Runner: `tests/acceptance/fixtures/Run-WO08Render.ps1`,
  `tests/acceptance/manifests/wo08-{gpu,units,r07,retained}.manifest.json`.
- Docs: `docs/reference/{README,rendering-2d,cameras,graphics-resources}.md`,
  `docs/plans/2d-stability-2026-09-16/contracts/known-issues.md`, this directory.

## Local commits (not pushed — Kaden pushes)

1. `cbcc8befee64aed186eae5377626435409c6c444` — Fix circle thickness, material bucket, UiImage
   orientation and camera NaN policy (WO-08): the four fixes, KI-35..KI-39 in the register, and
   the `Renderer2D::Statistics` observation counters.
2. `49a4e1ed885d34e8418b2598a544f1e703516681` — Add R01–R07 renderer/camera/capture acceptance
   cases and the 2D instancing golden (WO-08): tests, fixtures, the three goldens, `FinishGpu`,
   `GpuObjectStats`, the runner wrapper + manifests, reference docs.
3. The evidence commit that carries this report and `evidence/WO-08/**` (JSON/JUnit/excerpts/
   captures/KI counterfactuals; the gitignored `*.log` files are not committed).

Author and committer on all three: `kdadabhoy <kdadabhoy28@gmail.com>`, no trailers.
