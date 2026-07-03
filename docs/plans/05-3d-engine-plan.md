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
| S5 | CAD navigation, ViewCube, gizmos, 3D picking | **S5.1–S5.5 ✅ code-complete 2026-07-02** (roadmap Phase 8; build + `CosmicTests` 73/73 green; user visual pass pending) |
| S6 | Visual realism core: HDR, PBR+IBL, shadows, SSAO, bloom, AA | **S6.1–S6.7 ✅ code-complete 2026-07-03** (full build + `CosmicTests` 73/73 green; app boots into Engine3DDemo, OpenGL 4.5, all 13 new shaders compile + all post/IBL/shadow FBOs complete with zero error logs; **user visual pass pending**) |
| S7 | Sky, atmosphere, fog, time-of-day | **S7.1–S7.3 ✅ code-complete 2026-07-03** (analytic sky = the S6.3 procedural environment source; height fog in the tonemap; time-of-day sun scrub drives sky+light+shadows). S7.4 volumetric clouds parked |
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

> **S4 post-review hardening (2026-07-02, same branch).** A standards/Vulkan-portability review
> of the finished S4 code passed (factory pattern, engine enums, GL confined to the platform
> layer, std140 discipline all verified) and landed these behavior-neutral fixes:
> - **`RenderCommand::MemoryBarrier` → `GpuMemoryBarrier`** — winnt.h macro-defines the old name;
>   the header `#undef`s are gone (they were order-fragile and clobbered a macro apps may use).
> - **`RenderCommand::SetCullMode(CullMode::None|Back|Front)`** — the §0-rule-1 verb, no caller
>   yet; default stays None (2D sprites flip winding). S6.4/S12 consume it.
> - **`renderer/BindingPoints.h`** — UBO/SSBO binding-index registry (`Bindings::LightsUbo = 0`,
>   `CameraUbo = 1` reserved for S6.1, `AppSsbo0 = 0`); claim slots there first.
> - **`Texture::SetSampling(TextureFilter, TextureWrap)`** — new verb; `Font.cpp` now uses it,
>   deleting the last raw `gl*` outside `platform/OpenGL/` (S13.1's audit target, closed early).
> - **`UniformBuffer::Bind()`** (re-asserts its base binding; `SetLights` calls it before upload),
>   `Renderer3D::kMaxPointLights` + a once-per-run truncation warning in `SetLights`, glTF import
>   fixes (mirrored-transform winding flip; default-scene traversal instead of all nodes), and
>   depth attachments get NEAREST/clamp params so sampling them works.
> - Deliberately **deferred**: per-frame camera UBO (→ S6.1), async `ReadPixel` (→ S5.4),
>   command buffers/pipeline objects/render passes/shader reflection (→ S13 gate, unchanged).

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
> `GpuMemoryBarrier(GpuBarrier)`, `DrawArrays(PrimitiveTopology,…)` + lazy empty VAO + `GL_PROGRAM_POINT_SIZE`;
> `ComputeParticles.glsl` + `ParticlePoints.glsl`; Engine3DDemo "Compute (S4.7)" 1M-point toggle with
> fps readout. **Gotcha:** `<windows.h>` defines a `MemoryBarrier` macro — originally `#undef`'d, then
> the verb was renamed `GpuMemoryBarrier` in the post-review hardening pass (the `#undef`s are gone).
> Build + tests green; **user perf pass pending** (1M points ≥ 60 fps).

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
  static void GpuMemoryBarrier(GpuBarrier bits); // renamed from MemoryBarrier (winnt.h macro)
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
  `GpuMemoryBarrier(VertexAttribArray | ShaderStorage)` → point shader `Bind` + `u_ViewProjection`
  → `DrawArrays(Points, 0, N)`.
- **Acceptance:** 1M animated points at ≥ 60 fps on the dev GPU (`ImGui::GetIO().Framerate`
  readout in the panel); toggle off restores the normal scene; 2D overlay + `CosmicTests` green.

---

## 4. S5 — CAD navigation & editor interaction *(SolidWorks feel; gizmos)*

> **✅ S5.1–S5.5 code-complete 2026-07-02 (roadmap Phase 8, branch `phase-7-3d-foundations`).**
> Full VS-cmake build green (engine + ImGuizmo lib + every project DLL + tests); `CosmicTests`
> **73/73** (103,934 assertions) incl. the new `tests/test_s5_navigation.cpp` (SnapView poses,
> frame-to-fit distance, ViewCube face selection — all headless). New engine surface:
> `NavStyle`/`ViewPreset` + CAD bindings / zoom-to-cursor / orbit-about-cursor / snap+frame on
> `OrbitCameraController` (S5.1/S5.2), `camera/NavigationCube` (S5.3), `scene/ScenePicker` +
> `FrameBuffer::ReadDepth` + `Mesh` local AABB (S5.4), `graphics/Gizmo` over vendored ImGuizmo
> (S5.5). Every item wired into Engine3DDemo + hotkeys (F / Home / W-E-R).
>
> **Hardening pass 2026-07-02 (same day, post-commit `ec8f575`):**
> - **Gizmo interaction fix:** ImGuizmo hover-tests against the *host window of its draw list*;
>   drawing to the foreground list made the gizmo render but never activate (any hovered window
>   ⇒ `mbMouseOver == false`). New protocol: `ImGuiLayer::Begin` owns `ImGuizmo::BeginFrame()`
>   (Gizmo::BeginFrame removed), and `Gizmo::Manipulate` binds the CURRENT window's draw list —
>   call it inside `WorkspaceLayer::BeginViewportOverlay()/EndViewportOverlay()` (new engine API
>   that appends to the Viewport window). Ortho vs perspective now auto-detected from `proj[3][3]`.
> - **One coordinate space:** viewport math is ImGui SCREEN pixels end-to-end (multi-viewport is
>   on). New `Input::GetMouseScreenPosition()`; `OrbitCameraController` (drag/pivot/zoom-to-cursor)
>   and demo picking use it — window-client coords only matched when the app sat at the desktop
>   origin (borderless maximized), a latent multi-monitor/windowed bug.
> - **Polled-input gating:** the orbit controller polls the mouse, so panel drags also orbited the
>   camera. `WorkspaceLayer::IsViewportHovered()/IsViewportFocused()` + `Orbit::IsDragging()` let
>   the demo enable camera control only over the viewport (drags may finish outside), yield to
>   gizmo hover/drag + the ViewCube, and drop scroll-zoom when the cursor is on a panel.
> - **Demo UI split into dock-port panels** (port mode, one window per concern): left = "Camera &
>   Views" / "Editor Tools" / "Rendering & Lighting"; right = "Simulation & Timing" / "Feature
>   Demos"; the ViewCube + gizmo moved ONTO the viewport (top-right overlay) via
>   BeginViewportOverlay — the CAD convention. Panels carry usage instructions (nav bindings,
>   editor how-to, hotkeys). ImGuizmo now links PRIVATE (no include-dir leak to clients).
>
> **Remaining — user visual pass:** run Engine3DDemo, toggle CAD nav (MMB orbit about the
> cursor point, scroll-to-cursor), click the ViewCube faces (top-right of the viewport), enable
> editor mode → click-select an ECS mesh (outline shows), drag the gizmo (move/rotate/scale +
> snap), F/Home framing.
> **Deviation (documented, not silent):** the selection *outline* is an oriented mesh-AABB wire box
> drawn depth-test-off, not the ID-buffer edge-detect post pass named in S5.4 — the edge-detect
> variant needs a fullscreen pass that samples an FBO attachment as a texture, which has no clean
> engine verb until the S6.1 post-process stack. The wire outline is robust, self-contained, and
> S6.1 can upgrade it. Picking itself *is* ID-buffer based (`ScenePicker` + `ReadPixel`, S4.6 MRT).

1. **S5.1 Navigation presets on `OrbitCameraController`** ✅ **[filler — only needs S1, do any time]**
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
2. **S5.2 Frame & view shortcuts.** ✅ `F` frames selection (or whole scene bounds) with a smooth
   distance/target blend; `Home` = default iso view; numpad-style snap views
   (Front/Back/Top/Bottom/Left/Right/Iso) as an API (`SnapView(ViewPreset)`).
   *Acceptance:* framing a selected entity fills ~70% of the viewport height at any aspect.
3. **S5.3 ViewCube / axis triad overlay.** ✅ Corner widget showing orientation; clicking a
   face/edge/corner calls `SnapView` with an animated transition. Implementation: small
   `Renderer3D` pass with its own tiny viewport + ray-vs-box picking (no ImGui hacks).
   *Acceptance:* cube tracks the camera; clicking "Front" animates to the front view.
4. **S5.4 3D picking & selection outline.** ✅ *(outline = geometric wire box; see the deviation
   note above)* Entity-ID MRT attachment (S4.6) + `ReadPixel` →
   `EntitySelection` (reuses the existing 2D selection bus); hover highlight + selected outline
   (ID-buffer edge detect in a small post pass — no stencil complexity). Note: `ReadPixel` is a
   synchronous `glReadPixels` (pipeline stall) — fine for click-to-select; if hover picking runs
   every frame, move the readback to an async PBO round-robin as part of this item.
   *Acceptance:* click selects the exact mesh under the cursor incl. partial occlusion;
   outline renders behind UI.
5. **S5.5 Transform gizmos.** ✅ Vendor **ImGuizmo** (single file, MIT, ImGui-native — matches our
   stack; hand-rolling parity is weeks of math for no gain). Wrap it:
   `Gizmo::Manipulate(camera, transformComponent, Mode::Translate|Rotate|Scale, Space::Local|World,
   snap)`. Keyboard: `W/E/R` mode cycle (only when viewport hovered & no ImGui text focus).
   *Acceptance:* move/rotate/scale a `MeshRendererComponent` entity with snapping; undo hook
   deferred to S14 editor work (documented).

---

## 5. S6 — Visual realism core *(roadmap Phase 9 — explicit work orders)*

> **✅ S6.1–S6.7 + S7.1–S7.3 CODE-COMPLETE 2026-07-03 (roadmap Phase 9, branch `phase-7-3d-foundations`).**
> Full VS-cmake Debug build green (engine + all project DLLs + CosmicApp) and `CosmicTests` **73/73**
> (103,934 assertions). Smoke-run of `CosmicApp --project Engine3DDemo`: OpenGL 4.5 (RTX 5070 Ti),
> all 13 new shaders compiled with **zero** "Shader compilation failure"/"Framebuffer is incomplete"
> logs, the IBL bake + BRDF LUT ran, Duck.glb imported through the new PBR-material path — the whole
> run produced no warnings or errors. **New engine surface:**
> - **S6.2 textures** — `MeshVertex` grows a `a_Tangent` (location 3), every factory auto-generates a
>   TBN (`Mesh::ComputeTangents`); `PBR.glsl` gains albedo/normal/metal-rough/AO/emissive maps (each
>   `u_HasXMap`-gated) + tangent-space normal mapping; `Texture2D::Create(bytes,size)` decode-from-memory;
>   glTF import reads the full metallic-roughness material (factors + embedded/external textures) into
>   `ModelPart` and builds a per-part `Ref<Material>` PBR material; `Renderer3D::DrawModel` uses it.
> - **S6.3 IBL + skybox** — `graphics/TextureCube` (+ OpenGL impl, render-to-face bake FBO),
>   `renderer/EnvironmentMap` (procedural analytic sky → env cube → irradiance + prefilter + BRDF LUT,
>   all sun-driven), `Skybox.glsl` background pass, `PBR.glsl` IBL ambient term; `RenderCommand::BindTextureCubeSlot`;
>   `Renderer3D::SetIBL/ClearIBL` binds the IBL set on every material draw (reserved units 8–10).
> - **S6.4 shadows** — depth-only `FrameBuffer`, `renderer/ShadowMap` (fitted ortho light matrix +
>   depth pass + front-cull), `ShadowDepth.glsl`, 3×3 PCF in `PBR`/`MeshLit`/`Mesh3D` (the flat Lambert
>   path receives too); `Renderer3D::SetShadow/ClearShadow` (reserved unit 11).
> - **S6.5/6.6/6.7 + S7.2** — `PostProcessStack` grew SSAO (reconstruct-from-depth, half-res + blur),
>   bloom (soft-knee threshold + separable Gaussian ping-pong), FXAA (LDR final pass via an intermediate),
>   and height fog folded into the tonemap; `RenderCommand::GetBoundFramebuffer/BindFramebufferHandle`.
> - **S7.1/S7.3** — the S6.3 procedural sky IS the analytic sky (sun-driven env source); a time-of-day
>   clock in the demo scrubs the sun and rebakes the sky + drives the directional light, IBL and shadows.
>
> Every item has an Engine3DDemo toggle (in the "Rendering & Lighting" panel). **Documented deviations:**
> (a) S6.4 ships the single 2k map + PCF tier; 3-split CSM + texel-snapping is the next step (the
> `ShadowMap` API is CSM-ready). (b) SSAO is composited over the whole image in the tonemap rather than
> modulating only the ambient term (the "ambient-only" ideal needs a depth prepass / forward AO fetch).
> (c) Bloom uses a threshold + separable-Gaussian chain instead of the CoD progressive mip up/downsample
> (quality follow-up). (d) The skybox is drawn background-first with depth test off (a depth-func LEQUAL
> verb would let it draw after opaque — a small efficiency follow-up). (e) S6.2's full "matches a
> reference viewer" line still wants a committed **DamagedHelmet**-class glb — Duck exercises the textured
> PBR import path but has only a base-color map (no normal/MR), so normal mapping wants a normal-mapped
> asset for the visual pass. **Remaining: user visual pass** of every toggle + a committed screenshot.

Ordered; this stage is the prerequisite for anything called "realistic." **S6.1 is the load-bearing
foundation — do it (and build) before layering S6.2+; every later item is a fullscreen pass or a
shader that assumes the HDR float target and the post stack exist.**

> **2026-07-02 — foundation-first pass + work-order treatment.** Roadmap Phase 9 was scoped
> "foundation first": **S6.1 shipped this session** and §5 was expanded from one-line summaries into
> the explicit work orders below (files, signatures, GL gotchas, acceptance) — the same treatment
> §3 (S4) got, so each remaining item is one lower-tier session. Later stages (S7+) keep their
> original altitude until their turn.
>
> **Camera UBO deferred S6.1 → S6.2 (decision, not a slip).** The old S6.1 text folded the per-frame
> camera/engine-globals UBO (`Bindings::CameraUbo = 1`) into S6.1 "because this stage rewrites every
> shader anyway." A foundation-only pass does **not** rewrite the lit shaders (no PBR yet), so that
> "free" premise doesn't hold — the migration now rides **S6.2**, where `PBR.glsl` and the migrated
> `Mesh3D`/`MeshLit` shaders are rewritten and the injection-contract change is genuinely free.
> `Bindings::CameraUbo = 1` stays reserved; `Renderer3D` keeps setting the loose
> `u_ViewProjection`/`u_CameraPos` until then.

### S6 execution notes *(read once — they apply to every item)*

- **The post stack is the home for all of S6/S7's fullscreen work.** `renderer/PostProcessStack`
  (S6.1) owns the HDR scene target and runs fullscreen-triangle passes; bloom (S6.6), SSAO (S6.5),
  FXAA (S6.7) and fog (S7.2) are passes added to it, not new plumbing. Copy `Composite`'s shape:
  bind a shader, `RenderCommand::BindTextureSlot(slot, fbo->GetColorAttachmentRendererID(i))`,
  set uniforms, `PostProcessStack::DrawFullscreenTriangle()`.
- **Sampling an FBO attachment** (depth for SSAO/fog, color for bloom) uses the S6.1
  `RenderCommand::BindTextureSlot(slot, rendererID)` verb + a matching `SetInt("u_Sampler", slot)` —
  never a raw `gl*` bind (§0 rule 1). Depth attachments already carry NEAREST/clamp params (S4
  hardening), so they sample cleanly.
- **Shader-preprocessor contract (bit S6.1 — don't repeat it):** `OpenGLShader::PreProcess`
  pattern-matches fragment sources and *injects* `layout(location = 0) out vec4 color;` (plus
  `in vec2 v_TexCoord;` / `in vec4 v_Color;`) when the literal strings are absent. Name every post
  shader's fragment output **`color`** and its varying **`v_TexCoord`** (the engine-wide
  convention every shipped shader uses) or the injected duplicate location-0 output fails the
  compile — the program silently binds 0 and the pass draws nothing. The preprocessor now also
  skips the injection when any explicit `layout(location = 0) out` exists (hardening added
  2026-07-02), but follow the naming convention anyway.
- **New GPU resource types** (a `TextureCube` for S6.3, a shadow-map `FrameBuffer` depth spec for
  S6.4) follow the S4 factory pattern: abstract `graphics/X` + concrete `platform/OpenGL/OpenGLX` +
  static `Create` switching on `RendererAPI::GetAPI()`; **no GL tokens outside `platform/OpenGL/`.**
- **Build/run/test** identical to S4: features are accepted **visually in Engine3DDemo** (headless
  tests can't make a GL context), each behind a toggle in a dock-port panel; end every item with a
  build + `CosmicTests` green + the 2D overlay still correct (contract 6). `build_all` only when an
  ABI-sensitive header changes (e.g. a new `MeshRendererComponent`/`TransformComponent` field, or a
  new vertex layout that touches `Components.h`).

1. **S6.1 HDR pipeline.** ✅ **code-complete 2026-07-02.**
   - **Shipped:** `renderer/PostProcessStack.h/.cpp` — owns the HDR scene target
     (`{RGBA16F, DEPTH24STENCIL8}`) and the tonemap shader; `Init/Shutdown/SetViewportSize`,
     `BeginHDR(clearColor)` (bind + clear the float target), `GetSceneTarget()`,
     `Composite(exposure)` (fullscreen ACES resolve into the currently-bound LDR target, depth
     test/write off during and restored to ON/ON after), static `DrawFullscreenTriangle()`
     (attribute-less `DrawArrays(Triangles,0,3)` over the S4.7 empty VAO). New generic verb
     `RenderCommand::BindTextureSlot(slot, rendererID)` (+ `RendererAPI`/`OpenGLRendererAPI`) so a
     post pass can sample an FBO attachment that isn't a `Ref<Texture2D>` — closes the last missing
     §0-rule-1 primitive for post-processing. New `assets/shaders/Tonemap.glsl`: fullscreen triangle
     from `gl_VertexID` (no VBO), fragment = `exposure` scale → Narkowicz ACES → linear→sRGB gamma.
     Added to `Cosmic.h`.
   - **Engine3DDemo wiring:** the whole 3D world (main pass + ECS `OnRender3D` + selection outline +
     1M-point compute) renders into the HDR target between `BeginHDR` and the resolve; `Composite`
     tonemaps into the viewport FBO, then the 2D overlay composites in LDR (contract 7). "HDR +
     tonemap" toggle (default on) + logarithmic exposure slider in the "Rendering & Lighting" panel.
   - **Verified:** full VS-cmake Debug build green (engine + all project DLLs + CosmicApp);
     `CosmicTests` **73/73** (103,934 assertions — GPU-side, no new unit tests per the S6 notes).
     **User visual pass pending:** toggle HDR off/on (scene looks brighter/filmic on — the documented
     gamma difference), crank exposure past ~2× and confirm highlights roll off on the ACES shoulder
     instead of clipping to flat white; 2D overlay intact.
   - **Documented deviations / deferrals:** (a) camera UBO → S6.2 (see the note above); (b) ping-pong
     HDR buffers are **not** allocated yet — S6.1 has a single resolve pass, so the second buffer
     lands with S6.6 bloom (its first consumer); (c) authored colors are still fed to shaders without
     an sRGB→linear decode, so the final tonemap gamma makes HDR-on brighter than HDR-off — a full
     sRGB-correctness audit is **S12.6** (noted in `Tonemap.glsl`); (d) `PostProcessStack` is
     demo-owned (like the FPV inset / pick FBO) — promotion to an engine-global `SceneRenderer` that
     every 3D app gets for free is an S12-adjacent follow-up.
   - *Acceptance (met, pending user visual pass):* overbright (>1.0) values roll off instead of
     clipping; exposure slider works.

2. **S6.2 PBR metallic-roughness + camera UBO.** The first "real" material model, and the stage that
   pays off the camera-UBO migration deferred from S6.1.
   > **✅ code-complete 2026-07-03 (core + texture follow-up).** The texture/normal-map/glTF-import
   > follow-up flagged below shipped the same day — see the §5 banner. Original core-session notes:
   > - **Camera UBO (binding 1) — DONE, full migration.** `renderer/CameraUniforms.h`
   >   (`GpuCameraBlock`, std140, **80 bytes**: `mat4 ViewProjection; vec4 CameraPosition;` —
   >   time/viewport deferred to their first consumer rather than shipping dead fields, so the size is
   >   80 not the 96 sketched below). `Renderer3D` owns a UBO at `Bindings::CameraUbo`, packs+uploads
   >   in `BeginScene`, and **all six loose setters are gone** (grep-verified: no
   >   `SetMat4("u_ViewProjection")` / `SetFloat3("u_CameraPos")` in `Renderer3D` or the demo compute
   >   path). All five engine 3D shaders (`Line3D`/`Mesh3D`/`MeshLit`/`DemoChecker3D`/`ParticlePoints`)
   >   read an **instance-named** block `layout(std140, binding=1) uniform CameraBlock { mat4
   >   ViewProjection; vec4 CameraPosition; } u_Camera;` accessed as `u_Camera.ViewProjection`. The
   >   instance name is load-bearing: it keeps the literal `u_ViewProjection` out of the source so
   >   `OpenGLShader::PreProcess` does NOT inject a colliding loose `uniform mat4 u_ViewProjection;`
   >   (the same injector that broke the S6.1 tonemap). 2D shaders untouched (Renderer2D ortho, loose
   >   uniform). Verified: scene renders identically (grid/trail/pad/aircraft/navcube/compute all read
   >   the UBO).
   > - **PBR core — DONE (factors only).** `assets/shaders/PBR.glsl`: Cook-Torrance GGX + Smith +
   >   Schlick, energy-split by metallic, consuming the binding-0 lights block (sun + points) + the
   >   binding-1 camera block, ambient = the LightsBlock ambient knob × AO (flat stand-in until S6.3
   >   IBL — no magic constant). Material params `u_Albedo`/`u_Metallic`/`u_Roughness`/`u_AO`/
   >   `u_Emissive` via the plain `Material`+shader convention (no `PBRMaterial` class). Renders linear
   >   radiance into the S6.1 HDR target so metallic highlights roll off through the tonemap.
   >   Engine3DDemo "PBR sphere grid" toggle: a 5×5 grid (roughness across, metallic up) lit by the
   >   sun + two point lights, albedo colour picker. Verified visually (PrintWindow capture): smooth
   >   GGX shading + Fresnel edge + specular, no artifacts; metallic gradient is intentionally subtle
   >   under direct-only lighting (metals need IBL — S6.3).
   > - **DEFERRED to the S6.2 texture follow-up (own session):** the **tangent** vertex attribute +
   >   albedo/normal/metal-rough/AO/emissive **textures** (`u_*Map` + `u_HasXMap` gates) + glTF factor/
   >   texture import into `ModelPart` + a committed **DamagedHelmet**-class sample. The full-acceptance
   >   "matches a reference viewer" line needs those (DamagedHelmet is defined by its normal/AO maps);
   >   the factor-only core is a clean, verified milestone toward it. The spec below still describes the
   >   full item — items marked DONE above are done.
   - **Files:** extend the `Mesh` vertex layout with **tangents** (additive, contract rule 3 — a new
     `MeshVertexTangent` layout / attribute `a_Tangent` at `location = 3`; `CreateFromOBJ`/glTF
     import compute or pass them, primitives generate them); NEW `assets/shaders/PBR.glsl`;
     NEW `renderer/CameraUniforms.h` (the std140 `CameraBlock` C++ mirror) + upload path in
     `Renderer3D::BeginScene`; MODIFY `renderer/BindingPoints.h` comment (CameraUbo now live),
     `assets/shaders/Mesh3D.glsl` + `MeshLit.glsl` + `Line3D.glsl` + `DemoChecker3D.glsl` +
     `ParticlePoints.glsl` (read `u_ViewProjection`/`u_CameraPos` from the block), Engine3DDemo.
   - **Camera UBO spec (binding 1, `Bindings::CameraUbo`):** vec4-only std140 like the lights block —
     `mat4 u_ViewProjection; vec4 u_CameraPos_Time (xyz pos, w time); vec4 u_ViewportSize_pad;`
     `static_assert(sizeof == 96)`. `Renderer3D::BeginScene` packs + uploads it once per pass and
     stops setting the loose camera uniforms (material path too). 2D shaders keep their loose
     `u_ViewProjection` (Renderer2D has its own ortho camera — do **not** put it on binding 1).
   - **PBR material:** keep the `Material` + shader convention (no new `PBRMaterial` class needed —
     `MeshLit` proved the pattern); documented parameter names `u_Albedo` (vec4), `u_Metallic`,
     `u_Roughness`, `u_AO` (floats), `u_Emissive` (vec3) + textures `u_AlbedoMap`, `u_NormalMap`,
     `u_MetalRoughMap` (glTF packs metallic=B, roughness=G), `u_AOMap`, `u_EmissiveMap`, each gated
     by a `u_HasXMap` float. Cook-Torrance: GGX NDF + Smith geometry + Schlick Fresnel, energy split
     by metallic, consuming the binding-0 lights block (sun + points) exactly as `MeshLit` does.
     Match the glTF 2.0 metallic-roughness model so S4.4b imports map 1:1 (extend `ModelPart`/glTF
     import to carry the metallic/roughness/emissive factors + texture refs).
   - **Gotchas:** the tangent layout change touches every `Mesh` factory — regenerate normals+tangents
     consistently; a mesh with no UVs has no well-defined tangent (fall back to an arbitrary basis).
     Ambient is a flat term until S6.3 IBL replaces it — don't hardcode a magic ambient into `PBR.glsl`.
   - *Acceptance:* a glTF DamagedHelmet-class sample (commit a small one, like the S4.4b Duck) renders
     comparably to a reference viewer under the sun + point lights; the camera UBO drives every 3D
     shader (grep-verify no `SetMat4("u_ViewProjection")` left in `Renderer3D`); 2D overlay + tests green.

3. **S6.3 IBL + skybox.** Image-based ambient — the big visual jump, and the biggest single item.
   - **Files:** NEW `graphics/TextureCube.h` + `platform/OpenGL/OpenGLTextureCube.h/.cpp` (factory
     pattern; `Create(size, format)` for render targets, `CreateFromEquirect(path)` helper);
     NEW `renderer/EnvironmentMap.h/.cpp` (owns the environment/irradiance/prefilter cubemaps + BRDF
     LUT and the bake passes); NEW shaders `EquirectToCube.glsl`, `IrradianceConvolve.glsl`,
     `PrefilterEnv.glsl`, `BrdfLut.glsl`, `Skybox.glsl`; MODIFY `PBR.glsl` (add the IBL ambient term),
     Engine3DDemo; commit one small `.hdr` equirect under `assets/textures/`.
   - **Bake recipe (offline-at-load, render-to-cubemap-face — the compute path from S4.7 is optional):**
     equirect → cube (6 faces, one FBO per face via `glFramebufferTexture2D` to
     `GL_TEXTURE_CUBE_MAP_POSITIVE_X + i`); irradiance convolution (32³, cosine-weighted hemisphere
     sum); prefiltered specular (128³ base + 5 roughness mips, GGX importance sample); BRDF LUT
     (512² RG16F, one fullscreen pass). This needs the FBO layer to attach a **cube face** and to
     attach a specific **mip level** — add a `FrameBuffer` verb or a small dedicated bake FBO rather
     than bending the workspace FBO.
   - **Skybox pass:** draw the environment cube behind the scene — a fullscreen-triangle pass that
     reconstructs a view ray per pixel and samples the cubemap, depth test `LEQUAL`, drawn after
     opaque with depth write off (cheaper than a real cube mesh; fits the post stack's fullscreen idiom).
   - **Gotchas:** cube face winding + the +Y/−Y flip are the classic faceplants — validate against a
     known-good reference before wiring IBL into `PBR.glsl`; seamless cubemap filtering
     (`GL_TEXTURE_CUBE_MAP_SEAMLESS`) must be enabled (one-time, in `OpenGLRendererAPI::Init`).
   - *Acceptance:* a metallic sphere grid (roughness 0→1, an Engine3DDemo toggle) shows plausible
     reflections + correct roughness blur under the HDRI; skybox renders behind; 2D overlay + tests green.

4. **S6.4 Shadow mapping.** Directional sun shadows.
   - **Files:** MODIFY `graphics/FrameBuffer.h` (allow a **depth-only** spec — a lone
     `DEPTH24STENCIL8`, or add a `DEPTH32F` format sampled as a shadow map; the 0-color path already
     calls `glDrawBuffer(GL_NONE)`); `renderer/Renderer3D` (a **depth-only render path** —
     `RenderDepthPass(lightViewProj)` iterating the same submissions with a trivial depth shader);
     `PBR.glsl`/`MeshLit.glsl` (sample the shadow map, PCF); `scene/Scene.cpp` (consume
     `MeshRendererComponent::CastShadows`, already stored since S4.3); Engine3DDemo. NEW
     `assets/shaders/ShadowDepth.glsl`.
   - **Spec:** single 2k map + 3×3 PCF first; then **3-split CSM** (partition the view frustum,
     one ortho light matrix per split, **stable texel snapping** — round the light-space origin to
     texel size to kill shimmer during orbit). Slope-scaled depth bias + normal-offset to fight acne;
     `SetCullMode(Front)` during the depth pass to reduce peter-panning (restore `None` after — the
     verb exists since S4).
   - **Gotchas:** the depth pass needs its own viewport = shadow-map size; restore the scene viewport
     after. Cull-mode + depth-bias must be restored (same contract as the depth verbs). CSM split
     selection in the fragment shader by view-space depth.
   - *Acceptance:* Engine3DDemo aircraft shadows the grid; no shimmer while orbiting (texel snapping
     on); slope-bias value documented in the shader; toggle for map count; 2D overlay + tests green.

5. **S6.5 SSAO.** Contact shadows from depth.
   - **Files:** the HDR scene target grows a **view-space normal** attachment (MRT: add `RGBA16F`
     normals at `location = 1` — note this collides with the S4.6 entity-ID output; the HDR scene
     target is a *different* FBO than the pick FBO, so keep IDs on the pick pass and normals on the
     scene pass, or reconstruct normals from depth to avoid the attachment entirely — reconstruct-
     from-depth is the lower-risk first cut); NEW `assets/shaders/Ssao.glsl` + `SsaoBlur.glsl`;
     MODIFY `PostProcessStack` (an SSAO pass + a half-res AO target + the hemisphere kernel/noise
     texture), `PBR.glsl` (multiply ambient/IBL by AO), Engine3DDemo.
   - **Spec:** half-res, 16–32 sample hemisphere kernel oriented by the (reconstructed or sampled)
     normal, 4×4 rotation-noise tile, range check + bias, then a 4×4 box blur. Composited by
     modulating the ambient/IBL term (not the direct light).
   - *Acceptance:* visible contact darkening in crevices (the ECS scene + aircraft); toggle;
     ≤ 1.5 ms at 1080p on the dev GPU (quote the S12.5 profiler once it exists; eyeball until then);
     2D overlay + tests green.

6. **S6.6 Bloom.** Threshold + blur chain on the HDR buffer — **the lava/emissive enabler.**
   - **Files:** MODIFY `PostProcessStack` (allocate the ping-pong / mip-chain HDR buffers deferred
     from S6.1; a bloom pass inserted **before** `Composite`); NEW `assets/shaders/BloomDown.glsl`,
     `BloomUp.glsl` (CoD-style progressive down/upsample), and fold the bloom add into `Tonemap.glsl`
     (or a pre-tonemap combine); Engine3DDemo.
   - **Spec:** threshold (soft knee) the HDR color, downsample chain (~6 mips), upsample with
     additive blend, scale by an intensity uniform, add to the scene before the ACES curve. Emissive
     PBR materials (S6.2 `u_Emissive`) now glow.
   - *Acceptance:* an emissive mesh (Engine3DDemo toggle) blooms; no flicker while orbiting (stable
     threshold, no per-frame RNG); exposure + bloom interact sanely; 2D overlay + tests green.

7. **S6.7 Anti-aliasing.** Edge cleanup — the final post pass.
   - **Files:** NEW `assets/shaders/Fxaa.glsl`; MODIFY `PostProcessStack` (an FXAA pass after tonemap,
     since FXAA wants LDR/gamma input); optionally wire the long-reserved `FramebufferSpecification::
     Samples` field for an **MSAA** 3D pass with a resolve blit (closes the "reserved spec fields"
     question). TAA stays parked (needs motion vectors — no consumer yet).
   - *Acceptance:* grid/edge crawl visibly reduced (FXAA on vs off); a committed before/after
     screenshot; MSAA path (if built) resolves without artifacts; 2D overlay + tests green.

---

## 6. S7 — Sky, atmosphere & time-of-day

Kept at summary altitude (give it the §5 work-order treatment when Phase 9's S6 items are done and
S7 is next). All four ride the S6.1 post stack + S6.3 environment plumbing.

1. **S7.1 Procedural sky v1.** Sun-disk + Preetham/Hosek-style analytic sky driven by a sun
   direction; replaces S3.3's gradient and the S6.3 static HDRI skybox (the analytic sky becomes the
   environment source). Sun direction also drives the directional light + IBL ambient approximation
   (re-capture irradiance on big sun moves, amortized — reuse the S6.3 bake passes).
2. **S7.2 Height fog + aerial perspective.** Exponential height fog with inscatter color from the
   sky model; a depth-based fullscreen pass in the S6.1 post chain (reads `GetSceneTarget()` depth).
3. **S7.3 Time-of-day.** `Environment` scene object: sun elevation/azimuth from time; app-drivable
   (sim time ↔ visual time — ViperSim could drive it). Night = stars/moon texture (cheap).
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
   missing verbs promoted to `RendererAPI`. CI check added. (Head start: the S4 hardening pass
   already closed the last engine-side leak — `Font.cpp` now uses `Texture::SetSampling` — and
   added the `SetCullMode` verb; remaining known exceptions are the windowing layer (GLFW) and
   ImGui's vendored GL backend, which a second backend swaps wholesale.)
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
