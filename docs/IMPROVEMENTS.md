# Cosmic Engine — Improvement Proposals

> **Scope:** A prioritized, actionable engineering roadmap for the Cosmic engine core (`Cosmic/src/`).
> **Verified against:** commit `9078662`, 2026-06-24. Every claim below was checked against the
> current source; file/line references are live as of this commit.
> **Companion doc:** [`engine_analysis.md`](engine_analysis.md) is the longer reference analysis.
> This document is the *short list of things I would change next*, grouped by the value they deliver
> and ordered so the cheap, high-impact fixes come first.

---

## How to read this

Each item has a **cost** (rough implementation effort) and an **impact**. The summary table is the
TL;DR; the sections below give the rationale and the concrete change. Nothing here requires an
architectural rewrite — the engine is in good shape. These are sharpening passes.

| # | Improvement | Category | Cost | Impact |
|---|-------------|----------|------|--------|
| 1 | Fix `Stats` line/index counters | Correctness | XS | Medium |
| 2 | Delete `Renderer::s_SceneData` on shutdown | Correctness | XS | Medium |
| 3 | Handle 1/2-channel textures (and failed loads) | Correctness | S | High |
| 4 | Reconcile `m_PauseOnMinimize` doc vs. default | Correctness | XS | Low |
| 5 | Hoist the instanced-quad sampler upload out of the draw loop | Performance | S | Medium |
| 6 | Persistent scratch buffers in `Scene::OnRender` | Performance | S | Medium |
| 7 | O(1) texture-slot lookup | Performance | S | Medium |
| 8 | Collapse the six copies of texture-slot resolution | Maintainability | M | Medium |
| 9 | Unify or clearly demote the legacy `Renderer::Submit` path | Architecture | M | Medium |
| 10 | Normalize the `RendererAPI::Draw*` bind contract | Architecture | S | Low |
| 11 | Guard `ComponentArray<T>` multi-page UB in Release | Safety | S | High |
| 12 | Verify / wrap the `ImPlotSpec` plotting call | Portability | S | Medium |
| 13 | Asset cache (textures, shaders, fonts) | Feature | M | High |
| 14 | Sprite-animation component | Feature | M | Medium |
| 15 | Hot-reload shader file watching | Feature | M | Medium |

---

## 1. Correctness fixes

These produce wrong results *silently* — no crash, no log — which makes them the highest-leverage
fixes despite being tiny.

### 1.1 Renderer statistics under-report (XS)

`Renderer2D::DrawLine` advances the vertex count but never touches the stat counter, so
`Stats.LineCount` is always `0` and `GetTotalVertexCount()` drops the line contribution entirely.

- [`Renderer2D.cpp:1280`](../Cosmic/src/renderer/Renderer2D.cpp:1280) — add `if (s_Data.StatsEnabled) s_Data.Stats.LineCount++;` after `LineVertexCount += 2`.

Separately, `GetTotalIndexCount()` only counts quads, even though circles also emit 6 indices each:

- [`Renderer2D.h:178`](../Cosmic/src/renderer/Renderer2D.h:178) — change `QuadCount * 6` to `(QuadCount + CircleCount) * 6`.

**Why it matters:** the stats overlay is the only built-in profiling surface. If it lies, every
optimization decision made from it is made on bad data.

### 1.2 `Renderer::s_SceneData` leaks every run (XS)

[`Renderer.cpp:10`](../Cosmic/src/renderer/Renderer.cpp:10) heap-allocates `s_SceneData` but
[`Renderer::Shutdown`](../Cosmic/src/renderer/Renderer.cpp:29) never frees it.

**Fix:** make it a value member (`static Renderer::SceneData s_SceneData;`) so there is no allocation
to leak, or `delete`/null it in `Shutdown()`. The value-member form is strictly better — it also
removes a layer of pointer indirection from `Submit`.

### 1.3 Textures with 1 or 2 channels upload an invalid GL format (S)

[`OpenGLTexture.cpp:70-80`](../Cosmic/src/platform/OpenGL/OpenGLTexture.cpp:70) only assigns a format
for 3- and 4-channel images. A grayscale (1ch) or grayscale+alpha (2ch) PNG leaves
`internalFormat`/`dataFormat` at `0`, so `glTexImage2D` is called with an invalid enum, the upload
fails silently, and the texture renders black.

**Fix:** add `GL_R8`/`GL_RED` and `GL_RG8`/`GL_RG` branches, and an `else` that logs and bails.
While here, the constructor's `if (data)` block has no `else` — a failed `stbi_load` produces a
texture object with `m_RendererID == 0` and no diagnostic. Add a `CS_CORE_ERROR` on the failure path
so a missing/corrupt file is visible instead of mysteriously invisible.

