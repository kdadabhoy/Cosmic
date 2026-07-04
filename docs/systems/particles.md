# Particles — How It Works

> **STATUS: SKELETON** — to be filled by work order **D31** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** tens of thousands of smoke puffs, embers, raindrops and snowflakes live
entirely on the GPU — a compute shader moves them each frame, a ring buffer recycles dead
ones, and the vertex shader invents billboard corners with no vertex buffer at all.
**Source:** `Cosmic/src/particles/ParticleSystem.*`, `particles/Presets.h`, `Cosmic/assets/shaders/Particle*.glsl`, `Ribbon.glsl`
**API Reference:** [../reference/world-systems.md](../reference/world-systems.md)

## Section plan

1. **Overview** — why CPU particles cap out and GPU ones don't (the data never crosses the bus). <!-- TODO(D31) -->
2. **Mental model** — the pool as a ferris wheel (ring-buffer spawn), attribute-less billboards ("the shader computes the four corners from the particle's position + camera"). <!-- TODO(D31) -->
3. **Step-by-step** — one emitter frame: spawn window write → compute update dispatch → memory barrier → billboard draw; where soft-particles and flipbooks plug in. <!-- TODO(D31) -->
4. **Technical implementation** — std430 pool layout on SSBO binding 8, ring-buffer spawn math, fixed-count draw (why: no readback; indirect-draw later), `StretchByVelocity` (F9 rain), soft-particle depth fade, flipbook indexing, `RibbonEmitter` trails, `StepCpu` CPU fallback (the unit-tested contract — and why a CPU twin exists: headless tests), `Presets.h` catalog, god rays/heat haze relationship (they're PostFx, fed by particles' look — clarify boundaries). <!-- TODO(D31) -->
5. **Design decisions** — no intra-emitter sorting (documented deviation; additive blends hide it), fixed-count draw trade-off. <!-- TODO(D31) -->
6. **Limits & future work** — compaction/indirect draw, froxel volumetrics (parked). <!-- TODO(D31) -->

**Truth sources:** `ParticleSystem.cpp/h`, `ParticleUpdate.glsl` + `ParticleBillboards.glsl`,
doc 05 §9 banner, `tests/test_phase10_world.cpp` StepCpu tests.
