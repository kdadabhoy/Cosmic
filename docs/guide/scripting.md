# Scripting — Guide

**What this covers:** Cosmic's C++ script tier — `ScriptableEntity` and every lifecycle callback,
declaring Inspector fields, registering a game module with the `CS_*` macros, who owns and drives
the `ScriptHost`, the `SystemScript` tier for class-of-entity logic, hot reload and why it only
works in edit mode, and all eight script proxies:
`Physics()` · `Character()` · `Flow()` · `Signals()` · `Telemetry()` · `Nav()` · `Animator()` ·
`Voxels()`.
**Source of truth:** `Cosmic/src/scripting/ScriptableEntity.h`, `scripting/ScriptHost.{h,cpp}`,
`scripting/ModuleRegistry.{h,cpp}`, `scripting/ModuleMacros.h`, `scene/Components.h`
(`NativeScriptComponent`, `SystemScriptComponent`), `layers/PlayerLayer.cpp`,
`Projects/Starforge/src/{StarforgeApp.cpp,GameModule.cpp}`,
`Projects/Starforge/src/panels/InspectorPanel.cpp`,
`Projects/Starforge/assets/templates/src/{Module.cpp,scripts/*}`,
`Projects/ForgeIsle/src/{Module.cpp,scripts/PlayerController.h}`, `tests/test_scripthost.cpp`,
`tests/test_template_scripts.cpp`
**API Reference:** [../reference/physics.md](../reference/physics.md) (the `Physics()` /
`Character()` proxies) — the scripting headers have **no reference chapter yet**; this chapter and
the headers are the current source · **How it works:**
[../systems/ecs-scene.md](../systems/ecs-scene.md)
**Configuration:** both — the script tier itself, and `Physics()`, `Character()`, `Flow()`,
`Signals()` and `Telemetry()`, exist in every build. **`Nav()`, `Animator()` and `Voxels()` are 3D
only** (see [Which proxies a 2D build has](#which-proxies-a-2d-build-has) and
[../systems/build-2d-3d-split.md](../systems/build-2d-3d-split.md)).

A Cosmic script is **a real C++ class**, compiled into your project's game DLL, deriving from
`Cosmic::ScriptableEntity`. There is no embedded VM and no binding layer: inside a callback you have
the whole engine API, at full speed, with a debugger attached.

The scene stores only the script's **class name** plus the field values you tuned in the Inspector.
At Play the `ScriptHost` resolves that name to a factory, constructs an instance, injects the owning
entity and scene, pushes the saved field values in, and drives the callbacks. **Scripts do not run
in edit mode** — the editor viewport holds no instances at all, which is why editing a field is
always safe and why hot reload is an edit-mode operation.

---

## Quick start

Three files. A header for the script, one block in `Module.cpp`, and a component on an entity.

```cpp
// src/scripts/Spinner.h
#pragma once
#include <Cosmic.h>

class Spinner : public Cosmic::ScriptableEntity
{
public:
    float DegreesPerSecond = 90.0f;   // public => can become an Inspector field

protected:
    void OnUpdate(float ts) override
    {
        auto& t = GetComponent<Cosmic::TransformComponent>();
        t.Rotation.y += DegreesPerSecond * ts;   // Euler degrees
    }
};
```

```cpp
// src/Module.cpp
#include <Cosmic.h>
#include "scripts/Spinner.h"

CS_MODULE_BEGIN(MyGame)
    CS_SCRIPT(Spinner)
        CS_FIELD(DegreesPerSecond).Range(-720.0f, 720.0f)
    CS_END;
CS_MODULE_END()
```

Then in Starforge: **Build Scripts (Ctrl+B)**, select an entity, **Add Component ▸ Native Script**,
pick `Spinner` from the Class dropdown, tune `DegreesPerSecond`, press **Play**.

From code the same attachment is one line:

```cpp
entity.AddComponent<Cosmic::NativeScriptComponent>("Spinner");
```

---

## What a script is, and what holds it

| Piece | Lives in | Role |
| --- | --- | --- |
| Your class | your game DLL | The behaviour. Subclasses `ScriptableEntity`. |
| `CS_SCRIPT(T)` | `Module.cpp` | Registers a **factory + reflected field list** under `"T"` |
| `ModuleRegistry` | the engine DLL | Process-wide `name → descriptor` map, shared by every DLL |
| `NativeScriptComponent` | the scene | `ClassName` + saved field values (+ a runtime `Instance*`) |
| `ScriptHost` | whoever runs Play | Constructs, injects, ticks and destroys the instances |

`NativeScriptComponent` is a normal reflected component (`"NativeScript"`, category `Scripts`), so
it saves and loads like any other. Only `ClassName` is a plain reflected field — the tuned values
live in a `name → boxed FieldValue` map that the serializer writes out-of-band by consulting the
*script's* descriptor for each field's kind. `Instance` is runtime-only and always null in edit mode.

An entity carries **at most one** `NativeScriptComponent`. For an entity that needs several
behaviours, compose: split into child entities, or write one script that owns the pieces.

## The lifecycle

Override only what you need — every callback defaults to a no-op.

| Callback | When | Gated by `Active`? |
| --- | --- | --- |
| `OnCreate()` | After **every** entity and **every** instance in the scene exists | **No** |
| `OnStart()` | After every `OnCreate` has run | **No** |
| `OnUpdate(float ts)` | Every variable tick | **Yes** |
| `OnFixedUpdate(float fixedDt)` | Every fixed step, before physics | **Yes** |
| `OnEvent(Event& e)` | Input/application events while Play is live | **No** |
| `OnSignal(const std::string&, Entity source)` | Every signal on the scene `EventBus` | **No** |
| `OnCollisionEnter/Exit(Entity other)` | Main thread, after the fixed physics step | **No** |
| `OnTriggerEnter/Exit(Entity other)` | Main thread, after the fixed physics step | **No** |
| `OnDestroy()` | On Stop, before the runtime scene is torn down | **No** |

The two-phase start is the point of the design: **by `OnCreate` the whole scene already exists**, so
you can find other entities; by `OnStart` every other script has had its `OnCreate`, so you can
depend on what they set up. Do lookups in `OnCreate`, wire up in `OnStart`.

The `Active` column is exact and worth internalising: `ScriptHost::Tick` and `FixedTick` skip
entities that fail `Scene::IsActiveInHierarchy`, but nothing else does. **A script on a deactivated
entity still gets `OnCreate`, `OnStart`, `OnEvent`, `OnSignal`, the contact callbacks and
`OnDestroy`** — it just stops updating. If your script must do nothing at all while inactive, check
`GetScene().IsActiveInHierarchy(GetEntity())` yourself.

### Order within a frame

Per variable tick, `ScriptHost::Tick(ts)`:

1. every `SystemScript`'s `OnUpdateAll` (in `Order`, then registration order),
2. then every per-entity `OnUpdate`, in **creation order** — the order the entt view produced them
   at `Instantiate`.

Per fixed step, the contract every host implements identically
(`PlayerLayer::OnFixedUpdate`, `StarforgeApp::TickPlay`):

```
scripts OnFixedUpdate  ->  Scene::OnPhysicsStep  ->  Scene::OnNavStep (3D)  ->  Scene::DispatchPhysicsEvents
```

So a force applied in `OnFixedUpdate` is integrated by *this* step, and the contact callbacks that
step produced fire at the end of the same step. Use `OnFixedUpdate` for anything that touches
physics; use `OnUpdate` for input sampling, camera work and anything frame-rate-shaped.

`OnDestroy` runs in the mirror order: systems first (they were created last), then per-entity
scripts, then the instances are deleted and the component's `Instance` pointer nulled.

### Reaching the engine

```cpp
Entity  GetEntity() const;              // this script's entity handle
Scene&  GetScene()  const;              // spawn, destroy, FindByUUID, Events(), ActiveFlow()
template<typename T> T&   GetComponent();
template<typename T> bool HasComponent() const;
template<typename T> T&   AddComponent(Args&&...);
```

These are `public`; the lifecycle overrides and the proxies are `protected`. All of them are valid
from `OnCreate` onward — the host injects the entity and scene *before* any callback runs.

```cpp
void OnStart() override
{
    // Find a sibling by tag through the scene, not through a global.
    auto& reg = GetScene().GetRegistry();
    for (auto e : reg.view<Cosmic::TagComponent>())
        if (reg.get<Cosmic::TagComponent>(e).Tag == "PlayerCamera")
            m_Camera = Cosmic::Entity(e, &GetScene());
}
```

`ForgeIsle`'s `PlayerController` shows the hierarchy-aware version of that lookup (walk
`RelationshipComponent::Children`, resolve each `UUID` with `Scene::FindByUUID`), which is the right
shape when the target must be *this* entity's child rather than any entity with the tag.

