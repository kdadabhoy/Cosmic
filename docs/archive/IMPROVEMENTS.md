# Cosmic Engine — Improvement Proposals

> **Scope:** A prioritized, actionable engineering roadmap for the Cosmic engine core (`Cosmic/src/`).
> **Verified against:** commit `9078662`, 2026-06-24. Every claim below was checked against the
> current source before being written; file references are live as of this commit.
> **Status:** The §1–§4 items marked **✅ Implemented** were applied in the 2026-06-24 cleanup pass.
> The §5 feature items remain as roadmap. Two originally-suspected issues were **disproved on
> verification** and are recorded as such in §7 so they are not "fixed" by mistake later.
> **Companion doc:** [`engine_analysis.md`](engine_analysis.md) is the longer reference analysis.

---

## How to read this

Each item has a **cost** (rough effort), an **impact**, and a **status**. The summary table is the
TL;DR; the sections below give the rationale and the concrete change.

| # | Improvement | Category | Cost | Impact | Status |
|---|-------------|----------|------|--------|--------|
| 1 | Fix `Stats` line/index counters | Correctness | XS | Medium | ✅ Implemented |
| 2 | Stop leaking `Renderer::s_SceneData` | Correctness | XS | Medium | ✅ Implemented |
| 3 | Handle 1/2-channel textures | Correctness | S | High | ✅ Implemented |
| 4 | Reconcile `PauseOnMinimize` doc vs. default | Correctness | XS | Low | ✅ Implemented |
| 5 | Hoist instanced-quad sampler upload out of draw loop | Performance | S | Medium | ✅ Implemented |
| 6 | Persistent scratch buffers in `Scene::OnRender` | Performance | S | Medium | ✅ Implemented |
| 7 | O(1) texture-slot lookup | Performance | S | Medium | ✅ Implemented |
| 8 | Collapse the six copies of texture-slot resolution | Maintainability | M | Medium | ✅ Implemented |
| 9 | Demote the legacy `Renderer::Submit` path in docs | Architecture | S | Medium | ✅ Implemented |
| 10 | Normalize the `RendererAPI::Draw*` bind contract | Architecture | S | Low | ✅ Implemented |
| 11 | Guard `ComponentArray<T>` multi-page UB in Release | Safety | S | High | ✅ Implemented |
| 12 | Add `DrawRotatedQuad(vec2, …, Material)` overload | API ergonomics | XS | Low | ✅ Implemented |
| 13 | Asset cache (textures, shaders, fonts) | Feature | M | High | ⬜ Roadmap |
| 14 | Sprite-animation component | Feature | M | Medium | ⬜ Roadmap |
| 15 | Hot-reload shader file watching | Feature | M | Medium | ⬜ Roadmap |
| 16 | MSAA (or remove the reserved spec fields) | Feature | M | Low | ⬜ Roadmap |

---

## 1. Correctness fixes ✅ Implemented

These produced wrong results *silently* — no crash, no log — which made them the highest-leverage
fixes despite being tiny.

### 1.1 Renderer statistics under-reported

`Renderer2D::DrawLine` advanced the vertex count but never the stat counter, so `Stats.LineCount` was
always `0`; and `GetTotalIndexCount()` only counted quads, ignoring circles (which also emit 6 indices).

- [`Renderer2D.cpp`](../Cosmic/src/renderer/Renderer2D.cpp) `DrawLine` now does
  `if (s_Data.StatsEnabled) s_Data.Stats.LineCount++;`.
- [`Renderer2D.h`](../Cosmic/src/renderer/Renderer2D.h) `GetTotalIndexCount()` is now
  `(QuadCount + CircleCount) * 6`.

### 1.2 `Renderer::s_SceneData` leaked every run

It was `new`-allocated at namespace scope and never freed by `Renderer::Shutdown`. Replaced with a
**value** member (`static SceneData s_SceneData;`) in
[`Renderer.h`](../Cosmic/src/renderer/Renderer.h) / [`Renderer.cpp`](../Cosmic/src/renderer/Renderer.cpp) —
no allocation, nothing to leak, and one less pointer indirection in `Submit`.

