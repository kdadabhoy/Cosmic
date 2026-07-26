# Phase 30 Plan — 2D Engine Hardening: stress, edge cases, fuzz, and a driven-on-GPU campaign

> **STATUS 2026-07-25 — ☐ PLANNED, nothing implemented.** This document is the complete,
> self-contained work order set for hardening the **2D engine configuration** (`engine-2d` /
> `COSMIC_2D_ONLY=ON`) against scale, degenerate input, hostile data and long runs — and for
> driving both editors on the real GPU to find what a headless suite structurally cannot.
>
> **Created 2026-07-25** from a user request, immediately after Phase 29 completed: *"stress
> testing / edge case testing / general testing on the 2D engine in particular"*, with the plan
> doc's prompts to state explicitly that **the assistant may look at the user's computer and
> manually test and debug the running application**.
>
> **Depends on:** Phase 29 (doc 28) complete — the 2D configuration must build and pass before it
> can be stressed. It is otherwise independent and blocks nothing.
>
> **Work orders:** P0–P9. Each is independently executable; §12 gives every phase its own context,
> anchors, gotchas, DoD, verification commands, and a copy-paste prompt.

---

## 0. Execution notes

**The 2D configuration is the subject, but both must stay green.** Per the roadmap's standing
working agreement (added by Phase 29 W10), no work order may land with either configuration
broken. Most tests added here are **shared tier** — the 2D stack compiles in both — so a 2D
hardening test usually runs in the 3D suite too, and that is a feature, not an accident: it means
the 3D build polices the same invariants.

**Re-verify before edit.** Every line number and constant in this document is a *starting point*.
Find the target by content (function name, comment text, member name) and confirm the surrounding
lines match before editing. Phase 29 moved a great deal of code; anchors will drift again.

**Tests must be non-vacuous, and you must prove it.** This is the single most important rule in
the document, and it is Phase 29 W9's hard-won lesson. A test that passes against broken code is
worse than no test, because it manufactures confidence. **For every bug this phase fixes, revert
the fix, re-run the test, and record the observed failure in the commit message.** For every limit
test, verify it actually reaches the limit (assert the counter, not just the absence of a crash).

**Fuzz must be seeded.** Every fuzz loop takes a fixed seed so a failure is reproducible and a
rerun is bit-identical. No `std::random_device`, no time-based seeding. Record the seed in the
test name or a `CAPTURE`.

**Never add production API purely to make a test possible.** W9 hit this with `TelemHub`'s
dirty-recording flush and `SerialLink`'s connected-state paths and correctly left them uncovered
with a stated reason. If a behaviour is only reachable through ImGui or a GPU context, either move
it to the on-GPU tier (P8) or state in the test file why it is not covered. **A refactor that
extracts a genuinely pure function from a mixed one is fine** — that is what `TelemHub::IngestChunk`
was, and it is the preferred move.

**Git.** Unless the user says otherwise, the standing rule applies: **the assistant runs no git
write commands.** Leave working-tree edits and a summary; the user commits. Phase 29's local-commit
exception was granted for that phase specifically and **does not carry over** — see §13.

---

## 1. Why, and what "hardening" means here

Phase 29 gave 2D its own engine. What it did **not** do is prove that engine survives contact with
real content. Every 2D suite written so far — and Phase 29 W2 wrote seven of them — tests
*correctness on small, well-formed inputs*: a handful of sprites, a 4×4 tilemap, a dozen UI rects.
That was the right thing to build first, because those suites were the regression net the whole
partition was measured against. They are not a robustness net.

**What is untested today, verified by reading the suites:**

| Area | Covered today | Not covered |
|---|---|---|
| `Renderer2D` batching | nothing directly | **every documented limit** — `MaxQuads`, `MaxTextureSlots`, `MaxLines`, `MaxCircles`, `MaxTextQuads`, `MaxInstancedQuads`. No test crosses a batch boundary. |
| Sprite pipeline | `test_sprite_order` (8 cases) — painter order, flips, gates | scale (10k+ sprites), degenerate transforms (NaN, inf, zero/negative scale), Z ties, sort stability |
| Tilemaps | `test_tilemap` (3) + `test_tilemap_extra` (10) | **`kMaxGrid` = 1024 ⇒ 1,048,576 cells** vs a 10,000-quad batch. Culling at extreme zoom/pan is never exercised. |
| UI | `test_ui_rects` (18) + `test_ui_anchor` (12) | deep nesting, parent cycles, degenerate/inverted rects, hit-test at scale, canvas scale extremes |
| 2D lights | `test_light2d` (8) | many lights, zero/negative radius, lights off-screen, the half-res buffer at odd viewport sizes |
| Camera2D | `test_camera2d` (4 cases, 84 lines) | zoom extremes, aspect degeneracies, `ScreenToWorld`/`ZoomAboutPoint` round-trips |
| Flow / Story | `test_flowmachine` (9) + `test_story` (4) | **no fuzz.** Cyclic graphs, dangling targets, malformed `.cflow`/`.cstory` |
| Scene JSON | `test_scene_serializer` | **no fuzz.** W9 fuzzed telemetry binaries; scene/prefab/material JSON has never seen hostile input |
| `EventBus` | incidental use in 2 suites | no dedicated suite: handle reuse, disconnect-during-emit, emit-during-emit, `Clear` while live |
| Long runs | nothing | no soak test; no leak or drift detection |

**The deliverable is not "more tests".** It is: *the 2D engine has been driven past its documented
limits and fed hostile data, every failure found is either fixed or recorded as a known limit, and
the limits themselves are written down.*

---

## 2. Verified starting state (2026-07-25, `main` = `3066e6a`)

- **3D:** `CosmicTests` **513/513**, `CosmicRenderTests` 14/14, Debug + Release 0-warn,
  `check_gl_conformance.ps1` clean.
