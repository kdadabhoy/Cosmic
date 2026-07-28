# API Reference — Graphics Resources

> **STATUS: WRITTEN** — work order **D8** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/graphics/Shader.h`, `graphics/Texture.h`,
`graphics/TextureCube.h`, `graphics/Material.h`, `graphics/MaterialAsset.h`, `graphics/Buffer.h`,
`graphics/VertexArray.h`, `graphics/UniformBuffer.h`, `graphics/StorageBuffer.h`,
`graphics/FrameBuffer.h`, `renderer/Renderer.h`, `renderer/RenderCommand.h`,
`renderer/BindingPoints.h`.

**Read first:** the client guide chapter
[`../guide/materials-and-shaders.md`](../guide/materials-and-shaders.md) — it owns the *narrative*:
how to load a shader, the cached-uniform model, the **shader-preprocessor contract** (`#type`
blocks, the three routing paths, auto-injected uniforms, the `Renderer2D` attribute layout, and the
fact that there is no `#include`), `.cmat` authoring, material slots, and render-to-texture. **This
chapter does not repeat any of it.** It is the per-call lookup behind it: signature, exact
behaviour, failure mode, pitfalls.

**Owned here, linked from everywhere else:**
- the reserved GL binding registry — [**`BindingPoints`**](#bindingpoints);
- the material read-at-flush rule and its fix — [**the deferred-read contract**](#the-deferred-read-contract)
  and [`Material::Clone`](#materialclone).

**Owned elsewhere, linked not restated:** `Gizmo` (`graphics/Gizmo.h`) is documented in
[cameras.md → `Gizmo`](cameras.md#gizmo) even though it lives under `graphics/`. `Mesh`, `Model`,
`InstanceSet` and the 3D submission queue are [rendering-3d.md](rendering-3d.md) *(skeleton — D10)*.
`SceneRenderer`, `PostProcessStack`, `EnvironmentMap` and `ShadowMap` — the systems that *consume*
most of the reserved bindings below — are [rendering-pipeline.md](rendering-pipeline.md)
*(skeleton — D11)*. `AssetLibrary` itself is [assets-io.md](assets-io.md) *(skeleton — D16)*; this
chapter states only what `AssetLibrary` does to the resources it caches.

**How it works:** [rendering-2d](../systems/rendering-2d.md) *(skeleton — D28)* ·
[rendering-3d](../systems/rendering-3d.md) *(skeleton — D29)*. The renderer class diagram
(**DG-6**) is built in [root README §35](../../README.md#dg-6--the-renderer-stack).

---

## Configuration and lifetime

**Every header in this chapter ships in both engine configurations.** `Cosmic.h` includes all
thirteen *unfenced* (`Cosmic.h:33`, `:38`, `:40`, `:59-69`), and none of their `.cpp` files appears
in the 2D `list(FILTER)` block — `Cosmic/CMakeLists.txt:189` explicitly keeps `Material`, `Buffer`,
`Texture` and `Shader` ("generic GPU infrastructure"), and `:185` keeps `Renderer` and
`RenderCommand`. Nothing here is ³ᴰ or ³ᴰ⁺. Background:
[build-2d-3d-split](../systems/build-2d-3d-split.md).

### Ownership

Every GPU-owning type is created through a **static `Create` factory** that dispatches on
`RendererAPI::GetAPI()` and hands back a `Ref<T>` (`std::shared_ptr`). You never `new` one, and the
GL handle is released in the destructor when the last `Ref` drops. There are no copy constructors
to worry about because you only ever hold a `Ref`; `OpenGLShader` additionally `= delete`s copy and
assignment outright (`OpenGLShader.h:65-66`).

Two ownership rules bind hard:

- **All of this must come from the one `Cosmic.dll`.** A `Ref<Texture2D>` allocated in the engine
  and released in a project DLL only works because both link the same shared allocator — see
  [`../guide/project-anatomy.md`](../guide/project-anatomy.md#ref-scope-and-the-shared-allocator-rule).
- **Every GPU resource must die while a GL context is current.** Each destructor guards on
  `OpenGLContext::HasCurrentContext()` and *skips* the `glDelete*` when the context is already gone
  (`OpenGLShader.cpp:70`, `OpenGLTexture.cpp:291`, `OpenGLBuffer.cpp:48`/`:115`,
  `OpenGLVertexArray.cpp:47`, `OpenGLUniformBuffer.cpp:21`, `OpenGLStorageBuffer.cpp:19`,
  `OpenGLTextureCube.cpp:64`, `OpenGLFrameBuffer.cpp:111`). That prevents a fault inside
  `opengl32.dll` at teardown; the driver reclaims the memory with the context, so it leaks nothing.
  A static or namespace-scope `Ref<Texture2D>` is still a bad idea — it just fails quietly instead
  of crashing.

### Which of these types are actually DLL-exported

Strict-mode coverage tracks `COSMIC_API` classes, and the split here is **not** uniform. It matters
because `COSMIC_API` is `__declspec(dllimport)` in a project DLL (`Core.h:47-52`):

| Exported (`COSMIC_API`) | Not exported |
| --- | --- |
| `Shader`, `Texture`, `Texture2D`, `TextureCube`, `Material`, `MaterialAsset`, `FrameBuffer`, `UniformBuffer`, `StorageBuffer`, `RenderCommand` | `VertexBuffer`, `IndexBuffer`, `BufferLayout`, `BufferElement`, `VertexArray`, `Renderer` |

The unexported six still work from a project DLL in practice, for two different reasons:
`BufferLayout`/`BufferElement` are header-only (`Buffer.h:77-159`) and get compiled into the caller;
`VertexBuffer::Create`, `IndexBuffer::Create`, `VertexArray::Create` and every `Renderer::` static
are out-of-line in `Cosmic.dll` and resolve because the whole `Cosmic.lib` import library is linked.
`Projects/Engine3DDemo` and `Projects/ViperSim` both call them. Treat the omission as an
inconsistency to be aware of, not a capability boundary — but **do not** derive a class from
`VertexArray` or `VertexBuffer` in a project DLL and expect the vtable to line up.

---

## Failure conventions — the one table to memorise

The conventions differ **on purpose**, and getting them wrong is the most common source of
silent-black-geometry bugs in this engine.

| Call | On failure | Cached by `AssetLibrary`? |
| --- | --- | --- |
| [`Shader::Create`](#shadercreate) | **`nullptr`** + `CS_CORE_ERROR` | No — `GetOrLoad` refuses to cache a null (`AssetLibrary.cpp:76-79`) |
| [`Texture2D::Create(path)`](#texture2dcreate--from-a-file) | **Degraded non-null**: a live object with `GetWidth()==0`, `GetHeight()==0`, `GetRendererID()==0` | **Yes — the degraded object is cached until `AssetLibrary::Reload`** |
| [`Texture2D::CreateHDR`](#texture2dcreatehdr) | Degraded non-null, same shape | Not routed through `AssetLibrary` |
| [`Texture2D::Create(bytes, size)`](#texture2dcreate--from-memory) | Degraded non-null — **the header docstring saying `nullptr` is wrong**, see the entry | n/a |
| [`Texture2D::Create(w, h)`](#texture2dcreate--procedural) | Cannot fail (except `RendererAPI::None` ⇒ `nullptr`) | n/a |
| [`Material::Create`](#materialcreate) | Never null. **Accepts a null shader** and defers the crash to `Bind` | Via `GetMaterial`; a failed `.cmat` load returns null and is not cached |
| [`FrameBuffer::Create`](#framebuffercreate) | Non-null, but an incomplete FBO only **logs** (`OpenGLFrameBuffer.cpp:176`) | n/a |
| [`TextureCube::Create`](#texturecubecreate) | Non-null; a non-renderable format is caught on the first [`BeginRenderToFace`](#texturecubebeginrendertoface) and logged | n/a |
| `VertexBuffer` / `IndexBuffer` / `VertexArray` / `UniformBuffer` / `StorageBuffer` `::Create` | Cannot fail under OpenGL; **`nullptr` only when `RendererAPI::GetAPI()` is `None`/`DirectX`** | n/a |
| `Texture2D::Bind`, `SetData`, `SetSampling` on a degraded texture | **Log-and-continue** (`CS_CORE_WARN`), no draw-time error | n/a |

> **The degraded-texture trap, stated once.** A missing PNG does not give you `nullptr`. It gives
> you a real `Texture2D` whose GL handle is 0. It passes every `if (texture)` guard in the engine,
> occupies a batch slot, samples black, and — because `AssetLibrary::GetOrLoad` only declines to
> cache a *null* result — is remembered under its path key until you call
> `AssetLibrary::Reload(path)`. Fixing the file on disk is not enough. This is the same object that
> makes [`AssetLibrary::BuildMaterial`](#materialasset) set `u_Has<X>Map = 1` for a map that does
> not exist. Check `GetWidth() != 0` when it matters.

> **`CS_ASSERT` / `CS_CORE_ASSERT` are compiled out in every configuration.** They are gated on
> `CS_ENABLE_ASSERTS`, which is defined only when `GLCORE_DEBUG` or `CS_DEBUG` is (`Core.h:60-62`),
> and **no CMake target defines either**. Two guards in this chapter's scope are therefore not
> guards at all: the ≤8-colour-attachment check ([`FrameBuffer::Create`](#framebuffercreate)) and
> the framebuffer-completeness abort. Never rely on one to stop you.

---

## The deferred-read contract

**A material's values are read when the render queue *flushes*, not when you submit.** Both
renderers store the `Ref<Material>` and dereference it later:

- **2D** — `Renderer2D::Flush` calls `s_Data.CurrentMaterial->Bind()` and only then uploads
  `u_ViewProjection` (`Renderer2D.cpp:656-667`). A `DrawQuad` with a material only *records*
  vertices; the uniform upload happens at the flush that follows.
- **3D** — `MeshDrawCmd` holds `Ref<Material> MaterialRef` and the queue's `BindStateGroup` calls
  `cmd.MaterialRef->BindFull()` during execution (`Renderer3D.cpp:59-60`, `:674`, `:691`). The
  comment on the struct says it outright: *"the material by reference (its values are read at
  flush)"*.

The consequence, which surprises people:

```cpp
mat->Set("u_Color", red);
Cosmic::Renderer2D::DrawQuad(a, size, mat);    // records vertices only

mat->Set("u_Color", blue);
Cosmic::Renderer2D::DrawQuad(b, size, mat);    // same material => same batch

Cosmic::Renderer2D::EndScene();                // flush: reads u_Color ONCE => BOTH quads are blue
```

The first quad is retroactively recoloured. Nothing warns you. The rule generalises: **any uniform
you mutate between submit and flush applies to every earlier draw still in the batch.**

Two exceptions worth knowing:

1. `u_Color` on `Renderer2D`'s *non*-material overloads is per-quad, because the colour is written
   into the vertex data, not into a material.
2. Changing the material *identity* forces a flush first — `Renderer2D::DrawQuad` compares against
   `s_Data.CurrentMaterial` and calls `FlushAndReset()` on a mismatch (`Renderer2D.cpp:887`,
   `:1033`). So *different* materials never bleed into each other; only repeated mutation of *one*
   material does.

**The fix is [`Material::Clone`](#materialclone)** — one material per distinct appearance, mutated
before it is ever submitted.

---

## `Shader`

Declared in `Cosmic/src/graphics/Shader.h`. An abstract interface over a **linked GPU program**.
Under OpenGL a program may be vertex+fragment or a standalone compute stage; `OpenGLShader`'s
preprocessor decides which from `#type` directives in the file. There is exactly one implementation
(`OpenGLShader`), and it is not a public type — you always hold `Ref<Shader>`.

The `#type` block contract, the three routing paths, the auto-injected uniform registry, and the
`u_Textures[]` auto-mapping are **the guide's material**:
[`../guide/materials-and-shaders.md#write-a-shader-the-engine-will-accept`](../guide/materials-and-shaders.md#write-a-shader-the-engine-will-accept).
The entries below cover only the C++ surface.

### `Shader::Create`

```cpp
static Ref<Shader> Create(const std::string& filepath);
```

**What it does** — reads `filepath` off disk, preprocesses it into per-stage GLSL, compiles each
stage, links the program, and returns it. Returns **`nullptr`** if anything in that chain fails
(`Shader.cpp:22-40`).

**Why you'd use it** — the only way to obtain a `Shader`. Prefer
`AssetLibrary::GetShader(path)` when you want caching and VFS path resolution; reach for
`Shader::Create` directly when you are loading a one-off program or already hold a resolved path.

**Example**

```cpp
// FileSystem::Resolve is REQUIRED — Shader::Create does not understand project:// itself.
Cosmic::Ref<Cosmic::Shader> fire =
    Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
if (!fire)
{
    CS_ERROR("Fire.glsl failed to build — the log holds the preprocessed source dump");
    return;
}
```

**Notes & pitfalls**
- **Failure: `nullptr`, always.** `Shader::Create` constructs the `OpenGLShader`, tests
  `IsValid()` (`OpenGLShader.h:75` — `m_RendererID != 0`), and on false logs
  `"Shader::Create: compilation or link failure for '{0}'. Returning nullptr."` and returns null
  (`Shader.cpp:30-34`). Compile and link failures each additionally dump the **preprocessed** source
  with line numbers (`OpenGLShader.cpp:369-389`), which is what you want to read — the injected
  preamble shifts every line number away from your file.
- **`Shader::Create` does NOT resolve VFS paths.** `OpenGLShader::ReadFile` opens the string with a
  bare `std::ifstream` (`OpenGLShader.cpp:84`). A `project://` or `engine://` path reaches the
  filesystem verbatim, fails to open, logs `"Could not open file '{0}'"` (`:95`), and you get
  `nullptr` two steps later. Wrap the path in `FileSystem::Resolve`, or use
  `AssetLibrary::GetShader`, which resolves for you.
- **A file with no `#type` and no Shadertoy signature is a hard failure**: the preprocessor logs
  *"File contains no '#type' configurations and lacks Shadertoy compatibility signatures"* and
  returns an empty source map (`OpenGLShader.cpp:151`), which links an empty program.
- `RendererAPI::API::None` returns `nullptr` before touching the disk (`Shader.cpp:26`). In practice
  the API is always `OpenGL` (`RendererAPI.cpp:19`).
- **Engine defect (Phase 30 candidate):** `OpenGLShader::m_RendererID` is declared with **no
  initialiser** (`OpenGLShader.h:123`). On the compile-fail (`:438`) and link-fail (`:465`) paths
  `Compile` returns before `m_RendererID = program` (`:468`), so `IsValid()` reads an
  uninitialised `uint32_t` and the destructor passes it to `glDeleteProgram` (`:71`). In practice
  fresh heap is usually zero and the null return happens anyway, but the failure path is formally
  UB. One-word fix: `uint32_t m_RendererID = 0;`.
- Requires a current GL context.

**See also** — [`Material::Create`](#materialcreate),
[`../guide/materials-and-shaders.md#load-a-shader`](../guide/materials-and-shaders.md#load-a-shader)

### `Shader::Bind` / `Shader::Unbind`

```cpp
virtual void Bind() const = 0;
virtual void Unbind() const = 0;
```

**What it does** — `Bind` makes this program the active one (`glUseProgram(m_RendererID)`);
`Unbind` sets the active program to 0 (`OpenGLShader.cpp:509-519`).

**Why you'd use it** — before any manual uniform upload or `RenderCommand` draw that is not going
through `Renderer2D`/`Renderer3D`. `Material::Bind` and friends call it for you.

**Notes & pitfalls**
- **Uniform setters do not bind for you.** Setting a uniform on an unbound program writes into
  whatever program *is* bound — GL's `glUniform*` family targets the current program. Bind first.
- `Unbind` is rarely needed; the next `Bind` replaces the state.

### `Shader::SetInt` / `SetIntArray` / `SetFloat` / `SetFloat2` / `SetFloat3` / `SetFloat4` / `SetMat3` / `SetMat4`

```cpp
virtual void SetInt(const std::string& name, int value)                          = 0;
virtual void SetIntArray(const std::string& name, int* values, uint32_t count)   = 0;
virtual void SetFloat(const std::string& name, float value)                      = 0;
virtual void SetFloat2(const std::string& name, const glm::vec2& value)          = 0;
virtual void SetFloat3(const std::string& name, const glm::vec3& value)          = 0;
virtual void SetFloat4(const std::string& name, const glm::vec4& value)          = 0;
virtual void SetMat3(const std::string& name, const glm::mat3& value)            = 0;
virtual void SetMat4(const std::string& name, const glm::mat4& value)            = 0;
```

**What it does** — uploads one value to the named uniform of the **currently bound** program.

**Why you'd use it** — the escape hatch for per-draw values a `Material` does not hold (the engine
itself uses it for `u_ViewProjection`, `u_Transform`, and the reserved sampler units).

**Example**

```cpp
shader->Bind();
shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
shader->SetFloat3("u_LightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.25f)));
```

**Notes & pitfalls**
- **A name the shader does not declare is a silent no-op** — this is the engine-wide *silent-ignore*
  rule. Every uploader guards on `location != -1` (`OpenGLShader.cpp:580-626`). A typo'd uniform
  name produces no error, no warning, and no visual change. Deliberate: `Renderer3D` pushes the same
  scene uniform set at every shader and lets undeclared ones fall through
  (`Renderer3D.cpp:985-1030`).
- **Locations are cached forever.** `GetUniformLocation` memoises by name, *including the `-1`*
  (`OpenGLShader.cpp:569-578`). A uniform optimised out by the driver stays "missing" for the
  lifetime of the object, which is correct but means the cache never re-queries after a hot reload —
  reload the whole `Shader` instead.
- **There is no `SetBool`, no `SetMat2`, no `SetFloatArray`.** `SetIntArray` is the only array form;
  it takes a non-const `int*` and a count.
- `name` is `std::string` by value at the call site → one allocation per call on long names. In a
  per-frame loop with hundreds of uniforms this shows up; set once per state group, not per draw.

---

## `Texture` and `Texture2D`

Declared in `Cosmic/src/graphics/Texture.h`. `Texture` is the base interface (metadata, binding,
sampling, identity); `Texture2D` adds the 2D factories. Both are `COSMIC_API`. There is one
implementation, `OpenGLTexture`, which derives from `Texture2D`.

Two engine-side enums live in the same header and carry no GL tokens (`Texture.h:67-68`):

```cpp
enum class TextureFilter { Nearest = 0, Linear };
enum class TextureWrap   { Repeat  = 0, ClampToEdge };
```

### `Texture2D::Create` — procedural

```cpp
static Ref<Texture2D> Create(uint32_t width, uint32_t height, bool mipmapped = false);
```

**What it does** — allocates an empty RGBA8 texture of `width × height` on the GPU with no data
uploaded, ready for [`SetData`](#texturesetdata) (`Texture.cpp:17-26`, `OpenGLTexture.cpp:26-59`).

**Why you'd use it** — solid-colour textures (the engine's own 1×1 white texture is one), noise and
gradient maps generated in code, and any image you compute rather than load.

**Example**

```cpp
Cosmic::Ref<Cosmic::Texture2D> white = Cosmic::Texture2D::Create(1, 1);
uint32_t whitePixel = 0xFFFFFFFF;
white->SetData(&whitePixel, sizeof(uint32_t));
```

**Notes & pitfalls**
- **Sampling defaults differ from the file loader.** Procedural, `mipmapped == false`:
  `GL_LINEAR` minification, **`GL_NEAREST` magnification** (`OpenGLTexture.cpp:45-46`) — the
  pixel-art-friendly default. `mipmapped == true`: `GL_LINEAR_MIPMAP_LINEAR` / `GL_LINEAR`
  (`:40-41`), and the (empty) chain is generated immediately so the texture is mip-complete before
  the first `SetData` (`:56-58`). Wrap is `GL_REPEAT` either way.
- `mipmapped = true` also makes every subsequent `SetData` regenerate the chain
  (`OpenGLTexture.cpp:337-338`) — that is the *only* thing the flag does at upload time.
- The internal format is always `GL_RGBA8`; there is no procedural single-channel or float form.
- Cannot fail under OpenGL. `RendererAPI::API::None` returns `nullptr` (`Texture.cpp:21`).

### `Texture2D::Create` — from a file

```cpp
static Ref<Texture2D> Create(const std::string& path);
```

**What it does** — decodes an image with `stb_image`, **flips it vertically** (GL's origin is
bottom-left), uploads it, and generates a mip chain (`OpenGLTexture.cpp:71-151`).

**Why you'd use it** — direct, uncached texture loading. Prefer `AssetLibrary::GetTexture(path)`,
which resolves VFS paths, caches, and can apply a project-wide sampling override.

**Example**

```cpp
Cosmic::Ref<Cosmic::Texture2D> tex =
    Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://assets/dino.png"));
if (tex->GetWidth() == 0)                     // NOT `if (!tex)` — see below
    CS_WARN("dino.png did not load; the material will sample black");
```

**Notes & pitfalls**
- **Failure returns a DEGRADED, NON-NULL object.** On a load failure the constructor logs
  `"Failed to load texture at {0}"` and sets `m_Width = m_Height = 0` while leaving
  `m_RendererID` at 0 (`OpenGLTexture.cpp:145-150`); an unsupported channel count (not 1/2/3/4)
  takes the same exit at `:112-118`. **`if (!tex)` will not catch it — test `GetWidth() != 0` or
  `GetRendererID() != 0`.** Every later call on that object is a logged no-op
  (`Bind` `:352-356`, `SetData` `:305-309`, `SetSampling` `:371-375`), so nothing crashes; you get
  black.
- **`AssetLibrary` caches the degraded object.** `GetOrLoad` refuses to cache only a *null* result
  (`AssetLibrary.cpp:76-79`), and this one is not null. It stays in `s_Textures` under its
  normalised path key until `AssetLibrary::Reload(path)` evicts and re-loads it
  (`AssetLibrary.cpp:409-414`). Failed **shaders, meshes and materials** *are* correctly not
  cached — the inconsistency is textures-only.
- **This overload does not resolve VFS paths either.** `AssetLibrary::GetTexture` does.
- Sampling: `GL_LINEAR_MIPMAP_LINEAR` minification, **`GL_NEAREST` magnification**, `GL_REPEAT`
  wrap (`OpenGLTexture.cpp:129-132`). Magnifying a photographic texture will look blocky until you
  call [`SetSampling`](#texturesetsampling).
- The internal format follows the source channel count: 4→`RGBA8`, 3→`RGB8`, 2→`RG8`, 1→`R8`
  (`:90-109`). It is *not* promoted to RGBA — which is why [`SetData`](#texturesetdata) computes
  bytes-per-pixel from the stored format rather than assuming 4.
- `stbi_set_flip_vertically_on_load(1)` is global state that this constructor sets and does not
  restore (`:77`); the from-memory overload sets it back to 0 (`:208`). Only matters if you call
  `stb_image` yourself.

### `Texture2D::Create` — from memory

```cpp
static Ref<Texture2D> Create(const uint8_t* data, uint32_t size);
```

**What it does** — decodes `size` bytes of an **encoded** image (PNG/JPG file bytes, not raw
texels) already in RAM, uploads it and mips it (`Texture.cpp:72-81`,
`OpenGLTexture.cpp:204-252`). The path for glTF/`.glb` embedded textures — no temp file.

**Why you'd use it** — an image that arrives inside a container file or over the wire. For raw
texel data use the [procedural overload](#texture2dcreate--procedural) plus
[`SetData`](#texturesetdata).

**Notes & pitfalls**
- **The header docstring is wrong.** `Texture.h:151-152` says *"Returns an uploaded, mipmapped
  Texture2D, or nullptr on decode failure."* The implementation returns a **degraded non-null**
  object exactly like the file overload — `m_Width = m_Height = 0` and an early `return` from the
  constructor (`OpenGLTexture.cpp:211-218` on decode failure, `:228-236` on an unsupported channel
  count). `Texture.cpp:72-81` has no null path other than `RendererAPI::None`. Test
  `GetWidth() != 0`.
- **This overload decodes with the flip OFF** (`OpenGLTexture.cpp:208`), unlike the file overload.
  glTF's UV origin is top-left, which matches an unflipped upload. Feeding it a PNG you expected to
  behave like a file load gives you a vertically mirrored texture.
- Magnification here is `GL_LINEAR`, not `GL_NEAREST` (`:244`) — a third sampling default.

### `Texture2D::CreateHDR`

```cpp
static Ref<Texture2D> CreateHDR(const std::string& path);
```

**What it does** — loads a Radiance `.hdr` (RGBE) via `stbi_loadf`, forced to 4 channels, into a
linear **RGBA16F** texture with `GL_LINEAR` filtering and `GL_CLAMP_TO_EDGE` wrap and **no mip
chain** (`Texture.cpp:51-60`, `OpenGLTexture.cpp:163-193`).

**Why you'd use it** — the equirectangular source image for HDRI environment lighting. The
equirect→cube bake reads mip 0 only, which is why no chain is built.

**Notes & pitfalls**
- **Failure: degraded non-null**, `m_Width = m_Height = 0`, logged as *"failed to load HDR image"*
  (`OpenGLTexture.cpp:170-177`). The header documents this correctly.
- RGBA16F, not RGB16F, deliberately: three-channel float is not guaranteed colour-renderable and
  trips a 3-channel alignment quirk on some drivers (`:159-161`). Same reasoning as
  [`TextureCubeFormat`](#texturecube).
- Flipped vertically on load (`:167`), matching the standard spherical-map convention against the
  IBL cube capture orientation.
- `GetGpuBytes()` reports 8 bytes/texel for RGBA16F (`OpenGLTexture.cpp:269`) and adds no mip tail.

### `Texture::Create`

```cpp
static Ref<Texture> Create(const std::string& path);
```

**What it does** — a one-line forward to `Texture2D::Create(path)` (`Texture.cpp:90-93`).

**Why you'd use it** — essentially never. It exists so base-class code can load without naming a
subclass. It has all the same failure and caching behaviour as the 2D file overload, returned
through the base type.

### `Texture::GetWidth` / `GetHeight`

```cpp
virtual uint32_t GetWidth() const  = 0;
virtual uint32_t GetHeight() const = 0;
```

**What it does** — the base-level dimensions in pixels.

**Why you'd use it** — aspect-ratio maths, `SetData` sizing, and — critically — **as the degraded
check**: a failed load reports `0 × 0`.

### `Texture::GetGpuBytes`

```cpp
virtual uint64_t GetGpuBytes() const = 0;
```

**What it does** — an *estimate* of GPU footprint: `width × height × bytes-per-texel` of the base
level, **plus one third** when a mip chain exists (`OpenGLTexture.cpp:256-276`). Returns **0** for a
degraded texture.

**Why you'd use it** — asset accounting in the editor's Resources panel and status bar.

**Notes & pitfalls**
- Bytes-per-texel is derived from the GL internal format — `R8`=1, `RG8`=2, `RGB8`=3, `RGBA8`=4,
  `RGBA16F`=8, anything else falls back to 4 (`:263-271`).
- It is an approximation, not a driver allocation: no alignment, no compression, no padding. Do not
  budget VRAM off it.

### `Texture::SetData`

```cpp
virtual void SetData(void* data, uint32_t size) = 0;
```

**What it does** — replaces the whole base level via `glTexSubImage2D`, then regenerates the mip
chain **if and only if** the texture was created with `mipmapped = true`
(`OpenGLTexture.cpp:303-339`).

**Why you'd use it** — filling a procedural texture, or streaming a CPU-generated image (a coverage
mask, a computed lookup) each frame without reallocating.

**Example**

```cpp
std::vector<uint32_t> pixels(64 * 64, 0xFF00FF00);          // ABGR, GL_RGBA + GL_UNSIGNED_BYTE
Cosmic::Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create(64, 64);
tex->SetData(pixels.data(), static_cast<uint32_t>(pixels.size() * sizeof(uint32_t)));
```

**Notes & pitfalls**
- **`size` must equal `width × height × bytesPerPixel` exactly.** A mismatch logs
  `"Texture data must fill entire texture! Expected size: {0}, provided: {1}"` and **uploads
  nothing** (`:326-330`). There is no partial-region form.
- Bytes-per-pixel comes from the texture's *stored data format*, not from RGBA. A file-loaded 3-
  channel PNG wants `w*h*3` (`:313-323`); an unrecognised format logs and returns.
- **Degraded texture: logged no-op** (`:305-309`). Silent data loss if you don't read the log.
- Always uploads `GL_UNSIGNED_BYTE`. There is no float `SetData`; an RGBA16F texture from
  `CreateHDR` cannot be updated through this call at all (its `m_DataFormat` is `GL_RGBA`, so the
  size check will pass and then upload 8-bit data into a float texture — do not do this).

### `Texture::Bind`

```cpp
virtual void Bind(uint32_t slot = 0) const = 0;
```

**What it does** — `glActiveTexture(GL_TEXTURE0 + slot)` then binds this texture to the 2D target of
that unit (`OpenGLTexture.cpp:350-359`).

**Why you'd use it** — manual draws. `Renderer2D` owns slot assignment inside its batches, and
`Material::BindFull` assigns slots for you; call this yourself only outside both.

**Notes & pitfalls**
- **You still have to point the sampler uniform at the same slot** —
  `shader->SetInt("u_Texture", slot)`. Binding alone does nothing.
- **Degraded texture: logged warning, and the unit is left holding whatever was there before**
  (`:352-356`). The warning names the path and the slot.
- Do not bind into the reserved high units — see [`BindingPoints`](#bindingpoints). Material
  textures go from unit 0 upward; the engine's own sets start at 8.
- To bind a **raw GL handle** (an FBO attachment, which is not a `Ref<Texture2D>`) use
  [`RenderCommand::BindTextureSlot`](#rendercommandbindtextureslot) instead.

### `Texture::SetSampling`

```cpp
virtual void SetSampling(TextureFilter filter, TextureWrap wrap) = 0;
```

**What it does** — overrides min filter, mag filter and both wrap axes at once
(`OpenGLTexture.cpp:369-386`).

**Why you'd use it** — a use case whose needs differ from the factory default: the SDF font atlas
wants `Linear` + `ClampToEdge`; pixel art wants `Nearest` + `ClampToEdge`.

**Notes & pitfalls**
- **`Linear` sets the NON-mipmap minification filter** (`GL_LINEAR`, `:377`, `:381`). Calling this
  on a mipmapped file texture **opts it out of its mip chain** — the chain stays allocated and
  unused, and distant sampling starts to alias. There is no way to say "trilinear" through this API.
- Min and mag get the *same* filter; you cannot ask for linear-min / nearest-mag here, which is what
  the file loader itself uses.
- `TextureWrap` covers S and T only — no per-axis control, no `MirroredRepeat`, no border colour.
- Degraded texture: logged no-op (`:371-375`).
- `AssetLibrary::SetDefaultTextureSampling(filter, wrap)` applies this to every texture it loads
  (`AssetLibrary.cpp:53-58`) — the project-wide pixel-art switch. Details in
  [assets-io.md](assets-io.md).

### `Texture::operator==` / `GetRendererID`

```cpp
virtual bool     operator==(const Texture& other) const = 0;
virtual uint32_t GetRendererID() const                  = 0;
```

**What it does** — identity by GL handle. `operator==` compares `m_RendererID`
(`OpenGLTexture.cpp:395-398`); `GetRendererID` exposes the raw handle.

**Why you'd use it** — the 2D batcher uses `operator==` to decide whether a texture is already in a
slot. `GetRendererID` is what you pass to `ImGui::Image` and to
[`RenderCommand::BindTextureSlot`](#rendercommandbindtextureslot).

**Notes & pitfalls**
- **`operator==` does an unchecked `(const OpenGLTexture&)` downcast** (`:397`). It is safe today
  because `OpenGLTexture` is the only implementation, but it is not a `dynamic_cast` — a second
  backend would make it UB.
- **Two degraded textures compare EQUAL** (both handles are 0), and a degraded texture compares
  equal to any other failed load. Don't use `==` as a "is this the file I asked for" test.

---

## `TextureCube`

Declared in `Cosmic/src/graphics/TextureCube.h`. A six-face cubemap that is either a sampled
resource *or* a render target. The image-based-lighting pipeline uses three of them: the environment
capture, its diffuse-irradiance convolution, and the roughness-mip prefiltered specular map
(`EnvironmentMap.cpp:63-75`).

**Face index order matches `GL_TEXTURE_CUBE_MAP_POSITIVE_X + i`:** `0 +X`, `1 −X`, `2 +Y`, `3 −Y`,
`4 +Z`, `5 −Z` (`TextureCube.h:21-22`).

### `TextureCubeFormat` / `TextureCubeSpecification`

```cpp
enum class TextureCubeFormat { RGBA16F = 0, RGB16F };

struct TextureCubeSpecification
{
    uint32_t          Size      = 512;                      // per-face edge length (mip 0)
    TextureCubeFormat Format    = TextureCubeFormat::RGBA16F;
    bool              Mipmapped = false;                    // allocate a mip chain (prefilter needs it)
};
```

**Notes & pitfalls**
- **`RGBA16F` is the only format the GL spec guarantees is colour-renderable.** `RGB16F` is
  "texture-only" on some Intel/AMD/ANGLE drivers, which makes
  [`BeginRenderToFace`](#texturecubebeginrendertoface)'s framebuffer *incomplete* there — the bake
  silently produces nothing while working fine on NVIDIA. **Use `RGB16F` only for a cube you sample
  and never render into** (`TextureCube.h:31-37`).
- `Mipmapped` computes `1 + floor(log2(Size))` levels down to 1×1
  (`OpenGLTextureCube.cpp:38-42`) and generates the (empty) chain immediately so the cube is
  mip-complete before any bake (`:58-59`).
- There is no `Size == 0` guard. A zero-size cube produces a GL error, not a diagnostic.

### `TextureCube::Create`

```cpp
static Ref<TextureCube> Create(const TextureCubeSpecification& spec);
```

**What it does** — allocates all six faces at `spec.Size`, sets `CLAMP_TO_EDGE` on S/T/R and
`GL_LINEAR` magnification, and picks `GL_LINEAR_MIPMAP_LINEAR` or `GL_LINEAR` minification from
`spec.Mipmapped` (`TextureCube.cpp:9-17`, `OpenGLTextureCube.cpp:32-60`).

**Example**

```cpp
Cosmic::TextureCubeSpecification spec;
spec.Size      = 512;
spec.Mipmapped = true;                     // the prefilter bake needs the chain
Cosmic::Ref<Cosmic::TextureCube> cube = Cosmic::TextureCube::Create(spec);
```

**Notes & pitfalls**
- **Cannot fail** under OpenGL. `RendererAPI::API::None` returns `nullptr` (`TextureCube.cpp:13`).
- The render-target FBO is **lazy** — a sampled-only cube never allocates one
  (`OpenGLTextureCube.cpp:80-81`).
- There is no file-loading form. You bake into a cube; you do not load six PNGs into one.

### `TextureCube::Bind`

```cpp
virtual void Bind(uint32_t slot = 0) const = 0;
```

**What it does** — `glActiveTexture(GL_TEXTURE0 + slot)` then binds to `GL_TEXTURE_CUBE_MAP`
(`OpenGLTextureCube.cpp:72-76`).

**Notes & pitfalls**
- **No degraded guard.** Unlike `Texture2D::Bind`, this binds `m_RendererID` unconditionally; a cube
  is never degraded because `Create` cannot fail.
- Set the matching `samplerCube` uniform to the same slot. **Two sampler *types* on one unit is a
  draw-time `GL_INVALID_OPERATION`** on strict drivers — the reason `Renderer3D` assigns the IBL
  cube units *unconditionally* rather than only when IBL is active (`Renderer3D.cpp:994-1004`).
- The raw-handle equivalent is
  [`RenderCommand::BindTextureCubeSlot`](#rendercommandbindtexturecubeslot).

### `TextureCube::BeginRenderToFace`

```cpp
virtual void BeginRenderToFace(uint32_t face, uint32_t mip = 0) = 0;
```

**What it does** — lazily creates an internal FBO, attaches `face` at `mip` as colour attachment 0,
and sets the GL viewport to that mip's pixel size (`OpenGLTextureCube.cpp:78-99`). Issue your draws
afterwards.

**Why you'd use it** — the render-to-cubemap bake: equirect→cube, irradiance convolution, and the
per-roughness prefilter mips.

**Example**

```cpp
for (uint32_t face = 0; face < 6; ++face)
{
    cube->BeginRenderToFace(face, 0);
    shader->Bind();
    shader->SetMat4("u_ViewProjection", captureProj * captureView[face]);
    Cosmic::RenderCommand::DrawIndexed(boxMesh->GetVertexArray());
}
cube->FinishRender();
```

**Notes & pitfalls**
- **Failure is logged once per cube, not per call.** The first `BeginRenderToFace` checks
  `glCheckFramebufferStatus` and, if incomplete, logs *"render-to-face framebuffer is incomplete
  (format not color-renderable on this driver?) — bake will produce nothing"* (`:89-95`). Subsequent
  calls skip the check. There is **no return value** — the bake proceeds and writes nothing.
- **It changes the GL viewport and does not restore it.** After `FinishRender` you must set the
  viewport back yourself (`RenderCommand::SetViewport`) or the next pass renders into a corner.
- `face` and `mip` are **unvalidated**. `face > 5` produces an invalid GL enum;
  `mip >= GetMipLevels()` attaches a level that does not exist.
- The FBO has **no depth attachment**. Depth-tested geometry in a bake will not occlude correctly —
  the IBL bakes disable depth for exactly this reason.

### `TextureCube::FinishRender`

```cpp
virtual void FinishRender() = 0;
```

**What it does** — binds framebuffer 0 (`OpenGLTextureCube.cpp:101-104`). Call once when the whole
bake is done, not once per face.

**Notes & pitfalls**
- **It binds the *default* framebuffer, not the one that was bound before the bake.** If you were
  rendering into an offscreen target, capture it first with
  [`RenderCommand::GetBoundFramebuffer`](#rendercommandgetboundframebuffer--bindframebufferhandle)
  and restore it after.

### `TextureCube::GenerateMips`

```cpp
virtual void GenerateMips() = 0;
```

**What it does** — `glGenerateMipmap(GL_TEXTURE_CUBE_MAP)` from mip 0
(`OpenGLTextureCube.cpp:106-110`).

**Why you'd use it** — after baking mip 0 of a mipmapped cube you sample at varying roughness.

**Notes & pitfalls**
- **Never call this on a prefiltered specular cube.** The prefilter bake writes *each* mip
  deliberately (each level is a different roughness); regenerating would overwrite them all with a
  plain box filter of level 0.
- No-op-with-GL-error on a cube created with `Mipmapped = false` (no chain is allocated).

### `TextureCube::GetRendererID` / `GetSize` / `GetMipLevels`

```cpp
virtual uint32_t GetRendererID() const = 0;
virtual uint32_t GetSize() const       = 0;
virtual uint32_t GetMipLevels() const  = 0;
```

**What it does** — the raw GL handle, the mip-0 edge length, and the number of allocated mip levels
(1 when not mipmapped).

**Why you'd use it** — `GetRendererID` feeds
[`RenderCommand::BindTextureCubeSlot`](#rendercommandbindtexturecubeslot) and `Renderer3D::SetIBL`.
`GetMipLevels` bounds a prefilter loop and gives you `u_PrefilterMaxLod = GetMipLevels() - 1`.

---

## `Material`

Declared in `Cosmic/src/graphics/Material.h`. **A shader plus a named bag of cached uniform values
and texture references.** It is not a GPU object of its own: nothing is uploaded until a `Bind*`
call, which is why [the deferred-read contract](#the-deferred-read-contract) exists.

The cache is five `std::unordered_map`s keyed by uniform name — `float`, `vec2`, `vec3`, `vec4`,
`Ref<Texture>` (`Material.h:156-160`). The type you `Set` decides which map you land in, and the
getters are per-type; there is no variant.

Construction goes through the factories only: the constructor takes a private `PrivateTag`
(`Material.h:143-145`), so `Material m(...)` does not compile. You always hold `Ref<Material>`.

### `Material::Create`

```cpp
static Ref<Material> Create(const Ref<Shader>& shader, const std::string& name = "Untitled Material");
```

**What it does** — wraps `shader` in a new, empty material (`Material.cpp:5-8`).

**Why you'd use it** — every hand-authored material. For a PBR material described by a `.cmat` file,
use `AssetLibrary::GetMaterial(path)` instead — see [`MaterialAsset`](#materialasset).

**Example**

```cpp
Cosmic::Ref<Cosmic::Shader> shader =
    Cosmic::AssetLibrary::GetShader("project://shaders/Fire.glsl");
if (!shader) return;                                   // Shader::Create returned nullptr

Cosmic::Ref<Cosmic::Material> mat = Cosmic::Material::Create(shader, "Fire");
mat->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
mat->Set("u_NoiseTex", Cosmic::AssetLibrary::GetTexture("project://assets/noise.png"));
```

**Notes & pitfalls**
- **Never returns null — and it does not reject a null shader.** `Material::Create(nullptr, "x")`
  succeeds and hands you a material whose first `Bind()` dereferences null (`Material.cpp:42`) and
  crashes. Null-check the *shader* at load, not the material.
- `name` is metadata only: the editor and log lines use it. It is not a key, not unique, and does
  not affect rendering.
- No GL context is needed to *create* a material; only `Bind*` touches GL.

### `Material::Clone`

```cpp
static Ref<Material> Clone(const Ref<Material>& source, const std::string& newName);
```

**What it does** — deep-copies all five uniform maps and all three render-queue hints
(`m_Transparent`, `m_InstancingShader`, `m_SkinnedShader`) into a new instance, **sharing the
immutable compiled `Ref<Shader>`** (`Material.cpp:15-27`).

**Why you'd use it** — **this is the fix for [the deferred-read contract](#the-deferred-read-contract).**
When N objects need N different values for the same uniform, they need N materials. Cloning costs
one small allocation and no GPU work; the shader is not recompiled.

**Example**

```cpp
// Ten pillars, ten tints, one compiled program.
std::vector<Cosmic::Ref<Cosmic::Material>> pillars;
for (int i = 0; i < 10; ++i)
{
    auto m = Cosmic::Material::Clone(m_BaseMaterial, "Pillar" + std::to_string(i));
    m->Set("u_Tint", glm::vec4(i / 10.0f, 0.4f, 0.8f, 1.0f));   // set BEFORE any submit
    pillars.push_back(m);
}
```

**Notes & pitfalls**
- **Textures are shared, not copied.** The `Ref<Texture>` values are copied; the GPU textures behind
  them are the same objects. Changing a clone's *binding* (`Set(name, otherTexture)`) is isolated;
  calling `SetSampling` on a shared texture affects every clone.
- **The shader is shared by design** — that is the whole point. Two clones can never diverge in
  program, only in values.
- **`Clone(nullptr, ...)` dereferences null** (`Material.cpp:17` reads `source->m_Shader`). There is
  no guard.
- `Clone` is a *snapshot*: later `Set` calls on the source do not propagate.
- Cloning per frame is a leak-shaped anti-pattern. Clone once at setup; mutate the clone.

**See also** — [the deferred-read contract](#the-deferred-read-contract),
[`Material::Create`](#materialcreate),
[`../guide/materials-and-shaders.md#one-material-many-looks`](../guide/materials-and-shaders.md#one-material-many-looks)

### `Material::Set`

```cpp
void Set(const std::string& name, float value);
void Set(const std::string& name, const glm::vec2& value);
void Set(const std::string& name, const glm::vec3& value);
void Set(const std::string& name, const glm::vec4& value);
void Set(const std::string& name, const Ref<Texture>& texture);
```

**What it does** — stores the value in the matching cache map, overwriting any previous entry
(`Material.cpp:29-33`). **Nothing is uploaded.**

**Why you'd use it** — every material parameter. Colours, roughness, tiling factors, sampler
targets.

**Notes & pitfalls**
- **No validation whatsoever.** The uniform need not exist in the shader; a wrong name is stored
  happily and no-ops at bind time on location `-1`. Typos are invisible.
- **There is no `Set(name, int)` and no `Set(name, mat4)`.** An `int` argument converts to `float`
  and lands in the float map, which then uploads with `glUniform1f` — **wrong for an `int`/`sampler`
  uniform**. Sampler slots are assigned by `BindFull` itself; do not try to `Set` one.
- `Set(name, nullptr)` on the texture overload stores a null `Ref` under that key.
  `HasTexture(name)` then returns **true** while `GetTexture(name)` returns null, and `BindFull`
  skips it (`Material.cpp:77`). Erase-by-null is not supported; there is no `Unset`.
- Mutating after submission is [the deferred-read trap](#the-deferred-read-contract).

### `Material::GetFloat` / `GetVector2` / `GetVector3` / `GetVector4` / `GetTexture`

```cpp
float        GetFloat(const std::string& name);
glm::vec2    GetVector2(const std::string& name);
glm::vec3    GetVector3(const std::string& name);
glm::vec4    GetVector4(const std::string& name);
Ref<Texture> GetTexture(const std::string& name);
```

**What it does** — reads a cached value back (`Material.cpp:117-140`).

**Notes & pitfalls**
- **The defaults are not uniform, and one of them is deliberate.** A missing key gives `0.0f`,
  `vec2(0)`, `vec3(0)`, `nullptr` — but **`GetVector4` returns `vec4(1.0f)`, opaque white**
  (`Material.cpp:132-135`). Documented at `Material.h:47-48`: a missing `u_Color` should tint
  geometry at full brightness rather than erase it. If you need to distinguish "absent" from
  "explicitly white", call `HasFloat4` first.
- **None of these are `const`** — they take a non-const `this` even though they only read. You
  cannot call them through a `const Material&`. (`HasFloat`…`HasTexture` *are* const.)

### `Material::HasFloat` / `HasFloat2` / `HasFloat3` / `HasFloat4` / `HasTexture`

```cpp
bool HasFloat(const std::string& name) const;
bool HasFloat2(const std::string& name) const;
bool HasFloat3(const std::string& name) const;
bool HasFloat4(const std::string& name) const;
bool HasTexture(const std::string& name) const;
```

**What it does** — key presence in the corresponding map (`Material.cpp:142-165`).

**Why you'd use it** — to tell "absent" from a default, especially against `GetVector4`'s white
default; and to drive editor UI that only shows parameters a material actually carries.

**Notes & pitfalls**
- Presence is **per map**. `Set("u_X", 1.0f)` then `HasFloat4("u_X")` is `false` — the same name can
  legally live in several maps at once, and `Bind` would then upload it several times in map order.
- `HasTexture` is true for a key holding a **null** `Ref` (see [`Set`](#materialset)).

### `Material::Bind`

```cpp
void Bind();
```

**What it does** — binds the material's shader and uploads **every cached float/vec2/vec3/vec4**.
It deliberately does **not** bind textures (`Material.cpp:40-61`).

**Why you'd use it** — inside the 2D batch path, where `Renderer2D` owns texture-slot assignment
per quad. `Renderer2D::Flush` is the caller (`Renderer2D.cpp:658`).

**Notes & pitfalls**
- **Textures are NOT bound and sampler uniforms are NOT set.** Outside `Renderer2D`, `Bind` alone
  gives you a material whose samplers point at whatever unit was last configured — usually unit 0,
  usually black. Use [`BindFull`](#materialbindfull) there.
- **Dereferences a null shader without a guard** (`Material.cpp:42`).
- Upload order across the four maps is unspecified (`unordered_map` iteration). Never write a shader
  that depends on uniform upload order.

### `Material::BindFull`

```cpp
void BindFull();
```

**What it does** — `Bind()`, then binds each cached texture to units `0, 1, 2, …` **in
`unordered_map` iteration order** and sets its sampler uniform to that unit
(`Material.cpp:70-82`).

**Why you'd use it** — every draw that is not a `Renderer2D` batch: `Renderer::Submit`, and
`Renderer3D`'s queue, which calls it in `BindStateGroup` (`Renderer3D.cpp:691`).

**Example**

```cpp
material->BindFull();                                   // uniforms + textures + sampler slots
material->GetShader()->SetMat4("u_Model", transform);   // per-draw values go on top
mesh->GetVertexArray()->Bind();
Cosmic::RenderCommand::DrawIndexed(mesh->GetVertexArray());
```

**Notes & pitfalls**
- **Slot assignment is iteration-ordered, so it is not stable across runs** — the *n*-th texture
  is not "yours". That is fine because the sampler uniform is set alongside the bind; it is only a
  problem if you hardcode a unit number somewhere else.
- Slots are allocated **from 0 upward**, which is why the engine's own reserved sets start at 8 —
  see [`BindingPoints`](#bindingpoints). A material with 9+ textures **will collide with the IBL
  units**.
- Null texture values are skipped without advancing the slot counter (`Material.cpp:77`).
- A *degraded* texture is non-null, consumes a slot, and samples black.

### `Material::BindFullTo`

```cpp
void BindFullTo(const Ref<Shader>& shader);
```

**What it does** — `BindFull()`'s work, but onto a **caller-supplied** shader instead of the
material's own: binds `shader`, uploads every cached uniform, binds every texture and sets its
sampler on `shader` (`Material.cpp:91-115`).

**Why you'd use it** — driving a material's values through one of its *twins*: the instancing twin
(`Renderer3D`'s auto-instancing, `Renderer3D.cpp:857`) or the skinned twin
(`BindStateGroup`, `:681`). The twin declares the same uniform contract, so the cache maps 1:1.

**Notes & pitfalls**
- **`shader == nullptr` returns silently** (`Material.cpp:93-94`) — the one null-guarded `Bind*`.
- Names the twin does not declare no-op on location `-1`. That is the mechanism: the twin can omit
  `u_Model` (it reads the instance SSBO instead) and everything else still lands.
- It does **not** change the material's own `GetShader()`. This is a one-shot upload, not a rebind.

### `Material::SetTransparent` / `IsTransparent`

```cpp
void SetTransparent(bool transparent)    { m_Transparent = transparent; }
bool IsTransparent() const               { return m_Transparent; }
```

**What it does** — a **render-queue hint**, default `false` (opaque). `Renderer3D`'s queue draws
transparent-material meshes after all opaques, sorted back-to-front, with depth writes off and depth
test on, under the default `Alpha` blend (`Material.h:93-100`, read at `Renderer3D.cpp:573`).

**Why you'd use it** — glass, foliage cards, decals: the state juggling you would otherwise do by
hand around each `DrawMesh`.

**Notes & pitfalls**
- **`Renderer2D` ignores it entirely.** This is a 3D-queue hint only; the 2D batcher has one blend
  mode per flush and no back-to-front sort.
- Setting it also **disables auto-instancing** for the material: the instancable key requires
  `!transparent` (`Renderer3D.cpp:600`).
- Changing it mid-frame between submit and flush is another instance of
  [the deferred-read trap](#the-deferred-read-contract) — the flag is read at submit
  (`Renderer3D.cpp:573`) but the *bind* still happens later, so a mid-frame flip splits your queue
  incoherently. Set it at construction.

### `Material::SetInstancingShader` / `GetInstancingShader`

```cpp
void                SetInstancingShader(const Ref<Shader>& shader) { m_InstancingShader = shader; }
const Ref<Shader>&  GetInstancingShader() const                    { return m_InstancingShader; }
```

**What it does** — registers the material's **instancing twin**: a shader with the same uniform and
texture contract that reads per-instance `{ mat4 Model; vec4 Tint; }` from the SSBO at
[`Bindings::InstancesSsbo`](#bindingpoints) instead of a per-draw `u_Model` (e.g. `PBR.glsl` →
`PBRInstanced.glsl`). Null (the default) means *never auto-instanced* (`Material.h:104-116`).

**Why you'd use it** — to let `Renderer3D` collapse runs of identical `(mesh, material)` opaque
submissions into one instanced draw.

**Notes & pitfalls**
- Auto-instancing additionally requires the submissions to be **opaque**, to carry
  `entityID == -1`, and to form a run of at least 4 (`Renderer3D.cpp:600`, `kAutoInstanceMinRun`).
  Picking-enabled draws are never instanced.
- **Transforms should be rigid with uniform scale** — the twin derives normals from `mat3(Model)`,
  so non-uniform scale skews lighting.
- Setting a twin that does *not* match the base's uniform contract does not error; the mismatched
  names simply no-op and you get an object that looks wrong only when instanced.

### `Material::SetSkinnedShader` / `GetSkinnedShader`

```cpp
void                SetSkinnedShader(const Ref<Shader>& shader) { m_SkinnedShader = shader; }
const Ref<Shader>&  GetSkinnedShader() const                    { return m_SkinnedShader; }
```

**What it does** — registers the **skinned twin**: same uniform/texture contract plus the joint
palette read from the SSBO at [`Bindings::SkinningSsbo`](#bindingpoints), blended by the
location-4/5 skin attributes (e.g. `PBR.glsl` → `PBRSkinned.glsl`) (`Material.h:118-127`).

**Why you'd use it** — you almost never set this by hand. **`AssetLibrary::BuildMaterial` attaches
`engine://shaders/PBRSkinned.glsl` to every material it builds** (`AssetLibrary.cpp:165-169`), so
any `.cmat` material can be driven by an `Animator` already.

**Notes & pitfalls**
- **A material without a skinned twin does not error — it draws the mesh in bind pose.**
  `Renderer3D::DrawMeshSkinned` falls back to the regular shader (`Renderer3D.cpp:653-656`). A
  character that animates in the editor and stands frozen in a build is this, not a data bug.
- The twin load in `BuildMaterial` is null-safe: a failed `PBRSkinned.glsl` just means bind-pose
  rendering, with the shader error in the log.

### `Material::GetShader` / `GetName`

```cpp
inline Ref<Shader>        GetShader() const { return m_Shader; }
inline const std::string& GetName() const   { return m_Name; }
```

**What it does** — the compiled program and the display name.

**Notes & pitfalls**
- `GetShader()` returns **by value** (a `Ref` copy, i.e. an atomic refcount bump) while `GetName()`
  returns by const reference. Don't call `GetShader()` in a tight per-draw loop.
- `GetShader()` can be null if the material was built from a null shader — see
  [`Material::Create`](#materialcreate). `Renderer3D::DrawMesh` checks for exactly this and drops
  the submission (`Renderer3D.cpp:633-634`).

---

## `MaterialAsset`

Declared in `Cosmic/src/graphics/MaterialAsset.h`. A **plain reflected struct** — the data in a
`.cmat` file and the model behind the Material Editor. It is **not** a GPU object and holds no
`Ref`: `AssetLibrary::GetMaterial(path)` loads one of these and builds a live
[`Material`](#material) from it.

```cpp
struct COSMIC_API MaterialAsset
{
    glm::vec4 Albedo{ 0.8f, 0.8f, 0.8f, 1.0f };   // base colour (linear); a = alpha
    float     Metallic  = 0.0f;
    float     Roughness = 0.5f;
    float     AO        = 1.0f;
    glm::vec3 Emissive{ 0.0f };
    bool      Transparent = false;                // back-to-front, depth-write off

    std::string AlbedoMap;
    std::string NormalMap;
    std::string MetalRoughMap;   // glTF packing: roughness=G, metallic=B
    std::string AOMap;
    std::string EmissiveMap;

    MaterialAsset() = default;
    MaterialAsset(const MaterialAsset&) = default;
};
```

`CS_REGISTER_COMPONENT(Cosmic::MaterialAsset)` at `MaterialAsset.h:50` gives it a stable cross-DLL
type hash, and `TypeRegistry.cpp:249-260` registers every field, which is why both the editor UI and
`.cmat` (de)serialisation are generic — no per-field code.

### Field → uniform → Inspector mapping

| Field | Reflected as | PBR uniform | Inspector treatment (`TypeRegistry.cpp`) |
| --- | --- | --- | --- |
| `Albedo` | `Material.Albedo` | `u_Albedo` | colour picker (`.Color()`, `:250`) |
| `Metallic` | `Material.Metallic` | `u_Metallic` | slider 0–1 (`:251`) |
| `Roughness` | `Material.Roughness` | `u_Roughness` | slider 0–1 (`:252`) |
| `AO` | `Material.AO` | `u_AO` | slider 0–1 (`:253`) |
| `Emissive` | `Material.Emissive` | `u_Emissive` | free vec3, tooltip *"can exceed 1 for HDR"* (`:254`) |
| `Transparent` | `Material.Transparent` | — (calls `SetTransparent`) | checkbox (`:255`) |
| `AlbedoMap` | `Material.AlbedoMap` | `u_AlbedoMap` + `u_HasAlbedoMap` | asset slot, type `"texture"` (`:256`) |
| `NormalMap` | `Material.NormalMap` | `u_NormalMap` + `u_HasNormalMap` | asset slot (`:257`) |
| `MetalRoughMap` | `Material.MetalRoughMap` | `u_MetalRoughMap` + `u_HasMetalRoughMap` | asset slot, tooltip *"glTF pack: roughness=G, metallic=B"* (`:258`) |
| `AOMap` | `Material.AOMap` | `u_AOMap` + `u_HasAOMap` | asset slot (`:259`) |
| `EmissiveMap` | `Material.EmissiveMap` | `u_EmissiveMap` + `u_HasEmissiveMap` | asset slot (`:260`) |

The reflected *class* name is `"Material"`, not `"MaterialAsset"` (`TypeRegistry.cpp:249`) — that is
the string a `.cmat` file's `cosmic_type` carries.

### How a `.cmat` becomes a live `Material`

`AssetLibrary::BuildMaterial(const MaterialAsset&, const std::string& name)`
(`AssetLibrary.cpp:157-189`) loads `engine://shaders/PBR.glsl`, **returns `nullptr` if that shader
fails** (`:160-161`), attaches the skinned twin, `Set`s the five scalar/colour uniforms, forwards
`Transparent` to [`SetTransparent`](#materialsettransparent--istransparent), and then for each map:

```cpp
auto setMap = [&](const std::string& p, const char* mapU, const char* hasU)
{
    Ref<Texture2D> t = p.empty() ? nullptr : GetTexture(p);
    if (t) { m->Set(mapU, t); m->Set(hasU, 1.0f); }
    else   { m->Set(hasU, 0.0f); }
};
```

**Notes & pitfalls**
- **A non-empty map path that fails to load still sets `u_Has<X>Map = 1`.** `GetTexture` returns the
  [degraded non-null texture](#failure-conventions--the-one-table-to-memorise), so `if (t)` is
  true, the 0×0 texture is bound, and the shader takes the *textured* branch and samples black.
  A mistyped `AlbedoMap` therefore gives you a **black** material, not the `Albedo` factor you'd
  expect. Only an **empty** string takes the `u_Has<X>Map = 0` branch (`AssetLibrary.cpp:177-182`).
  This is the single most confusing consequence of the degraded-texture convention.
- **`u_Has*Map` are floats, not ints** (`m->Set(hasU, 1.0f)`) — `Material` has no int setter. Your
  shader must declare them as `float`.
- `AssetLibrary::GetMaterial(path)` caches the built `Material` by path; a `.cmat` that fails to
  deserialise returns `nullptr` and is **not** cached (`AssetLibrary.cpp:203-214` over
  `GetOrLoad`'s `:76-79`).
- `AssetLibrary::LoadMaterialAsset` / `SaveMaterialAsset` (`:191-201`) round-trip the struct through
  the same generic reflected-JSON serializer that powers `.cscene`. Verified field-by-field by
  `tests/test_material.cpp`, which also proves a bare `{"Metallic":0.33}` object loads without the
  `fields` wrapper.
- `MaterialAsset` is a **value type with no `Ref` members** — copy it freely. Note that
  `MeshRendererComponent` has a *member* also named `MaterialAsset`, of type `Ref<Material>`
  (`Components3D.h:54`); they are unrelated.

**See also** — [`../guide/materials-and-shaders.md#ship-a-material-as-an-asset`](../guide/materials-and-shaders.md#ship-a-material-as-an-asset)
(the file format and the editor workflow) · [assets-io.md](assets-io.md) (`AssetLibrary` itself) ·
[ecs.md](ecs.md) (`MeshRendererComponent::MaterialPaths`)

---

## Vertex data — `Buffer.h` and `VertexArray.h`

These are the low-level geometry types. You need them only when you are building geometry by hand —
a custom `Renderer::Submit` pipeline, a debug overlay, an app-owned compute pass. `Mesh` (see
[rendering-3d.md](rendering-3d.md)) wraps all of it for normal 3D work, and `Renderer2D` owns its
own buffers.

**None of these types is `COSMIC_API`-exported** — see
[the export table](#which-of-these-types-are-actually-dll-exported).

### `ShaderDataType` / `ShaderDataTypeSize`

```cpp
enum class ShaderDataType
{
    None = 0, Float, Float2, Float3, Float4, Mat3, Mat4, Int, Int2, Int3, Int4, Bool
};

static uint32_t ShaderDataTypeSize(ShaderDataType type);
```

**What it does** — names an attribute's type and returns its size in bytes (`Buffer.h:42-68`):
`Float`/`Int` 4, `Float2`/`Int2` 8, `Float3`/`Int3` 12, `Float4`/`Int4` 16, `Mat3` 36, `Mat4` 64,
`Bool` **1**, `None` 0.

**Notes & pitfalls**
- **`Mat3` and `Mat4` are declared but not usable in a vertex layout.**
  `OpenGLVertexArray::AddVertexBuffer` passes `GetComponentCount()` straight to
  `glVertexAttribPointer` (`OpenGLVertexArray.cpp:89-96`), and GL accepts only 1–4 components. A
  `Mat4` element requests 16 and raises `GL_INVALID_VALUE`. A matrix attribute has to be declared as
  four consecutive `Float4`s. No engine or project code uses `Mat3`/`Mat4` in a `BufferLayout`;
  treat the enumerators as reserved. *(Phase 30 candidate.)*
- **`Bool` sizes to 1 byte but `GL_BOOL` attributes are 4-byte in GL.** A `Bool` element will
  mis-stride. Use `Int` or `Float`.
- `ShaderDataTypeSize` is a **file-static free function in a header** — each translation unit gets
  its own copy, and unused-function warnings are possible on some compilers. It is `static`, not
  `inline`, deliberately or otherwise.

### `BufferElement`

```cpp
BufferElement(ShaderDataType type, const std::string& name, bool normalized = false, bool instanced = false);
uint32_t GetComponentCount() const;
```

**What it does** — one vertex attribute: `Name`, `Type`, `Size` (auto), `Offset` (filled by
`BufferLayout`), `Normalized`, `Instanced` (`Buffer.h:77-110`).

**Notes & pitfalls**
- **`Name` is documentation only.** The VAO binds attributes by *sequential index*, never by name
  (`OpenGLVertexArray.cpp:79-96`). Renaming an element changes nothing; reordering elements changes
  everything.
- `Instanced = true` sets `glVertexAttribDivisor(index, 1)`; `false` explicitly resets it to 0
  (`:101-108`) so a VAO reused between batched and instanced passes cannot inherit a stale divisor.
- `Offset` is `size_t` while `Size` is `uint32_t` — mixed widths in the same struct, harmless but
  surprising in a `printf`.

### `BufferLayout`

```cpp
BufferLayout();
BufferLayout(const std::initializer_list<BufferElement>& elements);
inline uint32_t GetStride() const;
inline const std::vector<BufferElement>& GetElements() const;
// begin()/end(), const and non-const
```

**What it does** — holds the element list, and on construction walks it once assigning each
element's `Offset` and accumulating `m_Stride` (`Buffer.h:120-159`).

**Why you'd use it** — every `VertexBuffer` needs one before it can be added to a `VertexArray`.

**Example**

```cpp
Cosmic::BufferLayout layout = {
    { Cosmic::ShaderDataType::Float3, "a_Position" },   // 12 B @  0
    { Cosmic::ShaderDataType::Float4, "a_Color"    },   // 16 B @ 12
    { Cosmic::ShaderDataType::Float2, "a_TexCoord" },   //  8 B @ 28
    { Cosmic::ShaderDataType::Int,    "a_EntityID" },   //  4 B @ 36
};                                                      // stride 40
vb->SetLayout(layout);
```

**Notes & pitfalls**
- **Tightly packed, no alignment padding.** Offsets are a running sum of sizes; the stride is the
  sum of all sizes. This matches a `#pragma pack`-free POD struct of `float`s and `int`s on MSVC x64
  but is *not* std140/std430 — do not reuse a `BufferLayout` to reason about a UBO.
- Offsets are computed **only in the initializer-list constructor**. A default-constructed
  `BufferLayout` you mutate through `begin()`/`end()` keeps stride 0.
- Verified by `tests/test_bufferlayout.cpp` (offsets 0/12/28/36, stride 40; `Mat4` sizes to 64; the
  `Instanced` flag survives).

### `VertexBuffer::Create`

```cpp
static std::shared_ptr<VertexBuffer> Create(uint32_t size);
static std::shared_ptr<VertexBuffer> Create(float* vertices, uint32_t size);
```

**What it does** — allocates a GPU vertex buffer. The one-argument form allocates `size` **bytes**
empty with `GL_DYNAMIC_DRAW` (streaming); the two-argument form uploads `vertices` with
`GL_STATIC_DRAW` (`Buffer.cpp:17-26`, `:38-47`, `OpenGLBuffer.cpp:15-35`).

**Notes & pitfalls**
- **`size` is BYTES in both overloads**, not a vertex or float count. `sizeof(verts)` for an array,
  `count * sizeof(Vertex)` for a vector.
- **The returned buffer has NO layout.** Call `SetLayout` before `VertexArray::AddVertexBuffer` or
  the VAO enables zero attributes and your draw renders nothing.
- The static overload takes `float*` — non-const, and typed. Pass any other vertex struct through a
  `reinterpret_cast<float*>`.
- Cannot fail under OpenGL; `nullptr` only for `RendererAPI::None` (`Buffer.cpp:21`, `:42`).
- Returns `std::shared_ptr`, spelled out rather than `Ref<>` — the same type, and `Ref<VertexBuffer>`
  binds to it.

### `VertexBuffer::Bind` / `Unbind` / `SetData` / `GetLayout` / `SetLayout`

```cpp
virtual void Bind() const = 0;
virtual void Unbind() const = 0;
virtual void SetData(const void* data, uint32_t size) = 0;
virtual const BufferLayout& GetLayout() const = 0;
virtual void SetLayout(const BufferLayout& layout) = 0;
```

**What it does** — `Bind`/`Unbind` set `GL_ARRAY_BUFFER`; `SetData` binds and issues
`glBufferSubData` **at offset 0** (`OpenGLBuffer.cpp:82-86`); the layout accessors are pure CPU
state.

**Notes & pitfalls**
- **`SetData` always writes from offset 0** and does not check `size` against the allocation.
  Overrunning a `GL_DYNAMIC_DRAW` buffer is a GL error you will only see in a debug context. This is
  the batch-streaming primitive; `Renderer2D` computes the byte count itself.
- Calling `SetData` on a `GL_STATIC_DRAW` buffer works but defeats the driver's placement hint.
- **`SetLayout` after the buffer is already in a VAO has no effect** — `AddVertexBuffer` reads the
  layout once and configures attribute pointers then (`OpenGLVertexArray.cpp:75`).

### `IndexBuffer::Create` / `Bind` / `Unbind` / `GetCount`

```cpp
static std::shared_ptr<IndexBuffer> Create(uint32_t* indices, uint32_t count);
virtual void     Bind() const = 0;
virtual void     Unbind() const = 0;
virtual uint32_t GetCount() const = 0;
```

**What it does** — uploads `count` **32-bit** indices with `GL_STATIC_DRAW`
(`OpenGLBuffer.cpp:96-102`) and remembers the count.

**Notes & pitfalls**
- **`count` is an index count, not a byte count** — the opposite convention from `VertexBuffer`.
- **Indices are always `uint32_t`.** There is no 16-bit path; `RenderCommand::DrawIndexed` hardcodes
  `GL_UNSIGNED_INT` (`OpenGLRendererAPI.cpp:146`). A 16-bit index array will be misread.
- There is no dynamic/`SetData` form. Index data is immutable after creation.
- `GetCount()` is what `DrawIndexed(vao, 0)` uses when you pass count 0.

### `VertexArray::Create`

```cpp
static Ref<VertexArray> Create();
```

**What it does** — allocates a VAO (`VertexArray.cpp:20-30`, `OpenGLVertexArray.cpp:37-40`). It
starts empty; you attach buffers to it.

**Notes & pitfalls**
- `RendererAPI::API::DirectX` explicitly returns `nullptr` here (`VertexArray.cpp:26`) — the only
  factory in this chapter that names DirectX at all.

### `VertexArray::AddVertexBuffer`

```cpp
virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) = 0;
```

**What it does** — binds the VAO and the buffer, then for each element of the buffer's layout
enables an attribute index, sets its pointer from the layout's stride/offset, and sets its divisor
from `Instanced` (`OpenGLVertexArray.cpp:69-114`). The VAO holds a `Ref` to the buffer.

**Why you'd use it** — every VAO needs at least one. Multiple buffers are supported: a second buffer
continues numbering attributes where the first left off.

**Example**

```cpp
Cosmic::Ref<Cosmic::VertexArray> vao = Cosmic::VertexArray::Create();

auto vb = Cosmic::VertexBuffer::Create(vertices, sizeof(vertices));
vb->SetLayout({ { Cosmic::ShaderDataType::Float3, "a_Position" },
                { Cosmic::ShaderDataType::Float2, "a_TexCoord" } });
vao->AddVertexBuffer(vb);                       // attribute locations 0 and 1

auto ib = Cosmic::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t));
vao->SetIndexBuffer(ib);
```

**Notes & pitfalls**
- **Attribute locations are assigned by cumulative element count, in call order**
  (`OpenGLVertexArray.cpp:79-83`). Adding buffers in a different order silently rewires your shader
  inputs. The layout `Name` strings play no part.
- **Adding a buffer with an empty layout consumes zero attribute slots and enables nothing** — and
  because the count is cumulative, it also does not shift the *next* buffer's indices. Silent.
- The buffer's layout must be set **before** this call.
- Requires a current GL context.

### `VertexArray::SetIndexBuffer` / `Bind` / `Unbind` / `GetVertexBuffers` / `GetIndexBuffer`

```cpp
virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) = 0;
virtual void Bind()   const = 0;
virtual void Unbind() const = 0;
virtual const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const = 0;
virtual const Ref<IndexBuffer>&               GetIndexBuffer()   const = 0;
```

**What it does** — `SetIndexBuffer` binds the VAO, binds the IBO into it (element-array bindings are
VAO state), and stores the `Ref` (`OpenGLVertexArray.cpp:118-124`). `Bind`/`Unbind` set/clear the
VAO. The accessors return the retained references.

**Notes & pitfalls**
- **Only one index buffer.** A second call replaces the first, and the previous `Ref` drops.
- **The draw verbs do NOT bind the VAO for you** — `OpenGLRendererAPI::DrawIndexed` and
  `DrawLines` both document the caller-binds contract (`OpenGLRendererAPI.cpp:156-158`). Always
  `vao->Bind()` immediately before a `RenderCommand` draw.
- `GetIndexBuffer()` returns a `const Ref&` that may be null if you never set one; `DrawIndexed`
  with count 0 will then dereference it.

---

## `UniformBuffer`

Declared in `Cosmic/src/graphics/UniformBuffer.h`. A **UBO**: a shared block of constants bound to a
GLSL binding index and read by any shader declaring a matching
`layout(std140, binding = N) uniform Block { … }`.

### `UniformBuffer::Create`

```cpp
static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
```

**What it does** — allocates `size` bytes `GL_DYNAMIC_DRAW` and immediately binds the buffer to
`binding` with `glBindBufferBase` (`UniformBuffer.cpp:7-15`, `OpenGLUniformBuffer.cpp:7-15`).

**Why you'd use it** — a block of per-frame or per-scene constants shared across many shaders,
uploaded once instead of per-draw. The engine owns two: `LightsUbo` and `CameraUbo` — see
[`BindingPoints`](#bindingpoints).

**Example**

```cpp
struct MyBlock { glm::mat4 ViewProj; glm::vec4 CameraPos_Time; };   // NOTE: vec4, never a bare vec3
Cosmic::Ref<Cosmic::UniformBuffer> ubo =
    Cosmic::UniformBuffer::Create(sizeof(MyBlock), Cosmic::Bindings::CameraUbo);

MyBlock block{ camera.GetViewProjectionMatrix(), glm::vec4(camera.GetPosition(), time) };
ubo->SetData(&block, sizeof(block));
```

**Notes & pitfalls**
- **std140: never put a bare `vec3` in the block.** Its padding silently offsets everything after
  it. Pack as `vec4` and use `.w` for a scalar (`UniformBuffer.h:15-17`). The engine's own
  `GpuLightsBlock` carries a `static_assert(sizeof(...) == 560)` for exactly this reason
  (`Renderer3D.cpp:52`).
- **Claim your binding index in `renderer/BindingPoints.h` first.** GLSL cannot consume the C++
  constants — the shader hardcodes the number — so the registry is the only thing preventing a
  collision.
- Cannot fail under OpenGL; `nullptr` only for `RendererAPI::None` (`UniformBuffer.cpp:11`).
- **There is no size accessor and no bounds check.** See [`SetData`](#uniformbuffersetdata).

### `UniformBuffer::SetData`

```cpp
virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
```

**What it does** — binds the buffer and uploads `size` bytes at `offset` via `glBufferSubData`
(`OpenGLUniformBuffer.cpp:25-29`).

**Notes & pitfalls**
- **`offset + size <= createdSize` is your responsibility.** Nothing checks it; the object does not
  even remember the size it was created with. An overrun is a `GL_INVALID_VALUE` that only a debug
  GL context reports.
- `data` layout must match the shader's std140 block byte for byte. A mismatch is not diagnosable
  from the C++ side — the shader just reads garbage.

### `UniformBuffer::Bind`

```cpp
virtual void Bind() = 0;
```

**What it does** — re-issues `glBindBufferBase` for the buffer's creation-time binding index
(`OpenGLUniformBuffer.cpp:31-34`).

**Why you'd use it** — cheap insurance before an upload, in case another UBO claimed the slot since
creation. `Renderer3D` calls it each `BeginScene` (`Renderer3D.cpp:328`).

**Notes & pitfalls**
- **The binding index is fixed at creation** and cannot be changed. There is no `Bind(uint32_t)`.
- Not `const`, despite changing nothing about the object.

---

## `StorageBuffer`

Declared in `Cosmic/src/graphics/StorageBuffer.h`. An **SSBO**: a large, read-write GPU buffer bound
to a std430 binding index and accessible from compute *and* graphics shaders. The storage half of
the GPU-compute path.

### `StorageBuffer::Create`

```cpp
static Ref<StorageBuffer> Create(uint32_t size, uint32_t binding);
```

**What it does** — allocates `size` bytes `GL_DYNAMIC_DRAW` and binds to `binding`
(`StorageBuffer.cpp:7-15`, `OpenGLStorageBuffer.cpp:7-15`). Identical shape to
[`UniformBuffer::Create`](#uniformbuffercreate), different GL target.

**Why you'd use it** — anything too big or too write-heavy for a UBO: a particle pool, a per-instance
transform array, a skinning palette. UBO size limits are typically 64 KB; SSBOs are bounded by VRAM.

**Example**

```cpp
// An app-owned compute particle pool. Apps use the App* slots; the engine never binds those.
m_ParticleSSBO = Cosmic::StorageBuffer::Create(
    kParticleCount * sizeof(glm::vec4), Cosmic::Bindings::AppSsbo0);
```

**Notes & pitfalls**
- **std430, not std140.** The packing rules differ — most importantly, arrays of scalars and of
  `vec2` are *not* padded to `vec4` in std430. Don't copy a UBO struct into an SSBO.
- **App code must use `Bindings::AppSsbo0` (0) or another slot in the app range [0, 7].** Engine
  systems claim from 8 upward (`BindingPoints.h:48-50`). `Projects/Engine3DDemo` passes a literal
  `0` here (`Engine3DDemo.cpp:119`); prefer the named constant.
- Cannot fail under OpenGL; `nullptr` only for `RendererAPI::None` (`StorageBuffer.cpp:11`).

### `StorageBuffer::SetData`

```cpp
virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
```

**What it does** — binds and `glBufferSubData`s `size` bytes at `offset`
(`OpenGLStorageBuffer.cpp:23-29`).

**Notes & pitfalls**
- **`size == 0` returns immediately** (`:25-26`) — a documented "skip", unlike
  `UniformBuffer::SetData`, which would issue a zero-byte upload. Allocation happens at `Create`.
- Same unchecked-bounds caveat as the UBO: nothing remembers the allocated size.
- **Uploading over data the GPU is still reading forces a sync.** The engine works around this by
  pooling scratch `InstanceSet`s per run rather than rewriting one buffer between draws
  (`Renderer3D.cpp:165-168`).

### `StorageBuffer::Bind`

```cpp
virtual void Bind() = 0;
```

**What it does** — re-issues `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id)`
(`OpenGLStorageBuffer.cpp:31-34`).

**Notes & pitfalls**
- **Compute writes are not visible to a later draw until you insert a barrier.** Pair this with
  [`RenderCommand::GpuMemoryBarrier`](#rendercommandgpumemorybarrier).

---

## `FrameBuffer`

Declared in `Cosmic/src/graphics/FrameBuffer.h`. An **off-screen render target** — the mechanism
behind the editor viewport, post-processing, entity-ID picking, shadow maps, water reflection and
refraction, and thumbnail capture.

### `FramebufferTextureFormat` / `FramebufferTextureSpecification` / `FramebufferAttachmentSpecification`

```cpp
enum class FramebufferTextureFormat
{
    None = 0,
    RGBA8,
    RGBA16F,
    RED_INTEGER,
    DEPTH24STENCIL8
};
```

`FramebufferTextureSpecification` has an **implicit** constructor from a format
(`FrameBuffer.h:81`), which is why an attachment list can be written as
`{ RGBA8, RED_INTEGER, DEPTH24STENCIL8 }`. `FramebufferAttachmentSpecification` is just a
`std::vector` of those with an initializer-list constructor.

| Format | GL internal | Use |
| --- | --- | --- |
| `RGBA8` | `GL_RGBA8` | ordinary LDR colour; `GL_LINEAR` filtered |
| `RGBA16F` | `GL_RGBA16F` | the HDR target; `GL_LINEAR` filtered |
| `RED_INTEGER` | `GL_R32I` | entity-ID picking; **forced `GL_NEAREST`** or the FBO is incomplete |
| `DEPTH24STENCIL8` | `GL_DEPTH24_STENCIL8` | the depth/stencil attachment; `GL_NEAREST` |

(`OpenGLFrameBuffer.cpp:36-81`.) Everything is `GL_CLAMP_TO_EDGE`.

### `FramebufferSpecification`

```cpp
struct FramebufferSpecification
{
    uint32_t Width = 0, Height = 0;
    uint32_t Samples = 1;             // Reserved — MSAA not yet implemented; always renders single-sample
    bool SwapChainTarget = false;     // Reserved — not yet implemented
    FramebufferAttachmentSpecification Attachments;   // empty ⇒ {RGBA8, DEPTH24STENCIL8}
};
```

**Notes & pitfalls**
- **`Samples` and `SwapChainTarget` are RESERVED. There is no MSAA path in this engine.** Setting
  `Samples > 1` renders single-sampled and logs
  *"FramebufferSpecification::Samples is reserved — MSAA is not implemented; rendering
  single-sampled."* (`FrameBuffer.cpp:26-30`). `SwapChainTarget = true` logs and does nothing. The
  warnings exist because silently ignoring them had burned people; they are the only reason you
  find out.
- **Empty `Attachments` means `{RGBA8, DEPTH24STENCIL8}`**, not "no attachments"
  (`OpenGLFrameBuffer.cpp:92-93`). That is what `Application`'s workspace FBO relies on
  (`Application.cpp:572-575`).
- **Attachment *order* is the MRT index order.** The first non-depth entry is colour attachment 0,
  the second is 1, and so on; the depth entry may appear anywhere in the list and is pulled out
  (`:95-101`). `ScenePicker` puts `RED_INTEGER` second so it is attachment 1
  (`ScenePicker.cpp:27-36`).
- **At most one depth attachment** — a second `DEPTH24STENCIL8` in the list overwrites the first,
  silently.
- `Width`/`Height` default to **0**, which is invalid. Always set them.

### `FrameBuffer::Create`

```cpp
static Ref<FrameBuffer> Create(const FramebufferSpecification& spec);
```

**What it does** — allocates the FBO, allocates and attaches a texture per attachment spec, sets up
draw buffers, and checks completeness (`FrameBuffer.cpp:22-39`, `OpenGLFrameBuffer.cpp:86-181`).

**Example**

```cpp
// Colour + entity ID + depth — the picking configuration.
Cosmic::FramebufferSpecification spec;
spec.Width  = 1280;
spec.Height = 720;
spec.Attachments = {
    Cosmic::FramebufferTextureFormat::RGBA8,
    Cosmic::FramebufferTextureFormat::RED_INTEGER,
    Cosmic::FramebufferTextureFormat::DEPTH24STENCIL8
};
Cosmic::Ref<Cosmic::FrameBuffer> fbo = Cosmic::FrameBuffer::Create(spec);
```

**Notes & pitfalls**
- **Failure is LOG-ONLY.** An incomplete framebuffer logs
  `"Framebuffer is incomplete! glCheckFramebufferStatus = {0:#x}"` and then hits a
  `CS_CORE_ASSERT(false, …)` that is **compiled out in every configuration**
  (`OpenGLFrameBuffer.cpp:173-178`). You get a non-null, unusable `FrameBuffer` and draws that go
  nowhere. There is no `IsComplete()` query — read the log.
- **Engine defect (Phase 30 candidate): the 9th colour attachment is real UB.** The MRT path guards
  with `CS_CORE_ASSERT(m_ColorAttachments.size() <= 8, …)` (compiled out) and then writes into a
  fixed `std::array<GLenum, 8> buffers` in a loop bounded by the *actual* attachment count
  (`OpenGLFrameBuffer.cpp:161-165`). Nine or more colour attachments overrun the stack array before
  `glDrawBuffers` is even called. Nothing in the engine does this today; a project could.
  `if (m_ColorAttachments.size() > 8) { CS_CORE_ERROR(...); return; }` is the fix.
- **Zero colour attachments is legal** and selects `glDrawBuffer(GL_NONE)` — the depth-only path
  used by shadow maps (`:167-171`).
- `CS_CORE_ASSERT(Width > 0 && Height > 0, …)` at `:124` is also compiled out; a 0×0 spec produces
  0×0 textures and an incomplete FBO.
- **Cube-target guidance:** for a *cube* render target use `RGBA16F` —
  see [`TextureCubeFormat`](#texturecube). `RGB16F` is not guaranteed colour-renderable off NVIDIA.
  The 2D `FramebufferTextureFormat` enum has no RGB16F member, so this trap is cube-only.
- Requires a current GL context.

### `FrameBuffer::Bind` / `Unbind`

```cpp
virtual void Bind()   = 0;
virtual void Unbind() = 0;
```

**What it does** — `Bind` directs subsequent draws here; `Unbind` binds framebuffer **0**, the
default (`OpenGLFrameBuffer.cpp:185-193`).

**Notes & pitfalls**
- **`Bind` does NOT set the viewport.** Follow it with
  `RenderCommand::SetViewport(0, 0, fbo->GetWidth(), fbo->GetHeight())` or you will render into a
  sub-rectangle sized by whatever ran before.
- **`Unbind` binds the *default* framebuffer, not the previously bound one.** For nesting, capture
  with [`RenderCommand::GetBoundFramebuffer`](#rendercommandgetboundframebuffer--bindframebufferhandle)
  and restore with `BindFramebufferHandle`.
- **`glClear` does not reliably clear integer attachments.** After `Bind` + `Clear`, call
  [`ClearAttachment`](#framebufferclearattachment) for every `RED_INTEGER` attachment, every frame.

### `FrameBuffer::Resize`

```cpp
virtual void Resize(uint32_t width, uint32_t height) = 0;
```

**What it does** — updates the spec and **destroys and re-allocates every attachment texture**
(`OpenGLFrameBuffer.cpp:197-209` → `Invalidate`, `:122-181`).

**Notes & pitfalls**
- **Every attachment's `rendererID` changes.** Any handle you cached — an `ImGui::Image` texture id,
  a `BindTextureSlot` argument — is dangling after a resize. Re-query
  [`GetColorAttachmentRendererID`](#framebuffergetcolorattachmentrendererid) each frame; that is why
  the engine's viewport code does.
- **All contents are lost.** There is no blit-preserving path.
- **Zero dimensions are refused with a warning**, not a crash:
  `"Attempted to resize framebuffer to {0}, {1}"` (`:199-203`). A minimised window hits this every
  frame — guard the call rather than spamming the log.
- Guard for no-ops yourself: `if (fbo->GetWidth() != w || fbo->GetHeight() != h) fbo->Resize(w, h);`
  (the pattern at `ScenePicker.cpp:45-46`). `Resize` does **not** early-out on identical dimensions
  and will happily reallocate every frame.

### `FrameBuffer::GetWidth` / `GetHeight` / `GetSpecification`

```cpp
virtual uint32_t GetWidth()  const = 0;
virtual uint32_t GetHeight() const = 0;
virtual const FramebufferSpecification& GetSpecification() const = 0;
```

**What it does** — current dimensions and the (live, resize-updated) spec.

**Notes & pitfalls**
- `GetSpecification().Samples` still reports whatever you asked for, even though it does nothing.
  Do not treat it as a capability query.

### `FrameBuffer::GetColorAttachmentRendererID`

```cpp
virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
```

**What it does** — the raw GL texture handle for colour attachment `index`
(`OpenGLFrameBuffer.cpp:213-222`).

**Why you'd use it** — `ImGui::Image` (the editor viewport), and
[`RenderCommand::BindTextureSlot`](#rendercommandbindtextureslot) when a post-process pass needs to
sample a previous target.

**Notes & pitfalls**
- **Out of range returns 0 and logs a warning** — `"index {0} out of range ({1} attachments)"`
  (`:217-219`). Handle 0 renders as black in ImGui; it is not an error you will see visually.
- The handle is invalidated by [`Resize`](#framebufferresize).

### `FrameBuffer::GetDepthAttachmentRendererID`

```cpp
virtual uint32_t GetDepthAttachmentRendererID() const = 0;
```

**What it does** — the depth/stencil texture handle.

**Notes & pitfalls**
- **Returns 0, without a warning, when the spec has no depth attachment.** Unlike the colour
  accessor there is no range check because there is only one.
- The texture is `GL_NEAREST`-filtered on purpose: without explicit filters it would default to a
  mipmap min filter, be mip-incomplete, and sample black in debug views
  (`OpenGLFrameBuffer.cpp:72-77`).

### `FrameBuffer::ReadPixel`

```cpp
virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;
```

**What it does** — reads one integer texel out of a `RED_INTEGER` attachment
(`OpenGLFrameBuffer.cpp:224-234`). This is the entity-ID picking read-back.

**Example**

```cpp
fbo->Bind();
const int glY = static_cast<int>(fbo->GetHeight()) - 1 - mouseY;   // CALLER flips y
int entityID = fbo->ReadPixel(1, mouseX, glY);
fbo->Unbind();
```

**Notes & pitfalls**
- **The FBO must already be bound.** Nothing checks; an unbound read returns data from whatever is
  bound.
- **GL's origin is bottom-left — the CALLER flips y**: `glY = height - 1 - mouseY`
  (`FrameBuffer.h:156-158`). Forgetting this is the classic "picking works, but mirrored
  vertically" bug.
- **An out-of-range `attachmentIndex` returns `-1`** — which is also the conventional "nothing
  here" value, so you cannot distinguish a miss from a programming error (`:226-227`).
- Coordinates are **not** clamped. An out-of-bounds `x`/`y` is a GL error and leaves the local
  `pixel` at its `-1` initialiser (`:231`).
- It is a **synchronous** read-back: it stalls the pipeline. One per click, not one per frame.

### `FrameBuffer::ClearAttachment`

```cpp
virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;
```

**What it does** — `glClearBufferiv` on one colour attachment, replicating `value` across all four
components (`OpenGLFrameBuffer.cpp:236-246`).

**Why you'd use it** — **`glClear` does not reliably clear integer attachments**, so an ID
attachment has to be cleared explicitly every frame after `Bind()`, normally to `-1`.

**Notes & pitfalls**
- **The FBO must be bound.** Out-of-range index is a silent no-op (`:238-239`).
- `attachmentIndex` doubles as the **draw-buffer** index, which is only equivalent because
  `glDrawBuffers` stored `GL_COLOR_ATTACHMENT0 + i` at slot `i` — and that only happens when there
  is **more than one** colour attachment (`:159-166`). On a single-attachment FBO the draw-buffer
  table is GL's default; clearing index 0 still works, higher indices do not exist.
- It writes *integers*. Calling it on an `RGBA8` or `RGBA16F` attachment is a type mismatch GL will
  reject.

### `FrameBuffer::ReadDepth`

```cpp
virtual float ReadDepth(int x, int y) = 0;
```

**What it does** — reads one window-space depth value in `[0, 1]` from the depth attachment
(`OpenGLFrameBuffer.cpp:248-258`).

**Why you'd use it** — reconstructing the world point under the cursor for orbit-about-cursor and
zoom-to-cursor navigation.

**Notes & pitfalls**
- **Returns `1.0` (the far plane, "nothing was drawn here") when there is no depth attachment**
  (`:250-251`), and equally when the read simply misses geometry. **You cannot distinguish the two.**
- Same y-flip contract as [`ReadPixel`](#framebufferreadpixel), and the same
  must-already-be-bound requirement.
- No `glReadBuffer` call is needed — depth reads come from the depth buffer directly.
- The value is **non-linear** window-space depth. Convert with the projection's near/far before
  using it as a distance.

### `FrameBuffer::ReadPixels`

```cpp
virtual bool ReadPixels(uint32_t attachmentIndex, std::vector<uint8_t>& outRGBA,
                        uint32_t& outWidth, uint32_t& outHeight) = 0;
```

**What it does** — reads a whole colour attachment into 8-bit RGBA, **row-major with a top-left
origin** (GL's bottom-left rows are flipped for you), ready for `stb_image_write`
(`OpenGLFrameBuffer.cpp:260-294`). HDR (`RGBA16F`) attachments are converted and clamped to 8-bit by
the driver.

**Why you'd use it** — thumbnail generation and screenshot capture.

**Notes & pitfalls**
- **Returns `false` only for an out-of-range attachment index** (`:263-264`), leaving the outputs
  untouched.
- **A 0×0 framebuffer returns `true` with an EMPTY vector** and `outWidth`/`outHeight` set to 0
  (`:270-274`). `true` does not mean you got pixels — check `outRGBA.empty()`.
- The FBO must be bound. It sets `GL_PACK_ALIGNMENT` to 1 and does not restore it (`:279`).
- Allocates `w * h * 4` bytes and does a full synchronous read-back plus a CPU row flip. Not a
  per-frame operation.

---

## `RenderCommand`

Declared in `Cosmic/src/renderer/RenderCommand.h`. An all-static, header-inline dispatcher that
forwards to the active `RendererAPI*` (`RenderCommand.h:301`, initialised at static-init time in
`RenderCommand.cpp`). **This is the only sanctioned way to issue GL state changes and draws from
feature code** — no `gl*` call belongs outside `platform/OpenGL/`.

`RenderCommand` re-exports the backend enums so callers never name `RendererAPI`
(`RenderCommand.h:214-218`): `RenderCommand::CullMode`, `::BlendMode`, `::PolygonMode`,
`::PrimitiveTopology`, `::GpuBarrier`.

> **The restore contract, once for all state verbs.** `Init()` establishes the engine defaults:
> **blend Alpha, depth test ON, depth write ON, cull None, polygon Fill.** Any pass that changes one
> **must restore it before its scope ends** — `Renderer2D`'s batches assume src-alpha-over blending
> and the default depth state when they flush. Note that `OpenGLRendererAPI::Init` only explicitly
> enables blending, sets the alpha func, enables depth test, and enables
> `GL_PROGRAM_POINT_SIZE` (`OpenGLRendererAPI.cpp:15-24`); cull-None, depth-write-ON and
> polygon-Fill are the *GL* defaults rather than anything `Init` sets. The engine-default claim
> holds; the mechanism is inheritance, not assignment.

### `RenderCommand::Init`

```cpp
inline static void Init();
```

**What it does** — enables blending with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, enables depth
testing, and enables `GL_PROGRAM_POINT_SIZE` so vertex shaders can set `gl_PointSize`
(`OpenGLRendererAPI.cpp:15-24`).

**Notes & pitfalls**
- **`Renderer::Init()` calls this for you** (`Renderer.cpp:25`). An `Application` host never calls
  it directly.
- Calling it twice is harmless (idempotent GL enables).

### `RenderCommand::SetViewport`

```cpp
inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
```

**What it does** — `glViewport`. `x`/`y` are offsets from the **bottom-left** corner.

**Notes & pitfalls**
- Viewport state is **global**, not per-framebuffer. Binding an FBO does not set it; neither does
  `TextureCube::BeginRenderToFace`'s counterpart restore it. Set it after every target switch.

### `RenderCommand::SetClearColor` / `Clear`

```cpp
inline static void SetClearColor(const glm::vec4& color);
inline static void Clear();
inline static void Clear(float r, float g, float b);
```

**What it does** — `Clear()` wipes **colour and depth** with the stored clear colour
(`OpenGLRendererAPI.cpp:56-59`). The three-float overload sets the colour (alpha forced to 1) and
clears in one call (`RenderCommand.h:108-112`).

**Notes & pitfalls**
- **`Clear()` does not clear the stencil buffer**, despite `DEPTH24STENCIL8` attachments carrying
  one.
- **It does not reliably clear `RED_INTEGER` attachments either** — use
  [`FrameBuffer::ClearAttachment`](#framebufferclearattachment).
- `Clear()` respects the current depth **mask**: with depth writes disabled, the depth clear is a
  no-op. Restore depth write before clearing.

### `RenderCommand::SetDepthTest` / `SetDepthWrite`

```cpp
inline static void SetDepthTest(bool enabled);
inline static void SetDepthWrite(bool enabled);
```

**What it does** — `glEnable/glDisable(GL_DEPTH_TEST)` and `glDepthMask`
(`OpenGLRendererAPI.cpp:69-78`). Both default ON.

**Why you'd use it** — a sky gradient drawn without depth; transparent 3D geometry drawn with the
test on and writes off.

**Notes & pitfalls**
- Restore both. `Renderer2D` relies on the defaults being in place at flush time.
- Disabling the *test* also disables depth *writes* in GL, regardless of the mask — the two are not
  independent in the direction you might expect.

### `RenderCommand::SetCullMode`

```cpp
inline static void SetCullMode(RendererAPI::CullMode mode);   // CullMode { None = 0, Back, Front }
```

**What it does** — `None` disables face culling; `Back`/`Front` enable it and set `glCullFace`
(`OpenGLRendererAPI.cpp:80-96`).

**Notes & pitfalls**
- **`None` must stay the engine-wide default** — 2D sprites flip winding via `FlipX`/`FlipY`, so a
  leaked `Back` makes half your sprites vanish. Opaque 3D and shadow passes enable it and restore.
- There is no `FrontAndBack`.

### `RenderCommand::SetBlendMode`

```cpp
inline static void SetBlendMode(RendererAPI::BlendMode mode);  // BlendMode { Alpha = 0, Additive, Off, Multiply }
```

**What it does** (`OpenGLRendererAPI.cpp:98-118`):

| Mode | GL | Use |
| --- | --- | --- |
| `Alpha` | `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` | **the engine default** — src-alpha-over |
| `Additive` | `GL_SRC_ALPHA, GL_ONE` | emissive / particle accumulation |
| `Off` | `glDisable(GL_BLEND)` | opaque passes that must not read the destination |
| `Multiply` | `GL_DST_COLOR, GL_ZERO` | the 2D light-buffer darkening composite |

**Notes & pitfalls**
- `Off` **disables** blending rather than setting a one/zero function — the distinction matters if
  you were relying on a blend equation elsewhere.
- Restore `Alpha` before handing the frame back.

### `RenderCommand::SetPolygonMode`

```cpp
inline static void SetPolygonMode(RendererAPI::PolygonMode mode);   // PolygonMode { Fill = 0, Line }
```

**What it does** — `glPolygonMode(GL_FRONT_AND_BACK, …)` — core profile accepts nothing else
(`OpenGLRendererAPI.cpp:120-125`). `Line` is the wireframe debug view.

**Notes & pitfalls**
- **Restore `Fill`.** A fullscreen post/composite triangle rasterised as lines is a *blank frame*,
  not an error the driver reports — the hardest failure in this group to diagnose.

### `RenderCommand::DrawIndexed`

```cpp
inline static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t count = 0,
                               uint32_t indexOffset = 0);
```

**What it does** — `glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset)`. `count == 0`
means "the VAO's whole index buffer"; `indexOffset` is in **elements** and is multiplied by 4
internally (`OpenGLRendererAPI.cpp:137-147`).

**Why you'd use it** — the standard draw. `indexOffset` + `count` together draw **one submesh
range** — the material-slots mechanism.

**Notes & pitfalls**
- **The caller binds the VAO.** `DrawIndexed` does not (`OpenGLRendererAPI.cpp:156-158`).
- **`count == 0` dereferences `GetIndexBuffer()`** (`:140`). A VAO with no index buffer crashes
  here.
- Topology is hardcoded `GL_TRIANGLES`. For points or lines use
  [`DrawArrays`](#rendercommanddrawarrays) or [`DrawLines`](#rendercommanddrawlines).
- Nothing validates `indexOffset + count` against the buffer.

### `RenderCommand::DrawLines`

```cpp
inline static void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount);
```

**What it does** — `glDrawArrays(GL_LINES, 0, vertexCount)` — **non-indexed**
(`OpenGLRendererAPI.cpp:160-163`).

**Notes & pitfalls**
- `vertexCount` is a **vertex** count, and each line consumes two. An odd count drops the last
  vertex.
- The `vertexArray` parameter is **unused by the implementation** — it exists for the API shape. The
  caller must still have bound the VAO.

### `RenderCommand::DrawIndexedInstanced`

```cpp
inline static void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray,
                                        uint32_t indexCount,
                                        uint32_t instanceCount);
```

**What it does** — `glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
instanceCount)` (`OpenGLRendererAPI.cpp:167-174`).

**Notes & pitfalls**
- **`indexCount` has no `0` default here** — you must pass the real count; there is no
  "whole buffer" shorthand.
- **Always draws from index 0** — no `indexOffset` parameter, so you cannot instance a submesh
  range.
- Per-instance data comes either from VAO attributes with a divisor (`BufferElement::Instanced`) or
  from an SSBO read by `gl_InstanceID` — the engine's `InstanceSet` uses the SSBO route at
  [`Bindings::InstancesSsbo`](#bindingpoints).

### `RenderCommand::DrawArrays`

```cpp
inline static void DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count);
// PrimitiveTopology { Points, Lines, Triangles }
```

**What it does** — an **attribute-less** array draw. Core GL still requires a bound VAO, so the
platform layer lazily creates and binds a private empty one (`OpenGLRendererAPI.cpp:195-210`).

**Why you'd use it** — geometry synthesised from `gl_VertexID`: the classic three-vertex fullscreen
triangle (`DrawArrays(Triangles, 0, 3)`, as the BRDF LUT bake does at `EnvironmentMap.cpp:93`), and
GPU particles rendered as points straight out of an SSBO.

**Notes & pitfalls**
- **It binds its own private VAO**, replacing whatever you had bound. Re-bind yours afterwards.
- `GL_PROGRAM_POINT_SIZE` is enabled at `Init`, so a `Points` draw honours `gl_PointSize` set in the
  vertex shader.

### `RenderCommand::DispatchCompute`

```cpp
inline static void DispatchCompute(uint32_t x, uint32_t y, uint32_t z);
```

**What it does** — `glDispatchCompute(x, y, z)` over a grid of **work groups** (not threads)
(`OpenGLRendererAPI.cpp:180-183`).

**Notes & pitfalls**
- **The bound program must be a compute program** — one built from a `#type compute` block
  (`OpenGLShader.cpp:32`). Dispatching a graphics program is a GL error, not an engine diagnostic.
- Arguments are **work groups**. Total invocations are `x*y*z × local_size_x*y*z` from the shader's
  own `layout(local_size_…)`. Divide your element count by the local size and round up.
- Follow with [`GpuMemoryBarrier`](#rendercommandgpumemorybarrier) before anything reads the result.

### `RenderCommand::GpuMemoryBarrier`

```cpp
inline static void GpuMemoryBarrier(GpuBarrier bits);
// GpuBarrier : uint32_t { VertexAttribArray = 1<<0, ShaderStorage = 1<<1, ShaderImage = 1<<2, All = 0xFFFFFFFF }
```

**What it does** — inserts a GL memory barrier so compute writes become visible to the named
consumers (`OpenGLRendererAPI.cpp:185-193`). `operator|` and `operator&` are defined for the enum
(`RendererAPI.h:240-247`).

**Notes & pitfalls**
- **Named `GpuMemoryBarrier`, not `MemoryBarrier`, because `<winnt.h>` macro-defines the latter** —
  the plain name would break any TU that includes `windows.h`.
- `All` is special-cased to `GL_ALL_BARRIER_BITS`, and the check is `bits == GpuBarrier::All`, so
  `All | ShaderStorage` still takes the `All` path only by exact equality of the OR'd value
  (`:191`). Pass `All` alone.
- Omitting the barrier does not error — it gives you last-frame's data, intermittently.

### `RenderCommand::BindTextureSlot`

```cpp
inline static void BindTextureSlot(uint32_t slot, uint32_t rendererID);
```

**What it does** — `glActiveTexture(GL_TEXTURE0 + slot)` then binds `rendererID` to that unit's 2D
target (`OpenGLRendererAPI.cpp:216-222`).

**Why you'd use it** — binding a **raw handle** that is not a `Ref<Texture2D>`: an FBO colour or
depth attachment in a post-process pass, or the shadow map.

**Example**

```cpp
Cosmic::RenderCommand::BindTextureSlot(0, sceneFbo->GetColorAttachmentRendererID(0));
tonemapShader->SetInt("u_Scene", 0);
```

**Notes & pitfalls**
- **Set the sampler uniform to the same `slot`.** Binding alone does nothing.
- `rendererID == 0` unbinds the unit (samples black); it is not an error.
- Respect the reserved units — see [`BindingPoints`](#bindingpoints).

### `RenderCommand::BindTextureCubeSlot`

```cpp
inline static void BindTextureCubeSlot(uint32_t slot, uint32_t rendererID);
```

**What it does** — the same, for `GL_TEXTURE_CUBE_MAP` (`OpenGLRendererAPI.cpp:224-228`).

**Notes & pitfalls**
- **Never point a `sampler2D` and a `samplerCube` at the same unit.** Two sampler *types* on one
  unit is a draw-time `GL_INVALID_OPERATION` — lenient on NVIDIA, fatal on Mesa/ANGLE-class drivers.
  This is why `Renderer3D::ApplySceneBindings` assigns the IBL cube units *unconditionally*, even
  when IBL is off (`Renderer3D.cpp:994-1004`).

### `RenderCommand::GetBoundFramebuffer` / `BindFramebufferHandle`

```cpp
inline static uint32_t GetBoundFramebuffer();
inline static void     BindFramebufferHandle(uint32_t id);
```

**What it does** — query and restore the bound **draw** framebuffer
(`GL_DRAW_FRAMEBUFFER_BINDING`) (`OpenGLRendererAPI.cpp:230-240`).

**Why you'd use it** — save/restore around a nested render target. The post-process stack tonemaps
into an intermediate then rebinds the caller's target for the final pass; the same pattern makes
`FrameBuffer::Unbind` and `TextureCube::FinishRender` safe to use inside an existing pass.

**Example**

```cpp
const uint32_t previous = Cosmic::RenderCommand::GetBoundFramebuffer();
cube->BeginRenderToFace(face);
// … bake …
cube->FinishRender();                                       // binds 0, NOT `previous`
Cosmic::RenderCommand::BindFramebufferHandle(previous);     // restore
```

**Notes & pitfalls**
- `GetBoundFramebuffer` is a `glGetIntegerv` — a driver round-trip. Call it once per pass, not per
  draw.
- It reports the draw binding only; a separately bound *read* framebuffer is not captured.

### `RenderCommand::BeginGpuZone` / `EndGpuZone` / `GpuFrameMark` / `GetGpuZoneResults`

```cpp
inline static void BeginGpuZone(const char* name);
inline static void EndGpuZone();
inline static void GpuFrameMark();
inline static const std::vector<GpuZoneResult>& GetGpuZoneResults();
```

```cpp
struct GpuZoneResult { std::string Name; float Milliseconds = 0.0f; uint32_t Depth = 0; };
```

**What it does** — scoped GPU timer zones built on `GL_TIMESTAMP` queries, which (unlike
`GL_TIME_ELAPSED`) may nest. `GpuFrameMark` closes the frame just recorded, pushes it into an
in-flight ring, and resolves the **oldest** frame *only when its last query is available* — never a
stall (`OpenGLRendererAPI.cpp:254-358`). `GetGpuZoneResults` returns the most recently resolved
frame; `Depth` is the nesting level for HUD indentation.

**Example**

```cpp
Cosmic::RenderCommand::BeginGpuZone("Shadow pass");
// … draws …
Cosmic::RenderCommand::EndGpuZone();
```

**Notes & pitfalls**
- **Results are a few frames old and the vector is EMPTY until the first resolve.** These are not
  this frame's numbers; do not drive logic from them.
- `SceneRenderer::Render` calls `GpuFrameMark` once per frame. **If you use zones outside a
  `SceneRenderer` frame, nothing ever resolves** and the ring force-drops the oldest after 3 frames
  (`kMaxPendingFrames`, `:254`, `:330`).
- **An unbalanced `EndGpuZone` is ignored** (`:293-294`); an unbalanced `BeginGpuZone` has its stack
  cleared at the next `GpuFrameMark` (`:308`) so it cannot corrupt the following frame — but that
  zone's timing is lost.
- A frame with **zero** zones is not pushed at all (`:309-310`), so `GetGpuZoneResults` keeps
  returning the last non-empty frame.
- `name` is copied into a `std::string` per zone per frame. Keep zones coarse.

---

## `Renderer`

Declared in `Cosmic/src/renderer/Renderer.h`. Two unrelated things in one class: **the renderer
lifecycle** (which you want) and **a legacy un-batched submission path** (which you almost certainly
do not).

> **Legacy path warning, from the header itself (`Renderer.h:19-31`).** `Renderer::Submit` is the
> original one-draw-call-per-submission path, and its camera is tracked **separately** from
> `Renderer2D`'s. `Renderer::BeginScene` has no effect on `Renderer2D` and vice versa — mixing
> `Renderer::Submit` with `Renderer2D::DrawQuad` in one frame silently draws the two sets under
> different cameras. Prefer `Renderer2D` for all normal 2D drawing and `Renderer3D` for meshes.

`Renderer` is **not** `COSMIC_API`-exported (see
[the export table](#which-of-these-types-are-actually-dll-exported)).

### `Renderer::Init`

```cpp
static void Init();
```

**What it does** — `RenderCommand::Init()`, then `Renderer2D::Init()`, then — in the 3D
configuration only — `Renderer3D::Init()` (`Renderer.cpp:23-30`).

**Notes & pitfalls**
- **`Application::Initialize` already calls it** (`Application.cpp:569`), immediately before the
  workspace framebuffer is created. A project layer must not call it.
- The `Renderer3D::Init()` line is inside `#ifndef COSMIC_2D_ONLY` (`Renderer.cpp:27-29`) — the only
  configuration-dependent thing in this header's implementation.
- Requires a current GL context; `Application` creates the window first.

### `Renderer::Shutdown`

```cpp
static void Shutdown();
```

**What it does** — `Renderer2D::Shutdown()`, then `Renderer3D::Shutdown()` (3D only), then
`Light2DRenderer::Shutdown()` (`Renderer.cpp:36-43`).

**Notes & pitfalls**
- Called by `Application` at `Application.cpp:430`, **while the GL context is still live** — that is
  the whole point of the ordering. Every `Ref` to a GPU resource must already be gone or be released
  by these calls.
- `Light2DRenderer::Shutdown` is called unconditionally, in both configurations.

### `Renderer::OnWindowResize`

```cpp
static void OnWindowResize(uint32_t width, uint32_t height);
```

**What it does** — `RenderCommand::SetViewport(0, 0, width, height)` and
`Renderer2D::SetViewportSize(width, height)` so 2D shader coordinates stay scaled
(`Renderer.cpp:50-54`).

**Notes & pitfalls**
- `Application::OnWindowResize` calls it (`Application.cpp:658`). Your framebuffers are **not**
  resized by it — that is the layer's job.

### `Renderer::BeginScene` / `EndScene`

```cpp
static void BeginScene(OrthographicCamera& camera);
static void EndScene();
```

**What it does** — `BeginScene` captures the camera's view-projection into a file-static
`SceneData` (`Renderer.cpp:63-66`). **`EndScene` does nothing** — it is a placeholder for future
command sorting (`:73-76`).

**Notes & pitfalls**
- **Orthographic only.** There is no perspective overload; this path predates 3D.
- The camera is taken by **non-const reference** even though it is only read.
- State is a single process-wide static. No nesting, no reentrancy.

### `Renderer::Submit`

```cpp
static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Ref<Texture>& texture, const glm::mat4& transform = glm::mat4(1.0f));
static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));
static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f));
static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, float rotationDegrees, const glm::vec3& scale = glm::vec3(1.0f));
```

**What it does** — binds the shader, uploads `u_ViewProjection` from `BeginScene`'s captured matrix
and `u_Transform`, binds `texture` to **slot 0** and sets `u_Texture` to 0 if non-null, binds the
VAO, and issues one `DrawIndexed` (`Renderer.cpp:88-102`). The three convenience overloads build a
transform and forward to the first with a null texture (`:108-138`).

**Why you'd use it** — as an escape hatch for custom-shader geometry the batch renderer cannot
express, driven by its own `BeginScene`/`EndScene`.

**Example**

```cpp
Cosmic::Renderer::BeginScene(m_Camera);                     // its OWN camera state
Cosmic::Renderer::Submit(m_Shader, m_Vao, glm::vec3(0.0f, 0.0f, 0.0f), 45.0f, glm::vec3(2.0f));
Cosmic::Renderer::EndScene();                               // no-op today
```

**Notes & pitfalls**
- **One GPU draw call per `Submit`.** No batching, no sorting, no culling.
- **No null guards at all.** A null `shader` or `vertexArray` dereferences immediately
  (`Renderer.cpp:90`, `:100`). A null `texture` is the one thing it does check (`:94`).
- **The uniform names are hardcoded**: `u_ViewProjection`, `u_Transform`, `u_Texture`. Your shader
  must use exactly those. Note `u_Transform`, **not** `u_Model` — the 3D path's name.
- `rotationDegrees` rotates about **Z only** (`:134`).
- A `Material` used here needs [`BindFull`](#materialbindfull) called yourself first; `Submit` takes
  a `Shader`, not a `Material`, and will then overwrite `u_ViewProjection`/`u_Transform` on top.

### `Renderer::GetAPI`

```cpp
inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
```

**What it does** — the active backend enum: `None`, `OpenGL` or `DirectX`. It is `OpenGL` on every
platform today (`RendererAPI.cpp:17-23`).

**Why you'd use it** — backend-specific branching. In practice, nothing needs it; the factories
already dispatch.

---

## `BindingPoints`

Declared in `Cosmic/src/renderer/BindingPoints.h`, namespace `Cosmic::Bindings`. **The single source
of truth for every UBO/SSBO binding index and reserved sampler unit the engine claims.**

GLSL cannot consume these C++ constants — shaders hardcode the number in their
`layout(…, binding = N)` — so **the registry is a convention, not an enforcement**. Any new block
must claim its slot in that header *first*.

Rules (`BindingPoints.h:18-21`):
- **One owner per slot.** Engine systems allocate from the top of the file; apps use the `App*`
  slots, which the engine never binds.
- **UBO and SSBO indices are separate namespaces in GL.** Overlap *between* the two tables is fine
  (`LightsUbo` 0 and `AppSsbo0` 0 coexist); overlap *within* one is not.

### The registry

| Constant | Kind | Index | Shader-side name | Who binds it | When |
| ---: | --- | ---: | --- | --- | --- |
| `Bindings::LightsUbo` | UBO (std140) | **0** | `LightsBlock` (`GpuLightsBlock`, 560 B) | `Renderer3D` | `SetLights` / `SetLightDirection` / `SetAmbient`, re-bound at `BeginScene` (`Renderer3D.cpp:244`, `:1142`, `:1228`) |
| `Bindings::CameraUbo` | UBO (std140) | **1** | `CameraBlock`, instance `u_Camera` | `Renderer3D` | `BeginScene`, once per scene (`Renderer3D.cpp:258`, `:328`) |
| `Bindings::AppSsbo0` | SSBO (std430) | **0** | app-defined | **your app** — the engine never binds SSBOs in the app range [0, 7] | whenever you like |
| `Bindings::ParticlesSsbo` | SSBO (std430) | **8** | the GPU particle pool | `ParticleSystem` | at pool creation; read/written by `ParticleUpdate.glsl` (compute) and the billboard vertex stage (`ParticleSystem.cpp:294`) |
| `Bindings::InstancesSsbo` | SSBO (std430) | **9** | `{ mat4 Model; vec4 Tint; }` array, 80 B/instance | `InstanceSet` | at upload; read by `PBRInstanced.glsl` / `ShadowDepthInstanced.glsl` via `gl_InstanceID` (`InstanceSet.cpp:37`) |
| `Bindings::SkinningSsbo` | SSBO (std430) | **10** | joint-palette `mat4` array, indexed `u_SkinBase + joint` | `Renderer3D` (all queued draws' palettes at `Flush`) and `ShadowMap` (per caster, base 0) | `Renderer3D.cpp:803`, `ShadowMap.cpp:136` |
| `Bindings::TexUnitIblIrradiance` | sampler unit | **8** | `u_IrradianceMap` (`samplerCube`) | `Renderer3D::ApplySceneBindings` | every material draw — **unit assigned unconditionally**, texture bound only when IBL is active (`Renderer3D.cpp:1002-1009`) |
| `Bindings::TexUnitIblPrefilter` | sampler unit | **9** | `u_PrefilterMap` (`samplerCube`) | `Renderer3D::ApplySceneBindings` | as above |
| `Bindings::TexUnitIblBrdfLut` | sampler unit | **10** | `u_BrdfLut` (`sampler2D`) | `Renderer3D::ApplySceneBindings` | as above |
| `Bindings::TexUnitShadowMap` | sampler unit | **11** | `u_ShadowMap` (`sampler2D`) | `Renderer3D::ApplySceneBindings`, **and the flat Lambert path** | every draw; unit assigned unconditionally (`Renderer3D.cpp:1019-1022`, `:706`) |
| `Bindings::TexUnitSnowMask` | sampler unit | **12** | `u_SnowMaskMap` (`sampler2D`, RG = coverage + encoded top-surface Y) | `Renderer3D::SetSnow` via `ApplySceneBindings` | pushed to PBR / PBRInstanced / Terrain; bound only when the `SnowDesc` supplies a mask (`Renderer3D.cpp:1037`) |
| `Bindings::TexUnitOutlineMask` | sampler unit | **13** | `u_IdMask` (`isampler2D`) — the `ScenePicker`'s `RED_INTEGER` attachment | `SceneRenderer::PassOutline` | the `Outline.glsl` composite only (`SceneRenderer.cpp:814-815`) |

Line references in `BindingPoints.h`: `:35`, `:42`, `:51`, `:56`, `:62`, `:69`, `:82`, `:84`, `:86`,
`:88`, `:94`, `:99`.

### Using it

```cpp
// C++: claim the slot by name, never by literal.
auto ubo = Cosmic::UniformBuffer::Create(sizeof(MyBlock), Cosmic::Bindings::CameraUbo);
```

```glsl
// GLSL: the number is hardcoded and MUST match the registry.
layout(std140, binding = 1) uniform CameraBlock { mat4 u_ViewProjection; vec4 u_CameraPos; } u_Camera;
```

**Notes & pitfalls**
- **Sampler units 8–13 are reserved. `Material::BindFull` allocates from unit 0 upward**
  (`Material.cpp:74-81`), so **a material carrying nine or more textures collides with
  `TexUnitIblIrradiance`.** The high numbering is exactly what buys headroom; GL guarantees only 16
  fragment units, so the practical ceiling for material textures is 8.
- **The reserved sampler *uniforms* are assigned unconditionally**, even when the feature is off.
  That is a portability rule, not an oversight: leaving a `samplerCube` at its default unit 0
  alongside a `sampler2D` is a draw-time `GL_INVALID_OPERATION` on strict drivers.
- **`SceneRenderer` claims no slots of its own** beyond `TexUnitOutlineMask` — it orchestrates the
  `Renderer3D` / `EnvironmentMap` / `ShadowMap` / `PostProcessStack` passes, which already own
  everything above (`BindingPoints.h:101-103`).
- Nothing detects a collision. A shader that declares `binding = 9` for its own block silently
  fights `InstancesSsbo`, and the symptom is corrupted transforms, not an error.
- This registry is also the seed for a future backend's descriptor-set layout — another reason to
  keep every claim in the header rather than in a literal at the call site.

---

*See also:* [`../guide/materials-and-shaders.md`](../guide/materials-and-shaders.md) (the guide —
worked examples, the shader-preprocessor contract, `.cmat` authoring, material slots) ·
[rendering-2d.md](rendering-2d.md) (`Renderer2D`, whose flush is where 2D materials are read) ·
[rendering-3d.md](rendering-3d.md) (`Renderer3D`, `Mesh`, `Model`, `InstanceSet`, the submission
queue) · [rendering-pipeline.md](rendering-pipeline.md) (`SceneRenderer`, `PostProcessStack`,
`EnvironmentMap`, `ShadowMap` — the systems that consume most of the reserved bindings) ·
[cameras.md](cameras.md#gizmo) (`Gizmo`, which lives under `graphics/` but is documented there) ·
[assets-io.md](assets-io.md) (`AssetLibrary` caching and the VFS) ·
[ecs.md](ecs.md) (`MeshRendererComponent`, `SpriteRendererComponent`) ·
[core.md](core.md) (`Application`, which owns `Renderer::Init`/`Shutdown` and the workspace FBO) ·
[root README §35](../../README.md#dg-6--the-renderer-stack) (**DG-6**, the renderer class diagram)

---
*Changelog:*
- 2026-07-26 — **D8**: chapter written from the headers. Scope expanded with `graphics/MaterialAsset.h`
  (D61 integration). `BindingPoints` registry and the `Material::Clone` / deferred-read contract
  established here as the single home other chapters link.
