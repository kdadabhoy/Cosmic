# Terrain — How It Works

> **STATUS: SKELETON** — to be filled by work order **D30** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** one shared patch mesh, redrawn many times at different scales by a quadtree
that keeps triangle density near the camera — heights come from a texture, materials paint
themselves by slope and altitude, and the CPU can query the exact rendered height anywhere.
**Source:** `Cosmic/src/terrain/Terrain.*`, `Cosmic/assets/shaders/Terrain*.glsl`
**API Reference:** [../reference/world-systems.md](../reference/world-systems.md) · **Guide exemplar:** `Projects/Frontier` (2049² island, F11 heightfield composer)

## Section plan

1. **Overview** — why you can't just make one giant mesh (memory + perspective waste); LOD as "paper map detail levels". <!-- TODO(D30) -->
2. **Mental model** — quadtree sketch + the single-skirted-patch trick (one mesh, many transforms). <!-- TODO(D30) -->
3. **Step-by-step** — camera moves → quadtree cut selection → per-node patch draw with `texelFetch` height lookup → auto-splat shading. <!-- TODO(D30) -->
4. **Technical implementation** — packed height+normal texture, skirts (crack hiding), 4-layer auto-splat + triplanar (what triplanar fixes and costs), `SampleHeight/SampleNormal` matching the render triangulation ≤ 1 cm (why that guarantee matters: buoys, boids, scatter placement), `HeightFunction` Source C (F4), `RenderDepth` shadow casting via shared `CollectCut/DrawCut`, wet band + shore accessors, snow overlay (`SetSnow` @ TexUnit 12), **Resolution = `32·2^k + 1` constraint (769 → 1025 gotcha)**, async build (JobSystem + `IsLoading`, 2049² ≈ 7 s one-time composer cost). <!-- TODO(D30) -->
5. **Design decisions** — heightfield vs mesh terrain; quadtree-on-one-patch vs chunked meshes; app-side `HeightfieldComposer` (engine ships noise verbs, apps compose islands — the design rule in action). <!-- TODO(D30) -->
6. **Limits & future work** — tessellation/holes parked (S8.4). <!-- TODO(D30) -->

**Truth sources:** `Terrain.cpp/h`, doc 05 §7 banner, `tests/test_phase10_world.cpp`
(query-exactness test), Frontier `common/HeightfieldComposer.h` (app-side pattern).