### 1.4 `PauseOnMinimize` default contradicts its own documentation (XS)

[`Application.h:113`](../Cosmic/src/core/Application.h:113) documents *"When true (default), all update
and render passes are skipped while the window is minimized"*, but the member is initialized to
`false` at [`Application.h:145`](../Cosmic/src/core/Application.h:145). The `Run()` loop honors the
field, so the engine actually keeps ticking while minimized — the opposite of the comment.

**Fix:** pick one. For a simulation/telemetry engine, keeping `false` (keep running) is the right
behavior; just correct the comment. If pausing is intended, flip the default. Either way the two
should agree.

---

## 2. Performance

The engine already does the hard part well (CPU-side batching, instancing, a real job system).
These are the next bottlenecks in order of how often they execute.

### 2.1 Instanced-quad sampler array is re-uploaded every draw (S)

[`DrawInstancedQuads`](../Cosmic/src/renderer/Renderer2D.cpp:1428) rebuilds and re-uploads the 32-entry
`u_Textures` sampler array on **every** call:

```cpp
int32_t samplers[MaxTextureSlots];
for (...) samplers[i] = i;
targetShader->SetIntArray("u_Textures", samplers, MaxTextureSlots);
```

The batch-quad pipeline does this exactly once in `Init()`. At, say, 500 instanced calls/frame that's
16,000 wasted uniform uploads/frame.

**Fix:** upload the sampler array once after `QuadInstance.glsl` loads in
[`Renderer2D::Init`](../Cosmic/src/renderer/Renderer2D.cpp:416), mirroring the batch path.

### 2.2 `Scene::OnRender` allocates its sort buckets every frame (S)

[`Scene::OnRender`](../Cosmic/src/scene/Scene.cpp:104) constructs a fresh
`unordered_map<Material*, vector<entity>>` plus a fallback `vector` every frame, then lets them free
at end of scope. That is a map + N vectors of churn per frame, every frame.

**Fix:** promote the buckets to members that are `.clear()`ed (not destroyed) each frame so their
backing storage is reused. `clear()` keeps capacity; the steady-state allocation count drops to zero.

### 2.3 Texture-slot lookup is a linear scan per quad (S)

Every textured `DrawQuad`/`DrawRotatedQuad` linearly scans up to 32 slots comparing renderer IDs
(e.g. [`Renderer2D.cpp:809`](../Cosmic/src/renderer/Renderer2D.cpp:809)). Bounded, but it runs once
per quad and degrades as the slot count approaches 32.

**Fix:** add `std::unordered_map<uint32_t, uint32_t>` (renderer-ID → slot) to `Renderer2DData`,
cleared on each `FlushAndReset`. This pairs naturally with item 3 below — both want a single
`ResolveTextureSlot()` helper.

---

## 3. Architecture & maintainability

### 3.1 Six near-identical copies of texture-slot resolution (M)

The block "search slots → if missing, flush-if-full → register" is duplicated verbatim across the
texture, material, and subtexture variants of both `DrawQuad` and `DrawRotatedQuad`
(~[`808`](../Cosmic/src/renderer/Renderer2D.cpp:808),
[`864`](../Cosmic/src/renderer/Renderer2D.cpp:864),
[`912`](../Cosmic/src/renderer/Renderer2D.cpp:912),
[`990`](../Cosmic/src/renderer/Renderer2D.cpp:990),
[`1047`](../Cosmic/src/renderer/Renderer2D.cpp:1047),
[`1091`](../Cosmic/src/renderer/Renderer2D.cpp:1091)). That's ~120 lines that must all change together.

**Fix:** a single private `float ResolveTextureSlot(const Ref<Texture>&)` that does the lookup,
flush-on-full, and registration, returning the slot index. Every draw variant collapses to one call.
This is also the cleanest place to land the O(1) map from item 2.3 — fix it once, fix it everywhere.

### 3.2 The legacy `Renderer::Submit` path is a second, desynced source of truth (M)

`Renderer` keeps its own `s_SceneData->ViewProjectionMatrix`, set by `Renderer::BeginScene`, entirely
independent of `Renderer2D`'s VP matrix. A project that mixes `Renderer::Submit` with
`Renderer2D::DrawQuad` in one frame silently draws under two different cameras, and every `Submit` is
an un-batched single draw call.

**Fix:** decide its fate explicitly. Either (a) demote it in code and docs to "low-level custom-shader
escape hatch only," with a `CS_CORE_WARN` in `Renderer::BeginScene` noting it does not sync with
`Renderer2D`; or (b) have `Renderer::BeginScene` forward its camera into
`Renderer2D::PushRenderPass` so the two can't diverge. (a) is less work and matches how it's actually
used today.

