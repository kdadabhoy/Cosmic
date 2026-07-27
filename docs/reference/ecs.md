# API Reference — Entity Component System

> **STATUS: WRITTEN** — work order **D13** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/scene/Scene.h` (+ `Scene.cpp`, `Scene3D.cpp`),
`scene/Entity.h`, `scene/Components.h`, `scene/Components3D.h`, `scene/System.h`,
`scene/ComponentRegistry.h`, `scene/SelectableComponent.h`. Reflection metadata is read out of
`reflect/TypeRegistry.cpp` and `reflect/TypeRegistry3D.cpp` — those two files decide what appears in
the Inspector and what reaches a `.cscene`, so they are quoted here per field.

**Read first — this chapter does not repeat the guide.** The task-oriented half lives in
[`../guide/entities-and-components.md`](../guide/entities-and-components.md) (how the ECS is meant to
be used, worked examples, the "what the engine draws for you" contract, custom components) and
[`../guide/scenes-and-serialization.md`](../guide/scenes-and-serialization.md) (`.cscene`,
reflection, prefabs, UUIDs, `SceneManager`, undo). This chapter is the **per-call, per-field
lookup**: verbatim signature, exact behaviour, failure mode, registered name, default value,
consumer. Where the two overlap, the guide owns the idiom and this file owns the fact.

Diagram **DG-9** (Scene ⇄ registry ⇄ `Entity` handle ⇄ component families ⇄ consuming passes) is
already built —
[reuse it in the guide](../guide/entities-and-components.md#dg-9--scene-registry-entity-components).
It is not redrawn here.

**Other chapters own these neighbours:** the 2D and UI component families in use —
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md),
[`../guide/game-ui.md`](../guide/game-ui.md) (the latter is also the client-facing source for
`scene/ui/UiComponents.h` + `scene/ui/UiSystem.h`, which have no manifest row); the physics tier's
runtime side — [physics.md](physics.md); `Scene::Events()` / `Scene::ActiveFlow()` and the
`EventBus`/`FlowMachine` they hand out — [`../guide/flow-and-story.md`](../guide/flow-and-story.md);
`System`'s parallel subclass and `SystemQuery` — [jobs.md](jobs.md) and
[`../guide/jobs-and-parallelism.md`](../guide/jobs-and-parallelism.md).

> ### `ScenePicker` is documented in the wrong chapter
>
> `scene/ScenePicker.h` carries a manifest row pointing at **this** file, but nothing about
> entity-ID picking belongs to the ECS: it is a camera/viewport concern, and the chapter that
> already covers it end to end — the viewport-pixel coordinate contract, what the ID pass can and
> cannot see, `WorldPoint` and the CAD pivot probe — is
> [`../guide/cameras.md`](../guide/cameras.md#click-to-select-an-entity-3d-only). **D13 did not
> absorb it.** The manifest row should be re-pointed to [cameras.md](cameras.md) (D14) rather than
> here. See [`ScenePicker` — routed elsewhere](#scenepicker--routed-elsewhere) for the pointer stub
> and the exact change needed.

---

## Contents

- [The warning that governs this chapter](#the-warning-that-governs-this-chapter) — nothing ticks `Scene::OnUpdate`
- [`Scene`](#scene) — [lifecycle & entities](#scene-lifecycle-and-entities) · [hierarchy](#scene-hierarchy) · [systems](#scene-systems) · [queries](#scene-queries) · [sessions](#scene-sessions-physics-and-navigation) · [render & sync](#scene-render-and-sync-hooks)
- [`Entity`](#entity) — the 16-byte handle and its six template members
- [Components — `scene/Components.h`](#components--scenecomponentsh-19-both-configurations) *(the field-by-field centerpiece)*
- [Components — `scene/Components3D.h`](#components--scenecomponents3dh-15-3d-only)
- [`System`](#system) · [`ComponentRegistry`](#componentregistry--cs_register_component) · [`SelectableComponent`](#selectablecomponent)
- [`ScenePicker` — routed elsewhere](#scenepicker--routed-elsewhere)
- [Configuration summary](#configuration-summary)

---

## The warning that governs this chapter

> ### ⚠ Nothing in the engine calls `Scene::OnUpdate` or `Scene::OnFixedUpdate`
>
> The entire four-pass system pipeline — sequential `System::OnUpdate`, then `StageQueries` /
> `OnPrepare`, then `OnParallelExecute`, then the single `JobSystem::WaitIdle()` barrier, then
> `OnMerge` / `CommitQueries` — lives **inside those two methods**, and both are dead code from the
> engine's point of view:
>
> - **`Scene::OnUpdate` has zero in-tree callers.** Verified across `Cosmic/src`, `Cosmic/templates`,
>   `Projects/`, `Runtime/` and `tests/`. `PlayerLayer.cpp:226` is `m_Scenes.OnUpdate(ts)` —
>   that is `SceneManager::OnUpdate`, a **different** method on a different class.
> - **`Scene::OnFixedUpdate` has exactly two in-tree callers**, both in the project scaffold:
>   `Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.cpp:305` and `:313`.
> - **`Scene::AddSystem` has exactly one in-tree call site**, in that same file
>   (`TemplateTelemetryLayer.cpp:80`).
>
> Consequence: **a project that registers a `System` and never calls `scene->OnUpdate(dt)` gets
> nothing.** No sequential pass, no parallel passes, no query staging, no `WaitIdle` barrier. The
> shipped host (`PlayerLayer`) and the editor (Starforge) tick `ScriptHost`,
> `Scene::UpdateSpriteAnimations` and `Scene::UpdateAnimators` **directly** and never route through
> `Scene::OnUpdate`. This is the owner-ticked-service pattern the engine uses for `SceneManager`,
> `SerialLink` and `ScriptHost` alike ([`../guide/project-anatomy.md`](../guide/project-anatomy.md)).
>
> If you want per-entity logic the engine *does* drive for you, use the script tier
> (`NativeScriptComponent` / `SystemScriptComponent`, [`../guide/scripting.md`](../guide/scripting.md)),
> not `System`.

A second global rule, which this chapter states once and then relies on:

> **`CS_ASSERT` and `CS_CORE_ASSERT` are compiled out in every configuration.** `core/Core.h:61`
> gates `CS_ENABLE_ASSERTS` on `GLCORE_DEBUG || CS_DEBUG`, and neither symbol is defined anywhere in
> the tree or in any CMake configuration. **No entry below ever describes a `CS_ASSERT` as an
> enforced guard**, because none of them exist in a built binary. Where a real diagnostic fires it
> is named explicitly (a `CS_CORE_WARN`, or EnTT's own `assert`, which is live in Debug and
> `NDEBUG`-compiled-out in Release).

---

## `Scene`

```cpp
// scene/Scene.h
class COSMIC_API Scene
{
public:
    Scene();
    ~Scene();   // out-of-line: m_Physics is a unique_ptr to a forward-declared type
    ...
};
```

**What it is** — the owner of one `entt::registry`, one UUID→handle index, the list of registered
`System`s, a per-scene `EventBus`, a world clock, and (only during a play session) the physics and
navigation runtime bindings. Held by `Ref<Scene>`; `SceneManager` owns the active one in a shipped
game. `Entity` handles hold a **raw, non-owning** `Scene*`, so the scene must outlive every handle
you keep.

**Configuration** — `Scene` itself ships in **both** engine builds. Its implementation is split
across two translation units: `scene/Scene.cpp` (everything shared) and `scene/Scene3D.cpp` (the 3D
half), and **`Scene3D.cpp` is excluded from the 2D build** (`Cosmic/CMakeLists.txt:202`). The
mechanism is [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

> **Seven `Scene` members are 3D-only but declared *unfenced*.** `OnRender3D` (`Scene.h:238`),
> `UpdateAnimators` (`:250`), `SyncPrimitiveMeshes` (`:261`), `SyncWorldSystems` (`:273`),
> `SyncVoxelVolumes` (`:285`), `SyncNavMeshes` (`:295`) and `OnRenderWorldFX` (`:307`) are defined
> **only** in `Scene3D.cpp`. In a 2D build they are declared, they compile at every call site, and
> they fail at **link** time as unresolved externals. Contrast the nav-session block
> (`OnNavStart`/`OnNavStep`/`OnNavStop`/`GetNav`, `Scene.h:121-144`) and the private
> `SubmitOpaqueMeshes`/`FindAnimatorFor` (`:414-426`), which *are* fenced in `#ifndef
> COSMIC_2D_ONLY` and therefore fail at compile time with a clear message. This is the same
> "compiles, then fails at link" class of 3D-only symbol the manifest marks ³ᴰ⁺ for
> `camera/NavigationCube.h`. Each entry below says which failure you get.

---

<a id="scene-lifecycle-and-entities"></a>
### Lifecycle and entities

#### `Scene::Create`

```cpp
template<typename... Args>
static Ref<Scene> Create(Args&&... args)
```

**What it does** — `std::make_shared<Scene>(...)`. `Scene` has only a default constructor, so in
practice this is `Scene::Create()`.

**Why you'd use it** — the engine's uniform smart-pointer factory convention. Constructing a bare
`Scene` on the stack works, but every API that stores a scene takes `Ref<Scene>`.

**Example**

```cpp
#include <Cosmic.h>

Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();
```

**Notes & pitfalls**
- A fresh scene is **completely empty** — no camera, no environment, no entities. `FindEnvironment()`
  returns `nullptr` and the renderer falls back to its own defaults.
- Both configurations.

#### `Scene::CreateEntity`

```cpp
Entity CreateEntity(const std::string& name = "GenericEntity");
```

**What it does** — creates a registry entity and emplaces exactly three components on it:
`IDComponent` (with a **fresh random `UUID`**), `TransformComponent` (defaulted) and `TagComponent`
(named). Registers the UUID in the scene's O(1) id index and returns the handle.

**Why you'd use it** — this is the **only** supported way to make an entity. See the pitfall below
for what you lose by going around it.

**Example**

```cpp
Cosmic::Entity crate = scene->CreateEntity("Crate");
crate.GetComponent<Cosmic::TransformComponent>().Position = { 0.0f, 1.0f, -4.0f };
```

**Notes & pitfalls**
- **The default parameter and the empty-string fallback differ.** Calling `CreateEntity()` names the
  entity `"GenericEntity"`; calling `CreateEntity("")` names it `"Entity"`
  (`Scene.cpp:97` — `name.empty() ? "Entity" : name`).
- Cannot fail; there is no error return.
- Both configurations.

> **Create entities through the scene, not the registry.** `GetRegistry().create()` produces an
> entity with **no `IDComponent`**: it is invisible to [`FindByUUID`](#scenefindbyuuid), cannot be
> parented (hierarchy links are UUIDs), is not serialized as a referenceable entity, and reports
> `EntityId = 0` to physics contact events.

#### `Scene::CreateEntityWithUUID`

```cpp
Entity CreateEntityWithUUID(UUID id, const std::string& name = "GenericEntity");
```

**What it does** — as [`CreateEntity`](#scenecreateentity), but adopts a caller-supplied identity.
**If `id` is invalid (`UUID(0)`) a fresh one is generated instead** (`Scene.cpp:91`) — the call never
produces a zero-id entity.

**Why you'd use it** — deserialization and prefab instantiation, so that saved parent links,
`EntityRef` script fields and prefab sources still resolve. `SceneSerializer` is the primary caller.

**Notes & pitfalls**
- **Reusing a live id silently overwrites the index entry**: `m_UUIDMap[id]` is assigned
  unconditionally, so the older entity becomes unreachable through `FindByUUID` while still existing
  in the registry. Do not hand out ids you have not first checked.
- Both configurations.

#### `Scene::FindByUUID`

```cpp
Entity FindByUUID(UUID id);
```

**What it does** — O(1) hash lookup in the scene's UUID index.

**Failure behaviour** — returns a **default-constructed `Entity`** (`operator bool == false`) when
the id is unknown *or* when the mapped handle is no longer valid in the registry. It never throws
and never returns a dangling handle.

**Example**

```cpp
if (Cosmic::Entity target = scene->FindByUUID(Cosmic::UUID(savedId)))
    target.GetComponent<Cosmic::TagComponent>().Active = false;
