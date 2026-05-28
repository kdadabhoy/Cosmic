# Cosmic Engine — Job System & Parallel Pipeline

This document explains the multithreading infrastructure provided by the engine, what the engine manages automatically, and exactly how to write parallel systems as a client.

---

## Table of Contents

1. [What the Engine Provides](#1-what-the-engine-provides)
2. [Core Primitives](#2-core-primitives)
3. [The Parallel Pipeline](#3-the-parallel-pipeline)
4. [Writing a Parallel System](#4-writing-a-parallel-system)
5. [Template Project Walkthrough](#5-template-project-walkthrough)
6. [Standalone Parallel Work (Outside a System)](#6-standalone-parallel-work-outside-a-system)
7. [DoubleBuffer Reference](#7-doublebuffer-reference)
8. [ComponentArray Reference](#8-componentarray-reference)
9. [Rules & Constraints](#9-rules--constraints)
10. [When NOT to Use the Job System](#10-when-not-to-use-the-job-system)

---

## 1. What the Engine Provides

| Header | What it gives you |
|--------|-------------------|
| `jobs/JobSystem.h` | Persistent thread pool. Submit any `void()` callable. |
| `jobs/ParallelFor.h` | Data-parallel iteration over index ranges and typed arrays. |
| `jobs/ParallelSystem.h` | Base class. Override three hooks; the Scene drives the pipeline. |
| `jobs/DoubleBuffer.h` | Thread-safe read/write buffering for component data. |
| `jobs/ComponentArray.h` | Zero-copy view (or flattened copy) of EnTT component storage. |

All five headers are included by `Cosmic.h`.

The `JobSystem` creates `(logical_core_count − 1)` persistent worker threads at `Application::Initialize` and joins them at `Application::Shutdown`. Thread creation happens **once** — there is no per-frame spawn overhead.

---

## 2. Core Primitives

### `JobSystem`

```cpp
JobSystem& js = JobSystem::Get();

// Submit any zero-argument callable
js.Submit([]{ DoSomeWork(); });

// Block until every submitted job has completed
js.WaitIdle();

// Hardware info
uint32_t cores   = js.GetCoreCount();   // logical CPUs detected
uint32_t workers = js.GetWorkerCount(); // cores - 1
```

**Thread safety contract:**
- `Submit()` — thread-safe, callable from any thread at any time.
- `WaitIdle()` — blocks caller until the queue is empty and all executing jobs have returned.
- `Shutdown()` — main thread only, drains queue before joining threads.

### `ParallelFor` / `ParallelForAsync`

```
Synchronous variants   — submit jobs + WaitIdle before returning
  ParallelFor(count, func)
  ParallelForEach(data, count, func)
  ParallelForEachIndexed(data, count, func)

Async variants         — submit jobs, NO WaitIdle
  ParallelForAsync(count, func)
  ParallelForEachAsync(data, count, func)
  ParallelForEachIndexedAsync(data, count, func)
```

**Which to use:**

| Context | Use |
|---------|-----|
| Standalone parallel work in a Layer or helper function | Synchronous (`ParallelFor`) |
| Inside `ParallelSystem::OnParallelExecute` | **Async** (`ParallelForAsync`) |

The async variants let the Scene issue a **single** `WaitIdle` barrier after all systems have submitted, so jobs from different systems run concurrently. The synchronous variants stall immediately after submission — using them inside `OnParallelExecute` would serialise your systems.

**Function signatures:**

```cpp
// Index-based: func(size_t begin, size_t end) — sub-range [begin, end)
ParallelFor(size_t totalCount, Func func, size_t minChunkSize = 64);
ParallelForAsync(size_t totalCount, Func func, size_t minChunkSize = 64);

// Pointer-based: func(T* begin, T* end) — contiguous sub-array
ParallelForEach(T* data, size_t count, Func func, size_t minChunkSize = 64);
ParallelForEachAsync(T* data, size_t count, Func func, size_t minChunkSize = 64);

// Indexed: func(T& element, size_t globalIndex) — one call per element
ParallelForEachIndexed(T* data, size_t count, Func func, size_t minChunkSize = 64);
ParallelForEachIndexedAsync(T* data, size_t count, Func func, size_t minChunkSize = 64);
```

`minChunkSize` controls the minimum number of elements dispatched to a single worker. Below that threshold the work runs serially on the calling thread. Default is 64 — tune per system based on profiling.

---

## 3. The Parallel Pipeline

Every frame the Scene runs systems in four ordered passes. The job system is only touched during passes B–D, and **only if at least one `ParallelSystem` is registered**. A Scene with no parallel systems never calls `WaitIdle` and never involves the job system at all.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ PASS A — Sequential (main thread)                                           │
│   for each System* s  →  s->OnUpdate(scene, dt)                            │
│   for each System* s  →  s->OnFixedUpdate(scene, dt)     [fixed step only] │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS B — Prepare (main thread, single-threaded)                             │
│   for each ParallelSystem* ps  →  ps->OnPrepare(scene, dt)                 │
│                                                                             │
│   Purpose: snapshot EnTT state into read buffers, resize output arrays,    │
│   compute per-frame constants. No jobs submitted here.                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS C — Execute (main thread submits; worker threads execute)              │
│   for each ParallelSystem* ps  →  ps->OnParallelExecute(scene, dt)         │
│                                                                             │
│   All systems submit their async jobs before any synchronisation.           │
│   Workers from different systems run concurrently — maximum utilisation.   │
│                                                                             │
│   JobSystem::WaitIdle()  ←── single barrier; main thread blocks here       │
├─────────────────────────────────────────────────────────────────────────────┤
│ PASS D — Merge (main thread, single-threaded)                               │
│   for each ParallelSystem* ps  →  ps->OnMerge(scene, dt)                   │
│                                                                             │
│   All workers are idle. Safe to swap DoubleBuffers, write back to registry, │
│   create/destroy entities, fire events.                                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

The fixed-timestep path (`OnFixedUpdate`) runs the identical B/C/D structure using `OnFixedPrepare`, `OnFixedParallelExecute`, `OnFixedMerge`.

### Why a single WaitIdle matters

With `N` parallel systems each doing `M` ms of work:

```
Sequential (wrong — synchronous ParallelFor in Execute):
  System A: submit → wait → merge   [M ms]
  System B: submit → wait → merge   [M ms]
  Total: N×M ms

Batched (correct — async submit + single WaitIdle):
  All systems submit               [~0 ms]
  Workers run A + B concurrently  [M ms, not N×M]
  All merge                       [~0 ms]
  Total: M ms
```

---

## 4. Writing a Parallel System

### Step 1 — Inherit from `ParallelSystem`

```cpp
#include "jobs/ParallelSystem.h"
#include "jobs/DoubleBuffer.h"
#include "jobs/ParallelFor.h"

class BallPhysicsSystem : public Cosmic::ParallelSystem
{
public:
    // Called once before the frame loop if you need setup
    void OnAttach(Cosmic::Scene& scene) { /* optional */ }

    void OnPrepare(Cosmic::Scene& scene, float dt) override;
    void OnParallelExecute(Cosmic::Scene& scene, float dt) override;
    void OnMerge(Cosmic::Scene& scene, float dt) override;

private:
    Cosmic::DoubleBuffer<PhysicsBody> m_Buffer;
    std::vector<entt::entity>         m_Entities;
};
```

### Step 2 — Prepare: snapshot state into your buffers

```cpp
void BallPhysicsSystem::OnPrepare(Cosmic::Scene& scene, float dt)
{
    auto& reg = scene.GetRegistry();
    auto view = reg.view<PhysicsBody>();
    const size_t count = view.size_hint();

    // Resize once if entity count changed
    if (m_Buffer.Count() != count)
    {
        m_Buffer.Resize(count);
        m_Entities.resize(count);
    }

    // Copy current state into the write buffer, then swap it to become the read buffer
    size_t i = 0;
    for (auto entity : view)
    {
        m_Buffer.WriteAt(i) = reg.get<PhysicsBody>(entity);
        m_Entities[i]       = entity;
        ++i;
    }
    m_Buffer.Swap(); // write becomes new read — authoritative snapshot for this frame
}
```

### Step 3 — Execute: submit async jobs

```cpp
void BallPhysicsSystem::OnParallelExecute(Cosmic::Scene& scene, float dt)
{
    const PhysicsBody* src = m_Buffer.GetReadBuffer();  // workers read this
          PhysicsBody* dst = m_Buffer.GetWriteBuffer(); // workers write this

    // Capture simulation constants by value — safe across threads
    const float gravity = m_Gravity;
    const float boundsY = m_BoundsY;

    // Use the ASYNC variant — no WaitIdle, Scene handles the barrier
    Cosmic::ParallelForAsync(m_Buffer.Count(),
        [src, dst, dt, gravity, boundsY](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; ++i)
                Integrate(src[i], dst[i], dt, gravity, boundsY);
        });
}
```

### Step 4 — Merge: write results back

```cpp
void BallPhysicsSystem::OnMerge(Cosmic::Scene& scene, float dt)
{
    // All workers are idle by the time this runs.
    m_Buffer.Swap(); // promote computed results to read buffer

    auto& reg = scene.GetRegistry();
    const PhysicsBody* result = m_Buffer.GetReadBuffer();

    for (size_t i = 0; i < m_Entities.size(); ++i)
    {
        if (reg.valid(m_Entities[i]))
            reg.get<PhysicsBody>(m_Entities[i]) = result[i];
    }
}
```

### Step 5 — Register with the Scene

```cpp
auto scene = Cosmic::Scene::Create();
scene->AddSystem<BallPhysicsSystem>();
// That's it — the Scene manages the pipeline automatically.
```

The `AddSystem` call detects that `BallPhysicsSystem` derives from `ParallelSystem` and caches the pointer. No per-frame `dynamic_cast` occurs.

---

## 5. Template Project Walkthrough

The included `TemplateRenderBenchmarkLayer` demonstrates a **self-contained** parallel physics loop driven directly from a `Layer`, without a `ParallelSystem` registration. This is the right approach for benchmark layers and other self-managed parallel work that doesn't need Scene-level scheduling.

It implements the exact same Prepare → Execute → Merge pattern manually:

```
OnFixedUpdate:
  ┌── PREPARE ───────────────────────────────────────────────────────────┐
  │  Group all physics bodies via EnTT view                              │
  │  Copy current component state into DoubleBuffer write buffer         │
  │  Swap write→read (authoritative snapshot for this tick)              │
  └──────────────────────────────────────────────────────────────────────┘
  ┌── EXECUTE ───────────────────────────────────────────────────────────┐
  │  ParallelFor(count, [src, dst, ...](begin, end) { Integrate(...); }) │
  │  ParallelFor calls WaitIdle internally — synchronous here is correct │
  │  because this is standalone code, not inside OnParallelExecute       │
  └──────────────────────────────────────────────────────────────────────┘
  ┌── MERGE ─────────────────────────────────────────────────────────────┐
  │  Swap DoubleBuffer (computed results → read buffer)                  │
  │  Write back positions to TransformComponent in EnTT registry         │
  └──────────────────────────────────────────────────────────────────────┘
```

**Single-threaded mode** runs the exact same `Integrate()` kernel in a plain `for` loop on the main thread — same math, no job overhead — making performance comparisons apples-to-apples.

### Why it uses `ParallelFor` (synchronous) instead of `ParallelForAsync`

Because the layer manually calls `WaitIdle` (implicitly, through `ParallelFor`) and then immediately reads results. There is no Scene barrier to rely on. `ParallelFor` is the correct choice whenever you own the call site end-to-end.

---

## 6. Standalone Parallel Work (Outside a System)

If you need parallel work inside a Layer or a one-off helper — not inside a `ParallelSystem` — use the synchronous variants:

```cpp
// Integrate physics on-demand (e.g. from a Layer::OnFixedUpdate)
Cosmic::ParallelFor(bodyCount,
    [src, dst, dt](size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
            Integrate(src[i], dst[i], dt);
    });
// Execution is complete here — safe to read dst immediately.

// Or with a typed array:
Cosmic::ParallelForEach(transforms, count,
    [dt](TransformComponent* begin, TransformComponent* end)
    {
        for (auto* t = begin; t != end; ++t)
            t->Position += t->Velocity * dt;
    });
```

---

## 7. DoubleBuffer Reference

`DoubleBuffer<T>` maintains two identically-sized arrays. At any given time one is the **read buffer** (authoritative world state) and the other is the **write buffer** (results being computed by workers).

```
Frame N:
  ReadBuffer  → state from end of frame N-1   (workers read)
  WriteBuffer → output of frame N computation (workers write)

After Swap():
  ReadBuffer  → state from end of frame N     (ready for render / merge)
  WriteBuffer → will be overwritten in frame N+1
```

| Method | Description | Thread safety |
|--------|-------------|---------------|
| `Resize(count)` | Reallocate both buffers | Main thread only |
| `GetReadBuffer()` | Pointer to current read buffer | Thread-safe concurrent reads |
| `GetWriteBuffer()` | Pointer to current write buffer | Each index owned by one thread |
| `ReadAt(i)` / `WriteAt(i)` | Element access | Same as above |
| `Swap()` | O(1) index toggle | Main thread only, after WaitIdle |
| `CopyReadToWrite()` | Carry-forward: copy read → write | Main thread only, before jobs |
| `Count()` | Number of elements per buffer | Read-only, any thread |

**Typical frame sequence:**

```cpp
// --- OnPrepare (main thread) ---
// Copy EnTT components into write buffer
for (size_t i = 0; i < count; ++i)
    m_Buffer.WriteAt(i) = reg.get<T>(entities[i]);
m_Buffer.Swap();             // write → read (snapshot)

// --- OnParallelExecute (main thread, jobs on workers) ---
const T* src = m_Buffer.GetReadBuffer();
      T* dst = m_Buffer.GetWriteBuffer();
ParallelForAsync(count, [src, dst](...) { /* process src[i] → dst[i] */ });

// --- OnMerge (main thread, after WaitIdle) ---
m_Buffer.Swap();             // computed results → read buffer
const T* result = m_Buffer.GetReadBuffer();
// ... write back to registry ...
```

---

## 8. ComponentArray Reference

`ComponentArray<T>` is a non-owning zero-copy view into EnTT's internal component storage. Use it when you need a raw pointer to pass to `ParallelFor`.

```cpp
auto view = Cosmic::ComponentArray<TransformComponent>::From(scene.GetRegistry());

Cosmic::ParallelForEach(view.Data(), view.Count(),
    [dt](TransformComponent* begin, TransformComponent* end)
    {
        for (auto* t = begin; t != end; ++t)
            t->Position.x += t->Velocity.x * dt;
    });
```

**Important limitation:** EnTT stores components in paged memory. `ComponentArray` accesses only the first page via `storage.raw()[0]`. For dense components under ~4000–16000 entities this is always one page and fully correct. For larger counts use `FlatComponentArray<T>`, which copies all pages into a single contiguous allocation.

```cpp
// FlatComponentArray — safe at any entity count, costs a memcpy upfront
auto flat = Cosmic::FlatComponentArray<PhysicsBody>::From(scene.GetRegistry());

Cosmic::ParallelForEach(flat.Data(), flat.Count(),
    [](PhysicsBody* begin, PhysicsBody* end) { /* ... */ });

// After parallel phase:
flat.WriteBack(scene.GetRegistry()); // copies modified values back by entity ID
```

---

## 9. Rules & Constraints

### Worker threads must NOT:
- Modify the EnTT registry (create/destroy entities, add/remove components)
- Call `WaitIdle()` — fixed-size pool would deadlock if a job tried to wait on other jobs
- Submit new jobs from within a running job (nested dispatch)
- Access mutable state shared with other workers without external synchronisation

### Safe from worker threads:
- Read from `DoubleBuffer::GetReadBuffer()` (concurrent reads, no writes)
- Write to `DoubleBuffer::GetWriteBuffer()` — **each index must be owned by exactly one thread**, which `ParallelFor` chunk assignment guarantees
- Read from `ComponentArray` (non-modifying access)
- Call pure functions and allocate thread-local scratch memory

### Capture rules for job lambdas:
- Capture simulation constants (floats, ints) **by value**
- Capture pointers to pre-allocated buffers **by value** (the pointer itself, not what it points to)
- Never capture `std::vector`, `std::string`, or other owning types by reference unless you can prove the lambda executes within the owning scope and WaitIdle is called before it returns
- Never capture `this` from a system unless you are certain no structural changes happen during execution

---

## 10. When NOT to Use the Job System

The job system has overhead — mutex locks, condition variable signals, cache misses from cross-thread data. It is beneficial only when the per-element work is substantial enough to amortise that cost.

| Scenario | Guidance |
|----------|----------|
| < ~500 entities | Serial loop is faster; `ParallelFor`'s serial fast-path triggers at `minChunkSize` |
| Work with complex branching or I/O | Workers stall, gaining nothing; keep on main thread |
| Code that modifies the registry | Must run single-threaded (OnMerge or OnUpdate) |
| Rendering / GPU submission | OpenGL contexts are thread-local; render on main thread only |
| Debug / editor tools | Simpler to keep synchronous; parallelise in shipping builds if needed |
