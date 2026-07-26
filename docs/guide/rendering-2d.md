# 2D Rendering — Guide

**What this covers:** `Renderer2D` end to end — the pass wrappers, every `DrawQuad` overload,
rotated quads, sprite-sheet tiles, SDF circles, lines, world-space text, the two hardware-instanced
paths, `RenderPass` multi-camera, the stats counters, **every batch limit and exactly what happens
when you hit it**, and where 2D pixels actually land now that the scene composites through
`SceneRenderer`.
**Source of truth:** `Cosmic/src/renderer/Renderer2D.{h,cpp}`, `renderer/RenderPass.h`,
`graphics/SubTexture2D.{h,cpp}`, `graphics/Font.h`, `renderer/RendererAPI.h`,
`renderer/SceneRenderer.h`, `scene/Scene.cpp` (`OnRenderSprites`)
**API Reference:** [../reference/rendering-2d.md](../reference/rendering-2d.md) *(skeleton — D9)* ·
**How it works:** [../systems/rendering-2d.md](../systems/rendering-2d.md) *(skeleton — D28)*
**Configuration:** **both.** `Renderer2D` is shared source and compiles unfenced in the 3D and 2D
engine builds ([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)).

`Renderer2D` is a **batching** renderer. A `DrawQuad` call does not talk to the GPU — it appends
four vertices to a CPU-side array. Geometry is uploaded and drawn in bulk when a batch *flushes*,
which happens at the end of a pass, when a limit is reached, or when some piece of GPU state has to
change. Everything interesting about performance, draw order and the surprising cases follows from
that one fact, so [Batch limits and flushes](#batch-limits-and-flushes) is the section to read twice.

---

## Quick start

```cpp
#include "Cosmic.h"

void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

    Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                 { 0.9f, 0.3f, 0.2f, 1.0f });
    Cosmic::Renderer2D::DrawQuad({ 1.5f, 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Texture);
    Cosmic::Renderer2D::DrawCircle({ -1.5f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                   { 0.2f, 0.8f, 1.0f, 1.0f }, 1.0f, 0.005f);

    Cosmic::Renderer2D::EndScene();   // uploads and draws everything staged above
}
```

Three things this snippet is quietly asserting, all of which matter later:

- **Drawing happens in `OnUpdate`, not in a render hook.** `Layer::OnRender()` is declared but never
  called by the engine (D46). See [`time-and-ticks.md`](time-and-ticks.md).
- **`EndScene` is what draws.** Nothing reaches the GPU until a flush.
- **You do not bind a framebuffer.** In the editor and in a packaged app, a target is already bound
  for you — see [Where the pixels go](#where-the-pixels-go).

---

## Open and close a pass

```cpp
static void BeginScene(const Camera& camera);
static void EndScene();
```

`BeginScene` takes the **base `Camera`**, so an `OrthographicCamera`, a `PerspectiveCamera` or any
controller's camera all work — only `GetViewProjectionMatrix()` is read. `BeginScene` is a thin shim
over `PushRenderPass`, and `EndScene` over `PopRenderPass` (`Renderer2D.cpp:622-638`).

`BeginScene` derives its viewport from whatever `Renderer2D::SetViewportSize(w, h)` last recorded
and **calls `RenderCommand::SetViewport` with it**. That tracked size starts at `1280 × 720`
(`Renderer2D.cpp:170`). The engine keeps it current for you from two places — `Renderer::OnWindowResize`
(`Renderer.cpp:53`) and `WorkspaceLayer` each frame with the viewport-panel size
(`WorkspaceLayer.cpp:92`) — so under either host it is right. If you drive `Renderer2D` from
somewhere neither of those covers, call `SetViewportSize` yourself or `BeginScene` will silently
reset the GL viewport to 1280 × 720.

`PopRenderPass` does **not** restore a viewport when the stack empties, so the last pass's bounds
remain in effect after `EndScene`.

### Pushing passes by hand

```cpp
static void PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds);
static void PopRenderPass();
```

`viewportBounds` is `{ x, y, width, height }` in pixels from the **bottom-left** (the GL
convention). Use these when you have a view-projection matrix but no `Camera` object — which is
exactly what `Scene::OnRenderSprites` does (`Scene.cpp:624`). Prefer the RAII
[`RenderPass`](#render-more-than-one-camera) wrapper in normal code.

---

## Draw a coloured quad

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
```

`position` is the quad's **centre**; `size` is its full width and height in world units. The `vec2`
overload inserts `z = 0` and forwards (`Renderer2D.cpp:834`).

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.5f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f },       { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f });
```

**Z does not sort anything.** Within one batch, quads rasterise in submission order. `z` only
matters if a depth test is active — which it is when sprites draw through `Scene::OnRenderSprites`
(depth test on, depth write off, so 3D geometry can occlude a sprite in a 2.5D scene:
`Scene.cpp:620-622`), and is not in a bare `BeginScene` block unless you set it up. For
back-to-front alpha, order your calls.

## Draw a textured quad

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
                     const Ref<Texture>& texture,
                     float tilingFactor = 1.0f,
                     const glm::vec4& tintColor = glm::vec4(1.0f));
// …and the vec3 twin.
```

```cpp
Ref<Cosmic::Texture2D> tex =
    Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://assets/sprite.png"));

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tex);
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 4.0f, 4.0f }, tex, 4.0f);        // 4x UV tiling
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tex, 1.0f,
                             { 1.0f, 0.5f, 0.5f, 1.0f });                              // tint
