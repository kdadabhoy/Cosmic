# Modularity Audit — how swappable are the 3D systems? (2026-07-04)

> Requested by the user: *"if I needed/wanted to change the way I render water to a different
> system I could; if I wanted to change the way I render light/how it behaves I could easily
> do that — find any improvements I need to make relating to modularity."*
>
> Method: read the seams (headers + call sites), not the marketing. Verdicts below name the
> exact files that would change for each swap. Actionable gaps are filed as phase work orders
> (docs 13/18) — this document records the analysis and the **swap cookbook**; it is a design
> record like `frame-lifecycle.md`, not a plan doc.
>
> **Resolution log** (findings are never rewritten — resolutions are appended under them):
> **G1** ✅ doc 13 H3 · **G2** ✅ doc 13 H2 · **G5** ✅ doc 13 H4 ·
> **G3 ✅ CLOSED 2026-07-25** by Phase 29 W3's physics backend registry — the first real instance
> of the registry-keyed-factory shape this audit recommended. See §G3 and the physics row in §4.

## 1. Verdict in one paragraph

The engine's *vertical* seams are genuinely strong: all GPU state goes through
`RendererAPI`/`RenderCommand` (enforced by `tests/check_gl_conformance.ps1` in CI), binding
points are a registry (`renderer/BindingPoints.h`), frame orchestration is data-driven
(`SceneRenderer` consumes a `SceneRenderDesc`), world systems are built from **data-only
recipes** (E18: components carry PODs, assets are derived), geometry is GL-free until upload
(`MeshData`), and the second-backend contract is already written down
(`docs/design/frame-lifecycle.md`, S13.2). Swapping an *implementation* is therefore mostly a
bounded, findable change — the audit found **no swap that requires touching more than ~3
files' call sites**. The real weaknesses are *horizontal*: three shading paths disagree about
lighting (the one user-visible bug this audit explains), `EnvironmentComponent` is consumed on
only one of the two render paths, and the concrete factories (`Water::Create`,
`Terrain::Create`, `ParticleEmitter::Create`) are hard-wired at two call sites each — fine for
replacement, awkward for *coexistence* of two implementations.

## 2. Seam inventory (what already protects you)

| Seam | Mechanism | Enforcement |
| --- | --- | --- |
| GPU API isolation | `RendererAPI`/`RenderCommand` verbs; GL only in `platform/OpenGL/` | conformance script + CI step (S13.1) |
| Binding/sampler contracts | `renderer/BindingPoints.h` registry; reserved units assigned unconditionally (`ApplySceneBindings`) | code review rule (doc 05 §0) |
| Frame orchestration | `SceneRenderer::Render(const SceneRenderDesc&)` — a pure desc→frame function; apps own policy | header contract (PRE/POST state restore) |
| Submission | `Renderer3D` sorted queue (cull → sort key → auto-instance) — pure logic in `renderer/RenderQueue.h`, headless-tested | tests |
| World-system authoring | E18 recipes: components store PODs; `BuildTerrainSpec`/`BuildWaterSpec`/`BuildEmitterSpec` map recipe→spec (GL-free, tested) | tests (`test_worldsystems.cpp`) |
| Geometry | `MeshData` + `Build*` (GL-free) → thin `Create*` uploaders | tests (E15/E16) |
| Second backend | `frame-lifecycle.md` = resource rules, state contract, pass graph, queue semantics | written spec; Vulkan gate closed w/ reopen conditions |
| Editor/engine boundary | reflection registry (E1) + generic serializer (E2) — the editor holds no schema | compat gate discipline |
| Sinks/services | null-default `ITelemetrySink`, owner-ticked services (`SceneManager`, `SerialLink` pattern) | precedent |

## 3. Gaps found (each with its filed fix)

### G1 — Lighting is three systems wearing one trenchcoat  → **doc 13 H3**
`Mesh3D.glsl` (legacy Lambert; driven by `SetLightDirection`/`SetAmbient` statics) ignores the
lights UBO that `MeshLit.glsl`/`PBR.glsl` read; `Scene::OnRender3D` gathers ECS lights into
`SetLights` (UBO only). Consequence: point/directional light *entities* do nothing to any
default-colored mesh — the user's "lights aren't a thing" report is this gap, not a bug in the
lights. **Fix (filed):** one lights source of truth (the UBO); the legacy path reads it;
`SetLightDirection/SetAmbient` become writers into the same state. After H3, "change how light
behaves" = edit one UBO block + the BRDF blocks that consume it.