- **2D:** `CosmicTests` **340/340**, 6/6 render cases, Debug + Release 0-warn, conformance clean.
- `engine-2d` is at `dce7e85`, **3 commits behind `main`** (`451b926` W9, `e5d7d29` `/MP`,
  `3066e6a` W10). **P0 carries these across before anything else.**
- Worktree `C:\dev\Cosmic-2D` on `engine-2d`, configured with `cmake --preset 2d`.
- `tests/CMakeLists.txt` hand-lists a shared tier and an `if(NOT COSMIC_2D_ONLY)` 3D tier.
- `tests/render/` holds `render_main.cpp` (window + renderer bootstrap, `--update-goldens`),
  `GoldenImage.{h,cpp}`, `render_2d.cpp` (6 cases), `render_3d.cpp` (8), and 14 committed PNGs.
  Gated behind `COSMIC_BUILD_RENDER_TESTS=ON`, **never in CI** — it needs a real GPU.

**Verified constants that this phase exists to test** (`Cosmic/src/renderer/Renderer2D.cpp`,
around L63–L156 — re-verify by content):

```cpp
MaxQuads            = 10000;   MaxVertices = MaxQuads * 4;   MaxIndices = MaxQuads * 6;
MaxTextureSlots     = 32;
MaxLines            = 10000;
MaxCircles          = 10000;
MaxInstancedCircles = 20000;
MaxTextQuads        = 10000;
MaxInstancedQuads   = 20000;
```

`TilemapComponent::kMaxGrid = 1024` (`scene/Components.h`) ⇒ a maximum map is **1,048,576 cells**,
two orders of magnitude past one quad batch.

---

## 3. The four test tiers

| Tier | Where | Runs in CI | What belongs here |
|---|---|---|---|
| **T1 — headless invariants** | `tests/test_*.cpp` | ✅ | Anything expressible as pure logic: draw-list construction, layout maths, graph evaluation, serialization round-trips. **Default tier. Prefer it.** |
| **T2 — seeded fuzz** | `tests/test_*_fuzz.cpp` (or a fuzz suite inside a T1 file) | ✅ | Hostile bytes/strings/structures against a parser or a state machine. Fixed seed, bounded iterations, deterministic across runs. |
| **T3 — golden image** | `tests/render/render_2d.cpp` | ❌ local only | Anything that needs a real GPU to be meaningful: batch-boundary correctness, blend order, the 2D light composite. |
| **T4 — driven on-GPU** | not a file — a **recorded manual campaign** (§11) | ❌ | Editor interaction, long-run soak, anything with an ImGui-only trigger. **The assistant drives this directly on the user's machine** (§0, §11). |

**Escalate only when you must.** A test that could be T1 and is written as T3 costs a GPU and a
human to run. A behaviour that is genuinely only reachable through the editor belongs in T4 and
must be *recorded* there, not silently dropped.

---

## 4. Work order table

| WO | Content | Tier | Size |
|---|---|---|---|
| **P0** | Carry `main` → `engine-2d`, re-baseline both suites, write the limits inventory | — | S |
| **P1** | `Renderer2D` batch boundaries and every documented limit | T3 + T1 | L |
| **P2** | Sprite pipeline at scale + degenerate transforms | T1 | M |
| **P3** | Tilemap stress — the million-cell map, culling, bounds | T1 + T3 | M |
| **P4** | UI system — depth, cycles, degenerate rects, hit-test at scale | T1 | M |
| **P5** | Flow / Story graph fuzz + `EventBus` suite | T1 + T2 | M |
| **P6** | Scene / prefab / material JSON fuzz | T2 | M |
| **P7** | Camera2D + Light2D edge cases | T1 + T3 | M |
| **P8** | **The driven-on-GPU campaign** — both editors, soak, leak watch | T4 | L |
| **P9** | Fix pass, limits documentation, phase report | — | M |

Every work order ends with **both** configurations green and a working-tree summary for the user
to commit.

---

## 5. Standing rules every prompt inherits

- Read this document's §0 and the named work order before editing.
- **Re-verify every anchor by content, not line number.**
- Both configurations must stay green: configure + build Debug **and** Release with zero warnings,
  `CosmicTests` all pass, `check_gl_conformance.ps1` clean — in **each**.
- New test files must be added to `tests/CMakeLists.txt` by hand (there is no glob) and go in the
  **shared tier** unless they name a 3D-only type.
- Never invoke `build.bat` / `build_all.bat` / `build_engine.bat` / `build_2d.bat` — they end in
  `pause` and hang. Use the `cmake.exe` recipe in §6.
- Don't pipe native-exe stderr through `2>&1` in PowerShell 5.1 — it wraps errors and flips the
  exit code. Use `Tee-Object` + `$LASTEXITCODE`.
- Reconfigure (not just rebuild) whenever source files are added or removed — the engine GLOB has
  no `CONFIGURE_DEPENDS`, and `tests/CMakeLists.txt` is hand-listed.
- **Prove non-vacuity** (§0) and record it.
- **No git write commands** unless the user extends Phase 29's exception (§13).

### The computer-use authorization (read this before P8, and before debugging anything)

**The assistant may look at the user's computer and manually test and debug the running
application.** This is explicit, standing authorization from the user for this phase. In practice:

- **Take screenshots of the desktop and drive the editors** with `mcp__computer-use__*` — click,
  type, scroll, open menus, run scenes, toggle chips.
- **Launch the built executables** and interact with them as a user would.
- **Read the engine's own logs** — `logs/Cosmic_*.log` under the build output — which is almost
  always faster and more reliable than reading a console through screenshots.
- **Debug interactively:** reproduce a failure live, form a hypothesis, edit code, rebuild, and
  re-drive to confirm. A bug found on-GPU should end the session with a headless test that pins it
  (T1) wherever that is possible, so it cannot come back.

This is not limited to P8. **If a headless test fails in a way you cannot explain, driving the real
app is a legitimate and encouraged next step at any point in this phase.** Phase 29's on-GPU pass
found four real bugs — including a pre-existing `abort()` on every viewport-strip toggle — that the
entire headless suite had missed. See §11 for the recorded gotchas about driving this specific app;
they will save you an hour each.

