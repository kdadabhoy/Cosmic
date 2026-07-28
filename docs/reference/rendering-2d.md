# API Reference — 2D Rendering

> **STATUS: WRITTEN** — work order **D9** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/renderer/Renderer2D.h`,
`renderer/RenderPass.h`, `graphics/SubTexture2D.h`, `graphics/Font.h`,
`renderer/Light2DRenderer.h`.

**Read first:** the client guide chapters
[`../guide/rendering-2d.md`](../guide/rendering-2d.md) (the immediate-mode half — batching
narrative, worked examples, the pitfall list) and
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md) (the component half —
`SpriteRendererComponent`, `TilemapComponent`, `Light2DComponent`, the painter list). **This
chapter does not repeat them.** It is the per-call lookup behind them: signature, exact behaviour,
flush trigger, failure mode. Systems explainer:
[rendering-2d](../systems/rendering-2d.md) *(skeleton — D28)*.

**Owned elsewhere, linked not restated:** the reserved GL binding registry
(`renderer/BindingPoints.h`), [`Material::Clone`](graphics-resources.md#materialclone), and the
material read-at-flush semantics all live in
[graphics-resources.md](graphics-resources.md) — the
[`BindingPoints` table](graphics-resources.md#bindingpoints) is the canonical registry. `Texture2D`,
`Shader` and `Material` factories and their failure modes are also D8's.

---

## Configuration and lifetime

Every class in this chapter ships in **both** engine configurations. None of these headers is
fenced in `Cosmic.h`, and `Cosmic/CMakeLists.txt`'s 2D `list(FILTER)` block removes none of their
`.cpp` files — `Renderer2D`, `RenderPass`, `SubTexture2D`, `Font` and `Light2DRenderer` are shared
source. Background: [build-2d-3d-split](../systems/build-2d-3d-split.md).

`Renderer2D` is an **all-static service over one file-scope `Renderer2DData s_Data`**
(`Renderer2D.cpp:184`). There is no instance to own, no handle to pass around, and no reentrancy:
one process-wide batch state, one pass stack. `Renderer::Init()` calls `Renderer2D::Init()` and
`Renderer::Shutdown()` calls `Renderer2D::Shutdown()` (`Renderer.cpp:26`, `:38`), so an
`Application` host never calls either. `Light2DRenderer` is likewise all-static, over a
function-local singleton (`Light2DRenderer.cpp:21`), and `Renderer::Shutdown` releases it too
(`Renderer.cpp:42`).

---

## The batching contract

Read this before any entry below: every per-call "flushes when…" note is an instance of it.

A `Draw*` call **writes vertices into a host array and returns**. Nothing reaches the GPU until a
*flush*, which uploads each non-empty batch and issues one draw call for it. There are four
independent batches — quads, lines, circles, text — and two bypass pipelines (instanced quads,
instanced circles).

### Limits — all compile-time, none configurable

`static const uint32_t` members of the file-local `Renderer2DData`:

| Constant | Value | Declared | What it bounds |
| --- | ---: | --- | --- |
| `MaxQuads` | 10 000 | `Renderer2D.cpp:63` | quads per batch (via `MaxIndices = 60 000`, `:65`) |
| `MaxTextureSlots` | 32 | `:66` | sampler units; slot 0 is permanently the 1×1 white texture, so **31 distinct textures** per batch |
| `MaxLines` | 10 000 | `:68` | lines per batch (via `MaxLineVertices = 20 000`, `:69`) |
| `MaxCircles` | 10 000 | `:71` | SDF circles per batch (via `MaxCircleIndices = 60 000`, `:73`) |
| `MaxTextQuads` | 10 000 | `:119` | **visible glyphs** per batch (via `MaxTextIndices = 60 000`, `:121`) |
| `MaxInstancedQuads` | 20 000 | `:150` | instances **per uploaded chunk** — the call itself is unbounded |
| `MaxInstancedCircles` | 20 000 | `:76` | instances **per uploaded chunk** — the call itself is unbounded |

Host staging is allocated once in `Init` regardless of what a project draws: 40 000 `QuadVertex`
(44 B), 20 000 `LineVertex` (28 B), 40 000 `CircleVertex` (44 B), 40 000 `TextVertex` (36 B) —
about 5.5 MB, mirrored by the GPU buffers.

### What happens at each limit — always the same thing

Each batched primitive checks its own limit **before** writing, calls the private `FlushAndReset()`,
then writes into the fresh batch. **No limit logs, warns, drops geometry or fails.** A flush is
invisible except in `Statistics::DrawCalls`.

| Limit | Check site | Note |
| --- | --- | --- |
| `MaxIndices` | `:816 :853 :895 :928 :962 :993 :1041 :1070` | one check per quad overload |
| `MaxTextureSlots` | `:799`, inside `ResolveTextureSlot` | the new texture takes slot 1 of the fresh batch |
| `MaxLineVertices` | `:1238` | written `>= MaxLineVertices - 1`; the counter moves in steps of 2 from 0, so the `- 1` never changes the outcome — the real ceiling is exactly 10 000 lines |
| `MaxCircleIndices` | `:1123` | |
| `MaxTextIndices` | `:1185`, **inside the glyph loop** | flushes mid-string; the atlas is re-installed at `:1188` because `FlushAndReset` nulls it |
| instanced chunks | `:1321-1343`, `:1421-1443` | a `while (remaining)` loop — cannot overrun |

### The state-driven flushes

These are the ones that surprise people, because volume has nothing to do with them.

| Trigger | Check site | Note |
| --- | --- | --- |
| **Material identity changes** | `:815 :852 :887 :927 :961 :992 :1033 :1069` | every non-material overload flushes when `CurrentMaterial != DefaultMaterial`; every material overload flushes when `CurrentMaterial != material`. **Pointer comparison** — two materials with identical contents are two batches |
| **Circle custom shader changes** | `:1116` | including switching back to the default |
| **Text font atlas changes** | `:1165` | keyed on the atlas's renderer ID |
| **`DrawInstancedQuads` / `DrawInstancedCircles`** | `:1289`, `:1374` | flush up front, unconditionally |
| **`PushRenderPass` / `PopRenderPass`** | `:514-521`, `:569-576` | only when something is pending |

### Draw order inside one flush is fixed

`Flush()` emits **quads → lines → circles → text** (`:647`, `:675`, `:689`, `:711`) regardless of
call order. Submission order is preserved *within* a primitive type only; across types a line always
draws over a quad and text always draws over everything.

> `Renderer2D.h:89-92`'s comment says "batched geometry is rasterized in submission order". That is
> true per-type and **misleading across types**. The same comment's reference to `Scene::OnRender`
> sorting sprites by `Position.z` describes a method with **zero callers** — `Scene::OnRenderSprites`
> superseded it and sorts by `ZOrder` first (see [`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md#control-what-draws-in-front)).

### Z and transparency

`z` sorts nothing here. It only matters when a depth test is active — which it is inside
`Scene::OnRenderSprites` (depth test on, depth **write off**, straight alpha, `Scene.cpp:618-622`)
and is not inside a bare `BeginScene` block unless you configured it. For back-to-front alpha in
raw `Renderer2D` code, order your calls.

---

## `Renderer2D` — lifecycle

Declared in `Cosmic/src/renderer/Renderer2D.h`. All members are `static`; the class is never
instantiated.

### `Renderer2D::Init`

```cpp
static void Init();
```

**What it does** — allocates the four host staging buffers, builds the shared quad index buffer, the
four batch VAOs and the two instancing VAOs, creates the 1×1 white texture that owns slot 0, and
loads six shaders: `Texture.glsl`, `Line.glsl`, `Circle.glsl`, `Text.glsl`, `CircleInstance.glsl`,
`QuadInstance.glsl`. It also creates `Cosmic_Default_Material` over `Texture.glsl` — the sentinel
every non-material draw compares against.

**Why you'd use it** — you almost never call it. `Renderer::Init()` does (`Renderer.cpp:26`), and
`Application` calls that. Call it directly only in a headless/offscreen harness that drives GL
itself, as `tests/render/render_main.cpp:100` does.

**Example**

```cpp
// Only in a custom host that owns the GL context itself.
Cosmic::RenderCommand::Init();
Cosmic::Renderer2D::Init();
```

**Notes & pitfalls**
- **Shader paths are literal, not VFS.** `Shader::Create("assets/shaders/Texture.glsl")` is resolved
  relative to the process working directory, not through `FileSystem::Resolve`.
- **Failure is per-shader and mostly non-fatal.** A failed `Line.glsl`, `Circle.glsl`, `Text.glsl`,
  `CircleInstance.glsl` or `QuadInstance.glsl` logs `CS_CORE_ERROR` and `Init` continues; the
  corresponding batch then silently draws nothing (`Flush` guards text on a null shader; the line
  and circle paths would dereference a null `Ref` at flush time). Only `Texture.glsl` is guarded by
  `CS_CORE_ASSERT` (`:238`) — **and that macro is compiled out in every configuration**, so a
  missing core shader is an unchecked null dereference at the first flush, not a diagnostic.
- Calling `Init` twice leaks the previous host buffers (raw `new[]`, no delete on the second pass).
- Requires a current GL context.

