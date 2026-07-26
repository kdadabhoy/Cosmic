# ECS & Scenes — How It Works

> **STATUS: SKELETON** — to be filled by work order **D31** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** entities are just IDs; all their data lives in tightly-packed component
arrays (entt), and "systems" — including the engine's own `Scene::OnRender3D` — iterate
those arrays to make things happen.
**Source:** `Cosmic/src/scene/*` (Scene, Scene3D, Entity, Components, Components3D, System, ComponentRegistry, ScenePicker) + `reflect/TypeRegistry*.cpp`
**API Reference:** [../reference/ecs.md](../reference/ecs.md), [../reference/physics.md](../reference/physics.md) · **Guide:** [../guide/entities-and-components.md](../guide/entities-and-components.md), [../guide/scenes-and-serialization.md](../guide/scenes-and-serialization.md)

> **Build note (Phase 29) — the scene layer is split down the middle.** Write this into §4 when
> D31 runs; the shape is:
>
> | Shared (both configurations) | 3D configuration only |
> | --- | --- |
> | `scene/Components.h` — **19** components: the 10 dimension-neutral (ID, Opaque, Relationship, Tag, Transform, Camera, Environment, NativeScript, SystemScript, Prefab), the 4 2D (SpriteRenderer, SpriteAnimation, Tilemap, Light2D), the 5 physics (RigidBody, box/sphere/capsule colliders, CharacterController) | `scene/Components3D.h` — **15**: MeshRenderer, PrimitiveMesh, LODGroup, Animator, Socket, DirectionalLight, PointLight, Terrain, Water, ParticleEmitter, VoxelVolume, MeshCollider, TerrainCollider, NavMesh, NavAgent |
> | `scene/Scene.cpp` — entity CRUD + hierarchy, update/fixed-update, **all four physics methods**, the 2D render path, sprite animation, `BuildSpriteDrawList`, `WorldOf` | `scene/Scene3D.cpp` — the nav session, the four render-path syncs, `OnRenderWorldFX`, `OnRender3D`, `UpdateAnimators`, `SubmitOpaqueMeshes`, `BuildRenderDesc` |
> | `reflect/TypeRegistry.cpp` — the 19 | `reflect/TypeRegistry3D.cpp` — `RegisterEngine3DTypes()`, the same 15 registrations verbatim |
> | `SceneSerializer` (2 fenced blocks), `ScenePhysics` (3D collider branches fenced) | `scene/SceneNav.*`, `scene/ScenePicker.*`, `scene/WorldSystemRecipes.*` |
>
> **The property that makes this safe is `OpaqueComponentsComponent`:** a 3D component block loaded
> by an engine that never registered its type is preserved as verbatim JSON and re-emitted unchanged
> on save. So a 3D scene opened, edited and saved by the 2D editor keeps every 3D block
> byte-for-byte (`tests/test_crossbuild_scene.cpp` asserts both directions). Full rules:
> [`build-2d-3d-split.md`](build-2d-3d-split.md).

## Section plan

1. **Overview** — composition over inheritance, the spreadsheet analogy (entities = row numbers, components = columns). <!-- TODO(D31) -->
2. **Mental model** — diagram **DG-9** (Scene ⇄ registry ⇄ Entity handle ⇄ component storage). <!-- TODO(D31) -->
3. **Step-by-step** — create entity → add `TransformComponent` + `MeshRendererComponent` → what `Scene::OnRender3D` does with them next frame (and same for terrain/water/particles/lights components). <!-- TODO(D31) -->
4. **Technical implementation** — entt underneath (views, groups if used), `Entity` as {handle, scene*} convenience wrapper, `ComponentRegistry` purpose (per-DLL registration/serialization prep for Starforge E1–E2), `ScenePicker` ID-buffer flow, `SelectableComponent` + telemetry selection bridge, parallel-system interaction (`jobs/SystemQuery` — cross-link). **Plus the Phase 29 partition** (table above): which components live in which header, `RegisterEngine3DTypes()` and why splitting the registration call changes no output (the registry is keyed by entt type hash, and both consumers of iteration order re-sort by name), the physics session as a *shared* concern, and `OpaqueComponentsComponent` as the cross-build data-safety guarantee. <!-- TODO(D31) -->
5. **Design decisions** — why entt; which engine features are component-driven vs direct-API and the rule for choosing. <!-- TODO(D31) -->
6. **Limits & future work** — serialization/UUIDs land with Starforge E2 (link doc 11). <!-- TODO(D31) -->

**Truth sources:** `Scene.cpp` (OnRender3D is the automatic-draw contract), `Components.h`
(enumerate honestly), `SceneSerializer.cpp` (the generic reflection visitor).

**Do not re-derive the component catalogue here.** D49 wrote it in
[`../guide/entities-and-components.md`](../guide/entities-and-components.md) — all 34 components
with fields, units, defaults and consumers, plus the `Active`/`Enabled` gates and the
automatic-draw contract. This explainer covers *mechanism* (registry, views, type ids, the file
partition, `OpaqueComponentsComponent`) and links there for the catalogue.
