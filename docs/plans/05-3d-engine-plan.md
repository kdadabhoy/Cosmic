# 3D Engine Plan — Full 3D Capability Roadmap

> **Rewritten 2026-07-01** (supersedes the "sim viewport + parked full tier" version; S1/S2 history
> preserved below). **Goal:** take Cosmic from today's sim-grade viewport to a genuine 3D engine
> tier: PBR lighting, terrain, water, snow/weather, volcanic/emissive effects, CAD-style navigation
> (SolidWorks feel), gizmos, and a credible path toward Unity-class editor workflows — **without
> ever breaking the 2D pipeline or the shipped apps.**
>
> Every stage below is broken into PR-sized items with acceptance criteria so a single item can be
> handed to an AI as one prompt. Items marked **[filler]** are safe to do out of order.
>
> **2026-07-02:** §3 (S4 — roadmap Phase 7) expanded into explicit, code-verified work orders
> (exact files, signatures, step lists, gotchas, acceptance procedures) so each item can be
> executed by a lower-tier model in one session. Added **S4.0** (GLAD loader regen — see the §0
> loader note) and split S4.4 into **S4.4a/S4.4b**. Later stages (S5+) stay at their original
> altitude; give them the same treatment when their phase comes up.

---

## 0. Graphics API decision — OpenGL now, Vulkan behind a gate

**DECIDED (2026-07-01): stay on OpenGL 4.5 core through stage S12. No Vulkan rewrite.**

> **Loader reality check (2026-07-02):** the *context* is already 4.5 core
> (`Window.cpp` hints `GLFW_CONTEXT_VERSION 4.5` + core profile) and shaders compile as
> `#version 450`, but the vendored GLAD loader (`Cosmic/dependencies/glad/`) is GL 3.3-era: it
> *does* expose the UBO/MRT entry points S4.5/S4.6 need (`glUniformBlockBinding`,
> `glBindBufferBase`, `glDrawBuffers`, `glClearBufferiv`), but **not**
> `glDispatchCompute`/`glMemoryBarrier` or any GL 4.x function. **S4.0** regenerates GLAD for
> 4.5 core; only **S4.7** is blocked on it. The decision above is unchanged.
> **Resolved same day:** S4.0 shipped 2026-07-02 — the loader is now GL 4.5 core (glad 0.1.36);
> the 4.3+ entry points are available.

**Why:** "Realistic volcanoes / water / snow" is a *techniques and content* problem, not an API
problem. Every technique in this plan (PBR+IBL, CSM shadows, FFT water, GPU particles, volumetric
fog, terrain clipmaps) shipped in AAA titles on GL4/D3D11-class hardware. A Vulkan port would
freeze visible feature work for ~2–3 months to reproduce what `platform/OpenGL/` already does,
with zero visual payoff. Vulkan pays off for *CPU-bound many-draw-call scenes* and multi-threaded
command recording — problems Cosmic does not have yet (batched 2D + tens-to-hundreds of 3D draws).

**What we adopt NOW so a future backend stays cheap (binding rules):**
1. **No raw `gl*` calls outside `platform/OpenGL/`.** Everything goes through
   `RendererAPI`/`RenderCommand` verbs (`SetDepthTest`, `SetCullMode`, `DispatchCompute`, …).
   Audit item: S13.1.
2. **No GL enums/types in public engine headers.** Formats, blend modes, texture params are engine
   enums translated in the platform layer (the `FrameBuffer`/`Texture2D` specs already do this —
   keep it that way).
3. **Shaders keep the `#type` contract** (README §10) and the canonical attribute layout; when we
   need post-GL portability we transpile — we do not hand-write two shader dialects.
4. **New GPU features land as `RendererAPI` verbs first** (compute, SSBO, indirect draw, timer
   queries), never as one-off GL calls in feature code.

**Reopen the decision (S13 gate) only when one of these is true:**
- Render-thread CPU time becomes the measured frame limiter after S12's culling/instancing/sorting.
- A required feature is GL-impossible or driver-broken (mesh shaders, hardware ray tracing,
  reliable multi-threaded resource streaming).
- A non-Windows target matters (then evaluate Vulkan-native vs. an RHI like bgfx/Diligent vs. ANGLE).

Alternatives considered: **Vulkan now** (rejected: months of parity work, kills momentum);
**bgfx/Diligent adoption now** (rejected: replaces a working renderer with a dependency and its
abstractions before we know our own requirements); **D3D11 backend** (rejected: same cost as
Vulkan, fewer future options).

---

## 1. Forward-compatibility contract (binding on all stages)

Carried forward from the original plan and extended — everything already shipped (S1/S2) obeys it:

1. **Camera-agnostic core.** `Renderer3D::BeginScene(const glm::mat4& viewProjection, const
   glm::vec3& cameraPos)` is the primitive; typed camera overloads are sugar.
2. **Generic render-state verbs on `RenderCommand`** — never GL calls in feature code (rule 0.1).
3. **`Mesh` is a first-class GPU resource** (`Ref<Mesh>`, factory-created) with the documented
   `position, normal, uv` layout. Extensions (tangents for normal mapping, skinning weights) are
   *additive* layout versions, decided per-mesh at creation.
4. **Shaders follow the `#type` contract** and declare the canonical attribute layout.
5. **Draw calls take `glm::mat4` transforms**; attitude math stays upstream (`math/Spatial.h`).
6. **No 2D regressions.** `Renderer3D` restores any state `Renderer2D` depends on; both run in one
   frame. Every stage's acceptance re-verifies a 2D overlay renders correctly.
7. **HDR-ready:** from S6 onward the 3D scene renders into a float target and tonemaps to the
   swapchain; 2D/UI composite after tonemap.
8. **The engine ships generic systems (terrain, water, particles); apps own scenarios** (the
   volcano is a *demo app*, not an engine feature).

---

## 2. Stage map

| Stage | Theme | Status |
| --- | --- | --- |
| S1 | Perspective camera, orbit controller, 3D lines/grid/axes | ✅ done 2026-07-01 |
| S2 | Meshes + primitives + OBJ + Lambert | ✅ done 2026-07-01 |
| S3 | Sim-viewport conveniences (FPV inset, ribbon, horizon, labels) | S3.1 + S3.2 ✅ 2026-07-02 (ViperSim P5); S3.3–S3.5 unpulled |
| S4 | 3D engine foundations (cameras, materials, scene, glTF, lights, MRT, compute) | **S4.0–S4.7 ✅ code-complete 2026-07-02** (full `build_all` + `CosmicTests` 66/66 green; user visual pass of the Engine3DDemo toggles pending) |
| S5 | CAD navigation, ViewCube, gizmos, 3D picking | planned — **S5.1 nav is [filler], do any time** |
| S6 | Visual realism core: HDR, PBR+IBL, shadows, SSAO, bloom, AA | planned |
| S7 | Sky, atmosphere, fog, time-of-day | planned |
| S8 | Terrain system | planned |
| S9 | Water system | planned |
| S10 | GPU particles + volumetrics | planned |
| S11 | Weather/nature systems + flagship demos (volcano, snow, ocean) | planned |
| S12 | Performance & scale (culling, sorting, instancing, LOD, profiler) | planned |
| S13 | RHI hardening + Vulkan decision gate | gate |
| S14 | Game-engine tier backlog (animation, physics, editor app, …) | parking lot with unlock conditions |

Stages are ordered by dependency, not calendar — the sim track (S3) and the realism track (S4+)
interleave freely with the ViperSim phases in the master roadmap.

### S1 + S2 — shipped foundation *(reference)*