**See also** — [`Renderer2D::Shutdown`](#renderer2dshutdown), `Renderer::Init` in
[graphics-resources.md](graphics-resources.md) *(D8)*.

### `Renderer2D::Shutdown`

```cpp
static void Shutdown();
```

**What it does** — `delete[]`s the four host staging buffers, clears the render-pass stack, and
**resets every `Ref` in `s_Data`** — texture slots, white texture, VAOs, VBOs, shaders, both
materials.

**Why you'd use it** — same answer as `Init`: `Renderer::Shutdown()` calls it. The `Ref` resets are
not cosmetic — `s_Data` is a file-scope static, so leaving handles set would run
`glDeleteTextures`/`glDeleteProgram` at static-destruction time, **after** the window and GL context
are gone (`:470-478` documents the access violation this used to cause on close).

**Notes & pitfalls**
- **Call it while the GL context is still current.** That is the entire point of the function.
- Not idempotent-safe against a following draw: after `Shutdown`, every batch pointer is `nullptr`
  and any `Draw*` call writes through a null pointer.

### `Renderer2D::SetViewportSize`

```cpp
static void SetViewportSize(uint32_t width, uint32_t height);
```

**What it does** — records the tracked viewport size in `s_Data.ViewportDimensions`. It does **not**
touch GL. The tracked value is what [`BeginScene`](#renderer2dbeginscene) turns into a real
`RenderCommand::SetViewport` call, and what is uploaded as the `u_ViewportSize` uniform to the quad
and circle shaders at flush.

**Why you'd use it** — only when you drive `Renderer2D` from a host neither of the engine's two
maintainers covers. `Renderer::OnWindowResize` calls it (`Renderer.cpp:53`) and `WorkspaceLayer`
calls it every frame with the viewport-panel size (`WorkspaceLayer.cpp:92`).

**Example**

```cpp
// Rendering into an offscreen framebuffer from a plain layer.
m_Fbo->Bind();
Cosmic::Renderer2D::SetViewportSize(m_Fbo->GetWidth(), m_Fbo->GetHeight());
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
```

**Notes & pitfalls**
- **The 1280 × 720 trap.** `ViewportDimensions` initialises to `{1280, 720}` (`Renderer2D.cpp:170`).
  Under a host that never updates it, the first `BeginScene` resets the GL viewport to 1280 × 720
  and your content lands in the wrong corner. `Renderer3D::BeginScene` has no equivalent behaviour —
  it never touches the viewport (`Renderer3D.cpp:294-314`), so a 3D-only project never meets this.
- `PushRenderPass` overwrites the tracked value from its `viewportBounds`; a later `BeginScene` then
  inherits *that*, not what you last passed here.

---

## `Renderer2D` — scene and pass control

### `Renderer2D::BeginScene`

```cpp
static void BeginScene(const Camera& camera);
```

**What it does** — reads `camera.GetViewProjectionMatrix()`, resets the active circle shader to the
engine default, and forwards to `PushRenderPass` with full-window bounds derived from the tracked
viewport size: `{ 0, 0, ViewportDimensions.x, ViewportDimensions.y }` (`:622-633`). **That includes
a real `RenderCommand::SetViewport` call.**

**Why you'd use it** — the ordinary entry point for drawing 2D from a layer. Use
[`PushRenderPass`](#renderer2dpushrenderpass) instead when you have a view-projection matrix but no
`Camera` object, or [`RenderPass`](#renderpass) when you want RAII and a sub-region.

**Example**

```cpp
void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                 { 0.9f, 0.3f, 0.2f, 1.0f });
    Cosmic::Renderer2D::EndScene();
}
```

**Notes & pitfalls**
- The parameter is the abstract base [`Camera`](cameras.md#camera) — an `OrthographicCamera`, a
  `PerspectiveCamera` or any controller's camera all work; only `GetViewProjectionMatrix()` is read.
- **Nesting is legal and is a push, not an error.** `BeginScene` inside `BeginScene` stacks a second
  pass; there is no "scene already open" warning (`Renderer3D::BeginScene` does warn — this one does
  not).
- Drawing happens in a layer's `OnUpdate`. `Layer::OnRender()` is declared but never called by the
  engine — see [core.md](core.md) *(D6)*.
- Cannot fail. With no GL context the underlying `RenderCommand::SetViewport` is what breaks, not
  this call.

### `Renderer2D::EndScene`

```cpp
static void EndScene();
```

**What it does** — calls `PopRenderPass()` and nothing else (`:635-638`). That flushes whatever is
pending, pops the stack entry, restores the enclosing pass if there is one, and resets every batch
counter.

**Notes & pitfalls**
- **It does not restore a viewport when the stack empties.** The last pass's bounds stay in effect
  on the GL side after the final `EndScene`.
- Unbalanced `EndScene` is undefined behaviour — see [`PopRenderPass`](#renderer2dpoprenderpass).

### `Renderer2D::PushRenderPass`

```cpp
static void PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds);
```

**What it does**, in order (`:509-562`): flushes if any batch has pending geometry; pushes a
`RenderPassState` onto the internal stack; installs `viewProj` as the active matrix; calls
`RenderCommand::SetViewport` with `viewportBounds` cast to `uint32_t`; records
`ViewportDimensions = { bounds.z, bounds.w }`; then **resets every batch counter and buffer
pointer**, sets `TextureSlotIndex = 1`, clears the slot lookup, restores `CurrentMaterial` to the
default material and `ActiveCircleShader` to the default circle shader, and nulls the text atlas.

**Why you'd use it** — when you have a matrix and no camera object. `Scene::OnRenderSprites` uses
exactly this (`Scene.cpp:624`). In ordinary layer code prefer [`RenderPass`](#renderpass), which
cannot leak an unmatched push.

**Example**

```cpp
// Draw an overlay with a matrix you built yourself.
const glm::mat4 vp = glm::ortho(0.0f, (float)w, 0.0f, (float)h);
Cosmic::Renderer2D::PushRenderPass(vp, { 0.0f, 0.0f, (float)w, (float)h });
Cosmic::Renderer2D::DrawRect({ 4.0f, 4.0f, 0.0f }, { 100.0f, 20.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
Cosmic::Renderer2D::PopRenderPass();
```

**Notes & pitfalls**
- `viewportBounds` is `{ x, y, width, height }` in pixels **from the bottom-left** (GL convention).
- **Negative bounds are not validated.** All four components are cast to `uint32_t` (`:533-538`), so
  `x = -10` becomes 4 294 967 286 and the GL viewport call is garbage. Clamp before you push.
- Every push **resets every counter**, so geometry can never leak across a pass boundary — but a
  quad submitted and not yet flushed before a push *is* flushed by the push, under the old matrix.
- The stack has no depth limit.

### `Renderer2D::PopRenderPass`

```cpp
static void PopRenderPass();
```

**What it does** — flushes pending geometry, `pop_back()`s the stack, and — **only if the stack is
still non-empty** — restores the previous pass's matrix and re-issues its `SetViewport`. It then
resets every batch counter exactly as `PushRenderPass` does.

**Notes & pitfalls**
- **An unmatched pop is undefined behaviour with no diagnostic.** The guard is
  `CS_CORE_ASSERT(!s_Data.RenderPassStack.empty(), …)` at `:566`, and `CS_CORE_ASSERT` is gated on
  `GLCORE_DEBUG || CS_DEBUG` — **neither is defined in any configuration**, so the macro compiles to
  nothing everywhere and the call becomes `pop_back()` on an empty `std::vector`. Use
  [`RenderPass`](#renderpass) and it cannot happen.
- **No viewport restore when the stack empties.** Deliberate asymmetry with the push.
- The circle shader and current material are reset to the engine defaults, not to the enclosing
  pass's values — a custom circle shader does not survive a nested pass.

### `Renderer2D::Flush`

```cpp
static void Flush();
```

**What it does** — uploads and draws each non-empty batch in the fixed order quads → lines → circles
→ text (`:644-726`). For quads it binds `TextureSlots[0 .. TextureSlotIndex)`, binds
`CurrentMaterial` (or `Texture.glsl` when null) and uploads `u_ViewProjection` and `u_ViewportSize`.
For text it binds the font atlas to unit 0 and sets `u_FontAtlas`. Each non-empty batch increments
`DrawCalls` by one.

**Why you'd use it** — **you would not.** It is public but is an internal step of
`FlushAndReset`/push/pop. Nothing in the engine, the sample projects or the tests calls it from
outside `Renderer2D`.

**Notes & pitfalls**
- **`Flush()` does not reset counters or buffer pointers.** Only the private `FlushAndReset()` and
  the pass push/pop do. A client call therefore draws the same geometry **twice**:

  ```cpp
  Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, red);
  Cosmic::Renderer2D::Flush();      // draws the quad
  Cosmic::Renderer2D::EndScene();   // PopRenderPass still sees it pending -> draws it AGAIN
  ```

  …and inflates `DrawCalls` and every primitive counter by the duplicated batches. If you need a
  mid-frame fence, open a nested [`RenderPass`](#renderpass).
- The text batch additionally requires a non-null atlas **and** a non-null `TextShader` (`:711`); if
  `Text.glsl` failed to load, text is silently skipped rather than crashing. The line and circle
  batches have no such guard.

---

## `Renderer2D` — quads

Sixteen entry points: four payloads (colour, texture, sub-texture, material) × `vec2`/`vec3`
position × unrotated/rotated. Every `vec2` form is a forwarder that inserts `z = 0`. `position` is
always the quad's **centre**; `size` is always its **full** width and height in world units.

### `Renderer2D::DrawQuad` — flat colour, `vec3` position

```cpp
static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
    const glm::vec4& color);
```

**What it does** — appends four vertices for an axis-aligned quad centred at `position`, with
`a_Color = color`, `TexIndex = 0` (the white texture) and `TilingFactor = 1`.

**Why you'd use it** — solid rectangles: backgrounds, bars, debug boxes. For a textured quad use the
[texture overload](#renderer2ddrawquad--texture-vec3-position); for your own shader use the
[material overload](#renderer2ddrawquad--material-vec3-position).

**Example**

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f }, { 0.9f, 0.3f, 0.2f, 1.0f });
```

**Notes & pitfalls**
- Flushes first when `CurrentMaterial != DefaultMaterial` (`:815`) or the quad batch is full
  (`:816`). Cannot fail otherwise.
- `color.a < 1` blends against whatever is already in the target; nothing sorts for you.

### `Renderer2D::DrawQuad` — flat colour, `vec2` position

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
    const glm::vec4& color);
```

**What it does** — forwards to the `vec3` form with `z = 0` (`:834-837`). Identical in every other
respect.

**Example**

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 2.0f, 1.0f }, { 0.9f, 0.3f, 0.2f, 1.0f });
```

### `Renderer2D::DrawQuad` — texture, `vec3` position

```cpp
static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
    const Ref<Texture>& texture,
    float tilingFactor = 1.0f,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — resolves `texture` to a slot in the active batch (reusing the slot if the same
renderer ID is already bound, flushing first if all 32 slots are taken), then appends four vertices
with UVs `(0,0)`→`(1,1)`, `a_Color = tintColor` and `a_TilingFactor = tilingFactor`. The shader
multiplies the sample by the tint.

**Why you'd use it** — the default way to put an image on screen from a layer. For one tile of a
sprite sheet use the [sub-texture overload](#renderer2ddrawquad--sub-texture-vec3-position), which
keeps every tile of the sheet in one slot.

**Example**

```cpp
Cosmic::Ref<Cosmic::Texture2D> tex =
    Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://assets/sprite.png"));

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tex);
Cosmic::Renderer2D::DrawQuad({ 2.0f, 0.0f, 0.0f }, { 4.0f, 4.0f }, tex, 4.0f);   // 4x UV tiling
Cosmic::Renderer2D::DrawQuad({ 6.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tex, 1.0f,
                             { 1.0f, 0.5f, 0.5f, 1.0f });                        // tint
```

**Notes & pitfalls**
- **Failure: a null `texture` logs `CS_CORE_WARN("Renderer2D: DrawQuad received null texture.
  Falling back to white.")` and re-dispatches to the flat-colour overload with `tintColor`**
  (`:845-850`). It never crashes and never draws nothing. This is the *only* quad overload with that
  guard — the sub-texture forms do not have it.
- A *degraded* texture (`Texture2D::Create` on a missing file returns a non-null 0×0 object — see
  [graphics-resources.md](graphics-resources.md), D8) is not null, so it passes the guard, consumes
  a slot and samples black.
- The parameter is `Ref<Texture>`, the base type; `Ref<Texture2D>` converts implicitly.
- `tilingFactor` is only meaningful when the texture's wrap mode repeats.
- Flushes when the material is not the default (`:852`), the quad batch is full (`:853`), or the
  texture needs slot 32 (`:799`).

### `Renderer2D::DrawQuad` — texture, `vec2` position

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
    const Ref<Texture>& texture,
    float tilingFactor = 1.0f,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — forwards to the `vec3` form with `z = 0` (`:874-877`), including the null-texture
guard.

**Example**

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, tex);
```

### `Renderer2D::DrawQuad` — sub-texture, `vec3` position

```cpp
static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
    const Ref<SubTexture2D>& subTexture,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — takes the parent atlas out of `subTexture`, resolves it to a batch slot, and
appends four vertices using the sub-texture's stored UV corners instead of `(0,0)`→`(1,1)`.

**Why you'd use it** — sprite sheets and tile atlases. Every tile of one sheet shares a single
texture slot, so a screenful of tiles from one atlas is typically one draw call.

**Example**

```cpp
Cosmic::Ref<Cosmic::SubTexture2D> tile =
    Cosmic::SubTexture2D::CreateFromCoords(atlas, { 2.0f, 0.0f }, { 64.0f, 64.0f });

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tile);
Cosmic::Renderer2D::DrawQuad({ 1.5f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tile,
                             { 1.0f, 1.0f, 1.0f, 0.5f });   // half-transparent
```

**Notes & pitfalls**
- **Failure: there is no null check.** `subTexture->GetTexture()` is dereferenced immediately
  (`:930`), and `ResolveTextureSlot` then dereferences the parent texture. A null `Ref<SubTexture2D>`
  — or one built over a null atlas — is an access violation, not a warning. Guard your own pointers.
- **No `tilingFactor` parameter, by design.** Tiling an atlas tile would bleed into its neighbours,
  so `a_TilingFactor` is hard-set to `1.0f` (`:943`).
- Flushes on material mismatch (`:927`), a full quad batch (`:928`) or slot exhaustion (`:799`).

### `Renderer2D::DrawQuad` — sub-texture, `vec2` position

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
    const Ref<SubTexture2D>& subTexture,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — forwards to the `vec3` form with `z = 0` (`:950-953`). Also has no null check.

**Example**

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, tile);
```

### `Renderer2D::DrawQuad` — material, `vec3` position

```cpp
static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
    const Ref<Material>& material);
```

**What it does** — flushes if `material` is not the currently-batched one, makes it current, then
reads **two specially-named uniforms out of the material at submit time** (`:890-893`) and bakes
them into the quad:

| Material key | Read as | Missing ⇒ |
| --- | --- | --- |
| `u_Texture` (`GetTexture`) | the quad's texture, resolved into a batch slot | the 1×1 white texture |
| `u_Color` (`GetVector4`) | the quad's `a_Color` vertex attribute | `glm::vec4(1.0f)` — opaque white (`Material.cpp:134`) |

Everything else in the material's uniform cache is uploaded once by `Material::Bind()` **at flush**.

**Why you'd use it** — a quad that needs your own fragment shader: a fire effect, a distortion, a
custom SDF. For a plain image the [texture overload](#renderer2ddrawquad--texture-vec3-position) is
strictly cheaper.

**Example**

```cpp
auto shader = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
if (!shader) { CS_ERROR("Fire.glsl failed to compile"); return; }

auto material = Cosmic::Material::Create(shader, "Fire");
material->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
material->Set("u_Time", GetLocalTime());

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f }, material);
```

**Notes & pitfalls**
- **Failure: a null `material` is a silent no-op** — `if (!material) return;` at `:885`, no log, no
  geometry.
- **`u_Color` varies per quad; every other uniform does not.** Two quads sharing one material in one
  batch get their own colours (already in the vertex data) but both see the *last* value set for any
  other uniform, because the rest is read at flush. `Material::Clone` is the fix — the semantics are
  stated once in [graphics-resources.md](graphics-resources.md#materialclone).
- **Interleaving material and non-material quads costs one draw call per quad**, because the
  material identity check fires on every call in both directions. Sort your draw list by material.
- `a_TilingFactor` is hard-set to `1.0f`; the material path has no tiling parameter.

### `Renderer2D::DrawQuad` — material, `vec2` position

```cpp
static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
    const Ref<Material>& material);
```

**What it does** — forwards to the `vec3` form with `z = 0` (`:916-919`), including the silent
null-material return.

**Example**

```cpp
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 2.0f, 2.0f }, material);
```

---

## `Renderer2D` — rotated quads

Identical to the four payloads above, plus a `float rotation` **in radians**, applied about the
quad's centre on the Z axis. The transform becomes `translate × rotate × scale` instead of
`translate × scale` — four extra multiplies per vertex, no extra GPU cost and no separate batch.

### `Renderer2D::DrawRotatedQuad` — flat colour, `vec3` position

```cpp
static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
    float rotation, const glm::vec4& color);
