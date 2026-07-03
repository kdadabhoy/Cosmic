# Phase 11 Plan — Frontier (flagship 3D showcase, S11 + pulled-forward S12 items)

> **Created 2026-07-03.** Executes roadmap **Phase 11** (doc 05 §10 S11 + selected S12 items).
> **Scope decisions (user-approved 2026-07-03):**
> - **ONE app — `Projects/Frontier`** — replaces the three planned demo apps (S11.3 VolcanoDemo /
>   S11.4 WinterDemo / S11.5 OceanDemo). A **World framework** (root layer + homescreen tiles +
>   per-world dock layouts, SF_Telem pattern) hosts five worlds: the seamless **Frontier Island**
>   (volcano + snowy range + alpine lake + ocean coast + waterfall/river + wildlife) and four
>   focused variants (**Night Volcano**, **Blizzard Peak**, **Dawn Mirror Lake**, **Storm Ocean**).
>   *Documented deviation, recorded in doc 05 §10.*
> - **Engine `SceneRenderer`** — the "S12-adjacent follow-up" named in doc 05 §5 S6.1(d) is pulled
>   into this phase (F2): engine-owned frame orchestration so worlds declare content instead of
>   copy-pasting Engine3DDemo's ~450-line pass sequence. Engine3DDemo itself is **not migrated**.
> - **Water v2** (F6) — the Gerstner tier gets 8 waves, shore awareness, caustics, whitecaps,
>   sparkle, underwater medium. **S9.3 FFT stays parked** (see "After Phase 11").
> - Pulled-forward S12 items: **S12.5 GPU profiler** (F3) and an **S12.3-lite instanced path +
>   frustum culling** (F5). The rest of S12 stays in Phase 12.
> - **Snow deformation trails (S11.1) deferred** — nothing touches the ground in a fly-through
>   demo. *Documented deviation.*
>
> **Division of labor:** the shaders and the app skeleton were **written up-front by the planning
> session (2026-07-03)** and are committed with this doc; each numbered work order below is ONE
> implementation session (Opus-tier) that builds the C++ that feeds them. The kickoff prompt is at
> the bottom (§"Kickoff prompt").

---

## 0. Execution notes — READ ONCE, THEY APPLY TO EVERY ITEM

1. **Build/test (non-interactive):** never run `build_all.bat`/`build.bat` (they `pause`). Use the
   VS-bundled cmake recipe in `00-MASTER-ROADMAP.md` §"Working agreement". Outputs land in
   `build/Runtime/<Config>/`; run `CosmicApp.exe` and pick **Frontier** (or
   `CosmicApp --project Frontier`). Tests: `build/Runtime/<Config>/CosmicTests.exe`. Re-run the
   cmake **configure** after adding new engine source files (GLOB without CONFIGURE_DEPENDS);
   Frontier's own glob is CONFIGURE_DEPENDS so app files need no reconfigure.
2. **THE SHADERS ARE ALREADY WRITTEN.** Every shader an item names exists in
   `Cosmic/assets/shaders/` and is the **uniform-contract truth**. Read the shader FIRST;
   implement the C++ that sets exactly those uniforms. Do not rewrite/rename shader uniforms; if
   a uniform seems missing, re-read the shader. New in Phase 11: `SkyDetail.glsl`,
   `FlowEmissive.glsl`, `WaterFlow.glsl`, `PBRInstanced.glsl`, `ShadowDepthInstanced.glsl`,
   `TerrainDepth.glsl`, `SnowAccum.glsl`, `LensFlare.glsl`. Modified (all additions **gated by
   uniforms whose GL default 0 = OFF**): `Water.glsl`, `EnvSky.glsl`, `PBR.glsl`, `Terrain.glsl`,
   `Tonemap.glsl`, `ParticleBillboards.glsl`. Corollary: **if a new feature doesn't appear, you
   forgot to set its gate uniform** — that's the first thing to check.
3. **Engine rules (doc 05 §0/§1):** no `gl*`/`GL_*` outside `platform/OpenGL/` — new GPU state
   goes through `RendererAPI`/`RenderCommand` verbs; new GPU resources copy the factory pattern
   (`graphics/X` + `platform/OpenGL/OpenGLX` + static `Create`); GPU-owning classes with
   Init/Shutdown are **non-copyable**; claim every new UBO/SSBO slot and reserved texture unit in
   `renderer/BindingPoints.h` FIRST; the engine never gains scenario-shaped API (no
   volcano/island/blizzard names in `Cosmic/src`).
4. **Shader-preprocessor contract (bit S6.1 twice):** post-pass shaders name the fragment output
   `color` and the varying `v_TexCoord`; mesh shaders read the camera from the **instance-named**
   binding-1 `CameraBlock` (`u_Camera.ViewProjection`) — never a loose `u_ViewProjection` (the
   injector would collide). All Phase 11 shaders already comply.
5. **State-restore contracts:** depth test/write default ON/ON, cull None, blend Alpha — any pass
   that changes them restores them. Callees restore the FRAMEBUFFER they replaced
   (`GetBoundFramebuffer`/`BindFramebufferHandle` pattern); the CALLER re-asserts the viewport
   after any pass that changed it (Water.h / PostProcessStack.h document this).
6. **App-side VFS rule:** `project://` must be resolved in the CALLING DLL —
   `Cosmic::FileSystem::Resolve("project://...")` inside Frontier code (Sound.h header note). If
   Frontier ever needs an app-private shader (none planned), it lives in
   `Projects/Frontier/assets/shaders/` → synced to `assets/projects/Frontier/shaders/` → loaded
   via `Shader::Create(FileSystem::Resolve("project://shaders/X.glsl"))`.
7. **Tests:** doctest, listed explicitly in `tests/CMakeLists.txt`'s `add_executable`. Headless —
   no GL; test CPU math only. `doctest::Approx.epsilon` is **RELATIVE** — use absolute tolerances
   for world coordinates (Phase 10 lesson).
8. **Frontier skeleton contracts:** worlds implement `Frontier::World` (`Projects/Frontier/src/World.h`);
   per-frame services arrive via `WorldContext` — work orders APPEND POINTERS to `WorldContext`
   (fed from `FrontierApp::OnUpdate`) instead of introducing singletons. `TODO(F#)` markers in the
   skeleton show every wiring point. The root layer binds + clears the viewport FBO before
   `World::OnUpdate`; the world drives everything after that.
9. **Process:** one work order per session, on the phase branch; finish with the item's
   **Acceptance** procedure (build + `CosmicTests` + the visual check), then update the item's
   status banner here (✅ + date + one-line result). Never run git write commands — the user
   commits. Engine3DDemo must keep rendering identically after every item (it consumes the same
   engine; the gate-uniform discipline makes this automatic — smoke-run it when an item touches a
   shared shader's C++ feed).

---

## F1 — `camera/FlyCameraController` (the exploration camera)

**Status:** ✅ 2026-07-03 — `FlyCameraController` (RMB look + WASD/EQ/Space/LCtrl move,
LShift boost, scroll speed, exp. velocity smoothing, optional ground-probe clamp) shipped +
exported; movement math factored into headless static helpers with `tests/test_flycamera.cpp`
(9 cases). Frontier wired: `WorldContext::Camera`, nav-panel Fly/Orbit toggle (fly default),
`SetPose` from spawn info, `DrawPlaceholder` renders the active camera. Build + CosmicTests 97/97
green; Frontier + Engine3DDemo boot clean.

**Files:** NEW `Cosmic/src/camera/FlyCameraController.h/.cpp`; MODIFY `Cosmic/src/Cosmic.h`
(export next to OrbitCameraController), `Projects/Frontier/src/World.h` +
`Projects/Frontier/src/FrontierApp.h/.cpp` (wire it; `TODO(F1)` markers).

**Spec — mirror `OrbitCameraController`'s architecture** (polling `OnUpdate`, discrete `OnEvent`,
screen-pixel mouse space via `Input::GetMouseScreenPosition()`, render frame right-handed Y-up):