### 1.3 Textures with 1 or 2 channels uploaded an invalid GL format

[`OpenGLTexture.cpp`](../Cosmic/src/platform/OpenGL/OpenGLTexture.cpp) only assigned a format for 3- and
4-channel images; a grayscale (1ch) or grayscale+alpha (2ch) image left the format at `0`, so
`glTexImage2D` got an invalid enum and the texture rendered black. Added `GL_R8`/`GL_RED` and
`GL_RG8`/`GL_RG` branches plus an `else` that logs the unsupported channel count, frees the pixels, and
bails. *(Note: the failed-`stbi_load` path already logged an error — that part of the original
suspicion was incorrect; only the channel-format gap was real.)*

### 1.4 `PauseOnMinimize` default contradicted its own documentation

The header comment in [`Application.h`](../Cosmic/src/core/Application.h) claimed *"When true (default)…"*
while the member defaulted to `false`. The `false` behavior (keep ticking while minimized) is correct
for a simulation/telemetry engine, so the **comment** was corrected to match the code.

---

## 2. Performance ✅ Implemented

### 2.1 Instanced-quad sampler array was re-uploaded every draw

`DrawInstancedQuads` rebuilt and re-uploaded all 32 `u_Textures` samplers on every call. The upload now
happens **once** in [`Renderer2D::Init`](../Cosmic/src/renderer/Renderer2D.cpp) for the default shader;
only a caller-supplied **custom** shader re-uploads it in the draw call (it has its own program and
can't inherit the default's binding).

### 2.2 `Scene::OnRender` allocated its sort buckets every frame

The material-bucket `unordered_map` and the fallback `vector` are now **persistent members** of
[`Scene`](../Cosmic/src/scene/Scene.h), cleared (not destroyed) each frame so their backing storage is
reused. Empty buckets left by a no-longer-used material are skipped during dispatch. Steady-state
per-frame heap allocation for this path drops to zero.

### 2.3 Texture-slot lookup was a linear scan per quad

Folded into the new `ResolveTextureSlot` helper (see §3.1) as a `std::unordered_map<uint32_t, uint32_t>`
(renderer-ID → slot), cleared at each of the three batch-reset sites. Slot 0 (the white texture) is
special-cased so it never consumes a slot — preserving the exact behavior of the old linear scan.

---

## 3. Architecture & maintainability ✅ Implemented

### 3.1 Six near-identical copies of texture-slot resolution

The "search slots → flush-if-full → register" block was duplicated verbatim across the texture,
material, and subtexture variants of both `DrawQuad` and `DrawRotatedQuad` (~120 lines). All six now
call a single private `Renderer2D::ResolveTextureSlot(const Ref<Texture>&)`, which also carries the
O(1) lookup from §2.3. One place to change, one place to get right.

### 3.2 The legacy `Renderer::Submit` path is a desynced second camera

