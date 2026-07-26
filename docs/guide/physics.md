# Physics — Guide

**What this covers:** authoring physics in a scene — `RigidBodyComponent` and the three motion
types, the collider set (box / sphere / capsule, plus the 3D-only mesh and terrain colliders),
triggers, the category/mask collision filter, the `CharacterControllerComponent` walk model,
queries and contact callbacks from a script, the fixed-step tick order every one of those is
specified against, and swapping the whole simulator for your own.
**Source of truth:** `Cosmic/src/physics/PhysicsWorld.{h,cpp}`, `physics/ScenePhysics.{h,cpp}`,
`physics/CharacterController.h`, `physics/PhysicsTypes.h`, `physics/PhysicsBackend.{h,cpp}`,
`physics/backends/JoltBackend.cpp`, `physics/backends/NullBackend.cpp`, `scene/Components.h`,
`scene/Components3D.h`, `scene/Scene.{h,cpp}`, `scripting/ScriptableEntity.h`,
`reflect/TypeRegistry.cpp`, `Projects/Starforge/src/StarforgeApp.cpp`,
`Projects/Starforge/src/ViewportController.cpp`, `Projects/Starforge/src/panels/InspectorPanel.cpp`,
`Cosmic/src/layers/PlayerLayer.cpp`, `tests/test_physics_scene.cpp`, `test_physics_character.cpp`,
`test_physics_events.cpp`, `test_physics_2d.cpp`, `test_physics_backend.cpp`
**API Reference:** [../reference/physics.md](../reference/physics.md) — every call, every
signature, every failure mode · **How it works:**
[../systems/physics-backends.md](../systems/physics-backends.md) — why `PhysicsWorld` is a
dispatcher, and the full backend contract
**Configuration:** **both.**

> ## Physics ships in *both* engine builds
>
> This is the most common wrong assumption about Cosmic's 2D configuration, so it is stated first.
> `COSMIC_2D_ONLY` does **not** remove physics. Jolt is linked into both engines, the whole
> `physics/` directory compiles on both, and `scripting/ScriptableEntity.h` includes
> `physics/ScenePhysics.h` **outside** the `#ifndef COSMIC_2D_ONLY` fence — deliberately, so a 2D
> game gets the complete body, query and character-controller surface.
>
> What *is* 3D-only is the pair of colliders whose shape is derived from 3D world geometry, plus
> voxel chunk collision:
>
> | 3D only | Why |
> | --- | --- |
> | `MeshColliderComponent` | needs `graphics/Mesh.h` + `PrimitiveMeshComponent` |
> | `TerrainColliderComponent` | needs `terrain/Terrain.h`'s CPU heightfield |
> | per-chunk voxel bodies | needs `voxel/` |
> | `PhysicsWorld::DebugDraw` | draws through `Renderer3D` (and needs `JPH_DEBUG_RENDERER`) |
>
> Everything else — `RigidBodyComponent`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`,
> `CharacterControllerComponent`, all four contact callbacks, every query, the backend registry —
> is dimension-agnostic. `tests/test_physics_2d.cpp` is the proof, and
> [Physics in a 2D game](#physics-in-a-2d-game) below is the section for it. See
> [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) §4.3.

**This chapter is the task-oriented half.** *"Make a crate fall", "make a character climb stairs",
"make a trigger fire once."* Per-call detail — signatures, defaults, what each call returns when it
fails — lives in [`../reference/physics.md`](../reference/physics.md) and is **not** repeated here.
Where you need the exact contract, follow the link.

---

## Quick start

### Make a crate fall (in the editor)

1. **Entity ▸ Empty**, name it `Ground`. In the Inspector, **Add Component ▸ Physics ▸ BoxCollider**
   and set `HalfExtents` to `(20, 0.5, 20)`. Drop it to `y = -0.5` so its top face is at `y = 0`.
   *No `RigidBody` needed* — a collider on its own is an implicit static body.
2. **Entity ▸ Empty**, name it `Crate`, put it at `y = 5`. Add **BoxCollider** (default half-extents
   are `0.5`), then **Add Component ▸ Physics ▸ RigidBody** and change **Motion** from `Static` to
   `Dynamic`.
3. Press **Play**. The crate falls and lands on the slab.

Step 2's last sentence is the one people trip on: **`RigidBodyComponent::Motion` defaults to
`Static`**, so a freshly added rigid body does nothing until you change it.

### Make a crate fall (in code)

```cpp
#include <Cosmic.h>

using namespace Cosmic;

Entity ground = scene->CreateEntity("Ground");
ground.GetComponent<TransformComponent>().Position = { 0.0f, -0.5f, 0.0f };
ground.AddComponent<BoxColliderComponent>().HalfExtents = { 20.0f, 0.5f, 20.0f };
// collider with no RigidBody => implicit static body

Entity crate = scene->CreateEntity("Crate");
crate.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, 0.0f };
auto& rb = crate.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
rb.Mass        = 20.0f;
rb.Restitution = 0.0f;                                    // a crate, not a ball
crate.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
```

The one-argument constructor `RigidBodyComponent(MotionType)` exists precisely so code-authored
bodies do not inherit the `Static` default by accident.

### Running it outside the editor

If you are hosting the scene yourself (a test, a tool, a custom layer rather than `PlayerLayer`),
the session is five calls:

```cpp
PhysicsWorld world;                        // held BY VALUE — it is concrete, not abstract
world.Init();                              // default settings: gravity (0, -9.81, 0)
scene.OnPhysicsStart(world);               // build bodies from the authored components