```cpp
class COSMIC_API FlyCameraController
{
public:
    FlyCameraController(float aspectRatio);

    void OnUpdate(float ts);                 // polls keys + RMB mouse-look
    void OnEvent(Event& e);                  // scroll = speed; resize = OnResize (return false)
    void OnResize(float width, float height);

    PerspectiveCamera&       GetCamera();
    const PerspectiveCamera& GetCamera() const;

    void      SetPose(const glm::vec3& position, float yawDeg, float pitchDeg);
    glm::vec3 GetPosition() const;
    float     GetYaw() const;                // degrees; yaw 0 looks -Z (Orbit convention)
    float     GetPitch() const;              // clamped to ±89°

    void  SetMoveSpeed(float metersPerSec);  // scroll multiplies ×1.15^clicks, clamp [0.5, 500]
    float GetMoveSpeed() const;
    void  SetBoostMultiplier(float x);       // LShift (default 4.0)
    void  SetSmoothing(float perSec);        // exp. velocity smoothing (default 12; 0 = raw)

    void SetControlEnabled(bool enabled);    // app gates on viewport hover (Orbit pattern)
    bool IsControlEnabled() const;
    void SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);
    bool IsLooking() const;                  // RMB currently held

    using GroundProbe = std::function<float(float x, float z)>;   // returns ground world-Y
    void SetGroundProbe(GroundProbe probe, float clearance = 1.5f); // clamp camera above; null = free
};
```

**Behavior:** RMB-held = mouse-look (yaw += dx·0.15°, pitch −= dy·0.15°, pitch clamp ±89°; deltas
from the per-frame screen-mouse difference, exactly like Orbit's drag path). Movement (only while
control enabled): W/S along the full look vector, A/D strafe, E/Space up (+world Y), Q/LCtrl down,
LShift boost. Velocity approaches the wish-velocity exponentially
(`v += (wish - v) * (1 - exp(-smoothing*ts))`). After integrating, if a ground probe is set:
`pos.y = max(pos.y, probe(pos.x, pos.z) + clearance)`. Derive the view matrix the same way
`OrbitCameraController` drives its `PerspectiveCamera` (open `OrbitCameraController.cpp`, find the
recompute by content, reuse the mechanism with eye = position, target = position + forward).
Key querying: `Cosmic::Input::IsKeyPressed` with the engine key codes (find the header by content
— grep `IsKeyPressed` for an existing caller to copy the include + code names).

**Frontier wiring:** `WorldContext` gains `Cosmic::FlyCameraController* Camera;` (keep
`OrbitFallback` — the nav panel gets a "Fly / Orbit" toggle; fly is default). `FrontierApp` owns
the controller; on `SetWorld` call `SetPose(info.SpawnPosition, info.SpawnYawDeg,
info.SpawnPitchDeg)`. `World::DrawPlaceholder` renders with whichever camera the toggle selects.
Update the nav-panel help text.

**Gotchas:** pitch clamp shy of ±90 or the view matrix degenerates; don't consume events (return
false, Orbit contract); `SetViewportRect` matters for future cursor math — store it now.

**Tests:** NEW `tests/test_flycamera.cpp` (+ list in `tests/CMakeLists.txt`): pose integration is
headless-safe — construct, `SetPose`, fake a frame of forward motion by calling the pure parts if
you extract them (recommended: static `ComputeWish(...)` helper), assert pitch clamps at ±89 and
the ground probe clamps `pos.y` (absolute tolerances).

**Acceptance:** build + tests green; in Frontier, enter any world: WASD+RMB flies smoothly at
DPI 100%/125%, shift boosts, scroll changes speed, orbit toggle still works; Engine3DDemo
untouched.

---

## F2 — `renderer/SceneRenderer` + wire Frontier through it

**Status:** ✅ 2026-07-03 — `renderer/SceneRenderer.h/.cpp` shipped + exported: `ScenePass` /
`SceneDrawContext` (routed submits: Reflection/Main → Renderer3D, ShadowDepth → ShadowMap::DrawCaster) /
`SceneRendererSettings` / `SceneRenderDesc` (+`SetCamera` sugar) / non-copyable `SceneRenderer` (owns
EnvironmentMap + PostProcessStack + ShadowMap; Init/Shutdown/SetViewportSize/Render). `Render()` reproduces
Engine3DDemo's exact 8-step sequence (capture final FBO → lights → env bake/IBL → shadow → reflection →
opaque HDR → transparents → post+composite) decomposed into one method per pass (F3 GPU-zone hooks), with
the private `MatrixCamera` adapter for `Scene::OnRender3D`, the ECS-lights re-assert, and a reentrancy
guard. `BindingPoints.h` note (no new slots). Frontier wired: `WorldContext::Renderer`, `FrontierApp` owns
one (lazy Init on first world entry, per-frame SetViewportSize, Shutdown in OnDetach); **IslandWorld**
replaced its placeholder with a real SceneRenderer scene (fBm island terrain 1025/2048/HS180, ocean-sized
Water @Y0 w/ planar reflection, smoke plume, monolith shadow casters, sky/IBL/shadows/bloom/fog + a ToD
scrub, feature toggles in the World Settings panel). Build + configure green; **CosmicTests 97/97**
(105,730 assertions); Frontier boots clean (Phase 11 shader self-check 8/8, zero errors, GL 4.5);
Engine3DDemo smoke-run renders its full scene 9 s with zero GL/shader/framebuffer errors (shared engine
unregressed). *In-world island pixel-check (fly-around + toggle A/B) remains a user visual pass — the
harness can't grant computer-use to the non-installed dev exe.*

The orchestration promotion. The full design was verified against Engine3DDemo's real pass
sequence on 2026-07-03 (design review); this work order is its condensed contract.

**Files:** NEW `Cosmic/src/renderer/SceneRenderer.h/.cpp`; MODIFY `renderer/BindingPoints.h`
(comment only — no new slots), `Cosmic.h` (export), `Projects/Frontier/src/World.h` (+`Renderer`
pointer), `FrontierApp.h/.cpp` (own + feed it), `worlds/IslandWorld.cpp` (the acceptance scene).

**Spec — public surface (condensed; keep these exact names):**

```cpp
enum class ScenePass : uint8_t { ShadowDepth = 0, Reflection, Main };
// Keep switches open-ended (default:) — F8 adds a top-down coverage pass value.

class COSMIC_API SceneDrawContext          // handed to DrawOpaque once per pass
{
public:
    ScenePass Pass;                        // which pass this invocation is
    glm::mat4 ViewProjection;              // lightVP / mirrored-oblique VP / camera VP
    glm::vec3 EyePosition;                 // this pass's eye (mirrored under Reflection)
    glm::vec3 CameraPosition;              // ALWAYS the real camera — LOD decisions use this
    bool IsDepthOnly() const;

    // Routed submits: Reflection/Main -> Renderer3D; ShadowDepth -> ShadowMap::DrawCaster
    // (material/color ignored there). Direct Renderer3D::Draw* calls are legal in
    // Reflection/Main (a scene is live), a bug in ShadowDepth.
    void DrawMesh (const Ref<Mesh>&, const glm::mat4&, const glm::vec4& color, int entityID = -1) const;
    void DrawMesh (const Ref<Mesh>&, const glm::mat4&, const Ref<Material>&,  int entityID = -1) const;
    void DrawModel(const Ref<Model>&, const glm::mat4&,                        int entityID = -1) const;
    // F5 appends DrawMeshInstanced here.
};

struct SceneRendererSettings
{
    bool Skybox = true; bool IBL = true; bool Shadows = true; bool WaterReflections = true;
    bool TerrainCastsShadows = true;                      // consumed from F4
    glm::vec4 ClearColor{ 0.1f, 0.1f, 0.1f, 1.0f };
    glm::vec3 ShadowCenter{ 0.0f }; float ShadowRadius = 50.0f; float ShadowBias = 0.0015f;
    bool SSAO = false;  float SsaoRadius = 0.5f, SsaoBias = 0.025f;
    bool Bloom = false; float BloomThreshold = 1.0f, BloomKnee = 0.6f, BloomIntensity = 0.6f;
    bool FXAA = true;
    bool Fog = false;   glm::vec3 FogColor{ 0.70f, 0.80f, 0.92f };
                        float FogDensity = 0.02f, FogHeightFalloff = 0.12f, FogBaseHeight = 0.0f;
    bool GodRays = false; float GodRaysIntensity = 0.6f, GodRaysDensity = 0.04f; // auto-off when !Shadows
    bool HeatHaze = false; float HeatHazeStrength = 0.02f;
    // F6 appends the underwater block; F7 appends detailed-sky + lens-flare fields.
};

struct COSMIC_API SceneRenderDesc
{
    glm::mat4 View{1}; glm::mat4 Projection{1}; glm::vec3 CameraPosition{0};
    void SetCamera(const Camera& camera);                 // sugar filling the three above
    Renderer3D::SceneLightsDesc Lights;
    float TimeSeconds = 0.0f; float Exposure = 1.0f;
    SceneRendererSettings Settings;

    Terrain* TerrainSystem = nullptr;                     // Reflection + Main (+ shadow via F4)
    std::vector<Water*>           WaterBodies;            // app submits far -> near
    int                           PrimaryReflectionWater = 0;   // index; -1 = IBL-only for all
    std::vector<ParticleEmitter*> Emitters;
    std::vector<RibbonEmitter*>   Ribbons;
    std::vector<ParticleEmitter*> DistortionEmitters;     // heat-haze field writers
    Scene* EcsScene = nullptr;                            // Main only (not Reflection)

    std::function<void(const SceneDrawContext&)> DrawOpaque;
    std::function<void(const SceneDrawContext&)> DrawTransparent;  // HDR still bound, after water/particles
    std::function<void()>                        DrawOverlay2D;    // after Composite (LDR bound)
};

class COSMIC_API SceneRenderer               // non-copyable Init/Shutdown GPU owner
{
public:
    void Init(uint32_t width, uint32_t height, uint32_t shadowMapSize = 2048);
    void Shutdown();                          // also Renderer3D::ClearIBL/ClearShadow
    bool IsInitialized() const;
    void SetViewportSize(uint32_t width, uint32_t height);
    void Render(const SceneRenderDesc& desc); // PRE: final LDR FBO bound; POST: same FBO re-bound,
                                              // viewport (0,0,w,h), depth ON/ON, cull None, blend Alpha
    EnvironmentMap&   GetEnvironment();       // APP drives sun policy through this; Render() Bake()s
    PostProcessStack& GetPostStack();
    ShadowMap&        GetShadowMap();
private:                                      // one method per pass = F3's GPU-zone hooks
    void PassShadow(...); void PassReflection(...); void PassOpaqueHDR(...);
    void PassTransparents(...); void PassPostAndComposite(...);
};
```