---

## 6. The verification recipe (referenced by every phase)

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# 2D configuration — the subject of this phase
& $cmake -S C:\dev\Cosmic-2D -B C:\dev\Cosmic-2D\build -A x64 -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_2D_ONLY=ON
& $cmake --build C:\dev\Cosmic-2D\build --config Debug   --parallel
& $cmake --build C:\dev\Cosmic-2D\build --config Release --parallel
& C:\dev\Cosmic-2D\build\Runtime\Debug\CosmicTests.exe --reporters=console --no-intro

# 3D configuration — must stay green (shared-tier tests run here too)
& $cmake -S C:\dev\Cosmic -B C:\dev\Cosmic\build -A x64 -DCOSMIC_BUILD_TESTS=ON
& $cmake --build C:\dev\Cosmic\build --config Debug   --parallel
& $cmake --build C:\dev\Cosmic\build --config Release --parallel
& C:\dev\Cosmic\build\Runtime\Debug\CosmicTests.exe --reporters=console --no-intro

powershell -ExecutionPolicy Bypass -File C:\dev\Cosmic\tests\check_gl_conformance.ps1
```

**Golden-image tier (T3), local only, needs a real GPU:**

```powershell
& $cmake -S C:\dev\Cosmic-2D -B C:\dev\Cosmic-2D\build -A x64 -DCOSMIC_BUILD_RENDER_TESTS=ON -DCOSMIC_2D_ONLY=ON
& $cmake --build C:\dev\Cosmic-2D\build --config Debug --parallel
& C:\dev\Cosmic-2D\build\Runtime\Debug\CosmicRenderTests.exe
```

**Build gotcha:** leftover MSBuild worker processes (node reuse) hold `Starforge.pdb` and cause
`LNK1201: error writing to program database` on the next build in *either* tree. Kill stray
`MSBuild` processes, or build with `-- /nodeReuse:false`.

---

## 7. What to look for — the failure taxonomy

When designing cases, work through this list rather than inventing ad hoc inputs. Each row has bitten
a real engine.

| Class | Concrete 2D instances |
|---|---|
| **Boundary ±1** | exactly `MaxQuads`, `MaxQuads + 1`, `MaxTextureSlots`, 33 distinct textures, `kMaxGrid`, `kMaxGrid + 1` |
| **Zero and empty** | zero sprites, empty tilemap, zero-radius light, empty string paths, zero-size viewport, empty canvas |
| **Negative** | negative scale (mirrors winding), negative tile indices, negative grid dims, negative zoom |
| **Non-finite** | NaN / ±inf in `Position`, `Scale`, `Rotation`, colour, UV, camera focus |
| **Extreme magnitude** | 1e9 world coordinates, 1e-9 scale, zoom 1e-6 and 1e6 |
| **Aliasing / identity** | same entity parented to itself, a UI cycle, an entity emitting a signal it also listens for |
| **Ordering ties** | equal Z, equal sort keys — is the order *stable* and does it match the golden? |
| **Lifetime** | destroy during iteration, disconnect during emit, `Clear` while handlers are live |
| **Hostile data** | truncated / byte-flipped / structurally-valid-but-absurd `.cscene`, `.cflow`, `.cstory`, `.cmat` |
| **Accumulation** | 100k frames — do counters wrap, do handles leak, does float drift show up? |

---

## 8. Test-file layout

New files, all **shared tier** in `tests/CMakeLists.txt` unless noted:

```
tests/test_renderer2d_limits.cpp     P1 — the headless half (draw-list/limit maths)
tests/test_sprite_stress.cpp         P2
tests/test_tilemap_stress.cpp        P3
tests/test_ui_stress.cpp             P4
tests/test_graph_fuzz.cpp            P5 — flow + story fuzz
tests/test_eventbus.cpp              P5
tests/test_scene_fuzz.cpp            P6
tests/test_camera2d_edge.cpp         P7
tests/test_light2d_edge.cpp          P7
tests/render/render_2d_limits.cpp    P1/P3/P7 — the T3 half (needs a GPU)
```

`docs/design/2d-hardening-notes.md` (P9) records the campaign's findings and the confirmed limits.

---

## 9. Non-goals

- **Not a performance-optimisation phase.** Measure and record, but do not tune. A limit that is
  slow-but-correct gets written down; a limit that is *wrong* gets fixed.
- **Not a feature phase.** 2D-native particles stay parked; ViperSim's 2D port stays parked. If a
  test needs a feature that does not exist, the test is out of scope — say so.
- **Not a 3D hardening phase.** Shared-tier tests will exercise 3D too, which is welcome, but no
  work order targets 3D-only code.
- **Not a CI change.** GitHub Actions keeps watching `main` with the 3D configuration only. If this
  phase makes a 2D CI leg look worthwhile, file it, do not build it.

---

## 10. Risk register

| # | Risk | Phase | Mitigation |
|---|---|---|---|
| 1 | **Limit tests that do not reach the limit.** A "10,001 quad" test that silently draws 10,001 quads in two batches without asserting the flush proves nothing. | P1 | Assert the *statistics* (`Renderer2D::GetStats().DrawCalls`), not just absence of a crash. `StatsEnabled` defaults **false** — arm it (Phase 29 W7 follow-up found this the hard way). |
| 2 | **Fuzz finds a real crash and the phase stalls fixing it.** | P5, P6 | Fix it — that is the point — but keep the fix and its non-vacuity proof in the same work order, and do not expand scope into adjacent code. |
| 3 | **Golden drift from GPU/driver differences** rather than a real regression. | P1, P3, P7 | T3 goldens already run with a ≤2/channel tolerance and a ≤0.1 % pixel budget. A *new* golden is captured once, eyeballed, then frozen. If one drifts, investigate — never regenerate to make it pass. |
| 4 | **A million-cell tilemap makes a headless test slow enough to hurt the suite.** | P3 | Budget it: the whole `CosmicTests` run should stay under a minute. If a case cannot, shrink the case or move it behind a doctest tag, and say which. |
| 5 | **The on-GPU campaign is unrepeatable** — findings live only in a chat log. | P8 | P8's DoD is a written record in `docs/design/2d-hardening-notes.md`, and every fixable finding gets a headless test. |
| 6 | **NaN tests pass by accident** because the comparison itself is false for NaN. | P2, P7 | Assert the *guard's* behaviour explicitly (`std::isfinite` on the output, or a documented clamp), never `CHECK(x == x)`. |

---

## 11. The driven-on-GPU campaign (P8) — recorded gotchas

These cost real time in Phase 29. Read them before driving anything.

**Access and launching**
- `request_access` needs the **exact exe basename** — `"Starforge.exe"`, not `"Starforge"`.
- The grant is keyed to the **full path**, so the 3D and 2D builds need **separate grants**.
- The app must already be running for the resolver to find it — it is not a Start-menu app.
- **`open_application` launches ANOTHER instance** rather than focusing an existing one. Use a
  `SetForegroundWindow` / `ShowWindow(3)` P/Invoke on `MainWindowHandle` instead.

**Interaction**
- Engine UI buttons need `left_mouse_down` → wait → `left_mouse_up`. An instantaneous click does
  **not** register `UiSystem`'s press/release edges, because they are sampled across frames.
- Homescreen project **cards** do not respond to clicks on the card body; the Pong / Flow Sample
  **buttons** do.
- Content-Browser folders and scenes **do** respond to an ordinary `double_click`.

**Observation**
- **Read `logs/Cosmic_*.log` instead of screenshotting the console.** Faster, complete, and
  greppable. This is how the 178-loads-per-second homescreen bug was quantified.
- Viewport stat chips read **this frame's** cost in 2D (Phase 29 armed `Renderer2D::StatsEnabled`)
  but **lifetime totals** in 3D, because Starforge never resets `Renderer3D`'s counters. Do not
  compare the two numbers.

**Known live issues to confirm or clear while driving** (from Phase 29's follow-up list)
- Starforge's homescreen re-parses `projects.toml` **every frame** (~178/s, 1.4 MB of log per 95 s)
  at the `Prefs::LoadProjects()` call inside the homescreen draw. Fix is a cached list invalidated
  on open/create/remove/pin. **Confirm it still reproduces; it is a P9 candidate.**
- Script-driven gameplay (Pong paddles, nav critters) needs Ctrl+B to build the game module first;
  until then the console logs `unknown script class … entity kept inert`, which is expected, not a
  bug.

---

## 12. Per-phase work orders

Each section is self-contained. The **📋 PROMPT** block is what you paste into a fresh session.

---

### P0 — Carry the branch, re-baseline, inventory the limits

**Goal.** Get `engine-2d` current, confirm the starting numbers, and produce the written inventory
of every documented 2D limit that the rest of the phase tests against.

**Preconditions.** Phase 29 complete. `main` = `3066e6a` or later.

**Deliverables.**
1. `engine-2d` carrying `451b926`, `e5d7d29`, `3066e6a` (§13 — the user pushes; ask before any git
   write).
2. Both suites re-run and recorded.
3. `docs/design/2d-hardening-notes.md` created with a **limits inventory**: every constant in
   `Renderer2D.cpp`, `TilemapComponent::kMaxGrid`, any UI/canvas bound, any camera clamp — each
   with its file, its value, and whether a test currently reaches it (almost all: no).

**DoD.** Both configurations green from a clean configure; the inventory table is complete and
every row cites a real file; nothing is guessed.

**📋 PROMPT**

```
Execute work order P0 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic.
Read §0, §2, §5 and §12/P0.