const float dt = 1.0f / 60.0f;
for (int i = 0; i < 600; ++i)
{
    scripts.FixedTick(dt);                 // scripts' OnFixedUpdate
    scene.OnPhysicsStep(dt);               // the simulation
    scene.DispatchPhysicsEvents(scripts);  // OnCollision* / OnTrigger*
}

scene.OnPhysicsStop(world);                // destroy every body
world.Shutdown();
```

That is exactly what `tests/test_physics_scene.cpp` does, and exactly what the editor's Play button
and `PlayerLayer` do around your game.

---

## Bodies exist only while a session runs

This is the single most important structural fact about Cosmic physics, and it explains a lot of
otherwise-surprising behaviour.

**The components are the authored truth; the bodies are derived.** In edit mode there is no
simulation object of any kind — no `PhysicsBody`, no Jolt state, nothing. `Scene::OnPhysicsStart`
walks the registry once and creates a body per qualifying entity; `Scene::OnPhysicsStop` destroys
them all. Consequences:

- **Nothing falls in the editor viewport.** Colliders draw as wireframes and that is all. Press
  Play to simulate.
- **Every `PhysicsBody` / `CharacterHandle` dies at Stop.** Never store one across a session, and
  never serialize one. They are not on the components for exactly this reason.
- **Entities spawned *during* Play get no body.** `BuildBodies` runs once. An entity you
  `CreateEntity` mid-session is inert to physics until the session restarts. (If you need spawned
  physics objects, create the body yourself against
  [`PhysicsWorld::CreateBody`](../reference/physics.md#physicsworldcreatebody) and track it.)
- **Values you tune during Play are discarded on Stop**, like every other Play-mode edit — the
  editor swaps back to the untouched edit scene.

## The fixed-step tick order

Physics advances on the **fixed** timestep, never the render frame. The order below is a contract:
every call in the physics API is specified against it, and the reference chapter opens by restating
it.

```
per fixed step, in order:

  scripts' OnFixedUpdate          ← you set velocities, forces, Character().Move(), transforms
  Scene::OnPhysicsStep(dt)        ← the simulation (expanded below)
  Scene::OnNavStep(dt)            ← 3D only: the nav crowd, AFTER physics
  Scene::DispatchPhysicsEvents()  ← OnCollisionEnter/Exit, OnTriggerEnter/Exit
```

and `OnPhysicsStep` is itself four ordered stages (five in the 3D build):

```
  0. rebuild collision for voxel chunks edited/streamed since last step   (3D only, budget 8)
  1. push kinematic targets   — MoveKinematic from each Kinematic body's world transform
  2. PhysicsWorld::Step(dt)   — EXACTLY ONCE per accumulated fixed step
  3. write back               — each Dynamic body's world pose into its TransformComponent
  4. tick characters          — CharacterController::Tick, then write its position back
```

**Where the rate comes from.** `PlayerLayer` rides the engine's own fixed pass, so the rate is
`Application::SetFixedTimestepHz` (default **60 Hz**, clamped to `[1, 1000]`). Starforge's Play mode
runs its own accumulator at a fixed `1/60` with a **catch-up clamp of 8 steps per frame**, so a
long hitch drops simulation time rather than spiralling. Raising the rate changes physics
*behaviour*, not just its precision — see
[`time-and-ticks.md`](time-and-ticks.md#physics-is-the-load-bearing-consumer).

**The rule this buys you:** never touch physics from `OnUpdate`. Put every force, impulse,
velocity write and `Character().Move()` in `OnFixedUpdate`.

---

## The three motion types

`RigidBodyComponent::Motion` picks which of the three code paths in `ScenePhysics::Step` touches
your entity — and that, not the physical description, is the useful way to think about it.

| Motion | Reads the transform each step? | Writes it back? | Use it for |
| --- | --- | --- | --- |
| `Static` (**default**) | no | no | the ground, walls, level geometry — anything that never moves |
| `Kinematic` | **yes** (stage 1) | no | platforms, lifts, doors, rotating hazards — script-driven movers |
| `Dynamic` | no | **yes** (stage 3) | crates, balls, ragdolls, debris — anything simulated |

### Make the ground

A collider with **no** `RigidBodyComponent` is an implicit static body. That is the idiom the whole
codebase uses — every ground slab in the tests, every arena plate in the Starforge playground:

```cpp
Entity floor = scene->CreateEntity("Arena Floor");
floor.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
floor.AddComponent<BoxColliderComponent>().HalfExtents = { 9.0f, 0.25f, 7.0f };
```

Adding an explicit `RigidBodyComponent` with `Motion = Static` is equivalent; it just gives you the
friction and restitution fields.

### Make a moving platform

Set `Motion = Kinematic` and **move the entity's transform in `OnFixedUpdate`**. `ScenePhysics`
reads that transform in stage 1 and calls `MoveKinematic`, which derives the velocity that reaches
your target pose in one step — so the platform *pushes* dynamic bodies resting on it instead of
passing through them.

```cpp
#include <Cosmic.h>
#include <cmath>

class Platform : public Cosmic::ScriptableEntity
{
public:
    float Travel = 4.0f;      // metres each way
    float Speed  = 1.5f;      // m/s

protected:
    void OnStart() override { m_Home = GetComponent<Cosmic::TransformComponent>().Position; }

