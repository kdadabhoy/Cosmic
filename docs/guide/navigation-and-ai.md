# Navigation & AI — Guide

**What this covers:** getting characters to walk somewhere sensible — authoring a `NavMeshComponent`
and **baking** it from the scene's collision view, the signature gate that decides when to rebake,
the `.cnav` sidecar the result rides in, `NavAgentComponent` and the **DetourCrowd** that steers a
group of them, driving an agent from a script through the `Nav()` proxy, the `nav.arrived` signal,
and running navmesh queries with no agent at all.
**Source of truth:** `Cosmic/src/nav/NavWorld.{h,cpp}`, `nav/NavTypes.h`,
`scene/SceneNav.{h,cpp}`, `scene/Components3D.h`, `scene/Scene.h`, `scene/Scene3D.cpp`,
`scripting/ScriptableEntity.h`, `reflect/TypeRegistry3D.cpp`,
`Projects/Starforge/src/StarforgeApp.cpp` (`TickNavMeshes`, `BuildForgePlayground`),
`Projects/Starforge/src/panels/InspectorPanel.cpp`,
`Projects/Starforge/src/ViewportController.cpp`,
`Projects/Starforge/assets/templates/src/scripts/NavCritter.h`, `tests/test_nav_world.cpp`,
`test_nav_bake.cpp`, `test_nav_agents.cpp`
**API Reference:** *none — `nav/NavWorld.h`, `nav/NavTypes.h` and `scene/SceneNav.h` have **no row**
in the [reference manifest](../reference/README.md), so this chapter is the client-facing source for
them.* · **How it works:** *none — there is no `docs/systems/` explainer for navigation either.
(`../systems/cameras-navigation.md` is about **camera** navigation — orbit/fly controllers and the
nav cube — not navmeshes.)*
**Configuration:** **3D only.** `Cosmic/src/nav/` is filtered out of the 2D engine build,
`NavMeshComponent` / `NavAgentComponent` live in `scene/Components3D.h`, `Scene::OnNavStart` /
`OnNavStep` / `OnNavStop` / `GetNav()` are inside `#ifndef COSMIC_2D_ONLY` in `scene/Scene.h`, and
the `Nav()` script proxy is fenced in `scripting/ScriptableEntity.h`. Naming any of it in a
`COSMIC_2D_ONLY` tree is a compile error, by design — see
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