### G2 — EnvironmentComponent has two consumers and one implementation → **doc 13 H2**
`SceneRenderer::ApplyEnvironment` exists (E4) but only desc-driven apps could call it, and
none do; the editor/PlayerLayer path (`Scene::OnRender3D`) never sees the component.
Consequence: sky/fog/post authoring is dead in the editor. **Fix (filed):** one desc-building
helper both surfaces share; after H2 there is exactly one function that decides "how a scene
becomes a frame".

### G3 — Concrete world-system factories: replaceable, not coexistable → **doc 18 R12 (parked)**
`Scene::SyncWorldSystems` calls `Terrain::Create`/`Water::Create`/`ParticleEmitter::Create`
directly; components hold `Ref<Terrain>`/`Ref<Water>`/`Ref<ParticleEmitter>` (concrete).
Swapping the water implementation today = implement the same public surface (`Water::Render`,
buoyancy queries) and change those two call sites — bounded, acceptable. What you *cannot* do
is run two water implementations side by side or select per-entity. **Fix (filed, parked):**
a builder registry keyed on the recipe (`RegisterWaterBuilder(name, fn)`; recipe gains an
`Implementation` string defaulting to "gerstner") — one session of work, but speculative
until a second implementation exists (S14 discipline). The recipe layer already guarantees
scenes won't change shape when it happens.

> **✅ RESOLVED 2026-07-25 — Phase 29 W3 (doc 28 §6), commit `cb4a3c3`.**
>
> The finding above stands exactly as written; what changed is that the shape it prescribed now
> exists in the engine, in **physics** rather than in the world-system triad. The recommendation
> was "a registry-keyed factory, once a second implementation is real". A second implementation
> became real when the user asked to be able to write their own physics for a single app, so the
> pattern was built there first:
>
> - **`physics/PhysicsBackend.h`** — `IPhysicsBackend` (one simulation implementation, expressed
>   purely in `PhysicsTypes.h` vocabulary: glm + PODs, no Jolt, no GL, no entt) plus
>   **`PhysicsBackendRegistry`**: `Register(name, factory)` / `Has` / `Names` / `SetDefault` /
>   `Default` / `Create`. The map is a function-local static, so there is no static-init-order
>   question across the DLL boundary, and built-in registration is an explicit call rather than a
>   file-scope registrar.
> - **Selection is per world**, through `PhysicsSettings::Backend` (empty ⇒ the registry default) —
>   which is precisely the `Implementation`-string-on-the-recipe idea this finding proposed, applied
>   to the settings struct instead of a component.
> - **`PhysicsWorld` did not become abstract.** It stayed concrete and by-value constructible and
>   became a **dispatcher** over the interface — the `RenderCommand` → `RendererAPI` idiom §2 already
>   credits. Its public API did not change by one character, so no call site moved and no gameplay
>   script changed. That is the part worth copying: the seam cost zero churn above it.
> - Two backends ship (`"jolt"`, `"null"`), `COSMIC_WITH_JOLT=OFF` is a supported configuration, and
>   `tests/test_physics_backend.cpp` is a complete third-party backend under 150 lines — driven
>   through `PhysicsWorld → ScenePhysics → ScriptHost` with its own counters, so a silent fallback to
>   Jolt cannot pass.
>
> **What this does and does not settle.** It settles the *pattern* question: the audit's advice was
> right, the shape works, and it costs nothing above the seam. It does **not** implement R12 — the
> world-system builder registry for water/terrain/particles is still parked in
> `FEATURE-MATRIX.md` with its original unlock ("a second implementation must coexist"). What it now
> has is a working in-tree precedent to copy rather than a design to invent. **Also still open:
> coexistence.** Physics selects *one* backend per `PhysicsWorld`; two simulators side by side in one
> scene is not supported, so "replaceable, not coexistable" is resolved only in the replaceability
> direction — which is the direction the requirement actually asked for.
>
> Documented in [`../systems/physics-backends.md`](../systems/physics-backends.md) and
> [`../reference/physics.md`](../reference/physics.md).

