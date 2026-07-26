# World Systems — Guide

**What this covers:** the three large-scale world systems — heightmap **terrain** (including the
`32·2^k + 1` resolution rule, the CPU height queries, and the async-build/loading-screen pattern),
**water** (Gerstner surfaces, the planar-reflection handoff, shore awareness, buoyancy queries and
the underwater medium), and **GPU particles** (presets first, then custom emitters, curl-noise
turbulence, bounds and ribbon trails) — plus the **E18 recipe model** that is how all three are
authored today: the component carries plain data, the runtime asset is derived from it.
**Source of truth:** `Cosmic/src/terrain/Terrain.{h,cpp}`, `water/Water.{h,cpp}`,
`water/Presets.h`, `water/GerstnerWave.h`, `particles/ParticleSystem.{h,cpp}`,
`particles/Presets.h`, `scene/WorldSystemRecipes.{h,cpp}`, `scene/Components3D.h`,
`scene/Scene3D.cpp`, `renderer/SceneRenderer.cpp`, `tests/test_worldsystems.cpp`,
`tests/test_phase10_world.cpp`, `tests/test_presets.cpp`, `tests/render/render_3d.cpp`
**API Reference:** [../reference/world-systems.md](../reference/world-systems.md)
*(skeleton — D12)* · **How it works:** [../systems/terrain.md](../systems/terrain.md) ·
[../systems/water.md](../systems/water.md) · [../systems/particles.md](../systems/particles.md)
*(all three skeletons — D30/D31)*
**Configuration:** **3D only.** `terrain/`, `water/` and `particles/` are filtered out of the 2D
engine build and their includes sit behind `#ifndef COSMIC_2D_ONLY` — as do
`TerrainComponent`, `WaterComponent` and `ParticleEmitterComponent`, which live in
`scene/Components3D.h`. Naming any of them in a `COSMIC_2D_ONLY` tree is a compile error, by design;
see [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md). `scene/WorldSystemRecipes.h`
and `water/Presets.h` currently have **no row in the reference manifest**, so this chapter is the
client-facing source for both.

All three systems follow the same two rules, and knowing them up front saves a lot of confusion.
**First: the component is not the asset.** A `TerrainComponent` is ~25 floats and strings; the
`Ref<Terrain>` it produces is a megabyte-scale heightfield. The scene file stores the former and
rebuilds the latter — that is the E18 recipe model, and it is why a `.cscene` with a 4 km island in
it is still a few kilobytes of JSON. **Second: nothing here is placed by the entity's
`TransformComponent`, except particles.** Terrain is world geometry positioned by its own spec;
water is positioned by `Center` and `SurfaceHeight`; only emitters read the entity's world
transform.

**Exemplars in the tree.** `Projects/Frontier/src/worlds/IslandWorld.cpp` is the everything-example
— a 4 km composed island with an ocean, an alpine lake, eight emitters, instanced forests and a
day/night cycle, built asynchronously behind a loading overlay. `Projects/Engine3DDemo` drives all
three by hand at small scale with a toggle per feature (its *World systems (Phase 10 / S8-S10)*
panel section). `Projects/ForgeIsle/scenes/Island.cscene` is the recipe route: terrain and ocean
authored as JSON, no code at all. The render goldens in `tests/render/render_3d.cpp` are the
smallest complete working setups for each.

---

## Quick start — the recipe route

If you are working in a scene, you never call `Terrain::Create` or `Water::Create`. You add a
component, set `UseRecipe`, and the scene builds the asset:

```cpp
// Terrain: a 256 m procedural island, snow above 12 m, edges sinking to sea level.
Cosmic::Entity ground = scene->CreateEntity("Island");
{
    auto& t = ground.AddComponent<Cosmic::TerrainComponent>();
    t.UseRecipe   = true;          // <- without this, nothing is ever built
    t.WorldSize   = 256.0f;
    t.Resolution  = 257;           // 32*2^3 + 1
    t.HeightScale = 26.0f;
    t.BaseHeight  = -6.0f;
    t.Frequency   = 2.5f;
    t.EdgeFalloff = 0.65f;
    t.SnowHeight  = 12.0f;
}
ground.AddComponent<Cosmic::TerrainColliderComponent>();   // walkable ground

// Water: an ocean plane at y = 0, seeded from the Ocean preset.
Cosmic::Entity sea = scene->CreateEntity("Ocean");
{
    auto& w = sea.AddComponent<Cosmic::WaterComponent>();
    w.UseRecipe     = true;
    w.Preset        = Cosmic::WaterPreset::Ocean;
    w.Center        = { 0.0f, 0.0f };
    w.Extent        = { 1600.0f, 1600.0f };
    w.SurfaceHeight = 0.0f;
}

// Particles: the default recipe is already a warm campfire ember cone.
Cosmic::Entity fire = scene->CreateEntity("Campfire");
fire.GetComponent<Cosmic::TransformComponent>().Position = { 34.0f, 8.2f, 22.0f };
fire.AddComponent<Cosmic::ParticleEmitterComponent>().UseRecipe = true;
```

