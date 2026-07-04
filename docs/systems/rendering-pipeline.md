# Frame Pipeline & Post-Processing — How It Works

> **STATUS: SKELETON** — to be filled by work order **D29** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md). Biggest
> explainer — the work order allows two sessions (pipeline/passes, then lighting theory).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a frame is a *pipeline of passes* — shadows, reflections, the main HDR render
with PBR + image-based lighting, water, particles, then a post chain (SSAO, bloom, god rays,
FXAA, tonemap) that turns raw HDR light into the final image.
**Source:** `Cosmic/src/renderer/SceneRenderer.*`, `PostProcessStack.*`, `EnvironmentMap.h`, `ShadowMap.*`, `CoverageCapture.*` + `Cosmic/assets/shaders/*`
**API Reference:** [../reference/rendering-pipeline.md](../reference/rendering-pipeline.md) · **Design spec:** [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md)

## Section plan

1. **Overview** — "a photograph is developed in stages"; what HDR means and why the engine renders in it (real-light math first, display conversion last). <!-- TODO(D29) -->
2. **Mental model** — diagram **DG-8** (the full pass graph with read/write targets per pass). <!-- TODO(D29) -->
3. **Step-by-step** — one Frontier IslandWorld frame narrated pass by pass, with the GPU-profiler zone names as the section beats (F3 HUD ties the doc to what users can see). <!-- TODO(D29) -->
4. **Technical implementation** — per stage: HDR target formats (S6.1), PBR in one honest page (Cook-Torrance GGX/Smith/Schlick, metallic-roughness — *explained, not just named*), IBL bake (procedural sky → RGBA16F cube, irradiance/prefilter/BRDF LUT as applicable — verify against `EnvironmentMap.h`), camera/lights UBOs, shadow map + PCF + camera-follow, SSAO, Gaussian bloom, god rays (shadow-map raymarch), heat haze distortion field, lens flare (F7), FXAA, ACES tonemap + underwater mode, sky v2 (sun disc/stars/phased moon) + height fog + time-of-day. Reserved sampler-unit contract (`ApplySceneBindings` binds unconditionally — why: unit-0 alias = INVALID_OPERATION on strict drivers). <!-- TODO(D29) -->
5. **Design decisions** — pass order rationale, single shadow map (CSM = follow-up), whole-image SSAO composite deviation, Gaussian vs progressive bloom, RGBA16F-not-RGB16F portability rule. <!-- TODO(D29) -->
6. **Limits & future work** — the documented tier deviations list (doc 05 §5–§9 banners), froxels/FFT parked. <!-- TODO(D29) -->

**Truth sources:** `frame-lifecycle.md` (pass graph + state contract — summarize, don't
fork), `SceneRenderer.cpp` (the real pass sequence), doc 05 §5/§6 banners, the shaders
themselves (uniform names are the contract).