```

**Failure behaviour, two layers deep:**

- `Texture2D::Create` **always returns a non-null `Ref`**. A missing or undecodable file yields a
  degraded texture with width 0, height 0 and GPU handle 0 — it binds nothing and the quad samples
  black. Check `GetWidth() > 0` if you care.
- `DrawQuad(..., const Ref<Texture>&, ...)` **null-checks** its texture: a null `Ref` logs
  `"Renderer2D: DrawQuad received null texture. Falling back to white."` and re-dispatches to the
  flat-colour overload with `tintColor` (`Renderer2D.cpp:845-850`). The `SubTexture2D` and
  rotated-texture overloads do **not** null-check — see [Pitfalls](#pitfalls).

`tilingFactor` multiplies the UVs in the shader. It is only useful when the texture's wrap mode
repeats; file textures are created repeating by default.

## Rotate a quad

Every quad form has a `DrawRotatedQuad` twin taking a `float rotation` **in radians**, applied about
the quad's centre on the Z axis:

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                    glm::radians(45.0f), { 1.0f, 1.0f, 0.0f, 1.0f });
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, rot, tex);
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, rot, subTexture);
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, rot, material);
```

There is no cost difference: the unrotated path builds `translate × scale`, the rotated path
`translate × rotate × scale`, and both write four transformed vertices. Use whichever reads better.

### The complete overload matrix

Sixteen `DrawQuad`/`DrawRotatedQuad` entry points, four payloads × two position types × rotated or
not (`Renderer2D.h:58-123`):

| Payload | `vec2` pos | `vec3` pos | Rotated `vec2` | Rotated `vec3` | Extra parameters |
| --- | :---: | :---: | :---: | :---: | --- |
| `const glm::vec4& color` | ✅ | ✅ | ✅ | ✅ | — |
| `const Ref<Texture>&` | ✅ | ✅ | ✅ | ✅ | `tilingFactor = 1.0f`, `tintColor = white` |
| `const Ref<SubTexture2D>&` | ✅ | ✅ | ✅ | ✅ | `tintColor = white` |
| `const Ref<Material>&` | ✅ | ✅ | ✅ | ✅ | — (values come from the material) |

`SubTexture2D` has **no** `tilingFactor` — tiling an atlas tile would bleed into its neighbours, so
the parameter is deliberately absent and the vertex attribute is hard-set to `1.0f`
(`Renderer2D.cpp:943`).

---

## Draw a sprite from a sprite sheet

`SubTexture2D` is a UV rectangle plus a reference to the parent atlas. It creates no GPU object, so
every tile of one sheet shares one texture slot and batches together.

```cpp
Ref<Cosmic::Texture2D> atlas =
    Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://assets/sheet.png"));

// CreateFromCoords(texture, gridCoords, cellSizePixels, spriteSizeInCells = {1,1})
Ref<Cosmic::SubTexture2D> tile =
    Cosmic::SubTexture2D::CreateFromCoords(atlas, { 2.0f, 0.0f }, { 64.0f, 64.0f });

Ref<Cosmic::SubTexture2D> wide =
    Cosmic::SubTexture2D::CreateFromCoords(atlas, { 4.0f, 1.0f }, { 64.0f, 64.0f }, { 2.0f, 2.0f });

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tile);
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tile, { 1.0f, 1.0f, 1.0f, 0.5f });
```

`coords` is `(column, row)`, zero-indexed **from the bottom-left** — GL's texture origin, not an
image editor's. The UV maths is (`SubTexture2D.cpp:26-34`):

```
min = (coords * cellSize) / textureSize
max = ((coords + spriteSize) * cellSize) / textureSize
```

Since `coords` is a `glm::vec2` of floats, fractional coordinates are legal — half-cell offsets
work if your sheet needs them.

If you already have normalised UVs, construct one directly (the constructor is public):

```cpp
auto tile = Cosmic::CreateRef<Cosmic::SubTexture2D>(atlas, glm::vec2{ 0.0f, 0.0f },
                                                           glm::vec2{ 0.25f, 0.25f });
```

`GetTexCoords()` returns a 4-element array in counter-clockwise order — index 0 bottom-left,
1 bottom-right, 2 top-right, 3 top-left (`SubTexture2D.cpp:10-13`) — matching the quad vertex
winding, which is why a sub-texture quad needs no extra transform.

The template project's sprite layer rebuilds its sub-textures whenever the animation frame changes,
which is the idiomatic shape (`Cosmic/templates/ExampleProject/src/TemplateSpriteLayer.cpp:163-177`):