    void OnFixedUpdate(float dt) override
    {
        m_T += dt * Speed;
        GetComponent<Cosmic::TransformComponent>().Position =
            m_Home + glm::vec3(0.0f, std::sin(m_T) * Travel, 0.0f);
    }

private:
    glm::vec3 m_Home{ 0.0f };
    float     m_T = 0.0f;
};
```

Do **not** call `SetBodyTransform` for this — that is a teleport, it carries no velocity, and
anything standing on the platform will be left behind or tunnelled through. `SetBodyTransform` is
for respawns and hard resets.

### Make a crate fall

`Motion = Dynamic` plus a collider, and gravity does the rest. The fields that shape how it falls:

| Field | Default | What it does |
| --- | --- | --- |
| `Mass` | `1.0` kg | dynamic bodies only |
| `Friction` | `0.5` | surface friction |
| `Restitution` | `0.1` | `0` = no bounce, `1` = perfectly elastic |
| `LinearDamping` / `AngularDamping` | `0.05` | air-drag-ish velocity decay |
| `GravityFactor` | `1.0` | scales gravity **for this body**; `0` = floats |
| `CCD` | `false` | continuous collision — turn on for fast, small bodies |
| `StartAsleep` | `false` | spawn settled, wake on contact or `Activate` |

**A `Dynamic` body ignores its `TransformComponent`.** Stage 3 overwrites the transform every step,
so editing `Position` mid-session does nothing visible. Use
[`SetBodyTransform`](../reference/physics.md#physicsworldsetbodytransform) to teleport, or
`Physics().SetVelocity()` to steer.

> **Write-back has a side effect on rotation.** Stage 3 sets `TransformComponent::RotationQuat` and
> flips `UseQuatRotation = true`. The two representations are independent (`Components.h:114`) —
> the Euler `Rotation` field is **stale** from that moment on and reading it gives you the authored
> value, not the simulated one. Read `RotationQuat`.

---

## Colliders

A body's shape comes from the collider components on the **same entity**. More than one collider on
an entity produces a **compound shape** — that is how you build an L, a table, a capsule with a
sensor bubble around it.

| Component | Key fields | Build |
| --- | --- | --- |
| `BoxColliderComponent` | `HalfExtents` (pre-scale), `Offset` | both |
| `SphereColliderComponent` | `Radius`, `Offset` | both |
| `CapsuleColliderComponent` | `Radius`, `HalfHeight`, `Offset` | both |
| `MeshColliderComponent` | `Convex` | **3D only** |
| `TerrainColliderComponent` | *(none — derives everything from the sibling `TerrainComponent`)* | **3D only** |

All five also carry `IsTrigger`; the first four carry `Enabled` (hidden in the Inspector, but
serialized and honoured — an unticked collider is simply not baked).

### Sizing rules that bite

- **`HalfExtents` and `Radius` are half-measures, pre-scale.** The entity's world **scale** is baked
  into the primitive at build time, so a `0.5` half-extent on an entity scaled `3` gives a
  1.5 m half-extent — a 3 m box.
- **`HalfHeight` on a capsule excludes the caps.** Total height is `2 * (HalfHeight + Radius)`.
- **A sphere under non-uniform scale uses the X scale and logs a warning.** A capsule uses X for its
  radius and Y for its half-height, also with a warning. Keep scale uniform on curved colliders.
- **`Offset` is *not* scaled.** The shape's dimensions honour world scale; its offset is applied in
  unscaled body-local metres. On an unscaled entity — the overwhelmingly common case — the two agree
  and you will never notice. On a scaled entity with a non-zero offset, the runtime shape sits
  somewhere the editor's collider wireframe does not (the overlay applies the full world matrix to
  the offset). Prefer offsetting a child entity over offsetting a collider on a scaled parent.

### Fit a collider to a mesh

Select a single entity that has a `MeshRendererComponent` with a loaded mesh; the Inspector shows a
**Fit to mesh** button on its `BoxCollider` or `SphereCollider`. It sets `HalfExtents`/`Radius` and
`Offset` from the mesh's local AABB, as one undoable edit. It is the fastest way to get a plausible
collider onto an imported model.

### Mesh colliders (3D only)

`MeshColliderComponent` has two modes and one big caveat.

- **`Convex = true`** builds a convex hull. Hulls can be `Dynamic`.
- **`Convex = false`** builds a triangle mesh. Triangle meshes are **static/kinematic only** — using
  one on a dynamic body logs *"a concave triangle mesh can't be dynamic"*.

**The caveat:** the geometry comes from a sibling `PrimitiveMeshComponent`, whose CPU vertices are
regenerated on demand. If the entity has an **imported** mesh instead (a `MeshRendererComponent`
pointing at a `.gltf`/`.fbx`), there is no CPU copy to read, so the build **falls back to the mesh's
local AABB as a box** and warns:

```
MeshCollider on an imported mesh (entity N) uses an AABB box in v1.
```

That is a documented v1 limit, not a bug you can configure around. For accurate collision on
imported geometry today, author primitive colliders (a compound of boxes and capsules is usually
better for gameplay anyway).

### Terrain colliders (3D only)

Add `TerrainColliderComponent` alongside a `TerrainComponent` and the terrain's CPU heightfield
becomes a static collision surface. It has no fields at all. Three behaviours worth knowing:

- **The entity transform is ignored.** Terrain is placed by its own specification, so the collider
  is built at the world origin with identity rotation and forced to `Static` — matching
  `TerrainComponent`'s own rule that its `TransformComponent` does not apply.
- **The far +X/+Z edge row is dropped.** Terrain resolutions are odd (`32·2^k + 1`) and Jolt's
  heightfield rounds its sample count up to a multiple of 2, so the build uses the `(n−1)²` grid.
  A harmless loss at the terrain rim; do not put gameplay on the last row.
- **Build the terrain first.** If the sibling `TerrainComponent` has no built `TerrainAsset` at
  `OnPhysicsStart`, the collider is skipped with a warning. Starforge calls
  `Scene::SyncWorldSystems()` before `OnPhysicsStart` for exactly this reason.

Voxel volumes get collision automatically — one static mesh body per resident chunk, rebuilt when a
chunk goes dirty (budget 8 chunk bodies per fixed step). See
[`voxels.md`](voxels.md#voxel-collision).

---

## Make a trigger fire once

A **trigger** (sensor) reports overlap and applies no contact response — things fall straight
through it. Tick `IsTrigger` on any collider:

```cpp
// A kill plane / pickup zone: a sensor slab with no RigidBody at all.
Entity zone = scene->CreateEntity("Kill Plane");
zone.GetComponent<TransformComponent>().Position = { 0.0f, -8.0f, 0.0f };
auto& col = zone.AddComponent<BoxColliderComponent>();
col.HalfExtents = { 50.0f, 0.5f, 50.0f };
col.IsTrigger   = true;
```

Then catch the transitions on **either** entity's script:

```cpp
class KillPlane : public Cosmic::ScriptableEntity
{
protected:
    void OnTriggerEnter(Cosmic::Entity other) override
    {
        CS_INFO("'{0}' fell out of the world", other.GetComponent<Cosmic::TagComponent>().Tag);
        Signals().Emit("player.died");
    }
};
```

`tests/test_physics_events.cpp` and `test_physics_2d.cpp` both assert the exact contract: **one
`OnTriggerEnter`, one `OnTriggerExit`, and zero `OnCollisionEnter`** for a body falling through a
sensor, which is what "fire once" means here — these are edge transitions, not per-step states.
There is no "still overlapping" event; latch it yourself between enter and exit if you need one.

**Four things about triggers that are not obvious:**

1. **`IsTrigger` is per *body*, not per collider.** The build ORs the flag across every collider on
   the entity, so one ticked collider turns the **whole compound body** into a sensor. If you want a
   solid thing with a sensor bubble, they must be two entities.
2. **Two non-movers never pair.** A trigger only sees `Dynamic` and `Kinematic` bodies. Two static
   colliders, or a static collider and a trigger, generate nothing.
3. **A character controller fires no trigger events at all.** The controller is a `CharacterVirtual`
   query volume, not a body in the simulation, and no character contact listener is installed — so
   walking a `CharacterControllerComponent` through a sensor raises **nothing**. Detect it from the
   trigger's side with an `OverlapSphere`/`OverlapBox` poll in `OnFixedUpdate`, or give the
   character a `Kinematic` rigid body + collider that you drive alongside the controller.
4. **In `ContactEvent`, `EntityA` is the sensor** for the two trigger kinds. For the collision kinds
   the order is unconstrained — do not depend on it.

---

## The collision filter

Every body carries a 16-bit **category** (which group it is in) and a 16-bit **mask** (which groups
it collides with). Both live on `RigidBodyComponent`:

```cpp
constexpr uint32_t kPlayer  = 1 << 0;
constexpr uint32_t kEnemy   = 1 << 1;
constexpr uint32_t kTerrain = 1 << 2;
constexpr uint32_t kPickup  = 1 << 3;