`Renderer` keeps its own view-projection matrix, independent of `Renderer2D`'s; mixing the two in one
frame silently draws under two cameras, and every `Submit` is an un-batched draw. Rather than rewire it
(risk for little gain — it's barely used), the path is now clearly **demoted in the header docs** of
[`Renderer.h`](../Cosmic/src/renderer/Renderer.h) as a low-level custom-shader escape hatch, with the
camera-desync caveat stated explicitly.

### 3.3 `RendererAPI::Draw*` bind contract was inconsistent

`OpenGLRendererAPI::DrawLines` bound the VAO internally while `DrawIndexed`/`DrawIndexedInstanced` did
not, and `Renderer2D::Flush` already binds before calling it (a redundant double-bind). The internal
bind was removed from [`OpenGLRendererAPI.cpp`](../Cosmic/src/platform/OpenGL/OpenGLRendererAPI.cpp) and
the "caller binds" contract documented. Verified the only caller (`Renderer2D::Flush`) binds first.

---

## 4. Safety & API ergonomics ✅ Implemented

### 4.1 `ComponentArray<T>` silently corrupted past one EnTT page

`From()` returned a pointer into page 0 while `Count()` reported the total across all pages; past
~1024 entities, indexing read out of bounds. The guard was a Debug-only `CS_CORE_ASSERT` that compiled
out in Release. It is now a **hard runtime guard in every build**: if the pool spans more than one page,
[`ComponentArray.h`](../Cosmic/src/jobs/ComponentArray.h) logs an error and returns an **empty** view
instead of a half-valid pointer. (Use `FlatComponentArray<T>` for large pools.)

### 4.2 `DrawRotatedQuad(vec2, …, Material)` overload was missing

Every other rotated-quad variant had both `vec2` and `vec3` position forms; the material one was
`vec3`-only, so `vec2` call sites wouldn't compile. Added the one-line forwarding overload to
[`Renderer2D.h`](../Cosmic/src/renderer/Renderer2D.h) / `Renderer2D.cpp`. Also documented the
batch submission-order (no automatic depth sort) contract in the header.

---

## 5. Feature roadmap ⬜ (not yet implemented)

Ordered by leverage for the engine's actual use case (interactive 2D simulations / telemetry tooling).
These are genuine new subsystems, intentionally left for a dedicated pass rather than folded into the
cleanup above.

### 5.1 Asset cache for textures/shaders/fonts (M, High)

`Texture2D::Create("foo.png")` reloads and re-uploads the file on every call with no shared ownership.
A small `AssetLibrary` keyed by path that returns the existing `Ref<>` would cut load time, VRAM, and
the duplicate-texture-slot pressure that motivates §2.3/§3.1. The single feature most others benefit
from.

### 5.2 Sprite-animation component (M)

`SubTexture2D` slices atlases but nothing advances frames over time. A
`SpriteAnimationComponent { frames, fps, loop }` plus a tiny system that swaps the active `SubTexture2D`
on `OnUpdate(dt)` would make animated sprites a data-only feature. The `Timestep`/local-time plumbing it
needs already exists.

### 5.3 Hot-reload shader file watching (M)

The DLL hot-reload story is strong; shaders are the obvious next iterate-without-rebuild target. A
background `last_write_time` poll on loaded `.glsl` files that recompiles-and-swaps would tighten the
loop dramatically. A failed recompile should keep the last-good program bound rather than dropping to a
black screen.

### 5.4 MSAA, or remove the reserved spec fields (M, Low)

`FramebufferSpecification::Samples` and `SwapChainTarget` are public but unimplemented — setting
`Samples = 4` yields single-sampled output with no warning. Either implement an MSAA resolve path or
hide the fields until then. They are at least clearly commented as "Reserved" today.

---

## 6. Suggested sequencing (remaining work)

1. **§5.1 asset cache first** among features — it pays for itself and unblocks the rest.
2. **§5.2 / §5.3** are independent and can be done in either order.
3. **§5.4** only when MSAA is actually wanted; until then the reserved fields are documented.

---

## 7. Verified NON-issues (do not "fix")

Recorded so a future reader doesn't re-flag code that is actually correct:

- **`TelemetryPanel` `ImPlotSpec` usage is correct.** The vendored ImPlot
  (`Cosmic/dependencies/implot/implot.h:990`) provides `PlotLine(label, xs, ys, count, const ImPlotSpec&)`
  as a first-class overload; `spec.Offset` is valid. The engine uses its bundled ImPlot, not stock, so
  there is nothing to change. *(Earlier analyses that assumed stock ImPlot were mistaken.)*
- **The file-based `OpenGLTexture` constructor does log on load failure.** The `else` branch on a null
  `stbi_load` already emits `CS_CORE_ERROR("Failed to load texture at …")` and resets to a safe 0×0
  state. Only the *channel-count* format gap (§1.3) was a real defect.