> **`OnStart` can run more than once for the same entity** when a screen flow pushes and pops a
> state that reloads the scene. `PlayerController` handles it by deriving every piece of state from
> the live components in `OnStart` instead of assuming a fresh object. Make `OnStart` idempotent.

## Inspector fields

A field is a **public data member** exposed with `CS_FIELD(name)` in the module block. It appears in
the Inspector, serializes with the scene, and is pushed into the instance at Play.

```cpp
CS_SCRIPT(HoverController)
    CS_FIELD(TargetAltitude).Range(0.0f, 100.0f)
    CS_FIELD(Kp)
    CS_FIELD(Kd)
CS_END;
```

Supported member types are the reflection kinds: `bool`, `int32_t`, `uint32_t`, `float`, `glm::vec2`
/`vec3`/`vec4`/`quat`, `std::string`, any enum, and `uint64_t` (an entity reference). Anything else
is a compile error. The hint calls chain directly onto `CS_FIELD` and are the same builder used for
components: `.Range(min, max)`, `.Step(s)`, `.Doc("…")` / `.Tooltip("…")`, `.Degrees()` /
`.Meters()` / `.Seconds()`, `.Color()`, `.AsAssetPath("texture")`, `.AsEntityRef()`,
`.EnumValue("Name", v)`, `.ReadOnly()`, `.HideInInspector()`, `.NoSerialize()`.