auto& rb = player.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
rb.CollisionCategory = kPlayer;
rb.CollidesWith      = kTerrain | kEnemy | kPickup;   // NOT other players
```

**The rule is two-sided.** Two bodies collide only when *each* one's category is in the *other's*
mask:

```
(A.Category & B.CollidesWith)  &&  (B.Category & A.CollidesWith)
```

Setting only one side does nothing — the single most common way to be surprised by this filter. If
players should pass through each other, `kPlayer` must be absent from `CollidesWith` on the player
body; there is no per-pair override.

Defaults are `Category = 0x0001`, `CollidesWith = 0xFFFF`, so out of the box everything collides
with everything.

**Where it applies.** The filter is enforced at contact validation *and* in every query's body
filter — so `layerMask` on `RayCast`, `SphereCast`, `OverlapSphere` and `OverlapBox` is tested
against the same `Category` bits. `0xFFFF` (the default) disables the check rather than testing
every bit.

**Where it does not apply.** The coarse broadphase layer (`Static` / `Dynamic` / `Trigger` /
`Character`) is assigned automatically from motion type and trigger flag; you never set it, and the
category/mask filter is applied *on top* of it. That two-level model is why a static-vs-static pair
is rejected before your mask is ever consulted.

---

## Make a character climb stairs

A dynamic rigid body makes a poor walker: it tips over, slides down ramps, and bounces off steps.
`CharacterControllerComponent` is the purpose-built alternative — a kinematic capsule with slope and
step handling, wrapped in a walk model that owns gravity, jump and floor-sticking.

```cpp
Entity player = scene->CreateEntity("Player");
player.GetComponent<TransformComponent>().Position = { 0.0f, 1.0f, 0.0f };
auto& cc = player.AddComponent<CharacterControllerComponent>();
cc.Height      = 1.8f;    // TOTAL capsule height, caps included
cc.Radius      = 0.3f;
cc.MaxSlopeDeg = 45.0f;   // steeper than this and it slides
cc.StepHeight  = 0.35f;   // obstacles up to this high are stepped onto — the stairs knob
cc.Mass        = 80.0f;
player.AddComponent<NativeScriptComponent>("WalkController");
```

**`StepHeight` is the stairs setting.** A step riser at or below it is climbed without a jump;
above it, the capsule stops dead. `MaxSlopeDeg` is the ramp equivalent — `tests/test_physics_character.cpp`
walks a character up a 25° ramp under the 45° default and asserts it gains height.

The controller does **not** need a `RigidBodyComponent` or a collider — it owns its own capsule. In
fact `BuildBodies` skips the rigid-body pass entirely for any entity carrying a
`CharacterControllerComponent`, so adding both gives you only the character.

### Drive it from a script

The `Character()` proxy is the whole gameplay surface. This is the shipped `WalkController` template
script, trimmed:

```cpp
class WalkController : public Cosmic::ScriptableEntity
{
public:
    float MoveSpeed = 4.0f;    // m/s
    float JumpSpeed = 5.0f;    // launch velocity; apex ≈ JumpSpeed² / 19.6

protected:
    void OnFixedUpdate(float) override
    {
        using namespace Cosmic;

        glm::vec3 dir(0.0f);
        if (Input::IsKeyPressed(CS_KEY_W)) dir.z -= 1.0f;   // forward = -Z
        if (Input::IsKeyPressed(CS_KEY_S)) dir.z += 1.0f;
        if (Input::IsKeyPressed(CS_KEY_A)) dir.x -= 1.0f;
        if (Input::IsKeyPressed(CS_KEY_D)) dir.x += 1.0f;
        if (glm::length(dir) > 1.0f) dir = glm::normalize(dir);

        Character().Move(dir * MoveSpeed);

        if (Input::IsKeyPressed(CS_KEY_SPACE) && Character().IsGrounded())
            Character().Jump(JumpSpeed);
    }
};
```

The three calls and their contracts:

- **`Move(v)` takes a velocity in m/s and its `.y` component is discarded** — gravity and `Jump` own
  the vertical axis. It also **persists until changed**, so a character told to move once walks
  forever. Set it to zero when input stops (the sample does, implicitly: `dir` is zero with no keys
  down).
- **`Jump(speed)` is a latched request** applied on the next tick *only if grounded*. Two calls
  before a tick are one jump; an ungrounded request is dropped, not buffered. For a target apex
  height *h*, `speed = sqrt(2 · g · h)` — about 4.43 m/s for 1 m.
- **`IsGrounded()` knows about the slope limit and step height.** Prefer it to a manual down-ray.
  (`Physics().IsGrounded()` exists as a convenience raycast, but it is dumber — it does not know
  what counts as walkable.)

### Character positions are at the feet

`GetPosition()` returns the **bottom** of the capsule, not its centre: the shape is built shifted up
by `HalfHeight + Radius` so the handle position is the character's feet. A character resting on
ground whose top face is at `y = 0` reports `y ≈ 0`. Author spawn points accordingly.

### Gravity for a character is separate from world gravity

The controller integrates its own vertical velocity, and `ScenePhysics::BuildBodies` seeds it with a
**hard-coded −9.81 m/s²** — it does *not* read `PhysicsSettings::Gravity`. So a moon-gravity world
(`ps.Gravity = {0, -1.62, 0}`) makes crates float down and leaves every character falling at full
Earth gravity.

There is no field for this on the component and none on the `Character()` proxy, but the underlying
controller is reachable:

```cpp
void OnStart() override
{
    if (auto* c = Character().Ctrl())     // CharacterController*, or nullptr in edit mode
        c->SetGravity(-1.62f);            // negative = downward
}
```

Same route for teleporting (`c->SetPosition(spawn)`) — the proxy does not expose that either.

### Yaw on a character entity does not stick

Stage 4 of the step writes the character's position back **and forces its rotation to identity**
(`RotationQuat = {1,0,0,0}`, `UseQuatRotation = true`) every fixed step. Anything a script writes
into that entity's `Rotation` or `RotationQuat` is overwritten before it is ever drawn — including
the yaw a first-person controller derives from the mouse, and therefore the orientation of any
child entity composed through it.

Work around it by putting the yaw on a **child**: parent a `Yaw` entity under the character, rotate
that, and hang the camera and mesh off it. The shipped `WalkController` sidesteps the issue
differently — its movement is world-relative, so it never authors a yaw at all.

---

## Query the world from a script

`Physics()` is the per-entity proxy. Every call on it is safe when no session is running or the
entity has no body — a no-op or an empty result — so scripts still run in edit-only harnesses.

```cpp
class Turret : public Cosmic::ScriptableEntity
{
public:
    float Range = 25.0f;

protected:
    void OnFixedUpdate(float) override
    {
        using namespace Cosmic;

        const glm::mat4 me  = GetScene().GetWorldTransform(GetEntity());
        const glm::vec3 eye = glm::vec3(me[3]);
        const glm::vec3 fwd = -glm::vec3(me[2]);           // -Z is forward

        // Line of sight. The proxy passes THIS entity's UUID as ignoreEntity for you.
        if (auto hit = Physics().RayCast(eye, fwd, Range))
        {
            if (Entity target = GetScene().FindByUUID(UUID(hit->EntityId)))
                CS_INFO("looking at '{0}' at {1} m",
                        target.GetComponent<TagComponent>().Tag, hit->Distance);
        }

        // Area query — already resolved to entities.
        for (Entity e : Physics().OverlapSphere(eye, 6.0f))
            Signals().Emit("turret.contact");
    }
};
```

| Proxy call | Notes |
| --- | --- |
| `RayCast(origin, dir, maxDist, mask = 0xFFFF)` | closest hit as `std::optional<RayHit>`; `ignoreEntity` = self, automatically |
| `OverlapSphere(center, radius, mask = 0xFFFF)` | returns `std::vector<Entity>` — resolution done for you; unresolvable ids are dropped |
| `AddForce` / `AddImpulse` / `AddTorque` | applied to this entity's body |
| `SetVelocity` / `GetVelocity` | linear velocity, m/s, world space |
| `IsGrounded(maxDist = 1.1f)` | short down-ray convenience; for a walker use `Character().IsGrounded()` |
| `World()` | the `PhysicsWorld*` — for `SphereCast`, `OverlapBox`, `Activate`, statistics, anything not on the proxy |
| `Body()` / `SelfId()` | the raw handle and this entity's UUID |

**Three gotchas the reference spells out and this chapter will not repeat, but you should know
exist:**

- **`RayHit::EntityId` is a UUID**, not an entity handle. Resolve it with
  `Scene::FindByUUID(UUID(id))`.
- **Triggers are hit by raycasts like anything else.** The query body filter checks the category
  mask and `ignoreEntity` only — sensors are *not* excluded. If a ground probe must ignore trigger
  volumes, give them a category bit you mask out.
- **The Jolt backend normalises `direction` for you** and treats a zero-length direction as a miss.
  A custom backend is not obliged to; pass a unit vector and the meaning is unambiguous everywhere.

Reaching past the proxy for the queries it does not wrap:

```cpp
if (auto* w = Physics().World())
{
    std::vector<uint64_t> ids;
    w->OverlapBox(center, half, glm::quat(1, 0, 0, 0), ids, 0xFFFF, Physics().SelfId());
    // ... resolve ids yourself
}
```

## Contact callbacks

Override any of the four on a `ScriptableEntity`; `Scene::DispatchPhysicsEvents` calls them each
fixed step, on the main thread, with the counterpart entity:

```cpp
void OnCollisionEnter(Cosmic::Entity other) override;
void OnCollisionExit(Cosmic::Entity other)  override;
void OnTriggerEnter(Cosmic::Entity other)   override;
void OnTriggerExit(Cosmic::Entity other)    override;
```

Both sides of a contact are dispatched, so you can put the logic on whichever entity owns the
behaviour — the bullet or the target, the pickup or the player.

**What to expect:**

- **They fire on the *fixed* step.** Several can arrive between two rendered frames, or none. Never
  assume one per frame.
- **Enter/exit counts jitter while a stack settles.** The manifold is added, reduced and re-formed
  as bodies come to rest; that is true of every solver. `tests/test_physics_events.cpp` asserts the
  meaningful contract (a landing raises Enter, a separation raises Exit) rather than exact counts,
  and so should you.
- **Events are reported exactly once.** The drain moves and clears. If your host forgets to call
  `DispatchPhysicsEvents` in a step, those events are gone.
- **They fire on inactive entities too.** Only `Tick`/`FixedTick` are gated by
  `IsActiveInHierarchy`; the four contact callbacks are not. Check `Enabled`/`Active` yourself if it
  matters.

---

## Physics in a 2D game

Author everything at `z = 0` and use the shared subset — `RigidBodyComponent` plus box, sphere and
capsule colliders. That is a supported, tested configuration, not a happy accident:
`tests/test_physics_2d.cpp` exists specifically to hold the line.

```cpp
// A platformer playfield: a wide thin slab, a player box that lands on it.
Entity ground = scene->CreateEntity("Ground");
ground.GetComponent<TransformComponent>().Position = { 0.0f, -0.5f, 0.0f };
ground.AddComponent<RigidBodyComponent>(MotionType::Static);
ground.AddComponent<BoxColliderComponent>().HalfExtents = { 40.0f, 0.5f, 40.0f };