**Render() sequence (reproduce EXACTLY — each numbered step's state contract is load-bearing):**

1. `finalFbo = RenderCommand::GetBoundFramebuffer()` FIRST. Compute `viewProj`, `invViewProj`.
2. Lights: `Renderer3D::SetLightDirection(desc.Lights.SunDirection)`, `SetAmbient`, `SetLights`.
3. Environment: if `Settings.IBL` → `m_Environment.Bake()` (dirty-flag no-op; leaves default FBO —
   safe HERE, never mid-pass) + `PushToRenderer()`; else `Renderer3D::ClearIBL()`.
4. **PassShadow** (if `Settings.Shadows`): `m_Shadow.SetLight(dir, Settings.ShadowCenter,
   Settings.ShadowRadius)`; `BeginDepthPass()`; invoke `desc.DrawOpaque` with a ShadowDepth
   context (routes to `DrawCaster`); iterate `desc.EcsScene`'s
   `<TransformComponent, MeshRendererComponent>` view (respect `CastShadows`, skip null mesh);
   `// F4 slot: terrain depth`; `EndDepthPass()`; `PushToRenderer(Settings.ShadowBias)`. Else
   `Renderer3D::ClearShadow()`.
5. **PassReflection**: only for `WaterBodies[PrimaryReflectionWater]` (bounds-checked; skip when
   `!Settings.WaterReflections` or index −1). `BeginReflection(view, proj, camPos, reflVP,
   reflCam)` → `Renderer3D::BeginScene(reflVP, reflCam)` → skybox (`DrawSkybox(reflVP)`) →
   `TerrainSystem->Render(desc.CameraPosition)` (**REAL camera pos — LOD parity, no seams**) →
   `DrawOpaque(Reflection ctx)` → `EndScene()` → `EndReflection()`. ECS scene deliberately
   skipped in reflection (shipped-demo parity). Secondary waters get no planar capture — Water.glsl
   falls back to the IBL prefilter via ApplySceneBindings (`u_HasReflection` stays 0). Policy
   justification (recorded): one mirrored re-render per frame max; sharing one capture across
   different heights is physically wrong and looks worse than the IBL fallback.
6. **PassOpaqueHDR**: `m_Post.SetViewportSize(w,h)`; `BeginHDR(Settings.ClearColor)`;
   `Renderer3D::BeginScene(viewProj, desc.CameraPosition)` (camera UBO now = main camera and
   **stays live for step 7** — load-bearing); skybox → terrain → `DrawOpaque(Main ctx)` →
   `EndScene()`. If `EcsScene`: `OnRender3D(...)` via a private `MatrixCamera : Camera` adapter
   (the pure interface in `camera/Camera.h` — four getters), then **re-assert
   `Renderer3D::SetLights(desc.Lights)`** — `Scene::OnRender3D` re-uploads the lights UBO from ECS
   components and would silently replace the app's lights for the transparent tail (latent demo
   inconsistency, fixed here by design).
7. **PassTransparents** (HDR still bound): fetch `colorID`/`depthID` from
   `m_Post.GetSceneTarget()`; for each water IN ORDER: `w->Render(camPos, desc.TimeSeconds,
   viewProj, colorID, depthID, w, h)` (each does its own refraction grab + FBO/viewport
   re-assert); then emitters `Render(desc.View, depthID, invViewProj)`; ribbons
   `Render(desc.View, desc.TimeSeconds)`; then `desc.DrawTransparent` if set.
8. **PassPostAndComposite**: push every Settings toggle/param into `m_Post`
   (`SetSSAOEnabled/Params`, bloom, FXAA, fog); **`m_Post.SetCamera(viewProj, camPos)` before
   RenderEffects AND Composite**; god rays: enabled = `Settings.GodRays && Settings.Shadows`, feed
   `SetSunShaftInputs(m_Shadow.GetDepthID(), m_Shadow.GetLightViewProj(),
   desc.Lights.SunDirection, desc.Lights.SunColor, desc.Lights.SunIntensity)`. Distortion: if
   `Settings.HeatHaze && !desc.DistortionEmitters.empty() && m_Post.BeginDistortion()` → each
   `RenderDistortion(desc.View, depthID, invViewProj)` → `EndDistortion()`.
   `m_Post.RenderEffects(desc.Projection)`; `RenderCommand::BindFramebufferHandle(finalFbo)` +
   `SetViewport(0,0,w,h)`; `m_Post.Composite(desc.Exposure)`; `desc.DrawOverlay2D` if set.
9. Reentrancy assert (`m_InRender`) — calling Render inside DrawOpaque is a bug.

**Frontier wiring (the acceptance surface):** `FrontierApp` owns one `SceneRenderer` (Init lazily
on first world entry with the viewport size; `SetViewportSize` each frame; Shutdown in
`OnDetach`). `WorldContext` gains `SceneRenderer* Renderer`. **IslandWorld** replaces its
placeholder with a first real scene: procedural fBm island terrain (existing
`TerrainSpecification` source B, `EdgeFalloff` ≈ 0.35, `WorldSize` 2048, `HeightScale` 180,
`Resolution` 1025), one ocean-sized `Water` (surface at Y≈0, defaults), one smoke `ParticleEmitter`
somewhere visible, sky/IBL/shadows/bloom/fog ON, ToD slider in its panel driving
`renderer.GetEnvironment().SetSunDirection(-lightDir)` + `desc.Lights` (copy Engine3DDemo's
sun-from-hour math app-side). The F11/F12 orders then replace this stopgap island.

**Gotchas:** header uses forward declarations for Terrain/Water/ParticleEmitter/RibbonEmitter/
Scene/Mesh/Model/Material (includes in the .cpp — Water.cpp already includes renderer headers, so
the direction is established); don't `#include` heavy subsystem headers into SceneRenderer.h.
`FrameBuffer::GetDepthAttachmentRendererID()` exists (S1); color via
`GetColorAttachmentRendererID(0)`.

**Tests:** no new GPU tests (headless); `CosmicTests` must stay green.

**Acceptance:** build + tests green; Frontier Island shows a lit fBm island with ocean, smoke,
shadows, bloom, fog, ToD scrub — fly around it (F1); toggles in the world panel kill/restore each
feature with zero GL errors in the log; Engine3DDemo renders identically.

---

## F3 — GPU profiler (S12.5): timer-query verbs + HUD

**Status:** ☐ not started

**Files:** MODIFY `renderer/RendererAPI.h`, `renderer/RenderCommand.h`,
`platform/OpenGL/OpenGLRendererAPI.h/.cpp`, `renderer/SceneRenderer.cpp` (zones around each pass);
NEW Frontier `src/panels/GpuProfilerPanel.h/.cpp` + dock it (`TODO(F3)` in ApplyDockLayout).

**Spec — verbs (engine enums/structs, GL confined to the platform layer):**