Where the values actually live:

- **In edit mode the component's map is authoritative.** There is no instance to hold them.
- When you first pick a class, the editor spins up a throwaway instance and pulls its C++ member
  defaults into the map — so the rows show real defaults immediately, and a script that gains a new
  field backfills that field on next selection.
- At Play the host pushes the map into the fresh instance. **A field absent from the map keeps the
  C++ member default.**
- **Nothing is pulled back when Play stops.** This is deliberate: the edit scene is authoritative,
  and a Play session must not silently rewrite your tuning. `ScriptHost::PullFields` exists and is
  public, but only the editor's seeding path calls it.

Private members are not fields. Keep runtime state private (`m_Vel`, `m_Yaw`) and only expose the
knobs you want authored — the template scripts follow that split consistently.

## Registering a module

`Module.cpp` is the whole registration surface of a project. `CS_MODULE_BEGIN`/`CS_MODULE_END`
expand to **three** `extern "C" __declspec(dllexport)` functions:

| Export | Called by | Does |
| --- | --- | --- |
| `CosmicModule_Register(ModuleRegistry&)` | the editor, on every hot reload | Registers scripts, systems and components. Touches no ImGui. |
| `CreatePluginLayer() -> Cosmic::Layer*` | the Launcher / `--project` / a packaged exe | Registers the module, then returns a `PlayerLayer` |
| `InitializePluginContexts(HostContext)` | the host, at load | Shares the ImGui + ImPlot contexts |

> `ModuleMacros.h`'s own header comment says "the two exports". It expands to three — the ImGui
> context share is the third. The comment is stale, the macro is right.

Four block macros, all usable standalone (no DLL needed — the unit tests register scripts in-exe
with them). Each opens a scope, so **every block ends with `CS_END;`**, and fields chain rather than
comma-join:

```cpp
CS_MODULE_BEGIN(MyGame)

    CS_SCRIPT(Spinner)                      // one instance per entity
        CS_FIELD(DegreesPerSecond)
    CS_END;

    CS_SYSTEM(FlockSystem)                  // one instance per scene, over a query
        .Requires<Cosmic::TransformComponent, BoidTag>().WithTag("Boid").Order(10)
        CS_FIELD(Cohesion).Range(0.0f, 1.0f)
    CS_END;

    CS_COMPONENT(ThrusterComponent)         // a custom reflected component
        CS_FIELD(MaxThrustN)
    CS_END;

CS_MODULE_END()
```