1. engine-2d is 3 commits behind main (451b926, e5d7d29, 3066e6a). Carry them across per §13 of
   docs/plans/28-phase29-engine-split-plan.md. ASK ME before running any git write command —
   Phase 29's local-commit exception does NOT carry over to this phase.
2. Build and run both configurations with the §6 recipe. Record the exact test counts.
3. Create docs/design/2d-hardening-notes.md with a "Limits inventory" table: every hard limit in
   the 2D stack, its file:symbol, its value, and whether any existing test reaches it. Read the
   sources — do not copy the values out of this plan, verify them.

You may look at my computer and drive the built apps if that helps you confirm anything.
Report the numbers and the inventory. Leave edits in the working tree; I will commit.
```

---

### P1 — `Renderer2D` batch boundaries and every documented limit

**Goal.** Cross every documented `Renderer2D` limit and prove the renderer does the right thing at
and past the boundary.

**Anchors.** `Cosmic/src/renderer/Renderer2D.cpp` — the constants block (§2). `Renderer2D::GetStats()`
/ `ResetStats()` / `SetStatsStatus()`.

**Cases (T3 unless noted).**
- Exactly `MaxQuads` quads ⇒ **one** draw call. `MaxQuads + 1` ⇒ **two**, and the image is correct
  across the seam (no dropped or duplicated quad).
- 32 distinct textures ⇒ one batch; **33** ⇒ two, and every sprite still samples its own texture.
- `MaxLines`, `MaxCircles`, `MaxTextQuads`, `MaxInstancedQuads` — the same ±1 pattern for each.
- A single `DrawQuad` after a flush lands in a fresh batch with correct state.
- Text overflow: a string long enough to cross `MaxTextQuads` mid-word.
- **T1 half:** whatever of the above is expressible without a GPU — index-buffer arithmetic,
  slot-assignment logic — extracted and tested headlessly. Prefer T1 wherever it is honest.

**Gotchas.**
- **`Renderer2D::StatsEnabled` defaults to `false`.** Arm it with `SetStatsStatus(true)` and call
  `ResetStats()` per frame, or every counter reads zero and every assertion passes vacuously. This
  exact trap shipped a broken stats chip in Phase 29 W7.
- Golden images are 320×180. A 10,001-quad frame needs a scene whose *result* is checkable at that
  size — e.g. a dense grid where a missing quad is a visible hole. Design the scene for the
  assertion, not for looks.
- Do not name a single `gl*` token or `GL_*` enum in `tests/` — `check_gl_conformance.ps1` scans it.

**DoD.** Every constant in §2 has a boundary test; each asserts the draw-call count, not just
absence of a crash; new goldens eyeballed once and frozen; both configurations green.

**📋 PROMPT**

```
Execute work order P1 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D
(branch engine-2d). Read §0, §3, §5, §7 and §12/P1.