Entity box = scene->CreateEntity("Player");
box.GetComponent<TransformComponent>().Position = { -2.0f, 6.0f, 0.0f };
box.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.0f;
box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
```

**What the test pins down, and what it honestly does not:**

- Gravity pulls along −Y and the body rests on the slab. ✔
- The body never tips out of the XY plane (measured ~8.7e-5 of out-of-plane quaternion, i.e. under a
  tenth of a degree). ✔
- **The depth axis is not exactly zero.** Contacts are solved by sequential impulses over a
  four-point manifold whose iteration order is not z-symmetric, so a little of each impulse lands on
  z: **0.59 mm** measured for a body that just falls and rests, **2.05 mm** after ten seconds of
  sliding. Crucially it is **not a rate** — it happens at the landing and then stops, which the test
  asserts by comparing the halfway and ending z. For a 2D game where sprites are ordered by z, that
  bounded offset is harmless; a drift *rate* would silently reorder your scene, and that is what the
  test would catch.
- Triggers work identically: one enter, one exit, no contact force.
- With `ThreadCount = 0`, two runs are bit-identical.

**Seeing colliders in the 2D editor.** `PhysicsWorld::DebugDraw` is a no-op without `Renderer3D`, so
Starforge's 2D mode draws collider wireframes with its own `DrawColliderOverlay2D` — same
**Collider gizmos** toggle in the viewport strip, same selected-bright / resting-dim palette, but it
reads the *components* rather than the backend, which means it also works on a custom backend.

---

## See what the simulation is doing

Three overlays, all on the viewport strip:

| Toggle | Shows | Works when |
| --- | --- | --- |
| **Collider gizmos** | authored collider wireframes (box / sphere / capsule) at the same world transform the runtime bakes | always, edit or Play; both builds |
| **Physics debug** | live body outlines coloured by sleep state, plus contact points | Play only · **Debug config only** (needs `JPH_DEBUG_RENDERER`) · 3D build only |
| **Nav overlay** | the walkable navmesh — see [`navigation-and-ai.md`](navigation-and-ai.md) | 3D build only |

`PhysicsWorld::GetStatistics()` gives you `BodyCount` and `ActiveBodies` (awake, non-sleeping) cheap
enough to poll every frame. `BodyCount == 0` right after `OnPhysicsStart` is the tell-tale for
*"nothing got baked"* — usually a missing collider, an unticked `Enabled`, an inactive entity, or
the null backend.

---

## Swap the backend

`PhysicsWorld` is a **dispatcher, not a simulator**: it holds one `IPhysicsBackend` resolved by name
at `Init` and forwards every call. Registering your own is two lines, in your layer's `OnAttach`,
before any Play session starts:

```cpp
void MyLayer::OnAttach()
{
    Cosmic::PhysicsBackendRegistry::Register("my2d",
        []{ return std::make_unique<My2DPhysics>(); });
    Cosmic::PhysicsBackendRegistry::SetDefault("my2d");
}
```

…or leave the process default alone and select per world:

```cpp
Cosmic::PhysicsSettings ps;
ps.Backend = "my2d";
world.Init(ps);
```

**That is the entire integration.** `ScenePhysics` keeps translating components → `BodyDesc` →
`CreateBody`, so authored scenes, the Inspector, serialization, the `Physics()` proxy and contact
events reaching `OnCollisionEnter` are all unchanged. `tests/test_physics_backend.cpp` proves it by
driving a complete ~150-line AABB integrator (`TinyPhysics`) through the whole stack, with counters
that a silent fall-back to Jolt could not fake.

Two built-ins are always available:

- **`"jolt"`** — the default when the engine was built with `COSMIC_WITH_JOLT` (it is, by default).
- **`"null"`** — always registered. Creates no bodies, steps nothing, returns empty queries. A scene
  with physics authored into it still *loads and runs*; authored poses simply stay put. This is what
  makes `COSMIC_WITH_JOLT=OFF` a real configuration, and it is where an unknown backend name lands.

**An unknown backend name is not fatal.** `Init` logs an error and falls back to `"null"`, leaving a
world where `IsInitialized()` is `true` and nothing simulates — a typo degrades rather than crashes.
If bodies mysteriously do not exist, read the log for:

```
PhysicsWorld: unknown physics backend "..." — falling back to "null".
```

The interface, the contracts an implementation must honour (fixed step, drain-and-clear events,
`ThreadCount == 0` means deterministic, `EntityId` round-trips), and the full worked example are in
[`../systems/physics-backends.md`](../systems/physics-backends.md). The registry's call surface is
in [the reference](../reference/physics.md#iphysicsbackend--physicsbackendregistry).

---

## Common patterns

**Deterministic physics for tests and replays.** Set `ThreadCount = 0` and the simulation is
bit-reproducible run to run — `tests/test_physics_determinism.cpp` and `test_physics_2d.cpp` both
assert exact float equality across two runs. Pair it with a fixed step you control.

```cpp
Cosmic::PhysicsSettings ps;
ps.ThreadCount = 0;
world.Init(ps);
```

**Read a body's state without owning it.** Physics writes dynamic poses back into the ECS, so the
transform *is* the answer for position. Velocity has to come from the body. This is exactly what the
shipped `PhysicsBall` template script does:

```cpp
void OnFixedUpdate(float) override
{
    Telemetry().Push("height", GetComponent<Cosmic::TransformComponent>().Position.y);
    Telemetry().Push("velY",   Physics().GetVelocity().y);
}
```

**Force vs impulse.** `AddForce` is newtons applied for **this step only** — call it every fixed
step for as long as the influence lasts (thrust, wind, buoyancy). `AddImpulse` is newton-seconds
applied once (jumps, explosions, bullet hits): `impulse = mass · desired_velocity_change`.

**Waking a settled body.** Physics engines sleep bodies that stop moving, and sleeping bodies ignore
forces — the classic *"my `AddForce` does nothing"*. Call `Physics().World()->Activate(body)` first,
or check `IsActive(body)`.

**Turn one body's collisions off temporarily.** There is no per-body enable at runtime. Either
author it with a `Category` you can drop out of other bodies' masks, or untick the collider's
`Enabled` and restart the session. (Un-ticking mid-Play does nothing: the bake already happened.)

**A body that ignores gravity but still collides.** `GravityFactor = 0` on the `RigidBodyComponent`
— it stays fully simulated and fully collidable, it just does not fall.

---

## Pitfalls

**"I added a RigidBody and nothing falls."** `RigidBodyComponent::Motion` defaults to **`Static`**.
Change it to `Dynamic`. (The `BodyDesc` you would build by hand defaults to `Dynamic`; the
*component* does not.)

**"I added a RigidBody and there is no body at all."** A rigid body with no collider creates
nothing, and logs *"RigidBody on entity N has no collider — no body created."* Add a collider.

**"Nothing happens in the editor viewport."** There is no simulation outside Play. Bodies exist only
between `OnPhysicsStart` and `OnPhysicsStop`.

**"My spawned enemy falls through the floor."** `BuildBodies` runs once, at session start. Entities
created during Play are invisible to physics.

**"Moving the transform of a dynamic body does nothing."** Stage 3 overwrites the transform every
step. Teleport with `SetBodyTransform`, or steer with velocity/forces.

**"My kinematic platform passes through the crates on it."** You are probably teleporting it with
`SetBodyTransform`. Move its *transform* in `OnFixedUpdate` and let `ScenePhysics` derive the
velocity via `MoveKinematic`.

**"Reading `Rotation` gives me the authored angle, not the simulated one."** Physics writes
`RotationQuat` and sets `UseQuatRotation = true`. The Euler field is stale; read the quaternion.

**"My character's mouse-look yaw snaps back every frame."** The character write-back forces the
entity's rotation to identity each fixed step. Put the yaw on a child entity.

**"My character walks through the pickup trigger and nothing fires."** Character controllers
generate no contact events. Poll from the trigger's side, or add a kinematic body to the character.

**"One collider is a trigger and now the whole object is a ghost."** `IsTrigger` is ORed across all
of an entity's colliders into a single per-body flag. Split it into two entities.

**"A and B still collide even though I set B's mask."** The filter is two-sided; set it on both, or
neither pairing survives — `(A.Category & B.CollidesWith) && (B.Category & A.CollidesWith)`.

**"My imported model's mesh collider is a big box."** It is: imported meshes keep no CPU copy, so
`MeshCollider` falls back to the local AABB and warns. Author primitive colliders instead.

**"My terrain collider was skipped."** The terrain has to be built before `OnPhysicsStart` — call
`Scene::SyncWorldSystems()` first, as Starforge does.

**"Characters fall at 9.81 in my low-gravity world."** `PhysicsSettings::Gravity` drives rigid
bodies; the controller integrates its own hard-coded −9.81. Override it with
`Character().Ctrl()->SetGravity(...)` in `OnStart`.

**"My collider wireframe does not line up with where things actually collide."** Check for a
non-zero collider `Offset` on a **scaled** entity: the shape honours world scale, the offset does
not, and the editor overlay scales it. Offset a child entity instead.

**"Physics is frame-rate dependent."** Something is stepping from `OnUpdate`. Every physics call
belongs in `OnFixedUpdate`.

**"Everything worked and now nothing simulates."** Check the log for the unknown-backend fallback
line — a typo in `PhysicsSettings::Backend` or a `SetDefault` naming a factory that was never
registered leaves you on `"null"`, which is initialised, silent and inert.

**"Contact events arrive one step late."** `DispatchPhysicsEvents` must run *after* `OnPhysicsStep`
in the same fixed step. Check your host's tick order.

---

## See also

**Reference** — [`../reference/physics.md`](../reference/physics.md): every call on `PhysicsWorld`,
`CharacterController`, `ScenePhysics` and `PhysicsBackendRegistry`, plus every value type in
`PhysicsTypes.h`. Start at [`PhysicsWorld::RayCast`](../reference/physics.md#physicsworldraycast)
and [`CharacterController::Tick`](../reference/physics.md#charactercontrollertick).
[`../reference/ecs.md`](../reference/ecs.md) covers the components themselves.

**Systems** — [`../systems/physics-backends.md`](../systems/physics-backends.md): why the
dispatcher shape was chosen, the complete `IPhysicsBackend` contract, and the worked `TinyPhysics`
example. [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) §4.3 explains why
physics is shared and the two geometry-derived colliders are not.

**Guide**

- [`entities-and-components.md`](entities-and-components.md) — the full component catalogue,
  including every physics component's fields, units and defaults, and the `Active`/`Enabled` gates
  that decide what gets baked.
- [`time-and-ticks.md`](time-and-ticks.md#physics-is-the-load-bearing-consumer) — the fixed pass
  itself: why `dt` is constant, what `TimeScale` does to it, and what raising the rate costs.
- [`scripting.md`](scripting.md) — `ScriptableEntity`, the eight proxies, and where
  `OnFixedUpdate` sits relative to everything else.
- [`navigation-and-ai.md`](navigation-and-ai.md) — the navmesh is baked from the same collision view
  these components define, through the same `ScenePhysics::BuildColliderDesc`.
- [`voxels.md`](voxels.md#voxel-collision) — per-chunk static collision, its rebuild budget, and why
  a moved volume leaves its collision behind.
- [`world-systems.md`](world-systems.md) — terrain, and why a scene holds exactly one of them at the
  world origin.

**Tests as executable documentation** — [`tests/test_physics_scene.cpp`](../../tests/test_physics_scene.cpp)
(the session, serialization round-trip, the `Enabled`/`Active` gates),
[`test_physics_character.cpp`](../../tests/test_physics_character.cpp) (ground rest, walking, wall
blocking, slope climbing), [`test_physics_events.cpp`](../../tests/test_physics_events.cpp)
(collision and trigger semantics), [`test_physics_2d.cpp`](../../tests/test_physics_2d.cpp) (the 2D
plane), [`test_physics_backend.cpp`](../../tests/test_physics_backend.cpp) (a complete third-party
backend).

---
*Changelog:*
*2026-07-26 — created (D57). Written from source against `Cosmic/src/physics/`, `scene/Components.h`,
`scene/Components3D.h` and the seven `tests/test_physics_*.cpp` files.*