```cpp
void TemplateSpriteLayer::UpdateSubTexture(int idx)
{
    m_SubTextures[idx] = Cosmic::SubTexture2D::CreateFromCoords(
        m_Atlases[m_AtlasIndex[idx]], { m_AnimCoords[idx], 0.0f }, k_CellSize);
}
```

Creating a `SubTexture2D` per frame is cheap (two divisions and a `make_shared`) but not free — for
a fixed sheet, build the tiles once at `OnAttach` and index into them.

---

## Draw a quad with your own shader

The material overloads route a quad through a custom `Ref<Material>`:

```cpp
static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material);
```

```cpp
auto shader   = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
if (!shader) { CS_ERROR("Fire.glsl failed to compile"); return; }
auto material = Cosmic::Material::Create(shader, "Fire");
material->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));

// per frame
material->Set("u_Time", GetLocalTime());
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f }, material);
```

**Two uniform names are special to this path** and are read out of the material *at submit time*
(`Renderer2D.cpp:890-893`):

| Name | Type | Used as | Missing ⇒ |
| --- | --- | --- | --- |
| `u_Texture` | `Ref<Texture>` | the quad's texture; resolved into a batch slot | the 1×1 white texture |
| `u_Color` | `glm::vec4` | baked into the quad's `a_Color` vertex attribute | `glm::vec4(1.0f)` — opaque white |