That is the whole setup. `Scene::SyncWorldSystems` — run at the top of `Scene::OnRender3D` and
`Scene::BuildRenderDesc` — turns each recipe into its asset, and `BuildRenderDesc` hands the results
to `SceneRenderer` (see [`lighting-and-environment.md`](lighting-and-environment.md)). Save the
scene and only the recipe fields are written; the assets rebuild on load.

In the editor the same thing is three buttons: **Entity ▸ World ▸ Terrain / Water / Particle
Emitter**, then the **World Systems** panel to tune. Water and particles apply live; terrain has an
explicit **Regenerate Terrain** button because the build is expensive.

### `UseRecipe` is the switch between two worlds

| `UseRecipe` | Who owns the asset | When it rebuilds |
| --- | --- | --- |
| `true` | the scene (`SyncWorldSystems`) | terrain: **once**, when the asset is null *and* never built. Water/particles: whenever the recipe's parameter hash changes |
| `false` (default) | your code — assign `TerrainAsset` / `WaterAsset` / `Emitter` directly | never; the scene does not touch it |

The `false` case is the compatibility gate: Frontier assigns its assets in code and is never
disturbed by the recipe machinery. If you assign an asset yourself, leave `UseRecipe` off or the
scene will overwrite it.

Change detection is a parameter **signature hash** — `TerrainRecipeSignature`,
`WaterRecipeSignature`, `EmitterRecipeSignature` — not a dirty flag, so it survives serialization
round-trips and undo. `UseRecipe` itself is excluded from the hash (toggling it does not count as a
change).

---

## Terrain

A CPU heightfield (the source of truth for queries) plus a chunked-quadtree LOD renderer that draws
one shared 32×32-quad patch mesh per visible node, displaced in the vertex shader from a packed
height+normal texture. Skirts hide the LOD cracks. Four splat layers are blended automatically by
height and slope, with triplanar projection on steep faces.

### The resolution rule

> **`Resolution` must be `32·2^k + 1`.** `Terrain::Create` validates `(Resolution − 1)` as a
> multiple of 32 *and* a power-of-two multiple of it (`Terrain.cpp:38-47`), and **returns `nullptr`**
> otherwise, logging `Terrain: bad spec — Resolution must be 32*2^k + 1 (got N)`. The rule comes
> straight from the renderer: every quadtree node draws the same 32×32-quad patch, so a node at
> depth *d* samples the heightfield with stride `texels / 32`, and that only divides evenly for
> those values.

Valid: **33, 65, 129, 257, 513, 1025, 2049, 4097, …** (Frontier uses 2049 for its 4 km island; the
render golden uses 129.) `Terrain.h:89`'s comment says `(64 * 2^k) + 1`, which is the same set
except that it excludes 33; the error message and the validation are the authority.

Recipe-authored terrain never hits the error, because `BuildTerrainSpec` runs `Resolution` through
`ClampTerrainResolution`, which **snaps to the nearest valid value in `[65, 1025]`**
(`WorldSystemRecipes.cpp:35-49`): `100 → 129`, `500 → 513`, `40 → 65`, `9000 → 1025`. 2049 and above
are deliberately excluded from the editor range — 4 M+ samples stalls interactive authoring — so a
terrain that large is a code-driven build, not a recipe. **The clamp is silent**: type 400 in the
Inspector and you get 513 with no message.

Cost scales as `Resolution²` for the heightfield and normals, so 1025 is 16× the build of 257.

### What the recipe can and cannot express

`BuildTerrainSpec` maps the shape parameters, the four layer colours, and the snow band
(`SnowHeight`/`SnowBlend` → `Material.HighHeight`/`HighBlend`). It leaves everything else at
`TerrainSpecification` defaults — which means a recipe-authored terrain:

- **is always centred on the world origin.** `Origin` is not a recipe field and stays `{0, 0}`. The
  entity's `TransformComponent` is *not* applied (`Components3D.h:337-340`) — terrain is world
  geometry. To move it, build the spec in code.
- uses the default `LodDistanceFactor` 2.5 and `SkirtDepth` 2.0 m.
- uses the default per-layer `Tiling`, the default slope threshold (`SlopeRockThreshold` 0.72), the
  default low/sand band, and **no wet band** (`WetDarken` 0 — the shoreline-darkening effect
  Frontier uses is code-only).
- cannot supply a `HeightFunction`. That is source C, and it wins over both the heightmap image and
  the fBm — it is how `IslandWorld` composes a volcano, a snow range, a lake basin, a beach shelf and
  a carved river into one field.