`CS_COMPONENT` does two things: it reflects the type into `Reflect::GetRegistry()` under category
`"Scripts"` (so it gets Inspector rows, `.cscene` round-tripping and undo), and it *notes* the type
id with the `ModuleRegistry` so hot reload can strip its storage before the DLL is freed. The type
still needs `CS_REGISTER_COMPONENT(T)` at global scope in its own header — see
[`entities-and-components.md`](entities-and-components.md#custom-components).

Re-registering a name overwrites, so reload is idempotent. Registrations are bracketed by
`BeginModule`/`EndModule` internally, which is how `UnregisterModule` strips exactly what one module
added.

## Who drives the `ScriptHost`

`ScriptHost` is not a singleton and nothing ticks it for you. Whoever runs a Play session owns one:
`PlayerLayer` standalone, Starforge's play state in the editor. Both do the same five things, and
this is the pattern to copy if you host scenes yourself.

```cpp
class MyPlayLayer : public Cosmic::Layer
{
    Cosmic::Ref<Cosmic::Scene> m_Scene;
    Cosmic::ScriptHost         m_Scripts;
    Cosmic::PhysicsWorld       m_Physics;
    MySink                     m_Sink;      // your ITelemetrySink, or skip the call entirely

    void StartPlay(const Cosmic::Ref<Cosmic::Scene>& runtime)
    {
        m_Scene = runtime;
        m_Scripts.SetTelemetrySink(&m_Sink);   // BEFORE Instantiate, or OnCreate/OnStart pushes are lost
        m_Scripts.Instantiate(*runtime);       // construct + inject + push fields + OnCreate + OnStart
        m_Physics.Init();
        runtime->OnPhysicsStart(m_Physics);    // bodies from components
    }

    void OnUpdate(float ts) override { m_Scripts.Tick(ts); }

    void OnFixedUpdate(float dt) override
    {
        m_Scripts.FixedTick(dt);
        m_Scene->OnPhysicsStep(dt);
        m_Scene->DispatchPhysicsEvents(m_Scripts);   // fires the contact callbacks
    }

    void OnEvent(Cosmic::Event& e) override
    {
        if (!Cosmic::Application::Get().IsPaused())  // PlayerLayer's guard: paused => no OnEvent
            m_Scripts.DispatchEvent(e);
    }

    void StopPlay()
    {
        m_Scene->OnPhysicsStop(m_Physics);
        m_Physics.Shutdown();
        m_Scripts.Destroy();                   // OnDestroy each, delete, null the Instance pointers
    }
};
```

Notes that save time later:

- **`Instantiate` is idempotent re-entry** — it calls `Destroy()` first. Swapping scenes is
  `Destroy()` then `Instantiate(newScene)`; `PlayerLayer::RebindScripts` is exactly that, plus the
  physics and nav unbind/rebind.
- **An unknown class name is inert, never a crash.** The host logs one warning
  (`"unknown script class 'X' — entity kept inert"`) and moves on. Same for an unknown system class.
- **Signals are wired automatically.** `Instantiate` subscribes one `EventBus::ConnectAny` listener
  that fans every scene signal into each live script's `OnSignal`; `Destroy` disconnects it before
  deleting instances.
- `ScriptHost` is non-copyable (it owns heap instances) and its destructor calls `Destroy()`.
- `IsInstantiated()` and `LiveCount()` are there for status UI — Starforge logs
  `"Started — N script(s)"` from the latter.

Because the host is GL-free, the whole tier is headless-testable: `tests/test_scripthost.cpp`
registers scripts in-exe with the same macros and drives the lifecycle with no DLL and no window.

## `SystemScript` — logic for a *class* of entities

`ScriptableEntity` is one instance per entity. When one behaviour should drive fifty entities —
every boid, every critter, every airplane — that is fifty instances doing fifty redundant lookups.
`SystemScript` is the other shape: **one instance per scene**, whose callback receives the entire
matching set each tick.

```cpp
class FlockSystem : public Cosmic::SystemScript
{
public:
    float Cohesion = 0.4f;

protected:
    void OnUpdateAll(std::span<Cosmic::Entity> boids, float ts) override
    {
        for (Cosmic::Entity b : boids) { /* … */ }
    }
    void OnFixedUpdateAll(std::span<Cosmic::Entity> boids, float dt) override {}
};
```

```cpp
CS_SYSTEM(FlockSystem).Requires<Cosmic::TransformComponent, BoidTag>().WithTag("Boid").Order(10)
    CS_FIELD(Cohesion).Range(0.0f, 1.0f)
CS_END;
```

- **Membership is the declared query, rebuilt every tick** — `Requires<Comps...>()` is the component
  filter, `.WithTag("…")` adds an exact `TagComponent::Tag` match. Entities that spawn or die
  between ticks are picked up automatically.
- **The span is scratch owned by the host.** It is valid for the duration of the call only. Copy any
  handles you keep — `NavCritter` keeps per-entity state in an `unordered_map` keyed by the entity's
  `IDComponent` UUID, which is the right pattern (handles are recycled; UUIDs are not).
- **Iteration order is the entt view order** and is not user-sortable in v1. `.Order(n)` sequences
  whole *systems* against each other, not entities within one.
- `SystemScript` has `OnCreate`, `OnStart`, `OnUpdateAll`, `OnFixedUpdateAll`, `OnDestroy` and
  `GetScene()`. It has **no `GetEntity()`** and **none of the proxies** — it is not bound to an
  entity. Reach the same services through the scene: `GetScene().GetNav()`,
  `GetScene().GetPhysics()`, `GetScene().Events()`, `GetScene().ActiveFlow()`.
- **Systems are not gated by `Active`.** Neither the system's own tick nor its membership query
  filters on `IsActiveInHierarchy`; a deactivated entity still appears in the span. Filter yourself
  if that matters.

A system is attached with a `SystemScriptComponent` on **any single entity** in the scene — it is a
scene-level object, so the holder is just a home. Fields serialize exactly like a script's.

`NavCritter` (`Projects/Starforge/assets/templates/src/scripts/NavCritter.h`) is the shipped worked
example: one system steers every `"Critter"`-tagged nav agent, patrolling around each one's spawn
and chasing the `"Player"` entity when it comes within `ChaseRadius`.

## Hot reload

In Starforge, **Build Scripts (Ctrl+B)** compiles the project's game module and swaps it live. The
mechanics, in order (`StarforgeApp::BuildScripts` → `ReloadModule`):

1. The build emits `<Project>_hotN.dll` with an incrementing suffix, because the currently loaded
   DLL is locked. Stale `_hot*` DLLs are swept afterwards; the locked one simply survives.
2. The **edit scene is serialized to a JSON string** while the *old* module is still loaded, so
   module-owned custom components serialize through their real descriptors.
3. Play is stopped, the selection and **the undo stack are cleared**, and the scene is *dropped* —
   before `FreeLibrary`, so any module-typed destructor runs against valid code.
4. `ModuleRegistry::UnregisterModule` strips the module's scripts, systems and component notes;
   the DLL is freed; the new one is loaded and its `CosmicModule_Register` runs.
5. The scene is rebuilt from the snapshot. Script classes and custom components now resolve against
   the new code.

### The edit-mode-only limit

**You cannot hot reload while playing.** `BuildScripts` refuses with
`"[Build] Stop Play before rebuilding scripts."` and does nothing else.

That is not a missing feature so much as an unavoidable one at this tier: live script instances are
C++ objects whose vtables live in the DLL about to be unloaded. There is no way to keep them across
a `FreeLibrary`. The workflow the engine is built around instead is fast: **Stop → Ctrl+B → Play**,
with the scene, selection and field values preserved across the swap.

Two consequences to plan for:

- **Undo history does not survive a reload** (nor entering or leaving Play). Save before a rebuild
  if the history matters.
- **Anything held only in a script instance is gone.** State you want to survive a rebuild belongs
  in components, which round-trip through the snapshot.

> **If the reload's *load* step fails**, Starforge logs
> `"[Module] Load failed — scripts unavailable this session."` Take that literally. The old module
> has already been unregistered and freed, but the reflection registry still holds descriptors whose
> type-erased thunks point into the unloaded DLL, and they are only overwritten by a *successful*
> next load. Restart the editor rather than continuing to edit.

## The proxies

Every proxy is a `protected` accessor returning a small by-value handle bound to this script's
entity and scene. They exist so ordinary gameplay code does not have to reach through
`GetScene()->GetPhysics()->GetBody(handle)` by hand. **Every call is safe when the underlying
service is absent** — a no-op, a default, or an empty result — so a script works unchanged in an
edit-only harness or a headless test.

| Proxy | Reaches | Present in a 2D build? |
| --- | --- | --- |
| `Physics()` | this entity's rigid body + the scene `PhysicsWorld` | **yes** |
| `Character()` | this entity's `CharacterController` | **yes** |
| `Signals()` | the scene `EventBus` | **yes** |
| `Flow()` | the active `FlowMachine`'s variable blackboard | **yes** |
| `Telemetry()` | the host's telemetry sink | **yes** |
| `Nav()` | this entity's nav agent + navmesh queries | **no — 3D only** |
| `Animator()` | this entity's `AnimatorComponent` | **no — 3D only** |
| `Voxels()` | a voxel volume + its palette | **no — 3D only** |

### `Physics()`

```cpp
void OnFixedUpdate(float dt) override
{
    Physics().AddForce({ 0.0f, m_Lift, 0.0f });          // newtons, this step
    Physics().AddImpulse({ 0.0f, 0.0f, -5.0f });         // newton-seconds, instantaneous
    Physics().AddTorque({ 0.0f, m_Yaw, 0.0f });
    Physics().SetVelocity(glm::vec3(0.0f));
    const glm::vec3 v = Physics().GetVelocity();

    if (auto hit = Physics().RayCast(origin, dir, 50.0f))  // std::optional<RayHit>
        CS_INFO("hit entity {0} at {1} m", hit->EntityId, hit->Distance);

    for (Cosmic::Entity e : Physics().OverlapSphere(centre, 3.0f))
        Damage(e);
}
```

Rays and overlaps **ignore this entity automatically** — the proxy passes its own UUID as the ignore
id, so you never hit yourself. `RayHit::EntityId` is a UUID value; resolve it with
`GetScene().FindByUUID(Cosmic::UUID(hit->EntityId))`. `World()` and `Body()` are exposed for
anything the proxy does not wrap.

`IsGrounded(maxDist)` is a convenience down-ray from the entity's world origin — pass a distance
that clears the collider's bottom. **For an actual walker use `Character().IsGrounded()`**, which
asks the controller rather than guessing with a ray.

### `Character()`

The Jolt `CharacterVirtual` walker — no forces, no torque, just "move this way".

```cpp
void OnFixedUpdate(float dt) override
{
    Character().Move(dir * MoveSpeed);           // horizontal VELOCITY; the Y component is ignored
    if (Input::IsKeyPressed(CS_KEY_SPACE) && Character().IsGrounded())
        Character().Jump(JumpSpeed);             // launch velocity, m/s
    const glm::vec3 n = Character().GetGroundNormal();
    const glm::vec3 v = Character().GetVelocity();
}
```

`Move` takes a **velocity**, not a delta — do not multiply by `dt`. The entity needs a
`CharacterControllerComponent`; without one every call is a no-op and `IsGrounded()` returns false.
`WalkController.h` (world-relative, minimal) and `ForgeIsle`'s `PlayerController.h` (view-relative
with mouse-look) are the two shipped worked examples.