Everything else in the material's cache is uploaded once, by `Material::Bind()`, when the batch
flushes. That asymmetry has a consequence worth internalising: **`u_Color` varies per quad, every
other uniform does not.** Two quads sharing one material in one batch get their own colours (already
in the vertex data) but both see the *last* value you set for `u_Time`. For genuinely per-quad
uniforms, use `Material::Clone` — see
[`materials-and-shaders.md`](materials-and-shaders.md#one-material-many-looks).

A null `Ref<Material>` is a **silent no-op**: `DrawQuad` returns without drawing or logging
(`Renderer2D.cpp:885`).

Also see [Materials break batches](#batch-limits-and-flushes): interleaving material quads with
plain quads costs one draw call per quad.

---

## Draw circles and rings

`DrawCircle` rasterises a quad and evaluates a signed distance field in the fragment shader, so the
edge stays smooth at any zoom. It has its own batch, separate from quads.

```cpp
static void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color,
                       float thickness, float fade, Ref<Shader> customShader = nullptr);

// Inline vec2 twin, with defaults (Renderer2D.h:137-147):
static void DrawCircle(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color,
                       float thickness = 1.0f, float fade = 0.005f,
                       Ref<Shader> customShader = nullptr);
```

Note the asymmetry: **`thickness` and `fade` have defaults only on the `vec2` overload.** The `vec3`
form requires both.

```cpp
Cosmic::Renderer2D::DrawCircle({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f },
                               { 0.2f, 0.8f, 1.0f, 1.0f }, 1.0f,  0.005f);  // solid disc
Cosmic::Renderer2D::DrawCircle({ 3.0f, 0.0f, 0.0f }, { 2.0f, 2.0f },
                               { 1.0f, 0.5f, 0.0f, 0.9f }, 0.05f, 0.005f);  // thin ring
Cosmic::Renderer2D::DrawCircle({ 6.0f, 0.0f },       { 1.0f, 1.0f },
                               { 1.0f, 1.0f, 1.0f, 1.0f });                 // disc, defaults
```

`size` is the **bounding quad's** full width and height, so a circle of world radius `r` needs
`size = { 2r, 2r }` — as the benchmark layer does
(`TemplateRenderBenchmarkLayer.cpp:397-401`). `thickness` is the ring wall as a fraction of the
radius (`1.0` = filled); `fade` is the anti-aliased edge width, `0.005` being crisp and larger
values reading as glow. A non-uniform `size` gives an ellipse.

`customShader` replaces `Circle.glsl` for that call. The renderer tracks the active circle shader
and **breaks the batch whenever it changes** (`Renderer2D.cpp:1116-1120`), so alternating two custom
circle shaders costs one draw call per circle. A null `customShader` — including a `Ref` that went
out of scope during a hot reload — falls back to the engine shader rather than crashing
(`Renderer2D.cpp:1110-1113`).

## Draw lines and boxes

```cpp
static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color);
static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
```

Lines have their own batch and their own shader (`Line.glsl`), and are drawn **non-indexed** with
`glDrawArrays` — which is why `Statistics::GetTotalIndexCount()` deliberately ignores them
(`Renderer2D.h:186-188`). Line width is whatever the GL default is; there is no width parameter.

`DrawRect` is a convenience that emits four `DrawLine` calls around a centre-anchored box
(`Renderer2D.cpp:1252-1263`) — it is a **wireframe outline**, not a filled quad, and it can straddle
a flush if it happens to cross the line limit mid-rectangle.

---

## Draw world-space text

`Renderer2D::DrawString` renders through `Cosmic::Font`, which bakes a TTF/OTF into a single-channel
SDF atlas (cached to disk after the first bake) so text stays sharp at any camera zoom. This is the
**world-space** path; for panel text use `Cosmic::UI::Fonts` and ImGui.

```cpp
static void DrawString(const std::string& text, const Ref<Font>& font,
                       const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f),
                       float kerning = 0.0f, float lineSpacing = 0.0f);

static void DrawString(const std::string& text, const Ref<Font>& font,
                       const glm::vec2& position, float size, const glm::vec4& color = glm::vec4(1.0f),
                       float kerning = 0.0f, float lineSpacing = 0.0f);
```

```cpp
Ref<Cosmic::Font> font = Cosmic::Font::Get("Roboto-Bold");   // by file stem
if (!font) font = Cosmic::Font::Default();

Cosmic::Renderer2D::DrawString("SCORE 1200", font, { -2.0f, 3.0f }, 0.5f,
                               { 1.0f, 1.0f, 1.0f, 1.0f });
```

Glyph metrics are in **em units**, where 1 em is the transform's unit scale — so one `Font` renders
at every size and the `size` parameter of the convenience overload is simply a uniform scale. The
**baseline of the first line sits at the transform origin**, and additional lines advance *downward*
in `-Y`. `\n` starts a new line, `\r` is skipped, and a codepoint with no glyph falls back to `?`
(then is skipped entirely if `?` is missing too) — `Renderer2D.cpp:1173-1180`.

`Font::Create` and `Font::Get` return `nullptr` on failure. `DrawString` itself is defensive: a null
font, an empty string or a font with no atlas all return without drawing (`Renderer2D.cpp:1159-1162`).

For y-down canvas space, flip with a negative Y scale — the pattern `UiSystem` uses
(`UiSystem.cpp:416-419`):

```cpp
glm::mat4 transform = glm::translate(glm::mat4(1.0f), { x, baselineY, 0.0f })
                    * glm::scale(glm::mat4(1.0f), { pixelSize, -pixelSize, 1.0f });
Cosmic::Renderer2D::DrawString(line, font, transform, color);
```

Text has its own batch, keyed on the **font atlas texture**. Mixing two fonts in one pass breaks the
batch on every switch; group your `DrawString` calls by font.

---

## Draw thousands of things at once

`DrawInstancedQuads` and `DrawInstancedCircles` bypass the batch entirely: you fill a flat array,
the renderer streams it to a per-instance vertex buffer, and one `glDrawElementsInstanced` draws all
of it.

```cpp
static void DrawInstancedQuads  (const InstanceQuadData*   instances, uint32_t count,
                                 Ref<Shader> customShader = nullptr);
static void DrawInstancedCircles(const InstanceCircleData* instances, uint32_t count,
                                 Ref<Shader> customShader = nullptr);
```

```cpp
struct InstanceQuadData          // 60 bytes, static_assert'd against QuadInstance.glsl
{
    glm::vec3 Position;          // world-space centre
    glm::vec2 Scale;             // full width and height
    glm::vec4 Color;             // RGBA tint, multiplied with the texture sample
    glm::vec2 TexCoordOffset;    // normalised UV origin  — {0,0} for solid colour
    glm::vec2 TexCoordScale;     // normalised UV extent  — {1,1} for solid colour
    float     TexIndex;          // u_Textures[] slot; 0 = the white texture
    float     TilingFactor;
};

struct InstanceCircleData        // 44 bytes
{
    glm::vec3 Position;
    glm::vec2 Scale;
    glm::vec4 Color;
    float     Thickness;
    float     Fade;
};
```

Real usage, from the template project's benchmark layer
(`TemplateRenderBenchmarkLayer.cpp:409-461`):

```cpp
std::vector<Cosmic::Renderer2D::InstanceQuadData> buf;
buf.reserve(count);

for (auto entity : view)
{
    const auto& t = view.get<Cosmic::TransformComponent>(entity);
    const auto& b = view.get<BenchmarkBallComponent>(entity);

    buf.push_back({
        t.Position,
        { b.Radius * 2.0f, b.Radius * 2.0f },
        b.Color,
        { 0.0f, 0.0f },   // TexCoordOffset — solid colour
        { 1.0f, 1.0f },   // TexCoordScale  — solid colour
        0.0f,             // TexIndex       — white texture slot
        1.0f              // TilingFactor
    });
}

if (!buf.empty())
    Cosmic::Renderer2D::DrawInstancedQuads(buf.data(), static_cast<uint32_t>(buf.size()));
```

### When instancing actually wins

The trade is **CPU work per object** against **draw calls**. Batched `DrawQuad` writes 4 vertices
(176 bytes) through a pointer and costs one draw call per 10,000 quads; instanced quads write 60
bytes and cost one draw call per 20,000. So instancing is *not* a draw-call rescue for a few
thousand sprites — the batcher already collapses those.

It wins when:

- **You already have the data in an array.** No per-object function call, no vertex expansion —
  `memcpy`-shaped work instead.
- **You need a custom per-instance shader.** The instance attributes reach the vertex stage at
  locations 1–7, which a batched quad cannot express.
- **Counts run into the tens of thousands.** Above ~20,000 the 3× smaller per-object payload and the
  absent per-quad transform maths dominate.

It loses when:

- **Counts are small.** Both instanced entry points call `FlushAndReset()` before doing anything
  (`Renderer2D.cpp:1289`, `:1374`), so a 50-instance call forces a flush of everything pending and
  then adds its own draw call — strictly worse than 50 `DrawQuad`s.
- **You need per-object textures.** Instances index `u_Textures[]` by `TexIndex`, but **the instanced
  path never populates the slot table** — it binds only the white texture to slot 0
  (`Renderer2D.cpp:1408`). Any `TexIndex != 0` samples whatever a previous batch happened to leave
  bound. Bind your own textures before the call if you need them.

Both calls are **safe at any `count`**: they stream in chunks (20,000 instances per chunk) and issue
one draw per chunk, so a 500,000-instance array works and reports 25 draw calls. `count == 0` or a
null pointer returns immediately.

Both must be called inside an active pass — they read `s_Data.ViewProjectionMatrix`, which is only
meaningful between `BeginScene`/`EndScene` or a `PushRenderPass`/`PopRenderPass` pair. Both restore
`CurrentMaterial` to the default afterwards so the next `DrawQuad` does not trigger a spurious flush.

---

## Render more than one camera

`RenderPass` (`renderer/RenderPass.h`) is an RAII wrapper over `PushRenderPass`/`PopRenderPass`:
construction flushes pending geometry and installs a new view-projection + viewport; destruction
flushes and restores the previous pass.

```cpp
void MyLayer::OnUpdate(float ts)
{
    {   // main world view — full window
        Cosmic::RenderPass main(m_MainCamera.GetCamera(), { 0.0f, 0.0f, 1280.0f, 720.0f });
        Cosmic::Renderer2D::DrawQuad(/* … */);
    }   // ← flushes and restores here

    {   // minimap — a corner region
        Cosmic::RenderPass minimap(m_OverviewCamera.GetCamera(), { 900.0f, 500.0f, 380.0f, 220.0f });
        Cosmic::Renderer2D::DrawQuad(/* … */);
    }
}
```

Targeting a framebuffer is just a matter of binding one around the scope:

```cpp
m_SideFbo->Bind();
{
    Cosmic::RenderPass side(m_SideCamera.GetCamera(),
                            { 0.0f, 0.0f, (float)m_SideFbo->GetWidth(), (float)m_SideFbo->GetHeight() });
    Cosmic::Renderer2D::DrawQuad(/* … */);
}
m_SideFbo->Unbind();
```

Rules:

- The constructor takes **`const Camera&`** — any camera, 2D or 3D (`RenderPass.h:90`). The header's
  own prose still describes an `OrthographicCamera` parameter; the code is the truth.
- `RenderPass` is **non-copyable and non-movable**. One scope owns one stack entry.
- Bounds are `{ x, y, width, height }` in pixels from the bottom-left. For a 1280 × 720 window split
  into quadrants: top-left `{0, 360, 640, 360}`, top-right `{640, 360, 640, 360}`, bottom-left
  `{0, 0, 640, 360}`, bottom-right `{640, 0, 640, 360}`.
- **Every push resets every batch counter**, so geometry never leaks across a pass boundary.
- **Push and pop must balance.** `PopRenderPass` opens with `CS_CORE_ASSERT(!stack.empty(), …)` — but
  that macro is compiled out in *every* configuration (D47), so an unmatched pop is `pop_back()` on
  an empty vector: undefined behaviour, no diagnostic. Use `RenderPass` and this cannot happen.

---

## Batch limits and flushes

Everything here is read out of **`Cosmic/src/renderer/Renderer2D.cpp`**. Line numbers are cited so
they can be re-checked when the constants move.

### The limits

| Constant | Value | Declared | Host buffer | Effective ceiling per batch |
| --- | ---: | --- | ---: | --- |
| `MaxQuads` | 10 000 | `:63` | 40 000 × 44 B ≈ 1.76 MB | **10 000 quads** (`MaxIndices` = 60 000, `:65`) |
| `MaxTextureSlots` | 32 | `:66` | — | **31 distinct textures** (slot 0 is reserved) |
| `MaxLines` | 10 000 | `:68` | 20 000 × 28 B ≈ 0.56 MB | **10 000 lines** (`MaxLineVertices` = 20 000, `:69`) |
| `MaxCircles` | 10 000 | `:71` | 40 000 × 44 B ≈ 1.76 MB | **10 000 circles** (`MaxCircleIndices` = 60 000, `:73`) |
| `MaxTextQuads` | 10 000 | `:119` | 40 000 × 36 B ≈ 1.44 MB | **10 000 visible glyphs** (`MaxTextIndices` = 60 000, `:121`) |
| `MaxInstancedQuads` | 20 000 | `:150` | 20 000 × 60 B = 1.2 MB | **per chunk** — the call itself is unbounded |
| `MaxInstancedCircles` | 20 000 | `:76` | 20 000 × 44 B ≈ 0.88 MB | **per chunk** — the call itself is unbounded |

They are `static const uint32_t` members of the file-local `Renderer2DData`, so they are **compile-time
and not configurable at runtime**. Total staging cost is roughly 5.5 MB host + the same again in GPU
buffers, allocated once in `Renderer2D::Init` regardless of what a project draws.

### What happens at each limit

Every batched primitive checks its own limit **before** writing its vertices, so no limit can be
overrun — the check is a hard fence, not a diagnostic.

| Limit reached | Check site | Behaviour |
| --- | --- | --- |
| `MaxIndices` (quads) | `:816 :853 :895 :928 :962 :993 :1041 :1070` | `FlushAndReset()`, then the quad is written into the fresh batch |
| `MaxTextureSlots` | `:799` in `ResolveTextureSlot` | `FlushAndReset()`, then the new texture takes slot 1 |
| `MaxLineVertices` | `:1238` | `FlushAndReset()`, then both endpoints are written |
| `MaxCircleIndices` | `:1123` | `FlushAndReset()`, then the circle is written |
| `MaxTextIndices` | `:1185`, inside the glyph loop | `FlushAndReset()` **mid-string**, the atlas is re-installed at `:1188`, and the glyph is written |
| `MaxInstancedQuads` / `Circles` | `:1421-1443`, `:1321-1343` | never overruns — a `while (remaining)` loop uploads and draws one chunk at a time |

No limit ever logs, warns, drops geometry or fails. **A flush is invisible except in the draw-call
counter** — which is the point, and also why over-limit behaviour is easy to miss without
[stats armed](#read-the-stats-counters).

Two details worth knowing precisely:

- **The line check is `LineVertexCount >= MaxLineVertices - 1`** (`:1238`), not `>= MaxLineVertices`.
  Because the counter always moves in steps of 2 from 0, it reaches 19 998, passes the check, writes
  vertices 19 998–19 999, and lands on exactly 20 000 — so the `- 1` never changes the outcome and
  the real ceiling is exactly 10 000 lines. The guard is defensive, not load-bearing.
- **`DrawRect` is four `DrawLine` calls.** A rectangle submitted at the boundary can be split across
  two draw calls. Visually identical; it shows up as an extra `DrawCalls` tick.

### The other reasons a batch breaks

Limits are the *predictable* flushes. These are the ones that surprise people, because they are
driven by state rather than volume:

| Trigger | Check site | Notes |
| --- | --- | --- |
| **Material identity changes** | `:815 :852 :887 :927 :961 :992 :1033 :1069` | Every *non-material* overload flushes if `CurrentMaterial != DefaultMaterial`; every material overload flushes if `CurrentMaterial != material`. Pointer comparison — two materials with identical contents are still two batches. |
| **Circle custom shader changes** | `:1116` | Including switching back to the default. |
| **Text font atlas changes** | `:1165` | Keyed on the atlas's renderer ID, so two `Font`s over the same TTF at the same size still break (different `Texture2D` objects). |
| **`DrawInstancedQuads` / `DrawInstancedCircles`** | `:1289`, `:1374` | Flush *up front*, unconditionally, for pipeline isolation. |
| **`PushRenderPass` / `PopRenderPass`** | `:514-521`, `:569-576` | Only if something is pending. |

The material one is the expensive trap. This costs **one draw call per quad**:

```cpp
for (const auto& s : sprites)
{
    Cosmic::Renderer2D::DrawQuad(s.pos, s.size, s.texture);      // flushes: material != default
    Cosmic::Renderer2D::DrawQuad(s.pos, s.size, m_GlowMaterial); // flushes: material changed
}
```

Sort by material and the same work becomes two draw calls. This is exactly what
`Scene::BuildSpriteDrawList` does for you before `OnRenderSprites` walks it (`Scene.cpp:552`).

### Draw order between primitive types is fixed

`Flush()` always emits in the same order — **quads, then lines, then circles, then text**
(`Renderer2D.cpp:647`, `:675`, `:689`, `:711`) — regardless of the order you called them in. Inside
one flush, a line always draws over a quad, and text always draws over everything.

Submission order is preserved *within* a primitive type only. `Renderer2D.h:89-92`'s "batched
geometry is rasterized in submission order" is true per-type and misleading across types; the same
comment's reference to `Scene::OnRender` sorting by `Position.z` describes a code path that has
had **zero callers** since `OnRenderSprites` superseded it (D49).

To force an interleaving, split the work across passes, or arrange a flush between the two groups.

### `Flush()` is public and does not reset

`Renderer2D::Flush()` (`:644`) draws every pending batch but **leaves the counters and buffer
pointers where they were**. Only the private `FlushAndReset()` and the pass push/pop reset them. So:

```cpp
Cosmic::Renderer2D::DrawQuad(/* … */);
Cosmic::Renderer2D::Flush();      // draws the quad
Cosmic::Renderer2D::EndScene();   // PopRenderPass sees pending geometry → draws it AGAIN
```

Nothing in the engine or the sample projects calls `Flush()` from outside `Renderer2D`. Treat it as
internal; if you need a mid-frame fence, use a nested `RenderPass`.

---

## Read the stats counters

```cpp
struct Statistics
{
    uint32_t DrawCalls, QuadCount, CircleCount, LineCount;
    uint32_t GetTotalVertexCount() const;   // Quads*4 + Circles*4 + Lines*2
    uint32_t GetTotalIndexCount()  const;   // (Quads + Circles) * 6 — lines are non-indexed
};

static void       ResetStats();
static Statistics GetStats();
static void       SetStatsStatus(bool enabled);
```

> **`StatsEnabled` defaults to `false`** (`Renderer2D.cpp:176`) and **nothing in the engine arms it
> for you.** Every counter increment in the file is guarded by `if (s_Data.StatsEnabled)`, so
> `GetStats()` returns all zeros until something calls `SetStatsStatus(true)`. The only in-tree
> caller is `StarforgeApp`, inside `#ifdef COSMIC_2D_ONLY` (`StarforgeApp.cpp:1223`) — a 3D editor
> build never arms them either.

Arm once, reset every frame, read after drawing:

```cpp
void MyLayer::OnAttach()  { Cosmic::Renderer2D::SetStatsStatus(true); }

void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::ResetStats();       // BEFORE the pass — counters are cumulative

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    /* … draw … */
    Cosmic::Renderer2D::EndScene();
}

void MyLayer::OnImGuiRender()
{
    const auto s = Cosmic::Renderer2D::GetStats();
    ImGui::Text("Draw calls: %u", s.DrawCalls);
    ImGui::Text("Quads:      %u", s.QuadCount);
    ImGui::Text("Circles:    %u", s.CircleCount);
    ImGui::Text("Lines:      %u", s.LineCount);
    ImGui::Text("Vertices:   %u", s.GetTotalVertexCount());
}
```

Reading them correctly:

- **`ResetStats` is yours to call.** Nothing in the engine calls it. Miss it and every counter is a
  lifetime total.
- **`QuadCount` includes text glyphs.** Each visible glyph increments it (`:1217`), so a scene with
  on-screen text reports more "quads" than sprites.
- **Instanced draws are counted**, by chunk: `+1` draw call and `+batchSize` quads or circles per
  chunk (`:1338-1339`, `:1438-1439`).
- **`DrawCalls` counts flushes, not primitives** — one per non-empty batch inside each `Flush()`, so
  a pass with quads, lines and text pending reports 3.
- **`GetTotalIndexCount()` excludes lines on purpose** — they go through `glDrawArrays`
  (`Renderer2D.h:186-188`).
- `ResetStats` `memset`s the struct (`:1271`); `GetStats` returns a copy.

`Renderer3D`'s counters behave differently — always on, never reset by the engine. See
[`logging-and-diagnostics.md`](logging-and-diagnostics.md).

---

## Where the pixels go

2D output **no longer goes straight to the backbuffer.** Since Phase 27/29 a scene's sprites
composite through the same `SceneRenderer` spine the 3D path uses, in both engine configurations
(`SceneRenderer.h:42-54`, `SceneRenderer.cpp:373-375`):

```
PassOpaqueHDR        → an RGBA16F HDR target is bound
PassTransparents     → your sprites + 2D lights draw here, via desc.DrawTransparent
PassPostAndComposite → tonemap → FXAA / bloom / vignette → resolve to the LDR viewport target
DrawOverlay2D        → canvas UI draws last, LDR bound
```

What that changes for you:

- **Colours are HDR until the tonemap.** A sprite colour above 1.0 blooms instead of clipping.
  Exposure and the post chain are scene settings, not renderer settings — see
  `EnvironmentComponent`.
- **Sprites draw in the transparent phase** with depth test **on** and depth write **off**, under
  straight alpha (`Scene.cpp:618-622`), so 3D geometry occludes a sprite in a 2.5D scene while
  sprites never occlude each other by depth.
- **Canvas UI composites after the tonemap**, so UI colours are literal — they are not exposed or
  bloomed.
- A scene with no 2D content **makes no GL calls at all** in these passes (`Scene.cpp:592-593`), so
  a 3D-only project pays nothing.

`PlayerLayer` wires the hooks for a packaged app and Starforge does the same for its viewport, which
is why a shipped build looks identical to the editor (`PlayerLayer.cpp:367-379`):

```cpp
desc.DrawTransparent = [scenePtr, vw, vh](const Cosmic::SceneDrawContext& c)
{
    scenePtr->OnRenderSprites(c.ViewProjection, vw, vh);
    scenePtr->OnRender2DLights(c.ViewProjection, vw, vh);
};
desc.DrawOverlay2D = [scenePtr, vw, vh, camVP]()
{
    Cosmic::UiSystem::Render(*scenePtr, Cosmic::UiRect{ {0,0}, {(float)vw, (float)vh} }, &camVP);
};
```

Drawing `Renderer2D` calls yourself from a layer's `OnUpdate` still works exactly as before — you
are drawing into whatever target the host bound, which in the editor is the viewport framebuffer.

### `BlendMode::Multiply`

`RendererAPI::BlendMode` gained a fourth mode in Phase 27 (`RendererAPI.h:109`):

| Mode | GL factors | Use |
| --- | --- | --- |
| `Alpha` | src-alpha over | the engine default, set at `Init` |
| `Additive` | (src-alpha, one) | emissive, particles, light accumulation |
| `Off` | blending disabled | opaque passes that must not read the destination |
| `Multiply` | `(GL_DST_COLOR, GL_ZERO)` | **darkening composites** — dst × src |

`Multiply` exists for the 2D light buffer: `Light2DRenderer` accumulates lights additively into a
half-resolution RGBA16F target over an ambient clear, then multiplies that buffer over the scene so
unlit regions darken (`Light2DRenderer.cpp:98-101`). It restores `Alpha`, depth test and depth write
on exit, so the mode never leaks into the next pass.

Set it yourself with `RenderCommand::SetBlendMode(RendererAPI::BlendMode::Multiply)` — and restore
`Alpha` afterwards; the engine's flush-time contract assumes the defaults.

---

## Common patterns

**Group by material, then by texture.** Both break batches. Sorting a draw list once per frame beats
paying a draw call per sprite — the engine does this for scene sprites in
`Scene::BuildSpriteDrawList`.

**One atlas per layer.** 31 distinct textures fit in a batch. A tile set, a character sheet and an
effects sheet is three slots; 200 individually-loaded PNGs is seven flushes per frame.

**Build sub-textures once.** For a static sheet, create the `SubTexture2D` set in `OnAttach` and
index it. Rebuild per frame only when the tile actually changes.

**Reach for instancing when the data is already an array**, not as a reflex. Below a few thousand
objects the batcher wins, and instanced calls force a flush.

**Arm stats in development, leave them off in a shipping build.** Every counter is behind a branch
in the hot path.

**Use `RenderPass`, not raw push/pop.** The assert that would catch an imbalance is compiled out.

---

## Pitfalls

**"Nothing draws and there's no error."** Most likely no pass is active — every draw call stages
into a batch that only reaches the GPU on flush, and a flush outside a pass has no view-projection
matrix. Check that `BeginScene` ran and that `EndScene`/the `RenderPass` scope actually closed.

**"Everything renders into the wrong corner of the window."** `BeginScene` sets the GL viewport from
`Renderer2D`'s tracked size, which defaults to 1280 × 720. Under `WorkspaceLayer` or after a window
resize that value is maintained for you; anywhere else, call `SetViewportSize` first.

**"My sprites are behind the collider overlay / the UI is bloomed."** Draw order between primitive
types inside one flush is fixed (quads → lines → circles → text), and canvas UI composites after the
tonemap while sprites composite before it. Neither is submission order.

**"My draw-call count is equal to my sprite count."** You are alternating materials — or alternating
between material and non-material overloads. Each switch is a flush.

**"`GetStats()` returns zeros."** `StatsEnabled` defaults to `false`. Call `SetStatsStatus(true)`
once.

**"The counters only ever go up."** Nothing calls `ResetStats()` for you.

**"Passing a null `SubTexture2D` crashes."** The texture overload null-checks and falls back to
white; the `SubTexture2D` overloads dereference immediately (`Renderer2D.cpp:930`, `:1072`), as does
`SubTexture2D::CreateFromCoords` on a null atlas. Guard your own pointers on these paths.

**"My instanced quads are all white / textured with the wrong image."** The instanced path binds
only the white texture to slot 0. `TexIndex != 0` samples whatever was left bound by an earlier
batch. Bind textures yourself before the call.

**"Calling `Flush()` draws my geometry twice."** It does — `Flush()` does not reset counters. Don't
call it.

**"A 10,001st quad vanished."** It did not; the batch flushed and the quad went into the next one.
No limit in `Renderer2D` ever drops geometry.

**"Text from two fonts costs a draw call per string."** The text batch is keyed on the font atlas.
Group by font.

---

## See also

- [`../reference/rendering-2d.md`](../reference/rendering-2d.md) — per-call signatures *(skeleton, D9)*
- [`../systems/rendering-2d.md`](../systems/rendering-2d.md) — how batching works internally *(skeleton, D28)*
- [`materials-and-shaders.md`](materials-and-shaders.md) — `Material`, the shader contract, framebuffers
- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — the component-driven 2D authoring path
- [`game-ui.md`](game-ui.md) — canvas UI, which composites after the tonemap
- [`entities-and-components.md`](entities-and-components.md) — `SpriteRendererComponent`,
  `TilemapComponent`, `Light2DComponent`
- [`logging-and-diagnostics.md`](logging-and-diagnostics.md) — the other stats counters and the GPU profiler
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what each configuration ships