```

**What it does** — the rotated twin of the flat-colour quad (`:959-979`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                    glm::radians(45.0f), { 1.0f, 1.0f, 0.0f, 1.0f });
```

**Notes & pitfalls**
- `rotation` is **radians**. Passing degrees is the classic silent bug — 45 radians is ≈ 2578°.
- Same flush triggers as the unrotated form (`:961`, `:962`).

### `Renderer2D::DrawRotatedQuad` — flat colour, `vec2` position

```cpp
static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
    float rotation, const glm::vec4& color);
```

**What it does** — forwards with `z = 0` (`:981-984`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Angle, white);
```

### `Renderer2D::DrawRotatedQuad` — texture, `vec3` position

```cpp
static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
    float rotation, const Ref<Texture>& texture,
    float tilingFactor = 1.0f,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — the rotated twin of the textured quad (`:990-1013`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f },
                                    glm::radians(30.0f), tex);
```

**Notes & pitfalls**
- **Failure: unlike the unrotated texture overload, this one has NO null-texture guard.** It calls
  `ResolveTextureSlot(texture)` directly at `:995`, which dereferences `texture->GetRendererID()`.
  A null `Ref` crashes. The asymmetry is real and is not documented in the header.
- Same flush triggers otherwise (`:992`, `:993`, `:799`).

### `Renderer2D::DrawRotatedQuad` — texture, `vec2` position

```cpp
static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
    float rotation, const Ref<Texture>& texture,
    float tilingFactor = 1.0f,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — forwards with `z = 0` (`:1015-1018`). Inherits the missing null guard.

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Angle, tex, 1.0f, tint);
```

### `Renderer2D::DrawRotatedQuad` — sub-texture, `vec3` position

```cpp
static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
    float rotation,
    const Ref<SubTexture2D>& subTexture,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — the rotated twin of the sub-texture quad (`:1067-1091`). This is the overload
