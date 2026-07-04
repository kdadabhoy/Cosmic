# ECS & Scenes — How It Works

> **STATUS: SKELETON** — to be filled by work order **D31** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** entities are just IDs; all their data lives in tightly-packed component
arrays (entt), and "systems" — including the engine's own `Scene::OnRender3D` — iterate
those arrays to make things happen.
**Source:** `Cosmic/src/scene/*` (Scene, Entity, Components, System, ComponentRegistry, ScenePicker)
**API Reference:** [../reference/ecs.md](../reference/ecs.md) · **Guide:** root README §15, §23

## Section plan

1. **Overview** — composition over inheritance, the spreadsheet analogy (entities = row numbers, components = columns). <!-- TODO(D31) -->
2. **Mental model** — diagram **DG-9** (Scene ⇄ registry ⇄ Entity handle ⇄ component storage). <!-- TODO(D31) -->
3. **Step-by-step** — create entity → add `TransformComponent` + `MeshRendererComponent` → what `Scene::OnRender3D` does with them next frame (and same for terrain/water/particles/lights components). <!-- TODO(D31) -->
4. **Technical implementation** — entt underneath (views, groups if used), `Entity` as {handle, scene*} convenience wrapper, `ComponentRegistry` purpose (per-DLL registration/serialization prep for Starforge E1–E2), `ScenePicker` ID-buffer flow, `SelectableComponent` + telemetry selection bridge, parallel-system interaction (`jobs/SystemQuery` — cross-link). <!-- TODO(D31) -->
5. **Design decisions** — why entt; which engine features are component-driven vs direct-API and the rule for choosing. <!-- TODO(D31) -->
6. **Limits & future work** — serialization/UUIDs land with Starforge E2 (link doc 11). <!-- TODO(D31) -->

**Truth sources:** `Scene.cpp` (OnRender3D is the automatic-draw contract), `Components.h`
(enumerate honestly), README §15/§23 (migrating here).
