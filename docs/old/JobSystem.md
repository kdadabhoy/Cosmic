# Cosmic Engine — Parallel Systems & Job Pipeline

This document explains how to write parallel systems using the engine's multithreading infrastructure, what the engine manages for you automatically, and when (if ever) you need to reach for lower-level primitives.

---

## Table of Contents

1. [The 30-Second Version](#1-the-30-second-version)
2. [How the Pipeline Works](#2-how-the-pipeline-works)
3. [Writing a Parallel System](#3-writing-a-parallel-system)
4. [ReadWriteQuery Reference](#4-readwritequery-reference)
5. [ReadOnlyQuery Reference](#5-readonlyquery-reference)
6. [Cross-Component Sync in OnMerge](#6-cross-component-sync-in-onmerge)
7. [Cross-Entity Patterns (Collision, Flocking)](#7-cross-entity-patterns-collision-flocking)
8. [Standalone Parallel Work (Layers & Helpers)](#8-standalone-parallel-work-layers--helpers)
9. [Template Project — BallPhysicsSystem](#9-template-project--ballphysicssystem)
10. [Rules & Constraints](#10-rules--constraints)
11. [Lower-Level Primitives Reference](#11-lower-level-primitives-reference)

---

## 1. The 30-Second Version

```cpp
class MyPhysicsSystem : public Cosmic::ParallelSystem
{
    // 1. Declare a query. Pass `this` — that's all the setup required.
    Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };

public:
    // 2. Submit parallel work. The engine stages m_Bodies before this runs
    //    and commits results back to the registry after OnMerge.
    void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
    {
        m_Bodies.ForEachAsync([dt](PhysicsBody& body)
        {
            body.Velocity.y  += -9.8f * dt;
            body.Position    += body.Velocity * dt;
        });
    }
};

// Register once. The engine handles everything else.
scene->AddSystem<MyPhysicsSystem>();
```

That's a fully parallel, data-safe physics system. No buffer management, no swap calls, no registry iteration, no WaitIdle.

---

## 2. How the Pipeline Works

Every frame the Scene runs all parallel systems in four ordered passes. A Scene with no parallel systems never touches the job system at all.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ PASS A — Sequential systems (main thread)                                   │
│   System::OnUpdate / OnFixedUpdate for each registered System               │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS B — Stage + Prepare (main thread)                                      │
│   For each ParallelSystem:                                                  │
│     [engine] StageQueries  → snapshot all declared queries from registry    │
│     [user]   OnPrepare     → optional per-frame setup (rarely needed)       │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS C — Execute (main thread submits; workers execute concurrently)        │
│   For each ParallelSystem:                                                  │
│     [user] OnParallelExecute → call ForEachAsync / DispatchAsync           │
│                                                                             │
│   JobSystem::WaitIdle()  ←── single barrier after ALL systems submit       │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS D — Merge + Commit (main thread)                                       │
│   For each ParallelSystem:                                                  │
│     [user]   OnMerge       → optional post-processing / cross-component sync│
│     [engine] CommitQueries → write ReadWriteQuery results back to registry  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Why one WaitIdle for all systems?

If each system waited for its own jobs before the next system submitted, systems would execute one at a time. With a single barrier after all submissions, jobs from System A and System B are in the worker queue simultaneously and execute in parallel — maximum utilisation.

```
Staggered (wrong):  [A submit][A wait][B submit][B wait]   → 2× M ms
Batched  (correct): [A submit][B submit][WaitIdle]          → M ms
```

---

## 3. Writing a Parallel System

### Inherit and declare queries

```cpp
#include <Cosmic.h>

class BallPhysicsSystem : public Cosmic::ParallelSystem
{
    // Declare as a member variable. Pass `this` to register automatically.
    Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };

public:
    float Gravity = -9.8f;
    // ...
};
```

The query constructor calls `RegisterQuery(this)` on the owning system. The Scene discovers registered queries via `StageQueries` / `CommitQueries` — no extra setup needed.

### Implement OnParallelExecute

```cpp
void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
{
    // Capture constants by value — required for cross-thread safety.
    const float g = Gravity;

    m_Bodies.ForEachAsync([g, dt](PhysicsBody& body)
    {
        body.Velocity.y += g * dt;
        body.Position   += body.Velocity * dt;
    });

    // Do NOT call WaitIdle here. Do NOT use ParallelFor (synchronous).
    // The Scene issues a single WaitIdle after all systems submit.
}
```

### Register with the Scene

```cpp
scene->AddSystem<BallPhysicsSystem>();
```

`AddSystem` detects that `BallPhysicsSystem` derives from `ParallelSystem` and caches the pointer. No per-frame `dynamic_cast` occurs.

### Variable vs fixed timestep

| Hook | Timestep | When to use |
|------|----------|-------------|
| `OnParallelExecute` | Variable (per-frame) | Transform animations, camera work |
| `OnFixedParallelExecute` | Fixed (60 Hz) | Physics, deterministic simulation |

Both use the same query objects — queries stage fresh data on every call regardless of timestep type.

---

## 4. ReadWriteQuery Reference

Declare as a member, pass `this`:
```cpp
Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };
```

### What the engine does automatically

| Engine action | When |
|---------------|------|
| Snapshot all `PhysicsBody` components from registry → internal buffer | Before `OnPrepare` |
| Make buffer available via `Data()`, `operator[]`, iteration methods | Throughout Prepare/Execute/Merge |
| Write buffer results back to registry by entity ID | After `OnMerge` |

### Async iteration (use in OnParallelExecute)

```cpp
// Per-element — worker receives mutable ref, modifies in place
m_Bodies.ForEachAsync([dt](PhysicsBody& body) { ... });

// Range-based — worker receives (T* begin, T* end) sub-array
m_Bodies.DispatchAsync([](PhysicsBody* begin, PhysicsBody* end) { ... });

// Optional minimum chunk size (default 64)
m_Bodies.ForEachAsync([](PhysicsBody& body) { ... }, /*minChunkSize=*/32);
```

### Sync iteration (use in OnPrepare or OnMerge)

```cpp
// Per-element, main thread
m_Bodies.ForEach([](PhysicsBody& body) { ... });

// Per-element with entity handle — for cross-component sync in OnMerge
m_Bodies.ForEachWithEntity([&scene](PhysicsBody& body, entt::entity e)
{
    scene.GetRegistry().get<TransformComponent>(e).Position = { body.Position.x, body.Position.y, 0.f };
});
```

### Direct data access

```cpp
T*           m_Bodies.Data()           // raw pointer to staged array
size_t       m_Bodies.Count()          // element count
bool         m_Bodies.IsEmpty()        // true if no entities have this component
T&           m_Bodies[i]               // indexed element access
entt::entity m_Bodies.EntityAt(i)      // entity handle for element i
```

---

## 5. ReadOnlyQuery Reference

Provides a stable, immutable snapshot — no writeback, no races. Use it when workers need to read the whole dataset safely while other workers are writing to a separate `ReadWriteQuery`.

```cpp
Cosmic::ReadOnlyQuery<PhysicsBody> m_ReadBodies{ this };
```

### Async iteration

```cpp
// Per-element read-only
m_ReadBodies.ForEachAsync([](const PhysicsBody& body) { ... });

// Range-based read-only
m_ReadBodies.DispatchAsync([](const PhysicsBody* begin, const PhysicsBody* end) { ... });
```

### Direct access

```cpp
const T*     m_ReadBodies.Data()
size_t       m_ReadBodies.Count()
const T&     m_ReadBodies[i]
entt::entity m_ReadBodies.EntityAt(i)
```

---

## 6. Cross-Component Sync in OnMerge

`ReadWriteQuery` handles writeback for its own component type automatically. When you also need to update a *second* component with the computed results (the classic "sync physics position to render transform" pattern), do it in `OnMerge`:

```cpp
void OnFixedMerge(Cosmic::Scene& scene, float dt) override
{
    // At this point: m_Bodies holds this tick's computed results.
    // The engine will auto-commit PhysicsBody AFTER this function returns.
    // Here we propagate position to TransformComponent for the renderer.

    auto& reg = scene.GetRegistry();

    m_Bodies.ForEachWithEntity([&reg](PhysicsBody& body, entt::entity e)
    {
        if (!reg.valid(e)) return;
        auto& t = reg.get<Cosmic::TransformComponent>(e);
        t.Position.x = body.Position.x;
        t.Position.y = body.Position.y;
    });
}
```

**Timing note:** `CommitQueries` runs *after* `OnMerge`. When `OnMerge` executes, `m_Bodies[i]` has the final computed result but `registry.get<PhysicsBody>(e)` still has last frame's value. Use `m_Bodies[i]` (or `ForEachWithEntity`) for reading results in `OnMerge`; the registry is updated automatically once `OnMerge` returns.

---

## 7. Cross-Entity Patterns (Collision, Flocking)

`ForEachAsync` gives each element a mutable reference. Workers operate on disjoint index ranges — no two workers touch the same element. This is safe for independent per-entity transforms (gravity, drag, bounds).

For algorithms where entity A reads entity B's value (collision response, boid steering), reading from the same buffer another worker is writing is a data race. Use a `ReadOnlyQuery` as the stable input snapshot and a `ReadWriteQuery` as the output:

```cpp
class CollisionSystem : public Cosmic::ParallelSystem
{
    Cosmic::ReadOnlyQuery<PhysicsBody>  m_Snapshot{ this }; // stable frame-start state
    Cosmic::ReadWriteQuery<PhysicsBody> m_Output{ this };   // computed new state

    void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
    {
        const PhysicsBody* snap    = m_Snapshot.Data();
        const size_t       count   = m_Snapshot.Count();

        m_Output.DispatchAsync([snap, count, dt](PhysicsBody* begin, PhysicsBody* end)
        {
            for (PhysicsBody* b = begin; b != end; ++b)
            {
                size_t i = static_cast<size_t>(b - /* base ptr */ ???);
                // Safe: reads snap[j] (immutable) for any j, writes b (disjoint range)
                for (size_t j = 0; j < count; ++j)
                    ResolveCollision(*b, snap[j], dt);
            }
        });
    }
};
```

> Both queries stage independently from the same registry state, so they begin each frame as identical snapshots. Workers read from `m_Snapshot` (constant), write to `m_Output` (disjoint ranges). No races.

---

## 8. Standalone Parallel Work (Layers & Helpers)

If you need parallel work outside a `ParallelSystem` — from a Layer's `OnFixedUpdate`, a one-off helper, etc. — use the **synchronous** `ParallelFor` family. These submit jobs *and* call `WaitIdle` before returning, so results are available immediately.

```cpp
// Index-based
Cosmic::ParallelFor(count, [src, dst, dt](size_t begin, size_t end)
{
    for (size_t i = begin; i < end; ++i)
        Integrate(src[i], dst[i], dt);
});
// Execution is complete here.

// Element-based (typed pointer)
Cosmic::ParallelForEach(bodies, count, [dt](PhysicsBody* begin, PhysicsBody* end)
{
    for (auto* b = begin; b != end; ++b)
        b->Velocity.y += -9.8f * dt;
});

// Element + global index
Cosmic::ParallelForEachIndexed(bodies, count, [](PhysicsBody& body, size_t i)
{
    body.Mass = static_cast<float>(i) * 0.1f;
});
```

The template project's `TemplateRenderBenchmarkLayer` uses this pattern — it drives a full Prepare/Execute/Merge cycle manually from a Layer without registering a `ParallelSystem`. This is correct for self-contained benchmark layers that manage their own timing.

---

## 9. Template Project — BallPhysicsSystem

`BallPhysicsSystem` in `templates/ExampleProject/src/BallPhysicsSystem.h` is the canonical example. Here is the complete annotated version:

```cpp
class BallPhysicsSystem : public Cosmic::ParallelSystem
{
    // ── Query declaration ─────────────────────────────────────────────────────
    // Engine stages PhysicsBody from the registry before OnFixedPrepare.
    // Engine commits results back to the registry after OnFixedMerge.
    Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };

public:
    float Gravity = -9.8f;
    float Damping = 0.85f;
    float BoundsX = 6.0f;
    float BoundsY = 4.0f;

    // ── PASS B (optional) ─────────────────────────────────────────────────────
    // m_Bodies is already staged when this runs. Override only for custom setup.
    void OnFixedPrepare(Cosmic::Scene& scene, float dt) override
    {
        m_PrepareStart = Clock::now(); // telemetry only
    }

    // ── PASS C ───────────────────────────────────────────────────────────────
    void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
    {
        if (m_Bodies.IsEmpty()) return;

        const float g = Gravity, d = Damping, bx = BoundsX, by = BoundsY;

        m_Bodies.ForEachAsync([g, d, bx, by, dt](PhysicsBody& body)
        {
            body.Velocity.y += g * dt;

            float drag = 1.f - (d * body.LinearDrag * dt);
            body.Velocity  *= glm::clamp(drag, 0.f, 1.f);
            body.Position  += body.Velocity * dt;

            const float r = glm::clamp(body.Restitution - d, 0.f, 1.f);

            if (body.Position.x + body.Radius >  bx) { body.Position.x =  bx - body.Radius; body.Velocity.x *= -r; }
            if (body.Position.x - body.Radius < -bx) { body.Position.x = -bx + body.Radius; body.Velocity.x *= -r; }
            if (body.Position.y - body.Radius < -by) { body.Position.y = -by + body.Radius; body.Velocity.y *= -r; }
            if (body.Position.y + body.Radius >  by) { body.Position.y =  by - body.Radius; body.Velocity.y *= -r; }
        }, 32);
    }

    // ── PASS D ───────────────────────────────────────────────────────────────
    // Sync computed physics positions to TransformComponent for the renderer.
    // The engine commits PhysicsBody AFTER this returns — use m_Bodies[i], not
    // the registry, to read results here.
    void OnFixedMerge(Cosmic::Scene& scene, float dt) override
    {
        auto& reg = scene.GetRegistry();
        m_Bodies.ForEachWithEntity([&reg](PhysicsBody& body, entt::entity e)
        {
            if (!reg.valid(e)) return;
            auto& t = reg.get<Cosmic::TransformComponent>(e);
            t.Position.x = body.Position.x;
            t.Position.y = body.Position.y;
        });
    }
};
```

Before this system was ~220 lines including manual `DoubleBuffer` management, two `Swap()` calls, an explicit entity-slot vector, EnTT group/view iteration in both Prepare and Merge, and raw `GetReadBuffer()`/`GetWriteBuffer()` pointer plumbing. The same system is now ~80 lines with none of that complexity.

---

## 10. Rules & Constraints

### Lambda captures in ForEachAsync

Workers execute after `OnParallelExecute` returns. By that point any local variable that was captured by reference is destroyed.

```cpp
// ✅ Correct — all captures by value
const float g = Gravity;
m_Bodies.ForEachAsync([g, dt](PhysicsBody& body) { ... });

// ❌ Wrong — reference to local variable dangling when workers run
float& gRef = Gravity;
m_Bodies.ForEachAsync([&gRef, dt](PhysicsBody& body) { gRef; }); // UB
```

**Safe to capture by value:** scalars, raw pointers to stable data (e.g. `m_Bodies.Data()` which lives for the frame), `glm::vec2`, `glm::vec4`.

**Never capture by reference in async lambdas.**

### Worker threads must NOT

- Modify the EnTT registry (add/remove components, create/destroy entities)
- Call `JobSystem::WaitIdle()` — deadlock with a fixed-size pool
- Submit new jobs from inside a running job
- Write to an element index owned by another worker's chunk

### Worker threads MAY

- Read from `ReadOnlyQuery::Data()` (concurrent reads, no writes)
- Write to any element in their assigned chunk of a `ReadWriteQuery`
- Call pure functions, allocate thread-local scratch memory
- Read simulation constants captured by value in the lambda

### Use the correct ParallelFor variant

| Context | Use |
|---------|-----|
| Inside `OnParallelExecute` / `OnFixedParallelExecute` | `ForEachAsync`, `DispatchAsync`, `ParallelForAsync` |
| Anywhere else (Layers, helpers, standalone) | `ForEach`, `ParallelFor`, `ParallelForEach` |

The `*Async` variants submit jobs without waiting. The synchronous variants submit and block until complete. Using a synchronous variant inside `OnParallelExecute` stalls after each system and defeats cross-system parallelism.

---

## 11. Lower-Level Primitives Reference

These are available when you need manual control — benchmarks, custom scheduling, or work that doesn't fit the ParallelSystem pattern.

### `JobSystem`

```cpp
JobSystem& js = JobSystem::Get();
js.Submit([]{ DoSomeWork(); });   // enqueue any void() callable
js.WaitIdle();                    // block until queue empty + all jobs done
js.GetWorkerCount();              // logical cores − 1
```

### `ParallelFor` (synchronous)

```cpp
Cosmic::ParallelFor(count, [](size_t begin, size_t end) { ... });
Cosmic::ParallelForEach(ptr, count, [](T* begin, T* end) { ... });
Cosmic::ParallelForEachIndexed(ptr, count, [](T& item, size_t i) { ... });
```

### `DoubleBuffer<T>`

Double-buffering container for raw simulation state. Used internally by `ReadWriteQuery` — you only need this directly for custom low-level systems.

```cpp
Cosmic::DoubleBuffer<PhysicsBody> buf;
buf.Resize(count);
buf.WriteAt(i) = ...;      // populate write buffer
buf.Swap();                // write → read
const T* r = buf.GetReadBuffer();  // workers read
T*       w = buf.GetWriteBuffer(); // workers write
buf.Swap();                // commit results
```

### `ComponentArray<T>` / `FlatComponentArray<T>`

Zero-copy or flattened views into EnTT storage. Useful for `ParallelFor` calls that need raw pointers without staging a full copy.

```cpp
// Zero-copy (first EnTT page only — safe up to ~4000–16000 entities)
auto view = Cosmic::ComponentArray<TransformComponent>::From(scene.GetRegistry());
Cosmic::ParallelForEach(view.Data(), view.Count(), ...);

// Fully flattened copy (any entity count, costs a memcpy)
auto flat = Cosmic::FlatComponentArray<PhysicsBody>::From(scene.GetRegistry());
flat.WriteBack(scene.GetRegistry()); // after modification
```
