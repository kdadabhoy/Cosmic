# Phase 27 Plan — World Rendering & 2D Game Parity

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
dawn/noon/dusk; conformance green; Engine3DDemo toggle. **Status:** ☐

### X2 — Environment polish bundle: sun-angle widget, ambient, gamma, sun size, settings nav *(gap §7.1/§7.3/§7.4/§7.5)*
**Files:** engine: `AmbientIntensity` (scales IBL/ambient term via `ApplySceneBindings`
uniforms), exposed gamma (`Tonemap.glsl` hardcodes 2.2 — becomes a uniform default 2.2),
`SunAngularSize` consumed by the Detailed/Physical sun disc; `EnvironmentComponent` fields
(defaults keep output byte-identical). Starforge: `EnvironmentPanel` gains an
Elevation/Azimuth paired widget (spherical ⇄ `SunDirection` conversion) beside the raw vec3;
Project Settings reorganized into a left-nav (General · Window · Packaging · Physics
defaults) — consolidation only, no new state.
**Acceptance:** angle widget round-trips the vector exactly (undo intact); gamma/ambient
defaults produce identical frames; settings dialog reachable as before. **Status:** ☐

### X3 — Particle curl-noise turbulence *(gap §11.1)*
**Files:** engine `particles/ParticleSystem` (spawn/step compute + the unit-tested `StepCpu`
mirror gain a curl-noise force: `NoiseEnabled`, `NoiseStrength`, `NoiseFrequency`,
`NoiseOctaves` on `ParticleEmitterSpec` + the reflected recipe in `scene/Components.h`;
curl of a 3D value-noise field — shared constants so CPU==GPU), `.cemitter` forward-load.
**Acceptance:** headless: `StepCpu` with noise matches itself across runs (determinism) and
matches the GPU path within tolerance on a readback test; ember cone visibly swirls with
strength; old presets load unchanged. **Status:** ☐

### X4 — Particle authoring extras: live noise preview + bounds *(gap §11.2/§11.3; dep X3)*
**Files:** Starforge `panels/WorldSystemsPanel.cpp` (128² CPU-rendered curl-magnitude
thumbnail, debounced on param change — `math/Noise` + a small `Texture2D` upload); engine
optional `BoundsExtents` kill/wrap clamp on the emitter (default off).
**Acceptance:** preview updates within a debounce tick of edits; bounds visibly contain a
volume emitter; compat (bounds off = identical). **Status:** ☐

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
before/after. **Status:** ☐

### X6 — World-anchored UI *(gap §12.2)*
**Files:** engine `scene/ui/UiComponents.h` (NEW
`UiWorldAnchorComponent{ TargetEntity(UUID), WorldOffset, ScreenOffset }`),
`scene/ui/UiSystem` (`ResolveRect` projects the anchor through the active camera into canvas
space before layout; off-screen/behind-camera → hidden).
**Spec:** works for 2D and 3D cameras (nameplates, health bars, interaction prompts); pure
math headless-tested (the U1 pattern). **Acceptance:** headless: projected rect matches
hand-computed positions incl. behind-camera hide; on-GPU: a nameplate tracks a moving entity
at 60 fps without jitter. **Status:** ☐

### X7 — Render-to-texture verb (+ minimap building block) *(gap §12.3)*
**Files:** engine `renderer/SceneRenderer` (`RenderToTexture(scene, SceneRenderDesc, Ref<FrameBuffer>)`
public verb — the offscreen plumbing exists, this stabilizes it; state-restore contract),
`scene/ui/UiComponents.h` (`UiImageComponent` gains a runtime `Ref<Texture2D>` slot beside
its path), script access to set it.
**Spec:** engine ships the verb; minimap/fog-of-war logic stays app-side (a mask the game
updates). **Acceptance:** an ortho top-down RTT drawn in a HUD `UiImage` updates live during
Play; main viewport renders identically after the offscreen pass (screenshot compare — the
A4 contract); headless-safe no-op without GL. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/26-phase27-world-2d-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md`. Default-off/identical-by-default with a demo
> toggle; CPU/GPU twins stay in lockstep; new GPU state via RendererAPI verbs +
> BindingPoints; conformance script green; compat gate; roadmap cmake recipe; no git writes.
> Finish with Acceptance + status banner.