### `Signals()`

The scene `EventBus` — the one channel that connects UI buttons, the screen flow, and other scripts.

```cpp
Signals().Emit("left_wins");                                  // broadcast

auto h = Signals().Connect("door_opened",                     // subscribe to one name
             [this](Cosmic::Entity source) { Celebrate(source); });
Signals().Disconnect(h);
```

Prefer overriding `OnSignal(name, source)` for a catch-all — the host wires that for you and tears
it down cleanly. Use `Connect` when you want one specific signal and an explicit lifetime. Dispatch
is same-frame, ordered, main-thread. `PongBall.h` emits `left_wins`/`right_wins` on match point,
which is what the ForgePong flow routes to its Win screen; `StoryUiBinding.h` receives
`story_choose_<i>` in `OnSignal`.

### `Flow()`

Read and write the blackboard of the `FlowMachine` currently driving this scene. No-ops and
defaults when no flow is running.

```cpp
Flow().SetBool("has_key", true);
Flow().AddNumber("coins", 5.0);
if (Flow().GetNumber("coins") >= 100.0) Signals().Emit("shop_unlocked");
const std::string difficulty = Flow().GetString("difficulty");
```

The typed helpers (`GetBool`/`SetBool`, `GetNumber`/`SetNumber`/`AddNumber`, `GetString`/`SetString`)
wrap `GetVar`/`SetVar`, which trade in `FlowValue` (`Bool` / `Number` / `String` / `Enum`; an `Enum`
value lives in `String`). Guards on flow transitions read the same variables, so a script setting
one can drive a screen change with no C++ in between. See
[`flow-and-story.md`](flow-and-story.md).