```

**Notes & pitfalls**
- Only entities created through `CreateEntity` / `CreateEntityWithUUID` are indexed.
- Both configurations.

#### `Scene::DestroyEntity`

```cpp
void DestroyEntity(Entity entity, bool destroyChildren = true);
```

**What it does** — removes the entity from its parent's `Children` list, then either recursively
destroys the whole subtree (`destroyChildren = true`, the default) or **orphans** the children by
clearing each child's `Parent` to `UUID(0)`. Erases the entity from the UUID index and destroys the
registry handle.

**Failure behaviour** — a no-op for a falsy handle or a handle that is no longer valid; it returns
silently rather than warning.

**Notes & pitfalls**
- **Discard every copy of the handle afterwards.** `Entity::operator bool` re-checks
  `registry.valid()`, so a stale copy tests `false` rather than aliasing a recycled slot — but
  `GetComponent<T>()` on a stale handle is still undefined.
- The implementation deliberately reads `Parent`, `Children` and the id **by value up front**
  (`Scene.cpp:119-130`) because destroying children mutates the `RelationshipComponent` pool.
- Both configurations.

---

<a id="scene-hierarchy"></a>
### Hierarchy

#### `Scene::SetParent`

```cpp
bool SetParent(Entity child, Entity parent, bool keepWorldPose = true);
```

**What it does** — re-parents `child` under `parent`, adding a `RelationshipComponent` to both
endpoints if missing, removing the child from its previous parent's `Children`, and appending it to
the new parent's. Pass a falsy `parent` to detach to root. `Children` order is the append order of
`SetParent` calls and is preserved by the serializer.

**Failure behaviour** — returns `false` without touching anything in two cases: a falsy `child`
(silently), and a cycle — parenting an entity to itself or to one of its own descendants, which logs
`"Scene::SetParent refused: the operation would create a cycle."` and returns `false`.

**Example**

```cpp
Cosmic::Entity turret = scene->CreateEntity("Turret");
Cosmic::Entity barrel = scene->CreateEntity("Barrel");
scene->SetParent(barrel, turret);              // keeps barrel's world pose
scene->SetParent(barrel, Cosmic::Entity{});    // detach back to root
```

**Notes & pitfalls**
- **`keepWorldPose = true` rewrites the child's `TransformComponent` and sets
  `UseQuatRotation = true`.** It decomposes `inverse(parentWorld) * worldBefore` and writes
  `Position`, `Scale` and `RotationQuat`; the Euler `Rotation` field is left stale and thereafter
  ignored by `GetTransform()`. If you author with Euler angles, pass `keepWorldPose = false`.
- If `glm::decompose` fails (a degenerate matrix), the transform is left **untouched** and the call
  still returns `true`.
- The implementation emplaces both `RelationshipComponent`s **before** taking any reference
  (`Scene.cpp:288-293`) precisely because an `emplace` may reallocate that pool. Copy the pattern.
- Both configurations.

#### `Scene::GetWorldTransform`

```cpp
glm::mat4 GetWorldTransform(Entity entity);
```

**What it does** — `parentWorld · local`, walking the whole parent chain (private `WorldOf`). An
entity with no `RelationshipComponent` resolves to its own `TransformComponent::GetTransform()`, so
flat scenes cost one matrix build.

**Failure behaviour** — returns the identity matrix for a falsy handle. A parent UUID that no longer
resolves terminates the walk and returns what has been composed so far.

**Notes & pitfalls**
- **In a 3D build this call is socket-aware.** If the entity has a `SocketComponent` and an ancestor
  carries an `AnimatorComponent` whose skeleton contains the named joint, the result is
  `ancestorWorld · jointFrame · (T·R·S of the socket offset)` and the entity's **own**
  `TransformComponent` is ignored (`Scene.cpp:198-227`). The whole block is fenced out of the 2D
  build.
- **This is the read you want for a pose, not `Transform.Rotation`.** Physics write-back, sockets
  and `SetParent(keepWorldPose)` all bypass the Euler field.
- Recursion is unmemoized: a deep chain re-walks per query. The socket search is guarded at 4096
  steps; the ordinary parent walk is **not** cycle-guarded, but `SetParent` is the only supported
  mutator and it refuses cycles.
- Both configurations.

#### `Scene::IsAncestor`

```cpp
bool IsAncestor(Entity ancestor, Entity node);
```

**What it does** — true if `ancestor` appears anywhere on `node`'s parent chain. Returns `false` if
either handle is falsy. Used by `SetParent`'s cycle check and by the Hierarchy panel's drag-drop
validation.

Both configurations.

#### `Scene::IsActiveInHierarchy`

```cpp
bool IsActiveInHierarchy(entt::entity handle);
bool IsActiveInHierarchy(Entity entity);
```

**What it does** — effective-active: the entity's own `TagComponent::Active` **and** every
ancestor's. Walks the parent chain with a 4096-step cycle guard. An entity **without** a
`TagComponent` counts as active.

**Failure behaviour** — the `Entity` overload returns `false` for a falsy handle; the `entt::entity`
overload assumes a valid handle.

**Notes & pitfalls**
- **Not universal.** Honoured by `BuildSpriteDrawList`/`OnRenderSprites`, `OnRender2DLights`, the
  legacy `OnRender`, `SubmitOpaqueMeshes`, `GatherSceneLights`, `SyncWorldSystems`,
  `OnRenderWorldFX`, `ScenePhysics::BuildBodies`, and `ScriptHost::Tick`/`FixedTick`.
  **Not** honoured by `ScriptHost::Bind` (an inactive entity's script is still constructed and still
  receives `OnCreate` and `OnStart`), by the terrain draw in either render path, or by
  `BuildRenderDesc`'s water and emitter gather — see
  [`BuildRenderDesc`](#scenebuildrenderdesc).
- Toggling `Active` mid-session does not create or destroy physics bodies; those are built once at
  `OnPhysicsStart`.
- Both configurations.

---

<a id="scene-systems"></a>
### Systems

#### `Scene::OnUpdate`

```cpp
void OnUpdate(float deltaTime);
```

**What it does** — in order: (3D only) `UpdateAnimators(deltaTime)`; **Pass A** every registered
`System::OnUpdate`; then, **only if at least one `ParallelSystem` is registered**, **Pass B**
`StageQueries` + `OnPrepare` per system, **Pass C** `OnParallelExecute` per system, one
`JobSystem::Get().WaitIdle()` barrier for all of them, and **Pass D** `OnMerge` + `CommitQueries`
per system.

**Why you'd use it** — you call it from your own layer's `OnUpdate` if, and only if, you registered
systems with [`AddSystem`](#sceneaddsystem). **See
[the warning](#the-warning-that-governs-this-chapter): nothing in the engine calls this for you.**

**Example**

```cpp
// In your Layer::OnUpdate — modelled on
// Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.cpp
void MyLayer::OnUpdate(Cosmic::Timestep ts)
{
    m_Scene->OnUpdate(ts);   // Pass A-D for every registered System
}
```

**Notes & pitfalls**
- The `WaitIdle` barrier is **global and shared**: every parallel system submits before any of them
  waits, so their jobs overlap. That is also why `OnParallelExecute` must use the *async* job
  helpers and must never call `WaitIdle` itself ([jobs.md](jobs.md)).
- With no parallel systems registered, passes B–D are skipped entirely — there is no barrier cost.
- The animator advance is inside `#ifndef COSMIC_2D_ONLY`; the rest of the body is shared.
- Both configurations (the method itself is in `Scene.cpp`).

#### `Scene::OnFixedUpdate`

```cpp
void OnFixedUpdate(float fixedDeltaTime);
```

**What it does** — the fixed-step twin: **Pass A** `System::OnFixedUpdate`, then (if any parallel
system exists) `StageQueries` + `OnFixedPrepare`, `OnFixedParallelExecute`, the single `WaitIdle`,
then `OnFixedMerge` + `CommitQueries`. It does **not** touch animators.

**Notes & pitfalls**
- Same no-engine-caller warning. Its only in-tree callers are `TemplateTelemetryLayer.cpp:305` and
  `:313`.