```cpp
struct GpuZoneResult { std::string Name; float Milliseconds; uint32_t Depth; };
// RenderCommand statics:
static void BeginGpuZone(const char* name);
static void EndGpuZone();
static void GpuFrameMark();                              // call once per frame (SceneRenderer::Render start)
static const std::vector<GpuZoneResult>& GetGpuZoneResults();  // most recent RESOLVED frame
```

**OpenGL implementation:** `GL_TIME_ELAPSED` queries **cannot nest** — use `glQueryCounter(id,
GL_TIMESTAMP)` pairs per zone. Pool query objects (`glGenQueries` on demand, reuse). Keep a ring
of 3 frames, each = vector of {name, depth, startQueryID, endQueryID}; `GpuFrameMark` resolves the
oldest frame **only if** `GL_QUERY_RESULT_AVAILABLE` on its last query (never stall), converting
ns → ms. Zone stack tracks depth. `API::None` and pre-Init: all no-ops (headless-safe —
`CosmicTests` must not crash).

**SceneRenderer instrumentation:** `GpuFrameMark()` at Render() entry; wrap each private pass
method (`"Shadow"`, `"Reflection"`, `"Opaque"`, `"Transparents"`, `"Post+Composite"`) — this is
why F2 decomposed Render() into methods.

**Frontier HUD:** "GPU Profiler" dockable panel (RightBottom): a table Name | ms + a horizontal
bar scaled to the frame total; plus CPU frame ms from `ImGui::GetIO()`. Dock it in
`ApplyDockLayout` (the `TODO(F3)` line).

**Gotchas:** timestamp queries return when the GPU *reaches* the command — the delta is correct
per zone; don't glGetQueryObject on the current frame (stall). Zones may be empty when a feature
is toggled off — the HUD must render an empty table gracefully.

**Acceptance:** build + tests green; the panel shows per-pass ms that respond to toggles (bloom
off shrinks Post; reflection off zeroes Reflection); numbers stable, no stalls (fps unchanged
within noise when the panel is open).

---

## F4 — Terrain growth: app heightfield source, shadow casting, wet band

**Status:** ☐ not started

**Files:** MODIFY `terrain/Terrain.h/.cpp`, `renderer/SceneRenderer.cpp` (the reserved shadow
slot), `scene/Components.h` is NOT touched. Shaders (already written): `TerrainDepth.glsl` +
`Terrain.glsl`'s wet-band uniforms.

**Spec:**
1. `TerrainSpecification` gains **Source C**:
   `std::function<float(float u, float v)> HeightFunction;` — when set it wins over
   HeightmapPath/fBm; sampled at every heightfield texel with u,v ∈ [0,1] (texel centers exactly
   as the fBm path samples), return clamped to [0,1]. Deterministic → `SampleHeight` unit tests
   apply unchanged.
2. `void Terrain::RenderDepth(const glm::mat4& lightViewProj, const glm::vec3& cameraPos);` —
   walks the SAME quadtree cut as `Render(cameraPos)` (share the node-selection code — refactor
   the cut into a private helper both call) but binds `TerrainDepth.glsl` (lazy-created like the
   main shader) and sets only `u_LightViewProj` + the per-node/height uniforms the depth shader
   declares. No render-state changes (ShadowMap's pass owns state).
3. Wet band: `TerrainMaterialParams` gains `float WetLine = 0, WetBand = 1.5f, WetDarken = 0;`
   → uploaded to the matching `Terrain.glsl` uniforms each draw (0 default = off).
4. Expose for water-v2 shore awareness (F6 consumes):
   `uint32_t GetHeightTextureID() const;` (the packed height+normal texture),
   plus `glm::vec2 GetWorldMinCorner() const`, `float GetWorldSize() const` (derive from
   Origin/WorldSize), and HeightScale/BaseHeight accessors if not public already.
5. SceneRenderer: in PassShadow's reserved slot —
   `if (desc.TerrainSystem && Settings.TerrainCastsShadows) desc.TerrainSystem->RenderDepth(m_Shadow.GetLightViewProj(), desc.CameraPosition);`

**Gotchas:** the depth pass runs with the shadow map's viewport already set by BeginDepthPass —
don't touch it. The quadtree cut MUST use the real camera position (not the light) or casters pop
between LODs vs. the receiver. `Create()` stays CPU-only/headless-safe (HeightFunction is pure
CPU).

**Tests:** extend `tests/test_phase10_world.cpp` (or NEW `test_terrain_source.cpp` + CMake list):
a linear-ramp HeightFunction → `SampleHeight` reproduces the ramp within 16-bit quantization
(absolute tolerance ~ HeightScale/65535 × 2 + ε) at texel corners and mid-cell.

**Acceptance:** build + tests green; in Frontier island (F2 scene), sunset ToD casts mountain
shadows across the valley (toggle `TerrainCastsShadows` to compare); no shimmer regression;
Engine3DDemo identical.

---

## F5 — Instancing (S12.3-lite) + frustum culling (S12.1-lite)

**Status:** ☐ not started

**Files:** MODIFY `renderer/BindingPoints.h` (claim `InstancesSsbo = 9`), `renderer/Renderer3D.h/.cpp`,
`renderer/ShadowMap.h/.cpp`, `renderer/SceneRenderer.h/.cpp` (`SceneDrawContext::DrawMeshInstanced`
routing); NEW `graphics/InstanceBuffer.h` + `platform/OpenGL/OpenGLInstanceBuffer` — OR reuse
`StorageBuffer` (preferred: **reuse `StorageBuffer::Create(size, 9)`**, no new resource type; add
a thin engine-side `renderer/InstanceSet.h` value type wrapping it); NEW `Cosmic/src/math/Frustum.h`;
NEW `tests/test_frustum.cpp` (+ CMake list). Shaders (already written): `PBRInstanced.glsl`,
`ShadowDepthInstanced.glsl` — read them: std430 struct is `{ mat4 Model; vec4 Tint; }` (80 bytes,
binding 9).

**Spec:**

```cpp
// renderer/InstanceSet.h — CPU-side packing + the SSBO (engine, generic)
class COSMIC_API InstanceSet
{
public:
    static Ref<InstanceSet> Create(uint32_t capacity);
    void SetInstances(const glm::mat4* transforms, const glm::vec4* tints, uint32_t count); // tints null -> white
    void Bind() const;                    // re-binds the SSBO at Bindings::InstancesSsbo
    uint32_t GetCount() const;
};

// Renderer3D:
static void DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
                              const Ref<InstanceSet>& instances, uint32_t count, int entityID = -1);
// material->BindFull() -> engine uniforms (NO u_Model/u_NormalMatrix — the SSBO replaces them)
// -> ApplySceneBindings -> instances->Bind() -> RenderCommand::DrawIndexedInstanced(vao, idxCount, count)

// ShadowMap:
void DrawCasterInstanced(const Ref<Mesh>& mesh, const Ref<InstanceSet>& instances, uint32_t count);
// binds its lazy ShadowDepthInstanced.glsl, sets u_LightViewProj, instances->Bind(), instanced draw

// SceneDrawContext:
void DrawMeshInstanced(const Ref<Mesh>&, const Ref<Material>&, const Ref<InstanceSet>&,
                       uint32_t count, int entityID = -1) const;   // routes per Pass

// math/Frustum.h — header-only, headless-testable
struct COSMIC_API Frustum
{
    glm::vec4 Planes[6];                                  // ax+by+cz+d, normals pointing IN
    static Frustum FromViewProjection(const glm::mat4& vp);   // Gribb-Hartmann extraction
    bool IntersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
};
```

**Culling policy (documented):** the app culls ONCE per frame against the MAIN camera frustum
(inflate by one node/instance radius to keep near-offscreen shadow casters), builds the visible
list, `SetInstances` once, and draws that set in every pass. Per-pass re-upload is a Phase 12
refinement.

**Gotchas:** `RenderCommand::DrawIndexedInstanced` already exists — do NOT add a new verb. The
instanced shaders derive normals from `mat3(Model)` — document the uniform-scale assumption in
`InstanceSet`'s header. std430 stride is naturally 80 bytes for {mat4, vec4} — pack a plain
`struct { glm::mat4 m; glm::vec4 t; }` array, `static_assert(sizeof == 80)`.

**Tests:** `test_frustum.cpp` — a unit cube at origin vs an identity-ish perspective VP: inside /
outside each face / straddling; sphere variants; absolute tolerances.

**Acceptance:** build + tests green; a quick scatter in the F2 island scene (few thousand cones as
stand-in pines on `SampleHeight`) renders in ONE draw (profiler F3 shows it), casts shadows, and
disappears cleanly when culled behind the camera; fps ≥ 60.

---

## F6 — Water v2 C++ (8 waves, shore, caustics, whitecaps, sparkle, underwater)

**Status:** ☐ not started

**Files:** MODIFY `water/Water.h/.cpp`, `renderer/PostProcessStack.h/.cpp` (underwater),
`renderer/SceneRenderer.h/.cpp` (Settings growth), `tests/test_phase10_world.cpp` (8-wave
invariants). Shaders (already written — READ FIRST): `Water.glsl` v2, `Tonemap.glsl` underwater
block, `WaterFlow.glsl` (consumed by app content items; no engine C++).

**Spec:**
1. **8 waves:** upload cap 4 → 8 (`u_WaveDirKA[8]`/`u_WaveQOP[8]`, count clamp, header comment).
   CPU queries already evaluate the full vector — verify and extend the doctest invariants to an
   8-wave set.
2. **Shore awareness:** `WaterSpecification` gains `float ShoreDepthRange = 6.0f;`; NEW
   `void Water::SetShoreTerrain(const Ref<Terrain>& terrain);` — stores it; each Render binds the
   terrain's packed height texture (F4 accessor) on the water's next free LOCAL unit (Water.cpp
   owns units 0..5 today; use 6 — engine-reserved units 8+ untouched) and sets `u_ShoreHeightTex`,
   `u_HasShoreTex = 1`, `u_ShoreRect` (xy = terrain world min corner, zw = 1/worldSize),
   `u_ShoreHeight` (x = HeightScale, y = BaseHeight), `u_ShoreDepthRange`. Null resets the gate.
3. **New optics:** spec fields `float CausticStrength = 0, CausticScale = 0.15f,
   SparkleStrength = 0, WhitecapStrength = 0;` → matching uniforms each draw.
4. **Underwater:** `PostProcessStack::SetUnderwater(bool enabled, float waterlineY,
   const glm::vec3& color, float density, const glm::vec3& tint);` → the `Tonemap.glsl`
   `u_UseUnderwater/u_WaterlineY/u_UnderwaterColor/u_UnderwaterDensity/u_UnderwaterTint`
   uniforms in Composite. `SceneRendererSettings` gains
   `bool Underwater = false; float UnderwaterY = 0; glm::vec3 UnderwaterColor{0.05f,0.18f,0.22f};
   float UnderwaterDensity = 0.08f; glm::vec3 UnderwaterTint{0.55f,0.75f,0.90f};` plumbed in
   PassPostAndComposite.
5. **Keep `Water::Render`'s signature stable** (F2's transparent tail must not churn).