### `Telemetry()`

Push a named scalar channel per step, for live plotting in the editor's Telemetry panel:

```cpp
void OnFixedUpdate(float) override
{
    Telemetry().Push("height", GetComponent<Cosmic::TransformComponent>().Position.y);
    Telemetry().Push("velY",   Physics().GetVelocity().y);
}
```

The engine stays name-agnostic — a host installs an `ITelemetrySink`, and with no sink installed
`Push` is a cheap no-op, so shipped apps pay nothing. The sink must be installed **before**
`Instantiate` to catch pushes from `OnCreate`/`OnStart`. `PhysicsBall.h` and `BouncingBall.h` are
the shipped examples.

### `Nav()` — 3D only

```cpp
Nav().SetTarget(destination);      // steer this entity's agent over the baked navmesh
Nav().Stop();
if (Nav().HasArrived()) Idle();
const glm::vec3 p = Nav().Position();

Cosmic::NavPath   path = Nav().FindPath(a, b);       // queries — no agent required
Cosmic::NavRayHit wall = Nav().Raycast(a, b);
if (auto pt = Nav().NearestPoint(somewhere)) Nav().SetTarget(*pt);
uint32_t rng = 1234;                                  // caller-owned, deterministic
if (auto wander = Nav().RandomPointAround(home, 20.0f, rng)) Nav().SetTarget(*wander);
```