### 3.3 `RendererAPI::Draw*` bind contract is inconsistent (S)

[`OpenGLRendererAPI::DrawLines`](../Cosmic/src/platform/OpenGL/OpenGLRendererAPI.cpp:84) binds the VAO
internally; `DrawIndexed` and `DrawIndexedInstanced` do not. `Renderer2D::Flush` already binds before
calling `DrawLines`, so the VAO is bound twice, and a reader can't tell from the interface who owns the
bind.

**Fix:** remove the internal `Bind()` from `DrawLines` (the caller already binds) and document in
`RendererAPI.h` that binding is the caller's responsibility for all `Draw*` entry points.

---

## 4. Safety & portability

### 4.1 `ComponentArray<T>` silently corrupts past one EnTT page (S, High impact)

`ComponentArray<T>::From` returns a pointer into EnTT's first storage page while `Count()` reports the
total across all pages. Past ~1024 entities, indexing reads out of bounds. A `CS_CORE_ASSERT` guards
it in Debug, but asserts compile out in Release, so a project that starts small and grows crosses the
threshold into silent memory corruption with no warning.

**Fix:** make the multi-page check a hard runtime guard in *both* configs (return empty + log), or
retire `ComponentArray<T>` in favor of the already-present, always-correct `FlatComponentArray<T>`.
The zero-copy win is not worth a Release-only corruption footgun.

### 4.2 `ImPlotSpec` may not exist in stock ImPlot (S)

[`TelemetryPanel.cpp:411`](../Cosmic/src/telemetry/TelemetryPanel.cpp:411) plots via an `ImPlotSpec`
struct with an `Offset` field. That is not part of the upstream ImPlot API, whose `PlotLine` takes
`flags`/`offset`/`stride` as plain parameters. If the vendored ImPlot is ever updated or swapped, this
fails to compile.

**Fix:** if `ImPlotSpec` is a local fork extension, note that explicitly at the call site and in the
build docs; otherwise switch to the standard `PlotLine(label, xs, ys, count, flags, offset)` overload
and pass `m_PlotOffset` as the `offset` argument.

---

## 5. Feature roadmap

Ordered by leverage for the engine's actual use case (interactive 2D simulations / telemetry tooling).

### 5.1 Asset cache for textures/shaders/fonts (M, High)

Today `Texture2D::Create("foo.png")` reloads and re-uploads the file every call, and there is no
shared ownership across layers. A small `AssetLibrary` keyed by path that hands back the existing
`Ref<>` would cut load time, VRAM, and the duplicate-texture-slot pressure that motivates items
2.3/3.1. This is the single feature most other features benefit from.

### 5.2 Sprite-animation component (M)

There is `SubTexture2D` for atlas slicing but no time-driven frame advance. A
`SpriteAnimationComponent { frames, fps, loop }` plus a tiny system that advances the active
`SubTexture2D` on `OnUpdate(dt)` would make animated sprites a data-only feature instead of per-project
boilerplate. The `Timestep`/local-time plumbing it needs already exists.

### 5.3 Hot-reload shader watching (M)

The DLL hot-reload story is already strong; shaders are the obvious next thing to iterate on without a
rebuild. A background `std::filesystem::last_write_time` poll on loaded `.glsl` files that triggers a
recompile-and-swap would tighten the shader iteration loop dramatically. Compile errors should keep the
last-good program bound rather than dropping to a black screen.

### 5.4 Smaller wins worth queueing

- **MSAA or remove the dead spec fields.** `FramebufferSpecification::Samples` and `SwapChainTarget`
  are public but unimplemented — a caller setting `Samples = 4` gets single-sampled output with no
  warning. Implement, or hide them behind `// RESERVED` until then.
- **`DrawRotatedQuad(vec2, …, Material)` overload.** Every other rotated-quad variant has both `vec2`
  and `vec3` forms; the material one is `vec3`-only, so `vec2` call sites won't compile.
- **Depth-sort documentation.** Batch geometry draws in submission order. That's a deliberate 2D
  tradeoff, but it should be stated in `Renderer2D.h` so users know layering is their responsibility.

---

## 6. Suggested sequencing

1. **Land the §1 correctness fixes in one pass** — they're all XS/S, touch isolated lines, and stop
   the engine from quietly lying (stats) or leaking/failing (textures, scene data).
2. **Do §3.1 + §2.3 together** — one `ResolveTextureSlot` helper delivers both the dedup and the O(1)
   lookup.
3. **Then §2.1 / §2.2** — cheap, hot-path wins.
4. **§4.1 before any project scales past ~1000 entities** — it's latent until it isn't.
5. **§5.1 asset cache first among features** — it pays for itself and unblocks the rest.

None of these block each other; the ordering is purely by ROI.
