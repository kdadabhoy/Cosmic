# API Reference — Entity Component System

> **STATUS: SKELETON** — to be filled by work order **D13** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/scene/Scene.h`, `scene/Entity.h`,
`scene/Components.h`, `scene/Components3D.h`, `scene/System.h`, `scene/ComponentRegistry.h`,
`scene/ScenePicker.h`, `scene/SelectableComponent.h`.

**Read first:** the guide chapter
[`../guide/entities-and-components.md`](../guide/entities-and-components.md) (the ECS and the full
component catalogue) and
[`../guide/scenes-and-serialization.md`](../guide/scenes-and-serialization.md) (`.cscene`,
reflection, prefabs, UUIDs, `SceneManager`, undo); systems explainer
[ecs-scene](../systems/ecs-scene.md). For how the 2D and UI component families are actually used —
sizing, sort order, the painter list, canvas anchors, the button state machine — see
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md) and
[`../guide/game-ui.md`](../guide/game-ui.md); the latter is also the client-facing source for
`scene/ui/UiComponents.h` + `scene/ui/UiSystem.h`, which have no manifest row (D52). For
`Scene::Events()` / `Scene::ActiveFlow()` and the `EventBus` those return — likewise unlisted —
see [`../guide/flow-and-story.md`](../guide/flow-and-story.md).

**`ScenePicker` is documented in the wrong place.** Its manifest row points here, but the guide
chapter that covers it end to end — the viewport-pixel coordinate contract, what the ID pass can and
cannot see, `WorldPoint` and the CAD pivot probe — is
[`../guide/cameras.md`](../guide/cameras.md#click-to-select-an-entity-3d-only). D13 should either
cross-link it or hand the header to `cameras.md` when D5 revisits the manifest.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Scene` — `Create`, `CreateEntity`/`DestroyEntity`, update/render hooks (`OnUpdate`, `OnRender`, `OnRender3D` — what each draws automatically: mesh renderers, terrain, water, particles, lights), view/each iteration if exposed
- [ ] `Entity` — `AddComponent`/`GetComponent`/`HasComponent`/`RemoveComponent`, validity, ID accessors
- [ ] **Every component in `Components.h` — a table row each** (field-by-field): `TransformComponent`, `SpriteRendererComponent`, `MeshRendererComponent`, light components (directional/point), `TerrainComponent`, `WaterComponent`, `ParticleEmitterComponent`, `LODGroupComponent` (S12.4 — distance levels; casters use the lit level), camera components if present, `TagComponent`, `SelectableComponent`, … enumerate the header exhaustively
- [ ] `System` / `ComponentRegistry` — the system base + registration surface clients can use
- [ ] `ScenePicker` — entity-ID picking (S5.4): pick call, coordinate space (viewport pixel contract — `GetViewportPos/Size`, see [`../guide/project-anatomy.md`](../guide/project-anatomy.md#the-control-api)), `-1`/empty result behavior
- [ ] Selection — `SelectableComponent` + telemetry-side `EntitySelection` cross-link ([serial-telemetry.md](serial-telemetry.md))

## Sections to write

1. Mermaid `classDiagram`: Scene ⇄ registry ⇄ Entity handle ⇄ components. <!-- TODO(D13) -->
2. Entries per checklist; the component table is the centerpiece — every field, its unit, its default, who consumes it (which render pass / system). <!-- TODO(D13) -->
3. "What `Scene::OnRender3D` does for you" — the automatic-draw contract, so clients know what NOT to draw manually. <!-- TODO(D13) -->

---
*Changelog:*