`HeightmapPath` and the four splat-texture paths *are* recipe fields, and `ResolveTerrainSpecAssets`
resolves them (VFS path → filesystem path for the heightmap, `AssetLibrary::GetTexture` for the
albedos) — **on the main thread only**, which is why the build is split in two.

### Height sources

| Source | Wins when | Notes |
| --- | --- | --- |
| `HeightFunction` | set (code only) | `f(u, v)` over `[0,1]²`, return clamped to `[0,1]`, sampled at the same texel centres as fBm. Must be deterministic |
| `HeightmapPath` | function unset, path non-empty | Grayscale image; `stbi_load_16` widens 8-bit so **16-bit precision is preserved**. Bilinearly resampled onto the grid. Failure logs and `Create` returns `nullptr` |
| Procedural fBm | neither | `Seed`, `Octaves`, `Frequency` (periods across the terrain), `Lacunarity`, `Gain`, plus `EdgeFalloff` — a radial smoothstep down to zero that turns the field into an island |

All three normalize to `[0,1]`, then world height is `BaseHeight + sample × HeightScale`.

### Querying the surface

The CPU heightfield is the ground truth for gameplay, physics and placement, and it is available
**without a GL context** — `Terrain::Create` is CPU-only, GPU resources are created lazily on the
first `Render`. That is what makes the async build below possible and what makes terrain testable
headless.

```cpp
float  y = terrain->SampleHeight(x, z);    // outside the extent: BaseHeight
glm::vec3 n = terrain->SampleNormal(x, z); // outside the extent: +Y
bool  in = terrain->Contains(x, z);
float lo = terrain->GetMinHeight(), hi = terrain->GetMaxHeight();   // world space
uint32_t patches = terrain->GetLastDrawnNodeCount();               // LOD debug HUD
```

`SampleHeight` interpolates on the **same triangle split the renderer draws** (diagonal toward
+x+z), so the query matches the full-detail rendered surface — `tests/test_phase10_world.cpp` pins
that to within 1 cm. Seat your content with it:

```cpp
auto groundAt = [&](float x, float z) { return terrain ? terrain->SampleHeight(x, z) : 0.0f; };
entity.GetComponent<Cosmic::TransformComponent>().Position = { fx, groundAt(fx, fz), fz };
```

Starforge's own *World Systems* sample scene builds its terrain locally at author time purely to do
this placement maths, then throws it away and saves only the recipe (`StarforgeApp.cpp:3313-3325`).

### Physics ground

Add a `TerrainColliderComponent` alongside and the physics session builds a Jolt heightfield from
the same samples. One caveat worth knowing: **the heightfield build drops the far +X/+Z edge row**,
because Jolt rounds its sample count up to a multiple of two and terrain resolutions are always odd.
Keep gameplay off the last cell. See [`physics.md`](physics.md) *(D57)*.

### Building a large terrain without freezing the frame

A 2049² terrain is a few seconds of pure CPU. Since `Terrain::Create` is GL-free, run it on a
JobSystem worker and keep pumping frames. `IslandWorld` is the reference implementation and the
shape is worth copying exactly:

```cpp
// 1) A result block owned by shared_ptr so the job stays valid across a detach.
struct LoadResult
{
    Cosmic::Ref<Cosmic::Terrain> Terrain;
    Cosmic::Ref<Cosmic::Water>   Ocean;
    std::atomic<bool>            Ready{ false };
};
std::shared_ptr<LoadResult> m_Load;

void OnAttach() override
{
    m_Load = std::make_shared<LoadResult>();
    m_RevealFrames = 0;
    auto load = m_Load;                 // the job captures the shared_ptr, never `this`
    IslandParams params = m_Island;     // and a COPY of the parameters
    Cosmic::JobSystem::Get().Submit([load, params]()
    {
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution     = 2049;                       // (32 * 64) + 1
        tspec.WorldSize      = 4096.0f;
        tspec.HeightScale    = 900.0f;
        tspec.BaseHeight     = -80.0f;
        tspec.HeightFunction = [params](float u, float v) { return IslandHeight(params, u, v); };
        load->Terrain = Cosmic::Terrain::Create(tspec);    // CPU-only: safe off the main thread
        load->Ready.store(true, std::memory_order_release);
    });
}

// 2) Adopt it on the main thread once the flag flips.
void OnUpdate(WorldContext& ctx) override
{
    if (!m_Terrain && m_Load && m_Load->Ready.load(std::memory_order_acquire))
        m_Terrain = m_Load->Terrain;
    if (!m_Terrain)
        return;                          // still building — the overlay covers the viewport
    // ... first frame with a terrain: build the GL-dependent content here ...
    if (m_RevealFrames < 3) ++m_RevealFrames;
}

// 3) Stay "loading" for a few rendered frames past the build.
bool IsLoading() const override
{
    const bool cpuReady = m_Load && m_Load->Ready.load(std::memory_order_acquire);
    return !cpuReady || m_RevealFrames < 3;
}
```