Arrival is also broadcast as the `nav.arrived` signal on the scene bus — catch it in `OnSignal`
rather than polling if that reads better. The queries are usable by any script, agent or not.
See [`navigation-and-ai.md`](navigation-and-ai.md).

### `Animator()` — 3D only

```cpp
Animator().CrossfadeTo("project://models/hero.gltf#Run", 0.2f);   // timed blend; retargets in flight
Animator().Play("project://models/hero.gltf#Idle");               // hard switch (seconds <= 0)
Animator().SetPlaying(false);
if (Animator().IsCrossfading()) { /* … */ }
const std::string clip = Animator().CurrentClip();
```

Clip paths are `"file#clip"`. Every call is a no-op without an `AnimatorComponent`. Call
`CrossfadeTo` from `OnUpdate`: both hosts advance animators *after* scripts, so the fade lands the
same frame. See [`animation.md`](animation.md).

### `Voxels()` — 3D only

Binds to this entity's `VoxelVolumeComponent` if it has one, otherwise the scene's first volume.

```cpp
Voxels().Break(origin, forward, Reach);                        // clear the first solid cell hit
Voxels().Place(origin, forward, Reach, (uint16_t)PlaceBlock);  // place on the hit face
Voxels().Set(Voxels().WorldToVoxel(worldPos), 0);              // direct edit, world voxel coords
const uint16_t id = Voxels().Get(x, y, z);
Cosmic::VoxelRayHit h = Voxels().RayCast(origin, dir, maxDist);
```

`Get`/`Set` take **world voxel coordinates**; `RayCast`/`Break`/`Place` take a world ray. Edits mark
chunks dirty and the render + collision rebuild picks them up next frame. Everything is a safe no-op
before the volume is initialised (the first `Scene::SyncVoxelVolumes`) or with no voxel entity in
the scene. `VoxelDigger.h` is the shipped example. See [`voxels.md`](voxels.md).

### Which proxies a 2D build has

`Nav()`, `Animator()` and `Voxels()` are compiled out of the 2D configuration — the components they
reach do not exist there. Naming one in a 2D build is a **compile error**, which is the intent: the
failure is at your desk, not at runtime.

Physics is *not* fenced. Jolt ships in both configurations, so `Physics()` and `Character()` are
available to a 2D game exactly as written — only mesh and terrain-heightfield colliders are 3D-only.

If one script must serve both configurations, fence the 3D-only calls:

```cpp
#ifndef COSMIC_2D_ONLY
    Animator().CrossfadeTo(clip, 0.2f);
#endif
```

---

## Common patterns

**Sample input in `OnUpdate`, act in `OnFixedUpdate`.** Input is frame-shaped; physics is
step-shaped. `PlayerController` accumulates yaw/pitch from the mouse in `OnUpdate` and applies the
resulting movement vector through `Character().Move` in `OnFixedUpdate`.

**Edge-detect held input yourself.** `Input::IsKeyPressed` / `IsMouseButtonPressed` are *state*
polls, not edges. Keep a `bool m_PrevLmb` and compare — `VoxelDigger` and `PlayerController` both do
this to turn "button held" into "one dig per click".

**Deadzone every gamepad axis.** `if (std::fabs(gx) > 0.2f) …` — the template scripts standardise on
0.2. See [`events-and-input.md`](events-and-input.md).

**Keep per-entity state in a `SystemScript` keyed by UUID.** Entity handles are recycled after a
destroy; `IDComponent::ID.Value()` is stable. (There is no `Entity::GetUUID()` — go through the
component.)

**Talk to other entities through tags or signals, not pointers.** Tags survive a scene reload;
raw pointers do not. `PongBall` finds `"PaddleL"`/`"PaddleR"`/`"ScoreL"` by tag every frame and it
is cheap enough.

