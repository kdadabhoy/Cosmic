# Water rendering — notes, fixes, and limits (2026-07-03)

> Engineering note for the Cosmic water system (Gerstner Tier 1, `Cosmic/src/water/`,
> `assets/shaders/Water.glsl`) after the "Subnautica-style water" pass. It records what the
> distance "pixelation" was, the surface + dive work layered on top, the tunable knobs, and what is
> deliberately **deferred** (with where it lives on the roadmap). Written because the fly-over of
> Frontier Island surfaced the shimmer and raised a fair strategic question about GLAD/Vulkan.

## The shimmer ("pixelation") was an aliasing bug, not a tier limit

Mid-to-horizon salt-and-pepper on the ocean was **minification aliasing**, fixable in OpenGL 4.5:

- **Primary:** the two procedural detail **normal maps had no mipmaps** — `Texture2D::Create(w,h)`
  built a single `GL_LINEAR` level, so past ~200 m each pixel sampled dozens of unfiltered texels.
- **Secondary:** the animated **sun glint + sparkle never faded with distance**, so even a
  correctly-filtered normal fed a tight specular that twinkled sub-pixel (FXAA can't fix shading
  shimmer).

## What shipped (all OpenGL 4.5 core, generic engine changes)

**Layer 0 — foundation (committed):**
- Opt-in mipmaps for procedural textures: `Texture2D::Create(w, h, bool mipmapped = false)` (mirrors
  the existing `TextureCube` `Mipmapped` path; `SetData` regenerates the chain). Default-off — every
  existing caller is byte-identical.
- `Water::MakeDetailNormalMap` creates the detail maps mipmapped → trilinear filtering kills the
  salt-and-pepper.
- `Water.glsl` distance fade: `detailFade = 1 - smoothstep(120, 2500, camDistXZ)` relaxes the detail
  normals and sparkle toward the smooth Gerstner normal at range, so far water reads as a clean
  Fresnel reflection of the sky. No new uniforms (uses the camera already in the shader).

**Layer 1 — clear surface (app policy, `IslandWorld`):** ocean depth palette retuned to tropical
turquoise shallows → deep blue (`ShallowColor` / `DeepColor` / `DepthFadeDistance ≈ 18 m`) so sand
and terrain read through shallow water; the lake gets an alpine-teal variant.

**Layer 2 — the dive (generic engine; app sets the palette + enables it):**
- **Depth-graded underwater fog** (`Tonemap.glsl` underwater block + `PostProcessStack::
  SetUnderwaterGrading`): density scales up and the fog color blends `UnderwaterColor →
  UnderwaterDeepColor` as the *camera* descends past `UnderwaterDepthReference` m — clear shallows,
  murky deep. A `smoothstep` waterline ramp removes the pop when crossing the surface.
- **Screen-space seafloor caustics** (`PostProcessStack::SetUnderwaterCaustics` + `SetTime`): the
  tonemap reconstructs world position from depth and multiplies an animated procedural caustic web
  onto submerged geometry (gated to `world.y < waterline`, faded with depth + view distance).
  Chosen over editing the already-complex `Terrain.glsl` (terrain is opaque-in-HDR before the post
  pass, so one screen-space pass covers all underwater surfaces).
- **God-ray shafts underwater**: the existing S10.3 shafts are tinted by the water medium when the
  camera is below the waterline (they read as light through water, not white sun). Shafts are
  strongest near shadow-casting geometry (the volcano, terrain) — open water shows only a glow.
- **Water surface seen from below** (`Water.cpp` cull `None` + a `Water.glsl` `dot(N,V) < 0` branch):
  a Snell's-window of refracted sky + sun toward the zenith, transitioning to total-internal-
  reflection of the depths at grazing angles. **First pass — the most art-directed piece; expect to
  tune it** (window brightness, TIR color, ripple strength) against real dives.

**Loading screen (app, `Projects/Frontier`):** the ~2049² island terrain (~7 s of pure CPU) now
builds on a `JobSystem` worker; the main thread keeps pumping frames and draws a Detroit-style
animated ring spinner (`common/LoadingScreen.h`, over `WorkspaceLayer::BeginViewportOverlay`) until
the build completes + a few warm frames hide the first-frame shader-compile hitch. `World::IsLoading()`
is the generic hook. The job captures a `shared_ptr` result + a **copy** of the island params, so a
detach mid-load is safe (no wait, no use-after-free).

## Tuning knobs (live, in the World Settings panel)

Underwater fog density, deep-tint color, "deep depth" (grading distance), caustic strength + scale,
god-ray power, exposure, and the aerial-fog density — all adjustable in-app so the subjective look is
dialed in without rebuilds. The Subnautica feel is intentionally left to this live pass.

## Deferred (documented, not done)

- **Anisotropic filtering** — the single biggest extra win for grazing-angle horizon water, but
  `GL_TEXTURE_MAX_ANISOTROPY` is not GL 4.5 core; it needs `GL_ARB/EXT_texture_filter_anisotropic`,
  and GLAD was regenerated (S4.0) **extension-free**. Reaching it = regenerate GLAD with the ext +
  a runtime availability check. → **S12.6 texture pipeline.**
- **Photoreal open ocean** — Tessendorf **FFT** (spectral waves) is the "realistic open ocean" tier.
  → **S9.3, parked** until S12 profiling exists.
- **Reflection/refraction FBO mipmapping** — the planar-reflection target is `GL_LINEAR`/unmipped
  (minor soft shimmer under distortion). Low priority.

## Why not move off GLAD / go Vulkan for this

- **GLAD is only a function-pointer loader**, not a renderer — irrelevant to render quality. The
  shimmer was a missing `glGenerateMipmap`, identical under any loader. The lone GLAD wrinkle
  (anisotropy constant undefined, because it was generated extension-free) is a config regen, not a
  reason to replace GLAD.
- **Vulkan is an API/backend swap, not a quality lever** — mipmaps, anisotropy, FFT, TAA all work in
  GL 4.5; a Vulkan port would reproduce this same shimmer if it skipped mipmaps. Its real wins are
  CPU draw-call throughput, explicit control, and platforms GL can't reach (macOS/mobile).
- The engine already confines every `gl*` to `platform/OpenGL/` behind `RendererAPI`/`RenderCommand`,
  which keeps a future Vulkan backend cheap. The stay-GL vs go-Vulkan decision is the **doc 05 S13
  "Vulkan gate", made on S12 profiler data or a platform need** — not on quality symptoms like this.
  **Recommendation: stay on GL 4.5; revisit at S13.**
