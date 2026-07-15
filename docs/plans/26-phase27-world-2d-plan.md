# Phase 27 Plan — World Rendering & 2D Game Parity

> **STATUS 2026-07-14 (UNcommitted) — PHASE CODE-COMPLETE (X1–X7 all ✅).** All seven work
> orders landed in one session. Engine gained only generic, default-off/identical-by-default
> surface: `SkyMode::Physical` (analytic Rayleigh+Mie scattering baked into the IBL cube),
> `EnvironmentComponent` polish fields (AmbientIntensity/Gamma/SunAngularSize/Ambient2D + the
> physical params), particle curl-noise turbulence (compute + `StepCpu` twin on the shared
> `PcgHash`, `ParticleEmitter::CurlNoise`) + local-space bounds, `Light2DComponent` +
> `renderer/Light2DRenderer` (+ `RendererAPI::BlendMode::Multiply`), `UiWorldAnchorComponent` +
> `UiSystem::ProjectToCanvas`, and `SceneRenderer::RenderToTexture` + `UiImageComponent::
> RuntimeTexture`. Starforge gained the sun-angle widget, the Project Settings left-nav, the live
> curl-noise preview, Entity ▸ 2D ▸ Light + the radius-ring gizmo. Build **Debug+Release zero
> warnings**, `CosmicTests` **352/352** (339→352: +5 curl noise, +3 bounds, +3 world-anchor, +1
> Light2D, +1 RTT), GL-conformance clean, compat gate held (every new field default-omits or is
> identity-by-default; no shipped app attaches any new component). Two build-system fixes: `/bigobj`
> on Starforge (StarforgeApp.cpp crossed the COFF section limit) and vendored node-editor `/w`→`/W0`
> (kills a full-rebuild D9025). Remaining = the user's on-GPU acceptance (turbidity sweep, campfire
> darkness scene, nameplate tracking, live minimap) + commit.
>
> **Created 2026-07-11.** Editor-vision phase 6 of 7 (spec of record:
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md)
> §7.2–§7.5, §11, §12). The engine-side additions the reference screenshots demand beyond the
> existing plans: a physical-atmosphere sky option, particle turbulence with live preview,
> and the three genuine gaps behind the 2D survival-game screenshot (2D lights, world-anchored
> UI, render-to-texture). The 2D *authoring* stack (ortho mode, tilemaps, painter, game view)
> stays doc 16's U3/U4/U7 — run those before/with X5–X7 consumers.
>
> **Depends on:** doc 16 U3/U4 for the 2D samples that exercise X5/X6; nothing else hard.
> Items independent unless marked.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate — every item default-off or
visually-identical-by-default (the doc 18 discipline), each with an Engine3DDemo (or 2D
sample) toggle. Shader/CPU twins stay in lockstep where both exist (the S10 `StepCpu`
invariant). New GPU state via RendererAPI verbs + `BindingPoints.h`. No git writes.

## 1. Work orders