Test every documented Renderer2D limit at the boundary: MaxQuads, MaxTextureSlots, MaxLines,
MaxCircles, MaxTextQuads, MaxInstancedQuads. For each: exactly-the-limit and limit+1, asserting
the DRAW CALL COUNT from Renderer2D::GetStats() as well as image correctness.

CRITICAL: Renderer2D::StatsEnabled defaults to FALSE. Call SetStatsStatus(true) and ResetStats()
or every counter reads zero and every assertion passes vacuously.

Put whatever is honestly expressible without a GPU in tests/test_renderer2d_limits.cpp (shared
tier, hand-add it to tests/CMakeLists.txt). Put the rest in tests/render/render_2d_limits.cpp
behind COSMIC_BUILD_RENDER_TESTS. Design golden scenes so a dropped quad is a VISIBLE hole at
320x180. No gl* tokens anywhere in tests/.

You may look at my computer, launch the render tests and the editors, and debug interactively.
Verify with the §6 recipe in BOTH configurations. Leave edits in the working tree; I will commit.
```

---

### P2 — Sprite pipeline at scale and under degenerate transforms

**Goal.** Push `Scene::BuildSpriteDrawList` and the sprite pass past realistic content sizes and
feed them values a real scene can actually contain.

**Anchors.** `Scene::BuildSpriteDrawList()` (`scene/Scene.h`, public since Phase 29 W2 — a pure
function, so this is all T1), `SpriteRendererComponent`, `SpriteAnimationComponent`.

**Cases.**
- 10k / 50k sprites: the draw list builds, the order is correct, and the time is recorded.
- **Sort stability at equal Z** — build twice, assert identical output; then permute insertion order
  and assert the documented tie-break holds.
- NaN / ±inf in `Position`, `Scale`, `Rotation`, `Color`: does the entry survive, get skipped, or
  poison the sort? **Whatever it does, pin it** — an unstable comparator on NaN is undefined
  behaviour in `std::sort` and can corrupt memory, so this case is load-bearing.
- Zero and negative scale (winding flip), 1e9 positions, 1e-9 scale.
- Sprite animation: zero-frame clip, one-frame clip, negative frame time, frame index past the end.

**DoD.** As above; the NaN behaviour is explicitly documented in the test file *and* in the P9
notes; both configurations green.

**📋 PROMPT**

```
Execute work order P2 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P2. New file tests/test_sprite_stress.cpp, shared tier.

Scale: 10k and 50k sprites through Scene::BuildSpriteDrawList — correct order, recorded time.
Stability: identical output across two builds; documented tie-break at equal Z.
Degenerate: NaN/inf in Position/Scale/Rotation/Color, zero and negative scale, 1e9 and 1e-9
magnitudes. Sprite animation: zero-frame, one-frame, negative frame time, out-of-range index.

The NaN case matters most: an unstable comparator on NaN is UB in std::sort and can corrupt
memory. Find out what actually happens, then pin it. If it IS broken, fix it and prove the test
non-vacuous by reverting the fix and recording the failure.

Keep the whole CosmicTests run under a minute. You may drive the app on my machine to
investigate anything. Verify both configurations per §6. Leave edits in the working tree.
```

---

### P3 — Tilemap stress: the million-cell map

**Goal.** A `kMaxGrid` × `kMaxGrid` tilemap is 1,048,576 cells against a 10,000-quad batch. Prove
the culled walk holds up.

**Anchors.** `TilemapComponent` (`scene/Components.h`) — `kMaxGrid`, `EnsureCells`, `InBounds`,
`At`. The culled cell walk exercised by `test_tilemap_extra`.

**Cases.**
- Full 1024×1024 map: memory, build time, and the culled draw count at several zoom levels.
- Camera fully outside the map ⇒ **zero** cells drawn.
- Camera covering the whole map at extreme zoom-out ⇒ the walk is bounded, not 1M draws.
- `GridW`/`GridH` of 0, −1, `kMaxGrid + 1` ⇒ `EnsureCells` clamps (assert the clamp, both ends).
- `Cells` shorter than `GridW * GridH` (a hand-edited or truncated scene file) ⇒ `At` must not read
  out of bounds. **This is a real crash vector: the serializer can produce it.**
- Tile indices past the atlas range; `Columns = 0` auto-derive with a null/absent texture.
- **T3:** one golden of a large map at a partial-cover camera, to catch off-by-one at the cull edge.

**DoD.** As above, plus a recorded per-cell cost so P9 can state the practical map-size limit.

**📋 PROMPT**

```
Execute work order P3 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P3. New file tests/test_tilemap_stress.cpp, shared tier; one new
golden in tests/render/render_2d_limits.cpp.

TilemapComponent::kMaxGrid is 1024, so a max map is 1,048,576 cells against a 10,000-quad batch.
Test: the full map (memory, build time, culled draw count at several zooms); camera fully
outside => zero cells; extreme zoom-out => a BOUNDED walk, not 1M draws; EnsureCells clamping at
0, -1 and kMaxGrid+1; and a Cells vector SHORTER than GridW*GridH, which the serializer can
produce and which At() must survive without reading out of bounds.

Record the per-cell cost so we can state a practical map-size limit. Keep the suite under a
minute — if the full map is too slow for CI, say so and shrink it rather than hiding it.