**Gotchas:** the shore texture is the S8 hi/lo-byte packed format — the shader texelFetches it
(bilinear would corrupt the packing); just bind with existing sampling state. The gate-uniform
rule: if waves don't flatten at the beach, `u_HasShoreTex` wasn't set. Underwater needs the
camera BELOW `waterlineY` — the app decides the waterline (primary water surface height).

**Tests:** extend the Gerstner inversion invariants to 8 waves; underwater/caustics are visual.

**Acceptance:** build + tests green; island (F2/F12a scene): waves visibly flatten + foam at the
beach, caustics dance on the shallows, whitecaps appear when the panel slider raises
WhitecapStrength, flying the camera underwater tints + fogs the frame; Engine3DDemo's lake
(no new uniforms set) renders exactly as before.

---

## F7 — Sky v2 C++ (detailed sky, moon/night IBL, lens flare)

**Status:** ☐ not started

**Files:** MODIFY `renderer/EnvironmentMap.h/.cpp`, `renderer/PostProcessStack.h/.cpp`,
`renderer/SceneRenderer.h/.cpp`. Shaders (already written — READ FIRST): `SkyDetail.glsl`,
`EnvSky.glsl` night tier, `LensFlare.glsl`.

**Spec:**
1. **Night IBL:** `EnvironmentMap::SetNightSky(bool enabled)`, `SetMoon(const glm::vec3& toMoon,
   float intensity)` — stored, marked dirty, fed to `EnvSky.glsl`'s `u_NightSky` /
   `u_MoonDirection` / `u_MoonIntensity` during the bake. Defaults keep the legacy bake
   byte-identical.
2. **Detailed sky pass:** `struct SkyDetailDesc { float SkyIntensity = 1; float SunDiscIntensity = 40;
   float SunAngularRadius = 0.00465f; glm::vec3 MoonDirection{0,1,0}; float MoonIntensity = 0;
   float MoonAngularRadius = 0.0087f; float StarIntensity = 1; float StarDensity = 90;
   float MilkyWayIntensity = 0.35f; glm::vec3 MilkyWayDir{0.36f,0.48f,0.80f}; float Time = 0; };`
   NEW `EnvironmentMap::DrawSkyboxDetailed(const glm::mat4& viewProj, const SkyDetailDesc&)` —
   same draw shape as `DrawSkybox` (depth off/restore, fullscreen triangle) but binds
   `SkyDetail.glsl` (lazy) and sets its uniforms (`u_InvViewProj` = inverse(viewProj), sun dir =
   the stored env sun). `SceneRenderDesc` gains `const SkyDetailDesc* DetailedSky = nullptr;` —
   when set, PassOpaqueHDR (and PassReflection) draw the detailed sky instead of the baked cube.
3. **Lens flare:** `PostProcessStack::SetLensFlare(bool enabled, float intensity,
   const glm::vec3& tint)` + per-frame `SetLensFlareSun(const glm::vec3& sunTravelDir)`. The pass
   runs in Composite's LDR stage AFTER tonemap (before FXAA), additively:
   `SetBlendMode(Additive)` → bind `LensFlare.glsl` → uniforms: `u_Depth` (scene depth slot),
   `u_SunScreenPos` + `u_SunInFront` computed C++-side by projecting `camPos - sunTravelDir*1e4`
   through the stored `m_ViewProjection` (w > 0 = in front; uv = ndc*0.5+0.5), `u_Intensity`,
   `u_Tint`, `u_Aspect = w/h` → fullscreen triangle → `SetBlendMode(Alpha)` restore.
   `SceneRendererSettings` gains `bool LensFlare = false; float LensFlareIntensity = 0.35f;`.