Navigation is [Recast](https://github.com/recastnavigation/recastnavigation) (the bake) plus Detour
(the navmesh, queries and crowd), vendored and linked **private** into the engine DLL behind a
pimpl — exactly the treatment Jolt gets. No `rc*` or `dt*` type appears in any public header, so
`nav/NavTypes.h` is plain glm and PODs, and the whole tier is GL-free and headless-testable.

Three pieces, and it pays to know which one you are touching:

| Piece | What it is | Lives |
| --- | --- | --- |
| `NavMeshComponent` | the **bake recipe** (reflected, serialized) plus a runtime `Ref<NavWorld>` | the scene |
| `NavWorld` | one baked navmesh + its query object + an optional crowd | a `.cnav` sidecar on disk |
| `NavAgentComponent` | one crowd agent's tuning (reflected, serialized) | the scene |

**The sample.** `ForgePlayground`'s nav-critter arena is the worked example: a ramp-and-platform
arena parented under a `Nav Mesh` entity, three `Critter` agents that patrol and chase, and a
`WalkController` player they chase. It is built in code by `StarforgeApp::BuildForgePlayground`
(`StarforgeApp.cpp:3376`), and every snippet marked *from the playground* below is quoted from
there. The AI that drives it is the shipped `NavCritter` template `SystemScript`.

---

## Quick start

### In the editor

1. **Entity ▸ World ▸ Nav Mesh.** That creates an empty marker entity carrying a `NavMeshComponent`.
   There is no dedicated panel — the reflected recipe in the Inspector plus one button *is* the
   authoring UI.
2. **Parent your level geometry under it.** The default `SourceMode` is **From children**, so the
   bake only rasterizes this entity's descendants. That is almost always what you want: it keeps a
   256 m terrain out of a 20 m arena's bake.
3. **Set `AgentRadius` / `AgentHeight` / `AgentMaxClimb`** to match the character that will walk it.
   These three matter far more than the rest.
4. Select the Nav Mesh entity and click **Regenerate now** in the Inspector. The bake runs in the
   background; the chip beside the button reads *Baking…*, then *Baked*.
5. The walkable surface draws as a translucent teal wireframe while the entity is selected. Tick
   `AlwaysRenderHelper` to keep it visible when it is not.

The result is written to a `.cnav` file beside your scene and the path is stored on the component,
so it reloads next session without a rebake.

### In code

```cpp
#include <Cosmic.h>
#include "scene/SceneNav.h"      // SceneNav::BakeSync / SaveSidecar
#include "nav/NavWorld.h"        // NavWorld::FindPath / IsBuilt

using namespace Cosmic;

// The marker + recipe.
Entity nav = scene->CreateEntity("Nav Mesh");
auto& nm = nav.AddComponent<NavMeshComponent>();
nm.SourceMode    = NavSourceMode::FromChildren;
nm.CellSize      = 0.15f;
nm.AgentRadius   = 0.4f;
nm.AgentHeight   = 1.6f;
nm.AgentMaxClimb = 0.5f;

// Walkable geometry, parented under it. A collider with no RigidBody is an
// implicit static body — and the bake reads the collision view, so that is all
// it takes to be walkable.
Entity floor = scene->CreateEntity("Arena Floor");
floor.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
floor.AddComponent<BoxColliderComponent>().HalfExtents = { 9.0f, 0.25f, 7.0f };
scene->SetParent(floor, nav, /*keepWorldPose*/ false);

// Bake now (GL-free) and persist the sidecar.
if (SceneNav::BakeSync(*scene, (entt::entity)nav))
{
    nm.SidecarPath = "project://scenes/Main.cnav";
    SceneNav::SaveSidecar(nm, nm.SidecarPath);
}
```

Then add an agent and steer it:

```cpp
Entity critter = scene->CreateEntity("Critter");
critter.GetComponent<TransformComponent>().Position = { -5.0f, 0.5f, -3.0f };
auto& ac = critter.AddComponent<NavAgentComponent>();
ac.Radius = 0.4f; ac.Height = 1.6f; ac.MaxSpeed = 3.0f; ac.StoppingDistance = 0.5f;
```

```cpp
// In a script on that entity:
void OnStart() override { Nav().SetTarget({ 8.0f, 0.0f, 8.0f }); }
```

### Which headers you need

`Cosmic.h` does **not** include any `nav/` header directly — they arrive transitively through
`scripting/ScriptableEntity.h` (which includes `scene/SceneNav.h` and `nav/NavWorld.h` inside the 3D
fence for the `Nav()` proxy).

| You name | Comes from | Extra include needed |
| --- | --- | --- |
| `NavMeshComponent`, `NavAgentComponent`, `NavSourceMode` | `scene/Components3D.h` (via `Cosmic.h`) | none |
| `NavWorld`, `NavPath`, `NavRayHit`, `NavAgentParams`, `SceneNavRuntime` | `scripting/ScriptableEntity.h` (via `Cosmic.h`) | none in practice; include `nav/NavWorld.h` + `scene/SceneNav.h` explicitly if you would rather not rely on that |
| `SceneNav::BakeSync` / `BeginBake` / `SaveSidecar` / `GatherGeometry` | — | `#include "scene/SceneNav.h"` |

`Components3D.h` only forward-declares `NavWorld`, which is enough to hold it in a `Ref<>` but not
to call anything on it.

---

## What the bake actually reads

**The navmesh is baked from the scene's *collision view*, not from its meshes.** `SceneNav` walks
the registry and calls the same public `ScenePhysics::BuildColliderDesc` the physics session uses to
build bodies, then tessellates the resulting shapes into a world-space triangle soup for Recast.

That is a deliberate design choice, and it is the most useful thing to know about this system: **if
a surface has no collider, agents cannot walk on it** — no matter how solid it looks. And
conversely, an agent will never walk into a wall the physics engine agrees is there, because both
read the same source.

What contributes:

| Source | How it is tessellated |
| --- | --- |
| `BoxColliderComponent` | exact — 12 triangles, world-transformed |
| `MeshColliderComponent` (`Convex = false`) | exact — the triangle soup |
| `TerrainColliderComponent` | exact — two triangles per heightfield cell |
| Voxel volumes | exact — per-chunk collision meshes for every resident chunk |
| `SphereColliderComponent` | **its AABB** (a conservative box) |
| `CapsuleColliderComponent` | **its AABB** (`radius` × `halfHeight + radius` × `radius`) |
| `MeshColliderComponent` (`Convex = true`) | **the hull's AABB** |

The three approximations are a documented v1 simplification: curved and convex shapes are rarely the
walkable floor, and a conservative box is the safe error to make (an agent avoids slightly more
space than it strictly has to). If you need an accurate walkable surface, build it out of boxes,
mesh colliders or terrain.

**What is skipped:** entities that fail `Scene::IsActiveInHierarchy`, colliders with `Enabled`
unticked, and — when `SourceMode` is `FromChildren` — anything that is not the navmesh entity or one
of its descendants.

> **Trigger volumes are baked as solid geometry.** The gather does not check `IsTrigger`, so a big
> sensor slab across your level becomes a wall or a floor in the navmesh. Keep sensors off the
> navmesh entity's subtree (or out of the scene, with `WholeScene`), or accept that agents will
> route around them.

### Scope: `FromChildren` vs `WholeScene`

| `SourceMode` | Bakes |
| --- | --- |
| `FromChildren` (**default**) | the navmesh entity itself and every descendant |
| `WholeScene` | every collidable entity in the scene |

`FromChildren` is the one to reach for. `WholeScene` rasterizes everything — including a 1 km
terrain — which at a 0.3 m cell size is an enormous grid for no benefit. The playground uses
`FromChildren` precisely so the 256 m terrain around the arena is not in the bake.

`tests/test_nav_bake.cpp` pins the filter: a ground parented under the nav entity is navigable, an
identical ground 40 m away that is *not* parented under it produces no navmesh polygon at all.

---

## Tune the recipe

Every field is reflected, so it appears in the Inspector with a range and a tooltip, serializes with
the scene, and undoes for free. The four that matter most are grouped first:

| Field | Default | What it does |
| --- | --- | --- |
| **`AgentRadius`** | `0.6` m | the walkable area is **eroded** by this — the single biggest control over where agents can go |
| **`AgentHeight`** | `2.0` m | vertical clearance required; low ceilings become unwalkable |
| **`AgentMaxClimb`** | `0.9` m | max step-up height — the stairs/ledge knob |
| **`AgentMaxSlope`** | `45°` | steeper faces are not walkable |
| `CellSize` | `0.30` m | XZ rasterization voxel size. Smaller = more accurate and much slower (cost is quadratic) |
| `CellHeight` | `0.20` m | Y rasterization voxel size |
| `RegionMinSize` | `8` voxels | discard walkable islands smaller than `size²` |
| `RegionMergeSize` | `20` voxels | merge regions smaller than this into neighbours |
| `EdgeMaxLen` | `12` m | max contour edge length |
| `EdgeMaxError` | `1.3` voxels | contour simplification error |
| `DetailSampleDist` | `6.0` × `CellSize` | detail-mesh sample spacing; **below `0.9` disables the detail mesh** |
| `DetailSampleMaxError` | `1.0` × `CellHeight` | detail-mesh max error |
| `VertsPerPoly` | `6` | max vertices per navmesh polygon, clamped to `[3, 6]` |
| `TileSize` | `0.0` | **parked.** v1 always builds a single-tile "solo" mesh; a non-zero value logs a warning and is ignored |

Plus three authoring fields:

| Field | Default | What it does |
| --- | --- | --- |
| `SidecarPath` | `""` | the `.cnav` asset path; empty ⇒ derived beside the scene |
| `AutoGenerate` | `false` | rebake automatically when the recipe or source geometry changes |
| `AlwaysRenderHelper` | `false` | draw the nav overlay even when the entity is not selected |

**Match `AgentRadius` to your actual agent.** It is applied at *bake* time, not at steer time, so
a navmesh baked for a 0.6 m agent has 0.6 m already carved out of every wall — and a 0.4 m agent
walking it simply keeps more distance than it needs. Bake per agent size if the difference matters;
v1 runs one navmesh at a time (see [Agents and the crowd](#agents-and-the-crowd)).

**When a bake produces nothing.** `NavWorld::Build` fails cleanly — it logs
`NavWorld::Build failed: <why>`, fills the optional `outError`, and returns `false`, leaving the
component unbaked. The two causes you will actually hit are *"empty or malformed geometry"* (nothing
in scope had a collider) and *"degenerate grid"* (bad bounds or an absurd cell size). The editor
surfaces it as:

```
[Nav] Bake produced no walkable surface (check colliders / recipe).
```

Recast's own warnings and errors are forwarded to the Console prefixed `Recast:`.

---

## The signature gate

`SceneNav::Signature` is an FNV-1a hash over **the recipe fields plus every gathered vertex and
index**. `NavMeshComponent::BuiltSignature` records the value the current navmesh was built from,
and `0` is reserved for *"never built"*.

That single number is what makes rebaking cheap to decide: re-gather, hash, compare. If it matches,
nothing that affects the navmesh changed and the bake would be a no-op. `test_nav_bake.cpp` asserts
both directions — re-gathering an unchanged scene reproduces the same signature; adding a wall
changes it.

**`AutoGenerate`** turns that into a background loop. The editor polls in edit mode only, throttled
to **every 0.5 s** (the gather is O(scene), so it is not free), and starts a bake whenever the
signature has drifted. Leave it off while you are building a level and flip it on when the layout
settles — otherwise every collider nudge queues a rebake.

Note what the signature covers: **the recipe and the geometry**. It does not cover `SidecarPath`,
`AutoGenerate` or `AlwaysRenderHelper`, so toggling those never triggers a rebake.

---

## The `.cnav` sidecar

A baked navmesh is big binary data, so it does not go in the scene JSON. It rides a sidecar file —
the same rule `.cvox` follows for voxel volumes. The scene stores only the recipe and the path.

```cpp
// Resolve the path: the component's SidecarPath if set, else <scene>.cnav beside it.
const std::string path = SceneNav::SidecarPathFor(nm, "project://scenes/Main.cscene");
SceneNav::SaveSidecar(nm, path);        // false if nothing is baked, or path is empty
SceneNav::LoadSidecar(nm, path);        // false on missing/empty/incompatible
```

The format is a small Cosmic header (magic `CNAV`, version `1`) followed by the raw Detour tile
data. `NavWorld::Serialize()` is byte-stable for a given built mesh, which is what lets
`test_nav_bake.cpp` assert a bake → save → load round-trip is byte-identical.

**Both save and load resolve through `FileSystem::Resolve`**, so `project://` and `user://` paths
work. `SaveSidecar` creates the parent directory if it is missing.

**Loading is automatic and lazy.** `Scene::SyncNavMeshes` runs at the top of `Scene::OnRender3D`
*and* `Scene::BuildRenderDesc`, and loads the sidecar for any `NavMeshComponent` that has a
`SidecarPath` and no navmesh yet. A missing or stale sidecar is not an error — the component simply
stays unbaked. `SceneNavRuntime::BuildAgents` does the same lazy load when a play session starts, so
a packaged game never needs an explicit call.

**Version bumps invalidate old sidecars deliberately.** A `.cnav` whose magic or version does not
match logs *"bad magic/version (stale .cnav — rebake)"* and refuses to load, rather than feeding
Detour bytes it will misread.

**One sidecar per navmesh.** The derived path is `<scene-without-extension>.cnav`, which is fine for
the single-navmesh default. If a scene holds more than one `NavMeshComponent`, author an explicit
`SidecarPath` on each or they will overwrite one another.

---

## Bake without stalling the frame

`BakeSync` gathers, builds and installs on the calling thread — fine for tests, tools and small
bakes, but a visible hitch on a real level. The async pair splits it the way the terrain build does:

```cpp
// Start: gathers on the main thread (it reads the ECS), then hands Recast to a worker.
NavBakeJob job = SceneNav::BeginBake(scene, (entt::entity)navEntity);

// Later frames: poll and install.
if (SceneNav::FinishBake(scene, (entt::entity)navEntity, job))
    CS_INFO("navmesh installed");
```

- `BeginBake` sets `NavMeshComponent::Baking` so UI can show progress, and returns a handle whose
  `IsDone()` you poll. Recast is GL-free, so the build is safe on a `JobSystem` worker.
- **With no `JobSystem` pool the build runs inline** and the returned job is already done — which is
  why the headless tests can use the async path unchanged.
- `FinishBake` installs the result, updates `BuiltSignature`, clears `Baking` and consumes the job.
  It returns `false` while the job is still running, so it is safe to call every frame.
- **If the build failed, `FinishBake` still returns `true`** (the job is complete) but leaves the
  old navmesh in place. Check `nm.Nav && nm.Nav->IsBuilt()` to tell success from failure — which is
  exactly what the editor does before it writes the sidecar.

`StarforgeApp::TickNavMeshes` is the reference orchestration: it drains the Inspector's *Regenerate
now* request, runs the throttled `AutoGenerate` check, polls in-flight jobs, and on success derives
+ writes the sidecar and marks the scene dirty. It also drops a job whose entity disappeared (undo,
delete) rather than installing onto a dead handle.

---

## Agents and the crowd

An agent is a `NavAgentComponent` plus a `TransformComponent`. Like physics bodies, **agents exist
only while a play session runs** — the crowd is created at start and released at stop; the baked
navmesh is authored data and survives.

```cpp
// from the playground
Entity e = scene->CreateEntity("Critter");   // the name is also the Tag — NavCritter matches on it
e.GetComponent<TransformComponent>().Position = { cx - 5.0f, cy + 0.5f, cz - 3.0f };
e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Box).Size = { 0.6f, 0.9f, 0.6f };
e.AddComponent<MeshRendererComponent>().Color = { 0.85f, 0.35f, 0.30f, 1.0f };
auto& ac = e.AddComponent<NavAgentComponent>();
ac.Radius = 0.4f; ac.Height = 1.6f; ac.MaxSpeed = 3.0f; ac.StoppingDistance = 0.5f;
```

| Field | Default | What it does |
| --- | --- | --- |
| `Radius` | `0.4` m | agent footprint — drives crowd separation and obstacle avoidance |
| `Height` | `1.8` m | agent height |
| `MaxSpeed` | `3.5` m/s | steering speed cap |
| `MaxAccel` | `8.0` m/s² | acceleration cap |
| `StoppingDistance` | `0.4` m | arrival tolerance — reaching within this emits `nav.arrived` |
| `AutoRepath` | `true` | **currently has no effect** — see the pitfall below |

### The session, and where it sits in the tick

```
Scene::OnNavStart()      → bind the primary navmesh, init the crowd, add one agent per component
Scene::OnNavStep(dt)     → each fixed step: advance the crowd, write transforms back, emit arrivals
Scene::OnNavStop()       → release the crowd and agents (the navmesh is untouched)
```

**The crowd steps *after* physics**, and that ordering is part of the contract:

```
per fixed step:
  scripts' OnFixedUpdate          ← Nav().SetTarget(...) goes here
  Scene::OnPhysicsStep(dt)        ← rigid bodies + character controllers
  Scene::OnNavStep(dt)            ← the crowd  ◄── after physics
  Scene::DispatchPhysicsEvents()
```

So within one step an agent steers against the world *after* this step's physics has resolved, and a
target a script set in `OnFixedUpdate` is honoured in the same step. Both hosts do it this way —
`StarforgeApp::TickPlay` and `PlayerLayer::OnFixedUpdate`.

**A compat gate keeps it free.** `OnNavStart` returns immediately when the scene has no
`NavAgentComponent` *and* no `NavMeshComponent`, so a project that never uses navigation pays
nothing.

### What `BuildAgents` does, exactly

1. **Picks the primary navmesh:** the first `NavMeshComponent` (in registry order) with a built
   `Nav`, lazily loading its `.cnav` sidecar first. **v1 runs one active navmesh for the whole
   crowd** — a second navmesh in the scene is simply not used by agents. With no built navmesh at
   all, `m_Nav` stays null and every agent is inert (calls are silent no-ops, not errors).
2. **Sizes the crowd** to `max(0.6, largest agent Radius)` and 256 agents.
3. **Adds one agent per `NavAgentComponent`** that passes `IsActiveInHierarchy`, in entt view order
   — deliberately, because DetourCrowd assigns ids in add order and that makes runs reproducible.
   Each spawn position is **snapped to the nearest navmesh polygon**.

### What `Step` does

`UpdateCrowd(dt)`, then per agent: write the crowd position back into the `TransformComponent`, then
test arrival. **Only the position is written** — rotation and scale are untouched, so an agent's
mesh does not turn to face its heading. Use `Nav().Velocity()` to orient it yourself if you want
that. Parented agents get the world position decomposed into the parent's local frame.

The crowd gives you obstacle avoidance and separation for free: `test_nav_agents.cpp` sends two
agents through each other and asserts their centres never collapse closer than 0.45 m with radii
summing to 0.8.

**Agents and physics are independent.** A `NavAgentComponent` is not a physics body and does not
collide with one. Agents avoid *each other* through the crowd and avoid *geometry* through the
navmesh — nothing else. An agent will happily walk through a dynamic crate that was not in the bake.

---

## Steer an agent from a script

The `Nav()` proxy on `ScriptableEntity` is the per-entity surface. Every call is a safe no-op or an
empty result when no play session or navmesh is active.

```cpp
class Wanderer : public Cosmic::ScriptableEntity
{
public:
    float Radius = 12.0f;

protected:
    void OnStart() override { m_Rng = 20260726u; PickNewTarget(); }

    // Arrival arrives as a signal on the scene bus — catch it here.
    void OnSignal(const std::string& signal, Cosmic::Entity source) override
    {
        if (signal == "nav.arrived" && source == GetEntity())
            PickNewTarget();
    }

private:
    void PickNewTarget()
    {
        const glm::vec3 here = Nav().Position();
        if (auto p = Nav().RandomPointAround(here, Radius, m_Rng))
            Nav().SetTarget(*p);
    }

    uint32_t m_Rng = 1u;
};
```

| `Nav()` call | What it does |
| --- | --- |
| `SetTarget(worldPos)` | request this entity's agent steer to `worldPos` (snapped to the navmesh) |
| `Stop()` | clear the move request — the agent decelerates and holds |
| `HasAgent()` | is there a live crowd agent for this entity? |
| `HasArrived()` | has it reached its current target's `StoppingDistance`? (latched until the next `SetTarget`/`Stop`) |
| `Position()` · `Velocity()` | the crowd's authoritative position and velocity |
| `FindPath(a, b)` | a straightened `NavPath` — **no agent required** |
| `Raycast(a, b)` | does the straight segment `a→b` stay walkable? |
| `NearestPoint(p)` | nearest navmesh point within `(2, 4, 2)` half-extents, or `nullopt` |
| `RandomPointAround(center, radius, rngState)` | a deterministic random walkable point |
| `Runtime()` · `World()` | the `SceneNavRuntime*` / `NavWorld*`, for anything not wrapped |

### From a `SystemScript`

A `SystemScript` is scene-bound, not entity-bound — it has **no `GetEntity()` and none of the eight
proxies**. Reach the same surface through the scene:

```cpp
// from the shipped NavCritter template script
void OnFixedUpdateAll(std::span<Cosmic::Entity> critters, float) override
{
    Cosmic::SceneNavRuntime* nav = GetScene().GetNav();
    if (!nav || !nav->HasNavmesh())
        return;

    for (Cosmic::Entity critter : critters)
    {
        const entt::entity h = static_cast<entt::entity>(critter);
        if (!nav->HasAgent(h))
            continue;

        const glm::vec3 pos = nav->AgentPosition(h);
        if (chasing)
            nav->SetTarget(h, playerPos);      // re-issue each tick — the crowd repaths
        else if (!targeted)
            nav->SetTarget(h, waypoint);
    }
}
```

`SceneNavRuntime`'s per-agent methods (`SetTarget`, `Stop`, `HasAgent`, `HasArrived`,
`AgentPosition`, `AgentVelocity`) all take an `entt::entity` — they are the same calls the `Nav()`
proxy forwards to. This is the pattern for *"one script drives every critter"*.

**Re-issuing a target every tick is fine and is the idiom for chasing.** The crowd repaths from the
new request; `NavCritter` does exactly this and `test_nav_agents.cpp` mirrors it.

## The `nav.arrived` signal

When an agent gets within its `StoppingDistance` of its current target, `SceneNavRuntime::Step`
does four things, in this order:

1. latches `ArrivedLatched` (what `HasArrived()` reports),
2. clears the internal `HasTarget`,
3. calls `ResetAgentTarget` so the agent stops steering,
4. emits **`nav.arrived`** on the scene `EventBus`, with the agent entity as the source.

Catch it in a script's `OnSignal` (filter on both the name and the source, as above), or subscribe
directly on the bus — which is what a host or a test does:

```cpp
scene.Events().Connect("nav.arrived", [&](Cosmic::Entity src)
{
    CS_INFO("'{0}' arrived", src.GetComponent<Cosmic::TagComponent>().Tag);
});
```

**It fires once per target.** The latch is cleared only by the next `SetTarget` or `Stop`. Because
`OnSignal` is a catch-all for *every* signal on the bus (buttons, flow graphs, other scripts), always
filter by name — see [`flow-and-story.md`](flow-and-story.md).

## Navmesh queries without an agent

`FindPath`, `Raycast`, `NearestPoint` and `RandomPointAround` are `const` queries on the navmesh
itself. They need a *navmesh*, not an *agent*, so a `SystemScript` computing patrol routes or a
spawner validating spawn points can use them freely.

```cpp
const Cosmic::NavPath path = Nav().FindPath(from, to);
if (path.Reached)
    CS_INFO("{0} corners, {1} m", path.Corners.size(), path.Length);
else if (path.Partial)
    CS_INFO("unreachable — closest approach is {0}", path.Corners.back());
else
    CS_INFO("no navmesh polygon under one of the endpoints");
```

`NavPath` distinguishes three outcomes and never throws:

| Result | `Reached` | `Partial` | `Corners` |
| --- | --- | --- | --- |
| full path | `true` | `false` | start → goal |
| goal unreachable | `false` | `true` | start → the frontier of what *is* reachable |
| endpoint off-mesh | `false` | `false` | empty |

- **Endpoints are snapped** to the nearest polygon within `(2, 4, 2)` half-extents. Query points a
  little off the surface are fine; a point 10 m in the air is a clean miss.
- **`Length` is the summed corner-to-corner distance** of the straightened path, in metres.
- **Paths are capped at 512 polygons and 512 straight-path points.** Very long routes across a large
  mesh will be truncated rather than failing.
- **`RandomPointAround` takes a caller-owned `uint32_t&` xorshift seed** and advances it. Seed it
  once per run and the draw is reproducible — deliberately, so an AI that wanders randomly is still
  bit-reproducible for a replay or a test.

---

## See the navmesh

The walkable surface draws as a translucent wireframe over the viewport: **bright teal** when the
navmesh entity is selected, dim when it is not. It is off for unselected entities unless you tick
`AlwaysRenderHelper` on the component. The **Nav-mesh overlay** chip in the viewport strip (waypoints
icon, on by default) is the master switch above both. The overlay reads
`NavWorld::GetDebugTriangles`, so it shows
the *baked detail mesh* — what agents actually walk on — not your source geometry. If the overlay is
missing where you expected floor, that gap is real.

`.cnav` files get a Content Browser row (waypoints icon, "NavMesh") but **no open action** — there
is no navmesh viewer, and the file is meaningless outside its scene.

---

## Common patterns

**Bake at author time, ship the sidecar.** For a sample or a fixed level, bake in code when the
scene is generated and write the sidecar next to it. The scene then plays and packages with no
manual Regenerate:

```cpp
if (SceneNav::BakeSync(*scene, (entt::entity)nav))
{
    auto& baked = nav.GetComponent<NavMeshComponent>();
    baked.SidecarPath = "project://scenes/Main.cnav";
    SceneNav::SaveSidecar(baked, baked.SidecarPath);
}
```

**Patrol between waypoints.** Keep a per-agent index, issue the next target when the current one is
within tolerance, and wrap. `NavCritter` uses a four-corner square around each critter's spawn and
`& 3` to cycle. Prefer this planar distance test over `HasArrived()` when you want a looser
tolerance than `StoppingDistance`.

**Chase, then fall back to patrol.** Test planar (XZ) distance to the player each tick; while
chasing, re-issue `SetTarget(playerPos)` every tick and invalidate the patrol target so the loop
resumes cleanly when the chase ends. That is the whole of `NavCritter`'s AI, and it holds up.

**Validate a target before you commit to it.** `SetTarget` is silent when the destination has no
navmesh polygon. If a target comes from a click, a spawn table or an unreliable source, snap it
first:

```cpp
if (auto snapped = Nav().NearestPoint(clickWorldPos))
    Nav().SetTarget(*snapped);
else
    CS_WARN("that spot isn't on the navmesh");
```

**Face the direction of travel.** The crowd writes position only (needs `<cmath>`):

```cpp
void OnUpdate(float) override
{
    const glm::vec3 v = Nav().Velocity();
    if (glm::length(glm::vec2(v.x, v.z)) > 0.05f)
        GetComponent<Cosmic::TransformComponent>().Rotation.y =
            glm::degrees(std::atan2(-v.x, -v.z));
}
```

**Deterministic AI.** The crowd is deterministic, agent ids are assigned in a stable order, and
`RandomPointAround` takes an explicit seed — so two identical runs produce bit-identical agent
traces. `test_nav_agents.cpp` asserts exactly that over 500 steps. Keep your own AI state seeded and
you keep the property.

---

## Pitfalls

**"The bake produced nothing."** Nothing in scope had a collider. Meshes are not baked — *colliders*
are. Check the Console for `[Nav] Bake produced no walkable surface`.

**"My floor is visible but agents can't walk on it."** Same cause: it has a `MeshRendererComponent`
and no collider. Add a `BoxCollider` (no `RigidBody` needed).

**"The bake takes forever / eats memory."** `SourceMode` is probably `WholeScene` with a terrain in
the scene, or `CellSize` is very small. Cost is quadratic in `1/CellSize`. Parent your level under
the navmesh entity and use `FromChildren`.

**"Agents won't go somewhere that clearly has floor."** `AgentRadius` erodes the walkable area at
bake time, and `AgentHeight` needs clearance. Narrow ledges and low tunnels vanish first. The
overlay shows you the truth — if it is not teal, agents cannot go there.

**"Agents route around an invisible obstacle."** A trigger volume, or a sphere/capsule collider
whose AABB is much bigger than the shape. Both are baked as solid.

**"An agent stepped up a ledge it should not have."** `AgentMaxClimb` (bake-time) is separate from
the character controller's `StepHeight` (runtime). If a character cannot climb what its agents can,
those two disagree.

**"`SetTarget` did nothing and `nav.arrived` never came."** The target had no navmesh polygon within
`(2, 4, 2)` of it, so the request was dropped — silently, and *without* clearing the agent's
previous target. The agent keeps steering to wherever it was already going. Snap with
`NearestPoint` first.

**"`AutoRepath` doesn't do anything."** It doesn't. The field is reflected, tooltipped and
serialized, but nothing reads it — the crowd's update flags are hard-coded (they already include
anticipate-turns, visibility and topology optimisation, obstacle avoidance and separation, which is
what you want anyway). Do not change it expecting an effect.

**"My second navmesh is ignored."** v1 binds the **first** built `NavMeshComponent` to the crowd.
One navmesh per scene, in practice.

**"Agents don't collide with the player / crates."** They don't. Agents avoid each other via the
crowd and geometry via the navmesh; they are not physics bodies. Chase the player by feeding its
position to `SetTarget`, as `NavCritter` does.

**"The agent's mesh never turns."** The crowd writes position only. Derive the facing from
`Nav().Velocity()` yourself.

**"I moved my level and the navmesh stayed put."** The navmesh is baked geometry. Moving the source
changes the signature, so `AutoGenerate` will pick it up — otherwise hit **Regenerate now**.

**"The `.cnav` won't load after an engine update."** A version bump invalidates old sidecars on
purpose (`bad magic/version (stale .cnav — rebake)`). Rebake.

**"Two navmeshes in one scene overwrite each other's sidecar."** The derived path is
`<scene>.cnav`. Give each an explicit `SidecarPath`.

**"`FinishBake` returned true but nothing changed."** `true` means the *job* finished, not that the
build succeeded. Check `nm.Nav && nm.Nav->IsBuilt()`.

**"`TileSize` has no effect."** The tiled build is parked. v1 always builds a single-tile solo mesh;
a non-zero value logs a warning and is ignored.

**"None of the nav names compile."** You are in a `COSMIC_2D_ONLY` tree. Navigation is 3D only.

---

## See also

**Guide**

- [`physics.md`](physics.md) — the collision view the bake reads: colliders, the implicit-static
  rule, and the `Enabled`/`Active` gates that decide what contributes.
- [`scripting.md`](scripting.md) — `ScriptableEntity` vs `SystemScript`, all eight proxies, and
  `OnSignal`.
- [`flow-and-story.md`](flow-and-story.md) — the `EventBus` that carries `nav.arrived`.
- [`entities-and-components.md`](entities-and-components.md) — `NavMeshComponent` and
  `NavAgentComponent` in the full component catalogue.
- [`voxels.md`](voxels.md) — voxel chunks are baked into the navmesh too.
- [`world-systems.md`](world-systems.md) — terrain, whose heightfield collider is exact in the bake.
- [`time-and-ticks.md`](time-and-ticks.md) — the fixed pass the crowd steps on.

**Reference / systems** — neither tier covers navigation yet (see the header block). The closest
neighbours are [`../reference/physics.md`](../reference/physics.md) for
`ScenePhysics::BuildColliderDesc`, the enumeration the bake is built on, and
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) for why the whole tier is 3D
only.

**Tests as executable documentation** — [`tests/test_nav_world.cpp`](../../tests/test_nav_world.cpp)
(bake, queries, serialization round-trip), [`test_nav_bake.cpp`](../../tests/test_nav_bake.cpp)
(the scene bake, the sidecar, the signature gate, `FromChildren` filtering),
[`test_nav_agents.cpp`](../../tests/test_nav_agents.cpp) (the crowd, `nav.arrived`, avoidance, the
`NavCritter` logic, determinism).

---
*Changelog:*
*2026-07-26 — created (D57). Written from source against `Cosmic/src/nav/`, `scene/SceneNav.{h,cpp}`,
`scene/Components3D.h` and the three `tests/test_nav_*.cpp` files.*
