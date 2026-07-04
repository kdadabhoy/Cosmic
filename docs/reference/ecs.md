# API Reference — Entity Component System

> **STATUS: SKELETON** — to be filled by work order **D13** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/scene/Scene.h`, `scene/Entity.h`,
`scene/Components.h`, `scene/System.h`, `scene/ComponentRegistry.h`, `scene/ScenePicker.h`,
`scene/SelectableComponent.h`.

**Read first:** root README §15 (ECS), §23 (scenes); systems explainer
[ecs-scene](../systems/ecs-scene.md).

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Scene` — `Create`, `CreateEntity`/`DestroyEntity`, update/render hooks (`OnUpdate`, `OnRender`, `OnRender3D` — what each draws automatically: mesh renderers, terrain, water, particles, lights), view/each iteration if exposed
- [ ] `Entity` — `AddComponent`/`GetComponent`/`HasComponent`/`RemoveComponent`, validity, ID accessors
- [ ] **Every component in `Components.h` — a table row each** (field-by-field): `TransformComponent`, `SpriteRendererComponent`, `MeshRendererComponent`, light components (directional/point), `TerrainComponent`, `WaterComponent`, `ParticleEmitterComponent`, `LODGroupComponent` (S12.4 — distance levels; casters use the lit level), camera components if present, `TagComponent`, `SelectableComponent`, … enumerate the header exhaustively
- [ ] `System` / `ComponentRegistry` — the system base + registration surface clients can use
- [ ] `ScenePicker` — entity-ID picking (S5.4): pick call, coordinate space (viewport pixel contract — README §3 `GetViewportPos/Size`), `-1`/empty result behavior
- [ ] Selection — `SelectableComponent` + telemetry-side `EntitySelection` cross-link ([serial-telemetry.md](serial-telemetry.md))

## Sections to write

1. Mermaid `classDiagram`: Scene ⇄ registry ⇄ Entity handle ⇄ components. <!-- TODO(D13) -->
2. Entries per checklist; the component table is the centerpiece — every field, its unit, its default, who consumes it (which render pass / system). <!-- TODO(D13) -->
3. "What `Scene::OnRender3D` does for you" — the automatic-draw contract, so clients know what NOT to draw manually. <!-- TODO(D13) -->

---
*Changelog:*