4. **Moonlight-as-shadow-light stays APP policy** (F12a/F13 set `desc.Lights.SunDirection` to the
   moon's travel dir at night, cool color, low intensity — engine unchanged).

**Gotchas:** SkyDetail's palette is a documented copy of EnvSky's — if a future item retunes one,
retune both. The flare's occlusion taps read the HDR target's DEPTH — pass the depth ID through
to the LDR stage (PostProcessStack already holds the scene target). Sun screen position must use
the SAME viewProj as the frame (SetCamera).

**Acceptance:** build + tests green; island at noon: crisp sun disc + limb darkening + flare that
occludes behind the volcano; scrub past sunset: stars fade in with twinkle + milky way, phased
moon rises, scene ambient goes cool (rebaked night IBL); Engine3DDemo (never calls the new APIs)
identical.

---

## F8 — Snow system (S11.1): overlay push + coverage capture + presets

**Status:** ☐ not started

**Files:** MODIFY `renderer/BindingPoints.h` (claim `TexUnitSnowMask = 12`),
`renderer/Renderer3D.h/.cpp` (SetSnow/ClearSnow + ApplySceneBindings push),
`renderer/SceneRenderer.h/.cpp` (open the `ScenePass` enum: add `TopDownDepth`; drive an optional
CoverageCapture from the desc); NEW `renderer/CoverageCapture.h/.cpp`; NEW `particles/Presets.h`;
NEW `tests/test_presets.cpp` (+ CMake list). Shaders (already written — READ FIRST): the snow
blocks in `PBR.glsl` / `PBRInstanced.glsl` / `Terrain.glsl`, and `SnowAccum.glsl`.

**Spec:**
1. **Scene-wide overlay:**
   ```cpp
   struct SnowDesc { float Amount = 1; float Line = 30; float BlendHalf = 6; float SlopeSharp = 3;
                     glm::vec3 Color{0.93f,0.95f,0.98f}; float Sparkle = 0.5f;
                     uint32_t MaskTextureID = 0;             // 0 = uniform coverage
                     glm::vec2 MaskWorldMin{0}; glm::vec2 MaskWorldInvSize{0};
                     glm::vec2 MaskYDecode{0};               // worldY = g*x + y
                     float MaskYTolerance = 0.5f; };
   Renderer3D::SetSnow(const SnowDesc&); Renderer3D::ClearSnow();
   ```
   `ApplySceneBindings` uploads the `u_Snow*` uniforms unconditionally (0s when cleared) and binds
   the mask texture on `Bindings::TexUnitSnowMask` when non-zero. Terrain additionally receives
   `u_SnowOverlayAmount` (from a `float OverlayAmount = 0;` SnowDesc field — mask-driven snow
   below the snow line).
2. **`renderer/CoverageCapture`** (generic — snow is one use; non-copyable Init/Shutdown owner):
   `Init(uint32_t resolution, const glm::vec2& worldMin, float worldSize, float worldYMin,
   float worldYMax)`; owns a depth-only FBO (top-down ortho looking −Y over the rect) + an RG16F
   ping-pong pair. `BeginDepthCapture()/EndDepthCapture()` (bind depth FBO + ortho viewport /
   restore — ShadowMap's shape); `UpdateCoverage(float dt, float accumPerSec, float meltPerSec)` —
   fullscreen SnowAccum.glsl pass into the write target reading the read target + the depth
   capture, with `u_DepthToWorldY` / `u_WorldYEncode` derived from the ortho volume, then swap.
   Getters: `GetMaskTextureID()`, `FillSnowDesc(SnowDesc&) const` (rect/decode fields).
   **SceneRenderer hookup:** `SceneRenderDesc` gains `CoverageCapture* Coverage = nullptr; float
   CoverageAccumPerSec = 0; float CoverageMeltPerSec = 0;` — when set, right after PassShadow:
   depth capture invoking `desc.DrawOpaque` with a `ScenePass::TopDownDepth` context (routes to
   `Coverage`'s depth draw — reuse ShadowDepth/TerrainDepth/instanced shaders via the same
   DrawCaster-style methods on CoverageCapture) + terrain depth + ECS casters, then
   `UpdateCoverage(dt…)` (dt from a new `float DeltaTime` field on the desc).
3. **`particles/Presets.h`** (header-only pure functions returning `ParticleEmitterSpec`):
   `Presets::SoftPuff()`, `Presets::Snowfall(glm::vec3 boxExtents, float rate)` (box shape, slow
   fall + drift wind, white fade), `Presets::Embers(float rate)` (additive, cone up, warm ramp),
   `Presets::SmokeColumn(float rate)` (large soft flipbook puffs, slow rise, grey ramp),
   `Presets::Mist(glm::vec3 boxExtents)` (huge slow soft puffs, low alpha). Rain rides F9.
4. `SnowDesc` values are APP policy — nothing scenario-shaped in the engine.

**Gotchas:** RG16F is color-renderable; clear the ping-pong on Init and use `u_FirstFrame` on the
first update. The mask's Y-decode MUST match `u_WorldYEncode` (one derivation, two uniform sets).
The overlay must stay a zero-cost no-op when cleared (all gates 0) — Engine3DDemo proof.

**Tests:** `test_presets.cpp` — presets return sane invariants (rates > 0, life ranges ordered,
snow gravity magnitude < 2 m/s², embers additive). CoverageCapture is GPU-only (visual).

**Acceptance:** build + tests green; island panel: enable snowfall → snow visibly accumulates on
terrain + instanced pines over ~30 s, sheltered ground under a test slab stays bare, sparkle
twinkles in sun; ClearSnow restores exactly; Engine3DDemo identical.

---

## F9 — Rain bits: velocity stretch + splash preset

**Status:** ☐ not started

**Files:** MODIFY `particles/ParticleSystem.h/.cpp` (spec field + uniform), `particles/Presets.h`
(Rain + SplashRings). Shader (already written): `ParticleBillboards.glsl`'s
`u_StretchByVelocity`.

**Spec:** `ParticleEmitterSpec` gains `float StretchByVelocity = 0.0f;` (seconds of velocity the
quad elongates by) → uploaded alongside the other billboard uniforms in BOTH the GPU and CPU
render paths. `Presets::Rain(glm::vec3 boxExtents, float rate)` — box spawn, straight-down
9.8 m/s² gravity pre-integrated into SpeedMin/Max (fast), thin size, `StretchByVelocity ≈ 0.02`,
cool translucent tint, short life ending at the water/ground plane (app positions the box).
`Presets::SplashRings(float rate)` — flat camera-up?? No — rings lie on the water: use the
existing billboard path with a top-down composition: spawn at surface, tiny life, size ramp up,
alpha down (reads as an expanding ring with the soft-puff sheet). Lightning/thunder is APP code
(`Projects/Frontier/src/common/LightningDirector.h`, written by F16) — no engine part.

**Gotchas:** the stretch shader path needs the emitter transform's rotation for local-space
velocities — already handled in the shader (`mat3(u_EmitterTransform)`); just set the uniform.
Keep the field's default 0 (byte-identical legacy emitters).

**Acceptance:** build + tests green; a Rain preset emitter in any world renders elongated streaks
that lengthen with fall speed; setting the field back to 0 restores round puffs.

---

## F10 — Ambience audio (app-side)

**Status:** ☐ not started

**Files (ALL app-side):** NEW `Projects/Frontier/src/common/ProceduralAudio.h/.cpp` (WAV synth +
cache), NEW `Projects/Frontier/src/common/DistanceLoop.h` (header-only).

**Spec:**
1. `ProceduralAudio::Ensure(const char* name)` → returns a `Ref<Cosmic::Sound>`: if
   `project://sounds/<name>.wav` resolves to an existing file, load it (user-droppable CC0
   assets win); else synthesize into `user://frontier/audio/<name>.wav` once (16-bit PCM mono
   44.1 kHz WAV writer — RIFF header + samples) and load that. Recipes:
   `"rumble"` = brown noise (integrated white, leaky) low-pass ~80 Hz + slow ±20% LFO, 8 s loop;
   `"wind"` = pink-ish noise band 200–800 Hz with slow amplitude wander, 10 s;
   `"water"` = white noise band 1–4 kHz with bubbly AM, 6 s; `"thunder"` = brown-noise burst with
   2 s exponential decay (one-shot). Make loops seamless (crossfade the last 0.5 s into the
   start before writing).
2. `DistanceLoop` — owns a `SoundHandle` from `AudioEngine::PlayLooping(sound, 0)`;
   `Update(const glm::vec3& listener, const glm::vec3& source, float radius, float maxVol)` →
   `SetVolume(handle, maxVol * clamp(1 - dist/radius, 0, 1)^2)`. Start/Stop lifecycle; safe when
   audio is headless-degraded (handles are 0 → all calls no-op).
3. Worlds use them: volcano rumble (source = caldera), wind at altitude (volume by camera Y),
   water babble near the waterfall, thunder fired by F16's LightningDirector with a
   distance-scaled delay (~0.3 s/100 m).

**Gotchas:** `Sound::Create` never returns null (degraded-silent) — no crash paths; resolve
`project://` in the app DLL (§0.6). Write the WAV cache with `std::ofstream` binary; create the
directory via the engine's FileSystem helpers if present (find by content) else
`std::filesystem::create_directories`.

**Acceptance:** build + tests green (audio tests stay headless-safe); flying toward the volcano
swells the rumble smoothly; deleting `user://frontier/audio` regenerates on next run; dropping a
real `rumble.wav` into `Projects/Frontier/assets/sounds/` overrides after a rebuild (asset sync).

---

## F11 — Island heightfield composer (app) + `Noise::Ridged2D` (engine)

**Status:** ☐ not started

**Files:** MODIFY `Cosmic/src/math/Noise.h` (+ its test file `tests/test_noise.cpp`); NEW
`Projects/Frontier/src/common/HeightfieldComposer.h` (header-only, pure).

**Spec — engine:** `float Noise::Ridged2D(float x, float y, int octaves = 5,
float lacunarity = 2.0f, float gain = 0.5f)` — per octave `r = 1 - |Perlin2D|`, squared, weighted
by the previous octave's value (classic ridged multifractal), normalized to [0, 1]. Deterministic
per seed; doctest: range ⊂ [0,1], determinism, ridges sharper than plain Fbm (variance check ok).

**Spec — app (`Frontier::HeightfieldComposer`):** pure functions over normalized island UV
(u,v ∈ [0,1], world = min + uv·size):

```cpp
struct IslandParams {
    uint32_t Seed = 20260703;
    // world-normalized [0,1] positions/radii:
    glm::vec2 VolcanoCenter{0.62f, 0.38f}; float VolcanoRadius = 0.16f;
    float VolcanoHeight = 1.0f;            // normalized height of the cone peak (maps to spec.HeightScale)
    float CalderaRadius = 0.045f; float CalderaDepth = 0.35f;
    glm::vec2 RangeA{0.18f, 0.22f}, RangeB{0.42f, 0.78f};  // ridge spine segment
    float RangeWidth = 0.16f; float RangeHeight = 0.75f;
    glm::vec2 LakeCenter{0.36f, 0.55f}; float LakeRadius = 0.085f; float LakeDepth = 0.16f;
    float SeaShelf = 0.10f;                // beach shelf width (edge-falloff band)
    std::vector<glm::vec2> RiverPath;      // lake -> coast waypoints (defaults provided)
    float RiverWidth = 0.012f; float RiverDepth = 0.05f;
};
float IslandHeight(const IslandParams& p, float u, float v);   // -> [0,1], plug into HeightFunction
```

Recipe (compose in this order; smooth everything — `smoothstep`, no hard max unless noted):
base rolling fBm (Fbm2D, freq 3, 6 octaves, weight 0.18) → **ridged range**: distance to segment
AB, band `smoothstep(RangeWidth,0,d)` × `Ridged2D`(freq 5) × RangeHeight, combined with
`h = max(h, range)` (sharp ridgelines are correct here) → **volcano cone**:
`cone = VolcanoHeight * smoothstep(VolcanoRadius, VolcanoRadius*0.15f, r)` + rim noise
(±6% Fbm at freq 18) then **caldera**: subtract `CalderaDepth * smoothstep(CalderaRadius,
CalderaRadius*0.45f, r)`; `h = max(h, cone)` → **lake basin**: subtract
`LakeDepth * smoothstep(LakeRadius, LakeRadius*0.3f, rLake)` (clamp ≥ shallow floor) → **river**:
subtract `RiverDepth * smoothstep(RiverWidth, 0, distToPolyline)` → **coast**: multiply by the
edge falloff `smoothstep(0.5, 0.5 - SeaShelf, max(|u-0.5|,|v-0.5|))` and re-bias so the sea floor
sits at ~0.04 (underwater shelf, not zero cliff) → clamp [0,1]. Waterline conventions the later
items rely on: with `HeightScale = 900` and `BaseHeight = -80`, the OCEAN surface sits at world
Y = 0 (heights below 80/900 ≈ 0.089 are submerged) and the LAKE surface at the basin's carved rim
minus ~2 m — expose `float OceanFloor01(const IslandParams&)` + `float LakeSurfaceWorldY(...)`
helpers so F12a doesn't re-derive.

**Gotchas:** every helper takes the params struct — no globals, fully deterministic (same seed →
same island; the doc's defaults are THE island). Keep it header-only pure so a future test target
could include it.

**Acceptance:** build + tests green (Ridged2D tests); temporary: point the F2 island scene's
`HeightFunction` at `IslandHeight` and confirm the shape reads (volcano SE, range NW, lake basin,
beach ring) before F12a tunes materials.

---

## F12a — Island assembly I: terrain, ocean + lake, sky, time-of-day

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/IslandWorld.h/.cpp`; NEW `common/DayNightCycle.h` (header-only).

**Checklist:**
1. Terrain spec: `Resolution 2049`, `WorldSize 4096`, `HeightScale 900`, `BaseHeight −80`,
   `HeightFunction = IslandHeight(params, u, v)`, LOD defaults. Splat: base = alpine grass
   (0.22,0.34,0.14), slope = grey basalt rock, high = snow (HighHeight ≈ 380, HighBlend 40),
   low = sand (LowHeight ≈ 2.5, LowBlend 2). Material wet band: `WetLine 0.4, WetBand 1.6,
   WetDarken 0.8`.
2. Waters: OCEAN — extent 6000² centered on the island, `SurfaceHeight 0`, 6-wave swell rolling
   shoreward, `ShoreDepthRange 8`, caustics 0.6, sparkle 0.4, `SetShoreTerrain(terrain)`,
   reflection 1024; LAKE — sized to the basin at `LakeSurfaceWorldY`, 3 tiny waves, caustics 0.9.
   `desc.WaterBodies = {ocean, lake}`; `PrimaryReflectionWater` = whichever the camera is nearer
   (switch per frame — free, per the F2 policy).
3. `DayNightCycle` (app policy, reusable by every world): `struct DayState { glm::vec3 SunDir;
   glm::vec3 SunColor; float SunIntensity; float Ambient; glm::vec3 FogColor; float FogDensity;
   glm::vec3 MoonDir; float MoonIntensity; bool Night; SkyDetailDesc Sky; };
   DayState Evaluate(float hours);` — sun elevation/azimuth from hour (sunrise 6, zenith 12,
   sunset 18 — copy Engine3DDemo's math), palettes warm at the terminators; night: moon opposite
   the sun's azimuth at +35° elevation, `Lights.SunDirection` becomes the moon travel dir
   (intensity ~0.12, color 0.62/0.71/0.90), `env.SetNightSky(true)` + `SetMoon`. World panel: ToD
   slider + play speed.
4. Frame: fill `SceneRenderDesc` (terrain, waters, `DetailedSky = &state.Sky`, lens flare on,
   settings from the panel), `renderer->Render(desc)`.

**Acceptance:** fly the whole island at 60 fps (profiler open): beach breakers + wet sand, snowy
range, distinct lake, crisp sun/flare by day, stars/moon by night; both waters correct with the
primary-reflection handoff unnoticeable.

---

## F12b — Island assembly II: the volcano

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/IslandWorld.cpp`; NEW `common/LavaFlowBuilder.h` (header-only).

**Checklist:**
1. **`LavaFlowBuilder`**: from a start point on the crater rim, march downhill
   (`pos -= normalize(XZ(SampleNormal)) * step` with `SampleHeight` re-projection, step ≈ 4 m,
   stop at the sea or after N steps); emit a triangle-strip `Mesh` ribbon (width 6–10 m, hugging
   the terrain +0.3 m, UV.u = arclength/width, UV.v ∈ [0,1]) via `Mesh::Create`. Build 2–3 flows
   from different rim points (seeded).
2. Lava material: `Material::Create(AssetLibrary::GetShader("assets/shaders/FlowEmissive.glsl"))`
   with `u_FlowSpeed 0.05, u_NoiseScale (3,1.5), u_Heat 0.85, u_EmissiveIntensity 6,
   u_CrustColor (0.035,0.025,0.025), u_EdgeCool 0.8, u_CoolAlongLength 0.15, u_CrackScale 14,
   u_RippleAmp 0.15`; set `u_Time` each frame. Caldera LAVA LAKE = a disc mesh with radial UVs,
   same shader, `u_Heat 0.95`, slow `u_FlowSpeed`.
3. Effects: smoke column (`Presets::SmokeColumn`, scaled up, emitter at the caldera, wind-bent),
   embers (`Presets::Embers`, additive), heat haze (`DistortionEmitters` over the lava lake +
   flows), steam fumaroles (3–4 small `Presets::SoftPuff` white emitters on the flanks), 2–3
   point lights along the flows (warm, radius ~60, intensity pulsing gently) — mind
   `kMaxPointLights = 16`.
4. Audio: F10 rumble `DistanceLoop` at the caldera (radius ~900 m).

**Acceptance:** day: smoke + shimmering haze + glowing cracks read from km away; night (ToD):
lava dominates exposure, blooms, lights the smoke column's base, embers stream — the "realistic
volcano" money shot; ≥ 60 fps.

---

## F12c — Island assembly III: forests, waterfall + river, wildlife

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/IslandWorld.cpp`; NEW `common/Scatter.h`, `common/Boids.h`,
`common/ProceduralMeshes.h` (all header-only).

**Checklist:**
1. **`ProceduralMeshes`**: `MakePine(seed)` — 3 stacked cones + cylinder trunk merged into one
   `Mesh` (append vertex/index vectors before `Mesh::Create`); `MakeBoulder(seed)` — a cube-sphere
   with Perlin-displaced vertices, recomputed flat normals.
2. **`Scatter`**: deterministic points via the engine RNG — reject slopes
   (`SampleNormal().y < 0.72`), altitude bands (pines 8–300 m, boulders anywhere dry), lake/river
   keep-out; per-instance transform (Y from `SampleHeight`, random yaw, scale 0.8–1.3) + tint
   jitter → two `InstanceSet`s (~4–8 k pines, ~1.5 k boulders), culled per frame with
   `Frustum::FromViewProjection` (inflated; see F5 policy) and drawn via
   `ctx.DrawMeshInstanced` with `PBRInstanced` materials.
3. **Waterfall + river**: strip meshes along the F11 river path (same builder as lava — reuse
   `LavaFlowBuilder`'s ribbon with a steeper drop at the cliff): river = `WaterFlow.glsl` material
   (`u_FlowSpeed 0.25, u_Opacity 0.55, u_FoamStrength 0.35`), fall = near-vertical sheet
   (`u_FlowSpeed 1.6, u_FoamStrength 1.0, u_Opacity 0.8`); draw in `DrawTransparent` (depth write
   OFF via `RenderCommand::SetDepthWrite(false)` + restore). Mist: `Presets::Mist` at the plunge
   pool + `Presets::SoftPuff` spray; water babble `DistanceLoop`.
4. **Wildlife**: `Boids` — ~40 birds (stretched-tetrahedron mesh instances, one `InstanceSet`
   updated per frame) circling the range with classic separation/alignment/cohesion + a loose
   orbit anchor; fish splash rings: every 4–9 s pick a lake point, `Burst` a `SplashRings`
   emitter moved there + a small `Water::SampleHeight`-timed plop; fireflies: additive tiny
   particles near the shore, alpha gated to night by ToD.

**Acceptance:** forests read as forests (snow-dusted at altitude via SetSnow), one-draw-per-set
in the profiler, waterfall + river animate into the lake with mist, birds wheel over the ridge,
fish ring the lake, fireflies at dusk; ≥ 60 fps everywhere.

---

## F13 — Night Volcano variant

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/NightVolcanoWorld.cpp` (reuse F12b pieces — factor shared volcano
builders into `common/` if IslandWorld hasn't already).

Fixed dusk (ToD locked ~20.5 h, panel-overridable), close-in caldera terrain (WorldSize 1024,
the F11 volcano params re-centered), lava lake + 2 flows, ember storm (rate ×3), smoke column lit
by 3 pulsing point lights, god rays ON through the smoke, heat haze strong, star/moon sky, rumble
loud. Exposure tuned so lava carries the frame (~0.9), bloom threshold ~1.1.

**Acceptance:** the money shot — screenshot committed by the user; ≥ 60 fps.

---

## F14 — Blizzard Peak variant

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/BlizzardWorld.cpp`.

Small ridged terrain (WorldSize 768, Ridged2D-dominant heightfield), cabin from box/cylinder
primitives with TWO warm emissive window quads (`u_Emissive` PBR material), instanced pines,
`CoverageCapture` over the 768² rect driven by the desc (`CoverageAccumPerSec ~1/45`), snowfall
`Presets::Snowfall` in a 120 m box tracking the camera + `StretchByVelocity 0.01` +
`Wind (9, 0, 3)`, `SetSnow` overlay w/ `OverlayAmount` ramped by accumulated time, grey sky
(SkyIntensity 0.4, fog dense 0.012, FogColor 0.62/0.65/0.70), sun a dim cool disc, wind audio
gusting (volume LFO).

**Acceptance:** snow VISIBLY builds on the cabin roof + pine tops over ~45 s while the sheltered
porch floor stays bare (the mask's Y-rejection working); whiteout depth cues from fog; ≥ 60 fps.

---

## F15 — Dawn Mirror Lake variant

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/MirrorLakeWorld.cpp`.

Basin terrain (WorldSize 1024), lake water with amplitudes ≤ 0.02 m (2 waves), reflection 2048,
`DetailStrength 0.08`, caustics 0.9, `SetShoreTerrain`; ToD locked 6.8 h (golden hour) + lens
flare; mist banks (`Presets::Mist`, 3 wide emitters skimming the surface); pine shoreline
(instanced); god rays through the eastern pines; fish splash rings every 5–10 s; loon-ish "water"
ambience + soft wind.

**Acceptance:** the water reads as a MIRROR (planar reflection dominant, barely-perturbed);
mist drifts; the "realistic water" money shot; ≥ 60 fps.

---

## F16 — Storm Ocean variant

**Status:** ☐ not started

**Files (app):** MODIFY `worlds/StormOceanWorld.cpp`; NEW `common/LightningDirector.h`.

Open water only (no terrain): ocean 8-wave set (two big swells λ 60/38 m, A 1.6/1.0, steepness
0.8 + six choppy detail waves), `WhitecapStrength 0.8`, `SparkleStrength 0`, reflection 512 (it's
all spray anyway), storm sky (SkyIntensity 0.35, fog 0.010, grey palette via low sun + dense
fog), rain `Presets::Rain` (200×80×200 m box tracking the camera, rate ~4000) + `SplashRings` on
the surface, a buoy (box + cylinder) riding `SampleHeight/SampleNormal`.
**`LightningDirector`**: every 6–18 s (RNG) pick an azimuth; for 0.12 s pulse
`Lights.SunIntensity` ×6 with a cold white color + `SkyDetailDesc.SkyIntensity` ×3; schedule
`thunder` one-shot at `distance/340 s` delay with distance-scaled volume (F10).

**Acceptance:** heavy seas with breaking whitecaps, rain streaks + rings, the buoy pitches
convincingly, lightning flashes then thunder rolls late; camera below the surface tints/fogs
(F6 underwater); ≥ 60 fps.

---

## F17 — Performance & acceptance pass (closes the phase)

**Status:** ☐ not started

Profiler-evidence pass over all five worlds at 1080p on the dev GPU: every world ≥ 60 fps with
the HUD open; note the top-3 costs per world in this doc; obvious wins only (reflection
resolution, emitter counts, instance counts — no engine work). User commits screenshots of each
world + the profiler HUD. Update: this doc's banners, doc 05 §10 (S11 ✅ w/ deviations), master
roadmap Phase 11 status. Remaining S12 items (frustum culling generalization, sort keys, LODs,
texture pipeline) explicitly roll to Phase 12.

**Done when:** the roadmap's Phase 11 line — "volcano/snow/water demos ≥ 60 fps at 1080p with
profiler evidence" — is demonstrably true in one app.

---

## After Phase 11 — where the future work lives

Phase 11 is the last CONTENT phase; what remains, and where to read about it:

| Topic | Where |
| --- | --- |
| Phase 12 = S12 performance & scale (culling, sort keys, instancing generalization, LOD groups, texture pipeline/sRGB audit) | doc 05 §11 |
| S13 RHI hardening + the stay-GL / Vulkan / RHI decision gate (made on S12 profiler data) | doc 05 §12 + §0 |
| S14 game-engine backlog (animation, physics, editor app, serialization, scripting, positional audio A3, decals) — each with an unlock condition | doc 05 §13 |
| FFT ocean (Tessendorf, the "realistic open ocean" tier) — parked until S12 profiling exists | doc 05 §8 S9.3 |
| Froxel volumetric fog + true raymarched smoke volumes | doc 05 §9 S10.3/S10.4 |
| Volumetric clouds | doc 05 §6 S7.4 |
| Cascaded shadow maps (CSM) + texel snapping | doc 05 §5 S6.4 |
| Snow deformation trails (deferred here — needs a ground actor) | doc 05 §10 S11.1 |
| Underwater caustics/full S9.4, SSR alternate reflections | doc 05 §8 |

---

## Kickoff prompt (paste into each implementation session)

```
Read docs/plans/00-MASTER-ROADMAP.md ("Working agreement" section + the Phase 11 entry), then
docs/plans/10-phase11-frontier-plan.md — §0 execution notes fully, then work order <F#> — and
implement that work order exactly as specified.

Key rules: the shaders the item names are ALREADY WRITTEN in Cosmic/assets/shaders/ — read them
first; they are the uniform-contract truth; implement the C++ that feeds them (if a feature
doesn't show, you forgot a gate uniform). Re-verify any quoted code by content before editing
(grep, don't trust line numbers). No raw gl* outside platform/OpenGL/. Never run git write
commands — the user commits. New engine source files require a cmake re-configure (§0.1 recipe).

Finish by running the item's Acceptance procedure (build via the §0.1 recipe, CosmicTests, the
visual check in CosmicApp --project Frontier, and an Engine3DDemo smoke-run when the item touched
shared engine code), then update the item's Status banner in doc 10 (✅ + date + one line).
Work order for this session: F<#>.
```

Recommended session order: F1 → F2 → F3 → F4 → F5 → F6 → F7 → F8 → F9 → F10 → F11 → F12a →
F12b → F12c → F13 → F14 → F15 → F16 → F17. (F6/F7/F9/F10 are mutually independent after F2 if
parallel branches are ever wanted; content items need everything before them.)
