# API Reference — World Systems (Terrain, Water, Particles)

> **STATUS: SKELETON** — to be filled by work order **D12** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/terrain/Terrain.h`, `water/Water.h`,
`water/GerstnerWave.h`, `particles/ParticleSystem.h`, `particles/Presets.h`.

**Read first:** systems explainers [terrain](../systems/terrain.md),
[water](../systems/water.md), [particles](../systems/particles.md);
[`docs/design/water-rendering-notes.md`](../design/water-rendering-notes.md). Usage
exemplars: `Projects/Frontier` (all five worlds), Engine3DDemo "World systems" panel.

## Coverage checklist *(starting point — headers are authoritative)*

**Terrain**
- [ ] Creation desc — size/resolution (**Resolution must be `32·2^k + 1`, e.g. 1025/2049 — pin this gotcha**), heightmap source, `HeightFunction` Source C (F4), layer/splat setup
- [ ] `SampleHeight` / `SampleNormal` — CPU queries **guaranteed to match render triangulation ≤ 1 cm**
- [ ] Render entry points — via `TerrainComponent`/`Scene::OnRender3D` and direct; `RenderDepth` shadow casting (F4); wet band + shore accessors (F4); snow overlay interaction (`SetSnow`)

**Water**
- [ ] `GerstnerWave` — pure wave-math struct (shader==CPU contract), wave parameter fields
- [ ] `Water` creation desc — grid size, wave set (v2: up to 8 waves, whitecaps, flow)
- [ ] `BeginReflection` / reflection-refraction flow — Lengyel oblique clip plane, `BlitCopy` refraction grab, per-frame primary-reflection handoff (F12a)
- [ ] Buoyancy queries — `SampleHeight`-style API for floating objects (Storm Ocean buoy)
- [ ] Underwater — dive detection, depth-graded fog / caustics / god-ray tint hooks (Tonemap pairing; Layer 0 shimmer fix: mipmapped procedural texture + distance fade)

**Particles**
- [ ] `ParticleEmitter` — emitter desc (pool size, SSBO binding 8, spawn rate ring buffer), compute-update + attribute-less billboard draw, soft particles, flipbooks, `StretchByVelocity` (F9), `StepCpu` CPU fallback (unit-tested contract)
- [ ] `RibbonEmitter` — trail API
- [ ] `Presets` — `Snowfall`, `Rain`/`SplashRings`, fire/smoke/embers etc. — enumerate all from `Presets.h`

## Sections to write

1. Entries per checklist, one major section per system. <!-- TODO(D12) -->
2. Each system's "minimum viable setup" example (terrain quad + water plane + one emitter), lifted from Frontier/Engine3DDemo and compile-checked against headers. <!-- TODO(D12) -->

---
*Changelog:*