`Scene::OnRenderSprites` uses for every textured sprite in a scene.

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 1.2f, 1.2f },
                                    glm::radians(15.0f), tile);
```

**Notes & pitfalls**
- **Failure: no null check** — `subTexture->GetTexture()` at `:1072`.
- No `tilingFactor` parameter; `a_TilingFactor` is hard-set to `1.0f`.

### `Renderer2D::DrawRotatedQuad` — sub-texture, `vec2` position

```cpp
static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
    float rotation,
    const Ref<SubTexture2D>& subTexture,
    const glm::vec4& tintColor = glm::vec4(1.0f));
```

**What it does** — forwards with `z = 0` (`:1093-1096`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Angle, tile);
```

### `Renderer2D::DrawRotatedQuad` — material, `vec3` position

```cpp
static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
    float rotation, const Ref<Material>& material);
```

**What it does** — the rotated twin of the material quad, with the same `u_Texture`/`u_Color`
read-at-submit rule and the same flush-on-identity-change behaviour (`:1029-1061`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f },
                                    glm::radians(10.0f), material);
```

**Notes & pitfalls**
- **Failure: a null `material` is a silent no-op** (`:1031`).
- Note the check order: the material identity flush happens *before* the batch-full check
  (`:1033` then `:1041`), same as the unrotated form.

### `Renderer2D::DrawRotatedQuad` — material, `vec2` position

```cpp
static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
    float rotation, const Ref<Material>& material);
```

**What it does** — forwards with `z = 0` (`:1024-1027`).

**Example**

```cpp
Cosmic::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f }, { 2.0f, 2.0f }, m_Angle, material);
```

---

## `Renderer2D` — circles

SDF circles rasterise a bounding quad and evaluate a signed distance field in the fragment shader,
so the edge stays smooth at any zoom. They have their **own batch** and their own shader.

### `Renderer2D::DrawCircle` — `vec3` position

```cpp
static void DrawCircle(
    const glm::vec3& position,
    const glm::vec2& size,
    const glm::vec4& color,
    float thickness,
    float fade,
    Cosmic::Ref<Cosmic::Shader> customShader = nullptr);
```

**What it does** — appends four circle vertices with local positions spanning `[-1, 1]`, plus
per-vertex `thickness` and `fade`. `size` is the **bounding quad's** full width and height, so a
circle of world radius `r` needs `size = { 2r, 2r }`; a non-uniform `size` gives an ellipse.
`thickness` is the ring wall as a fraction of the radius (`1.0` = filled disc); `fade` is the
anti-aliased edge width (`0.005` is crisp, larger reads as glow).

**Why you'd use it** — resolution-independent discs, rings, radar sweeps, selection halos. For tens
of thousands of them use [`DrawInstancedCircles`](#renderer2ddrawinstancedcircles).

**Example**

```cpp
Cosmic::Renderer2D::DrawCircle({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f },
                               { 0.2f, 0.8f, 1.0f, 1.0f }, 1.0f,  0.005f);  // solid disc, r = 1
Cosmic::Renderer2D::DrawCircle({ 3.0f, 0.0f, 0.0f }, { 2.0f, 2.0f },
                               { 1.0f, 0.5f, 0.0f, 0.9f }, 0.05f, 0.005f);  // thin ring
