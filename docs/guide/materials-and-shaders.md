# Materials & Shaders — Guide

**What this covers:** loading and compiling shaders, building and configuring `Material` objects,
the cached-uniform model and its read-at-flush rule, the **shader contract** the preprocessor
enforces (`#type` blocks, the three routing paths, auto-injected uniforms, the vertex-attribute
layout, and the fact that there is no `#include`), `.cmat` material assets, per-submesh **material
slots**, and framebuffers including MRT and pixel read-back.
**Source of truth:** `Cosmic/src/graphics/Shader.h`, `graphics/Material.{h,cpp}`,
`graphics/MaterialAsset.h`, `graphics/FrameBuffer.h`, `platform/OpenGL/OpenGLShader.cpp`,
`platform/OpenGL/OpenGLFrameBuffer.cpp`, `assets/AssetLibrary.{h,cpp}`, `graphics/Mesh.h`,
`scene/Components3D.h`, `renderer/BindingPoints.h`
**API Reference:** [../reference/graphics-resources.md](../reference/graphics-resources.md)
*(per-call signatures and the full [`BindingPoints` table](../reference/graphics-resources.md#bindingpoints))* ·
**How it works:** [../systems/rendering-2d.md](../systems/rendering-2d.md) ·
[../systems/rendering-3d.md](../systems/rendering-3d.md)
**Configuration:** **both.** `Shader`, `Material`, `FrameBuffer` and the preprocessor are shared and
unfenced. Two things on this page are 3D-only: **material slots** (they need `Mesh` submeshes) and
the PBR uniform contract that `.cmat` files target — see
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

A **shader** is a compiled GPU program. A **material** is a shader plus a named bag of uniform
values. The split exists so a hundred objects can share one compiled program and still look
different — and so the engine can decide *when* to upload those values, which turns out to be the
one thing about materials that catches people out.

---

## Quick start

```cpp
#include "Cosmic.h"

// Once, at OnAttach:
Ref<Cosmic::Shader> shader = Cosmic::Shader::Create(
    Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
if (!shader)                                       // nullptr on compile OR link failure
{
    CS_ERROR("Fire.glsl failed to build — see the log for the preprocessed dump");
    return;
}

m_Material = Cosmic::Material::Create(shader, "Fire");
m_Material->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
m_Material->Set("u_NoiseTex", Cosmic::AssetLibrary::GetTexture("project://assets/noise.png"));

// Every frame:
m_Material->Set("u_Time", GetLocalTime());
Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f }, m_Material);
```

---

## Load a shader

```cpp
static Ref<Shader> Shader::Create(const std::string& filepath);
```

**`Shader::Create` returns `nullptr`** if the file cannot be read, the preprocessor cannot route it,
a stage fails to compile, or the program fails to link (`Shader.cpp:31-36`). Always null-check —
this is the one engine factory that hands you nothing on failure, unlike `Texture2D::Create`, which
returns a degraded non-null object.

**`Shader::Create` does not resolve VFS paths.** It opens the string as a filesystem path
(`OpenGLShader.cpp:84`). Resolve it yourself, or go through `AssetLibrary`, which resolves *and*
caches:

```cpp
// Explicit — you own the Ref, one compile per call.
auto a = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));

// Cached — one compile per normalised path, for the process lifetime. Preferred.
auto b = Cosmic::AssetLibrary::GetShader("project://shaders/Fire.glsl");
auto c = Cosmic::AssetLibrary::GetShader("engine://shaders/PBR.glsl");
```

`AssetLibrary::GetShader` logs `"AssetLibrary: failed to load '<path>'"` and returns `nullptr` on
failure, and deliberately **does not cache the failure** so a later call retries after you fix the
file (`AssetLibrary.cpp:74-79`).

When a build fails the engine dumps the **fully preprocessed source, line-numbered**, to the error
log (`OpenGLShader.cpp:369-389`). That dump is what you want — GLSL line numbers refer to the
post-injection source, not your file. Remember that Release builds have no console; read
`user://logs` ([`logging-and-diagnostics.md`](logging-and-diagnostics.md)).

---

## Create and configure a material

```cpp
static Ref<Material> Material::Create(const Ref<Shader>& shader,
                                      const std::string& name = "Untitled Material");
```

`Material` has a private-tag constructor, so `Create` is the only way in. It does **not** null-check
the shader; a material over a null shader crashes on the first `Bind()`.

```cpp
Ref<Cosmic::Material> mat = Cosmic::Material::Create(shader, "Fire");

mat->Set("u_Color",     glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
mat->Set("u_Roughness", 0.4f);
mat->Set("u_Offset",    glm::vec2(0.1f, 0.0f));
mat->Set("u_WindDir",   glm::vec3(1.0f, 0.0f, 0.0f));
mat->Set("u_NoiseTex",  noiseTexture);
```

Five `Set` overloads — `float`, `vec2`, `vec3`, `vec4`, `Ref<Texture>` — each writing into its own
`unordered_map` keyed by uniform name (`Material.cpp:29-33`). There is no `int`, no `mat3/mat4`, and
no array setter on `Material`; those go through `Shader::Set*` directly on a bound shader.

**Nothing is validated at `Set` time.** The material is a plain cache — the name does not have to
exist in the shader, the type does not have to match, and there is no warning either way. A name the
shader does not declare resolves to location `-1` at upload and is silently dropped
(`OpenGLShader.cpp:580-583`). That is deliberate — it is what lets one material feed a shader and
its instancing/skinned twins — but it also means **a typo in a uniform name fails completely
silently**.

### Reading values back

```cpp
float           r   = mat->GetFloat("u_Roughness");     // missing ⇒ 0.0f
glm::vec2       o   = mat->GetVector2("u_Offset");      // missing ⇒ vec2(0)
glm::vec3       d   = mat->GetVector3("u_WindDir");     // missing ⇒ vec3(0)
glm::vec4       c   = mat->GetVector4("u_Color");       // missing ⇒ vec4(1) — OPAQUE WHITE
Ref<Cosmic::Texture> t = mat->GetTexture("u_NoiseTex"); // missing ⇒ nullptr

if (mat->HasFloat("u_Roughness"))  { /* … */ }
if (mat->HasFloat2("u_Offset"))    { /* … */ }
if (mat->HasFloat3("u_WindDir"))   { /* … */ }
if (mat->HasFloat4("u_Color"))     { /* … */ }
if (mat->HasTexture("u_NoiseTex")) { /* … */ }
```

`GetVector4`'s white default is intentional and documented in the header (`Material.h:47-48`): a
missing `u_Color` should tint at full brightness, not erase the geometry. It is also why
`Renderer2D`'s material quad path draws white rather than black when you forget to set a colour.
Every other getter defaults to zero, so use the `Has*` predicates when zero is a meaningful value.

---

## Bind a material

Three bind verbs, and picking the wrong one is a common source of "my texture is black":

| Call | Uploads scalars | Binds textures | Use when |
| --- | :---: | :---: | --- |
| `Bind()` | ✅ | ❌ | drawing through `Renderer2D` — the batcher owns texture slots |
| `BindFull()` | ✅ | ✅ (slots 0, 1, 2… in map order) | drawing by hand: `Renderer::Submit`, a custom pass |
| `BindFullTo(shader)` | ✅ | ✅ | uploading this material's values onto a *different* shader with the same contract |

`Bind()` binds the shader, then streams every cached float/vec2/vec3/vec4 (`Material.cpp:40-61`). It
deliberately does **not** bind textures, because inside `Renderer2D` the batch renderer has already
assigned each texture a slot and set `u_Textures[]` — binding again would fight it.

`BindFull()` adds a loop binding each cached texture to slot 0, 1, 2… and setting its sampler
uniform to that index. Slot assignment follows `unordered_map` iteration order, so it is **stable
for a given material instance but not predictable across materials** — never hard-code a slot number
against it.

`BindFullTo(shader)` is how the engine drives instancing and skinning twins: the twin declares the
same uniform contract, so the whole cache maps 1:1 and names it does not declare no-op
(`Material.cpp:91-115`).

Reserved sampler units the engine binds behind your material — IBL at 8–10, the shadow map at 11,
the snow mask at 12, the outline id mask at 13 — are listed in `renderer/BindingPoints.h`, which is
the single source of truth for every UBO/SSBO index and reserved texture unit. See
[`../reference/graphics-resources.md`](../reference/graphics-resources.md) for the full table. Keep
your own textures on low slots and they will never collide.

---

## One material, many looks

```cpp
static Ref<Material> Material::Clone(const Ref<Material>& source, const std::string& newName);
```

`Clone` deep-copies all five uniform caches **and** the three render-queue hints (`Transparent`, the
instancing twin, the skinned twin), while **sharing** the compiled `Ref<Shader>` — the program is
never recompiled (`Material.cpp:15-27`).

```cpp
Ref<Cosmic::Material> red = Cosmic::Material::Clone(base, "Base_Red");
red->Set("u_Albedo", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));   // independent of `base`
```

> **The read-at-flush rule.** `Renderer3D` captures transform, colour and entity ID per call, but
> captures the **material by reference and reads its values at flush** (`Renderer3D.h:64-67`).
> Mutating one material between two `DrawMesh` calls therefore gives **both draws the last value**,
> not per-draw variation. `Clone` per variant is the supported way to get N looks from one shader.

`Renderer2D`'s material quads are a partial exception, and the partial matters: `u_Color` and
`u_Texture` are read **at submit** and baked into each quad's vertex data, while every other uniform
is uploaded once by `Material::Bind()` at flush. So per-quad colour works; per-quad anything else
does not. See [`rendering-2d.md`](rendering-2d.md#draw-a-quad-with-your-own-shader).

### Render-queue hints

```cpp
mat->SetTransparent(true);                 // draw after opaques, back-to-front, depth write off
mat->SetInstancingShader(pbrInstanced);    // opt in to auto-instancing (>= 4 identical submissions)
mat->SetSkinnedShader(pbrSkinned);         // required for GPU skinning; without it, bind pose
```

`SetTransparent` replaces the manual depth-write juggling apps used to do around `DrawMesh`.
`SetInstancingShader` registers a twin that reads `{ mat4 Model; vec4 Tint; }` from the SSBO at
`Bindings::InstancesSsbo`; transforms should be rigid with uniform scale, because the twin derives
normals from `mat3(Model)`. `SetSkinnedShader` registers a twin that reads the joint palette from
`Bindings::SkinningSsbo`. Details in [`rendering-3d.md`](rendering-3d.md) and
[`animation.md`](animation.md).

---

## Write a shader the engine will accept

Cosmic compiles **one file per program**, containing every stage, split by `#type` directives. What
reaches the driver is not what you wrote — the preprocessor rewrites each stage before compiling it,
and understanding those rewrites is the difference between shaders that build first time and shaders
that produce baffling GLSL errors.

### The three routing paths

`OpenGLShader::PreProcess` (`OpenGLShader.cpp:102-364`) looks for `#type` and routes:

**Path 1 — multi-stage file (write this).** One or more `#type` blocks. Recognised stage names are
`vertex`, `fragment` (or `pixel`), and `compute` (`OpenGLShader.cpp:28-36`). Each block is processed
independently.

**Path 2 — fragment only.** A file with `#type fragment` but no `#type vertex` gets a complete
`#version 450 core` batch-layout vertex shader prepended automatically (`:354-357`), supplying
`v_Color` and `v_TexCoord`. Handy for quick fragment experiments. The reverse — vertex with no
fragment — logs an error and produces a broken program.

**Path 3 — Shadertoy.** *No* `#type` anywhere, but the source contains `mainImage` or `iTime`. The
whole file is wrapped as a fragment stage with a generated vertex stage, plus:

```glsl
#define iTime       u_Time
#define iResolution vec3(u_ViewportSize, 1.0)
```

and a `main()` that calls `mainImage(fragColor, v_TexCoord * u_ViewportSize)` and multiplies the
result by `v_Color`. `iMouse`, `iChannel*` and the rest of the Shadertoy surface are **not**
provided — declare them yourself.

No `#type` and no Shadertoy signature is a hard error: the preprocessor logs
*"File contains no '#type' configurations…"* and `Shader::Create` returns `nullptr`.

### Auto-injected uniforms

Three engine uniforms are injected **per stage**, and only when the stage's source *mentions* one
without *declaring* it (`OpenGLShader.cpp:218-276`):

| Uniform | Type | Triggered by any of | Injected into |
| --- | --- | --- | --- |
| `u_ViewProjection` | `mat4` | `u_ViewProjection` | **vertex stage only** (`:270-273`) |
| `u_Time` | `float` | `u_Time`, `iTime`, `TIME`, `_Time` | any stage |
| `u_ViewportSize` | `vec2` | `u_ViewportSize`, `iResolution`, `BUFFER_SIZE`, `_ScreenParams` | any stage |

A fragment stage additionally gets `in vec2 v_TexCoord`, `in vec4 v_Color` and
`layout(location = 0) out vec4 color` injected if it declares none of its own (`:278-298`). The
output guard checks for both `out vec4 color` and any `layout(location = 0) out` — because injecting
a second location-0 output is a GLSL compile error, and that silently killed an early tonemap pass.

**Comments are stripped from a working copy before the scan** (`:182-206`), so a commented-out
declaration does not suppress injection — the preprocessor will inject a live one and you get a
duplicate-declaration error. Delete dead uniform lines rather than commenting them.

**To opt out of injection entirely, declare the uniform yourself.** The scanner walks back to the
start of the line and looks for the `uniform` keyword; if it finds one, it skips.

> **Nothing feeds `u_Time` for a client material.** The preprocessor *declares* it, and four engine
> subsystems set it on their own shaders — but there is no global per-frame upload. If your shader
> reads `u_Time`, you must push it: `mat->Set("u_Time", GetLocalTime())`. `GetLocalTime()` is the
> right source because it already carries the global and per-layer time scales
> ([`time-and-ticks.md`](time-and-ticks.md)).

### `u_Textures[]` is wired for you

At link time the engine looks up `u_Textures` and, if present, uploads the sampler index array
`[0, 1, 2, …]` sized to `min(GL_MAX_TEXTURE_IMAGE_UNITS, 32)` (`OpenGLShader.cpp:473-496`), logging
*"Automatically mapped 'u_Textures' to N hardware slots."* You never call `SetIntArray("u_Textures", …)`
yourself for a batch shader.

### The `Renderer2D` vertex-attribute contract

Any shader that draws batched 2D geometry **must** match this layout exactly. The VAO attribute
pointers are configured once in `Renderer2D::Init` (`Renderer2D.cpp:199-205`) and never change; a
mismatch misreads vertex data with **no runtime error**.

```glsl
#type vertex
#version 450 core

layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec4  a_Color;
layout(location = 2) in vec2  a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

uniform mat4 u_ViewProjection;      // declared explicitly — no injection

out vec4  v_Color;
out vec2  v_TexCoord;
out float v_TexIndex;
out float v_TilingFactor;

void main()
{
    v_Color        = a_Color;
    v_TexCoord     = a_TexCoord;
    v_TexIndex     = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    gl_Position    = u_ViewProjection * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
in float v_TexIndex;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];   // sampler indices uploaded automatically at link
uniform float     u_Time;           // declared → not injected; YOU must Set() it
uniform vec2      u_ViewportSize;

void main()
{
    color = texture(u_Textures[int(v_TexIndex)], v_TexCoord * v_TilingFactor) * v_Color;
}
```

The engine's own `assets/shaders/Texture.glsl` is this shader, at `#version 330 core` and without
the extra uniforms — read it when in doubt. Note it declares **no `u_Color`**: `Renderer2D`'s
material path folds `u_Color` into `a_Color` at submit, so a `Set("u_Color", …)` that reaches
`Texture.glsl` lands on location `-1` and is dropped, having already done its job.

The instanced paths use a different layout — locations 1–7 as per-instance attributes, described in
[`rendering-2d.md`](rendering-2d.md#draw-thousands-of-things-at-once).

### There is no `#include`

The preprocessor handles `#type` and uniform injection. **It has no include directive, no import,
and no snippet system**, and no shader in `Cosmic/assets/shaders/` uses one. Shared GLSL is
duplicated across files today. If you need common code, duplicate it or generate the file — a
`#include` line reaches the GLSL compiler untouched and fails there.

### Compute shaders

`#type compute` is recognised and compiled (`OpenGLShader.cpp:32`); `ComputeParticles.glsl` and
`ParticleUpdate.glsl` are working examples. Dispatch and memory barriers go through
`RenderCommand`, and storage buffers through `graphics/StorageBuffer.h` with an index claimed in
`BindingPoints.h`. Covered in [`world-systems.md`](world-systems.md).

---

## Ship a material as an asset

A `.cmat` file is a serialized `MaterialAsset` — a reflected plain struct describing a PBR material
(`graphics/MaterialAsset.h`). It is *not* the GPU object: `AssetLibrary` loads one and builds a live
`Ref<Material>` bound to the engine PBR shader.

```cpp
struct MaterialAsset
{
    glm::vec4   Albedo{ 0.8f, 0.8f, 0.8f, 1.0f };   // linear; .a is alpha
    float       Metallic  = 0.0f;
    float       Roughness = 0.5f;
    float       AO        = 1.0f;
    glm::vec3   Emissive{ 0.0f };
    bool        Transparent = false;

    std::string AlbedoMap, NormalMap, MetalRoughMap, AOMap, EmissiveMap;  // project:// paths
};
```

```cpp
// Load + cache a live material (one build per normalised path).
Ref<Cosmic::Material> m = Cosmic::AssetLibrary::GetMaterial("project://materials/Rock.cmat");

// Read / write the description itself.
Cosmic::MaterialAsset desc;
if (Cosmic::AssetLibrary::LoadMaterialAsset(desc, "project://materials/Rock.cmat"))
{
    desc.Roughness = 0.2f;
    Cosmic::AssetLibrary::SaveMaterialAsset(desc, "project://materials/Rock.cmat");
}

// Build a live material from a description without caching it.
Ref<Cosmic::Material> oneOff = Cosmic::AssetLibrary::BuildMaterial(desc, "Rock_Variant");
```

`GetMaterial` returns `nullptr` if the file will not load; `BuildMaterial` returns `nullptr` if
`engine://shaders/PBR.glsl` will not load (`AssetLibrary.cpp:159-161`).

### What `BuildMaterial` maps

Every field lands on a fixed PBR uniform contract (`AssetLibrary.cpp:157-189`):

| Asset field | Uniform | Notes |
| --- | --- | --- |
| `Albedo` | `u_Albedo` | |
| `Metallic` / `Roughness` / `AO` | `u_Metallic` / `u_Roughness` / `u_AO` | |
| `Emissive` | `u_Emissive` | |
| `Transparent` | — | forwarded to `Material::SetTransparent` |
| `AlbedoMap` … `EmissiveMap` | `u_AlbedoMap` … `u_EmissiveMap` | each paired with `u_Has*Map` = `1.0` / `0.0` |

Every built material also gets `PBRSkinned.glsl` registered as its skinned twin, so any `.cmat` can
drive a skinned mesh. `MetalRoughMap` follows the glTF packing: roughness in G, metallic in B.

### The file format

`.cmat` is JSON written through the reflection layer, pretty-printed with two-space indent:

```json
{
  "cosmic_type": "MaterialAsset",
  "fields": {
    "Albedo": [0.8, 0.8, 0.8, 1.0],
    "Metallic": 0.0,
    "Roughness": 0.5,
    "AO": 1.0,
    "Emissive": [0.0, 0.0, 0.0],
    "Transparent": false,
    "AlbedoMap": "project://textures/rock_albedo.png",
    "NormalMap": "",
    "MetalRoughMap": "",
    "AOMap": "",
    "EmissiveMap": ""
  }
}
```

The loader also accepts a **bare field object** without the `cosmic_type`/`fields` wrapper, and enum
fields deserialize from either an integer or an option name — useful when hand-authoring. Because
every field is reflected, the Material Editor's UI and the file format are both generic: adding a
field to `MaterialAsset` extends both with no per-field code. There are no `.cmat` files checked
into the tree today; the editor writes them.

---

## Give one mesh several materials

**3D only.** A `Mesh` may carry a table of `Submesh` ranges, each naming a material *slot*
(`Mesh.h:85-96`):

```cpp
struct Submesh
{
    uint32_t IndexOffset;    // first index into the mesh's index buffer
    uint32_t IndexCount;     // number of indices in this range
    uint32_t MaterialIndex;  // which slot this range draws with
};
```

`MeshRendererComponent` holds the slots (`Components3D.h:75-85`):

```cpp
std::vector<std::string>   MaterialPaths;   // one project:// .cmat path per slot
std::vector<Ref<Material>> MaterialAssets;  // runtime-resolved, parallel to MaterialPaths
```

```cpp
auto& mr = entity.GetComponent<Cosmic::MeshRendererComponent>();
mr.MaterialPaths = {
    "project://materials/Body.cmat",
    "project://materials/Glass.cmat",
    "project://materials/Trim.cmat",
};
mr.MaterialPathsResolved = false;   // ask the scene to re-resolve into MaterialAssets
```

Rules worth knowing before you rely on it:

- **Empty `MaterialPaths` is the compat gate.** An empty list draws the whole mesh with the single
  `MaterialAsset`/`MaterialPath`, byte-identically to before Phase 24.
- **Several submeshes may share a slot** — `MaterialIndex` indexes the slot list, not the submesh
  list. The importer populates the table for multi-part sources; primitives and OBJ leave it empty.
- **An empty or unresolved slot falls back** to `MaterialAsset`, then to the flat `Color`
  (`Scene3D.cpp:722-725`).
- **`MaterialPaths` is not reflected** — a `vector<string>` is not a reflectable field kind. The
  serializer special-cases the `"MaterialPaths"` array (`SceneSerializer.cpp:194-200`, `:287-292`)
  and the Inspector draws a bespoke list, so it *does* round-trip through `.cscene`.
- **Skinned meshes stay single-material.** The multi-material path is skipped for skinned meshes,
  and depth/shadow passes draw one whole-mesh caster regardless of the split (`Scene3D.cpp:705-720`).

Each slot becomes its own `SceneDrawContext::DrawMeshRange(mesh, transform, material, color,
indexOffset, indexCount, entityID)` submission, and the render queue still sorts and culls each one
independently.

---

## Render into a texture

A `FrameBuffer` is a GPU render target. Bind one and every subsequent draw goes into its textures
instead of the screen — which is how the editor shows a live scene inside an ImGui panel, and how
you build minimaps, portals, thumbnails and render-to-texture effects.

You rarely need one for the main view: `WorkspaceLayer` creates the viewport FBO and binds it before
calling your layer, and `Application::Get().GetFrameBuffer()` hands you that object if you need its
size. Create your own for **secondary** targets.

### Creating one

```cpp
Cosmic::FramebufferSpecification spec;
spec.Width  = 1280;
spec.Height = 720;
// spec.Attachments left empty ⇒ { RGBA8, DEPTH24STENCIL8 }

Ref<Cosmic::FrameBuffer> fbo = Cosmic::FrameBuffer::Create(spec);
```

`Samples` and `SwapChainTarget` are **reserved and not implemented** (`FrameBuffer.h:105-106`) —
MSAA does nothing; leave them at `1` and `false`.

Available attachment formats (`FrameBuffer.h:68-75`):

| Format | For |
| --- | --- |
| `RGBA8` | ordinary colour |
| `RGBA16F` | HDR colour — what the scene's HDR target uses |
| `RED_INTEGER` | entity-ID picking; read back with `ReadPixel` |
| `DEPTH24STENCIL8` | the depth attachment |

### Using one

```cpp
m_Fbo->Bind();
Cosmic::RenderCommand::SetViewport(0, 0, m_Fbo->GetWidth(), m_Fbo->GetHeight());
Cosmic::RenderCommand::Clear();

Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
/* … draws … */
Cosmic::Renderer2D::EndScene();

m_Fbo->Unbind();               // back to the default framebuffer
```

`Resize(w, h)` destroys and reallocates the attachments, so call it on genuine size changes only —
not every frame — and never with a zero dimension.

### Multiple render targets

Name more than one colour attachment and the engine calls `glDrawBuffers` for you
(`OpenGLFrameBuffer.cpp:159-165`):

```cpp
Cosmic::FramebufferSpecification spec;
spec.Width = w; spec.Height = h;
spec.Attachments = { Cosmic::FramebufferTextureFormat::RGBA8,        // 0 — colour
                     Cosmic::FramebufferTextureFormat::RED_INTEGER,  // 1 — entity IDs
                     Cosmic::FramebufferTextureFormat::DEPTH24STENCIL8 };
```

Your fragment stage writes one output per colour attachment:

```glsl
layout(location = 0) out vec4 color;
layout(location = 1) out int  entityID;
```

**Eight colour attachments is the hard ceiling**, and the check guarding it is a `CS_CORE_ASSERT`
that is compiled out in every configuration (D47) — a ninth attachment overruns a fixed 8-element
array. Stay at or below eight.

An attachment list with **no** colour formats is a legal depth-only target (`glDrawBuffer(GL_NONE)`).

### Reading pixels back

```cpp
virtual int  ReadPixel(uint32_t attachmentIndex, int x, int y);
virtual void ClearAttachment(uint32_t attachmentIndex, int value);
virtual float ReadDepth(int x, int y);
virtual bool ReadPixels(uint32_t attachmentIndex, std::vector<uint8_t>& outRGBA,
                        uint32_t& outWidth, uint32_t& outHeight);
```

All four require the FBO to be **bound**. `ReadPixel` and `ReadDepth` take **GL coordinates**, so
the caller flips Y: `glY = height - 1 - mouseY`.

```cpp
m_Fbo->Bind();
const int glY = (int)m_Fbo->GetHeight() - 1 - mouseY;
const int id  = m_Fbo->ReadPixel(1, mouseX, glY);       // -1 = nothing there
m_Fbo->Unbind();
```

- **`glClear` does not reliably clear integer attachments.** Call `ClearAttachment(1, -1)` every
  frame after `Bind()` — this is why the picking pass exists as a separate step.
- **`ReadDepth` returns `1.0`** (the far plane) when there is no depth attachment or the read misses
  geometry. It is what powers orbit-about-cursor and zoom-to-cursor.
- **`ReadPixels` gives you a screenshot-ready buffer**: 8-bit RGBA, row-major, **top-left origin**
  (GL's bottom-left rows are flipped for you), ready for `stb_image_write`. An `RGBA16F` attachment
  is converted and clamped to 8-bit. Returns `false` and leaves the outputs untouched when the
  attachment index is out of range.

All of these are synchronous GPU→CPU reads and stall the pipeline. They are fine per click or per
thumbnail; they are not fine per frame per pixel.

### Displaying one in ImGui

```cpp
ImGui::Begin("Viewport");
const uint32_t texID = m_Fbo->GetColorAttachmentRendererID();   // index defaults to 0
ImGui::Image((ImTextureID)(intptr_t)texID, ImGui::GetContentRegionAvail(),
             ImVec2{ 0, 1 }, ImVec2{ 1, 0 });                   // flip V — GL origin
ImGui::End();
```

`GetColorAttachmentRendererID(index)` selects the attachment on an MRT target and logs an error
returning `0` if the index is out of range.

---

## Common patterns

**Go through `AssetLibrary` for shaders, textures and materials.** It resolves VFS paths, caches by
normalised path, and does not cache failures. Direct `Shader::Create` recompiles every call.

**Null-check `Shader::Create`, but not `Texture2D::Create`.** The first hands you `nullptr`; the
second hands you a degraded object with a zero handle. Different failure conventions, on purpose.

**Declare every engine uniform your shader uses.** Injection is a convenience for quick shaders; an
explicit declaration is what makes a shader's contract readable, and it removes the
commented-out-declaration trap entirely.

**`Clone` per visual variant, don't mutate between draws.** Values are read at flush.

**Set `u_Time` yourself, from `GetLocalTime()`.** Nothing does it for you.

**Keep material textures on low slots.** The engine binds its own at 8–13; `BindFull` starts at 0
and counts up.

**Resize framebuffers on the resize event, not per frame.** `Resize` reallocates GPU textures.

---

## Pitfalls

**"My shader compiles but the uniform does nothing."** An undeclared name resolves to location `-1`
and is silently dropped. Check the spelling against the GLSL source, and remember that GLSL strips
unused uniforms at link time — a uniform your `main()` never reads does not exist.

**"GLSL reports an error on a line that isn't in my file."** Line numbers refer to the preprocessed
source. The error log contains the full line-numbered dump; read that, not your file.

**"Duplicate declaration of `u_Time`."** You commented out a declaration. Comments are stripped
before the injection scan, so the commented line does not suppress injection. Delete it instead.

**"`layout(location = 0) out` declared twice."** Your fragment stage declares an output under a name
the guard does not recognise. Use `out vec4 color` or an explicit `layout(location = 0) out …`.

**"My fragment shader can't see `u_ViewProjection`."** It is injected into the **vertex stage only**.
Declare it explicitly if a fragment stage genuinely needs it.

**"`#include` doesn't work."** There is no include support. Duplicate the code.

**"My material's texture renders black outside `Renderer2D`."** You called `Bind()`, which uploads
scalars only. Use `BindFull()` for manual draws.

**"All my meshes changed colour when I only meant to change one."** Material values are read at
flush. `Material::Clone` per variant.

**"`GetVector4` returned white for a colour I never set."** That is the documented default. Use
`HasFloat4` to distinguish unset from white.

**"`ReadPixel` returns garbage / always the same value."** Either the FBO is not bound, or you did
not flip Y, or you did not `ClearAttachment` the integer target this frame — `glClear` does not
reliably clear it.

**"My picking texture reads fine but the screenshot is upside down."** `ReadPixel`/`ReadDepth` take
GL coordinates (bottom-left); `ReadPixels` returns a top-left-origin buffer. Two different
conventions in the same class, both deliberate.

**"MSAA does nothing."** `FramebufferSpecification::Samples` is reserved and unimplemented.

---

## See also

- [`../reference/graphics-resources.md`](../reference/graphics-resources.md) — per-call signatures
  and the full `BindingPoints` registry *(skeleton, D8)*
- [`rendering-2d.md`](rendering-2d.md) — the batch renderer, the material quad path, batch limits
- [`rendering-3d.md`](rendering-3d.md) — submit/sort/instance and the read-at-flush contract in full
- [`lighting-and-environment.md`](lighting-and-environment.md) — the `SceneRenderer` pass graph, PBR/IBL
- [`assets-and-vfs.md`](assets-and-vfs.md) — `AssetLibrary`, the VFS, import
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — reflection, which is what makes
  `.cmat` generic
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what each configuration ships