`PerspectiveCamera`, `OrbitCameraController` (LMB orbit / RMB pan / scroll zoom),
`Renderer3D` (batched lines, `DrawGrid/DrawAxes/DrawWireBox`, `DrawMesh` + Lambert `Mesh3D.glsl`),
`Mesh` primitives (`CreateBox/Cylinder/Cone/Plane/UVSphere`) + `CreateFromOBJ`,
`RenderCommand::SetDepthTest/SetDepthWrite`, `FrameBuffer::GetDepthAttachmentRendererID()`.
Acceptance app: `Projects/Engine3DDemo`. Details: git history of this file.

### S3 — Sim-viewport conveniences *(pull-as-needed; ViperSim P4–P5 pulled two)*

| Item | Contents |
| --- | --- |
| S3.1 FPV inset ✅ 2026-07-02 | second `Renderer3D` pass into its own `FrameBuffer`, shown via `ImGui::Image` — shipped app-side in ViperSim's FlightScreen (belly camera; FPV pass renders first, then rebinds the viewport FBO + viewport) |
| S3.2 Trajectory ribbon ✅ 2026-07-02 | `DrawPolyline` over a ring buffer (ViperSim flight trail); fade-by-age color option still open |
| S3.3 Horizon/sky gradient | full-screen gradient pass behind the scene (replaced by S7 later) |
| S3.4 Ground texture | textured ground plane (checker/asphalt) — needs `DrawMesh` + texture path (S4.2 material or a minimal textured-mesh shader) |
| S3.5 `Renderer3D::WorldToScreen(vec3)` | for SDF-font labels drawn by the 2D pass |

---

## 3. S4 — 3D engine foundations *(roadmap Phase 7 — explicit work orders)*

