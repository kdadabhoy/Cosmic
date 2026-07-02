# Cosmic Engine — Analysis Report

> **⚠️ Historical analysis (2026-05-30).** P1/P2 items were fixed in the 2026-06-24 pass (see
> [IMPROVEMENTS.md](IMPROVEMENTS.md)); remaining live items were re-audited into
> [docs/plans/01-bug-audit.md](plans/01-bug-audit.md) (2026-07-01). Section 6/5.1 (text rendering)
> is stale — world-space SDF text now exists (README §27).

**Date:** 2026-05-30 (updated from 2026-05-29)  
**Scope:** Render system (graphics/, renderer/, platform/OpenGL/), Telemetry system (telemetry/), Job System & Parallel Pipeline (jobs/), Engine/Client architecture, README accuracy  
**Files Reviewed:** 60+ source files across all subsystems

---

## Table of Contents

1. [Render System Overview](#1-render-system-overview)
2. [Telemetry System Overview](#2-telemetry-system-overview)
3. [Job System & Parallel Pipeline Overview](#3-job-system--parallel-pipeline-overview)
4. [Issues & Bugs — Priority List](#4-issues--bugs--priority-list)
   - [P1 — Critical Bugs](#p1--critical-bugs)
   - [P2 — High Priority](#p2--high-priority)
   - [P3 — Medium Priority](#p3--medium-priority)
   - [P4 — Low Priority / Design Issues](#p4--low-priority--design-issues)
5. [README Inaccuracies](#5-readme-inaccuracies)
6. [Possible Additions](#6-possible-additions)

---

## 1. Render System Overview

### Architecture

The render system is a clean three-tier stack:

```
Client Code (Layer)
        ↓
Renderer2D          — high-level batch API (DrawQuad, DrawCircle, DrawLine, etc.)
Renderer            — legacy single-draw submit path (mostly unused)
RenderCommand       — static dispatcher → RendererAPI (abstract)
        ↓
OpenGLRendererAPI   — concrete OpenGL 4.5 implementation
```

**Renderer2D** is the primary drawing interface. It runs three independent CPU-side batch pipelines internally:

| Pipeline | Max Objects | Draw Mode | GPU Upload |
|---|---|---|---|
| Quad batch | 10,000 quads | Batched `DrawIndexed` | CPU-staged vertex stream |
| Circle batch (SDF) | 10,000 circles | Batched `DrawIndexed` | CPU-staged vertex stream |
| Line batch | 10,000 lines | Batched `DrawLines` | CPU-staged vertex stream |
| Instanced Quads | 20,000 / draw call | `DrawIndexedInstanced` | Per-instance stream |
| Instanced Circles | 20,000 / draw call | `DrawIndexedInstanced` | Per-instance stream |

A **RenderPass stack** (push/pop) allows multiple cameras to render sequentially in one frame without manual VP matrix tracking. The RAII `RenderPass` class wraps this automatically.

**Materials** attach a shader + uniform bag to a draw call. Within the batch renderer, switching materials forces a `FlushAndReset` — effectively ending the current batch. Materials work best when a large group of quads share the same shader.

### Engine vs. Client Perspective

**From the engine side**, Renderer2D is initialized once by `Application::Initialize()` → `Renderer::Init()`, which allocates all six VAOs, vertex buffers, and staging arrays. The engine owns the raw memory for staging buffers (`new QuadVertex[MaxVertices]`, etc.) and frees them in `Renderer2D::Shutdown()`.

**From the client side**, any `Layer::OnUpdate()` call can issue draw calls within a `BeginScene`/`EndScene` block. The RenderPass RAII pattern is the modern, preferred entry point. Clients should not call `Flush()` directly; the engine calls it automatically on `EndScene`/`PopRenderPass` and whenever a batch capacity limit is hit.

---

## 2. Telemetry System Overview

### Architecture

Five cooperating subsystems:

```
DataRecorder   — thread-safe columnar capture (float channels per entity)
DataPlayer     — binary file reader with linear interpolation and seek
TelemetryPanel — ImGui/ImPlot UI: mode state machine, ring buffer display
EntitySelection — global "which entity is selected" pub-sub service
EntityPicker   — header-only CPU AABB hit test + screen-to-world math
```

**DataRecorder** stores data in a columnar layout: `columns[channel][frame]` rather than `frames[frame][channel]`. After `ReserveCapacity()` is called, `Record()` performs zero heap allocations per call. Each entity has its own mutex; parallel workers recording to different entities never contend. `Register()` returns a stable `uint32_t` ID that parallel jobs capture by value to avoid string lookups in the hot path.

**DataPlayer** reads the binary `scene.bin` v1 format and exposes linear interpolation via `GetFrame()` and `SampleAt()`. Binary search on stored timestamps gives O(log n) seek. Each frame stores its recorded simulation timestamp, so playback is correct regardless of what `TimeScale` was active during recording.

**TelemetryPanel** owns the replay lifecycle — it drives `DataPlayer::Tick()` each frame when in Replay mode. The panel tracks an explicit `Mode` enum (`Mode::None`, `Mode::Live`, `Mode::Replay`) so data sources are unambiguous. `SetRecorder()` switches to Live; the panel switches to Replay automatically on a successful user-initiated file load. It uses a circular ring buffer (512 samples deep) per channel, and dispatches to user-registered inspector callbacks by entity name or tag (exact name takes priority over tag match). **`DrawTransportControls()`** is a separate public method intentionally decoupled from `OnImGuiRender()` — it renders the playback transport bar and can be embedded in any independent ImGui window.

**EntitySelection** is a global static service with a snapshot-based pub-sub model. `OnChanged()` returns a `SubscriptionHandle` (opaque `uint32_t`) that callers must store and pass to `Unsubscribe()` from their destructor to prevent dangling callback captures. `Notify()` snapshots the subscriber list under the lock before firing, so re-entrant calls (e.g. calling `Set()` from inside a callback) don't deadlock. `SetByName()` allows replay mode to set name/tag without a live entity handle.

**EntityPicker** is header-only (no `COSMIC_API` export). `Pick()` accepts an optional `std::function<bool(Entity)>` predicate to filter hits beyond the basic AABB + `SelectableComponent` check. `ScreenToWorld()` handles the GLFW-to-viewport offset and the Y-axis flip from GLFW top-left origin to OpenGL bottom-left NDC.

### Canonical Reference: `TemplateTelemetryLayer`

`Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.h/.cpp` is the complete end-to-end reference wiring all five subsystems for 20 simulated agents. It also includes `AgentSystem.h` as a `ParallelSystem` + `DataRecorder` integration example. The README §26 references this as the complete working reference.

### Engine vs. Client Perspective

**From the engine side**, the telemetry system has zero renderer or GPU dependencies — it only touches `core/`, `scene/`, ImGui, and ImPlot. The engine provides `TelemetryPanel` as a ready-to-embed ImGui widget. The `EntityPicker` provides picking support using the engine's `TransformComponent` and the `SelectableComponent` tag.

**From the client side**, a project registers entities with `DataRecorder::Register()`, calls `Record()` from parallel systems (thread-safe), ticks the recorder clock with `Tick()`, and exports data with `Flush()`. The `TelemetryPanel` handles all UI automatically once `SetRecorder` and `SetPlayer` are called. Transport controls are embedded via `DrawTransportControls()` in a separate window (typically the "Project Inspector Top" sidebar slot).

---

## 3. Job System & Parallel Pipeline Overview

### Architecture

Six cooperating components across `jobs/`:

```
JobSystem          — singleton thread pool (coreCount−1 persistent workers)
ParallelFor        — 6 free-function helpers: sync and async index-range dispatch
ParallelSystem     — 4-pass ECS parallel integration base class
ReadWriteQuery<T>  — auto-staged mutable component snapshot (ISystemQuery impl)
ReadOnlyQuery<T>   — auto-staged immutable snapshot for cross-entity reads
DoubleBuffer<T>    — O(1) Swap() read/write separation for inter-entity parallelism
ComponentArray<T>  — zero-copy pointer into the first EnTT storage page
FlatComponentArray<T> — full-copy of all pages into a single contiguous buffer
```

### The 4-Pass Pipeline

Every fixed-step (and variable-step) frame the `Scene` executes four passes in strict order:

| Pass | Thread | What happens |
|---|---|---|
| A — Sequential | Main | All `System::OnFixedUpdate()` calls. Safe for entity create/destroy, ordered logic, registry writes. |
| B — Prepare | Main | Engine stages all `SystemQuery` snapshots from registry. Then `ParallelSystem::OnFixedPrepare()` runs. Per-tick setup, constant pre-computation. |
| C — Execute | Workers | All `ParallelSystem::OnFixedParallelExecute()` submit jobs and return immediately. `JobSystem::WaitIdle()` is called **once** after all systems have submitted — jobs from different systems can overlap. |
| D — Merge | Main | All `ParallelSystem::OnFixedMerge()` run. Engine commits `ReadWriteQuery` results back to registry. Structural changes (entity create/destroy) are safe here. |

Variable-rate equivalents (`OnPrepare`, `OnParallelExecute`, `OnMerge`) run through the same pipeline inside `Scene::OnUpdate`.

### `SystemQuery<T>` — Staged Snapshot Protocol

`ReadWriteQuery<T>` and `ReadOnlyQuery<T>` implement `ISystemQuery`. Declare them as member variables of a `ParallelSystem` subclass, pass `this` to the constructor, and the engine handles the rest:

```cpp
class BallPhysicsSystem : public Cosmic::ParallelSystem
{
    Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };  // self-registers
public:
    void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
    {
        const float g = Gravity;
        m_Bodies.ForEachAsync([dt, g](PhysicsBody& body)
        {
            body.Velocity.y += g * dt;
            body.Position   += body.Velocity * dt;
        });
        // Do NOT call WaitIdle — Scene calls it once after all systems submit
    }

    void OnFixedMerge(Cosmic::Scene& scene, float dt) override
    {
        m_Bodies.ForEachWithEntity([&scene](PhysicsBody& body, entt::entity e)
        {
            auto& t = scene.GetRegistry().get<Cosmic::TransformComponent>(e);
            t.Position = { body.Position.x, body.Position.y, t.Position.z };
        });
        // Engine commits m_Bodies → PhysicsBody registry after this returns
    }
};
```

`Stage()` copies component values + entity handles into internal vectors before Pass B. `Commit()` writes them back after Pass D. `ReadOnlyQuery::Commit()` is a no-op.

### `ParallelFor` Free Functions

Six functions are provided — three synchronous (call `WaitIdle` before returning) and three async (submit only; caller's barrier covers them):

| Synchronous | Async | Signature pattern |
|---|---|---|
| `ParallelFor` | `ParallelForAsync` | `(count, func(begin, end))` |
| `ParallelForEach<T>` | `ParallelForEachAsync<T>` | `(T* data, count, func(T* begin, T* end))` |
| `ParallelForEachIndexed<T>` | `ParallelForEachIndexedAsync<T>` | `(T* data, count, func(T& item, size_t i))` |

**Critical rule:** Use only the **Async** variants inside `OnParallelExecute`. The synchronous variants call `WaitIdle` internally and would serialize systems against each other. Never call `JobSystem::WaitIdle()` yourself from `OnParallelExecute` — the Scene calls it once after all systems submit.

The async variants capture their `func` argument **by value** — unlike the synchronous variants which capture by reference (the caller's stack frame is alive for the synchronous lifetime). Closures submitted to the async pool execute after the caller returns; capturing stack locals by reference produces dangling pointers.

### `DoubleBuffer<T>` — Inter-Entity Parallel Safety

When a worker must read one entity's data while writing another's (e.g. flocking, collision), a single buffer creates data races. `DoubleBuffer<T>` provides read/write separation:

```cpp
DoubleBuffer<glm::vec2> velocities;
velocities.Resize(entityCount);
velocities.CopyReadToWrite();  // seed write buffer
velocities.Swap();             // O(1) — XOR on m_ReadIndex; no data move
// Workers read GetReadBuffer(), write GetWriteBuffer() — no overlap
```

`T` must be **trivially copyable** — `CopyReadToWrite` uses `std::memcpy`. A `static_assert` fires at instantiation if `T` is not trivially copyable. Use `ReadWriteQuery<T>` instead for component types with non-trivial copy semantics.

### `ComponentArray<T>` and `FlatComponentArray<T>`

`ComponentArray<T>::From(registry)` returns a non-owning pointer into EnTT's **first storage page only** (≤ ~1024 elements). Zero allocation, zero copy — useful for small scenes. `CS_CORE_ASSERT` fires if the pool spans more than one page. For larger scenes, use `FlatComponentArray<T>`, which copies all pages into a single contiguous buffer and provides a `WriteBack()` method to patch the registry after mutation.

### Engine vs. Client Perspective

**From the engine side**, the job system is initialized before any other subsystem in `Application::Initialize()` and shut down first in `Application::Shutdown()` to prevent jobs from accessing freed resources. The engine's `Scene::OnFixedUpdate()` drives all four passes automatically.

**From the client side**, a project subclasses `ParallelSystem`, declares `ReadWriteQuery<T>` or `ReadOnlyQuery<T>` members (passing `this`), overrides the parallel hooks, and registers the system with `m_Scene->AddSystem<MySystem>()`. The engine handles all staging, dispatch, barrier, and commit plumbing. The most common anti-pattern is calling the synchronous `ParallelFor` variants (or `JobSystem::WaitIdle()`) from inside `OnParallelExecute`, which collapses the parallel overlap into serial execution.

---

## 4. Issues & Bugs — Priority List

---

### P1 — Critical Bugs

These bugs produce incorrect behavior silently and should be fixed immediately.

---

#### P1-A: `Stats.LineCount` is never incremented — line statistics are always zero

**File:** `Cosmic/src/renderer/Renderer2D.cpp`, `DrawLine()` (~line 1074)  
**Priority:** Critical (silent data corruption in statistics)

**The bug:** `DrawLine` increments `s_Data.LineVertexCount += 2` but never increments `s_Data.Stats.LineCount`. As a result, `Stats.LineCount` stays at 0 for the entire session regardless of how many lines are drawn.

This silently breaks `Statistics::GetTotalVertexCount()`:
```cpp
// Stats.h — this formula always returns 0 for the line contribution:
uint32_t GetTotalVertexCount() const { return QuadCount * 4 + CircleCount * 4 + LineCount * 2; }
```

**How to fix:** Add the stat increment in `DrawLine` after writing both vertices:
```cpp
s_Data.LineVertexCount += 2;
if (s_Data.StatsEnabled) s_Data.Stats.LineCount++;  // add this line
```

---

#### P1-B: `Statistics::GetTotalIndexCount()` omits circles

**File:** `Cosmic/src/renderer/Renderer2D.h`, `Statistics` struct (~line 159)  
**Priority:** Critical (incorrect API surface)

**The bug:**
```cpp
uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
```
Circles also use 6 indices per circle (two-triangle quad with SDF fragment discard). The formula should be:
```cpp
uint32_t GetTotalIndexCount() const { return (QuadCount + CircleCount) * 6; }
```
Any code using `GetTotalIndexCount()` to reason about GPU work underestimates it whenever circles are drawn.

**How to fix:** Update the formula to `(QuadCount + CircleCount) * 6`.

---

#### P1-C: `Renderer::s_SceneData` is a permanent memory leak

**File:** `Cosmic/src/renderer/Renderer.cpp`, line 10  
**Priority:** Critical (memory leak on every engine run)

**The bug:**
```cpp
Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;
```
`Renderer::Shutdown()` never calls `delete s_SceneData`. Every engine session leaks this allocation.

**How to fix:** In `Renderer::Shutdown()`, add:
```cpp
void Renderer::Shutdown()
{
    Renderer2D::Shutdown();
    delete s_SceneData;
    s_SceneData = nullptr;
}
```
Alternatively, replace the raw pointer with a `static Renderer::SceneData s_SceneData;` value (no allocation needed, no leak possible).

---

#### P1-D: `OpenGLTexture` produces a corrupt GL state for 1- and 2-channel images

**File:** `Cosmic/src/platform/OpenGL/OpenGLTexture.cpp`, file-based constructor (~line 71)  
**Priority:** Critical (silent GL error, texture renders black or garbage)

**The bug:** The channel detection block handles 4 (RGBA) and 3 (RGB) channels but leaves `internalFormat` and `dataFormat` at 0 for any other channel count:
```cpp
if (channels == 4) { internalFormat = GL_RGBA8; dataFormat = GL_RGBA; }
else if (channels == 3) { internalFormat = GL_RGB8; dataFormat = GL_RGB; }
// channels == 1 (grayscale) or channels == 2 (grayscale+alpha): both remain 0
```
`glTexImage2D` is then called with `format = 0`, which is an invalid GL enum. The texture upload silently fails, the texture remains black, and a GL error is set in the error queue.

**How to fix:** Add handling for grayscale textures and emit a clear error when channels are unsupported:
```cpp
if (channels == 4)      { internalFormat = GL_RGBA8; dataFormat = GL_RGBA; }
else if (channels == 3) { internalFormat = GL_RGB8;  dataFormat = GL_RGB;  }
else if (channels == 1) { internalFormat = GL_R8;    dataFormat = GL_RED;  }
else if (channels == 2) { internalFormat = GL_RG8;   dataFormat = GL_RG;   }
else
{
    CS_CORE_ERROR("OpenGLTexture: Unsupported channel count ({}) in '{}'", channels, path);
    stbi_image_free(data);
    return;
}
```

---

#### P1-E: `DataPlayer::Load()` — legacy `.bin` fallback is documented but not implemented

**File:** `Cosmic/src/telemetry/DataPlayer.cpp`, `Load()` (~line 34)  
**Priority:** Critical (users who follow the documented behavior will get silent empty loads)

**The bug:** `DataPlayer.h` documents that when a directory is passed and `scene.bin` is absent, the player "falls back to individual v1 .bin files." The implementation does not do this:
```cpp
if (fs::is_directory(path))
{
    for (const auto& entry : fs::directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().filename() == "scene.bin")
        {
            LoadBinaryFile(entry.path().string());
            break;   // ← stops after scene.bin, never tries other .bin files
        }
    }
}
// If no scene.bin found: m_Entities is empty, Load returns false — no fallback
```
Any user with a legacy session folder (containing only per-entity `.bin` files, no `scene.bin`) will silently get an empty load.

**How to fix:** After the `scene.bin` search loop, if `m_Entities` is still empty, do a second pass over all `.bin` files in the directory:
```cpp
if (m_Entities.empty())
{
    for (const auto& entry : fs::directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".bin")
            LoadBinaryFile(entry.path().string());
    }
}
```

---

### P2 — High Priority

These issues affect correctness or performance noticeably but have narrower blast radius than P1 items.

---

#### P2-A: `DrawInstancedQuads` re-uploads the sampler array every call

**File:** `Cosmic/src/renderer/Renderer2D.cpp`, `DrawInstancedQuads()` (~line 1234)  
**Priority:** High (performance)

**The issue:** Every call to `DrawInstancedQuads` re-uploads all 32 sampler integers via `SetIntArray("u_Textures", ...)`:
```cpp
int32_t samplers[Renderer2DData::MaxTextureSlots];
for (int32_t i = 0; i < MaxTextureSlots; ++i) samplers[i] = i;
targetShader->SetIntArray("u_Textures", samplers, Renderer2DData::MaxTextureSlots);
```
The batch quad pipeline correctly does this only once in `Init`. For high-frequency instanced draws (e.g., 1,000 calls/frame), this is 32,000 redundant uniform uploads per frame.

**How to fix:** Move the sampler array upload into `Renderer2D::Init()` after the instanced quad shader is loaded, just like the batch quad pipeline does. Cache the sampler array as a module-level constant or set it once on shader creation.

---

#### P2-B: `OpenGLRendererAPI::DrawLines` binds the VAO internally (inconsistent contract)

**File:** `Cosmic/src/platform/OpenGL/OpenGLRendererAPI.cpp`, `DrawLines()` (~line 83)  
**Priority:** High (API contract inconsistency, potential double-bind confusion)

**The issue:** `DrawLines` calls `vertexArray->Bind()` internally, but `DrawIndexed` and `DrawIndexedInstanced` do not. In `Renderer2D::Flush()`, the calling code already binds the VAO before calling `RenderCommand::DrawLines()`, causing a redundant bind:
```cpp
// Flush() already binds:
s_Data.LineVertexArray->Bind();
RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);
    // ↑ internally calls vertexArray->Bind() again
```
`DrawIndexed` has no such redundancy. The inconsistency makes the API contract for `RendererAPI` unclear — callers cannot tell whether they are responsible for binding.

**How to fix:** Remove the `vertexArray->Bind()` from `OpenGLRendererAPI::DrawLines`. Binding is the caller's responsibility for all three methods. Optionally add a comment to `RendererAPI.h` that the caller must bind before all `Draw*` calls.

---

#### P2-C: Version label mismatch in `DataRecorder` header documentation

**File:** `Cosmic/src/telemetry/DataRecorder.h`, `Flush()` docstring (~line 165)  
**Priority:** High (documentation actively misleads users about the file format)

**The issue:** The `Flush()` docstring says:
> "Output layout: `<baseFolder>/<sessionName>/scene.bin` — all entities, **v3 binary format**"

But the block comment at the top of the same file says `BINARY FILE FORMAT v1`, and the actual code writes `version = 1u`. There is no v3 format. This is an outdated docstring from an internal refactor.

Similarly, `DataPlayer.h` `Load()` says "loads scene.bin (v2/v3)" when only v1 exists.

**How to fix:** Update `DataRecorder.h` `Flush()` docstring: "all entities, **v1 binary format**". Update `DataPlayer.h` `Load()` docstring: "loads scene.bin (v1)".

---

#### P2-D: `DrawRotatedQuad` with `Material` has no `glm::vec2` position overload

**File:** `Cosmic/src/renderer/Renderer2D.h` (~line 78)  
**Priority:** High (API inconsistency; client code that passes a `vec2` won't compile)

**The issue:** Every other `DrawRotatedQuad` variant has both `vec3` and `vec2` position overloads. The Material variant only has `vec3`:
```cpp
// Has vec2 + vec3:
static void DrawRotatedQuad(const glm::vec2&, const glm::vec2&, float, const glm::vec4&);
static void DrawRotatedQuad(const glm::vec3&, const glm::vec2&, float, const glm::vec4&);

// Only has vec3:
static void DrawRotatedQuad(const glm::vec3&, const glm::vec2&, float, const Ref<Material>&);
// Missing:
// static void DrawRotatedQuad(const glm::vec2&, const glm::vec2&, float, const Ref<Material>&);
```

**How to fix:** Add the missing overload to `Renderer2D.h` and implement it as a one-liner forwarding delegate in `Renderer2D.cpp`:
```cpp
// .h
static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
    float rotation, const Ref<Material>& material);

// .cpp
void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size,
    float rot, const Ref<Material>& material)
{
    DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, material);
}
```

---

#### P2-E: `TelemetryPanel::DrawPlots` uses `ImPlotSpec` — non-standard ImPlot API

**File:** `Cosmic/src/telemetry/TelemetryPanel.cpp`, `DrawPlots()` (~line 407)  
**Priority:** High (may not compile or behave correctly with stock ImPlot)

**The issue:**
```cpp
ImPlotSpec spec;
spec.Offset = m_PlotOffset;
ImPlot::PlotLine(chName, m_PlotTimes.data(), m_PlotBuffers[ch].data(), m_PlotCount, spec);
```
`ImPlotSpec` is not part of the standard ImPlot API. The standard signature is:
```cpp
void ImPlot::PlotLine(const char* label, const float* xs, const float* ys,
                      int count, ImPlotLineFlags flags = 0,
                      int offset = 0, int stride = sizeof(float));
```
The offset for the ring buffer should be passed as the plain `int` parameter. Either `ImPlotSpec` is a custom engine extension (which should be documented), or this code will fail to compile against a clean ImPlot build.

**How to fix:** If `ImPlotSpec` is not defined anywhere in the engine's copy of ImPlot, replace with:
```cpp
ImPlot::PlotLine(chName,
    m_PlotTimes.data(),
    m_PlotBuffers[ch].data(),
    m_PlotCount,
    0,           // flags
    m_PlotOffset // offset (ring buffer start)
);
```

---

### P3 — Medium Priority

These are real issues but have lower immediate impact or are only triggered in specific scenarios.

---

#### P3-A: `DrawInstancedCircles` leaves `ActiveCircleShader = nullptr`, causing a spurious flush

**File:** `Cosmic/src/renderer/Renderer2D.cpp`, `DrawInstancedCircles()` cleanup (~line 1191)  
**Priority:** Medium (unnecessary GPU flush in mixed pipeline frames)

**The issue:** After `DrawInstancedCircles` finishes, it sets `s_Data.ActiveCircleShader = nullptr` as cleanup. If the client then calls `DrawCircle()` in the same frame, the shader comparison `s_Data.ActiveCircleShader != targetShader` (null != DefaultCircleShader) is true, triggering a `FlushAndReset()` even though the circle batch buffer is empty. This is wasteful but harmless correctness-wise.

**How to fix:** Instead of setting to `nullptr`, restore to `s_Data.DefaultCircleShader`:
```cpp
// Line 1191 in DrawInstancedCircles:
s_Data.ActiveCircleShader = s_Data.DefaultCircleShader;  // was: nullptr
```

---

#### P3-B: Texture linear lookup is O(n) per draw call

**File:** `Cosmic/src/renderer/Renderer2D.cpp`, all `DrawQuad` texture variants (~line 695)  
**Priority:** Medium (performance degrades as texture slot count approaches 32)

**The issue:** Every texture-using `DrawQuad` call performs a linear scan of `TextureSlots[0..TextureSlotIndex]` to check if the texture is already registered:
```cpp
for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
{
    if (s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID())
    { textureIndex = (float)i; break; }
}
```
With 32 slots this is bounded and fast in practice, but scenes that frequently switch textures near the 32-slot limit will perform 32 comparisons per quad. A `std::unordered_map<GLuint, uint32_t>` keyed by renderer ID would make this O(1) amortized.

**How to fix:** Add an `unordered_map<uint32_t, uint32_t> TextureSlotLookup` to `Renderer2DData`. On each `FlushAndReset`, clear the map. On each new texture registration, insert `{rendererID → slotIndex}`. Replace the linear scan with a single map lookup.

---

#### P3-C: CPU-side 4×4 matrix per quad in the batch pipeline

**File:** `Cosmic/src/renderer/Renderer2D.cpp`, all `DrawQuad`/`DrawRotatedQuad` implementations  
**Priority:** Medium (CPU bottleneck at high quad counts)

**The issue:** The batch pipeline pre-transforms all four vertices of each quad on the CPU before uploading:
```cpp
glm::mat4 transform = glm::translate(...) * glm::scale(...);
for (uint32_t i = 0; i < 4; i++)
    s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
```
A 4×4 matrix multiply per vertex × 4 vertices = 16 full mat4 multiplies per quad. At 10,000 quads per flush, that is 160,000 mat4 multiplies per flush. The instanced pipeline avoids this entirely by passing position/scale to the GPU and expanding to world space in the vertex shader. For simulations with hundreds to thousands of dynamic quads, this is the performance bottleneck.

**How to fix (long term):** Add an optional `DrawQuad` overload that accepts `position + scale` as raw floats and packs them into the instanced pipeline. This is already possible today via `DrawInstancedQuads` — the main gap is documentation clarity. For the batch pipeline, consider replacing the 4-vertex transform with a per-vertex position + a per-instance TRS stored in the GPU vertex buffer, deferring the expansion to the vertex shader.

---

#### P3-D: `Material::Bind()` does not bind textures outside the batch renderer

**File:** `Cosmic/src/graphics/Material.h`, `Bind()` (~line 66)  
**Priority:** Medium (silent rendering failure when Material is used outside Renderer2D)

**The issue:** `Material::Bind()` uploads scalar and vector uniforms but explicitly does NOT bind textures to GPU slots. The comment explains this is by design for the batch path, but it means any code using `Renderer::Submit` with a material-backed draw will see blank textures unless the caller manually binds each texture. This is a dangerous footgun — the omission is documented in a code comment but not visible in the public API signature.

**How to fix:** Either document the limitation clearly in the header (done, but improve visibility), or add a separate `BindFull()` method that also binds textures to their slots, which callers outside the batch renderer can use safely.

---

#### P3-E: `EntityPicker::Pick` ignores entity rotation

**File:** `Cosmic/src/telemetry/EntityPicker.h`, `Pick()` (~line 88)  
**Priority:** Medium (picking misses or incorrectly hits rotated entities)

**The issue:** The AABB test uses axis-aligned bounds based on `Scale`:
```cpp
float halfW = transform.Scale.x * 0.5f;
float halfH = transform.Scale.y * 0.5f;
bool hitX = glm::abs(worldPos.x - transform.Position.x) <= halfW;
bool hitY = glm::abs(worldPos.y - transform.Position.y) <= halfY;
```
`TransformComponent` almost certainly has a rotation field. A 45° rotated square will have a picking box that doesn't match its visual bounds. Clicking on a corner of a rotated entity may miss; clicking near the center but outside the actual shape may hit.

**How to fix:** For the general case, transform the query point into the entity's local space, then do an axis-aligned test there:
```cpp
// Undo the entity's rotation to get a local-space query point:
float cosA = std::cos(-transform.Rotation);
float sinA = std::sin(-transform.Rotation);
float dx = worldPos.x - transform.Position.x;
float dy = worldPos.y - transform.Position.y;
float localX = cosA * dx - sinA * dy;
float localY = sinA * dx + cosA * dy;
bool hit = std::abs(localX) <= halfW && std::abs(localY) <= halfH;
```

---

#### P3-F-NEW: `ParallelForAsync` captures `func` by value, `ParallelFor` by reference — different capture rules are easy to confuse

**File:** `Cosmic/src/jobs/ParallelFor.h`  
**Priority:** Medium (subtle undefined behavior if the wrong capture convention is used)

**The issue:** The synchronous `ParallelFor` captures `func` by reference (safe because `WaitIdle` blocks until workers finish, keeping the caller's stack alive). The async `ParallelForAsync` captures `func` by value (required because it returns before workers finish). This difference is documented in the header but it's easy to write a closure that captures a local variable by reference, paste it into the async variant, and get a dangling reference with no compile-time warning.

There is no `static_assert` or type-level distinction that prevents accidentally using a by-reference capturing lambda with the async path.

**How to fix (long term):** Add a `static_assert` checking that the functor is trivially copyable, or at minimum add a `[[nodiscard]]` attribute and a comment at each call site reminding users of the capture convention. A template wrapper that forces `std::decay_t<Func>` (value copy) on the async path would catch most accidental by-reference captures at instantiation time.

---

#### P3-G-NEW: `ComponentArray<T>` single-page limit has silent UB beyond ~1024 entities

**File:** `Cosmic/src/jobs/ComponentArray.h` (referenced in README §35)  
**Priority:** Medium (works on small projects, silently corrupts on growth)

**The issue:** `ComponentArray<T>::From(registry)` returns a pointer into EnTT's first storage page only. `Count()` returns the total component count across all pages. If `Count() > page_size` (~1024 by default), indexing past the first page is out-of-bounds. A `CS_CORE_ASSERT` fires at `From()` time in Debug, but in Release the assert is compiled out and the access silently reads garbage memory or crashes.

Projects that start small and grow beyond 1024 entities will hit this silently in Release builds.

**How to fix:** Either always return an error/assert in both Debug and Release when the pool spans multiple pages, or remove `ComponentArray<T>` in favor of always using `FlatComponentArray<T>`. The zero-copy advantage of `ComponentArray` is real but not worth the silent Release corruption.

---

#### P3-F: `Renderer` class maintains a parallel `SceneData` alongside `Renderer2DData`

**File:** `Cosmic/src/renderer/Renderer.h`, `Renderer.cpp`  
**Priority:** Medium (architectural confusion, dual source of truth for VP matrix)

**The issue:** `Renderer` stores its own `SceneData::ViewProjectionMatrix` (used only by `Renderer::Submit`), while `Renderer2D` stores its own VP matrix in `s_Data.ViewProjectionMatrix` (used by all batch draws). These two are set independently — calling `Renderer::BeginScene()` does not affect `Renderer2D`, and calling `Renderer2D::BeginScene()` does not affect `Renderer`. A developer mixing `Renderer::Submit` with `Renderer2D::DrawQuad` in the same frame will silently draw geometry under two different cameras.

The `Renderer` class's `Submit` methods are also unbatched — each call is one GPU draw, bypassing the entire batch system.

**How to fix:** For the long term, document clearly that `Renderer::Submit` is a legacy low-level path for custom shader work only. Add an assertion or `CS_CORE_WARN` in `Renderer::BeginScene` noting that it doesn't sync with `Renderer2D`. Alternatively, have `Renderer::BeginScene` forward the camera to `Renderer2D::PushRenderPass` so they stay in sync.

---

### P4 — Low Priority / Design Issues

---

#### P4-A: `FramebufferSpecification::Samples` and `SwapChainTarget` are public dead fields

**File:** `Cosmic/src/graphics/FrameBuffer.h` (~line 63)  
**Priority:** Low (misleading public API)

Both `Samples` (MSAA) and `SwapChainTarget` are marked "Reserved — not yet implemented" in comments but are still first-class public fields in the spec struct. A developer who sets `Samples = 4` expecting MSAA will get single-sample rendering with no error or warning.

**How to fix:** Either implement MSAA, or replace the fields with a `// RESERVED` comment and remove them from the public struct until implemented. If they must stay for forward-compatibility, add a `CS_CORE_WARN` in `FrameBuffer::Create` when `Samples > 1` or `SwapChainTarget == true`.

---

#### P4-B: `BeginScene`/`EndScene` are legacy shims but primary README examples use them

**File:** `Cosmic/src/renderer/Renderer2D.h`, `Renderer2D.cpp`  
**Priority:** Low (user experience / best-practice guidance)

`BeginScene`/`EndScene` are implemented as thin wrappers over `PushRenderPass`/`PopRenderPass`. They work correctly but provide no viewport control and force the full-window bounds. The `RenderPass` RAII class is strictly more capable. The README client guide (§8) leads with `BeginScene`/`EndScene` examples, which trains new users on the legacy path.

**How to fix:** Update §8 and the minimal project skeleton in the README to show `RenderPass` as the primary path, with `BeginScene`/`EndScene` noted as a shorthand for full-window, single-camera setups.

---

#### P4-C: No automatic depth sorting for alpha-blended geometry

**File:** `Renderer2D.cpp` — entire batch pipeline  
**Priority:** Low (known 2D engine tradeoff, but worth documenting)

All batched geometry is rendered in submission order. For layered 2D with alpha blending, this is intentional — the client controls draw order by submission sequence. However, there is no built-in sort pass (by z-value or a layer index), which means getting correct layering in dynamically ordered scenes requires manual draw-call sequencing. There are no warnings when `DrawQuad` receives a negative z that would require back-to-front sorting.

**How to fix:** Document the submission-order guarantee explicitly in `Renderer2D.h`. A future enhancement could be a sort step before `Flush()` that groups by z-value within each batch.

---

#### P4-D: `DataRecorder::GetTotalFrameCount` only proxies the first entity

**File:** `Cosmic/src/telemetry/DataRecorder.cpp`, `GetTotalFrameCount()` (~line 140)  
**Priority:** Low (incorrect proxy if entities have different sample counts)

```cpp
size_t DataRecorder::GetTotalFrameCount() const
{
    if (m_Records.empty()) return 0;
    std::lock_guard<std::mutex> lock(m_Records[0]->mutex);
    return m_Records[0]->timestamps.size();
}
```
If entities are registered at different times or have different recording intervals, their frame counts can diverge. Callers relying on this function for total session length will get a misleading value.

**How to fix:** Return the maximum across all entities, or rename to `GetFirstEntityFrameCount()` with a doc comment clarifying it's a proxy.

---

## 5. README Inaccuracies

---

#### README-A: `RenderPass` constructor in §34 documents a non-existent optional-viewport interface

**Section:** §34 RenderPass Stack — Implementation Details  
**Severity:** Misleading (still present; the README was updated but in the wrong direction)

The actual constructor in `Cosmic/src/renderer/RenderPass.h:90`:
```cpp
RenderPass(const OrthographicCamera& camera, const glm::vec4& viewportBounds)
```
The viewport is **mandatory** — no default, no `std::optional`.

The README §34 now shows:
```cpp
RenderPass(const OrthographicCamera& camera,
           std::optional<glm::vec4> viewportBounds = std::nullopt);
```
and describes semantics for the `std::nullopt` case ("viewport is left unchanged"). This interface does not exist. The `PushRenderPass` function that the constructor calls also takes a mandatory `const glm::vec4&` (confirmed in the §25 API table). The README §14 usage examples always pass a `glm::vec4` — only §34 shows the wrong signature.

The README was edited after the original analysis but in a way that retained (and arguably deepened) the inaccuracy, now also attributing non-existent runtime behavior to `std::nullopt`.

**Fix:** Update §34 to show the correct mandatory signature:
```cpp
RenderPass(const OrthographicCamera& camera, const glm::vec4& viewportBounds);
```
Remove the `std::nullopt` behavior description entirely.

---

#### README-B: DataPlayer and DataRecorder header docstrings still claim wrong versions

**Section:** `DataRecorder.h` and `DataPlayer.h` header docstrings  
**Severity:** Minor confusion (README itself is now accurate; only the header docs are wrong)

The README §26 and §38 both correctly document the v1 format. However the header files have not been corrected:
- `DataRecorder.h` `Flush()` docstring (line ~166): still says **"v3 binary format"** — but the code writes `version = 1u` and the file-level block comment says "BINARY FILE FORMAT v1".
- `DataPlayer.h` `Load()` docstring (line ~67): still says **"loads scene.bin (v2/v3) if present"** — but only v1 is parsed in `LoadBinaryFile`.

Developers reading the header files (e.g. in an IDE tooltip) see wrong version numbers even though the README is correct.

**Fix:** Update `DataRecorder.h` `Flush()` docstring: change "v3 binary format" → "v1 binary format". Update `DataPlayer.h` `Load()` docstring: change "scene.bin (v2/v3)" → "scene.bin (v1)".

---

#### README-C: DataPlayer fallback behavior is described but not implemented

**Section:** `DataPlayer.h` header doc (also referenced in §26 README)  
**Severity:** Moderate (broken behavior claim)

The `DataPlayer.h` Load() docstring says:
> "For a directory: loads scene.bin (v2/v3) if present; otherwise loads all individual .bin files (v1)."

This fallback is not implemented. If a directory contains only per-entity `.bin` files and no `scene.bin`, `Load()` silently returns false. The README §26 does not explicitly claim this fallback, but users reading the header will expect it.

**Fix:** Implement the fallback (see P1-E above) or remove the claim from the header docstring.

---

#### README-D: `DrawInstancedQuads` texture binding requirement not documented

**Section:** §13 Instanced Rendering  
**Severity:** Minor (missing callout)

`DrawInstancedQuads` requires callers to manually bind textures (via `texture->Bind(slot)`) before the call when `TexIndex > 0`. Only slot 0 (white texture) is guaranteed to be bound automatically. The README shows the `InstanceQuadData` struct and the call signature but does not call out this manual binding requirement. A developer setting `TexIndex = 1` without understanding the setup will get black quads with no error.

**Fix:** Add a note in §13 that all texture slots referenced by `TexIndex > 0` must be bound by the caller before `DrawInstancedQuads`.

---

#### README-E: §8 leads with the legacy `BeginScene`/`EndScene` API

**Section:** §8 2D Rendering API  
**Severity:** Minor (best-practice gap)

The minimal skeleton and all introductory examples in §8 use `Renderer2D::BeginScene` / `EndScene`. These are thin wrappers that work for single-camera, full-window scenarios. The modern, more capable `RenderPass` RAII pattern (§14) is only introduced later. For new users the entry point should be `RenderPass`.

**Fix:** Add a callout in §8 noting that `BeginScene`/`EndScene` exist for simplicity and that `RenderPass` is preferred for any project that needs multi-camera or viewport control.

---

## 6. Possible Additions

These are features not currently present that would be high-value additions to the engine or editor tooling.

> **Note:** §6.9 (Multi-Entity Overlay Charts) and the underlying 20-agent simulation infrastructure are now **implemented** in `TemplateTelemetryLayer` and `AgentSystem`. The `TelemetryPanel` entity selector and ring buffer architecture are in place for single-entity display; the overlay chart extension (plotting the same channel from multiple entities simultaneously) remains a potential addition on top of the existing UI.

---

### 5.1 Text Rendering

**Priority:** High — currently impossible to render any text in the game world (only ImGui text works in the editor).

A text renderer using SDF (signed distance field) glyphs would integrate cleanly into the existing pipeline. SDF text renders at any scale without blurring and uses a single channel texture. Implementation path:
- Integrate `stb_truetype` (already a dependency in the STB family) or FreeType for glyph atlas generation.
- Produce an atlas texture + glyph UV map on first use.
- Extend `Renderer2D` with `DrawText(const std::string& text, position, scale, color, font)` that maps each glyph to a `DrawQuad` call with the atlas SubTexture.
- Add a `Text.glsl` SDF fragment shader that applies smoothstep on the distance field for crisp edges.

---

### 5.2 Sprite Animation System

**Priority:** High — the atlas infrastructure (`SubTexture2D`) exists but there is no playback system.

A component-based animation system would use the existing SubTexture2D and scene pipeline:
- `AnimationClip`: array of SubTexture2D frames + frame duration.
- `AnimatorComponent`: active clip, current frame index, elapsed time, loop flag.
- `AnimationSystem` (serial, implements `System`): advances frame indices each tick and updates `SpriteRendererComponent::Texture`.

This requires no GPU changes — it purely schedules SubTexture2D swaps per frame.

---

### 5.3 Hot-Reload Shader Watching

**Priority:** Medium — currently shaders must be recompiled at engine init.

A file-watcher thread (using `ReadDirectoryChangesW` on Windows) that detects changes to `assets/shaders/*.glsl` and triggers `Shader::Recompile()` at the start of the next frame would eliminate the edit → full rebuild → relaunch cycle during shader development. The engine already has the shader compilation path — only the watcher and hot-swap logic is new.

---

### 5.4 Particle System

**Priority:** Medium — naturally fits the instanced rendering pipeline.

A CPU-side particle system backed by `DrawInstancedQuads`:
- `ParticleEmitter` component: spawn rate, lifetime, initial velocity, color gradient over lifetime, size-over-lifetime.
- `ParticleSystem` (parallel-friendly): updates alive particles each fixed tick, packs surviving particles into a `std::vector<Renderer2D::InstanceQuadData>`, and submits via `DrawInstancedQuads` in the render pass.

With 20,000 instances per call and chunked streaming, this could handle large spark/smoke/confetti effects.

---

### 5.5 Asset Cache / Texture Library

**Priority:** Medium — currently `Texture2D::Create("path")` always allocates a new GPU texture regardless of whether the same path was already loaded.

A central `AssetCache` singleton keyed by canonical file path would return the existing `Ref<Texture>` on repeat requests. This prevents duplicated VRAM usage and repeated disk I/O. The `Ref<T>` shared ownership model makes this straightforward — the cache holds one `Ref`, callers hold additional `Ref`s, and the GPU texture is freed when the last holder releases.

---

### 5.6 Multi-Attachment Framebuffer

**Priority:** Medium — required for post-processing, entity ID GPU picking, and deferred rendering.

The current `FrameBuffer` supports exactly one color attachment. Extending `FramebufferSpecification` with an `std::vector<AttachmentSpec>` (color formats + depth/stencil) and updating `OpenGLFrameBuffer` to call `glDrawBuffers` would unlock:
- **Entity ID picking**: render entity IDs to a second color attachment; read pixel under cursor on click, replacing the CPU AABB loop in `EntityPicker`.
- **Post-processing**: ping-pong between two FBOs for bloom, blur, edge detection.
- **Depth texture read-back**: access the rendered depth for lens flare occlusion or shadow receivers.

---

### 5.7 Audio System

**Priority:** Medium — there is currently no audio support whatsoever.

A thin audio layer using **miniaudio** (header-only, no dependency) would add sound effects and music:
- `AudioEngine::Init()` / `Shutdown()` in the application lifecycle.
- `Sound::Load("assets/sounds/explosion.wav")` → `Ref<Sound>`.
- `AudioEngine::Play(sound, volume, pitch)` for one-shots.
- `AudioEngine::PlayLooping(music)` / `Stop()` for background tracks.

miniaudio handles device enumeration, resampling, and mixing entirely in a header; integration cost is low.

---

### 5.8 Camera Easing and Entity Tracking

**Priority:** Low — quality-of-life for simulation projects.

Extend `OrthographicCameraController` with:
- `TrackEntity(Entity e)` — each update, lerp the camera position toward the entity's transform.
- `LerpToPosition(glm::vec2 target, float speed)` — smooth camera transitions between POIs.
- `ShakeCamera(float intensity, float duration)` — time-based positional noise for impact effects.

These are purely cosmetic but significantly improve the feel of simulations and game prototypes.

---

### 5.9 Telemetry: Multi-Entity Overlay Charts

**Priority:** Low — for multi-agent debugging.

Currently `TelemetryPanel` displays one entity's channels at a time. An overlay mode would let the user select multiple entities and plot the same channel from all selected entities on a single chart (different colors, one line per entity). This would make comparing agent behaviors across a population much faster than switching the selector back and forth.

---

### 5.10 Telemetry: Discrete Event Markers

**Priority:** Low — complements the continuous float-channel data.

Add a second recording path in `DataRecorder` for discrete events:
```cpp
void RecordEvent(uint32_t id, const std::string& label);
```
Events are stored as `(timestamp, label)` pairs per entity and are rendered as vertical marker lines on the ImPlot charts in `TelemetryPanel`. This makes state transitions (agent spawned, target acquired, energy depleted) visible on the timeline alongside continuous data.

---

*End of analysis report.*