Four details that are not obvious:

- **The job must not capture `this`.** A detach mid-load would leave it writing into a dead object.
  Capture the `shared_ptr` and a copy of the parameters; `OnDetach` just does `m_Load.reset()` and
  the job frees the block when it finishes.
- **Only the CPU build goes off-thread.** Anything touching GL — splat textures, meshes, emitters,
  `AssetLibrary` — happens on the main thread the frame the terrain is adopted. Starforge splits it
  the same way: `BuildTerrainSpec` + `ResolveTerrainSpecAssets` on the main thread, then
  `Terrain::Create` on a worker (`WorldSystemsPanel.cpp:55-73`).
- **`RevealFrames` hides the shader-compile hitch.** GPU resources are lazy, so the *first* rendered
  frame pays for the patch mesh, the height texture, the terrain shader and the IBL bake. Holding
  the overlay up ~3 frames past `Ready` hides all of it.
- **The overlay is ImGui, drawn into the viewport window**, so it animates while the worker runs:

```cpp
void OnImGuiRender() override
{
    if (!world.IsLoading())
        return;
    auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
    if (ws && ws->BeginViewportOverlay())
        LoadingScreen::Draw(Cosmic::Application::Get().GetViewportPos(),
                            Cosmic::Application::Get().GetViewportSize(),
                            static_cast<float>(ImGui::GetTime()),
                            "GENERATING WORLD", world.GetInfo().Name);
    if (ws)
        ws->EndViewportOverlay();
}
```

`Projects/Frontier/src/common/LoadingScreen.h` is a header-only ImGui draw-list spinner — copy it,
it needs no engine GPU resources.

> **This is not the same thing as `SceneManager`'s async load.** That one is a *fade*, not a thread:
> the loader runs on the main thread in a single `OnUpdate` and `Progress()` reports transition
> progress, never bytes. See [`scenes-and-serialization.md`](scenes-and-serialization.md). For heavy
> world content, the JobSystem pattern above is what you want.

### Drawing terrain by hand

If you are not going through `SceneRenderer`, `Terrain::Render` must be called inside a
`Renderer3D` scene — its vertex stage reads the camera UBO:

```cpp
Cosmic::Renderer3D::BeginScene(camera);
    m_Terrain->Render(camera.GetPosition());       // quadtree LOD around this point
Cosmic::Renderer3D::EndScene();
```

Pass the **real** camera position even in a mirrored reflection pass, so the reflected tessellation
matches the main view and there is no seam at the waterline — which is exactly what
`SceneRenderer::PassReflection` does (`SceneRenderer.cpp:556-557`). `RenderDepth(lightViewProj,
cameraPos)` is the shadow/coverage twin and walks the same LOD cut.

---

## Water

A Gerstner-displaced grid with dual scrolling detail normals, depth-fade absorption colour,
screen-space refraction, a real planar reflection, Fresnel, sun glint and shoreline foam. The wave
set is shared between the GPU displacement and the CPU queries, so `SampleHeight` matches the
rendered surface — that is the buoyancy contract.

### Minimal setup

```cpp
Cosmic::WaterSpecification spec;
spec.Extent        = { 256.0f, 256.0f };
spec.SurfaceHeight = -5.0f;
Cosmic::Ref<Cosmic::Water> water = Cosmic::Water::Create(spec);   // CPU-only, headless-safe
```

An empty `Waves` list gets a plausible default three-wave swell. `Create` returns `nullptr` on a
degenerate spec (a zero extent, for instance). **At most 8 waves are kept**; a ninth is dropped.

Or through the recipe, seeded from a preset:

```cpp
auto& w = entity.AddComponent<Cosmic::WaterComponent>();
w.UseRecipe  = true;
w.Preset     = Cosmic::WaterPreset::Ocean;   // Lake = 0, Ocean = 1, Storm = 2
w.Amplitude  = 1.0f;                         // multiplies the preset wave amplitudes
w.Choppiness = 1.0f;                         // multiplies the preset steepness, clamped to 1
w.Extent     = { 1600.0f, 1600.0f };
w.SurfaceHeight = 0.0f;
w.WhitecapStrength = 0.55f;
```

| Preset | Waves | Character |
| --- | --- | --- |
| `Lake` | 3 short, 2–6 m, amplitude ≤ 0.03 | near-mirror, `SpecularPower` 300 |
| `Ocean` | 4, up to 22 m / amplitude 0.32 | gentle swell, whitecaps 0.25, shore-aware |
| `Storm` | 5, up to 30 m / amplitude 0.75 | tall steep crossing swell, whitecaps 0.6 |