Ordered (S4.0 is a prerequisite only for S4.7 — do it first anyway, it's the lowest-risk PR);
each item is one PR and one AI session. All signatures/paths below were **verified against the
working tree on 2026-07-02** — re-verify by content (grep) before editing; never trust line
numbers or assume a quoted API survived intervening PRs.

### S4 execution notes *(read once — they apply to every item)*

- **Build/run/test:** `build_all.bat` = full reconfigure + engine + all project DLLs — required
  whenever an ABI-sensitive header changes (anything in `Cosmic/src/scene/Components.h`, or adding
  virtuals to a `COSMIC_API` class). `build.bat` = incremental. Outputs land in
  `build/Runtime/<Config>/`; run `CosmicApp.exe` there and pick **Engine3DDemo** in the launcher.
  Tests: `build/Runtime/<Config>/CosmicTests.exe` (doctest; headless — no window/GL, so GPU
  features are accepted via the demo app, not unit tests). Per the working agreement the user
  compiles and runs; finish the item, then request one build+test pass.
- **New engine sources need no CMake edit** — `Cosmic/CMakeLists.txt` does
  `file(GLOB_RECURSE COSMIC_SOURCES "src/*.cpp" "src/*.h" ...)`. New *vendored include dirs* DO:
  extend the existing line
  `target_include_directories(Cosmic PRIVATE dependencies/stb_image ... dependencies/miniaudio)`.
- **New GPU resources copy the factory pattern** (model: `graphics/Shader.h` + `Shader::Create`
  in `graphics/Shader.cpp`): abstract `class COSMIC_API X` in `graphics/`, concrete `OpenGLX` in
  `platform/OpenGL/`, static `Create(...)` switching on `RendererAPI::GetAPI()` and returning
  `nullptr` for `API::None`. **No `gl*`/`GL_*` tokens outside `platform/OpenGL/`** (§0 rule 1).
  Use `glGen*`/`glBind*` style in the platform layer (matches existing code; DSA `glCreate*`
  functions are absent from the loader until S4.0 and we don't switch styles after).
- **Setting a uniform the bound shader doesn't declare is safe and silent** —
  `OpenGLShader::GetUniformLocation` caches lookups and every `UploadUniform*` no-ops on location
  `-1`. The engine therefore sets convention uniforms unconditionally; shaders opt in by
  declaring them.
- **Every item's acceptance ends the same way:** Engine3DDemo runs, the item's demo works, the
  demo's **"2D overlay" toggle still renders its HUD correctly** (contract 6), and `CosmicTests`
  is green.

### S4.0 GLAD 4.5-core loader regeneration **[do first; hard prerequisite only for S4.7]**

> **✅ code complete 2026-07-02.** Generated with the official `glad==0.1.36` Python generator
> against the current Khronos `gl.xml` (GL 4.5, **core**, no extensions, loader included;
> `khrplatform.h` fetched from the Khronos EGL registry). Pre-swap audit: all 59 GL functions the
> engine calls verified present in the new header; zero compatibility-profile symbols
> (`glBegin`/`glVertex*` absent); `glDispatchCompute`/`glMemoryBarrier` present.
> `OpenGLContext::Init` now logs the actual context version + renderer (step 3 below). Verified:
> full Debug build green (engine + all project DLLs + CosmicApp) and `CosmicTests` 58/58
> (103,881 assertions). **Remaining — user visual pass:** run the launcher, Engine3DDemo, and a
> ViperSim screen; confirm identical rendering and a startup log line reporting OpenGL ≥ 4.5.
> To regenerate in the future: `pip install glad==0.1.36`, download `gl.xml` from the
> KhronosGroup/OpenGL-Registry repo into the CWD, then
> `python -m glad --generator=c --spec=gl --api="gl=4.5" --profile=core --extensions="" --out-path=out`
> (do **not** pass `--reproducible` — 0.1.36's packaged spec is broken; the local `gl.xml` is
> picked up automatically).

The context is already 4.5 core; only the function loader is stale (§0 loader note).

- **Files:** replace `Cosmic/dependencies/glad/include/glad/glad.h`,
  `include/KHR/khrplatform.h`, `src/glad.c`. CMake unchanged
  (`add_library(glad STATIC dependencies/glad/src/glad.c)` already exists).
- **Steps:**
  1. Generate with the **glad v1** web generator (https://glad.dav1d.de): Language C/C++,
     Specification OpenGL, gl **4.5** (4.6 also fine), Profile **Core**, no extensions,
     "Generate a loader" checked. **Do not use glad2** — its entry point is `gladLoadGL(...)`,
     which would break the existing `gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)` call in
     `platform/OpenGL/OpenGLContext.cpp` (find it by content).
  2. Drop the three generated files over the old ones (same directory layout).
  3. After the load call, log once (add if absent):
     `CS_CORE_INFO("OpenGL {}.{} — {}", GLVersion.major, GLVersion.minor, (const char*)glGetString(GL_RENDERER));`
  4. Sanity-grep the new `glad.h` for `glDispatchCompute` and `glMemoryBarrier` — both must exist.
- **Acceptance:** `build_all.bat`; launcher, Engine3DDemo, and one ViperSim screen render
  identically to before; startup log reports context ≥ 4.5; `CosmicTests` green.

### S4.1 Unified camera hierarchy

> **✅ code-complete 2026-07-02.** `camera/Camera.h` pure interface added; Orthographic + Perspective
> derive and mark the four getters `override`; `RenderPass`, `Renderer2D::BeginScene`, and a new
> `Renderer3D::BeginScene(const Camera&)` overload take the base. No app-side edits. Full build +
> `CosmicTests` green; all apps compile unchanged.

`Camera` interface; both cameras derive; renderer entry points accept the base. **No app-side
source changes** — all ~20 existing `BeginScene(...)` call sites compile unchanged via upcast
(the original "touches every 2D call site" wording predates this design).

- **Files:** NEW `Cosmic/src/camera/Camera.h`; MODIFY `camera/OrthographicCamera.h`,
  `camera/PerspectiveCamera.h`, `renderer/RenderPass.h`, `renderer/Renderer2D.h/.cpp`,
  `renderer/Renderer3D.h/.cpp`.
- **Spec — the new base (pure interface, no data members):**
  ```cpp
  class COSMIC_API Camera
  {
  public:
      virtual ~Camera() = default;
      virtual const glm::mat4& GetViewMatrix() const = 0;
      virtual const glm::mat4& GetProjectionMatrix() const = 0;
      virtual const glm::mat4& GetViewProjectionMatrix() const = 0;
      virtual const glm::vec3& GetPosition() const = 0;
  };
  ```
  Both cameras already have all four getters as inline non-virtual methods returning stored
  members — derive publicly from `Camera` and mark those four `override` (bodies unchanged).
- **Steps:**
  1. Add `camera/Camera.h`; derive `OrthographicCamera` and `PerspectiveCamera` from it.
  2. `RenderPass` ctor: `const OrthographicCamera&` → `const Camera&` (body already only calls
     `GetViewProjectionMatrix()`).
  3. `Renderer2D::BeginScene(const OrthographicCamera&)` → `(const Camera&)` in .h and .cpp
     (the impl only calls `GetViewProjectionMatrix()` — verified).
  4. `Renderer3D`: add overload `BeginScene(const Camera& c)` forwarding to
     `BeginScene(c.GetViewProjectionMatrix(), c.GetPosition())`. Keep the existing
     `PerspectiveCamera` overload (exact match beats base — no ambiguity).
- **Gotchas:** adding a vtable to exported classes is an **ABI change → `build_all.bat`**;
  `OrbitCameraController::GetCamera()` and `Scene::OnRender(const OrthographicCamera&)` need no
  change; do not edit any app/template code.
- **Acceptance:** `build_all.bat`; launcher (2D), Engine3DDemo (3D + 2D overlay), and a ViperSim
  screen render identically; `CosmicTests` green.

### S4.2 Material-driven meshes

> **✅ code-complete 2026-07-02.** `Renderer3D::DrawMesh(mesh, xform, Ref<Material>)` added
> (BindFull → engine convention uniforms incl. `u_NormalMatrix`); `DemoChecker3D.glsl` +
> Engine3DDemo "Material pad (S4.2)" toggle (2×2 in-code texture). Build + tests green;
> **user visual pass pending** (tinted checker renders; toggle off restores plain pad).

- **Files:** MODIFY `renderer/Renderer3D.h/.cpp`; NEW `Cosmic/assets/shaders/DemoChecker3D.glsl`;
  MODIFY `Cosmic/assets/shaders/Mesh3D.glsl` (header comment only);
  MODIFY `Projects/Engine3DDemo/src/Engine3DDemo.h/.cpp` (acceptance demo).
- **Spec:**
  ```cpp
  static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
                       const Ref<Material>& material);
  ```
  Implementation order matters: `material->BindFull()` first (binds shader, uploads the
  material's cached floats/vecs, binds textures to sequential slots — see
  `graphics/Material.cpp`), **then** upload the engine-owned uniforms through
  `material->GetShader()` so they always win:

  | Uniform | Type | Source | Notes |
  | --- | --- | --- | --- |
  | `u_ViewProjection` | mat4 | `s_Data.ViewProjection` | scene-owned |
  | `u_Model` | mat4 | `transform` param | per-draw |
  | `u_NormalMatrix` | mat3 | `glm::transpose(glm::inverse(glm::mat3(transform)))` | per-draw; NEW convention — `Mesh3D.glsl` keeps computing its own in-shader (unchanged) |
  | `u_CameraPos` | vec3 | `s_Data.CameraPos` | scene-owned |
  | `u_LightDir`, `u_Ambient` | vec3, float | `s_Data` | scene-owned, so Lambert-style materials light correctly |

  All are set unconditionally (silent-ignore rule). `u_Color` is **material-owned** — the
  material path never sets it; materials call `material->Set("u_Color", ...)`.
  Then bind the VAO and `RenderCommand::DrawIndexed` exactly like the existing color-overload
  tail of `DrawMesh` (mirror it; don't refactor the color path).
- **Acceptance demo:** in Engine3DDemo add a "Material pad (S4.2)" toggle: draw `m_Pad` with a
  `Material::Create(Shader::Create("assets/shaders/DemoChecker3D.glsl"))` material. The demo
  shader uses the canonical attribute layout, declares the table's uniforms plus
  `u_Color`/`u_Tiling` (float)/`u_Texture` (sampler2D), and renders a `v_TexCoord`-derived checker
  tinted by `u_Color`, modulated by a texture sample and Lambert-lit via `u_LightDir`/`u_Ambient`.
  Prove `BindFull()`'s texture path without adding an asset: build a 2×2 texture in code —
  `auto tex = Texture2D::Create(2, 2); uint32_t px[4] = {...}; tex->SetData(px, sizeof(px));`
  then `material->Set("u_Texture", tex)`.
- **Acceptance:** pad renders the tinted checker (custom material path) while the aircraft still
  renders via the Lambert color path; toggling the material off restores the plain pad; 2D
  overlay + `CosmicTests` green.

### S4.3 3D scene integration

> **✅ code-complete 2026-07-02.** `TransformComponent` now vec3 `Scale` + optional
> `RotationQuat`/`UseQuatRotation`; new `MeshRendererComponent` (+registered); `Scene::OnRender3D`.
> Two template vec2→vec3 assignments fixed (`TemplateTelemetryLayer`/`TemplateSpriteLayer`).
> `tests/test_components.cpp` (vec3 diagonal + quat==Euler) green; Engine3DDemo "ECS scene (S4.3)"
> toggle. `build_all` + tests green; user visual pass pending.

- **Files:** MODIFY `Cosmic/src/scene/Components.h` (**ABI break → `build_all.bat`**),
  `scene/Scene.h/.cpp`; NEW `tests/test_components.cpp` + add it to the `add_executable` list in
  `tests/CMakeLists.txt`; Engine3DDemo or template only if you want a visual (acceptance below
  works via ECS in Engine3DDemo).
- **Spec — `TransformComponent` changes (keep everything else in the struct as-is):**
  - `glm::vec2 Scale{1,1}` → `glm::vec3 Scale{1.0f, 1.0f, 1.0f}`; `GetTransform()` ends with
    `glm::scale(glm::mat4(1.0f), Scale)` (drop the vec2→vec3 promotion).
  - Add the quaternion option:
    ```cpp
    glm::quat RotationQuat{ 1.0f, 0.0f, 0.0f, 0.0f }; // used only when UseQuatRotation
    bool      UseQuatRotation = false; // Euler degrees stay the default and the 2D path
    ```
    `GetTransform()` rotation term becomes
    `UseQuatRotation ? glm::mat4_cast(RotationQuat) : <existing X·Y·Z rotate product>`.
    **Conversion policy (documented in the header):** the two representations are independent —
    no implicit sync on write; helpers deferred until an editor needs them (S14). Include
    `<glm/gtc/quaternion.hpp>`.
  - Scale consumers today read `.Scale.x/.y` and compile unchanged (verified:
    `scene/Scene.cpp` ×2, `telemetry/EntityPicker.h`, template `TemplateTelemetryLayer.cpp` /
    `TemplateSpriteLayer.cpp`) — re-grep `\.Scale` (excluding `dependencies/`) and fix any
    vec2-typed copies the compiler flags.
- **Spec — new component (copy `SpriteRendererComponent`'s shape, incl. default/copy ctors):**
  ```cpp
  struct COSMIC_API MeshRendererComponent
  {
      Ref<Mesh>     MeshAsset;            // entity skipped when null
      Ref<Material> MaterialAsset;        // null → Lambert color path
      glm::vec4     Color{ 1.0f };        // Lambert tint when MaterialAsset is null
      bool          CastShadows = true;   // consumed from S6.4; stored now so the ABI breaks once
  };
  ```
  Register it with the existing three at the bottom of Components.h:
  `CS_REGISTER_COMPONENT(Cosmic::MeshRendererComponent)`.
- **Spec — `Scene::OnRender3D`:**
  ```cpp
  void Scene::OnRender3D(const Camera& camera); // include camera/Camera.h in Scene.h
  ```
  Body: `Renderer3D::BeginScene(camera)` (S4.1 overload); iterate
  `m_Registry.view<TransformComponent, MeshRendererComponent>()`; skip null `MeshAsset`;
  `MaterialAsset ? DrawMesh(mesh, xform, material) : DrawMesh(mesh, xform, color)`;
  `Renderer3D::EndScene()`. No sorting/culling — that's S12. Does not touch `OnRender` (2D).
- **Acceptance:** Engine3DDemo gains an "ECS scene (S4.3)" toggle that builds a small `Scene`
  (3–4 entities: primitives with vec3 scales, one using `UseQuatRotation`) and renders it via
  `OnRender3D` each frame; 2D scene rendering in the launcher/templates unaffected;
  `tests/test_components.cpp` proves headlessly that (a) a vec3 scale lands in the matrix
  diagonal and (b) `UseQuatRotation` with `glm::angleAxis(glm::radians(45.f), vec3(0,1,0))`
  matches the Euler `{0,45,0}` matrix within 1e-4; `build_all.bat` + all apps run.

### S4.4a Asset cache *(closes IMPROVEMENTS §5.1)*

> **✅ code-complete 2026-07-02.** `assets/AssetLibrary.h/.cpp` (texture/shader/mesh/model caches,
> `NormalizeKey`, `Clear()` wired into Application shutdown before GL teardown). `test_assetlibrary.cpp`
> (VFS≡raw, `..` collapse, backslash, idempotence) green; Engine3DDemo "Cache check" button.
> Build + tests green.

- **Files:** NEW `Cosmic/src/assets/AssetLibrary.h/.cpp` (GLOB picks the new dir up);
  MODIFY `core/Application.cpp` (shutdown hook); NEW `tests/test_assetlibrary.cpp` + add to
  `tests/CMakeLists.txt`.
- **Spec:**
  ```cpp
  class COSMIC_API AssetLibrary
  {
  public:
      static Ref<Texture2D> GetTexture(const std::string& path); // VFS or raw path
      static Ref<Shader>    GetShader(const std::string& path);
      static Ref<Mesh>      GetMesh(const std::string& path);    // .obj via Mesh::CreateFromOBJ
      static Ref<Model>     GetModel(const std::string& path);   // .gltf/.glb — stub until S4.4b:
                                                                 // forward-declare `class Model;`
                                                                 // (Ref<T> is fine on an incomplete
                                                                 // type); stub logs + returns null

      static void           Clear();      // release all cached Refs (needs a live GL context)
      static std::string    NormalizeKey(const std::string& path); // public for tests
  };
  ```
  - `NormalizeKey` = `std::filesystem::path(FileSystem::Resolve(path)).lexically_normal().generic_string()`
    — so `engine://models/duck.glb` and `assets/models/../models/duck.glb` share one cache slot.
    Purely lexical (no disk I/O) → headless-testable.
  - Cache = one `static std::unordered_map<std::string, Ref<T>>` per type in the .cpp. Hit →
    return the stored `Ref`. Miss → load (pass the **resolved** path to the factories — they take
    real disk paths, e.g. `Mesh::CreateFromOBJ` documents this), store, return. Loader returned
    `nullptr` → `CS_CORE_ERROR` once, **don't cache the failure** (retry next call), return null.
  - `Clear()` called from `Application` shutdown next to the existing `AudioEngine::Shutdown()`
    call (find by content) — **before** GL context teardown, else the GPU handles leak with no
    context to delete them.
- **Acceptance:** doctest covers `NormalizeKey` equivalences (VFS vs raw, `..` collapse,
  backslash→forward); Engine3DDemo gains a "cache check (S4.4a)" button that calls
  `GetMesh("engine://...")` twice and logs PASS iff both `Ref.get()` pointers match;
  `CosmicTests` green.

### S4.4b glTF import (cgltf) + `Model`

> **✅ code-complete 2026-07-02.** Vendored `cgltf.h` v1.15 (MIT) + `CgltfImpl.cpp`; `graphics/Model.h/.cpp`
> (`CreateFromGLTF`: world-transform baked into verts, POSITION/NORMAL/TEXCOORD_0 + base-color,
> flat-normal fallback, non-triangle warn+skip); `Renderer3D::DrawModel`; `AssetLibrary::GetModel` real;
> committed `assets/models/Duck.glb` (120 KB Khronos sample, copies to runtime VFS); Engine3DDemo
> "glTF Duck (S4.4b)" toggle + reload-returns-same-Ref check. cgltf include dir added to CMake.
> Build + tests green; user visual pass pending (Duck renders upright at sane scale).

- **Files:** NEW `Cosmic/dependencies/cgltf/cgltf.h` (vendor the single header, MIT); NEW
  `Cosmic/src/graphics/CgltfImpl.cpp` (the one TU:
  `#define CGLTF_IMPLEMENTATION` + `#include "cgltf.h"` — mirror `src/audio/MiniaudioImpl.cpp`'s
  header comment); MODIFY `Cosmic/CMakeLists.txt` (append `dependencies/cgltf` to the
  `target_include_directories(Cosmic PRIVATE ...)` line); NEW `graphics/Model.h/.cpp`; MODIFY
  `renderer/Renderer3D.h/.cpp` (`DrawModel`), `assets/AssetLibrary.cpp` (`GetModel` real);
  NEW committed sample `Cosmic/assets/models/Duck.glb` (a small CC0/Khronos-sample glb,
  < 200 KB); Engine3DDemo demo toggle.
- **Spec:**
  ```cpp
  struct ModelPart { Ref<Mesh> Geometry; glm::vec4 BaseColor{1.0f}; std::string Name; };

  class COSMIC_API Model
  {
  public:
      static Ref<Model> CreateFromGLTF(const std::string& resolvedPath); // .gltf or .glb
      const std::vector<ModelPart>& GetParts() const;
  };

  // Renderer3D convenience: DrawMesh(part.Geometry, transform, part.BaseColor) per part
  static void DrawModel(const Ref<Model>& model, const glm::mat4& transform);
  ```
- **Import recipe (use cgltf's high-level helpers, not raw buffer views):**
  `cgltf_parse_file` → `cgltf_load_buffers` (handles .glb, external .bin, and base64) → recurse
  scene nodes; per node get `cgltf_node_transform_world(node, m[16])` and **bake it into the
  vertices** (positions by the mat4, normals by `transpose(inverse(mat3))`, renormalized) — skip
  this and multi-node models collapse at the origin. Per triangle primitive
  (`cgltf_primitive_type_triangles`; warn+skip others): read `POSITION`/`NORMAL`/`TEXCOORD_0`
  with `cgltf_accessor_read_float`, indices with `cgltf_accessor_read_index` (no indices →
  identity `0..n-1`); missing normals → flat face normals (same fallback `CreateFromOBJ` uses);
  missing UVs → `(0,0)`. `BaseColor` = the material's `pbr_metallic_roughness.base_color_factor`
  (white if absent). Each primitive → `Mesh::Create(vertices, indices)` → one `ModelPart`.
  `cgltf_free` when done. **Tangents deferred to S6.2** (additive layout change per contract
  rule 3 — ignore `TANGENT` for now). Axis note: glTF is right-handed +Y-up meters — that *is*
  the render frame; no NED conversion (`Spatial.h` is for sim state, not assets).
- **Acceptance:** Engine3DDemo "glTF (S4.4b)" toggle renders
  `AssetLibrary::GetModel("engine://models/Duck.glb")` upright at sane scale; loading it twice
  returns the same `Ref` (log PASS); 2D overlay + `CosmicTests` green.

### S4.5 Lighting v1 (Blinn-Phong forward, ≤ 16 point lights, UBO)

> **✅ code-complete 2026-07-02.** `UniformBuffer` resource (OpenGL impl, binding 0); `GpuLightsBlock`
> (`static_assert(sizeof==560)`); `MeshLit.glsl` (Blinn-Phong, std140 LightsBlock, consumes
> `u_NormalMatrix`); `Renderer3D::SetLights` + `SceneLightsDesc`/`PointLightDesc`; `Directional`/`PointLightComponent`
> (+registered); `Scene::OnRender3D` gathers + uploads. Engine3DDemo "Lighting v1 (S4.5)" section
> (sun + red/blue point lights). Build + tests green; user visual pass pending (colored highlights).

- **Files:** NEW `graphics/UniformBuffer.h` + `platform/OpenGL/OpenGLUniformBuffer.h/.cpp`;
  NEW `Cosmic/assets/shaders/MeshLit.glsl`; MODIFY `renderer/Renderer3D.h/.cpp`,
  `scene/Components.h` (**ABI → `build_all.bat`**), `scene/Scene.cpp`, Engine3DDemo.
- **Spec — the new GPU resource (factory pattern, §S4 notes):**
  ```cpp
  class COSMIC_API UniformBuffer
  {
  public:
      virtual ~UniformBuffer() = default;
      virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
      static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding); // binding = GLSL binding index
  };
  ```
  OpenGL impl: `glGenBuffers` + `glBindBuffer(GL_UNIFORM_BUFFER)` + `glBufferData(..,
  GL_DYNAMIC_DRAW)` + `glBindBufferBase(GL_UNIFORM_BUFFER, binding, id)` at create;
  `glBufferSubData` in `SetData`. All present in the current loader.
- **Spec — the std140 block, binding 0 (reserved engine-wide for lights).** Identical layout in
  GLSL and C++; **vec4s only** — never `vec3` in a UBO struct, std140 padding will silently skew
  everything after it:
  ```glsl
  layout(std140, binding = 0) uniform LightsBlock {
      vec4 u_SunDirection_Ambient;     // xyz = dir light TRAVELS (normalized), w = ambient [0,1]
      vec4 u_SunColor_Intensity;       // rgb, w = intensity
      vec4 u_PointCount;               // x = active point count (as float)
      vec4 u_PointPos_Radius[16];      // xyz world pos, w = radius
      vec4 u_PointColor_Intensity[16]; // rgb, w = intensity
  };
  ```
  C++ mirror = same five members as `glm::vec4`/arrays; `static_assert(sizeof(GpuLightsBlock) == 560)`.
- **Spec — CPU API on `Renderer3D` (UBO created in `Init` and owned by `s_Data`):**
  ```cpp
  struct PointLightDesc  { glm::vec3 Position; float Radius = 10.0f;
                           glm::vec3 Color{1.0f}; float Intensity = 1.0f; };
  struct SceneLightsDesc { glm::vec3 SunDirection{-0.4f,-1.0f,-0.3f}; // dir light TRAVELS
                           glm::vec3 SunColor{1.0f}; float SunIntensity = 1.0f;
                           float Ambient = 0.25f;
                           std::vector<PointLightDesc> Points; };    // first 16 uploaded
  static void SetLights(const SceneLightsDesc& lights); // packs + uploads immediately
  ```
  The legacy `SetLightDirection`/`SetAmbient` + Lambert `Mesh3D.glsl` path stays untouched as the
  no-material fallback.
- **Spec — components (register with `CS_REGISTER_COMPONENT`):**
  ```cpp
  struct COSMIC_API DirectionalLightComponent { glm::vec3 Direction{-0.4f,-1.0f,-0.3f};
                                                glm::vec3 Color{1.0f}; float Intensity = 1.0f; };
  struct COSMIC_API PointLightComponent       { glm::vec3 Color{1.0f}; float Intensity = 1.0f;
                                                float Radius = 10.0f; }; // position ← TransformComponent
  ```
  `Scene::OnRender3D` gathers them each call (first directional wins; ambient from
  `Renderer3D::GetAmbient()`) and calls `SetLights` before drawing.
- **Spec — `MeshLit.glsl`:** canonical attributes; consumes `u_Model`, `u_ViewProjection`,
  `u_CameraPos`, and `u_NormalMatrix` (first real consumer of the S4.2 convention — use it, do
  not recompute in-shader); material-owned uniforms `u_Color` (vec4) and `u_Shininess` (float) —
  both must be `Set` on the material (no GLSL defaults). Lighting: `ambient + sun N·(-L) +
  Σ points` with attenuation `att = pow(clamp(1 - pow(d/radius, 4), 0, 1), 2) / (d*d + 1)` and
  Blinn specular `pow(max(dot(N, H), 0), u_Shininess)`.
- **Acceptance:** Engine3DDemo "Lighting v1 (S4.5)" toggle draws the aircraft through a
  `MeshLit` material with sun-direction sliders plus two point lights (red + blue, position
  sliders) producing visibly colored highlights; toggle off restores the Lambert path;
  `build_all.bat`; 2D overlay + `CosmicTests` green.

### S4.6 Multi-attachment framebuffer (MRT) + entity-ID readback

> **✅ code-complete 2026-07-02.** `FramebufferSpecification` grown with an attachment list
> (empty ⇒ {RGBA8, DEPTH24STENCIL8} back-compat); `FramebufferTextureFormat` enum; RED_INTEGER
> (GL_NEAREST), `GetColorAttachmentRendererID(index=0)`, `ReadPixel`, `ClearAttachment` (glClearBufferiv),
> `glDrawBuffers`/`glDrawBuffer(GL_NONE)`. Entity-ID plumbed through both `DrawMesh` overloads + `DrawModel`
> (`int entityID=-1` → `u_EntityID`); all four Renderer3D shaders (incl. Line3D writing −1) emit
> `layout(location=1) out int o_EntityID`. Engine3DDemo "Picking (S4.6)" panel (own MRT FBO pre-pass,
> hover ReadPixel → part name). Build + tests green; user visual pass pending.

Unlocks S5.4 picking and all S6 post-processing.

- **Files:** MODIFY `graphics/FrameBuffer.h`, `platform/OpenGL/OpenGLFrameBuffer.h/.cpp`,
  `renderer/Renderer3D.h/.cpp`, `scene/Scene.cpp`, `assets/shaders/Mesh3D.glsl` +
  `MeshLit.glsl` + every other shader `Renderer3D::Init` loads (open `Renderer3D.cpp` and list
  the `Shader::Create` calls — the line/grid shader must also gain the ID output, writing `-1`,
  or MRT-bound line draws leave the ID attachment undefined); Engine3DDemo picking panel.
- **Spec — the grown specification (back-compat is the whole game):**
  ```cpp
  enum class FramebufferTextureFormat { None = 0, RGBA8, RGBA16F, RED_INTEGER, DEPTH24STENCIL8 };
  struct FramebufferTextureSpecification { FramebufferTextureFormat TextureFormat =
                                           FramebufferTextureFormat::None; /* implicit ctor */ };
  struct FramebufferAttachmentSpecification
  { std::vector<FramebufferTextureSpecification> Attachments; /* init-list ctor */ };

  struct FramebufferSpecification
  {
      uint32_t Width = 0, Height = 0;
      uint32_t Samples = 1;            // still reserved
      bool SwapChainTarget = false;    // still reserved
      FramebufferAttachmentSpecification Attachments; // EMPTY ⇒ default {RGBA8, DEPTH24STENCIL8}
  };
  ```
  The empty-means-default rule keeps every existing `FrameBuffer::Create` call site (the
  Application workspace FBO, ViperSim's FPV inset) byte-for-byte identical in behavior.
- **Spec — API additions:**
  ```cpp
  virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0; // default arg keeps ImGui::Image callers compiling
  virtual int      ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;      // RED_INTEGER attachments; FBO must be bound
  virtual void     ClearAttachment(uint32_t attachmentIndex, int value) = 0;   // glClearBufferiv; FBO must be bound
  ```
- **OpenGL implementation notes (each one is a known faceplant):**
  - Format mapping: RGBA8 → (`GL_RGBA8`, `GL_RGBA`, `GL_UNSIGNED_BYTE`); RGBA16F →
    (`GL_RGBA16F`, `GL_RGBA`, `GL_FLOAT`); RED_INTEGER → (`GL_R32I`, `GL_RED_INTEGER`, `GL_INT`).
  - Integer textures **must use `GL_NEAREST`** filtering or the FBO is incomplete.
  - With > 1 color attachment, call `glDrawBuffers(n, {GL_COLOR_ATTACHMENT0..n-1})` after
    attaching; with 0 color attachments call `glDrawBuffer(GL_NONE)` (free prep for S6.4
    depth-only passes).
  - `glClear` does **not** reliably clear integer attachments — callers clear the ID attachment
    with `ClearAttachment(i, -1)` every frame after `Bind()`.
  - `ReadPixel`: `glReadBuffer(GL_COLOR_ATTACHMENT0 + i)` then
    `glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &px)`. GL's origin is bottom-left —
    the *caller* flips: `glY = height - 1 - mouseY`.
  - Rebuild `Invalidate()` around a vector of attachment IDs; `Resize` keeps flowing through it.
- **Spec — ID plumbing:** both `DrawMesh` overloads and `DrawModel` gain a trailing
  `int entityID = -1` parameter (defaulted → source-compatible) uploaded as `SetInt("u_EntityID", ...)`
  (silent-ignore covers old shaders). Mesh/lit shaders add
  `layout(location = 1) out int o_EntityID;` written from `uniform int u_EntityID;` — writing an
  output the bound FBO doesn't have is harmless. `Scene::OnRender3D` passes
  `(int)(uint32_t)entity`.
- **Acceptance:** Engine3DDemo "Picking (S4.6)" panel renders the scene into its **own** MRT FBO
  ({RGBA8, RED_INTEGER, DEPTH24STENCIL8}) as a pre-pass, then re-binds the app viewport FBO +
  `RenderCommand::SetViewport` — copy the proven inset pattern from ViperSim
  `FlightScreen.cpp` (S3.1). Panel shows attachment 0 via `ImGui::Image`; hovering shows the
  `ReadPixel` ID and the matching aircraft-part name (parts submitted with IDs 1..N); empty
  space reads −1. The app-wide workspace FBO stays on the default spec (whether it grows an ID
  attachment is S5.4's call). 2D overlay + `CosmicTests` green.

### S4.7 Compute + storage buffers *(requires S4.0)*

> **✅ code-complete 2026-07-02.** `#type compute` → GL_COMPUTE_SHADER in the shader parser (compute-only
> program links + binds); `StorageBuffer` SSBO resource; RendererAPI/RenderCommand verbs `DispatchCompute`,
> `MemoryBarrier(GpuBarrier)`, `DrawArrays(PrimitiveTopology,…)` + lazy empty VAO + `GL_PROGRAM_POINT_SIZE`;
> `ComputeParticles.glsl` + `ParticlePoints.glsl`; Engine3DDemo "Compute (S4.7)" 1M-point toggle with
> fps readout. **Gotcha handled:** `<windows.h>` defines a `MemoryBarrier` macro — `#undef`'d in
> RendererAPI.h/RenderCommand.h. Build + tests green; **user perf pass pending** (1M points ≥ 60 fps).

The infrastructure S9 (FFT water) and S10 (GPU particles) build on. Will not compile before
S4.0 — `glDispatchCompute` isn't in the old loader.

- **Files:** NEW `graphics/StorageBuffer.h` + `platform/OpenGL/OpenGLStorageBuffer.h/.cpp`;
  MODIFY `renderer/RendererAPI.h`, `renderer/RenderCommand.h`,
  `platform/OpenGL/OpenGLRendererAPI.h/.cpp`, `platform/OpenGL/OpenGLShader.cpp`;
  NEW `Cosmic/assets/shaders/ComputeParticles.glsl` + `ParticlePoints.glsl`; Engine3DDemo demo.
- **Spec — compute shaders ride the existing `Shader` class:** extend the `#type` parser
  (`OpenGLShader.cpp`, `ShaderTypeFromString` + `PreProcess` — find by content) to accept
  `#type compute` → `GL_COMPUTE_SHADER`; a file containing only a compute stage links a compute
  program; `Bind()` + uniform setters already work on it. No separate ComputeShader class.
- **Spec — SSBO wrapper (factory pattern):**
  ```cpp
  class COSMIC_API StorageBuffer
  {
  public:
      virtual ~StorageBuffer() = default;
      virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
      virtual void Bind() = 0; // re-issues glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id)
      static Ref<StorageBuffer> Create(uint32_t size, uint32_t binding); // std430 binding index
  };
  ```
- **Spec — new RendererAPI/RenderCommand verbs (engine enums, GL translation in platform layer):**
  ```cpp
  enum class GpuBarrier : uint32_t { VertexAttribArray = 1u<<0, ShaderStorage = 1u<<1,
                                     ShaderImage = 1u<<2, All = 0xFFFFFFFFu };
  static void DispatchCompute(uint32_t x, uint32_t y, uint32_t z);
  static void MemoryBarrier(GpuBarrier bits);
  enum class PrimitiveTopology { Points, Lines, Triangles };
  static void DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count);
  ```
  `DrawArrays` exists for attribute-less draws: GL core requires *a* bound VAO, so
  `OpenGLRendererAPI` lazily creates one empty VAO and binds it inside `DrawArrays`. Also add
  `glEnable(GL_PROGRAM_POINT_SIZE)` in `OpenGLRendererAPI::Init` (one-time).
- **Demo recipe (Engine3DDemo "Compute (S4.7)" toggle, N = 1,000,000):**
  `ComputeParticles.glsl` = `#type compute`, `layout(local_size_x = 256)`,
  `layout(std430, binding = 0) buffer Particles { vec4 pos[]; }`, uniforms `u_Time`/`u_Count`;
  guard `gid < u_Count`; write a swirl/orbit position. `ParticlePoints.glsl` = vertex stage reads
  the same std430 block by `gl_VertexID` (no vertex attributes), sets `gl_PointSize = 2.0`;
  fragment outputs `u_Color`. Per frame: compute shader `Bind` + uniforms → `ssbo->Bind()` →
  `DispatchCompute((N + 255) / 256, 1, 1)` →
  `MemoryBarrier(VertexAttribArray | ShaderStorage)` → point shader `Bind` + `u_ViewProjection`
  → `DrawArrays(Points, 0, N)`.
- **Acceptance:** 1M animated points at ≥ 60 fps on the dev GPU (`ImGui::GetIO().Framerate`
  readout in the panel); toggle off restores the normal scene; 2D overlay + `CosmicTests` green.

---

## 4. S5 — CAD navigation & editor interaction *(SolidWorks feel; gizmos)*

1. **S5.1 Navigation presets on `OrbitCameraController`** **[filler — only needs S1, do any time]**
   `SetNavigationStyle(NavStyle::Classic | NavStyle::CAD)`:
   - **CAD style (SolidWorks bindings):** **MMB drag = orbit**, **Ctrl+MMB = pan**,
     **Shift+MMB = dolly**, **scroll = zoom toward cursor** (not toward center), LMB stays free
     for selection.
   - **Orbit about point under cursor:** on MMB-down, ray/depth-probe the scene
     (`FrameBuffer` depth or entity AABBs until S4.6 exists) and orbit about the hit point;
     fall back to the current target on miss. This single behavior is most of the "SolidWorks feel."
   - Optional inertial damping (exponential smoothing on yaw/pitch/pan velocities; off by default).
   - Existing apps keep `Classic` unless they opt in.
   *Acceptance:* Engine3DDemo toggles styles at runtime; zoom-to-cursor keeps the point under the
   cursor stationary; MMB-orbit pivots about the model surface point.
2. **S5.2 Frame & view shortcuts.** `F` frames selection (or whole scene bounds) with a smooth
   distance/target blend; `Home` = default iso view; numpad-style snap views
   (Front/Back/Top/Bottom/Left/Right/Iso) as an API (`SnapView(ViewPreset)`).
   *Acceptance:* framing a selected entity fills ~70% of the viewport height at any aspect.
3. **S5.3 ViewCube / axis triad overlay.** Corner widget showing orientation; clicking a
   face/edge/corner calls `SnapView` with an animated transition. Implementation: small
   `Renderer3D` pass with its own tiny viewport + ray-vs-box picking (no ImGui hacks).
   *Acceptance:* cube tracks the camera; clicking "Front" animates to the front view.
4. **S5.4 3D picking & selection outline.** Entity-ID MRT attachment (S4.6) + `ReadPixel` →
   `EntitySelection` (reuses the existing 2D selection bus); hover highlight + selected outline
   (ID-buffer edge detect in a small post pass — no stencil complexity).
   *Acceptance:* click selects the exact mesh under the cursor incl. partial occlusion;
   outline renders behind UI.
5. **S5.5 Transform gizmos.** Vendor **ImGuizmo** (single file, MIT, ImGui-native — matches our
   stack; hand-rolling parity is weeks of math for no gain). Wrap it:
   `Gizmo::Manipulate(camera, transformComponent, Mode::Translate|Rotate|Scale, Space::Local|World,
   snap)`. Keyboard: `W/E/R` mode cycle (only when viewport hovered & no ImGui text focus).
   *Acceptance:* move/rotate/scale a `MeshRendererComponent` entity with snapping; undo hook
   deferred to S14 editor work (documented).

---

## 5. S6 — Visual realism core

Ordered; this stage is the prerequisite for anything called "realistic."

1. **S6.1 HDR pipeline.** Scene renders to RGBA16F; final **tonemap pass** (ACES + exposure
   uniform) to the target; UI composites after. Post-pass framework: `PostProcessStack` running
   fullscreen-triangle passes with ping-pong buffers.
   *Acceptance:* overbright (>1.0) values roll off instead of clipping; exposure slider works.
2. **S6.2 PBR metallic-roughness.** `PBRMaterial` params/textures: albedo, metallic, roughness,
   normal (needs tangents — extend `MeshVertex` additively per contract rule 3), AO, emissive.
   Cook-Torrance GGX + Schlick Fresnel, matching the glTF 2.0 material model so S4.4 imports map 1:1.
   *Acceptance:* glTF DamagedHelmet-class sample renders comparably to a reference viewer.
3. **S6.3 IBL + skybox.** HDRI equirect → cubemap; irradiance convolution + prefiltered specular
   mip chain + BRDF LUT (offline-at-load via S4.7 compute); skybox pass.
   *Acceptance:* metallic sphere grid (roughness 0→1) shows correct reflections under an HDRI.
4. **S6.4 Shadow mapping.** Directional sun: single 2k map + PCF first, then **3-split CSM** with
   stable texel snapping. `castShadows` on `MeshRendererComponent`; depth-only render path in
   `Renderer3D`.
   *Acceptance:* Engine3DDemo aircraft shadows the grid; no shimmer during orbit; slope bias documented.
5. **S6.5 SSAO.** Half-res hemisphere SSAO + blur, from the depth (+ normal reconstruct or a
   normals attachment via S4.6).
   *Acceptance:* contact darkening in crevices; toggleable; < 1.5 ms at 1080p on the dev GPU.
6. **S6.6 Bloom.** Threshold + downsample chain + upsample (CoD-style) on the HDR buffer.
   Emissive materials (S6.2) glow — **this is the lava enabler.**
   *Acceptance:* emissive mesh blooms; no flicker while orbiting.
7. **S6.7 Anti-aliasing.** FXAA post pass first (cheap, fits the stack); MSAA resolve path for the
   3D pass as an option (closes the old "reserved spec fields" question); TAA parked until motion
   vectors exist.
   *Acceptance:* grid/edge crawl visibly reduced; screenshot comparison committed.

---

## 6. S7 — Sky, atmosphere & time-of-day

1. **S7.1 Procedural sky v1.** Sun-disk + Preetham/Hosek-style analytic sky driven by a sun
   direction; replaces S3.3's gradient. Sun direction also drives the directional light + IBL
   ambient approximation (re-capture irradiance on big sun moves, amortized).
2. **S7.2 Height fog + aerial perspective.** Exponential height fog with inscatter color from the
   sky model; applied in the tonemap/post chain (depth-based).
3. **S7.3 Time-of-day.** `Environment` scene object: sun elevation/azimuth from time; app-drivable
   (sim time ↔ visual time). Night = stars/moon texture (cheap).
4. **S7.4 (later) Volumetric clouds** — park until a consumer exists; note: raymarched noise
   clouds are S10-adjacent and expensive; revisit after S12 profiling exists.

*Acceptance (stage):* Engine3DDemo under a morning→noon→sunset scrub looks continuously plausible;
fog hides the grid horizon.

---

## 7. S8 — Terrain system *(engine component; the volcano's foundation)*

1. **S8.1 `TerrainComponent` + renderer.** Heightmap-based: source = image (R16) or procedural
   (engine `math/Noise.h` — see doc 03 E14). Chunked quadtree with distance-based LOD and skirt
   stitching (chosen over geo-clipmaps for implementation simplicity; revisit at S12 scale-up).
   CPU-generated normals at load; world size/height scale in the component.
2. **S8.2 Terrain materials.** Splat-map blended PBR layers (4 first, 8 later): grass/rock/
   snow/ash…; **triplanar** projection on steep slopes; per-layer tiling; height-based +
   slope-based auto-splat option (rock above slope threshold, snow above altitude — parameterized,
   *not* hardcoded to any scenario).
3. **S8.3 Terrain queries.** `Terrain::SampleHeight(x, z)` and `SampleNormal(x, z)` on the CPU —
   the generic verb sims use for ground contact (ViperSim landing legs) and demos use for placing
   objects/particles.
4. **S8.4 (later) GPU tessellation displacement** for near-field detail; **holes** (caves/craters
   — the caldera) via per-chunk mask. Editor sculpting brushes belong to S14's editor app.

*Acceptance (stage):* 4×4 km, 1 m-resolution terrain at 60 fps with LOD transitions free of pops
at walking distance; splat layers blend by height/slope; `SampleHeight` matches rendered surface
within 1 cm in tests.

---

## 8. S9 — Water system

Two tiers; Tier 1 is most of the visual payoff for lakes/rivers.

1. **S9.1 `WaterComponent` Tier 1 (lake/river).** Flat plane (or terrain-carved region):
   dual scrolling normal maps + 2–4 **Gerstner waves** for swell; **depth-fade** absorption color
   (soft shorelines, using scene depth); refraction via scene-color grab pass with distorted UVs;
   **planar reflection** (render-to-texture with clip plane) *or* SSR — planar first (simpler,
   robust for one water plane); Fresnel blend; shoreline **foam** from depth delta + noise.
2. **S9.2 Buoyancy/height query.** `Water::SampleHeight(x, z, t)` evaluating the same Gerstner set
   on CPU — generic verb for floating objects (and a Viper water-landing someday).
3. **S9.3 Tier 2 ocean (FFT).** Tessendorf FFT spectrum (JONSWAP) on compute (S4.7):
   displacement + choppiness, whitecaps from the Jacobian, cascaded (2–3) spectra for detail;
   projected-grid or clipmesh LOD. This is the "realistic ocean" checkbox — schedule only after
   Tier 1 ships and S12 profiling exists.
4. **S9.4 Underwater** (fog volume + tinted post + caustics texture) — optional, demo-driven.

*Acceptance (Tier 1):* lake demo — shoreline foam, soft depth edges, sun + sky reflections, boat
(box) bobbing via `SampleHeight`, 2D overlay intact. *(Tier 2):* open-ocean demo at 60 fps.

---

## 9. S10 — GPU particles & volumetrics

1. **S10.1 GPU particle system.** SSBO pool + compute update (S4.7) + indirect draw; emitter
   component: spawn shape (point/sphere/cone/box), rate/burst, over-lifetime curves (size, color,
   velocity), gravity/drag/wind, world/local space; soft-particle depth fade; texture **flipbook**
   animation with frame blending; CPU fallback path for tiny emitters. Sorting: per-emitter
   back-to-front within the transparent queue (S12 ties in).
2. **S10.2 Ribbons/trails.** Camera-facing ribbon emitter (rocket exhaust, tire tracks in snow,
   wingtip vortices for the sim).
3. **S10.3 Froxel volumetric fog + light shafts.** Clustered froxel grid, sun shadow-map sampling
   → god rays; density from height fog + local fog volumes (box/sphere components).
4. **S10.4 Raymarched local volumes.** 3D-noise raymarch inside a bounded volume for **smoke
   plumes** (the volcano column): flipbook-billboard hybrid first, true raymarch second; lit by
   sun + N strongest point lights.
5. **S10.5 Heat-haze distortion.** Screen-space UV distortion post pass masked by "distortion"
   particles/volumes (refraction vector in a small RT).

*Acceptance (stage):* 200k live GPU particles at 60 fps; a lit smoke plume casts plausible
self-shading; heat shimmer above an emissive surface.

---

## 10. S11 — Weather/nature systems + flagship demo apps

Engine grows **generic** systems; each flagship scenario is a **`Projects/` demo app** and the
stage's acceptance test. (Rule 8: the engine never gains a `Volcano` class.)

1. **S11.1 Snow system (engine).**
   - **Snow overlay material feature:** world-up-facing snow blend on any PBR material
     (mask = N·up smoothstep + altitude band), sparkle micro-glint (jittered specular), usable on
     terrain layers (S8.2) and meshes.
   - **Accumulation mask v1:** top-down orthographic depth capture → coverage buffer that fades in
     over exposed surfaces (drivable rate), sampled by the overlay feature.
   - **Deformation trails v1:** RTT height-stamp buffer (objects stamp as they move) sampled as
     displacement/normal perturbation on snow surfaces — decal-quality first; tessellated
     displacement upgrade later.
   - Falling snow = S10 emitter preset (the engine ships the preset as an example asset).
2. **S11.2 Lava/emissive-flow material feature (engine).** Flow-map-scrolled emissive PBR layer
   (temperature gradient LUT → emissive intensity, crust/glow bands), animated by time — generic
   "glowing flowing surface" usable for lava, neon, tron-floors. Blooms via S6.6.
3. **S11.3 `Projects/VolcanoDemo`.** Terrain w/ caldera (S8 + hole mask), lava flows (S11.2 on
   terrain decal meshes), smoke column (S10.4), ash particles + embers (S10.1), heat haze (S10.5),
   point-light glow, night mode (S7), rumble (doc 08 A3 positional audio) — **the "realistic
   volcano" acceptance scene.**
4. **S11.4 `Projects/WinterDemo`.** Snowy terrain + falling snow + accumulation on a cabin/trees
   (glTF), footprint/vehicle trails, grey sky preset — **the "realistic snow" acceptance scene.**
5. **S11.5 `Projects/OceanDemo`** (or fold into WinterDemo lake): S9 showcase + buoyant objects —
   **the "realistic water" acceptance scene.**

---

## 11. S12 — Performance & scale

1. **S12.1 Frustum culling** — AABB per `MeshRendererComponent` (mesh-local bounds × transform),
   camera frustum test before submit; stats counter proves cull rate.
2. **S12.2 Render queue + sort keys** — opaque front-to-back (state-change key: shader→material→
   mesh), transparent back-to-front; replaces immediate-mode submission inside `Renderer3D`.
3. **S12.3 Instanced mesh path** — `DrawMeshInstanced(mesh, material, span<mat4>)` + automatic
   instancing for identical mesh/material pairs in the queue (rocks, trees, debris).
4. **S12.4 LOD groups** — `LODGroupComponent` (N meshes + switch distances, optional cross-fade).
5. **S12.5 GPU profiler** — timer-query verb (`RenderCommand::BeginGpuZone/EndGpuZone`) + a
   profiler HUD panel (per-pass ms: shadow, opaque, water, particles, post) — **build this early
   in the stage; every later item quotes its numbers.**
6. **S12.6 Texture pipeline** — mip generation policy, BCn compression at import (stb_dxt or a
   `package.bat` bake step), sRGB correctness audit.

*Acceptance (stage):* VolcanoDemo + 10k instanced meshes ≥ 60 fps at 1080p on the dev GPU, with
the profiler HUD screenshot committed.

---

## 12. S13 — RHI hardening + Vulkan gate

1. **S13.1 Conformance audit** — grep-verified: zero `gl*`/`GL_*` outside `platform/OpenGL/`;
   missing verbs promoted to `RendererAPI`. CI check added.
2. **S13.2 Frame-lifecycle doc** — one internals doc: resource creation/destruction rules, pass
   ordering, who owns which FBO — the spec a second backend would implement.
3. **S13.3 Decision gate** — evaluate against §0's reopen conditions with S12 profiler data.
   Outcomes: stay GL (default) / native Vulkan backend / adopt an RHI. A Vulkan backend, if
   chosen, lands per-subsystem (clear → 2D → 3D → compute) behind `RendererAPI::SetAPI`.

---

## 13. S14 — Game-engine tier backlog *(parking lot; each item lists its unlock)*

| Item | What | Unlocks when |
| --- | --- | --- |
| Skeletal animation | glTF skins/clips, GPU skinning, blend/state machine | a project needs characters |
| Physics middleware gate | vendored **Jolt** for contact-rich gameplay (the sim's 6DOF stays app-side per doc 03) | a game project needs stacks/ragdolls/queries |
| Scene serialization | save/load scenes (old E8) — prefab-ish text format | editor app or a content-heavy game |
| Editor app (`CosmicEd`) | scene view (S5 nav/gizmos/picking), hierarchy + inspector via `ComponentRegistry` reflection, play/pause using the responsive-rendering+pause design (docs/design/) | S5 done + a real content workflow need |
| Undo/redo command stack | editor-grade command pattern | CosmicEd |
| Scripting | staying C++-DLL-first is the story; optional Lua/AngelScript later | external users demand it |
| Positional audio | doc 08 A3 | any S11 demo wanting ambience |
| Decals | projected PBR decals | VolcanoDemo polish / games |

---

## 14. Order, dependencies, and how this interleaves with the sim

```
S3   ──────────────► driven by ViperSim P4–P5 (any time after S2)
S4.0 ✅ 2026-07-02 [GLAD 4.5 — was hard-gating S4.7]
S4.1→S4.7 (ordered; S4.4 = a then b) ─► S5.4/S5.5 (need S4.6)   S6 (needs S4.2/S4.5/S4.6/S4.7)
S5.1–S5.3 [filler] ──► any time after S1
S6 ─► S7 ─► S8 ─► S9 ─► S10 ─► S11 ─► S12 ─► S13
                 (S8/S9/S10 internally reorderable; S11 needs all three)
```

- **2D pipeline, shipped apps, and the docked workspace stay untouched at every stage** (contract 6).
- Every item lands as its own PR with its acceptance line demonstrated (screenshot or demo app
  committed under `Projects/`).
- Where a technique has competing implementations, this doc names the chosen one and the trade-off;
  implementers should not silently substitute alternatives.