```

**Notes & pitfalls**
- **No defaults on this overload.** `thickness` and `fade` are required; only the
  [`vec2` twin](#renderer2ddrawcircle--vec2-position) defaults them.
- **`customShader` breaks the batch whenever it changes** (`:1116-1120`), including switching back
  to the default. Alternating two custom circle shaders costs one draw call per circle.
- **Failure: a null `customShader` falls back to the engine `Circle.glsl`** (`:1110-1113`) — the
  documented guard against a `Ref` going out of scope during a hot reload. If `Circle.glsl` itself
  failed to load in `Init`, `Flush` re-checks and falls back again (`:696`), but with both null the
  flush dereferences a null `Ref`.
- Flushes when the circle batch is full (`:1123`).
- Circles always draw **after** quads and lines inside one flush.

### `Renderer2D::DrawCircle` — `vec2` position

```cpp
inline static void DrawCircle(
    const glm::vec2& position,
    const glm::vec2& size,
    const glm::vec4& color,
    float thickness = 1.0f,
    float fade = 0.005f,
    Cosmic::Ref<Cosmic::Shader> customShader = nullptr)
{
    DrawCircle({ position.x, position.y, 0.0f }, size, color,
        thickness, fade, customShader);
}
```

**What it does** — an **inline** header-defined forwarder (`Renderer2D.h:137-147`) inserting
`z = 0`. This is the only overload that defaults `thickness` and `fade`.

**Example**

```cpp
Cosmic::Renderer2D::DrawCircle({ 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
```

**Notes & pitfalls**
- Being `inline` in the header, this one is compiled into your DLL rather than called across the
  boundary; behaviour is otherwise identical.

---

## `Renderer2D` — lines and boxes

### `Renderer2D::DrawLine`

```cpp
static void DrawLine(const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec4& color);
```

**What it does** — appends two vertices to the line batch. Lines have their own shader
(`Line.glsl`) and are drawn **non-indexed** through `RenderCommand::DrawLines` (`glDrawArrays`).

**Why you'd use it** — debug overlays, grids, collider outlines, plot traces in world space.

**Example**

```cpp
Cosmic::Renderer2D::DrawLine({ -5.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f },
                             { 0.4f, 0.4f, 0.4f, 1.0f });
```

**Notes & pitfalls**
- **There is no width parameter.** Line width is whatever GL's default is; the engine never calls
  `glLineWidth`.
- Cannot fail. Flushes only when the line batch is full (`:1238`).
- Lines are excluded from `Statistics::GetTotalIndexCount()` on purpose, because they are
  non-indexed (`Renderer2D.h:186-188`).
- Inside one flush a line always draws **over** a quad and **under** a circle.

### `Renderer2D::DrawRect`

```cpp
static void DrawRect(const glm::vec3& position, const glm::vec2& size,
    const glm::vec4& color);
```

**What it does** — emits four `DrawLine` calls around a **centre-anchored** box (`:1252-1263`). It
is a wireframe outline, not a filled quad.

**Example**

```cpp
// A 2 x 1 outline centred on the origin.
Cosmic::Renderer2D::DrawRect({ 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f });
```

**Notes & pitfalls**
- Four lines, four `LineCount` ticks — not one.
- A rectangle submitted right at the line limit can be **split across two draw calls**; visually
  identical, but it shows up as an extra `DrawCalls` tick.
- For a filled box use [`DrawQuad`](#renderer2ddrawquad--flat-colour-vec3-position).

---

## `Renderer2D` — world-space text

### `Renderer2D::DrawString` — transform form

```cpp
static void DrawString(const std::string& text, const Ref<Font>& font,
    const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f),
    float kerning = 0.0f, float lineSpacing = 0.0f);
```

**What it does** — lays `text` out in **em units** (1 em = the transform's unit scale) and appends
one quad per *visible* glyph to the text batch, each vertex transformed by `transform`. The
**baseline of the first line sits at the transform origin**; `\n` resets the pen and advances
`-lineAdvance` in Y, where `lineAdvance = font->LineHeight() + lineSpacing`. `\r` is skipped.
`kerning` is added to every glyph advance.

**Why you'd use it** — text that lives in the world and scales with the camera: floating damage
numbers, labels on machinery, signage. For panel text use `Cosmic::UI::Fonts` and ImGui
([ui.md](ui.md), D18); for canvas UI text use `UiTextComponent`
([`../guide/game-ui.md`](../guide/game-ui.md)), which calls this under the hood.

**Example**

```cpp
Cosmic::Ref<Cosmic::Font> font = Cosmic::Font::Get("Roboto-Bold");
if (!font) font = Cosmic::Font::Default();

// y-down canvas space: flip with a negative Y scale (the UiSystem pattern).
glm::mat4 transform = glm::translate(glm::mat4(1.0f), { 0.0f, 3.0f, 0.0f })
                    * glm::scale(glm::mat4(1.0f), { 0.5f, -0.5f, 1.0f });
Cosmic::Renderer2D::DrawString("SCORE 1200", font, transform, { 1.0f, 1.0f, 1.0f, 1.0f });
```

**Notes & pitfalls**
- **Failure is defensive and silent**: a null `font`, an empty `text`, or a font whose atlas is null
  all return without drawing and without logging (`:1159-1162`).
- **A codepoint with no glyph falls back to `?`**, and is skipped entirely if `?` is missing too
  (`:1179-1180`).
- **ASCII only, byte by byte.** Each `char` is cast to `unsigned char` and looked up directly
  (`:1175`), and `Font` only bakes codepoints 32–126. A UTF-8 multi-byte sequence therefore renders
  as two or three `?` glyphs, not one.
- **Whitespace advances the pen but emits no quad**, so it does not count against `MaxTextQuads`
  and does not increment `QuadCount`.
- **Every visible glyph increments `Statistics::QuadCount`** (`:1217`) — a scene with on-screen text
  reports more "quads" than it has sprites.
- **Switching fonts mid-pass breaks the batch** (`:1165`, keyed on the atlas's renderer ID). Two
  `Font` objects over the same TTF are two `Texture2D`s and still break. Group `DrawString` calls by
  font.
- Text always draws **last** inside a flush, over everything else.
- The glyph loop can flush **mid-string** at `MaxTextIndices` and re-installs the atlas afterwards
  (`:1185-1189`).

### `Renderer2D::DrawString` — position + size form

```cpp
static void DrawString(const std::string& text, const Ref<Font>& font,
    const glm::vec2& position, float size, const glm::vec4& color = glm::vec4(1.0f),
    float kerning = 0.0f, float lineSpacing = 0.0f);
```

**What it does** — builds `translate(position, 0) × scale(size, size, 1)` and forwards to the
transform form (`:1224-1230`). `size` is the em height in world units — the cap-to-cap height of the
face, not the pixel size.

**Example**

```cpp
Cosmic::Renderer2D::DrawString("READY", font, { -2.0f, 0.0f }, 0.5f);
```

**Notes & pitfalls**
- No rotation and **no Y flip** — this form draws y-up, so use it for world text and the transform
  form for canvas text.
- Inherits every note from the transform form.

---

## `Renderer2D` — instanced pipelines

Both instanced entry points **bypass the batch entirely**: they flush everything pending, stream a
flat array to a per-instance VBO, and issue one `glDrawElementsInstanced` per chunk.

### `Renderer2D::DrawInstancedQuads`

```cpp
static void DrawInstancedQuads(const InstanceQuadData* instances,
    uint32_t count,
    Ref<Shader> customShader = nullptr);
```

**What it does** — calls `FlushAndReset()` unconditionally, binds `QuadInstance.glsl` (or
`customShader`), uploads `u_ViewProjection`, binds the white texture to unit 0, then loops uploading
at most `MaxInstancedQuads` (20 000) instances at a time and drawing each chunk. On exit it restores
`CurrentMaterial` to the default material so the next `DrawQuad` does not trigger a spurious flush.

**Why you'd use it** — when the data is **already in an array** and the count runs into the tens of
thousands, or when you need per-instance vertex attributes a batched quad cannot express. Below a
few thousand objects the batcher wins: batched `DrawQuad` costs one draw call per 10 000 quads,
this costs one per 20 000, and the up-front flush is pure overhead at small counts.

**Example**

```cpp
std::vector<Cosmic::Renderer2D::InstanceQuadData> buf;
buf.reserve(balls.size());

for (const Ball& b : balls)
    buf.push_back({ b.Position,
                    { b.Radius * 2.0f, b.Radius * 2.0f },
                    b.Color,
                    { 0.0f, 0.0f },   // TexCoordOffset — solid colour
                    { 1.0f, 1.0f },   // TexCoordScale  — solid colour
                    0.0f,             // TexIndex       — the white texture
                    1.0f });          // TilingFactor

if (!buf.empty())
    Cosmic::Renderer2D::DrawInstancedQuads(buf.data(), (uint32_t)buf.size());
```

**Notes & pitfalls**
- **`TexIndex != 0` samples stale state.** The call binds *only* the white texture to slot 0
  (`:1408`) and **never populates `TextureSlots`** — `ResolveTextureSlot` is not on this path at all.
  Any non-zero `TexIndex` reads whatever an earlier batch happened to leave bound in that unit. If
  you need textured instances, bind them yourself before the call.
- **Failure modes, in order:** `instances == nullptr` or `count == 0` returns immediately (`:1366`);
  a null `customShader` **and** a failed `QuadInstance.glsl` logs `CS_CORE_ERROR` and returns
  (`:1385-1390`) — *after* the flush has already happened.
- **Safe at any `count`.** 500 000 instances is 25 chunks, 25 draw calls, no reallocation.
- **A pass must be active.** It reads `s_Data.ViewProjectionMatrix`, which is only meaningful
  between `BeginScene`/`EndScene` or a push/pop pair.
- A `customShader` gets the 32-entry `u_Textures` sampler array uploaded on **every call**
  (`:1398-1404`); the default shader had it uploaded once in `Init` (`:432-433`).
- Stats: `+1` `DrawCalls` and `+batchSize` `QuadCount` per chunk (`:1438-1439`).

### `Renderer2D::DrawInstancedCircles`

```cpp
static void DrawInstancedCircles(const InstanceCircleData* instances,
    uint32_t count,
    Ref<Shader> customShader = nullptr);
```

**What it does** — the circle twin: flush, bind `CircleInstance.glsl` (or `customShader`), upload
`u_ViewProjection`, then chunk at `MaxInstancedCircles` (20 000). On exit it restores **both**
`ActiveCircleShader` to the default circle shader and `CurrentMaterial` to the default material
(`:1355-1356`).

**Why you'd use it** — particle-like discs, dot plots, agent swarms: thousands of SDF circles that
already live in an array.

**Example**

```cpp
std::vector<Cosmic::Renderer2D::InstanceCircleData> buf;
for (const Agent& a : agents)
    buf.push_back({ a.Position, { a.R * 2.0f, a.R * 2.0f }, a.Color, 1.0f, 0.005f });

if (!buf.empty())
    Cosmic::Renderer2D::DrawInstancedCircles(buf.data(), (uint32_t)buf.size());
```

**Notes & pitfalls**
- **Failure:** null pointer or `count == 0` returns immediately (`:1281`); no shader at all logs
  `CS_CORE_ERROR` and returns after the flush (`:1300-1305`).
- Binds **no texture at all** — the instanced circle shader is procedural.
- Stats: `+1` `DrawCalls` and `+batchSize` `CircleCount` per chunk (`:1338-1339`).
- Same "a pass must be active" and "safe at any count" rules as the quad path.

---

## `Renderer2D` — statistics

### `Renderer2D::Statistics` *(nested struct)*

```cpp
struct Statistics
{
    uint32_t DrawCalls = 0;
    uint32_t QuadCount = 0;
    uint32_t CircleCount = 0;
    uint32_t LineCount = 0;

    uint32_t GetTotalVertexCount() const { return QuadCount * 4 + CircleCount * 4 + LineCount * 2; }
    // Quads and SDF circles each emit 6 indices (two triangles). Lines are
    // non-indexed (glDrawArrays) and therefore contribute nothing here.
    uint32_t GetTotalIndexCount()  const { return (QuadCount + CircleCount) * 6; }
};
```

**What it is** — a plain value struct, returned by copy from `GetStats()`. Counters are **cumulative
until something calls `ResetStats()`** — and nothing in the engine does.

| Field | Incremented by |
| --- | --- |
| `DrawCalls` | one per **non-empty batch** inside each `Flush()` (so a pass with quads + lines + text pending reports 3), plus one per instanced chunk |
| `QuadCount` | one per quad from any quad overload, **one per visible text glyph**, `+batchSize` per instanced-quad chunk |
| `CircleCount` | one per batched circle, `+batchSize` per instanced-circle chunk |
| `LineCount` | one per `DrawLine` — so a `DrawRect` is 4 |

`GetTotalIndexCount()` deliberately excludes lines because they go through `glDrawArrays`.

### `Renderer2D::SetStatsStatus`

```cpp
static void SetStatsStatus(bool enabled);
```

**What it does** — sets the single `s_Data.StatsEnabled` flag. Every counter increment in
`Renderer2D.cpp` is guarded by `if (s_Data.StatsEnabled)`.

**Why you'd use it** — arm the counters once, in `OnAttach`. Leave them off in a shipping build:
each increment is a branch in the hot path.

**Example**

```cpp
void MyLayer::OnAttach() { Cosmic::Renderer2D::SetStatsStatus(true); }
```

**Notes & pitfalls**
- > **`StatsEnabled` defaults to `false`** (`Renderer2D.cpp:176`) **and nothing in the engine arms
  > it for you.** `GetStats()` returns all zeros until you call this. The only in-tree caller is
  > `StarforgeApp`, inside `#ifdef COSMIC_2D_ONLY` (`StarforgeApp.cpp:1223`) — a 3D editor build
  > never arms them either. This shipped once as a permanently-zero stats chip in the editor.
- Toggling it mid-frame yields a partial count for that frame, not a wrong one.

### `Renderer2D::ResetStats`

```cpp
static void ResetStats();
```

**What it does** — `memset`s the `Statistics` struct to zero (`:1271`). It runs **regardless of
`StatsEnabled`**.

**Why you'd use it** — call it once per frame, *before* the pass, to get per-frame numbers.

**Example**

```cpp
void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::ResetStats();          // counters are cumulative — this is on you
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    /* draws */
    Cosmic::Renderer2D::EndScene();
}
```

**Notes & pitfalls**
- **Nothing in the engine calls it.** Miss it and every counter is a lifetime total.
- `Renderer3D`'s counters behave differently again — always on, and also never reset by the engine.
  See [rendering-3d.md](rendering-3d.md) *(D10)*.

### `Renderer2D::GetStats`

```cpp
static Statistics GetStats();
```

**What it does** — returns a **copy** of the current counters. Read it after `EndScene`, not before.

**Example**

```cpp
void MyLayer::OnImGuiRender()
{
    const auto s = Cosmic::Renderer2D::GetStats();
    ImGui::Text("Draw calls: %u", s.DrawCalls);
    ImGui::Text("Quads:      %u  (text glyphs included)", s.QuadCount);
    ImGui::Text("Vertices:   %u", s.GetTotalVertexCount());
}
```

**Notes & pitfalls**
- Returns zeros unless [`SetStatsStatus(true)`](#renderer2dsetstatsstatus) was called.
- Cannot fail; safe before `Init` (returns a zeroed struct).

---

## `Renderer2D` — nested data types

### `Renderer2D::RenderPassState`

```cpp
struct RenderPassState
{
    glm::mat4 ViewProjectionMatrix{ 1.0f };
    glm::vec4 ViewportBounds{ 0.0f, 0.0f, 1280.0f, 720.0f };
};
```

**What it is** — one entry of the internal pass stack. It is public in the header but **no public
API accepts or returns one**: `PushRenderPass` takes the two members separately and the stack itself
is private. Treat it as documentation of the pass state rather than a type you construct. Note the
`1280 × 720` default, which is where the tracked-viewport trap comes from.

### `Renderer2D::InstanceQuadData`

```cpp
struct InstanceQuadData
{
    glm::vec3 Position;           // World-space centre of the quad
    glm::vec2 Scale;              // Full width and height in world units
    glm::vec4 Color;              // RGBA tint (multiplied with texture sample)
    glm::vec2 TexCoordOffset;     // Normalised UV origin  (atlas support)
    glm::vec2 TexCoordScale;      // Normalised UV extent  (atlas support)
    float     TexIndex;           // u_Textures[] slot index (0 = white)
    float     TilingFactor;       // UV tiling multiplier
};

static_assert(sizeof(InstanceQuadData) == 60,
    "InstanceQuadData must be exactly 60 bytes (15 floats) to match "
    "the QuadInstance.glsl attribute stride.");
```

**What it is** — the per-instance vertex payload for
[`DrawInstancedQuads`](#renderer2ddrawinstancedquads). It maps 1:1 onto `QuadInstance.glsl`
attribute locations 1–7 (location 0 is the shared unit-quad geometry), 60 bytes, enforced by the
`static_assert`. For a flat-colour instance: `TexIndex = 0`, `TexCoordOffset = {0,0}`,
`TexCoordScale = {1,1}`, `TilingFactor = 1`.

**Notes & pitfalls**
- Aggregate-initialise it in field order; there is no constructor and no defaults, so a
  brace-init that omits fields leaves them value-initialised, and a `push_back` of an
  uninitialised struct uploads garbage.
- `TexIndex` is only honoured if you bound the texture yourself — see the pitfall on
  [`DrawInstancedQuads`](#renderer2ddrawinstancedquads).

### `Renderer2D::InstanceCircleData`

```cpp
struct InstanceCircleData
{
    glm::vec3 Position;
    glm::vec2 Scale;
    glm::vec4 Color;
    float     Thickness;
    float     Fade;
};
```

**What it is** — the per-instance payload for
[`DrawInstancedCircles`](#renderer2ddrawinstancedcircles): 11 floats, 44 bytes, matching
`CircleInstance.glsl`. `Scale` is the bounding quad's full size (so `{2r, 2r}` for radius `r`);
`Thickness` and `Fade` mean exactly what they do on [`DrawCircle`](#renderer2ddrawcircle--vec3-position).

**Notes & pitfalls**
- Unlike `InstanceQuadData` there is **no `static_assert`** pinning the size to the shader stride.

---

## `RenderPass`

```cpp
class COSMIC_API RenderPass { /* … */ };
```

Declared in `Cosmic/src/renderer/RenderPass.h`, **header-only** — both the constructor and the
destructor are defined inline. An RAII scope guard over
[`Renderer2D::PushRenderPass`](#renderer2dpushrenderpass) /
[`PopRenderPass`](#renderer2dpoprenderpass). It owns no resources of its own; the stack entry it
represents lives inside `Renderer2D`.

### `RenderPass::RenderPass`

```cpp
RenderPass(const Camera& camera, const glm::vec4& viewportBounds)
{
    Renderer2D::PushRenderPass(camera.GetViewProjectionMatrix(), viewportBounds);
}
```

**What it does** — pushes a pass built from the camera's view-projection matrix and the given pixel
bounds. Everything `PushRenderPass` does, this does: flush pending geometry, install the matrix, set
the hardware viewport, reset every batch counter.

**Why you'd use it** — **always, in preference to raw push/pop.** It is the only thing that makes
the unmatched-pop UB impossible, and it makes multi-camera rendering a matter of scoping.

**Example**

```cpp
void MyLayer::OnUpdate(float ts)
{
    {   // main world view — full window
        Cosmic::RenderPass main(m_MainCamera.GetCamera(), { 0.0f, 0.0f, 1280.0f, 720.0f });
        Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, white);
    }   // flushes and restores here

    {   // minimap — a corner region
        Cosmic::RenderPass minimap(m_OverviewCamera.GetCamera(),
                                   { 900.0f, 500.0f, 380.0f, 220.0f });
        Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 8.0f, 8.0f }, blue);
    }
}
```

Target a framebuffer by binding one around the scope:

```cpp
m_SideFbo->Bind();
{
    Cosmic::RenderPass side(m_SideCamera.GetCamera(),
                            { 0.0f, 0.0f, (float)m_SideFbo->GetWidth(),
                                          (float)m_SideFbo->GetHeight() });
    Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tex);
}
m_SideFbo->Unbind();
```

**Notes & pitfalls**
- **The parameter is `const Camera&` — any camera, 2D or 3D.** `RenderPass.h`'s own prose block
  (`:60`) still documents an `OrthographicCamera` parameter and a "Defaults to a full-screen pass if
  width/height are 0" behaviour (`:88`). **Both claims are wrong**: the signature is `const Camera&`
  (`:90`) and zero bounds are passed straight through to `glViewport` as a 0 × 0 rectangle, which
  draws nothing. The code is the truth.
- Bounds are `{ x, y, width, height }` in pixels **from the bottom-left**. A 1280 × 720 window in
  quadrants: top-left `{0, 360, 640, 360}`, top-right `{640, 360, 640, 360}`, bottom-left
  `{0, 0, 640, 360}`, bottom-right `{640, 0, 640, 360}`.
- The header's rule "do NOT nest two `RenderPass` instances targeting the same viewport bounds" is
  advice, not an enforced check — nothing validates it.
- Cannot fail; `Renderer2D::Init()` must have run.

### `RenderPass::~RenderPass`

```cpp
~RenderPass()
{
    Renderer2D::PopRenderPass();
}
```

**What it does** — flushes the geometry staged under this pass and pops it, restoring the enclosing
pass's matrix and viewport if there is one.

**Notes & pitfalls**
- **It flushes — so it can issue GL calls — from a destructor.** Do not let a `RenderPass` outlive
  the GL context, and do not construct one in a scope that may be unwound by an exception during
  shutdown.
- When the stack empties, the viewport is **not** restored.

### `RenderPass` — copy and move are deleted

```cpp
RenderPass(const RenderPass&) = delete;
RenderPass& operator=(const RenderPass&) = delete;
RenderPass(RenderPass&&) = delete;
RenderPass& operator=(RenderPass&&) = delete;
```

**What it does** — makes the class non-copyable **and non-movable**, so exactly one scope owns one
stack entry. You cannot store a `RenderPass` in a container, return one from a function, or hold one
in an `std::optional` that gets reassigned. That is deliberate: any of those would decouple the pop
from the scope that pushed.

---

## `SubTexture2D`

```cpp
class COSMIC_API SubTexture2D { /* … */ };
```

Declared in `Cosmic/src/graphics/SubTexture2D.h`. A **UV rectangle plus a `Ref` to a parent
`Texture2D`** — it creates no GPU object and owns nothing but four `glm::vec2`s. That is why every
tile of one sheet shares a single texture slot and batches together.

### `SubTexture2D::SubTexture2D`

```cpp
SubTexture2D(const Ref<Texture2D>& texture, const glm::vec2& min, const glm::vec2& max);
```

**What it does** — stores the texture and expands the `min`/`max` **normalised** UV corners into the
four-corner array in counter-clockwise order: bottom-left, bottom-right, top-right, top-left
(`SubTexture2D.cpp:10-13`), matching the quad vertex winding.

**Why you'd use it** — when you already have normalised UVs. When you have grid coordinates, use
[`CreateFromCoords`](#subtexture2dcreatefromcoords) instead.

**Example**

```cpp
// The top-right quarter of an atlas.
auto tile = Cosmic::CreateRef<Cosmic::SubTexture2D>(atlas,
                                                    glm::vec2{ 0.5f, 0.5f },
                                                    glm::vec2{ 1.0f, 1.0f });
```

**Notes & pitfalls**
- The constructor is public, so `CreateRef<SubTexture2D>` works directly.
- **`texture` is not validated.** A null atlas constructs fine and crashes later, at the first draw.
- `min`/`max` are **normalised `0..1` UVs**, not pixels, and `v` is measured **upward from the
  bottom** (GL's origin). `Scene::OnRenderSprites` converts from the image-editor top-left
  convention when it builds one from `SpriteRendererComponent::SourceRect` (`Scene.cpp:714-716`).
- No clamping: UVs outside `0..1` sample per the texture's wrap mode.

### `SubTexture2D::CreateFromCoords`

```cpp
static Ref<SubTexture2D> CreateFromCoords(
    const Ref<Texture2D>& texture,
    const glm::vec2& coords,
    const glm::vec2& cellSize,
    const glm::vec2& spriteSize = { 1.0f, 1.0f }
);
```

**What it does** — converts a grid cell into normalised UVs and returns a new `SubTexture2D`
(`SubTexture2D.cpp:16-37`):

```
min = (coords * cellSize) / textureSize
max = ((coords + spriteSize) * cellSize) / textureSize
```

`coords` is `(column, row)`, zero-indexed **from the bottom-left**; `cellSize` is the tile size in
**pixels**; `spriteSize` is the tile's extent in **cells**, for sprites that span a block.

**Why you'd use it** — the sprite-sheet workflow. One atlas, many tiles, one texture slot.

**Example**

```cpp
Cosmic::Ref<Cosmic::Texture2D> atlas =
    Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://assets/sheet.png"));

// Column 2, row 0, of a 64 x 64 grid.
auto tile = Cosmic::SubTexture2D::CreateFromCoords(atlas, { 2.0f, 0.0f }, { 64.0f, 64.0f });

// A 2 x 2 block whose lower-left cell is column 4, row 1.
auto wide = Cosmic::SubTexture2D::CreateFromCoords(atlas, { 4.0f, 1.0f },
                                                   { 64.0f, 64.0f }, { 2.0f, 2.0f });

Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, tile);
```

**Notes & pitfalls**
- **Failure: a null `texture` is an access violation** — `texture->GetWidth()` is called with no
  guard (`:22`). There is no nullptr return path; this function always returns a valid `Ref` or
  crashes.
- **A 0 × 0 degraded texture divides by zero**, producing `inf`/`NaN` UVs rather than an error.
  `Texture2D::Create` on a missing file returns exactly such an object.
- **`coords` is `glm::vec2` of floats, so fractional cells are legal** — half-cell offsets work.
- Row 0 is the **bottom** row (GL origin), not an image editor's top row.
- Each call is a `make_shared` plus four divisions — cheap, not free. For a fixed sheet, build the
  tiles once in `OnAttach` and index them.

### `SubTexture2D::GetTexture`

```cpp
const Ref<Texture2D>& GetTexture() const { return m_Texture; }
```

**What it does** — returns the parent atlas by const reference. This is what the `Renderer2D`
sub-texture overloads call to resolve a batch slot.

**Notes & pitfalls**
- Returns a reference to the member — do not outlive the `SubTexture2D`.
- May be a null `Ref` if one was passed to the constructor.

### `SubTexture2D::GetTexCoords`

```cpp
const glm::vec2* GetTexCoords() const { return m_TexCoords; }
```

**What it does** — returns a pointer to the internal **4-element** array, in counter-clockwise order:
index 0 bottom-left, 1 bottom-right, 2 top-right, 3 top-left.

**Why you'd use it** — feeding a custom vertex writer, or reading back a tile's UVs.

**Example**

```cpp
const glm::vec2* uv = tile->GetTexCoords();
CS_INFO("uv0 = ({}, {})  uv2 = ({}, {})", uv[0].x, uv[0].y, uv[2].x, uv[2].y);
```

**Notes & pitfalls**
- **The length is not encoded in the type.** It is always exactly 4; reading past index 3 is UB.
- The pointer is invalidated when the `SubTexture2D` dies.

---

## `Font`

```cpp
class COSMIC_API Font { /* … */ };
```

Declared in `Cosmic/src/graphics/Font.h`. Bakes a TTF/OTF into a single-channel **signed distance
field** glyph atlas via `stb_truetype`, so world-space text stays crisp at any camera zoom. The bake
is cached to disk (`assets/cache/fonts/<stem>_<px>.png` + `.csmfont`) and reused on later runs.
Metrics are in **em units**, so one `Font` renders at every size.

This is the **world-space** text path, consumed by
[`Renderer2D::DrawString`](#renderer2ddrawstring--transform-form). For ImGui panel text see
`Cosmic::UI::Fonts` in [ui.md](ui.md) *(D18)* — a different atlas over the same TTFs.

### `Font::Create`

```cpp
static Ref<Font> Create(const std::string& path, int atlasPixelSize = 64);
```

**What it does** — tries the disk cache first (`LoadFromCache`), falls back to a full bake
(`BakeFromTTF`), and returns the font. A bake rasterises codepoints **32–126** to individual SDF
bitmaps, shelf-packs them into a 1024-px-wide atlas, uploads it as an RGBA `Texture2D` with
`Linear`/`ClampToEdge` sampling, and writes the PNG + metrics cache.

**Why you'd use it** — to load a face by explicit path, at a specific atlas resolution. For a face
that lives in `engine://fonts` or `project://fonts`, use [`Font::Get`](#fontget) instead — it
caches the object in a process-wide library, which `Create` does not.

**Example**

```cpp
Cosmic::Ref<Cosmic::Font> font =
    Cosmic::Font::Create(Cosmic::FileSystem::Resolve("project://fonts/Display.ttf"), 96);
if (!font)
    CS_ERROR("Display.ttf could not be baked");
```

**Notes & pitfalls**
- **Failure: returns `nullptr`** after logging `CS_CORE_ERROR("Font: failed to create font from
  '{0}'")` (`Font.cpp:283-284`). Unreadable file, invalid font data and a failed atlas allocation all
  land here.
- **`path` is NOT VFS-resolved.** Pass a real disk path — `FileSystem::Resolve` it yourself.
- **`atlasPixelSize` is clamped up to 8** (`:77`) and is part of the cache key, so two sizes bake two
  atlases. It trades quality against memory; it does **not** set the render size (the transform
  does).
- **The cache is written to a relative path**, `assets/cache/fonts/…` (`:37`), i.e. relative to the
  process working directory rather than through the VFS.
- **The cache is invalidated by mtime**: a source TTF newer than the cached PNG forces a re-bake
  (`:219-224`).
- **Every call creates a new `Font` and a new `Texture2D`.** Two `Font` objects over the same TTF
  break the text batch on every switch — call `Create` once and keep the `Ref`.
- Requires a current GL context (it uploads a texture).

### `Font::Get`

```cpp
static Ref<Font> Get(const std::string& name);
```

**What it does** — looks `name` up in a process-wide library keyed by **file stem**, initialising the
library on first use by scanning `engine://fonts` then `project://fonts` for `.ttf`/`.otf` and
`Create`-ing each one (first stem wins).

**Why you'd use it** — the normal way to get a font. It is cached, so repeated calls return the same
object and therefore the same atlas and the same text batch.

**Example**

```cpp
Cosmic::Ref<Cosmic::Font> font = Cosmic::Font::Get("Roboto-Bold");
if (!font) font = Cosmic::Font::Default();
```

**Notes & pitfalls**
- **Failure: returns `nullptr`** when the stem is not in the library. No log.
- **`name` is a bare file stem, not a path and not a VFS URI.** `"Roboto-Bold"`, not
  `"project://fonts/Roboto-Bold.ttf"`. `UiComponents.h:147` describes `UiTextComponent::FontPath`
  as "font stem or VFS path" and `UiSystem.cpp:284` passes it straight to `Get` — a VFS path there
  silently misses and falls back to the default face.
- **The first call does the whole scan and bakes every uncached face**, which can be slow and
  requires a GL context. It happens lazily on first use, not at startup.
- Extension matching is case-insensitive; stem matching is not.

### `Font::Default`

```cpp
static Ref<Font> Default();
```

**What it does** — initialises the library if needed and returns the default face: `Roboto-Regular`
if present, otherwise the first font found in map order.

**Why you'd use it** — as a fallback when a named lookup misses, and as the face for UI text with no
explicit font.

**Example**

```cpp
Cosmic::Ref<Cosmic::Font> font = Cosmic::Font::Default();
if (font)
    Cosmic::Renderer2D::DrawString("HELLO", font, { 0.0f, 0.0f }, 0.5f);
```

**Notes & pitfalls**
- **Failure: returns `nullptr` when no font was found at all** (both folders empty or missing).
  Always null-check, or rely on `DrawString`'s own null-font early-out.
- "First font found" is `std::unordered_map::begin()` order — **not** alphabetical and not stable
  across runs. Do not depend on which face you get; ship `Roboto-Regular`.

### `Font::LoadProjectFonts`

```cpp
static void LoadProjectFonts();
```

**What it does** — rescans `project://fonts` for the currently mounted project and adds any stems the
library does not already have. If the library has **not** been initialised yet it returns
immediately, deliberately staying lazy.

**Why you'd use it** — you would not; `Application::LoadProjectDLL` calls it (`Application.cpp:764`).
It exists for the case where the lazy first-use scan already ran *before* a project was mounted (for
example from the Launcher) and therefore resolved `project://fonts` against nothing.

**Notes & pitfalls**
- **Idempotent** — stems already in the library are kept, first wins.
- Baking creates GL textures, so it must run on the main thread with the context current.
- It also gives `Default()` a second chance if the pre-mount init found no faces at all.

### `Font::GetAtlas`

```cpp
const Ref<Texture2D>& GetAtlas() const { return m_Atlas; }
```

**What it does** — returns the SDF atlas texture. `Renderer2D::DrawString` uses its renderer ID as
the text batch key, and binds it to unit 0 at flush.

**Notes & pitfalls**
- May be null on a partially-constructed font; `IsValid()` is the same test.
- The atlas is RGBA with the SDF value replicated into all four channels (`Font.cpp:160-164`), not a
  single-channel texture, despite the "single-channel SDF" phrasing in the header comment.

### `Font::GetGlyph`

```cpp
const Glyph* GetGlyph(uint32_t codepoint) const;
```

**What it does** — returns a pointer to the glyph record for `codepoint`, or **`nullptr` if the font
has no glyph for it**.

**Why you'd use it** — measuring a string before drawing it: sum `advance`, track `size` and
`offset` for a bounding box.

**Example**

```cpp
float widthEm = 0.0f;
for (char c : std::string("SCORE"))
    if (const Cosmic::Glyph* g = font->GetGlyph((uint32_t)(unsigned char)c))
        widthEm += g->advance;

const float widthWorld = widthEm * fontSizeInWorldUnits;
```

**Notes & pitfalls**
- **Only codepoints 32–126 are ever baked**, so anything else returns `nullptr`.
- The returned pointer aliases the font's internal map — it is invalidated if the font is destroyed.
- A glyph with `size == {0,0}` is whitespace: it advances the pen and emits no quad.

### `Font::LineHeight`, `Ascent`, `Descent`, `Name`, `IsValid`

```cpp
float              LineHeight() const { return m_LineHeight; }
float              Ascent()     const { return m_Ascent; }
float              Descent()    const { return m_Descent; }
const std::string& Name()       const { return m_Name; }
bool               IsValid()    const { return m_Atlas != nullptr; }
```

**What they do** — the per-font metrics, all in **em units**. `LineHeight` is
`(ascent - descent + lineGap) / px` and is what `DrawString` adds `lineSpacing` to for each `\n`.
`Ascent` is positive (default `0.8`), `Descent` is **negative** (default `-0.2`). `Name` is the
source file's stem. `IsValid` is exactly "the atlas is non-null".

**Why you'd use them** — laying out multi-line blocks, vertically centring a label, or checking a
font survived construction.

**Example**

```cpp
// Baseline of the Nth line of a block drawn at world scale `s`.
const float baselineY = topY - font->Ascent() * s - (float)n * font->LineHeight() * s;
```

**Notes & pitfalls**
- Multiply by your world size to get world units; these are never pixels.
- `Descent` being negative is the usual typographic sign convention — `Ascent - Descent` is the
  face's total height.

### `Glyph` *(struct, not `COSMIC_API`)*

```cpp
struct Glyph
{
    glm::vec2 uv0{ 0.0f };    // atlas UV of the glyph's top-left
    glm::vec2 uv1{ 0.0f };    // atlas UV of the glyph's bottom-right
    glm::vec2 size{ 0.0f };   // quad size in em (1 em = font size)
    glm::vec2 offset{ 0.0f }; // baseline -> glyph top-left, em (offset.y up = positive)
    float     advance = 0.0f; // horizontal pen advance, em
};
```

**What it is** — one glyph's atlas placement and layout metrics, returned by
[`GetGlyph`](#fontgetglyph). `uv0` is the **top-left** in atlas space (the bake writes the atlas
top-row-first and loads it without a vertical flip). `DrawString` builds each glyph quad as
`x0 = pen.x + offset.x`, `y0 = pen.y + offset.y`, `x1 = x0 + size.x`, `y1 = y0 - size.y`
(`Renderer2D.cpp:1191-1194`).

**Notes & pitfalls**
- Not exported with `COSMIC_API`, but it is a header-only aggregate — a project DLL uses it by value
  without a linker symbol.
- `size == {0,0}` marks whitespace.

---

## `Light2DRenderer`

```cpp
class COSMIC_API Light2DRenderer { /* … */ };
```

Declared in `Cosmic/src/renderer/Light2DRenderer.h`. A screen-space **darkening composite** for the
2D sprite path, not a lighting model: sprites draw fully lit, then this service accumulates additive
radial lights into a **half-resolution RGBA16F** buffer cleared to an ambient colour, and
**multiplies** that buffer over the currently-bound target. All-static, over a function-local
singleton holding two shaders and one framebuffer.

Ships in **both** engine configurations. The component-level story — `Light2DComponent`,
`EnvironmentComponent::Ambient2D`, the falloff curve, the compat gate — is
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md#light-a-2d-scene); only the
two calls are here.

### `Light2DRenderer::Light` *(nested struct)*

```cpp
struct Light
{
    glm::vec2 Center{ 0.0f };   // world XY
    float     Radius = 1.0f;
    glm::vec3 Color{ 1.0f };
    float     Intensity = 1.0f;
    float     Falloff = 2.0f;
};
```

**What it is** — one light, in world space. `Radius` is a **hard cut**: the contribution is exactly
zero at and beyond it. `Falloff` is the radial exponent — `pow(clamp(1 - d, 0, 1), Falloff)` where
`d` is the normalised distance — so higher is tighter. `Intensity` is HDR, so values above 1 bloom
through the post chain rather than clipping. `Scene::OnRender2DLights` fills one of these per active
`Light2DComponent` from the entity's **raw** `TransformComponent` XY (`Scene.cpp:748-759`).

### `Light2DRenderer::Composite`

```cpp
static void Composite(const std::vector<Light>& lights, const glm::vec3& ambient,
                      const glm::mat4& viewProjection, uint32_t targetW, uint32_t targetH);
```

**What it does**, in three steps (`Light2DRenderer.cpp:56-112`):

1. **Captures the caller's bound framebuffer *before* anything else**, then creates or resizes the
   half-res (`(targetW + 1) / 2` × `(targetH + 1) / 2`) RGBA16F light buffer, binds it, clears it to
   `ambient`, and draws each light as a VBO-free six-vertex additive quad.
2. Rebinds the captured target, sets `BlendMode::Multiply` (`GL_DST_COLOR, GL_ZERO`), and blits the
   light buffer over it as one full-screen triangle with bilinear upsampling.
3. Restores the engine defaults: `BlendMode::Alpha`, depth test **on**, depth write **on**.

**Why you'd use it** — you normally would not: `Scene::OnRender2DLights` calls it (`Scene.cpp:766`)
and `PlayerLayer`/Starforge wire that into `SceneRenderDesc::DrawTransparent`. Call it directly only
from a custom host that draws 2D without a `Scene`.

**Example**

```cpp
// From a custom host, immediately after drawing sprites into the HDR target.
std::vector<Cosmic::Light2DRenderer::Light> lights;
lights.push_back({ { 0.0f, 1.0f }, 4.0f, { 1.0f, 0.85f, 0.6f }, 1.5f, 2.0f });

Cosmic::Light2DRenderer::Composite(lights, glm::vec3(0.12f, 0.13f, 0.18f),
                                   camera.GetViewProjectionMatrix(), vw, vh);
```

**Notes & pitfalls**
- **Failure is a clean no-op in three cases**: `targetW == 0 || targetH == 0` returns immediately;
  a failed `Light2D.glsl` or `BlitCopy.glsl` (headless, or missing shader) returns leaving the scene
  untouched; and the shader-load attempt is made **once** and remembered, so it does not retry per
  frame. There is no error beyond whatever `Shader::Create` logged.
- **White ambient with no lights is identity**, which is why `Scene::OnRender2DLights` skips the call
  entirely in that case — that gate is what makes 2D output byte-identical to a pre-lighting build
  (`Scene.cpp:763-764`, `tests/render/render_2d.cpp:369`).
- **Call it with your target bound**, and expect it to be restored. The "capture before `Ensure()`"
  ordering exists because framebuffer creation/resize ends by unbinding to the default framebuffer —
  reading the binding afterwards used to yield 0 on exactly the frames after startup and after every
  viewport resize, so the multiply landed on the window and the pass appeared to skip
  (`:62-69`).
- **It restores depth write to `true`**, which is *not* the transparent phase's contract (sprites run
  with depth write off, `Scene.cpp:620-622`). Anything drawn in the same phase after this call
  therefore writes depth. In the shipped wiring nothing does — `DrawTransparent` runs sprites then
  lights, in that order.
- **There is no cap on the light count**, unlike the 3D point-light path, and each light is one
  6-vertex `glDrawArrays`.
- The composite happens **inside the HDR phase**, before tonemapping, so canvas UI (which composites
  after) is never darkened.
- Needs a live GL context.

### `Light2DRenderer::Shutdown`

```cpp
static void Shutdown();
```

**What it does** — releases the two shaders and the light framebuffer and clears the cached size and
the "already tried to load" flag, so a later `Composite` re-attempts the shader load.

**Why you'd use it** — `Renderer::Shutdown()` calls it (`Renderer.cpp:42`). Call it directly only in
a custom host, and only while the GL context is still current — `tests/render/render_main.cpp:124`
is the in-tree example.

**Notes & pitfalls**
- **Must run while the context is live**, for the same static-destruction reason as
  [`Renderer2D::Shutdown`](#renderer2dshutdown).
- Safe to call twice, and safe to call before any `Composite`.

---

*See also:* [`../guide/rendering-2d.md`](../guide/rendering-2d.md) (the immediate-mode guide — batching
narrative, worked examples, pitfalls) ·
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md) (the component authoring
path) · [`../guide/game-ui.md`](../guide/game-ui.md) (canvas UI, which composites after the tonemap) ·
[graphics-resources.md](graphics-resources.md) (`Texture2D`, `Shader`, `Material`, `Material::Clone`,
`FrameBuffer`, `RenderCommand`, the `BindingPoints` registry — D8) ·
[cameras.md](cameras.md) (`Camera`, `OrthographicCamera`, `Camera2DController`) ·
[ecs.md](ecs.md) (`SpriteRendererComponent`, `TilemapComponent`, `Light2DComponent`,
`EnvironmentComponent::Ambient2D`) · [rendering-3d.md](rendering-3d.md) (`Renderer3D`, whose
`BeginScene` and stats behave differently on purpose) ·
[rendering-pipeline.md](rendering-pipeline.md) (`SceneRenderer`, where 2D pixels actually land) ·
[ui.md](ui.md) (`UI::Fonts`, the ImGui text path) ·
[build-2d-3d-split](../systems/build-2d-3d-split.md) (what each configuration ships).

---
*Changelog:*
*2026-07-26 — created (D9). Covers `Renderer2D` (lifecycle, pass control, all 16 quad overloads
individually, both `DrawCircle` overloads, lines, both `DrawString` overloads, both instanced
pipelines, the stats surface and the four nested structs), `RenderPass`, `SubTexture2D`, `Font` +
`Glyph`, and `Light2DRenderer` — plus the batch-limit and flush-trigger tables.*