The recipe overrides `Center`, `Extent`, `SurfaceHeight`, `GridResolution`, the two colours and the
caustic/whitecap/sparkle strengths on top of the preset; everything else — `DepthFadeDistance`,
`FoamDepth`, refraction and reflection strength, detail tiling and speed, `SpecularPower`,
`ShoreDepthRange`, `ReflectionResolution` — comes from the preset and is **not** exposed as a recipe
field. For those, build a `WaterSpecification` in code.

> **Water is placed by `Center` and `SurfaceHeight`, not by the entity's transform.** Moving a water
> entity in the viewport moves nothing.

### The reflection handoff

Water is a multi-pass effect. `SceneRenderer` owns the sequencing and you get it for free by
putting the body in `desc.WaterBodies`:

```cpp
desc.WaterBodies.clear();
if (m_Ocean) desc.WaterBodies.push_back(m_Ocean.get());   // app submits FAR -> NEAR
if (m_Lake)  desc.WaterBodies.push_back(m_Lake.get());
desc.PrimaryReflectionWater = nearLake ? (int)desc.WaterBodies.size() - 1 : 0;
desc.Settings.WaterReflections = true;                     // on by default
```

- **Exactly one body gets a real planar reflection per frame** — the one at
  `PrimaryReflectionWater`. Everything else falls back to the IBL cube, which for a distant ocean is
  indistinguishable and for a mirror-calm lake at close range is not.
- **`PrimaryReflectionWater = -1`** (or an out-of-range index) means IBL-only for all of them. So
  does `WaterReflections = false`.
- **`Scene::BuildRenderDesc` picks the nearest body to the camera** automatically and sets `-1` when
  there is no water (`Scene3D.cpp:814-836`). Frontier picks by hand because "nearest" is the wrong
  answer when you are standing on a beach with the lake technically closer.
- **Order matters for the *draw*, not the reflection.** The transparent pass renders
  `desc.WaterBodies` in list order, each grabbing the scene colour for refraction, so submit far to
  near.