You may drive the editor on my machine (paint a big map, watch the stats chip) to sanity-check
the numbers. Verify both configurations per §6. Leave edits in the working tree.
```

---

### P4 — UI system: depth, cycles, degenerate rects, hit-test at scale

**Goal.** `UiSystem`'s layout and hit-test are pure and already partly tested; push them into the
shapes an author can actually create by accident.

**Anchors.** `scene/ui/UiSystem.h` — `ResolveRect`, `PivotPoint`, `CanvasScale`, `CollectElements`,
`Update`, `HitTest`, `StepButtonState`, `ProjectToCanvas`. `scene/ui/UiComponents.h`.

**Cases.**
- Nesting depth 100, 1000 — bounded time, no stack overflow. **`CollectElements` recursing on a
  hand-built hierarchy is the stack-overflow candidate; test it deliberately.**
- **A parent cycle** (A→B→A). This is creatable through the serializer even if the editor prevents
  it. Must terminate.
- Inverted rects (min > max), zero-size rects, zero-size viewport, zero-size canvas.
- `CanvasScale` with a zero or absurd reference resolution.
- `ProjectToCanvas` with a point behind the camera, at infinity, or with a singular view-projection.
- `HitTest` with 5k elements — bounded time; and the overlap rule (topmost wins) asserted.
- `StepButtonState` — every transition of the state machine, including pointer-leaves-while-armed.

**DoD.** As above; anything that does not terminate or overflows is fixed with a non-vacuity proof.

**📋 PROMPT**

```
Execute work order P4 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P4. New file tests/test_ui_stress.cpp, shared tier.

Target scene/ui/UiSystem.h: ResolveRect, PivotPoint, CanvasScale, CollectElements, Update,
HitTest, StepButtonState, ProjectToCanvas.

Cases: nesting depth 100 and 1000 (CollectElements is the stack-overflow candidate — test it
deliberately); a PARENT CYCLE A->B->A, which the serializer can produce even if the editor
prevents it, and which must terminate; inverted and zero-size rects; zero-size viewport and
canvas; CanvasScale with a zero reference resolution; ProjectToCanvas behind the camera and with
a singular view-projection; HitTest with 5k elements plus the topmost-wins rule; and every
StepButtonState transition including pointer-leaves-while-armed.

Anything that hangs or overflows is a real bug: fix it, then prove the test non-vacuous by
reverting the fix and recording what you saw.

You may drive the editor on my machine to reproduce anything interactively. Verify both
configurations per §6. Leave edits in the working tree.
```

---

### P5 — Flow / Story graph fuzz and an `EventBus` suite

**Goal.** The `.cflow` / `.cstory` runtimes drive whole games with no test against malformed or
malicious graphs; `EventBus` has no dedicated suite at all.

**Anchors.** `scene/FlowMachine.{h,cpp}`, `scene/StoryGraph.{h,cpp}`, `scene/EventBus.h`
(`Connect` / `Disconnect` / `Emit` / `Clear` / `IsNamedLive` / `IsAnyLive`, `Handle` = `uint64_t`,
0 invalid).

**Cases — graphs (T1 + T2).**
- Cyclic flow (A→B→A) driven for many steps: terminates, does not stack-overflow.
- A transition targeting a node that does not exist; a start node that does not exist; an empty
  graph; a graph with one node and no transitions.
- Guards referencing an undefined variable, a wrong-typed variable, a deleted entity.
- Story: an option whose target is missing, `Once` options re-entered, a node with zero options
  reached in a UI-bound context.
- **Seeded fuzz** over the JSON: byte flips, truncation, type swaps, absurd counts — ~200 iterations,
  fixed seed. Load must fail cleanly or succeed coherently; **never crash, never hang.**

**Cases — `EventBus` (T1).**
- `Disconnect` during `Emit` (handler removes itself; handler removes a *sibling*).
- `Emit` during `Emit` (re-entrancy), including a handler that emits the signal it handles.
- `Clear` while handlers are live.
- Handle reuse after disconnect — a stale handle must not disconnect a *new* subscriber.
  `IsNamedLive` / `IsAnyLive` are there to make this assertable; use them.
- 10k subscribers on one signal.

**DoD.** Fuzz is seeded and bit-identical across five reruns; every graph pathology terminates.

**📋 PROMPT**

```
Execute work order P5 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P5. New files tests/test_graph_fuzz.cpp and tests/test_eventbus.cpp,
both shared tier.

Graphs: cyclic flow driven many steps (must terminate, no stack overflow), missing transition
targets, missing start node, empty graph, guards on undefined/wrong-typed/deleted references,
story options with missing targets, Once re-entry, zero-option nodes. Then a SEEDED ~200-iteration
fuzz over the .cflow/.cstory JSON — byte flips, truncation, type swaps, absurd counts. Loading
must fail cleanly or succeed coherently; never crash, never hang.

EventBus has no suite at all. Cover: Disconnect during Emit (self and sibling), Emit during Emit
including self-emitting handlers, Clear while live, STALE HANDLE REUSE (a disconnected handle
must never disconnect a new subscriber — IsNamedLive/IsAnyLive exist to make this assertable),
and 10k subscribers on one signal.

