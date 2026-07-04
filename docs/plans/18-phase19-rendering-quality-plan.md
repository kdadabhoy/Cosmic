# Phase 19 Plan — Rendering Quality Tier 2 (deviation closures)

> **Created 2026-07-04.** Collects every *documented tier deviation* left behind by Phases
> 9–13 into one place, per the 2026-07-04 rule ("live docs contain only unimplemented work;
> every future feature has a phase home"). Each item cites its origin. **This doc is a menu,
> not a march:** items are independent, each has its own unlock, and none blocks Phases 14–18.
> Run an item when its unlock fires or when a quality pass is explicitly wanted.
>
> **Depends on:** nothing outside shipped code (Phase 14 H2 makes the results visible in
> Starforge, but Engine3DDemo/Frontier already exercise these paths).

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules (RendererAPI verbs, BindingPoints, conformance
script). Every item keeps its feature default-off or visually-identical-by-default until an
app opts in (compat gate). Each item lands with an Engine3DDemo toggle (the house acceptance
pattern) and, where sensible, a Frontier world upgrade.

## 1. Work orders (independent; pick by unlock)

### R1 — Cascaded shadow maps *(origin: S6.4 deviation "single 2k map, CSM next")*
3–4 cascades, stable fit (texel snapping), cascade blending, `SceneRendererSettings` grows
cascade count/lambda; `ShadowMap` becomes an array target; PCF preserved. **Unlock:** shadow
swimming/range complaints on any large world (Frontier island already shows the 50 m-radius
limit — camera-follow shadows were the F12 workaround). **Acceptance:** Frontier island sun
shadows stay sharp from 1 m to 1 km with no visible cascade seams; ≥60 fps held; toggle
compares single-map vs CSM. **Status:** ☐

### R2 — Depth prepass + ambient-only SSAO *(origin: S6.5 deviation "whole-image composite")*
Optional depth prepass feeding SSAO so occlusion darkens only ambient/IBL terms (physically
sane) instead of the whole image; prepass reuses the shadow-caster draw path. **Unlock:** SSAO
visibly dirtying lit surfaces in a real scene. **Acceptance:** side-by-side toggle shows
sunlit surfaces unaffected by SSAO; frame cost delta measured in the F3 profiler. **Status:** ☐

### R3 — Progressive (CoD-style) bloom *(origin: S6.6 deviation "Gaussian")*
Dual-filter down/up chain with knee curve; replaces the Gaussian pyramid; identical API.
**Unlock:** bloom shimmer/aliasing complaints or a cinematic pass. **Acceptance:** no
flicker on subpixel emitters (ember stress scene); cost ≤ current at 1080p. **Status:** ☐

### R4 — Froxel volumetrics *(origin: S10.3 "shadow-map raymarch ships, froxel grid is the follow-up")*
3D froxel grid (e.g. 160×90×64) accumulating sun+fog scattering, replacing/augmenting the
god-rays raymarch; height-fog integration. **Unlock:** a scene needs local fog volumes or
light shafts from more than the sun. **Acceptance:** Night Volcano column lights produce
volumetric shafts; ≥60 fps at 1080p. **Status:** ☐

### R5 — FFT ocean (water tier 2) *(origin: S9.3 parked per plan)*
Phillips/JONSWAP spectrum, compute IFFT displacement+normal maps feeding the existing water
shader; Gerstner path stays for lakes. **Unlock:** an app needs open-ocean scale seas (Storm
Ocean's 8-wave stack stops convincing beyond ~1 km views). **Acceptance:** tiling invisible at
4 km viewing distance; buoyancy queries still CPU-exact (spec the CPU mirror or displacement
readback — decide in-order). **Status:** ☐

### R6 — Terrain tessellation + holes *(origin: S8.4 parked)*
GPU tessellation LOD path behind the existing quadtree (or replacing it — measure first), and
terrain holes (mask) for caves/basements. **Unlock:** terrain silhouette quality or a
cave-entrance need (voxel phase may supersede the cave case — check before starting).
**Acceptance:** silhouette pop eliminated at cell-LOD boundaries; SampleHeight parity
maintained (≤1 cm test stays green). **Status:** ☐

### R7 — Particle indirect draw + sorting *(origin: S10.1 "fixed-count quads, no intra-emitter sort")*
GPU compaction + `glDrawArraysIndirect` (via a new RendererAPI verb) so dead particles cost
nothing; optional per-emitter depth sort for correct alpha. **Unlock:** an effects-heavy scene
shows fill-rate/overdraw cost in the profiler. **Acceptance:** 100k-pool emitter at 10% alive
costs ~10% of today's draw; sorted smoke renders without popping. **Status:** ☐

### R8 — Wireframe fill-mode verb + ID-buffer visualize *(origin: E9 deviation)*
`RenderCommand::SetPolygonMode(Fill|Line)` (platform-layer verb) + Starforge view-mode menu
entries (Lit / Wireframe / Entity-ID debug). Small. **Unlock:** immediate (editor QoL) — do
opportunistically. **Acceptance:** all three modes render; conformance script green.
**Status:** ☐

### R9 — Compressed texture pipeline (BCn/KTX2) *(origin: S12.6 "parked w/ unlock")*
Offline compress on import (BC7/BC5/BC4 via a vendored encoder), KTX2 container, loader path;
`.cmeta` gains a compress flag. **Unlock:** VRAM/load-time pressure on a real project (the
S12.6 audit found none at current content scale). **Acceptance:** a 4k-textured scene loads
measurably faster with no visible quality loss at 1080p. **Status:** ☐

### R10 — Projected decals *(origin: doc 05 S14 row, re-annotated "Starforge-era content polish")*
Screen-space PBR decal pass (box-projected) after opaque; `DecalComponent` (reflected).
**Unlock:** content polish on a shipped-app scene (blast marks, posters, road wear).
**Acceptance:** decals wrap terrain+meshes, respect normals, batch to one pass. **Status:** ☐

### R11 — Skybox depth-func verb *(origin: Phase 9 deviation "background-first, LEQUAL verb pending")*
`SetDepthFunc(LEqual|Less)` verb → skybox draws last at far depth instead of first (saves
full-screen shading). Small. **Unlock:** opportunistic with any other sky work (H4 HDRI is a
natural pairing). **Acceptance:** identical image, measured fill-rate win in the profiler.
**Status:** ☐

### R12 — World-system builder registry *(origin: modularity audit G3, `docs/design/modularity-audit.md`)*
The E18 recipes stay the authoring truth; add `RegisterTerrainBuilder/WaterBuilder/
EmitterBuilder(name, buildFn)` consumed by `Scene::SyncWorldSystems`, with each recipe gaining
an `Implementation` string (default names map to today's concrete classes — scenes unchanged).
Enables two implementations coexisting / per-entity selection. **Unlock:** a second
implementation of any world system actually exists (do NOT build speculatively — the audit's
recommendation). **Acceptance:** default scenes byte-identical; a registered test builder is
selected per-entity via the recipe field; headless-tested. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/18-phase19-rendering-quality-plan.md`
> in `C:\dev\Cosmic`. Read §0 and your item's origin citation — the original deviation note
> in the archived plan (docs/plans/archive/) carries context worth reading. New GPU state =
> RendererAPI/RenderCommand verbs; BindingPoints registry; default-off/identical-by-default;
> Engine3DDemo toggle as acceptance; conformance script green; compat gate; roadmap cmake
> recipe; no git writes. Finish with Acceptance + status banner.