- This is **not** the physics tick. Physics runs through
  [`OnPhysicsStep`](#scene-sessions-physics-and-navigation), which the host drives separately; the documented
  fixed-step order is `scripts' OnFixedUpdate → OnPhysicsStep → (3D) OnNavStep →
  DispatchPhysicsEvents`.
- Both configurations.

#### `Scene::AddSystem`

```cpp
template<typename T, typename... Args>
T& AddSystem(Args&&... args)
```

**What it does** — constructs a `Scope<T>` (the scene owns it), records a **non-owning** pointer in
the parallel list if `dynamic_cast<ParallelSystem*>` succeeds, appends the owning pointer to the
system list, and returns a reference to the constructed system.

**Why you'd use it** — scene-scoped simulation logic that is not per-entity. Compare
`SystemScriptComponent`, which the `ScriptHost` **does** drive automatically.

**Example**

```cpp
// From Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.cpp:80
m_AgentSystem = &m_Scene->AddSystem<AgentSystem>(&m_Recorder, k_Bounds);
```

**Notes & pitfalls**
- **Hold the returned reference (or a pointer to it), not a copy.** `ParallelSystem` is deliberately
  non-copyable and non-movable — its `ReadWriteQuery` members self-register in their constructors, so
  a copy would stage and commit every query twice per frame.
- Registration order is execution order within each pass.
- The system only ever runs if you call [`OnUpdate`](#sceneonupdate) / [`OnFixedUpdate`](#sceneonfixedupdate).
- Both configurations.

#### `Scene::GetSystem`

```cpp
template<typename T>
T* GetSystem()
```

**What it does** — linear search over the owned system list, `dynamic_cast`ing each entry; returns
the first match or `nullptr`.

**Notes & pitfalls**
- **O(n) with a `dynamic_cast` per entry — never call it per frame.** The header says so at
  `Scene.h:389-395`. Cache the pointer in your layer's `OnAttach`.
- Both configurations.

#### `Scene::RemoveAllSystems`

```cpp
void RemoveAllSystems()
```

**What it does** — destroys every owned system (running their destructors) and then clears the
non-owning parallel pointer list, **in that order** — deliberately, so that a destructor which
inspects the parallel list still sees it intact.

There is no "remove one system" API. Both configurations.

---

<a id="scene-queries"></a>
### Queries and raw access

#### `Scene::View`

```cpp
template<typename... Components>
auto View()
```

**What it does** — forwards to `entt::registry::view<Components...>()`. All listed types are
**required** (an AND, not an OR).

**Example**

```cpp
for (auto [entity, transform, sprite]
     : scene->View<Cosmic::TransformComponent, Cosmic::SpriteRendererComponent>().each())
{
    transform.Position.x += speed * ts;
}
```

**Notes & pitfalls**
- **Do not add or destroy components while iterating a view.** Structural changes invalidate the
  iteration. Collect handles into a `std::vector` first — that is exactly what
  `Scene::SyncPrimitiveMeshes` does before it `get_or_emplace`s sibling mesh renderers
  (`Scene3D.cpp:162`).
- For *optional* components use `GetRegistry().try_get<T>(handle)`, never `Entity::GetComponent`.
- Both configurations.

#### `Scene::GetRegistry`

```cpp
inline entt::registry& GetRegistry();
inline const entt::registry& GetRegistry() const;
```

**What it does** — hands out the raw EnTT registry for everything the thin wrapper does not cover:
`try_get`, `any_of`, `all_of`, `get_or_emplace`, `patch`, groups, sorting, `valid`.

**Notes & pitfalls**
- Entities created directly on the registry bypass the UUID index — see
  [`CreateEntity`](#scenecreateentity).
- `try_get<T>` is the safe read; it returns `nullptr` cleanly where `GetComponent<T>` is undefined.
- Both configurations.

#### `Scene::Events` / `Scene::SetActiveFlow` / `Scene::ActiveFlow`

```cpp
inline EventBus& Events();
inline const EventBus& Events() const;
inline void         SetActiveFlow(FlowMachine* flow);
inline FlowMachine* ActiveFlow() const;
```

**What they do** — `Events()` returns the per-scene signal channel (UI buttons emit onto it; the
`FlowMachine` and scripts subscribe), empty by default. `ActiveFlow()` returns the `FlowMachine`
currently driving this scene or `nullptr`; it is **not owned**, and a running flow sets and clears it.

Covered in full by [`../guide/flow-and-story.md`](../guide/flow-and-story.md); `scene/EventBus.h`
and `scene/FlowMachine.h` have no manifest row. Both configurations.

---

<a id="scene-sessions-physics-and-navigation"></a>
### Sessions — physics and navigation

Bodies and nav agents exist **only while a session runs** (editor Play, or `PlayerLayer` in a
shipped game). Edit mode holds no Jolt or Detour objects: the components are the authored truth and
the runtime objects are derived. Full treatment in [physics.md](physics.md).

| Call | Signature | Does | Configuration |
| --- | --- | --- | --- |
| `OnPhysicsStart` | `void OnPhysicsStart(PhysicsWorld& world)` | Constructs `ScenePhysics` and calls `BuildBodies()` — one shot at session start | both |
| `OnPhysicsStep` | `void OnPhysicsStep(float fixedDeltaTime)` | Push kinematic targets → step the world → write dynamic poses back → update characters. **No-op when no session is active** | both |
| `DispatchPhysicsEvents` | `void DispatchPhysicsEvents(ScriptHost& scripts)` | Drains queued contact events into `OnCollision*` / `OnTrigger*` script callbacks. No-op with no session | both |
| `OnPhysicsStop` | `void OnPhysicsStop(PhysicsWorld& world)` | Tears down every body/character and releases the runtime. The `world` parameter is **unused** (`Scene.cpp:75`) | both |
| `GetPhysics` | `ScenePhysics* GetPhysics()` | The live binding, or `nullptr` in edit mode. Backs `ScriptableEntity::Physics()` / `Character()` | both |
| `OnNavStart` | `void OnNavStart()` | Binds the primary baked navmesh + DetourCrowd and creates one agent per `NavAgentComponent`. **No-op** when the scene has neither a `NavAgentComponent` nor a `NavMeshComponent` | **3D only** — fenced, compile error in 2D |
| `OnNavStep` | `void OnNavStep(float fixedDeltaTime)` | Steps the crowd, writes agent transforms back, emits `nav.arrived`. No-op with no session | **3D only** — fenced |
| `OnNavStop` | `void OnNavStop()` | Releases the crowd and agents | **3D only** — fenced |
| `GetNav` | `SceneNavRuntime* GetNav()` | The live nav binding, or `nullptr`. Backs `ScriptableEntity::Nav()` | **3D only** — fenced |

**Notes & pitfalls**
- **The fixed-step order is a contract, stated in `Scene.h:98-99` and `:124-125`:**
  `scripts' OnFixedUpdate → OnPhysicsStep → (3D) OnNavStep → DispatchPhysicsEvents`. Nav is stepped
  *after* physics on purpose.
- Physics ships in **both** configurations — `COSMIC_2D_ONLY` does not remove it. Only the
  geometry-derived colliders are 3D-only. Navigation is 3D-only in full.
- These four physics methods are pure forwarders that null-check `m_Physics`; calling `OnPhysicsStep`
  outside a session is safe and does nothing.

---

<a id="scene-render-and-sync-hooks"></a>
### Render and sync hooks

These are the calls that make the engine draw your components for you. The *contract* — what you
therefore must **not** draw by hand — is the guide's
["What the engine draws for you"](../guide/entities-and-components.md#what-the-engine-draws-for-you)
section. Below is the per-call reference.

#### `Scene::BuildRenderDesc`

```cpp
void BuildRenderDesc(const Camera& camera, float deltaTime, SceneRenderDesc& out);
```

**What it does** — the ECS → `SceneRenderer` bridge, and the path both the editor viewport and the
shipped `PlayerLayer` use. In the 3D build it runs `SyncPrimitiveMeshes`, `SyncWorldSystems`,
`SyncVoxelVolumes(camera position)` and `SyncNavMeshes`; advances the world clock; fills camera +
`TimeSeconds` + `DeltaTime`; gathers lights; picks the **first built** terrain as `TerrainSystem` and
as the shore-attenuation source; pushes every built water body, marking the one nearest the camera as
`PrimaryReflectionWater`; advances and collects every particle emitter; and installs a `DrawOpaque`
callback that routes the private `SubmitOpaqueMeshes(const SceneDrawContext&)` per pass so meshes
appear in shadow, reflection and main passes alike. (`SceneDrawContext` and `SceneRenderDesc` are
forward-declared in `Scene.h` and defined in `renderer/SceneRenderer.h` —
[rendering-pipeline.md](rendering-pipeline.md).)

**Why you'd use it** — when you want the full engine frame (sky, IBL, shadows, HDR, post) rather than
the cheap direct path. The caller then applies `FindEnvironment()` through
`SceneRenderer::ApplyEnvironment` and calls `Render`.

**Notes & pitfalls**
- **Defined twice, once per configuration, and the declaration is unfenced on purpose** so call
  sites need no `#ifdef`. The 2D twin (`Scene.cpp:788`) advances the clock and fills camera + time —
  everything else it drops is a 3D gather with nothing to gather.
- **`out.EcsScene` is deliberately left null** in both twins; setting it would double-draw against
  `DrawOpaque`.
- **⚠ It ignores `WaterComponent::Enabled` and `ParticleEmitterComponent::Enabled`, and every active
  check.** The gathers test `if (!wc.WaterAsset)` (`Scene3D.cpp:822`) and `if (!pc.Emitter)`
  (`Scene3D.cpp:843`) and nothing else — no `Enabled`, no `IsActiveInHierarchy`. `SyncWorldSystems`
  and `OnRenderWorldFX` *do* honour both. Because `BuildRenderDesc` is the path the editor and the
  player use, **unticking a water body that has already been built leaves it rendering.** Until it is
  fixed, clear the recipe or destroy the entity. Terrain has the same gap (first built asset wins,
  no enable/active check).

#### `Scene::OnRender3D`

```cpp
void OnRender3D(const Camera& camera);
```

**What it does** — the cheap direct 3D path. In order: `SyncPrimitiveMeshes()`,
`SyncWorldSystems()`, `SyncVoxelVolumes(camera.GetPosition())`, `SyncNavMeshes()`, then
`GatherSceneLights` → `Renderer3D::SetLights`, then `BeginScene`, the terrain draw (quadtree LOD
around the pass camera), `SubmitOpaqueMeshes` with a `ScenePass::Main` context, and `EndScene`.

**Notes & pitfalls**
- **It owns its own `BeginScene`/`EndScene` — never wrap it in yours.**
- Not included: water, particles ([`OnRenderWorldFX`](#sceneonrenderworldfx)), sprites
  ([`OnRenderSprites`](#sceneonrendersprites)), UI, sky, shadows, HDR and post (`SceneRenderer`).
- The terrain draw checks only `tc.TerrainAsset` — no `Enabled`, no active check.
- **3D only, unfenced declaration** → a 2D build fails at **link**, not compile.

#### `Scene::OnRenderWorldFX`

```cpp
void OnRenderWorldFX(const Camera& camera,
                     uint32_t sceneColorID, uint32_t sceneDepthID,
                     uint32_t viewportWidth, uint32_t viewportHeight,
                     float deltaTime);
```

**What it does** — advances the world clock, then draws water and particles into the currently bound
target *after* the opaque world, inside its own `Renderer3D::BeginScene`/`EndScene`. Water gets the
scene colour/depth attachments for refraction and depth-fade, and the first built terrain as its
shore-attenuation source; emitters are placed at `WorldOf(entity)`, updated by `deltaTime`, and drawn.

**Notes & pitfalls**
- Uses the **cheap IBL-fallback** water reflection; planar reflection is a `SceneRenderer`-only path.
- Honours `Enabled` **and** `IsActiveInHierarchy` for both water and emitters — unlike
  `BuildRenderDesc`.
- **It advances `m_WorldTime` itself.** Calling both this and `BuildRenderDesc` in one frame double-
  advances the clock.
- **3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::OnRenderSprites`

```cpp
void OnRenderSprites(const glm::mat4& viewProjection,
                     uint32_t viewportWidth, uint32_t viewportHeight);
```

**What it does** — draws every enabled, effectively-active sprite **and** tilemap in the painter
order decided by [`BuildSpriteDrawList`](#scenebuildspritedrawlist). Sets depth **test** on, depth
**write** off, straight alpha, pushes a `Renderer2D` render pass, draws, pops, and restores depth
write. Sprite textures resolve lazily from `TexturePath` (re-resolved whenever the path changes);
tilemaps resolve their atlas the same way, clamp their grid via `EnsureCells()`, cull the cell walk
to the view frustum's world-XY rect, and build one `SubTexture2D` per distinct tile id per draw.

**Why you'd use it** — call it from a `SceneRenderDesc::DrawTransparent` hook while HDR is still
bound, or straight after `OnRender3D`. Draw precedence per sprite is `TexturePath` →
`ActiveMaterial` → flat `Color`.

**Notes & pitfalls**
- **Sprites and tilemaps use the RAW `TransformComponent`, not the hierarchy.** Parenting a sprite
  under a moving entity does not move it (`Scene.cpp:585`). Only `Rotation.z` is applied.
- **The compat gate tests for the *existence* of 2D entities, not for visible ones**
  (`Scene.cpp:590-593`). A scene whose sprites are all `Enabled = false` still enters the pass and
  issues the depth/blend state changes and an empty render pass; only a scene with **no**
  `SpriteRendererComponent` and **no** `TilemapComponent` returns before any GL call.
- Tilemap world mapping: one cell = one world unit, entity `Position` is the map's **bottom-left**
  corner, cells grow +X/+Y; rotation and scale are ignored in v1.
- Flips are applied as negative draw scale, not by swapping UVs.
- Main thread / GL. Both configurations.

#### `Scene::OnRender2DLights`

```cpp
void OnRender2DLights(const glm::mat4& viewProjection,
                      uint32_t viewportWidth, uint32_t viewportHeight);
```

**What it does** — collects every enabled, effectively-active `Light2DComponent` at its entity's raw
`Transform.Position.xy`, then composites them through `Light2DRenderer`: a half-res HDR buffer
cleared to `EnvironmentComponent::Ambient2D` and **multiplied** over the bound target.

**Notes & pitfalls**
- **Compat gate:** with no lights *and* a white `Ambient2D` the multiply is the identity, so the
  function returns before any GL call — unlit 2D output is byte-identical.
- Call it immediately after `OnRenderSprites`, on the same target.
- Ambient comes from [`FindEnvironment`](#scenefindenvironment); with no environment entity it is
  white.
- Both configurations.

#### `Scene::BuildSpriteDrawList`

```cpp
struct SpriteDrawItem { entt::entity E; int32_t Z; float Key; bool Map; };
std::vector<SpriteDrawItem> BuildSpriteDrawList();
```

**What it does** — the pure, GL-free, headless-testable function that decides 2D draw order, and the
**only** place it is decided. Sprites contribute `(ZOrder, YSort ? -Position.y : Position.z, Map=false)`
and are skipped when `!Enabled` or `!IsActiveInHierarchy`; tilemaps contribute
`(ZOrder, Position.z, Map=true)` and are skipped only on `!IsActiveInHierarchy` (they have no
`Enabled`). The list is sorted ascending by `(Z, Key, entity id)`.

**Why you'd use it** — to reproduce or test the engine's painter order without a GL context.

Both configurations.

#### `Scene::OnRender`

```cpp
void OnRender(const OrthographicCamera& camera);
```

**What it does** — the **legacy** 2D path: `Renderer2D::BeginScene`, material-bucketed quads sorted
ascending by `Position.z` within each bucket, `EndScene`. Honours `Enabled` and
`IsActiveInHierarchy`.

> **Dead code.** It has **zero callers** anywhere in the engine, the editor, the samples or the
> tests. It predates `SourceRect`, `TexturePath`, `ZOrder`, `YSort` and tilemaps, and it draws
> straight to whatever is bound rather than compositing through `SceneRenderer`. Use
> [`OnRenderSprites`](#sceneonrendersprites) in new code.

Owns its own `BeginScene`/`EndScene`. Both configurations.

#### `Scene::UpdateSpriteAnimations`

```cpp
void UpdateSpriteAnimations(float deltaTime);
```

**What it does** — for every entity with both a `SpriteAnimationComponent` and a
`SpriteRendererComponent`: accumulates `Elapsed` while `Playing`, picks the frame with
`SpriteAnimationComponent::SelectFrame`, resolves the sheet through `AssetLibrary` for its pixel
size, and writes the frame's UV into the sibling sprite's `SourceRect`.

**Failure behaviour** — if the sheet is empty, missing, or zero-sized the entity is **skipped** and
the sprite keeps its previous `SourceRect`. `Elapsed` still advances, so playback resumes in sync
once the texture arrives.

**Notes & pitfalls**
- **It does not check `Enabled` or `IsActiveInHierarchy`** — a hidden or inactive sprite's flipbook
  still advances and still overwrites `SourceRect`.
- Main thread (texture resolve). Call once per variable tick, before the 2D draw.
- Both configurations.

#### `Scene::UpdateAnimators`

```cpp
void UpdateAnimators(float deltaTime);
```

**What it does** — for every `AnimatorComponent`: resolves `ClipPath` (guarded, once per path),
finds the skinned mesh it drives (own entity or any descendant), advances the play head while
`Playing` or honours a scrubbed `NormalizedTime` while paused, blends a pending crossfade through
`AnimationClip::BlendLocals`, promotes the next clip when the fade completes, and publishes both the
skinning `Palette` and the baked-space `JointModelMatrices` that sockets read.

**Notes & pitfalls**
- A rig with no resolved clip holds its **bind pose** — `JointModelMatrices` is published so sockets
  still track, while `Palette` stays empty, which keeps the draw on the static path.
- Pure CPU, safe headless. Compat gate: a scene with no animators returns immediately.
- `Scene::OnUpdate` calls it, but **nothing calls `Scene::OnUpdate`** — `PlayerLayer` and Starforge
  call `UpdateAnimators` directly each frame and skip it while paused.
- **3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::SyncPrimitiveMeshes`

```cpp
void SyncPrimitiveMeshes();
```

**What it does** — two jobs. (1) For every `PrimitiveMeshComponent`, `get_or_emplace`s a sibling
`MeshRendererComponent` and rebuilds its `MeshAsset` whenever the parameter hash disagrees with
`BuiltSignature` — Inspector edit, undo, script or hand-edited scene, no dirty flag required. (2)
Resolves `MeshRendererComponent::MeshPath` / `MaterialPath` / `MaterialPaths` through `AssetLibrary`.

**Failure behaviour** — the path resolutions are **guarded one-shots**: the `*Resolved` flag is set
*before* the attempt, so a missing file logs **once per session**, not every frame, and the component
keeps a null asset.

**Notes & pitfalls**
- It collects handles into a vector before emplacing, precisely because `get_or_emplace` during view
  iteration is a structural change (`Scene3D.cpp:162`).
- `Segments`/`Rings` are clamped to ≥ 3 at build time, not on the field.
- Called automatically at the top of `OnRender3D` and `BuildRenderDesc`. Main thread / GL.
- **3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::SyncWorldSystems`

```cpp
void SyncWorldSystems();
```

**What it does** — rebuilds terrain, water and particle assets from their reflected recipes, gated on
`UseRecipe`.

| Family | Rebuild rule |
| --- | --- |
| `TerrainComponent` | **Auto-builds once only** — `UseRecipe && !TerrainAsset && BuiltSignature == 0`. The build is expensive, so signature-change rebuilds are the editor's explicit, JobSystem-offloaded job |
| `WaterComponent` | `UseRecipe && Enabled && IsActiveInHierarchy`, rebuilt when the asset is null **or** the recipe signature changed — `Water::Create` is GL-free, so Inspector edits apply live |
| `ParticleEmitterComponent` | Same rule as water; the GPU pool is lazy |

**Notes & pitfalls**
- **`UseRecipe` is the compat gate.** An asset assigned from code keeps `UseRecipe == false` and is
  never touched.
- Terrain's rule means editing a terrain recipe at runtime does **nothing** until something clears
  the asset or the editor drives an explicit rebuild.
- Main thread / GL (uploads assets, resolves `AssetPath` fields).
- **3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::SyncVoxelVolumes`

```cpp
void SyncVoxelVolumes(const glm::vec3& cameraPos);
```

**What it does** — per `VoxelVolumeComponent`: lazily loads the palette (`.cpal`, else the default
block set) and the volume (`.cvox`, best-effort — a miss yields an empty volume), places the volume at
`WorldOf(entity)`'s translation with `VoxelSize` metres per voxel, rebuilds the procedural atlas when
the palette version changes, procedurally generates ungenerated chunks within `ViewRadius` of
`cameraPos` when `GenEnabled` (**budget: 2 chunks per call**, nearest first), and re-meshes dirty
chunks (workers build the mesh data; the main thread uploads a bounded budget per call).

**Notes & pitfalls**
- Changing the generation recipe clears `Render->Generated` so untouched chunks re-terrain; **edited**
  chunks are kept.
- Compat gate: a scene with no `VoxelVolumeComponent` is a no-op.
- **3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::SyncNavMeshes`

```cpp
void SyncNavMeshes();
```

**What it does** — for each `NavMeshComponent` with a `SidecarPath` and no `Nav` yet, loads the
`.cnav` sidecar via `SceneNav::LoadSidecar`. **Loading only** — baking is driven by the editor /
`SceneNav`, never by this call.

**Failure behaviour** — best-effort: a missing or stale sidecar leaves the component unbaked, with no
throw.

Compat gate: a no-op without a `NavMeshComponent`. Main thread (file I/O).
**3D only, unfenced declaration** → link error in a 2D build.

#### `Scene::FindEnvironment`

```cpp
EnvironmentComponent* FindEnvironment();
```

**What it does** — returns a pointer to the **first** `EnvironmentComponent` in the registry's view
order, or `nullptr`.

**Why you'd use it** — the caller feeds it to `SceneRenderer::ApplyEnvironment`. A scene without one
renders on the engine defaults (flat clear, no sky/IBL).

**Notes & pitfalls**
- **It matches by component, not by name.** The editor's convention of one entity named
  `"Environment"` is a convention only; with two environment entities the winner is registry order
  and can change across a save/load.
- The returned pointer is invalidated by any `emplace` of an `EnvironmentComponent`. Do not cache it
  across structural changes.
- Both configurations. (`Scene::OnRender2DLights` uses it for `Ambient2D`, so it is not 3D-specific.)

---

## `Entity`

```cpp
// scene/Entity.h
class COSMIC_API Entity
{
public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene);
    Entity(const Entity& other) = default;
    ...
private:
    entt::entity m_EntityHandle{ entt::null };
    Scene*       m_Scene = nullptr;
};
```

**What it is** — a **16-byte value type**: an `entt::entity` integer plus a raw, non-owning
`Scene*`. It owns nothing. Copy it freely, pass it by value, store it in containers. Every member is
a forwarded call into the scene's registry. Both configurations.

> ### ⚠ There is no `Entity::GetUUID()`
>
> No such method exists, has ever existed, or is planned — verified by a tree-wide search for
> `GetUUID` across `Cosmic/src`, `Projects/` and `tests/`, which returns **nothing**. Documentation
> has repeatedly invented it. The identity of an entity is read from its component:
>
> ```cpp
> uint64_t id = entity.GetComponent<Cosmic::IDComponent>().ID.Value();
> ```
>
> `UUID` also converts implicitly to `uint64_t`, has `IsValid()` and `ToString()` / `FromString()`,
> and is hashable (`core/UUID.h`). Go the other way with
> [`Scene::FindByUUID`](#scenefindbyuuid).

#### `Entity::AddComponent`

```cpp
template<typename T, typename... Args>
T& AddComponent(Args&&... args)
```

**What it does** — constructs `T` in place on the entity, forwarding `args` to its constructor, and
returns a reference to it.

**Failure behaviour** — **adding a component the entity already has does not fail and does not
assert.** It logs `CS_CORE_WARN("Entity::AddComponent: entity already owns this component type …")`
and returns the **existing** component, **discarding your constructor arguments**. This guard is
always active — it is not gated on `CS_ENABLE_ASSERTS` — because forwarding a second `emplace` to
EnTT trips a sparse-set assertion and aborts the process.

**Example**

```cpp
crate.AddComponent<Cosmic::RigidBodyComponent>(Cosmic::MotionType::Dynamic);
auto& box = crate.AddComponent<Cosmic::BoxColliderComponent>();
box.HalfExtents = { 0.5f, 0.5f, 0.5f };
```

**Notes & pitfalls**
- **Any `emplace` may reallocate that component type's pool, invalidating references to *other*
  components of the same type.** Re-fetch after every structural change:
  ```cpp
  auto& a = e1.AddComponent<Cosmic::PointLightComponent>();
  auto& b = e2.AddComponent<Cosmic::PointLightComponent>();   // `a` may now dangle
  a.Intensity = 2.0f;                                         // ← undefined
  ```
- **Empty (tag) types share one instance.** For a `T` with `std::is_empty_v<T>` — such as
  `SelectableComponent` or `TerrainColliderComponent` — EnTT stores no data, so the entity is marked
  and the returned reference is a **process-lifetime static sentinel**. Presence is the entire
  signal; never write through that reference expecting per-entity storage.
- If "add if missing" is what you actually mean, use `GetOrAddComponent` — it does the same thing
  without the warning.

#### `Entity::GetOrAddComponent`

```cpp
template<typename T, typename... Args>
T& GetOrAddComponent(Args&&... args)
```

**What it does** — returns the existing component if present, otherwise forwards to `AddComponent`.
Never warns. Same pool-reallocation caveat as `AddComponent` (stated in the header at
`Entity.h:62-65`).

#### `Entity::GetComponent`

```cpp
template<typename T> T&       GetComponent();
template<typename T> const T& GetComponent() const;
```

**What it does** — returns a mutable (or const) reference to the component.

**Failure behaviour — read this carefully.** Calling it on a component the entity does **not** have
is **unchecked in practice**. The `CS_ASSERT(HasComponent<T>(), …)` on `Entity.h:84` and `:104` is
compiled out in every configuration. What actually fires is EnTT's own `ENTT_ASSERT` inside the
component pool, which is a plain `assert()`: it **aborts in a Debug build** and **compiles away under
`NDEBUG` in Release**, where the read is undefined behaviour. There is no return value or log that
tells you.

**Guard it:**

```cpp
if (crate.HasComponent<Cosmic::RigidBodyComponent>())
    crate.GetComponent<Cosmic::RigidBodyComponent>().GravityFactor = 0.0f;

// or, cleaner for optional reads — returns nullptr on absence:
if (auto* rb = scene->GetRegistry().try_get<Cosmic::RigidBodyComponent>((entt::entity)crate))
    rb->GravityFactor = 0.0f;
```

**Notes & pitfalls**
- For an empty type it returns the shared static sentinel, exactly as `AddComponent` does.
- The reference is invalidated by any structural change to that type's pool.

#### `Entity::HasComponent`

```cpp
template<typename T> bool HasComponent() const
```

Forwards to `registry.all_of<T>(handle)`. Safe on any handle that is still valid; **not** safe on a
destroyed entity (test `operator bool` first).

#### `Entity::RemoveComponent`

```cpp
template<typename T> void RemoveComponent()
```

**What it does** — `registry.remove<T>(handle)`.

**Failure behaviour** — the `CS_ASSERT` above it is compiled out, but EnTT's `registry::remove` is
the **tolerant** variant (it returns the number of elements actually removed, unlike `erase`), so
removing a component the entity does not have is a **safe no-op**. Calling it on a *destroyed* entity
is not — that is an invalid identifier.

#### Conversions and comparison

```cpp
operator bool() const;              // m_Scene != nullptr && handle != entt::null && registry.valid(handle)
operator entt::entity() const;
operator uint32_t() const;
bool operator==(const Entity& other) const;   // same handle AND same scene
bool operator!=(const Entity& other) const;
```

**Notes & pitfalls**
- `operator bool` checks **all three** conditions, so a copy held after `DestroyEntity` evaluates to
  `false` rather than aliasing a recycled slot. It does **not** protect `GetComponent`.
- `operator uint32_t` is the raw registry index — it is what render passes write into the entity-ID
  attachment. It is **not** the UUID, is not stable across a save/load, and is recycled.

---

## Components — `scene/Components.h` (19, both configurations)

**These 19 exist in every engine build, unchanged.** The 15 in
[`Components3D.h`](#components--scenecomponents3dh-15-3d-only) do not.

**How to read these tables.**
*Type* and *Default* are copied from the header — that is exactly what `AddComponent<T>()` gives you.
*Reflected as* is the name from `reflect/TypeRegistry.cpp`; a field marked **—** is **not reflected**,
meaning it is neither shown in the Inspector nor written to a `.cscene` unless the serializer
special-cases it. **runtime** marks an unreflected field the engine recomputes. **hidden** =
`HideInInspector()` (reflected, so it serializes, but no Inspector row). **omit-if-true** =
`OmitIfTrue()` (skipped in serialization while `true`, so unchanged scenes stay byte-identical).
*Read by* names the code that actually consumes the field.

Every one of the 19 is `COSMIC_API`-exported, default-constructible, copy-constructible, and
registered for a stable cross-DLL type id via `CS_REGISTER_COMPONENT` at the bottom of the header.

### `IDComponent` — stable identity

```cpp
struct COSMIC_API IDComponent
{
    UUID ID;
    IDComponent(UUID id);   // + default + copy
};
```

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `ID` | `UUID` | fresh random (a default-constructed `UUID` is **never 0**) | — (not reflected) | Stable identity across save/load |

**Not a reflected class at all** — the serializer writes it as the per-entity `"id"` key, not a
component block. **Read by** `Scene::FindByUUID` (the index), `RelationshipComponent` links,
`EntityRef` script fields, `PrefabComponent` resolution, and `ScenePhysics` (`BodyDesc::EntityId`,
which is how contact events and raycast hits map back to entities).

### `RelationshipComponent` — parent/child links

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `Parent` | `UUID` | `UUID{ 0 }` (root) | — | Back-link; `0` means no parent |
| `Children` | `std::vector<UUID>` | empty | — | Ordered, authoritative child list |

Present **only** on entities that participate in a hierarchy. Not reflected; the serializer handles
it structurally and preserves `Children` order. **Read by** `Scene::WorldOf`/`GetWorldTransform`,
`IsActiveInHierarchy`, `IsAncestor`, `DestroyEntity`, `Scene::FindAnimatorFor`,
`ScenePhysics::WriteBackWorldPose`.

> **Mutate only through [`Scene::SetParent`](#scenesetparent).** It keeps both sides consistent and
> refuses cycles. Also note `Parent` **must** default to `UUID(0)`: a default-constructed `UUID` is
> **random**, so a hand-emplaced `RelationshipComponent{}` with an uninitialised parent would point
> at nothing and silently detach the entity. The header pins this at `Components.h:75-76`.

### `TagComponent` — name + per-entity active flag

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `Tag` | `std::string` | `""` (set by `CreateEntity`) | `Tag.Tag` | Display name; the Hierarchy label |
| `Active` | `bool` | `true` | `Tag.Active` — **hidden, omit-if-true** | Per-entity enable (T13) |

Registered class name is **`Tag`**, category `Core`. Converting constructor:
`TagComponent(const std::string& tag)`. `Active` is driven by the Hierarchy panel's eye toggle rather
than an Inspector row. **Read by** [`Scene::IsActiveInHierarchy`](#sceneisactiveinhierarchy), which
nearly every pass consults.

### `TransformComponent` — placement

| Field | Type | Default | Reflected as | Units | Meaning |
| --- | --- | --- | --- | --- | --- |
| `Position` | `glm::vec3` | `{0,0,0}` | `Transform.Position` | world units (metres in 3D) | Local translation |
| `Rotation` | `glm::vec3` | `{0,0,0}` | `Transform.Rotation` (`.Degrees()`) | **degrees** | Euler X, Y, Z; Z is 2D roll |
| `Scale` | `glm::vec3` | `{1,1,1}` | `Transform.Scale` | multiplier | Per-axis scale (was `vec2` before S4.3) |
| `RotationQuat` | `glm::quat` | identity `{1,0,0,0}` = `(w,x,y,z)` | `Transform.RotationQuat` | — | Used only when `UseQuatRotation` |
| `UseQuatRotation` | `bool` | `false` | `Transform.UseQuatRotation` | — | Switches `GetTransform()` to the quaternion |

Registered as **`Transform`**, category `Core`. Converting constructor:
`TransformComponent(const glm::vec3& position)`.

#### `TransformComponent::GetTransform`

```cpp
glm::mat4 GetTransform() const
```

**What it does** — returns `translate(Position) · R · scale(Scale)`, where `R` is
`glm::mat4_cast(RotationQuat)` when `UseQuatRotation` is set, otherwise the Euler product
`rotate(X) · rotate(Y) · rotate(Z)` in that order.

**Notes & pitfalls**
- **The two rotation representations are independent — writing one does not sync the other.** Pick
  one per entity; there are no Euler↔quat helpers on the component.
- **Two engine paths flip `UseQuatRotation` to `true` behind your back:**
  `ScenePhysics::WriteBackWorldPose` (every dynamic body and character, every fixed step) and
  `Scene::SetParent(…, keepWorldPose = true)`. After either, the Euler `Rotation` field is stale and
  ignored. Read poses from [`GetWorldTransform`](#scenegetworldtransform), not from `Rotation`.
- **`GetTransform()` applies all three Euler angles, but the 2D draws pass only `Rotation.z`.** An
  entity with a non-zero X or Y rotation renders differently in 2D than its matrix says
  (`Components.h:127-131`).
- Covered by `tests/test_components.cpp` — vec3 scale on the diagonal, quat matching the Euler
  product, and `UseQuatRotation` defaulting off.

### `PrefabComponent`

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `SourcePath` | `std::string` | `""` | `Prefab.SourcePath` (`AssetPath("prefab")`) | e.g. `"project://prefabs/Foo.cprefab"` |

Registered as **`Prefab`**, category `Core`. Converting constructor from `const std::string&`.
No per-field override tracking in v1 — an instance is a plain detached copy that remembers its
origin. **Read by** the editor's "Revert to Prefab"; prefabs are
[`../guide/scenes-and-serialization.md`](../guide/scenes-and-serialization.md)'s topic.

### `OpaqueComponentsComponent` — forward-compatibility store

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `Blocks` | `std::vector<std::pair<std::string, std::string>>` | empty | — | `(component name → verbatim JSON text)` |

**Never attach this by hand.** When a scene loads a component block whose type is not registered in
this build, the serializer parks the raw JSON here and re-emits it unchanged on save. It is why a 3D
scene can be opened, edited and saved by a **2D** editor without losing a single 3D block. **Read by**
`SceneSerializer` only. Not reflected.

### `SpriteRendererComponent`

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `ActiveMaterial` | `Ref<Material>` | `nullptr` | — **runtime** | Legacy material path |
| `Color` | `glm::vec4` | `{1,1,1,1}` | `SpriteRenderer.Color` (`.Color()`) | Tint, or the flat quad colour when untextured |
| `FlipX` / `FlipY` | `bool` | `false` | `SpriteRenderer.FlipX` / `.FlipY` | Applied as negative draw scale |
| `SourceRect` | `glm::vec4` | `{0,0,1,1}` | `SpriteRenderer.SourceRect` | Sampled sub-rect in **normalized UV** `{u0,v0,u1,v1}`, V top-left origin |
| `PixelsPerUnit` | `float` | `100.0` | `SpriteRenderer.PixelsPerUnit` (range 1–4096) | Texels per world unit |
| `ZOrder` | `int32_t` | `0` | `SpriteRenderer.ZOrder` | Primary painter key within the 2D pass |
| `TexturePath` | `std::string` | `""` | `SpriteRenderer.TexturePath` (`AssetPath("texture")`) | The modern path |
| `YSort` | `bool` | `false` | `SpriteRenderer.YSort` | Within a `ZOrder`, sort by `-Position.y` instead of `Position.z` |
| `Enabled` | `bool` | `true` | `SpriteRenderer.Enabled` — **hidden, omit-if-true** | `false` skips the sprite |
| `Resolved` | `Ref<Texture2D>` | `nullptr` | — **runtime** | Lazy texture cache |
| `ResolvedPath` | `std::string` | `""` | — **runtime** | The path `Resolved` was loaded from |

Registered as **`SpriteRenderer`**, category `Rendering`. Converting constructors from
`const Ref<Material>&` and from `const glm::vec4&`. **Read by**
[`BuildSpriteDrawList`](#scenebuildspritedrawlist) / [`OnRenderSprites`](#sceneonrendersprites),
`Scene::UpdateSpriteAnimations` (which **overwrites** `SourceRect`), and the dead legacy
`Scene::OnRender`. Draw precedence: `TexturePath` → `ActiveMaterial` → flat `Color`.

#### `SpriteRendererComponent::WorldSize`

```cpp
static glm::vec2 WorldSize(const SpriteRendererComponent& s,
                           const glm::vec2& scale, int texW, int texH)
```

**What it does** — the one sizing rule, shared by the draw and by editor picking/outlines. Textured
(`texW > 0 && texH > 0`): `(SourceRect texels / PixelsPerUnit) × scale`. Untextured: returns `scale`
unchanged — the scale **is** the size (legacy quad behaviour).

**Notes & pitfalls**
- **Unsigned.** Flips are applied by the caller as negative scale, not here.
- A `PixelsPerUnit <= 0` is treated as `1.0` rather than dividing by zero.
- Pure and header-inline — safe headless.

### `SpriteAnimationComponent` — flipbook

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `SheetPath` | `std::string` | `""` | `SpriteAnimation.SheetPath` (`AssetPath("texture")`) | The sprite sheet |
| `FrameW` / `FrameH` | `int32_t` | `16` / `16` | `.FrameW` / `.FrameH` (range 1–4096) | Cell size in **texels** |
| `Frames` | `int32_t` | `1` | `.Frames` (range 1–4096) | Number of cells played along `Row` |
| `Row` | `int32_t` | `0` | `.Row` (range 0–4096) | 0-based row; **row 0 is the TOP of the sheet** |
| `FPS` | `float` | `8.0` | `.FPS` (range 0–120) | Playback rate |
| `Playing` | `bool` | `true` | `.Playing` | Advance the clock |
| `Loop` | `bool` | `true` | `.Loop` | Wrap vs clamp to the last frame |
| `Elapsed` | `float` | `0.0` | — **runtime** | Accumulated seconds |

Registered as **`SpriteAnimation`**, category `Rendering`. **Read by**
[`Scene::UpdateSpriteAnimations`](#sceneupdatespriteanimations).

#### `SpriteAnimationComponent::SelectFrame`

```cpp
static int SelectFrame(float elapsed, float fps, int frames, bool loop)
```

Returns the frame index at `elapsed` seconds. `frames <= 1` or `fps <= 0` returns `0`. Looping wraps
(and handles negative `elapsed` correctly via `((f % frames) + frames) % frames`); one-shot clamps to
`[0, frames-1]`. Pure; pinned by `tests/test_scene_components.cpp:130`.

#### `SpriteAnimationComponent::FrameUV`

```cpp
static glm::vec4 FrameUV(int texW, int texH, int frameW, int frameH,
                         int row, int frame)
```

Returns the normalized `{u0,v0,u1,v1}` of a cell, **V top-left origin** (row 0 is the top). Any
non-positive dimension returns the whole-image rect `{0,0,1,1}` rather than dividing by zero. Pure;
pinned by `tests/test_scene_components.cpp:147`.

### `TilemapComponent` — tile grid

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `TilesetPath` | `std::string` | `""` | `Tilemap.TilesetPath` (`AssetPath("texture")`) | The tile atlas |
| `TileW` / `TileH` | `int32_t` | `16` / `16` | `.TileW` / `.TileH` (range 1–1024) | Atlas tile size in texels |
| `Columns` | `int32_t` | `0` | `.Columns` (range 0–1024) | Atlas columns; `0` derives from texture width / `TileW` |
| `GridW` / `GridH` | `int32_t` | `32` / `32` | `.GridW` / `.GridH` (range 1–1024) | Map size in cells |
| `ZOrder` | `int32_t` | `0` | `.ZOrder` | Painter key, shared with sprites |
| `Cells` | `std::vector<uint16_t>` | empty | — (serializer special case) | Row-major `[y*GridW + x]`, **`y = 0` is the bottom row**. `0` = empty, `v > 0` = atlas tile `v - 1` |
| `Resolved` / `ResolvedPath` | `Ref<Texture2D>` / `std::string` | `nullptr` / `""` | — **runtime** | Lazy atlas cache |

Registered as **`Tilemap`**, category `Rendering`. `static constexpr int32_t kMaxGrid = 1024;`
World mapping: **one cell = one world unit**; the entity's `Position` is the map's **bottom-left**
corner; cells grow +X/+Y; entity rotation and scale are ignored in v1. `Cells` is written by
`SceneSerializer` as a plain integer array (diff-friendly), not as a reflected field.

> **`TilemapComponent` has no `Enabled` field.** Unlike sprites, the only way to switch a tilemap off
> is the entity's `TagComponent::Active`.

**Member functions**

```cpp
void EnsureCells();                        // clamp GridW/GridH to 1..kMaxGrid, resize Cells (new cells = 0)
bool InBounds(int x, int y) const;
uint16_t At(int x, int y) const;           // 0 when out of bounds or past the end of Cells
static std::vector<uint32_t> FloodFill(std::vector<uint16_t>& cells,
                                       int gridW, int gridH,
                                       int x, int y, uint16_t value);
```

`FloodFill` is a pure 4-connected fill shared by the editor's fill tool and its tests: it grows
`cells` to `gridW*gridH` if short, fills the connected region of the value at `(x, y)` with `value`,
and **returns the changed indices** — empty when the seed is out of bounds or the region already holds
`value`. **Read by** `BuildSpriteDrawList` / `OnRenderSprites`, which walk only the cells inside the
camera's world rect and build one `SubTexture2D` per distinct tile id per draw.

### `Light2DComponent` — additive 2D point light

| Field | Type | Default | Reflected as | Units | Meaning |
| --- | --- | --- | --- | --- | --- |
| `Color` | `glm::vec3` | `{1, 0.85, 0.6}` | `Light2D.Color` (`.Color()`) | linear RGB | Warm campfire default |
| `Radius` | `float` | `4.0` | `.Radius` (0–100, metres) | world units | Reach |
| `Intensity` | `float` | `1.5` | `.Intensity` (0–20) | HDR | Brightness at the centre |
| `Falloff` | `float` | `2.0` | `.Falloff` (0.1–8) | exponent | Higher = tighter |
| `Enabled` | `bool` | `true` | `.Enabled` — **omit-if-true, but NOT hidden** | — | Visible Inspector row, unlike every other `Enabled` |

Registered as **`Light2D`**, category `Rendering`. **Read by**
[`Scene::OnRender2DLights`](#sceneonrender2dlights). The light sits at the entity's **raw**
`Transform.Position.xy`. Normal-mapped 2D lights are out of scope in v1. Defaults and the serializer
round-trip are pinned by `tests/test_scene_components.cpp:190`.

### `CameraComponent`

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `Primary` | `bool` | `true` | `Camera.Primary` | First primary camera wins |
| `ProjectionType` | `Projection` | `Perspective` | `.ProjectionType` (enum `Perspective=0`, `Orthographic=1`) | — |
| `FovDeg` | `float` | `60.0` | `.FovDeg` (10–170, degrees) | Vertical FOV (perspective) |
| `Near` / `Far` | `float` | `0.1` / `1000.0` | `.Near` (0.001–100) / `.Far` (1–100000), metres | Clip planes |
| `OrthoSize` | `float` | `10.0` | `.OrthoSize` (0.1–1000) | Half-**height** in world units (orthographic) |

Registered as **`Camera`**, category `Rendering`. `enum class Projection { Perspective = 0,
Orthographic = 1 };` Position and orientation come from the entity's `TransformComponent`; this holds
only the projection.

#### `CameraComponent::GetProjection`

```cpp
glm::mat4 GetProjection(float aspect) const
```

Returns `glm::perspective(radians(FovDeg), aspect, Near, Far)` or
`glm::ortho(-OrthoSize*aspect, OrthoSize*aspect, -OrthoSize, OrthoSize, Near, Far)`. Pinned by
`tests/test_scene_components.cpp:30`.

**Read by** `PlayerLayer::UpdateCamera` (`view = inverse(worldTransform)`; a warning fires **once**
and a fixed ¾ view is used when no primary camera exists), Starforge's Play mode and its "adopt
camera pose on scene open", and `EditorCameraRig`'s read-only Possess mode.

### `EnvironmentComponent` — the scene's rendering environment

Registered as **`Environment`**, category `Rendering`. The editor keeps exactly one entity (named
`"Environment"`) carrying this; [`Scene::FindEnvironment`](#scenefindenvironment) returns the first
one found. **Every field defaults to the current `SceneRenderer` default**, so a scene *without* one
renders exactly as it did before this component existed — pinned by
`tests/test_scene_components.cpp:50`. **Read by** `SceneRenderer::ApplyEnvironment` (called by the
**host**, not by the scene) and by `Scene::OnRender2DLights` for `Ambient2D`.

| Field | Type | Default | Range (Inspector) | Meaning |
| --- | --- | --- | --- | --- |
| `SunDirection` | `vec3` | `{-0.4,-1,-0.3}` | — | Direction the light **travels** |
| `SunColor` | `vec3` | `{1,1,1}` | colour | — |
| `SunIntensity` | `float` | `1.0` | 0–10 | — |
| `Sky` | `SkyMode` | `Procedural` | enum | `Procedural=0`, `Detailed=1`, `HDRI=2`, `Physical=3` |
| `HdriPath` | `string` | `""` | `AssetPath("hdri")` | Equirectangular `.hdr`; used when `Sky == HDRI` |
| `Turbidity` | `float` | `2.5` | 1–10 | **Physical only.** Scales Mie density (1 pristine … 10 smoggy) |
| `RayleighScale` | `float` | `1.0` | 0–4 | **Physical only.** Blue-scattering scale |
| `MieScale` | `float` | `1.0` | 0–4 | **Physical only.** White-haze / sun-halo scale |
| `MieG` | `float` | `0.80` | 0–0.99 | **Physical only.** Mie phase asymmetry |
| `TimeOfDay` | `float` | `12.0` | 0–24 | Hours (sun scrub) |
| `Skybox` | `bool` | `true` | — | Draw the sky |
| `IBL` | `bool` | `true` | — | Image-based lighting from the environment cube |
| `IBLIntensity` | `float` | `1.0` | 0–4 | — |
| `Exposure` | `float` | `1.0` | 0–8 | Tonemap exposure |
| `AmbientIntensity` | `float` | `1.0` | 0–4 | Scales the ambient/IBL term (1 = unchanged) |
| `Gamma` | `float` | `2.2` | 1–3 | Tonemap output gamma |
| `SunAngularSize` | `float` | `0.53` | 0.1–10 | Sun-disc **diameter** in degrees (Detailed/Physical). Real sun ≈ 0.53 |
| `Ambient2D` | `vec3` | `{1,1,1}` | colour | 2D light-buffer clear colour; **white = no darkening** |
| `Fog` | `bool` | `false` | — | Height fog on |
| `FogColor` | `vec3` | `{0.70,0.80,0.92}` | colour | — |
| `FogDensity` | `float` | `0.02` | 0–1 | — |
| `FogHeightFalloff` | `float` | `0.12` | 0–2 | — |
| `FogBaseHeight` | `float` | `0.0` | metres | World Y |
| `Bloom` | `bool` | `false` | — | — |
| `BloomThreshold` | `float` | `1.0` | 0–8 | — |
| `BloomIntensity` | `float` | `0.6` | 0–4 | — |
| `SSAO` | `bool` | `false` | — | — |
| `SsaoRadius` | `float` | `0.5` | 0–4 | — |
| `FXAA` | `bool` | **`true`** | — | The one post effect that is **on** by default |
| `LensFlare` | `bool` | `false` | — | — |
| `LensFlareIntensity` | `float` | `0.35` | 0–2 | — |
| `Vignette` | `bool` | `false` | — | Post-tonemap edge darkening |
| `VignetteAmount` | `float` | `0.35` | 0–1 | Blend strength |
| `VignetteRadius` | `float` | `0.9` | 0–1.5 | — |
| `VignetteFeather` | `float` | `0.4` | 0–1 | — |
| `VignetteColor` | `vec3` | `{0,0,0}` | colour | Edge colour |

**Notes & pitfalls**
- The physical-atmosphere block (`Turbidity`, `RayleighScale`, `MieScale`, `MieG`) is ignored by
  every sky mode except `Physical`, and the `Vignette` block is a no-op while `Vignette` is false.
  Both were added under a byte-identical-output constraint.
- `Ambient2D` is the one field a pure-2D project cares about — it is the only field this component
  contributes in the 2D configuration.

### Scripts

#### `NativeScriptComponent`

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `ClassName` | `std::string` | `""` | `NativeScript.ClassName` | Registered script class (`CS_SCRIPT` / `ModuleRegistry`) |
| `Instance` | `ScriptableEntity*` | `nullptr` | — **runtime**, owned by `ScriptHost` | The live script object |
| `Fields` | `std::unordered_map<std::string, Reflect::FieldValue>` | empty | — (serializer special case) | Reflected per-instance overrides |

Registered as **`NativeScript`**, category `Scripts`. Converting constructor from
`const std::string&`. `Fields` is (de)serialized out-of-band against the script's descriptor.
**Read by** `ScriptHost::Bind` (resolve the class → construct → push fields → `OnCreate` on everyone
→ `OnStart` on everyone) and `ScriptHost::Tick`/`FixedTick`.

**Failure behaviour** — an unknown class name **warns once and leaves the entity inert**; nothing
throws and the component is not modified.

> `ScriptHost::Bind` does **not** consult `IsActiveInHierarchy`. An inactive entity's script is still
> constructed and still receives `OnCreate` and `OnStart`; only the per-frame ticks are skipped.

#### `SystemScriptComponent`

Same three fields (`ClassName`, `void* Instance`, `Fields`), registered as **`SystemScript`**,
category `Systems`. The class is a `SystemScript` registered with `CS_SYSTEM`, and there is **one
instance per component** that receives the whole matching entity set each tick via `OnUpdateAll` /
`OnFixedUpdateAll`. Systems are resolved after per-entity scripts and run **before** them each tick.
Hold it on any single entity — it is scene-level logic. Full treatment:
[`../guide/scripting.md`](../guide/scripting.md).

### Physics — shared, because Jolt ships in both configurations

A body is a `RigidBodyComponent` **plus at least one collider on the same entity** (multiple colliders
compound into one shape). **A collider without a rigid body is an implicit static body** — that is how
you build ground and world geometry. All of these are read by `ScenePhysics::BuildColliderDesc` /
`BuildBodies`, and the collider set is *also* read by `SceneNav` when a navmesh bake gathers
geometry. Runtime behaviour: [physics.md](physics.md).

#### `RigidBodyComponent`

| Field | Type | Default | Reflected as (range) | Units | Meaning |
| --- | --- | --- | --- | --- | --- |
| `Motion` | `MotionType` | `Static` | `RigidBody.Motion` (enum) | — | `Static=0` world/ground, `Kinematic=1` script-moved, `Dynamic=2` simulated |
| `Mass` | `float` | `1.0` | `.Mass` (0.001–10000) | kg | Dynamic only |
| `Friction` | `float` | `0.5` | `.Friction` (**0–2**) | — | See the pitfall |
| `Restitution` | `float` | `0.1` | `.Restitution` (0–1) | — | 0 = no bounce, 1 = elastic |
| `LinearDamping` | `float` | `0.05` | `.LinearDamping` (0–10) | — | — |
| `AngularDamping` | `float` | `0.05` | `.AngularDamping` (0–10) | — | — |
| `GravityFactor` | `float` | `1.0` | `.GravityFactor` (0–4) | multiplier | 0 = floats |
| `CCD` | `bool` | `false` | `.CCD` | — | Continuous collision for fast small bodies |
| `StartAsleep` | `bool` | `false` | `.StartAsleep` | — | — |
| `CollisionCategory` | `uint32_t` | `0x0001` | `.CollisionCategory` | bits | Stored as 16-bit; `uint32_t` for reflection |
| `CollidesWith` | `uint32_t` | `0xFFFF` | `.CollidesWith` | mask | — |

Registered as **`RigidBody`**, category `Physics`. Converting constructor `RigidBodyComponent(MotionType m)`.

**Notes & pitfalls**
- **The collision filter is two-sided:** two bodies collide iff **each one's category is in the
  other's mask**. Setting only one side does nothing.
- **The header comment on `Friction` says `// 0..1` (`Components.h:511`) and is wrong** — the
  reflected slider is `.Range(0.0f, 2.0f)`, and Jolt friction is not capped at 1. Trust the range.
- A `RigidBodyComponent` with **no collider** logs `"RigidBody on entity N has no collider — no body
  created."` and produces nothing.

#### `BoxColliderComponent` / `SphereColliderComponent` / `CapsuleColliderComponent`

| Component (registered as) | Fields — type, default, reflected range |
| --- | --- |
| `BoxColliderComponent` (`BoxCollider`) | `HalfExtents` `vec3{0.5,0.5,0.5}` · `Offset` `vec3{0}` · `IsTrigger` `false` · `Enabled` `true` (**hidden, omit-if-true**) |
| `SphereColliderComponent` (`SphereCollider`) | `Radius` `float 0.5` m (0.001–1000) · `Offset` `vec3{0}` · `IsTrigger` `false` · `Enabled` `true` (**hidden, omit-if-true**) |
| `CapsuleColliderComponent` (`CapsuleCollider`) | `Radius` `float 0.5` m (0.001–1000) · `HalfHeight` `float 0.5` m (0–1000) · `Offset` `vec3{0}` · `IsTrigger` `false` · `Enabled` `true` (**hidden, omit-if-true**) |

**Notes & pitfalls**
- **Extents and radii are pre-scale**: the entity's decomposed world scale is baked into the shape at
  build time.
- `HalfHeight` is half the **cylinder** section only, excluding the two hemispherical caps; the
  capsule is Y-axis aligned.
- A sphere on a **non-uniformly scaled** entity warns once and uses the X scale (`Components.h:542`).
- `IsTrigger` makes the shape a sensor (overlap events, no contact response); if *any* collider on the
  entity is a trigger, the whole body is one.
- **`Enabled = false` skips that collider at bake time only** — it has no effect on a session already
  running.

#### `CharacterControllerComponent`

| Field | Type | Default | Reflected as (range) | Units |
| --- | --- | --- | --- | --- |
| `Height` | `float` | `1.8` | `CharacterController.Height` (0.2–10) | m, total capsule height **including** caps |
| `Radius` | `float` | `0.3` | `.Radius` (0.05–5) | m |
| `MaxSlopeDeg` | `float` | `45.0` | `.MaxSlopeDeg` (0–89) | degrees |
| `StepHeight` | `float` | `0.35` | `.StepHeight` (0–2) | m |
| `Mass` | `float` | `80.0` | `.Mass` (1–1000) | kg |

A kinematic capsule with slope and step handling (Jolt `CharacterVirtual`). **It owns its own
capsule** — it needs neither a `RigidBodyComponent` nor a collider, and `BuildBodies` handles it in a
separate first pass, skipping such entities in the rigid-body pass. Gravity is set to −9.81 m/s² at
creation. Drive it from a script through the `Character()` proxy ([physics.md](physics.md)).

---

## Components — `scene/Components3D.h` (15, 3D only)

**Everything in this section is absent from a 2D build.** `Components3D.h` is dropped from the
build outright (`Cosmic/CMakeLists.txt:202`), and `Cosmic.h` includes it behind the same
`#ifndef COSMIC_2D_ONLY` fence (`Cosmic.h:112-114`), so a 2D engine never compiles a line of it.
**Naming any of these types in a 2D build is a compile error, not a silent no-op.** In a 3D build, a
translation unit that names one must `#include "scene/Components3D.h"` — `Components.h` alone no
longer declares them; including `<Cosmic.h>` does it for you. The mechanism:
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

Struct bodies, field order, defaults and registered names are the pre-split text **verbatim**, so
type ids and serialized scenes are unaffected in either direction —
`tests/test_components3d_registry.cpp` pins the names and the `entt::type_hash` values.

Their reflection lives in `reflect/TypeRegistry3D.cpp`, which `RegisterEngineTypes` calls behind the
same fence.

### `MeshRendererComponent`

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `MeshAsset` | `Ref<Mesh>` | `nullptr` | — **runtime** | Entity is skipped when null |
| `MaterialAsset` | `Ref<Material>` | `nullptr` | — **runtime** | Null → the Lambert `Color` path |
| `Color` | `glm::vec4` | `{1,1,1,1}` | `MeshRenderer.Color` (`.Color()`) | Lambert tint used when no material resolves |
| `CastShadows` | `bool` | `true` | `.CastShadows` | Consulted by the depth-only passes |
| `Enabled` | `bool` | `true` | `.Enabled` — **hidden, omit-if-true** | Hides the mesh in *every* pass, shadows included |
| `MeshPath` | `std::string` | `""` | `.MeshPath` (`AssetPath("mesh")`) | Resolved once via `AssetLibrary::GetMesh` |
| `MeshPathResolved` | `bool` | `false` | — **runtime** | One-shot resolution guard |
| `MaterialPath` | `std::string` | `""` | `.MaterialPath` (`AssetPath("material")`) | Resolved once via `AssetLibrary::GetMaterial` |
| `MaterialPathResolved` | `bool` | `false` | — **runtime** | One-shot guard |
| `MaterialPaths` | `std::vector<std::string>` | empty | — (serializer special case) | Per-submesh material slots, indexed by `Submesh::MaterialIndex` |
| `MaterialAssets` | `std::vector<Ref<Material>>` | empty | — **runtime** | Parallel to `MaterialPaths` |
| `MaterialPathsResolved` | `bool` | `false` | — **runtime** | One-shot guard |

Registered as **`MeshRenderer`**, category `Rendering`. Converting constructor from `const Ref<Mesh>&`.
Defaults pinned by `tests/test_components.cpp:69`.

**Read by** `Scene::SyncPrimitiveMeshes` (path resolution) and `Scene::SubmitOpaqueMeshes`, which
picks one of three paths per entity:

1. **skinned** — mesh `IsSkinned()`, a `MaterialAsset` is set, and an animator in the parent chain has
   a live palette of exactly `Skeleton::JointCount()` entries → `DrawMeshSkinned`;
2. **multi-material** — not skinned, `MaterialAssets` non-empty, mesh `HasSubmeshes()` → one
   `DrawMeshRange` per submesh (depth-only passes collapse this to a single whole-mesh caster);
3. **single** — `DrawMesh` with the material, or with `Color` if none.

An empty `MaterialPaths` keeps path 3 **byte-identical** to the pre-slots behaviour — that is the
compat gate. A slot whose path is empty or unresolved falls back to `MaterialAsset`, then to `Color`.

### `PrimitiveMeshComponent` — parametric shape

| Field | Type | Default | Reflected as (range) | Meaning |
| --- | --- | --- | --- | --- |
| `ShapeType` | `Shape` | `Box` | `PrimitiveMesh.ShapeType` (enum) | `Box=0, Sphere=1, Plane=2, Cylinder=3, Cone=4, Torus=5` |
| `Size` | `glm::vec3` | `{1,1,1}` | `.Size` | Box: **full** extents. Plane: X = width, Z = depth |
| `Radius` | `float` | `0.5` | `.Radius` (0.01–100) | Sphere/Cylinder/Cone radius; Torus **ring** radius |
| `Height` | `float` | `1.0` | `.Height` (0.01–100) | Cylinder/Cone height |
| `TubeRadius` | `float` | `0.2` | `.TubeRadius` (0.01–50) | Torus tube radius |
| `Segments` | `int32_t` | `24` | `.Segments` (3–256) | Radial / longitude subdivisions |
| `Rings` | `int32_t` | `16` | `.Rings` (3–256) | Sphere latitude bands / Torus tube sides |
| `BuiltSignature` | `std::size_t` | `0` | — **runtime** | Hash of the params the current mesh was built from |

Registered as **`PrimitiveMesh`**, category `Rendering`. Converting constructor from `Shape`. The
scene stores the shape, **never the mesh**. `Segments`/`Rings` are clamped to ≥ 3 **at build**, not on
the field. **Read by** [`Scene::SyncPrimitiveMeshes`](#scenesyncprimitivemeshes), which
`get_or_emplace`s a sibling `MeshRendererComponent` (so you never have to add one).

### `LODGroupComponent` — distance-switched level of detail

```cpp
struct Level { Ref<Mesh> MeshAsset; float MaxDistance = 25.0f; };
```

| Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- |
| `Levels` | `std::vector<Level>` | empty | — **not reflected, not serialized** | Ordered nearest → farthest |
| `MaterialAsset` | `Ref<Material>` | `nullptr` | — **runtime** | Shared by all levels; null → Lambert |
| `Color` | `glm::vec4` | `{1,1,1,1}` | `LODGroup.Color` (`.Color()`) | Lambert tint |
| `CastShadows` | `bool` | `true` | `LODGroup.CastShadows` | — |

Registered as **`LODGroup`**, category `Rendering`.

> **`Levels` is neither reflected nor special-cased by the serializer.** Only `Color` and
> `CastShadows` are registered. **LOD levels must be assigned from C++ and do not survive a scene
> save/load.** The worked example is `Projects/Engine3DDemo/src/Engine3DDemo.cpp:400`.

#### `LODGroupComponent::SelectLevel`

```cpp
static int SelectLevel(const std::vector<Level>& levels, float distance)
```

Returns the index of the first level whose `MaxDistance >= distance`, or **`-1`** when the distance is
beyond every level (a built-in distance cull) or there are no levels. Pure and unit-tested.

**Read by** `Scene::SubmitOpaqueMeshes`, which selects the level from the **real** camera distance in
every pass — so a caster always matches its lit mesh. Switches are hard cuts; cross-fade is a
documented follow-up. A level whose `MeshAsset` is null is skipped for that frame.

### `AnimatorComponent`

| Field | Type | Default | Reflected as (range) | Meaning |
| --- | --- | --- | --- | --- |
| `ClipPath` | `std::string` | `""` | `Animator.ClipPath` (`AssetPath("animation")`) | `"project://models/Fox.glb#Run"` by name, `"…glb#0"` by index, bare path → first clip |
| `Speed` | `float` | `1.0` | `.Speed` (−4…4) | Rate multiplier; may be negative |
| `Loop` | `bool` | `true` | `.Loop` | — |
| `Playing` | `bool` | `true` | `.Playing` | — |
| `NormalizedTime` | `float` | `0.0` | `.NormalizedTime` (0–1) | Play head; **scrub it while paused to re-pose** |
| `ClipRef`, `ResolvedClipPath`, `SkelRef`, `TimeSeconds` | `Ref<AnimationClip>`, `string`, `Ref<Skeleton>`, `float` | null/`""`/null/`0` | — **runtime** | Resolution + clock |
| `Palette` | `std::vector<glm::mat4>` | empty | — **runtime** | This frame's skinning matrices |
| `JointModelMatrices` | `std::vector<glm::mat4>` | empty | — **runtime** | Baked-space joint **frames** (no inverse-bind) — what sockets read |
| `ScratchLocals`, `ScratchLocalsB`, `ScratchGlobals` | `std::vector<glm::mat4>` | empty | — **runtime** | Reused sampling buffers |
| `NextClipPath`, `ResolvedNextClipPath`, `NextClipRef`, `NextTimeSeconds`, `FadeDuration`, `FadeElapsed` | — | `""`/`""`/null/`0`/`0`/`0` | — **runtime** | Crossfade state |

Registered as **`Animator`**, category `Rendering`. v1 plays **one clip** per animator; blend trees and
state machines are parked. **Read by** [`Scene::UpdateAnimators`](#sceneupdateanimators).

#### `AnimatorComponent::CrossfadeTo`

```cpp
void CrossfadeTo(const std::string& clipPath, float seconds)
```

**What it does** — sets intent only; `Scene::UpdateAnimators` resolves the clip and runs the blend.
Three cases, all handled inline in the header:
- `clipPath` empty **or** equal to the current `ClipPath` → **cancels** any pending fade and returns;
- `seconds <= 0` → **hard switch**: `ClipPath = clipPath`, `NormalizedTime = 0`, pending fade cleared;
- otherwise → `NextClipPath = clipPath`, `FadeDuration = seconds`, `FadeElapsed = 0`.

Re-targets an in-flight fade. Never throws; an unresolvable clip simply never poses.

### `SocketComponent` — attach to a joint

| Field | Type | Default | Reflected as | Units |
| --- | --- | --- | --- | --- |
| `Joint` | `std::string` | `""` | `Socket.Joint` | Target joint **name**, e.g. `"hand.r"` |
| `Position` | `glm::vec3` | `{0,0,0}` | `.Position` (metres) | Offset from the joint |
| `Rotation` | `glm::quat` | identity `{1,0,0,0}` | `.Rotation` | Offset rotation |
| `Scale` | `glm::vec3` | `{1,1,1}` | `.Scale` | Offset scale |

Registered as **`Socket`**, category `Rendering`. **Read by** `Scene::WorldOf` — the socket override
runs **before** the ordinary parent walk:

```
socketWorld = ancestorWorld · jointFrame · (T(Position) · R(Rotation) · S(Scale))
```

where `jointFrame` is the nearest animated ancestor's published
`AnimatorComponent::JointModelMatrices[j]`.

**Notes & pitfalls**
- **The entity's own `TransformComponent` local is ignored while the socket resolves** — the offset
  lives on the socket.
- If no ancestor animates that joint yet (no `SkelRef`, empty `JointModelMatrices`, or an unknown
  joint name), the walk **falls through** to the normal parent-relative transform, so a socket behaves
  as an ordinary child until the rig poses.
- The ancestor search is guarded at 4096 steps.

### 3D lights

| Component (registered as) | Field | Type | Default | Reflected range | Meaning |
| --- | --- | --- | --- | --- | --- |
| `DirectionalLightComponent` (`DirectionalLight`, `Lighting`) | `Direction` | `vec3` | `{-0.4,-1,-0.3}` | — | Direction the light **travels** |
| | `Color` | `vec3` | `{1,1,1}` | colour | — |
| | `Intensity` | `float` | `1.0` | 0–10 | — |
| | `Enabled` | `bool` | `true` | **hidden, omit-if-true** | — |
| `PointLightComponent` (`PointLight`, `Lighting`) | `Color` | `vec3` | `{1,1,1}` | colour | — |
| | `Intensity` | `float` | **`8.0`** | 0–20 | Raised from 1 → 8 because the windowed inverse-square falloff made 1 nearly invisible a few metres out |
| | `Radius` | `float` | `10.0` | 0–100, metres | — |
| | `Enabled` | `bool` | `true` | **hidden, omit-if-true** | — |

**Read by** `GatherSceneLights` (`Scene3D.cpp:96`), shared by `Scene::OnRender3D` and
`Scene::BuildRenderDesc` so there is one truth for "what lights this scene has".

**Notes & pitfalls**
- **The first enabled + active directional light becomes the sun and the loop stops there.** Extra
  directional lights are silently ignored. "First" is registry view order, not scene-tree order.
- Point lights use the entity's **`Transform.Position`** (not `WorldOf`) and are all collected;
  `Renderer3D::SetLights` is the single truncation point at `Renderer3D::kMaxPointLights = 16`.

### 3D world systems — recipe components

All three follow the same shape: a runtime `Ref<>` asset plus a **reflected recipe**, with
`UseRecipe` (**hidden**, but serialized) gating regeneration, plus an unreflected `BuiltSignature`.
An asset assigned from **code** keeps `UseRecipe == false` and is never touched — that is the compat
gate for hand-built scenes.

#### `TerrainComponent` *(registered as `Terrain`, category `World`)*

| Field | Type | Default | Reflected range | Units / meaning |
| --- | --- | --- | --- | --- |
| `TerrainAsset` | `Ref<Terrain>` | `nullptr` | — **runtime** | Entity skipped when null |
| `UseRecipe` | `bool` | `false` | **hidden** | Gates regeneration |
| `WorldSize` | `float` | `512.0` | 16–8192 m | Metres along X and Z |
| `Resolution` | `int32_t` | `513` | 65–1025 | Vertices per side; **snapped to `32·2^k + 1`** at build |
| `HeightScale` | `float` | `60.0` | 0–1000 m | World height of a 1.0 sample |
| `BaseHeight` | `float` | `0.0` | metres | World Y of a 0.0 sample |
| `Seed` | `uint32_t` | `1337` | — | — |
| `Octaves` | `int32_t` | `6` | 1–12 | — |
| `Frequency` | `float` | `3.0` | 0.1–32 | fBm periods across the terrain |
| `Lacunarity` | `float` | `2.0` | 1–4 | — |
| `Gain` | `float` | `0.5` | 0–1 | — |
| `EdgeFalloff` | `float` | `0.0` | 0–1 | 0 = none; else island edge fade |
| `HeightmapPath` | `std::string` | `""` | `AssetPath("texture")` | Empty → procedural fBm |
| `GrassColor` / `RockColor` / `SnowColor` / `SandColor` | `vec3` | `{0.24,0.38,0.15}` / `{0.36,0.33,0.31}` / `{0.92,0.94,0.98}` / `{0.55,0.48,0.36}` | — | Auto-splat layer tints |
| `GrassTex` / `RockTex` / `SnowTex` / `SandTex` | `std::string` | `""` | `AssetPath("texture")` | Optional splat albedo |
| `SnowHeight` | `float` | `30.0` | metres | World Y where snow fades in |
| `SnowBlend` | `float` | `6.0` | 0.01–50 m | Smoothstep half-width for the snow band |
| `BuiltSignature` | `std::size_t` | `0` | — **runtime** | `0` = never built |

**Terrain is world geometry placed by its own spec — the entity's `TransformComponent` is not applied
to it.** **Read by** `SyncWorldSystems` (auto-builds **once only**), `OnRender3D` (quadtree LOD
around the pass camera), `BuildRenderDesc` (first built terrain becomes `TerrainSystem` **and** the
shore-attenuation source for water), and `ScenePhysics` via `TerrainColliderComponent`. Terrain
geometry itself: [world-systems.md](world-systems.md).

#### `WaterComponent` *(registered as `Water`, category `World`)*

| Field | Type | Default | Reflected range | Units / meaning |
| --- | --- | --- | --- | --- |
| `WaterAsset` | `Ref<Water>` | `nullptr` | — **runtime** | — |
| `UseRecipe` | `bool` | `false` | **hidden** | — |
| `Preset` | `WaterPreset` | `Lake` | enum | `Lake=0, Ocean=1, Storm=2` — seeds the wave stack + base optics |
| `Center` | `glm::vec2` | `{0,0}` | — | World **XZ** centre of the plane |
| `Extent` | `glm::vec2` | `{200,200}` | — | World size along X and Z |
| `SurfaceHeight` | `float` | `0.0` | metres | World Y of the calm surface |
| `GridResolution` | `int32_t` | `129` | 2–513 | Vertices per side of the displaced grid |
| `Amplitude` | `float` | `1.0` | 0–4 | Multiplies the preset wave heights |
| `Choppiness` | `float` | `1.0` | 0–2 | Multiplies the preset wave steepness |
| `ShallowColor` | `vec3` | `{0.10,0.42,0.45}` | — | — |
| `DeepColor` | `vec3` | `{0.02,0.12,0.20}` | — | — |
| `CausticStrength` / `WhitecapStrength` / `SparkleStrength` | `float` | `0.0` | 0–2 each | — |
| `Enabled` | `bool` | `true` | **hidden, omit-if-true** | See the warning below |
| `BuiltSignature` | `std::size_t` | `0` | — **runtime** | — |

`enum class WaterPreset { Lake = 0, Ocean = 1, Storm = 2 };` is declared in this header. **Read by**
`SyncWorldSystems` (rebuilds on **any** recipe change — `Water::Create` is GL-free, so edits are
live), `OnRenderWorldFX` (the simple path, cheap IBL-fallback reflection) and `BuildRenderDesc`,
which pushes every built body and marks the one **nearest the camera** as `PrimaryReflectionWater`.

#### `ParticleEmitterComponent` *(registered as `ParticleEmitter`, category `World`)*

| Field | Type | Default | Reflected range | Units / meaning |
| --- | --- | --- | --- | --- |
| `Emitter` | `Ref<ParticleEmitter>` | `nullptr` | — **runtime** | — |
| `UseRecipe` | `bool` | `false` | **hidden** | — |
| `Enabled` | `bool` | `true` | **hidden, omit-if-true** | See the warning below |
| `MaxParticles` | `uint32_t` | `2048` | 1–65536 | Pool size |
| `SpawnRate` | `float` | `60.0` | 0–5000 | particles / second |
| `Shape` | `EmitterShape` | `Cone` | enum | `Point=0, Sphere=1, Cone=2, Box=3` |
| `ShapeRadius` | `float` | `0.5` | 0–50 m | Sphere radius / Cone base |
| `ConeAngleDeg` | `float` | `20.0` | 0–180° | — |
| `BoxExtents` | `vec3` | `{1,1,1}` | — | Box shape extents |
| `SpeedMin` / `SpeedMax` | `float` | `1.0` / `3.0` | 0–100 | m/s |
| `LifeMin` / `LifeMax` | `float` | `1.0` / `2.5` | 0.01–60 s | — |
| `Gravity` | `vec3` | `{0, 1.5, 0}` | — | m/s² — **positive Y by default**: hot air lifts embers |
| `Drag` | `float` | `0.6` | 0–10 | — |
| `Wind` | `vec3` | `{0.4, 0, 0}` | — | m/s |
| `SizeStart` / `SizeEnd` | `float` | `0.10` / `0.02` | 0–50 | world units |
| `ColorStart` | `vec4` | `{1, 0.75, 0.30, 1}` | colour | RGBA |
| `ColorEnd` | `vec4` | `{1, 0.25, 0.05, 0}` | colour | RGBA (fades out) |
| `Blend` | `ParticleBlend` | `Additive` | enum | `Alpha=0, Additive=1` |
| `Space` | `ParticleSpace` | `World` | enum | `World=0, Local=1` |
| `TexturePath` | `std::string` | `""` | `AssetPath("texture")` | Empty → procedural puff |
| `FlipbookTilesX` / `FlipbookTilesY` | `int32_t` | `1` / `1` | 1–16 | Sheet grid |
| `FlipbookFps` | `float` | `0.0` | 0–60 | 0 = no flipbook |
| `FlipbookBlend` | `bool` | `false` | — | Blend between frames |
| `SoftFadeDistance` | `float` | `0.2` | 0–10 m | Depth soft-fade |
| `StretchByVelocity` | `float` | `0.0` | 0–1 | — |
| `NoiseEnabled` | `bool` | `false` | — | Curl-noise turbulence; off = byte-identical |
| `NoiseStrength` | `float` | `3.0` | 0–20 | Acceleration scale |
| `NoiseFrequency` | `float` | `0.4` | 0.01–4 | Spatial frequency |
| `NoiseOctaves` | `int32_t` | `2` | 1–4 | Clamped 1–4 at build |
| `BoundsExtents` | `vec3` | `{0,0,0}` | — | Local half-extents; **all-zero = unbounded**, a zero axis is unbounded |
| `BoundsWrap` | `bool` | `false` | — | Past bounds: `false` kills, `true` wraps |
| `BuiltSignature` | `std::size_t` | `0` | — **runtime** | — |

`EmitterShape`, `ParticleBlend` and `ParticleSpace` come from `particles/ParticleSystem.h`
([world-systems.md](world-systems.md)). **The reflected recipe *is* the `.cemitter` preset format.**
Defaults describe a warm additive campfire ember cone. **Read by** `SyncWorldSystems`,
`OnRenderWorldFX` (update + draw at the entity's world transform) and `BuildRenderDesc` (which
*advances* the emitter and hands `SceneRenderer` a pointer — the renderer only draws).

> ### ⚠ `WaterComponent::Enabled` and `ParticleEmitterComponent::Enabled` do not switch off an already-built asset on the `SceneRenderer` path
>
> `SyncWorldSystems` and `OnRenderWorldFX` both honour `Enabled` **and** `IsActiveInHierarchy`, but
> `Scene::BuildRenderDesc` gathers water bodies and emitters on `if (!wc.WaterAsset)` /
> `if (!pc.Emitter)` **alone** (`Scene3D.cpp:822`, `:843`) — no enable check, no active check.
> `BuildRenderDesc` is the path Starforge's viewport **and** `PlayerLayer` use, so unticking a water
> body that has already been built leaves it rendering. Until this is fixed, clear the recipe or
> destroy the entity rather than relying on the flag. Terrain has the same gap in both render paths.

### `VoxelVolumeComponent` *(registered as `VoxelVolume`, category `World`)*

| Field | Type | Default | Reflected range | Meaning |
| --- | --- | --- | --- | --- |
| `Volume` / `Palette` / `Render` | `Ref<VoxelVolume>` / `Ref<BlockPalette>` / `Ref<VoxelRenderData>` | `nullptr` | — **runtime** | Chunk store, block table, GPU meshes + atlas |
| `PalettePath` | `std::string` | `""` | `AssetPath("voxel_palette")` | `.cpal`; empty → default palette |
| `VolumePath` | `std::string` | `""` | `AssetPath("voxel_volume")` | `.cvox`; empty → empty/generated volume |
| `VoxelSize` | `float` | `1.0` | 0.05–16 m | **Metres per voxel** |
| `ViewRadius` | `int32_t` | `8` | 1–64 | **Chunk** radius streamed around the camera |
| `Greedy` | `bool` | `true` | — | Greedy-merged vs culled per-face render mesh |
| `GenEnabled` | `bool` | `false` | — | Procedurally stream-generate chunks in view |
| `Seed` | `uint32_t` | `1337` | — | — |
| `SurfaceLevel` | `float` | `32.0` | — | Average ground height, in **voxels** (world Y) |
| `Amplitude` | `float` | `24.0` | 0–512 | ± voxels of height variation |
| `Frequency` | `float` | `0.010` | 0.0001–1 | Noise frequency, per voxel |
| `Octaves` | `int32_t` | `5` | 1–10 | — |
| `Lacunarity` | `float` | `2.0` | 1–4 | — |
| `Gain` | `float` | `0.5` | 0–1 | — |
| `Ridged` | `bool` | `false` | — | Ridged multifractal vs fBm |
| `CaveThreshold` | `float` | `0.0` | 0–1 | `0` = no caves |
| `CaveFrequency` | `float` | `0.05` | 0.001–1 | — |
| `DirtDepth` | `int32_t` | `4` | 0–32 | Voxels of dirt under the surface |
| `SandLevel` | `float` | `-1.0e9` | — | Surface at/below this height (voxels) is sand |
| `GrassBlock` / `DirtBlock` / `StoneBlock` / `SandBlock` | `uint32_t` | `1` / `2` / `3` / `4` | — | Palette indices |
| `BuiltGenSignature` | `std::size_t` | `0` | — **runtime** | Generation-recipe signature |

The voxel **data** rides a `.cvox` sidecar, not the scene JSON. **Read by**
[`Scene::SyncVoxelVolumes`](#scenesyncvoxelvolumes), `Scene::SubmitOpaqueMeshes` (one draw per
uploaded chunk mesh, frustum-culled per chunk), and `ScenePhysics::BuildVoxelBodies` /
`RebuildDirtyVoxelChunks`. Depth: [`../guide/voxels.md`](../guide/voxels.md).

### 3D colliders

| Component (registered as) | Field | Type | Default | Reflected as | Meaning |
| --- | --- | --- | --- | --- | --- |
| `MeshColliderComponent` (`MeshCollider`, `Physics`) | `Convex` | `bool` | `false` | `.Convex` | `true` → `ConvexHullShape` (dynamic-capable); `false` → static/kinematic-only triangle mesh |
| | `IsTrigger` | `bool` | `false` | `.IsTrigger` | — |
| | `Enabled` | `bool` | `true` | **hidden, omit-if-true** | — |
| `TerrainColliderComponent` (`TerrainCollider`, `Physics`) | *(no fields)* | — | — | registered with **no fields** | Empty tag component |

**Notes & pitfalls**
- `MeshColliderComponent` sources geometry from a sibling `PrimitiveMeshComponent` first. Failing
  that it falls back to the `MeshRendererComponent` mesh's **local AABB as a box** and warns —
  triangle colliders for imported meshes wait on CPU-side mesh retention, a documented v1 limit. A
  concave mesh on a dynamic body warns and is treated as static.
- **`TerrainColliderComponent` is an empty type**, so `AddComponent`/`GetComponent` return the shared
  process-lifetime sentinel — presence is the whole signal. It builds a Jolt `HeightFieldShape` from
  the sibling `TerrainComponent`'s CPU heightfield. Because Jolt rounds its sample count up to a
  multiple of 2 and terrain resolutions are odd (`32·2^k + 1`), the build uses an `(n−1)²` grid —
  **the far +X/+Z edge row is dropped**, a documented, harmless loss at the rim. Terrain is always
  static and the entity transform is ignored. No built terrain → warn and skip.

### Navigation

#### `NavMeshComponent` *(registered as `NavMesh`, category `Navigation`)*

| Field | Type | Default | Reflected range | Units / meaning |
| --- | --- | --- | --- | --- |
| `Nav` | `Ref<NavWorld>` | `nullptr` | — **runtime** | Baked navmesh; null = none |
| `BuiltSignature` | `std::size_t` | `0` | — **runtime** | Recipe + geometry signature |
| `Baking` | `bool` | `false` | — **runtime** | An async bake is in flight |
| `SidecarPath` | `std::string` | `""` | `AssetPath("navmesh")` | `.cnav`; empty → derived beside the scene |
| `CellSize` | `float` | `0.30` | 0.05–4 m | XZ rasterization voxel |
| `CellHeight` | `float` | `0.20` | 0.05–4 m | Y rasterization voxel |
| `AgentRadius` | `float` | `0.6` | 0–10 m | Walkable area is eroded by this |
| `AgentHeight` | `float` | `2.0` | 0.1–20 m | Vertical clearance |
| `AgentMaxClimb` | `float` | `0.9` | 0–10 m | Max auto-step |
| `AgentMaxSlope` | `float` | `45.0` | 0–89° | — |
| `RegionMinSize` | `float` | `8.0` | 0–150 | Voxels (area = size²) |
| `RegionMergeSize` | `float` | `20.0` | 0–150 | Voxels |
| `EdgeMaxLen` | `float` | `12.0` | 0–50 m | — |
| `EdgeMaxError` | `float` | `1.3` | 0.1–3 | Voxels |
| `DetailSampleDist` | `float` | `6.0` | 0–16 | × `CellSize` |
| `DetailSampleMaxError` | `float` | `1.0` | 0–16 | × `CellHeight` |
| `VertsPerPoly` | `int32_t` | `6` | 3–6 | — |
| `TileSize` | `float` | `0.0` | 0–256 | Voxels; `0` = solo (single-tile) build in v1 |
| `SourceMode` | `NavSourceMode` | `FromChildren` | enum | `FromChildren=0` this entity's descendants only; `WholeScene=1` every collidable entity |
| `AutoGenerate` | `bool` | `false` | — | Rebake when the recipe/geometry signature changes |
| `AlwaysRenderHelper` | `bool` | `false` | — | Draw the nav overlay even when unselected |

`enum class NavSourceMode : int32_t { FromChildren = 0, WholeScene = 1 };` is declared in this header
(the Inspector labels are `"From children"` / `"Whole scene"`). The built navmesh rides a `.cnav`
sidecar, not the scene JSON. Bake geometry is the **collision** view of the scene. **Read by**
[`Scene::SyncNavMeshes`](#scenesyncnavmeshes) (lazy sidecar load only) and `SceneNav`.

#### `NavAgentComponent` *(registered as `NavAgent`, category `Navigation`)*

| Field | Type | Default | Reflected range | Units |
| --- | --- | --- | --- | --- |
| `Radius` | `float` | `0.4` | 0.05–5 m | Footprint |
| `Height` | `float` | `1.8` | 0.1–20 m | — |
| `MaxSpeed` | `float` | `3.5` | 0–50 | m/s |
| `MaxAccel` | `float` | `8.0` | 0–100 | m/s² |
| `StoppingDistance` | `float` | `0.4` | 0–10 m | Arrival tolerance; emits `nav.arrived` |
| `AutoRepath` | `bool` | `true` | — | Re-plan when the path is invalidated |

Steered by DetourCrowd **only while a play session runs** — the same lifetime rule as physics bodies —
and the transform is written back each fixed step like a body. **Read by** `Scene::OnNavStart` /
`OnNavStep` / `OnNavStop` and `SceneNavRuntime`; scripts drive it through `Nav().SetTarget` / `Stop`.
Depth: [`../guide/navigation-and-ai.md`](../guide/navigation-and-ai.md).

---

## `System`

```cpp
// scene/System.h — the entire header
namespace Cosmic
{
    class Scene;

    class COSMIC_API System
    {
    public:
        virtual ~System() = default;

        virtual void OnUpdate(Scene& scene, float deltaTime) {}
        virtual void OnFixedUpdate(Scene& scene, float deltaTime) {}
    };
}
```

**What it is** — the scene-scoped logic base class, in full. Both hooks are no-op virtuals, so you
override only what you need. Register with [`Scene::AddSystem<T>`](#sceneaddsystem), retrieve with
[`GetSystem<T>`](#scenegetsystem), clear with [`RemoveAllSystems`](#sceneremoveallsystems). Both
configurations.

**Why you'd use it** — scene-wide simulation that is not per-entity: a spawner, a scoreboard, an
integrator over a whole population.

**Notes & pitfalls**
- **Nothing ticks it for you.** See [the warning](#the-warning-that-governs-this-chapter).
- `jobs/ParallelSystem.h` extends it with the four-pass data-parallel model (`OnPrepare` /
  `OnParallelExecute` / `OnMerge` plus their `OnFixed*` twins, `RegisterQuery`, `StageQueries`,
  `CommitQueries`). That header belongs to [jobs.md](jobs.md) and is not duplicated here. Two facts
  matter from this side: `ParallelSystem` is **non-copyable and non-movable** on purpose (its
  `ReadWriteQuery` members self-register in their constructors, so a copy would stage and commit
  twice per frame), which is why `AddSystem` stores systems as `Scope<T>`; and
  `ReadWriteQuery::Commit`'s structural-change guard is a `CS_CORE_ASSERT` inside
  `#ifdef CS_ENABLE_ASSERTS` (`jobs/SystemQuery.h:153`, `:169`), so **it does not exist in any
  build**. Obey the no-structural-changes-between-Pass-B-and-Pass-D rule anyway; nothing enforces it.
- The scene passes **itself** to every hook, so a system needs no stored back-pointer.

---

## `ComponentRegistry` / `CS_REGISTER_COMPONENT`

```cpp
// scene/ComponentRegistry.h — the entire header
#include <entt/entt.hpp>

#define CS_REGISTER_COMPONENT(T) \
    template<> struct entt::type_hash<T> final { \
        [[nodiscard]] static consteval entt::id_type value() noexcept { \
            return entt::hashed_string::value(#T); \
        } \
    };
```

**What it does** — replaces EnTT's default sequential-static-counter type ids with a **compile-time
hash of the stringified type name**, so the same component type gets the same id in the engine DLL
and in every project DLL.

**Why you'd use it** — **you must use it for every component type a project DLL defines.** The engine
DLL and your project DLL have separate data segments; without this macro the same type gets different
ids on each side and component storage silently corrupts. Every built-in component is registered this
way at the bottom of `Components.h` (19 expansions) and `Components3D.h` (15).

**Example**

```cpp
// MyComponents.h — in your project
#include <scene/ComponentRegistry.h>

namespace Workspace
{
    struct PhysicsBody
    {
        glm::vec2 Position{ 0.0f };
        glm::vec2 Velocity{ 0.0f };
    };
}

CS_REGISTER_COMPONENT(Workspace::PhysicsBody)   // global scope, after the struct
```

**Notes & pitfalls**
- **It must appear at global scope, in the header, after the struct definition**, and it must be
  included by every TU that names the type.
- **The ODR contract is load-bearing** (`Components.h:656-668`). The expansion is a full template
  specialisation whose only member is a `consteval` function returning a compile-time constant, which
  makes the definition token-for-token identical in every translation unit. Any registration you add
  must preserve that: **no static data members, no non-`consteval` members, no TU-dependent
  initialisation.**
- The macro stringifies `T` **as written**, so `CS_REGISTER_COMPONENT(Workspace::PhysicsBody)` hashes
  `"Workspace::PhysicsBody"`. Registering the same type under two different spellings produces two
  different ids — always use the fully qualified name.
- This is **type identity only**. It does not reflect the component; the Inspector, `.cscene`
  serialization and undo need a separate `Reflect::Class<T>` registration
  ([`../guide/scenes-and-serialization.md`](../guide/scenes-and-serialization.md)).
- Both configurations.

---

## `SelectableComponent`

```cpp
// scene/SelectableComponent.h
namespace Cosmic
{
    struct SelectableComponent {};
}
CS_REGISTER_COMPONENT(Cosmic::SelectableComponent)
```

**What it is** — an **empty tag component** that marks an entity as clickable by
`EntityPicker::Pick`. It carries no data; its presence in the registry is the entire signal.

**Read by** `EntityPicker::Pick` (`telemetry/EntityPicker.h:95-100`), which iterates
`View<TransformComponent, SelectableComponent>()` and tests a world-space point against each entity's
CPU bounding box. Selection then flows onto the shared bus via `EntitySelection::Set` —
[serial-telemetry.md](serial-telemetry.md).

**Notes & pitfalls**
- **It is an empty type**, so `AddComponent`/`GetComponent` return the shared process-lifetime
  sentinel. Never write through the reference expecting per-entity storage.
- **It is *not* `COSMIC_API`-exported and *not* reflected.** There is no `Reflect::Class` registration
  for it in either `TypeRegistry.cpp` or `TypeRegistry3D.cpp`, so it does **not** appear in the
  Inspector's Add menu and it does **not** round-trip through a `.cscene`. Add it from code. (This
  corrects the guide's grouping of it with the UI components as "will show up in the Inspector".)
- Not to be confused with the editor's 3D click-to-select, which is `ScenePicker`'s entity-ID pass and
  needs no marker component at all.
- Both configurations.

---

## `ScenePicker` — routed elsewhere

`scene/ScenePicker.h` declares `class COSMIC_API ScenePicker` — the entity-ID 3D picking service
(`Create`, `RenderIdPass`, `Pick`, `WorldPoint`, `GetColorTextureID`, `GetIdTextureID`, `GetWidth`,
`GetHeight`). It owns a private `{RGBA8, RED_INTEGER, DEPTH24STENCIL8}` framebuffer, renders the
scene's entity IDs into it, and reads one texel back with a synchronous `glReadPixels`.

**This chapter deliberately does not document it.** It is a camera/viewport service, not an ECS
service: it takes a `Camera`, works in **viewport pixels** (x from the left, y from the **top** — the
GL bottom-left flip is handled internally), and feeds `OrbitCameraController::SetPivotProbe`. The
chapter that covers it end to end — the coordinate contract, what the ID pass can and cannot see, the
`-1`/invalid-`Entity` miss behaviour, and the CAD pivot probe — is
[`../guide/cameras.md`](../guide/cameras.md#click-to-select-an-entity-3d-only).

**Configuration:** 3D only. Its `Cosmic.h` include is fenced (`Cosmic.h:117-119`) **and**
`ScenePicker.cpp` is filtered out of the 2D build (`Cosmic/CMakeLists.txt:202`), so in a 2D tree it
fails at **compile** time with a clear "undeclared identifier" — unlike `NavigationCube`, whose
include is unfenced and which therefore fails at link.

**Manifest re-routed (D61 integration, 2026-07-26):** the row now reads
`| scene/ScenePicker.h ³ᴰ | cameras.md |`, and the full entries live in
[`cameras.md`](cameras.md), written by D14 in the same wave. This section is a stub so a reader
looking for `ScenePicker` under the ECS finds the pointer rather than nothing.

---

## Configuration summary

| Symbol | 2D build | 3D build | Failure in 2D |
| --- | --- | --- | --- |
| `Scene`, `Entity`, `System`, `ComponentRegistry`, `SelectableComponent` | ✅ | ✅ | — |
| The 19 components in `Components.h` | ✅ | ✅ | — |
| The 15 components in `Components3D.h` | ❌ | ✅ | **compile error** (header fenced out of `Cosmic.h` and dropped from the build) |
| `Scene::OnNavStart` / `OnNavStep` / `OnNavStop` / `GetNav` | ❌ | ✅ | **compile error** (declaration fenced) |
| `Scene::OnRender3D`, `UpdateAnimators`, `SyncPrimitiveMeshes`, `SyncWorldSystems`, `SyncVoxelVolumes`, `SyncNavMeshes`, `OnRenderWorldFX` | ❌ | ✅ | **link error** — declared unfenced in `Scene.h`, defined only in `Scene3D.cpp` |
| `Scene::BuildRenderDesc` | ✅ (2D twin) | ✅ (3D gather) | — |
| `Scene::OnRenderSprites`, `OnRender2DLights`, `BuildSpriteDrawList`, `UpdateSpriteAnimations`, `OnRender`, `FindEnvironment` | ✅ | ✅ | — |
| The whole physics session (`OnPhysicsStart/Step/Stop`, `DispatchPhysicsEvents`, `GetPhysics`) | ✅ | ✅ | — |
| `ScenePicker` | ❌ | ✅ | **compile error** |

Rules and rationale: [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md); the
configuration table in root [README §1.6](../../README.md#16-the-two-engine-configurations).

---

*See also:* [`../guide/entities-and-components.md`](../guide/entities-and-components.md) (the guide
chapter — usage, the full worked catalogue, DG-9) ·
[`../guide/scenes-and-serialization.md`](../guide/scenes-and-serialization.md) (`.cscene`,
reflection, prefabs, undo) · [`../systems/ecs-scene.md`](../systems/ecs-scene.md) (how the registry,
views and the Phase 29 file partition work) · [physics.md](physics.md) (the runtime behind the
physics components) · [jobs.md](jobs.md) (`ParallelSystem`, `SystemQuery`, `JobSystem`) ·
[cameras.md](cameras.md) (`ScenePicker`'s intended home) ·
[world-systems.md](world-systems.md) (`Terrain`, `Water`, `ParticleEmitter` themselves) ·
[serial-telemetry.md](serial-telemetry.md) (`EntityPicker`, `EntitySelection`).

---
*Changelog:*
*2026-07-26 — created (D13). Covers `Scene` (lifecycle, hierarchy, systems, queries, physics/nav
sessions, every render and sync hook), `Entity`, all 34 built-in components field-by-field with
reflected names and metadata, `System`, `CS_REGISTER_COMPONENT` and `SelectableComponent`.
`ScenePicker` is pointed at `cameras.md` rather than absorbed — the manifest row needs re-pointing.*