**Release anything global in `OnDestroy`.** `PlayerController` releases cursor capture there, so a
flow that pushes a pause menu always gets a live cursor back.

**Write scripts that are testable headlessly.** The macros work in-exe with no DLL, so a pure-logic
script can be registered and ticked in a doctest — `tests/test_scripthost.cpp` and
`tests/test_template_scripts.cpp` do exactly that.

---

## Pitfalls

**"My script doesn't run."** In order: the class is registered in `Module.cpp` · the module was
built (Ctrl+B) and loaded · the entity's `NativeScriptComponent::ClassName` matches the registered
name exactly · you are in **Play** (scripts never run in edit mode). The Inspector shows
`'X' is not currently loaded.` when the name does not resolve, and the log carries one
`unknown script class 'X'` warning.

**"No scripts appear in the Class dropdown."** The game module has not been built or failed to load
— the Inspector says so in place of the list.

**"My field doesn't show in the Inspector."** It is not `public`, or there is no `CS_FIELD` line for
it, or its type is not a reflection kind.

**"My tuned value didn't apply."** Values are pushed at `Instantiate`. A field the component's map
does not carry keeps the C++ default — re-select the entity so the editor backfills it, or press
Ctrl+B after adding the field.

**"I tuned a value during Play and it reverted."** Expected. Nothing is pulled back on Stop; the
edit scene is authoritative.

**"`OnStart` ran twice."** A flow pushed/popped a state that reloaded the scene. Make `OnStart`
idempotent — derive state from live components.

**"A script on a deactivated entity still fired."** Only `OnUpdate` and `OnFixedUpdate` are gated by
`IsActiveInHierarchy`. Everything else — including `OnCreate`, `OnStart` and the contact callbacks
— is not.

**"Ctrl+B does nothing."** You are in Play. The build refuses with
`"Stop Play before rebuilding scripts."`

**"My undo history vanished after a rebuild."** The stack is cleared by hot reload, and by entering
and leaving Play. See
[`scenes-and-serialization.md`](scenes-and-serialization.md#undo-and-redo--commandstack).

**"My character sinks / flies."** `Character().Move` takes a velocity, not a delta — do not multiply
by `dt`. Its **Y component is ignored**: gravity and `Jump` own the vertical axis (the controller's
own gravity defaults to −9.81 m/s²).

**"`Physics().RayCast` never hits anything."** There is no physics session (edit mode / no
`OnPhysicsStart`), or this entity has no body. Every proxy call degrades to an empty result rather
than reporting the reason.

**"`Nav()` / `Animator()` / `Voxels()` won't compile."** You are building the 2D configuration.
Those three are fenced out; `Physics()` and `Character()` are not.

**"My `SystemScript` sees inactive entities."** Membership is the raw query — it does not filter on
`Active`. Filter in the loop.

**"The span I stored from `OnUpdateAll` dangles."** It is host-owned scratch, valid for the call
only. Copy what you keep.

**"Telemetry pushes from `OnStart` are missing."** The sink was installed after `Instantiate`.

---

## See also

- [`entities-and-components.md`](entities-and-components.md) — `NativeScriptComponent`,
  `SystemScriptComponent`, the `Active`/`Enabled` gates, and every component a script touches.
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — how script fields ride along in
  `.cscene`, the reflection registry the `CS_*` macros feed, and `CommandStack`.
- [`project-anatomy.md`](project-anatomy.md) — the plugin-DLL model, `PlayerLayer`, and the
  shared-allocator rule that governs everything crossing the DLL boundary.
- [`getting-started.md`](getting-started.md) — scaffolding a project so `Module.cpp` and the build
  exist in the first place.
- [`physics.md`](physics.md) · [`../reference/physics.md`](../reference/physics.md) — bodies,
  colliders, the character controller, queries, contact events.
- [`flow-and-story.md`](flow-and-story.md) — `.cflow` variables and guards behind `Flow()`, and the
  `EventBus` signals behind `Signals()`.
- [`navigation-and-ai.md`](navigation-and-ai.md) · [`animation.md`](animation.md) ·
  [`voxels.md`](voxels.md) — the three 3D-only subsystems behind `Nav()`, `Animator()`, `Voxels()`.
- [`serial-and-telemetry.md`](serial-and-telemetry.md) — the telemetry store behind `Telemetry()`.
- [`time-and-ticks.md`](time-and-ticks.md) — variable vs fixed timestep, and why pause freezes
  `OnFixedUpdate` without a guard in your script.
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — the configuration rules
  behind the fenced proxies.