The reflection itself mirrors the camera about the water plane with an oblique near plane clipping
at the surface (Lengyel), re-renders sky → terrain → `DrawOpaque` into the body's own reflection
target, then restores. Reflection resolution is per-body (`ReflectionResolution`, default 512;
Frontier's ocean uses 1024).

Driving it by hand is the same three steps, and `Water.h:22-35` documents the exact contract:

```cpp
glm::mat4 reflVP; glm::vec3 reflCam;
if (water->BeginReflection(view, proj, camPos, reflVP, reflCam))
{
    Cosmic::Renderer3D::BeginScene(reflVP, reflCam);
    // ... redraw sky + terrain + key meshes ...
    Cosmic::Renderer3D::EndScene();          // MUST flush before EndReflection unbinds
    water->EndReflection();
    Cosmic::RenderCommand::SetViewport(0, 0, w, h);   // the caller re-asserts its viewport
}
// ... opaque scene into the HDR target ...
water->Render(camPos, time, viewProj, sceneColorID, sceneDepthID, w, h);
```

`Render` must be called **while the target that owns `sceneColorID`/`sceneDepthID` is bound** — it
copies the scene colour into its own refraction target (a shader cannot sample the target it writes)
and then re-binds yours.

### Shore awareness

Bind a terrain and waves flatten in shallow water, breakers foam at the shoreline, and the beach
reads correctly instead of the surface slicing through the sand:

```cpp
water->SetShoreTerrain(terrain);   // null clears the gate (open water)
```

The water shader `texelFetch`es the terrain's packed height texture each `Render`, so **the terrain
must have built its GPU resources first** — which it has, because terrain draws before water in the
pass order. `ShoreDepthRange` (in the spec, not the recipe) is the water depth over which waves
regain full amplitude. `Scene::BuildRenderDesc` and `Scene::OnRenderWorldFX` both wire the *first*
terrain in the scene as the shore source for every water body automatically.

### Buoyancy and the underwater medium

```cpp
const float  surfaceY = water->SampleHeight(x, z, timeSeconds);
const glm::vec3 n     = water->SampleNormal(x, z, timeSeconds);
```

Pure CPU, matching the rendered surface, and it converges through the Gerstner horizontal
displacement (the query lands on itself — `tests/test_phase10_world.cpp:238`). Engine3DDemo's
bobbing orange box is a four-line demonstration.

Going *under* the surface is a post-chain effect, not a water feature — it lives on
`SceneRendererSettings` and is documented in
[`lighting-and-environment.md`](lighting-and-environment.md). The one-line summary:

```cpp
desc.Settings.Underwater  = m_UnderwaterEnabled && (camPos.y < kOceanY + 1.0f);
desc.Settings.UnderwaterY = kOceanY;
```

Water does not set that flag for you. It is your decision each frame, with a small margin so
crossing the surface is not an instant pop.

---

## Particles

A GPU particle system: a std430 SSBO pool updated by a compute shader and drawn attribute-less as
camera-facing billboards — six vertices per particle from `gl_VertexID`, no vertex buffer. Emission
is a ring buffer, so there are no free lists and no readbacks; dead slots render as zero-area
triangles.

### Start from a preset

`particles/Presets.h` is header-only pure functions returning a tuned `ParticleEmitterSpec`. Reach
for one first — they are what the showcase scenes use and they are unit-tested
(`tests/test_presets.cpp`):

| Preset | Signature | What it is |
| --- | --- | --- |
| `SoftPuff()` | — | one soft alpha puff that grows and fades — steam vents |
| `Snowfall(boxExtents, rate)` | box volume | slow fluttering flakes with lateral drift; 8192 particles |
| `Embers(rate)` | — | additive warm sparks rising in a 20° cone; lava, fire pits |
| `SmokeColumn(rate)` | — | big grey puffs rising and expanding, bent by wind |
| `Mist(boxExtents)` | box volume | huge slow low-alpha puffs — ground mist, fog banks |
| `Rain(boxExtents, rate)` | box volume | near-terminal-velocity fall, velocity-stretched into streaks; 16384 particles |
| `SplashRings(rate)` | — | tiny-life expanding rings for a water surface; `Burst()` these |

```cpp
auto spec = Cosmic::Presets::Embers(320.0f);
spec.SoftFadeDistance = 0.2f;                        // tune on top
m_Embers = Cosmic::ParticleEmitter::Create(spec);
m_Embers->SetTransform(glm::translate(glm::mat4(1.0f), firePit));
```

Presets set placement to nothing — `SetTransform` is always yours — and leave the texture null,
which gets you a procedural soft puff sheet.

### A custom emitter

```cpp
Cosmic::ParticleEmitterSpec spec;
spec.MaxParticles = 2048;
spec.SpawnRate    = 55.0f;                          // particles / second
spec.Shape        = Cosmic::EmitterShape::Cone;     // Point | Sphere | Cone | Box
spec.ShapeRadius  = 0.35f;                          // Sphere / Cone base
spec.ConeAngleDeg = 14.0f;                          // cone axis = the transform's +Y
spec.SpeedMin = 0.8f;  spec.SpeedMax = 1.6f;
spec.LifeMin  = 2.5f;  spec.LifeMax  = 4.0f;
spec.Gravity  = { 0.0f, 0.55f, 0.0f };              // positive = buoyant
spec.Drag     = 0.35f;                              // 1/s velocity damping
spec.Wind     = { 0.35f, 0.0f, 0.12f };             // constant acceleration
spec.SizeStart = 0.45f; spec.SizeEnd = 1.9f;        // linear over lifetime
spec.ColorStart = { 0.62f, 0.62f, 0.64f, 0.42f };
spec.ColorEnd   = { 0.72f, 0.72f, 0.75f, 0.0f };
spec.Blend = Cosmic::ParticleBlend::Alpha;          // Alpha | Additive
spec.Space = Cosmic::ParticleSpace::World;          // World | Local
spec.FlipbookTilesX = 4; spec.FlipbookTilesY = 4;
spec.FlipbookFps    = 9.0f;                         // 0 = static random tile per particle
spec.FlipbookBlend  = true;
spec.SoftFadeDistance = 0.6f;                       // 0 disables soft particles
auto emitter = Cosmic::ParticleEmitter::Create(spec);
```

Per frame, when driving by hand:

```cpp
emitter->SetTransform(placement);
emitter->Update(deltaTime, timeSeconds);            // compute dispatch, or the CPU step
// ... after the opaque scene, inside a Renderer3D scene ...
emitter->Render(view, sceneDepthID, invViewProj);
emitter->Burst(8);                                  // extra spawns on the next Update
```

Through `SceneRenderer`, only `Render` is the engine's job: **you still own `Update`**. That is why
`Scene::BuildRenderDesc` calls `SetTransform` + `Update` on every emitter before pushing the pointer
into `desc.Emitters` (`Scene3D.cpp:838-848`) — the renderer draws, nothing more.

Facts worth having:

- **HDR colour feeds bloom.** `ColorStart = { 4.0f, 1.6f, 0.35f, 1.0f }` with `Additive` blending is
  how Engine3DDemo's embers glow — the values are above 1 on purpose and the post chain does the
  rest. Turn `Settings.Bloom` on or they just look bright.
- **Soft particles need scene depth**, which means an HDR target, which means going through
  `SceneRenderer` (or binding one yourself). `SoftFadeDistance = 0` opts out.
- **`ParticleSpace::Local` simulates in the emitter's frame**, so the whole plume moves with the
  entity; `World` spawns in the emitter's frame and then lets particles live in world space.
- **Box emission has no initial direction** — that is why `Rain` sets `SpeedMin = SpeedMax = 0` and
  lets gravity plus drag drive a uniform fall.
- **The draw is a fixed `MaxParticles` quads** regardless of how many are alive (a documented tier
  deviation; indirect draw + GPU compaction is the follow-up). Size the pool to what you need.
- **There is no intra-emitter depth sort.** Additive blending does not care; large alpha puffs
  sometimes do.
- **`GpuSimulation = false`** runs an identical ring-buffer step on the CPU — for tiny emitters, for
  GL-less tests, and for the render golden, which uses it precisely because the CPU twin is bit-exact
  run to run.

### Curl-noise turbulence

A divergence-free curl of a 3D value-noise field, added as a swirling acceleration. Off by default;
identical on the compute path and the CPU mirror (shared `PcgHash`), so a headless test or an editor
preview matches the GPU sim exactly.

```cpp
spec.NoiseEnabled   = true;
spec.NoiseStrength  = 3.0f;     // acceleration scale
spec.NoiseFrequency = 0.4f;     // spatial frequency, world units^-1
spec.NoiseOctaves   = 2;        // clamped 1..4 at build

emitter->SetTurbulence(true, 6.0f, 0.4f, 2);   // live-tunable; takes effect next Update
```

`ParticleEmitter::CurlNoise(pos, frequency, octaves)` is public and static — the exact function the
sim integrates. The editor's World Systems panel renders a 128² slice of it as a live preview
thumbnail; you can use it for the same purpose, or in tests.

### Bounds

Half-extents about the emitter origin; an axis with extent ≤ 0 is unbounded, and all-zero (the
default) disables the clamp entirely.

```cpp
spec.BoundsExtents = { 20.0f, 0.0f, 20.0f };   // bound X and Z, leave Y free
spec.BoundsWrap    = true;                     // wrap to the far side; false kills instead
```

Wrapping is how you keep a snow or rain volume populated around a moving camera without respawning
the pool: track the camera with `SetTransform` and let particles wrap.

### Ribbon trails

The CPU sibling — feed it points, it builds a camera-facing, age-faded triangle strip each frame.
Rocket exhaust, wingtip vortices, tyre tracks.

```cpp
Cosmic::RibbonSpec rs;
rs.MaxPoints     = 96;
rs.Width         = 0.14f;
rs.PointLifetime = 1.4f;
rs.MinDistance   = 0.05f;      // points closer than this are skipped
rs.ColorHead     = { 0.45f, 0.9f, 1.0f, 0.85f };
rs.ColorTail     = { 0.45f, 0.9f, 1.0f, 0.0f };
rs.Additive      = true;
m_Ribbon = Cosmic::RibbonEmitter::Create(rs);
...
m_Ribbon->AddPoint(tailWorldPos, timeSeconds);
m_Ribbon->Update(timeSeconds);                    // expire old points
m_Ribbon->Render(view, timeSeconds);              // inside a Renderer3D scene
```

Ribbons go in `desc.Ribbons` and are drawn in the transparent pass after water and particles.

### `.cemitter` presets

The emitter recipe's reflected fields **are** the `.cemitter` file format — it round-trips through
the generic reflected-struct serializer, so there is no separate schema. The editor's World Systems
panel writes and reads `project://emitters/<name>.cemitter`:

```cpp
const uint32_t tid = entt::type_hash<Cosmic::ParticleEmitterComponent>::value();
void* comp = /* the component instance */;
Cosmic::SceneSerializer::SaveReflectedToFile(tid, comp, Cosmic::FileSystem::Resolve(path));
Cosmic::SceneSerializer::LoadReflectedFromFile(tid, comp, Cosmic::FileSystem::Resolve(path));
```

Loading one sets `UseRecipe = true` and clears `Emitter` so `SyncWorldSystems` rebuilds. Like every
other reflected asset it is pretty-printed with `dump(2)`, and enum fields accept either the integer
or the option name on read. See
[`scenes-and-serialization.md`](scenes-and-serialization.md).

---

## Common patterns

**Author with recipes, escape to code when you hit a limit.** The recipe covers the common case
completely; the moment you need a `HeightFunction`, a non-origin terrain, a custom wave set or a
water optic the recipe does not expose, build the spec in code, assign the asset, and leave
`UseRecipe` false. The two routes coexist in one scene without interfering.

**Seat everything with `SampleHeight`.** Build the terrain first — it is CPU-only, so you can do it
at author time, in a test, or on a worker — then place props, colliders, spawn points and cameras
against the surface. Content buried inside a hill is the single most common terrain bug and this is
the fix.

**One terrain is the shore source.** The first `TerrainComponent` with a built asset becomes both
`desc.TerrainSystem` and the shore-attenuation source for every water body. If you need two
terrains, only the first participates.

**Keep the pointers alive.** `desc.TerrainSystem`, `desc.WaterBodies`, `desc.Emitters`,
`desc.Ribbons` and `desc.DistortionEmitters` are all raw pointers into storage you own. Hold the
`Ref`s as members; the desc is a per-frame view.

**Release GPU-owning assets while the context is live.** Terrain, water and emitters all own lazily
created GPU resources. Reset them in `OnDetach`, before the context goes away — see
[`project-anatomy.md`](project-anatomy.md).

**Heat haze wants its emitters in two lists.** A distortion emitter usually also draws normally:
push it into `desc.Emitters` *and* `desc.DistortionEmitters`. The haze field is only written by the
second list.

---

## Pitfalls

**"`Terrain::Create` returned null."** `Resolution` is not `32·2^k + 1`, or `WorldSize <= 0`, or the
heightmap file could not be read. All three log; check the console — and remember Release builds
have no console, so read the log file.

**"I typed 400 for Resolution and got 513."** Recipe terrain snaps silently to the nearest valid
value in `[65, 1025]`.

**"My terrain won't move when I drag the entity."** It never will. Terrain is world geometry placed
by its spec, and the recipe cannot set `Origin` — it is always centred on the world origin.

**"My water won't move either."** `Center` and `SurfaceHeight`, not the transform.

**"Nothing was built."** `UseRecipe` is false. It is `false` by default on all three components,
because that is the compatibility gate for code-assigned assets.

**"I changed a terrain parameter and nothing happened."** By design — terrain auto-builds **once**.
Use the editor's **Regenerate Terrain** button, or in code clear `TerrainAsset` and reset
`BuiltSignature` to 0. Water and particles do rebuild on any change.

**"Unticking `Enabled` on a water body or emitter doesn't hide it."** Only on the `SceneRenderer`
path. `SyncWorldSystems` and `OnRenderWorldFX` honour `Enabled` and `IsActiveInHierarchy`, but
`Scene::BuildRenderDesc` gates on `!wc.WaterAsset` / `!pc.Emitter` only (`Scene3D.cpp:819`, `:840`)
— and that is the path the editor viewport and `PlayerLayer` use. An already-built body keeps
rendering. Clear the asset to actually remove it.

**"Only one of my two lakes reflects."** By design — one planar reflection per frame, chosen by
`PrimaryReflectionWater`. The rest use the IBL cube.

**"The water surface is black / the refraction is wrong."** `Water::Render` must be called with the
target that owns the colour and depth attachments you passed still bound, and inside a `Renderer3D`
scene. Through `SceneRenderer` this is automatic.

**"Waves cut straight through the beach."** No shore terrain bound. `SetShoreTerrain`, or let
`BuildRenderDesc` do it.

**"Particles are frozen."** Nothing called `Update`. `SceneRenderer` only draws; `BuildRenderDesc`
updates for you, hand-driven emitters do not.

**"Particles are invisible."** In order: is the emitter in `desc.Emitters`? Is `SpawnRate` non-zero
(or did you `Burst`)? Does `ColorEnd.a` reach 0 almost immediately? Is `MaxParticles` large enough
for `SpawnRate × LifeMax`? A pool that small recycles slots before they are seen.

**"Additive embers look dull."** `Settings.Bloom` is off. HDR particle colours need the post chain
to bloom.

**"Soft particles have no effect."** No scene depth — you are not rendering into a target with a
depth attachment, or `SoftFadeDistance` is 0.

**"My snowfall box empties as the camera moves."** Track the camera with `SetTransform` and turn on
`BoundsWrap`, or the particles are left behind in world space.

---

## See also

- [`lighting-and-environment.md`](lighting-and-environment.md) — `SceneRenderer`, the pass graph,
  the underwater medium, the snow/coverage overlay, and the post chain these systems feed
- [`rendering-3d.md`](rendering-3d.md) — `Renderer3D`, instancing (how Frontier scatters 5,000
  pines over terrain), frustum culling and the statistics counters
- [`entities-and-components.md`](entities-and-components.md) — `TerrainComponent`,
  `WaterComponent`, `ParticleEmitterComponent` field-by-field, with units and defaults
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — how recipes are stored, the
  reflected-struct serializer behind `.cemitter`, and `SceneManager`'s (different) async load
- [`physics.md`](physics.md) *(D57)* — `TerrainColliderComponent` and the heightfield build
- [`voxels.md`](voxels.md) *(D56)* — the other large-scale world system, on the same
  "params, not meshes" principle
- [`../reference/world-systems.md`](../reference/world-systems.md) *(skeleton — D12)* ·
  [`../systems/terrain.md`](../systems/terrain.md) · [`../systems/water.md`](../systems/water.md) ·
  [`../systems/particles.md`](../systems/particles.md) *(skeletons — D30/D31)*
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — why none of this exists in
  a 2D build