Fixed seeds only, no random_device. Rerun the fuzz 5 times and confirm bit-identical results.
You may drive the editor on my machine to reproduce anything. Verify both configurations per §6.
Leave edits in the working tree.
```

---

### P6 — Scene / prefab / material JSON fuzz

**Goal.** W9 fuzzed telemetry binaries and found two process-killing bugs. Scene JSON has never
been fuzzed, and it is the format every project loads on every boot.

**Anchors.** `scene/SceneSerializer.{h,cpp}`, `OpaqueComponentsComponent`, `graphics/MaterialAsset.h`.

**Cases.**
- Empty file, whitespace-only, not-JSON, JSON that is not an object, missing version key, unknown
  version.
- Structurally valid but absurd: 1e6 entities, an entity with 1e6 components, deeply nested
  hierarchy, a 100 MB string field.
- Wrong types in every reflected field kind (string where a float is expected, object where an
  array is expected, `null` everywhere).
- **Hierarchy pathologies:** an entity parented to itself, a two-node cycle, a `Children` list
  naming a UUID that does not exist, duplicate UUIDs.
- **The `OpaqueComponentsComponent` path** (this is the cross-build guarantee, so it must be
  bulletproof): an opaque block whose text is not valid JSON; an opaque block that collides with a
  now-registered type name; round-tripping a file with both real and opaque blocks.
- Seeded byte-flip fuzz over a real saved scene, ~200 iterations.

**Gotchas.**
- `SceneSerializer::SaveToString` uses `dump(2)` — **pretty, not compact.** Needles like
  `"Flag":true` will not appear. W9's `Squeeze()` helper (strip whitespace outside string literals)
  is the precedent.
- `nlohmann::json` **sorts object keys on parse**, so round-trip comparisons must be dump-vs-dump
  (`pass1 == pass2`), not against the original file bytes.
- Reflected field names are `Tag.Tag` and `Transform.Position` — not `Name` / `Translation`.

**DoD.** No input crashes, hangs, or allocates unboundedly; every failure is a clean `false` plus a
log line; fuzz is seeded and reproducible.

**📋 PROMPT**

```
Execute work order P6 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P6. New file tests/test_scene_fuzz.cpp, shared tier.

Fuzz scene/prefab/material JSON — the format every project loads on every boot, and the one W9
did not get to. Cover: malformed files; structurally-valid-but-absurd (1e6 entities, 1e6
components, deep nesting, a 100 MB string); wrong types in every reflected field kind; hierarchy
pathologies (self-parent, cycles, dangling child UUIDs, duplicate UUIDs); and the
OpaqueComponentsComponent path, which is the Phase 29 cross-build guarantee and must be
bulletproof — invalid opaque text, an opaque name colliding with a now-registered type, and
mixed real/opaque round-trips. Finish with a seeded ~200-iteration byte-flip fuzz over a real
saved scene.

Gotchas: SaveToString uses dump(2) (pretty, not compact) — use a whitespace-squeeze helper.
nlohmann sorts keys on parse, so compare dump-vs-dump, never against original bytes. Reflected
names are Tag.Tag and Transform.Position.

Anything that crashes, hangs, or allocates unboundedly is a real bug: fix it, then revert the fix
and record the observed failure to prove the test non-vacuous — exactly as W9 did for DataPlayer.

You may drive the editor on my machine to confirm a bad file's real-world effect. Verify both
configurations per §6. Leave edits in the working tree.
```

---

### P7 — Camera2D and Light2D edge cases

**Goal.** Two small, high-traffic subsystems with thin coverage (4 and 8 cases respectively).

**Anchors.** `camera/Camera2DController.h` — the static pure helpers `ScreenToWorld`, `PanBy`,
`ZoomAboutPoint`, plus `VisibleRect` and `FrameBounds`. `renderer/Light2DRenderer.{h,cpp}` and
`Light2DComponent`.

**Cases — camera (T1; the statics make this easy).**
- `ScreenToWorld` ∘ world-to-screen round-trip at several zooms and viewport rects.
- `ZoomAboutPoint`: the world anchor stays fixed under the transform — that is the whole contract.
- Zoom 1e-6 and 1e6; zero and negative zoom; zero-size viewport; aspect 0 and inf.
- `FrameBounds` on an empty (min > max) and a zero-size bounds.
- `PanBy` with a zero viewport height (division guard).

**Cases — 2D lights (T1 + T3).**
- Zero radius, negative radius, zero intensity, black colour.
- 100 lights; lights entirely off-screen; a light exactly on the viewport edge.
- **Odd viewport sizes against the half-res buffer** — 1×1, 3×3, 1919×1079. The half-res target is
  a rounding hazard, and Phase 29 W2 already found one real first-frame bug in this file.
- The X5 invariant re-asserted: no lights + white ambient is **byte-identical** to the pass not
  running.

**DoD.** As above; every guard's behaviour is asserted rather than assumed.

**📋 PROMPT**

```
Execute work order P7 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic-2D.
Read §0, §5, §7 and §12/P7. New files tests/test_camera2d_edge.cpp and
tests/test_light2d_edge.cpp, shared tier; extend tests/render/render_2d_limits.cpp as needed.

Camera2DController's static helpers (ScreenToWorld, PanBy, ZoomAboutPoint) are pure — test them
hard: round-trips at several zooms and viewport rects; ZoomAboutPoint keeping its world anchor
fixed (that is the entire contract); zoom 1e-6 and 1e6; zero/negative zoom; zero-size viewport;
aspect 0 and inf; FrameBounds on empty and zero-size bounds; PanBy with zero viewport height.

Light2D: zero/negative radius, zero intensity, 100 lights, fully off-screen lights, a light on
the viewport edge, and ODD VIEWPORT SIZES against the half-res buffer (1x1, 3x3, 1919x1079) —
that buffer is a rounding hazard and Phase 29 W2 already found a real first-frame bug in this
file. Re-assert the X5 invariant: no lights + white ambient is byte-identical to skipping the pass.