### G4 — PostProcessStack is a fixed pipeline with toggles
Pass order (SSAO→bloom→FXAA→tonemap/fog/god-rays/haze/flare) is compiled in; apps toggle but
cannot insert. This is the *right* v1 shape (order is a correctness contract — see
frame-lifecycle.md), and `SceneRenderDesc::DrawTransparent`/`DrawOverlay2D` are the sanctioned
injection points before/after post. **No work filed**; unlock = a real custom-pass need (file
under doc 18 then). The audit's advice: extend by adding a named pass INSIDE PostProcessStack
with a settings toggle, exactly like heat-haze was added — precedent, not plugin API.

### G5 — Sky is procedural-or-nothing → **doc 13 H4 closes the practical gap**
`EnvironmentMap` bakes only the analytic sky; `SkyMode::HDRI` is authorable but unimplemented
(found live: dead enum). H4 adds the equirect→cube source. A full `ISkyProvider` interface
stays unfiled — two sources with a switch is fine; interface it when a third source (e.g.
volumetric sky) actually appears.

### G6 — Shader/material extension is by-convention, not by-registry
Engine shaders load from fixed asset paths; materials bind to "the PBR shader" via
`AssetLibrary::BuildMaterial`; instanced twins register via `Material::SetInstancingShader`.
Convention is documented and consistent; a shader-graph/registry is far-future. **No work
filed** — record only. (If a project needs custom surface shaders: author a Material on a
custom `Shader::Create` program — the path exists today via the material API.)

### G7 — Renderer2D/3D static state
Both are static-singleton renderers (one GL context, one window — true today by design).
`frame-lifecycle.md` §queue-semantics already notes what un-staticking costs when a second
context appears. **No work filed.**

## 4. Swap cookbook (how to change X today, post-Phase-14)

| To swap… | Touch | Untouched |
| --- | --- | --- |
| **Water rendering** | implement the `Water` public surface (Create/Render/SetShoreTerrain/buoyancy queries); the recipe→spec mapper (`scene/WorldSystemRecipes.cpp`) or its R12 registry entry | scenes (`.cscene` recipes), editor panels (reflection UI), SceneRenderer pass order |
| **Terrain rendering** | same pattern: `Terrain` surface (Render/RenderDepth/SampleHeight parity) + mapper | scenes, panels, physics (J7 reads the CPU heightfield accessor — keep it) |
| **Light behavior/BRDF** | after H3: the lights UBO block + the BRDF section of `MeshLit/PBR` (+ `Mesh3D` Lambert) | ECS components, editor UI, scene files |
| **Post effect chain** | add a pass inside `PostProcessStack` + a `SceneRendererSettings` toggle | apps (defaults off), frame order contract |
| **Sky source** | after H4: add a source to `EnvironmentMap` (bake-to-cube contract) | IBL consumers (they read the cube, not the source) |
| **The physics simulator** *(shipped 2026-07-25 — the registry exists; this is the only row that needs no engine edit at all)* | implement `IPhysicsBackend` (`physics/PhysicsBackend.h`) in **your own project**, then two lines in your layer's `OnAttach`: `PhysicsBackendRegistry::Register("mine", factory)` + either `SetDefault("mine")` or `PhysicsSettings::Backend = "mine"`. Honour the four contracts: fixed step, `DrainContactEvents` moves-and-clears, `ThreadCount == 0` ⇒ deterministic (or say you ignore it), `RayHit::EntityId` round-trips `BodyDesc::EntityId` | **the entire engine.** `PhysicsWorld`'s public API, every call site, `ScenePhysics`, the components, the inspector, the serializer, saved scenes, gameplay scripts. A backend that only cares about XY simply ignores Z |
| **The whole graphics backend** | `platform/<NewAPI>/` implementing `RendererAPI` + resource classes, per `frame-lifecycle.md` | everything above the RHI (that's the point of the S13 audit) |

## 5. Recommendations summary

1. Execute doc 13 H2 + H3 first — they close the only *user-visible* modularity failures
   (this audit's G1/G2) and collapse the render-path fork to one function.
2. Keep the recipe layer as the official extension point for world systems; take R12 only
   when a second implementation is real.
3. Resist interface-izing single-implementation seams (sky, post, factories) — the codebase's
   discipline of *documented conventions + enforcement scripts* is currently beating
   speculative abstraction, and `frame-lifecycle.md` already reserves the big swap.
4. Re-run this audit when Phase 18 (voxel) lands — it is the first consumer that renders
   outside the mesh/terrain/water triad and will stress the queue + material conventions.