### X1 — Physical atmosphere sky (`SkyMode::Physical`) *(gap §7.2)*
**Files:** engine `renderer/EnvironmentMap` + sky shaders (extend the EnvSky/SkyDetail family
with a Preetham/Hosek-style analytic scattering path), `scene/Components.h`
(`EnvironmentComponent`: `Turbidity`, `RayleighScale`, `MieScale`, `MieG` behind the new mode),
IBL bake follows the visual sky (lighting matches).
**Spec:** existing Procedural/Detailed/HDRI modes byte-identical; Physical is a fourth option.
Pairs naturally with doc 18 R11 (sky depth verb) if scheduled together. **Acceptance:**
turbidity sweep visibly hazes the horizon; sun elevation drives correct color ramps
dawn/noon/dusk; conformance green; Engine3DDemo toggle. **Status:** ✅ 2026-07-14 —
`SkyMode::Physical` (enum + `Turbidity`/`RayleighScale`/`MieScale`/`MieG` on
`EnvironmentComponent`, reflected). Analytic Rayleigh+Mie single-scattering (16×8 ray-march)
added to **`EnvSky.glsl`** behind `u_SkyMode` (default 0 = the shipped gradient,
byte-identical) — it bakes into the SAME env cube the skybox draws and irradiance/prefilter
convolve, so IBL matches the visible sky *by construction*. `EnvironmentMap::SetPhysicalSky`
(dirty-gated) + `SceneRenderer::ApplyEnvironment` wire it (enabled only for `Physical`; every
other mode's bake untouched). Engine3DDemo "Physical atmosphere (X1)" toggle + turbidity/
Rayleigh/Mie/G sliders; editor exposes it free via reflection. Debug build green, 339/339,
conformance clean. On-GPU turbidity-sweep + dawn/noon/dusk ramp = user acceptance.

### X2 — Environment polish bundle: sun-angle widget, ambient, gamma, sun size, settings nav *(gap §7.1/§7.3/§7.4/§7.5)*
**Files:** engine: `AmbientIntensity` (scales IBL/ambient term via `ApplySceneBindings`
uniforms), exposed gamma (`Tonemap.glsl` hardcodes 2.2 — becomes a uniform default 2.2),
`SunAngularSize` consumed by the Detailed/Physical sun disc; `EnvironmentComponent` fields
(defaults keep output byte-identical). Starforge: `EnvironmentPanel` gains an
Elevation/Azimuth paired widget (spherical ⇄ `SunDirection` conversion) beside the raw vec3;
Project Settings reorganized into a left-nav (General · Window · Packaging · Physics
defaults) — consolidation only, no new state.
**Acceptance:** angle widget round-trips the vector exactly (undo intact); gamma/ambient
defaults produce identical frames; settings dialog reachable as before. **Status:** ✅ 2026-07-14 —
five fields on `EnvironmentComponent` (`AmbientIntensity`/`Gamma`/`SunAngularSize`, all reflected;
+ X1's four), defaults byte-identical. **Gamma:** `Tonemap.glsl`'s hardcoded `1.0/2.2` →
`u_Gamma` (default 2.2 = the same folded constant) via `SceneRendererSettings::Gamma` +
`PostProcessStack::SetGamma`. **Ambient:** `Renderer3D::SetAmbientIntensity` →
`u_AmbientIntensity` bound in `ApplySceneBindings` (the funnel every PBR draw uses;
`s_Data` default 1.0 so direct-`Renderer3D` callers are safe), `ambient *= u_AmbientIntensity`
in PBR/PBRInstanced/PBRSkinned/Terrain (`×1.0` = bit-identical). **SunAngularSize:** a crisp
limb-darkened disc sized by it in the X1 physical sky (deg-diameter → rad-radius); Detailed mode
consumes it via the existing `SkyDetailDesc::SunAngularRadius` for apps that build a detailed sky.
**Sun-angle widget:** Elevation/Azimuth pair under the raw vec3 in `EnvironmentPanel` — angles
derived from `SunDirection` every frame (never written back), the vector written ONLY while a
slider drags (so display never perturbs it), magnitude preserved; the conversion is an exact
algebraic inverse (vec3→angles→vec3 = identity to float precision), one undoable `FieldEdit`
through the raw-vec3 commit path. **Settings:** Project Settings reorganized into a left-nav
(General · Window · Packaging · Physics defaults) — every prior control preserved, no new state.
Engine3DDemo gains Gamma / Ambient-intensity / Sun-size sliders. Debug green, 339/339,
conformance clean.

### X3 — Particle curl-noise turbulence *(gap §11.1)*
**Files:** engine `particles/ParticleSystem` (spawn/step compute + the unit-tested `StepCpu`
mirror gain a curl-noise force: `NoiseEnabled`, `NoiseStrength`, `NoiseFrequency`,
`NoiseOctaves` on `ParticleEmitterSpec` + the reflected recipe in `scene/Components.h`;
curl of a 3D value-noise field — shared constants so CPU==GPU), `.cemitter` forward-load.
**Acceptance:** headless: `StepCpu` with noise matches itself across runs (determinism) and
matches the GPU path within tolerance on a readback test; ember cone visibly swirls with
strength; old presets load unchanged. **Status:** ✅ 2026-07-14 —
`NoiseEnabled`/`NoiseStrength`/`NoiseFrequency`/`NoiseOctaves` on `ParticleEmitterSpec` +
the reflected `ParticleEmitterComponent` recipe (mapped in `BuildEmitterSpec` + hashed in
`EmitterRecipeSignature`). A divergence-free **curl of a 3D value-noise field** added
IDENTICALLY to `StepCpu` (C++) and `ParticleUpdate.glsl` (compute) — value noise built on the
**already-shared `PcgHash`** (no perm table to upload), the four magic constants + epsilon
duplicated verbatim with a cross-referencing comment, octaves clamped 1..4 on both sides. The
field is exposed as `ParticleEmitter::CurlNoise` (reused by X4's preview) so the CPU preview,
the CPU sim, and the GPU sim are one function. Off = the term is skipped ⇒ byte-identical.
New `tests/test_particle_noise.cpp` (5 cases): `StepCpu`+noise is **bit-deterministic** across
runs; disabled ignores strength (compat); enabled perturbs; `CurlNoise` deterministic/finite/
spatially-varying + octave clamp. `.cemitter` forward-loads (generic reflected serializer;
missing keys default off). Live `ParticleEmitter::SetTurbulence` verb + Engine3DDemo "Ember curl
noise (X3)" toggle. Deviation: chose PcgHash value noise over `math/Noise`'s perm-table Value3D
(the table doesn't port to GLSL); the GPU-readback tolerance match is by-construction (shared
source) + the recorded on-GPU swirl. Debug green, **344/344**, conformance clean.

### X4 — Particle authoring extras: live noise preview + bounds *(gap §11.2/§11.3; dep X3)*
**Files:** Starforge `panels/WorldSystemsPanel.cpp` (128² CPU-rendered curl-magnitude
thumbnail, debounced on param change — `math/Noise` + a small `Texture2D` upload); engine
optional `BoundsExtents` kill/wrap clamp on the emitter (default off).
**Acceptance:** preview updates within a debounce tick of edits; bounds visibly contain a
volume emitter; compat (bounds off = identical). **Status:** ✅ 2026-07-14 —
**Preview:** `WorldSystemsPanel::DrawNoisePreview` CPU-renders a 128² |curl| slice of
`ParticleEmitter::CurlNoise` (the *exact* field the sim uses, so preview == sim) into a
`Texture2D` on a dark→ember→white heat ramp, drawn under the emitter fields; debounced 0.12 s
after the last freq/octaves edit (rebuilt on first show, freed when the module is off).
**Bounds:** `BoundsExtents`/`BoundsWrap` on `ParticleEmitterSpec` + the reflected recipe
(mapped/hashed), applied IDENTICALLY in `StepCpu` and `ParticleUpdate.glsl` — kill (age→life) or
wrap (per-axis modulo about the emitter origin) past the box; all-zero extents skip the block ⇒
byte-identical. 3 new tests in `test_particle_noise.cpp`: off = inert (wrap flag ignored, free
flight), kill dies in place, wrap stays inside + alive. Debug green, **347/347**, conformance
clean. On-GPU: preview tracks edits; a volume emitter is visibly contained = user acceptance.

### X5 — 2D lighting *(gap §12.1)*
**Files:** engine NEW `Light2DComponent{ Color, Radius, Intensity, Falloff }` (reflected) +
`Ambient2D` color (environment or canvas-level), `renderer/Renderer2D` composite: additive
radial lights into a half-res R11G11B10F buffer, multiplied over the 2D scene output (verb-
gated; the PostFx-active path composites in LDR resolve).
**Spec:** default: no lights + white ambient ⇒ byte-identical 2D output (compat). Normal-
mapped 2D lights explicitly out of scope v1. Starforge: Entity ▸ 2D ▸ Light; gizmo radius ring
(the point-light glyph pattern).
**Acceptance:** the campfire scene: darkness with a warm radius + falloff matching the
reference feel; 100 lights ≥60 fps at 1080p; 2D samples without lights render identical
before/after. **Status:** ✅ 2026-07-14 — reflected `Light2DComponent{Color,Radius,Intensity,
Falloff,Enabled}` + `EnvironmentComponent::Ambient2D` (white default). New
`renderer/Light2DRenderer` service: accumulates each active light as an additive radial quad
(VBO-free `Light2D.glsl`, world-rect from `gl_VertexID`) into a half-res HDR buffer cleared to
Ambient2D, then MULTIPLIES it over the bound target (new `RendererAPI::BlendMode::Multiply` =
`GL_DST_COLOR,GL_ZERO`; `BlitCopy.glsl` upsamples). `Scene::OnRender2DLights` (raw-transform XY,
Enabled/active gates) called right after `OnRenderSprites` in PlayerLayer + Starforge; **no
lights + white ambient early-returns before any GL call ⇒ byte-identical**. Editor: Entity ▸ 2D
▸ Light + a flat radius RING gizmo (point-light idiom). `Light2DRenderer::Shutdown` in
`Renderer::Shutdown`. New test: Light2D defaults + serializer round-trip. Deviations: **RGBA16F**
light buffer (conformance-clean, LINEAR, negligible half-res cost) not R11G11B10F; composite in
the **HDR transparent phase** (multiplicative blit) not a separate LDR resolve (darkening
tonemaps naturally; no 2D sample uses bloom); **/bigobj** on Starforge (StarforgeApp.cpp crossed
the COFF section limit) + vendored node-editor `/w`→`/W0` (kills a full-rebuild D9025). Debug
**zero warnings**, **348/348**, conformance clean. Campfire darkness + 100-light perf = user acceptance.

### X6 — World-anchored UI *(gap §12.2)*
**Files:** engine `scene/ui/UiComponents.h` (NEW
`UiWorldAnchorComponent{ TargetEntity(UUID), WorldOffset, ScreenOffset }`),
`scene/ui/UiSystem` (`ResolveRect` projects the anchor through the active camera into canvas
space before layout; off-screen/behind-camera → hidden).
**Spec:** works for 2D and 3D cameras (nameplates, health bars, interaction prompts); pure
math headless-tested (the U1 pattern). **Acceptance:** headless: projected rect matches
hand-computed positions incl. behind-camera hide; on-GPU: a nameplate tracks a moving entity
at 60 fps without jitter. **Status:** ✅ 2026-07-14 — reflected
`UiWorldAnchorComponent{TargetEntity(UUID/EntityRef),WorldOffset,ScreenOffset,HideWhenOffscreen}`.
New pure `UiSystem::ProjectToCanvas(worldPos, viewProj, canvasRect)` (clip.w<=0 ⇒ false =
behind-camera; NDC→top-left/+y-down canvas). `VisitUi` projects the tracked world point (target
world-transform + WorldOffset) into a zero-size parent origin (+ScreenOffset) that the
RectTransform offsets size the box around; behind-camera / off-screen hides the element AND its
subtree. Camera VP threaded as an optional `const glm::mat4*` through
CollectElements/Update/HitTest/Render (default nullptr = the unchanged parent-relative layout,
so existing callers are byte-identical); PlayerLayer + Starforge pass the active camera VP.
Works for 2D (ortho) + 3D (perspective) VPs. 3 new tests in `test_ui_rects.cpp`: projection
mapping (center/up/right), behind-camera hide, and a full CollectElements nameplate that tracks
then hides when its target moves behind the camera. Debug **zero warnings**, **351/351**,
conformance clean. On-GPU 60 fps nameplate tracking = user acceptance.

### X7 — Render-to-texture verb (+ minimap building block) *(gap §12.3)*
**Files:** engine `renderer/SceneRenderer` (`RenderToTexture(scene, SceneRenderDesc, Ref<FrameBuffer>)`
public verb — the offscreen plumbing exists, this stabilizes it; state-restore contract),
`scene/ui/UiComponents.h` (`UiImageComponent` gains a runtime `Ref<Texture2D>` slot beside
its path), script access to set it.
**Spec:** engine ships the verb; minimap/fog-of-war logic stays app-side (a mask the game
updates). **Acceptance:** an ortho top-down RTT drawn in a HUD `UiImage` updates live during
Play; main viewport renders identically after the offscreen pass (screenshot compare — the
A4 contract); headless-safe no-op without GL. **Status:** ✅ 2026-07-14 —
`SceneRenderer::RenderToTexture(desc, Ref<FrameBuffer> target)`: captures the caller's bound FBO,
sizes the post stack to the target, runs the normal `Render()` (env/sky/shadows/post all apply,
composites into the target), then RE-BINDS the caller's FBO — the A4 state-restore contract, so
the main viewport is untouched; uninitialized / null-target ⇒ safe no-op. `UiImageComponent::
RuntimeTexture` (runtime-only `Ref<Texture2D>`) wins over the path-loaded image in
`UiSystem::Render` — the injection point a script/app sets each frame to the RTT result. Minimap/
fog-of-war logic stays app-side (engine ships only the generic verb + slot; scripts set the slot
via the public member, C++-first). New headless test: RenderToTexture no-op when uninitialized /
null. Debug **zero warnings**, **352/352**, conformance clean. On-GPU live minimap +
screenshot-identical main viewport = user acceptance.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/26-phase27-world-2d-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md`. Default-off/identical-by-default with a demo
> toggle; CPU/GPU twins stay in lockstep; new GPU state via RendererAPI verbs +
> BindingPoints; conformance script green; compat gate; roadmap cmake recipe; no git writes.
> Finish with Acceptance + status banner.