You may drive the editor on my machine and resize the viewport interactively to hunt rounding
bugs. Verify both configurations per §6. Leave edits in the working tree.
```

---

### P8 — The driven-on-GPU campaign

**Goal.** Find what a headless suite structurally cannot: interaction bugs, ImGui-only paths,
long-run drift, and anything that only appears with a real driver.

**This work order is executed by driving the applications on the user's machine.** §5's
authorization and §11's gotchas are the operating manual.

**Scope.**
1. **Both editors, side by side.** 2D from `C:\dev\Cosmic-2D`, 3D from `C:\dev\Cosmic`. Confirm the
   Phase 29 gating still holds: 2D has no 3D panels/menus/view modes, the collider overlay draws
   *over* sprites, the stats chip reads this frame.
2. **Author something real in the 2D editor** — a scene with sprites, a painted tilemap, 2D lights,
   canvas UI, a flow transition — save, reload, and Play it. This is the acceptance path no test
   covers.
3. **Every viewport-strip chip and every menu item**, in both Debug and Release. Phase 29 found an
   `abort()` on *every* toggle chip that had been live for months; that class of bug is invisible
   headlessly.
4. **Soak.** Leave ForgePong (or FlowDemo) running for a long session. Watch: memory growth, handle
   growth, frame-time drift, log-file growth, and counter wraparound.
5. **Confirm or clear the known live issues** in §11 — especially the homescreen's per-frame
   `projects.toml` re-parse.
6. **Resize, minimise, restore, alt-tab, DPI change, fullscreen toggle** during Play. The
   responsive-rendering path (`docs/design/responsive-rendering-and-pause.md`) has its own contract
   and this is where it gets exercised.

**DoD.** Every finding recorded in `docs/design/2d-hardening-notes.md` with a reproduction; every
fixable finding has a headless test added wherever one is possible; nothing found is left
undocumented.

**📋 PROMPT**

```
Execute work order P8 from docs/plans/29-phase30-2d-hardening-plan.md.
Read §0, §5, §11 and §12/P8 in full. This work order is DRIVEN ON MY COMPUTER.

You have my explicit authorization to take screenshots of my desktop, launch the built
executables, click/type/scroll to drive both editors, read the engine log files, and debug
interactively — reproduce, hypothesize, edit, rebuild, re-drive. §11 lists the gotchas that will
otherwise cost you an hour each: exact exe basename for request_access, per-full-path grants,
open_application launching a duplicate instead of focusing, and engine UI buttons needing
mouse_down -> wait -> mouse_up rather than an instant click. Read logs/Cosmic_*.log rather than
screenshotting the console.

Cover: (1) both editors side by side, confirming the Phase 29 gating still holds; (2) author a
real 2D scene — sprites, painted tilemap, 2D lights, canvas UI, a flow transition — save, reload,
Play; (3) EVERY viewport-strip chip and menu item in BOTH Debug and Release (Phase 29 found an
abort() on every toggle chip that had been live for months); (4) a long soak on ForgePong watching
memory, handles, frame-time drift and log growth; (5) confirm or clear the known live issues in
§11, especially the homescreen re-parsing projects.toml every frame; (6) resize/minimise/restore/
alt-tab/DPI-change/fullscreen during Play.

Record EVERY finding in docs/design/2d-hardening-notes.md with a reproduction, and add a headless
test for each one that can have one. Leave edits in the working tree; I will commit.
```

---

### P9 — Fix pass, limits documentation, phase report

**Goal.** Close the loop: fix what was found, write down what the 2D engine's real limits are, and
report honestly.

**Deliverables.**
1. Every P1–P8 finding either **fixed with a non-vacuity proof** or **recorded as a known limit**
   with a reason. Nothing silently dropped.
2. `docs/design/2d-hardening-notes.md` finished: the limits inventory from P0 updated with measured
   reality, the findings log, and a short "what the 2D engine is good for" section giving practical
   ceilings (sprites per frame, map size, UI elements, lights).
3. Documentation hooks: if a limit or behaviour is now pinned, it belongs in
   `docs/reference/rendering-2d.md` and/or `docs/systems/rendering-2d.md` — coordinate with
   whatever docs work orders have landed by then rather than duplicating.
4. `docs/plans/FEATURE-MATRIX.md` rows for anything found-but-not-fixed.
5. This document gains status lines and a deviation section, the way doc 28 §16 did.

**DoD.** No finding is undocumented; both configurations green; the phase report states what was
*not* covered as clearly as what was.

**📋 PROMPT**

```
Execute work order P9 from docs/plans/29-phase30-2d-hardening-plan.md in C:\dev\Cosmic.
Read §0 and §12/P9, plus everything recorded in docs/design/2d-hardening-notes.md.

Close the phase: fix every outstanding finding (each with a non-vacuity proof — revert the fix,
watch the test fail, record it) or record it as a known limit with a reason. Finish
2d-hardening-notes.md with the measured limits and a practical-ceilings section. Add FEATURE-MATRIX
rows for anything found-but-not-fixed. Add status lines and an HONEST deviation section to this
plan doc, the way doc 28 §16 did — everywhere the campaign diverged from this plan and why.

State what was NOT covered as clearly as what was. You may drive the app on my machine to confirm
fixes. Verify both configurations per §6. Leave edits in the working tree; I will commit.
```

---

## 13. Git

**The default rule applies: the assistant runs no git write commands.** Phase 29's local-commit
exception was granted for that phase specifically and does not extend here. Work orders leave
edits in the working tree with a summary; the user commits and pushes.

**P0 is the exception-shaped case** — carrying three commits from `main` to `engine-2d` is a git
operation by nature. P0's prompt tells the assistant to **ask first**.

If the user prefers to re-grant the Phase 29 arrangement (assistant commits locally, authored as
`kdadabhoy`, no `Co-Authored-By: Claude` trailer, never pushes), say so and this section is amended
in place — do not assume it.

---

## 14. Acceptance matrix

| Gate | Target |
|---|---|
| Both configurations configure + build Debug **and** Release, zero warnings | every work order |
| `CosmicTests` | all pass in both; total runtime stays under ~1 minute |
| `check_gl_conformance.ps1` | clean in both |
| `CosmicRenderTests` | all cases pass; pre-existing goldens still byte-match |
| Every `Renderer2D` limit in §2 | has a boundary test asserting the draw-call count |
| Fuzz suites | seeded; five reruns bit-identical |
| Every bug fixed this phase | has a recorded non-vacuity proof |
| On-GPU campaign | recorded in `docs/design/2d-hardening-notes.md` with reproductions |
| Practical ceilings | stated: sprites/frame, map size, UI elements, lights |

---

*Changelog:*
*2026-07-25 — created; P0–P9 planned, nothing implemented.*
