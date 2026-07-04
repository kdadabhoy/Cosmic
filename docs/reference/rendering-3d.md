# API Reference — 3D Rendering

> **STATUS: SKELETON** — to be filled by work order **D10** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md). This is one of
> the two largest chapters (with the README 3D sections) — the work order allows splitting
> into two sessions.
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/renderer/Renderer3D.h`, `graphics/Mesh.h`,
`graphics/Model.h`, `renderer/InstanceSet.h`, `math/Frustum.h` (+ the *documented semantics*
of `renderer/RenderQueue.h` even though clients don't include it directly).

**Read first:** systems explainer [rendering-3d](../systems/rendering-3d.md);
[`docs/design/frame-lifecycle.md`](../design/frame-lifecycle.md) (pass/state contract);
Phase 12 banner in [`docs/plans/00-MASTER-ROADMAP.md`](../plans/00-MASTER-ROADMAP.md) for the
queue semantics that MUST be reflected in every mutating entry.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Renderer3D` scene control — `BeginScene` (camera UBO upload), `EndScene`, **`Flush` (mid-scene state islands)**, frustum parameter / culling opt-out verb
- [ ] `Renderer3D` submission — `DrawMesh`, `DrawModel`, instanced variants (`DrawMeshInstanced`, caster variants), entity-ID parameter contract (−1 = not pickable, −1 required for auto-instancing)
- [ ] `Renderer3D` environment hooks — `SetIBL`, `SetShadow`, `SetSnow`, `ApplySceneBindings` (reserved units bound unconditionally — link BindingPoints table)
- [ ] `Renderer3D::Statistics` — submitted/culled/drawn/instanced counters (S12.1), how to read cull rate
- [ ] **Deferred-submission semantics (S12.2 breaking change)** — material values are read at FLUSH, not at submit; per-draw variation requires `Material::Clone`. Every submission entry repeats this in Notes & pitfalls.
- [ ] Sort behavior — opaque key order (shader→material→mesh→front-to-back), transparent = back-to-front + depth-write-off via `Material::SetTransparent`
- [ ] Auto-instancing — trigger conditions (≥4 identical mesh/material runs, registered `Material::SetInstancingShader` twin, entityID −1)
- [ ] `Mesh` — creation (vertices/layout), primitive factories if present, local AABB accessor, tangents
- [ ] `Model` — glTF load via cgltf (`Create`), submeshes/materials, PBR material import behavior, winding/scene fixes
- [ ] `InstanceSet` — per-instance transform pool (SSBO binding 9), update/draw API (F5)
- [ ] `Frustum` — extraction from view-projection, containment/intersection tests (header-only)
- [ ] LOD — `LODGroupComponent` behavior contract (component itself lives in [ecs.md](ecs.md); the distance-switch + caster-uses-lit-level rule is documented here)

## Sections to write

1. "One frame through the queue" intro (submit → cull → sort → instance → flush) with a Mermaid flowchart — shared source with README §3D. <!-- TODO(D10) -->
2. Entries per checklist. <!-- TODO(D10) -->
3. Light submission (directional/point via ECS components vs direct API — verify against `Renderer3D.h` which of the two the client uses). <!-- TODO(D10) -->

---
*Changelog:*
