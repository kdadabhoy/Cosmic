# 3D Renderer — How It Works

> **STATUS: SKELETON** — to be filled by work order **D28** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** `DrawMesh` doesn't draw — it *submits*. The renderer culls against the camera
frustum, sorts by a packed key (shader → material → mesh → depth), collapses identical runs
into instanced draws, and only then touches the GPU.
**Source:** `Cosmic/src/renderer/Renderer3D.*`, `renderer/RenderQueue.h`, `renderer/InstanceSet.*`, `math/Frustum.h`
**API Reference:** [../reference/rendering-3d.md](../reference/rendering-3d.md) · **Design spec:** [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md)

> **Build note (Phase 29):** everything in this document is **3D-configuration only**.
> `renderer/Renderer3D.*`, `InstanceSet.*`, `EnvironmentMap.*`, `ShadowMap.*` and
> `CoverageCapture.*` are excluded from the 2D engine build, as are the 15 3D components that now
> live in `scene/Components3D.h`, the 3D scene half in `scene/Scene3D.cpp` and their reflection
> registrations in `reflect/TypeRegistry3D.cpp`. `SceneRenderer` is **shared** — 2D composites
> through the same HDR → tonemap → overlay spine, with the 3D passes fenced individually. See
> [`build-2d-3d-split.md`](build-2d-3d-split.md).

## Section plan

1. **Overview** — immediate vs deferred submission; why sorting saves the GPU from thrashing (the restaurant-kitchen analogy: cook all burgers together). <!-- TODO(D28) -->
2. **Mental model** — diagram **DG-7** (submit → cull → sort → auto-instance → flush). <!-- TODO(D28) -->
3. **Step-by-step** — 5,000 pine trees from `DrawMesh` calls to ~2 instanced draws; what the `Statistics` counters mean along the way. <!-- TODO(D28) -->
4. **Technical implementation** — S12 series: sort-key packing (`RenderQueue.h`, headless-tested), frustum cull at submit + opt-out, transparency (`Material::SetTransparent` → back-to-front + depth-write-off), auto-instancing preconditions (≥4 run, instancing-shader twin, entityID −1, per-run scratch `InstanceSet`s), **material-read-at-flush semantics + `Material::Clone`** (the breaking change and the migration examples), `LODGroupComponent` switching (casters use lit level), mid-scene `Flush()` state islands. <!-- TODO(D28) -->
5. **Design decisions** — why cull at submit not flush; key layout rationale; engine-owned transparency vs app state juggling. <!-- TODO(D28) -->
6. **Limits & future work** — no intra-emitter/queue-level per-triangle sort; indirect draw later; S14 backlog pointers. <!-- TODO(D28) -->

**Truth sources:** Phase 12 banner (roadmap), `RenderQueue.h` + `tests/test_render_queue.cpp`
(the tested truth), `Renderer3D.cpp` flush path, `frame-lifecycle.md` §queue semantics.
