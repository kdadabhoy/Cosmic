# Cosmic Engine — Part 2: Engine Internals

> **Audience:** Engine contributors and advanced client developers who need to understand how Cosmic works under the hood. Assumes familiarity with [Part 1 — Client Developer Guide](Part1_ClientGuide.md).

---

## Table of Contents

- [§25 Source File Map](#25-source-file-map)
- [§26 Hot-Reloadable DLL Architecture](#26-hot-reloadable-dll-architecture)
- [§27 Top-Down Time Propagation Waterfall](#27-top-down-time-propagation-waterfall)
- [§28 The Double-Tick Trap](#28-the-double-tick-trap)
- [§29 The OpenGL Graphics Pipeline](#29-the-opengl-graphics-pipeline)
- [§30 Hardware Abstraction Architecture](#30-hardware-abstraction-architecture)
- [§31 Batch Rendering Deep Dive](#31-batch-rendering-deep-dive)
- [§32 Shader Preprocessing System](#32-shader-preprocessing-system)
- [§33 RenderPass Stack — Implementation Details](#33-renderpass-stack--implementation-details)
- [§34 Parallel Pipeline Architecture](#34-parallel-pipeline-architecture)
- [§35 Build System](#35-build-system)
- [§36 Event System — Implementation Details](#36-event-system--implementation-details)

---

## §25 Source File Map

```
Cosmic/
├── src/
│   ├── Cosmic.h                        Single-include public API
│   ├── core/
│   │   ├── Application.h / .cpp        Main loop, DLL loading, time system
│   │   ├── Core.h                      Ref<T>, Scope<T>, BIT(), macros
│   │   ├── Layer.h                     Layer base class + timeline API
│   │   ├── LayerStack.h / .cpp         Ordered layer container
│   │   ├── Log.h / .cpp                spdlog wrappers, CS_* macros
│   │   ├── Input.h / .cpp              Platform-agnostic polling
│   │   └── Window.h                    Abstract window + fullscreen API
│   ├── events/
│   │   ├── Event.h                     EventType, EventCategory, EventDispatcher
│   │   ├── ApplicationEvent.h          WindowResize, WindowClose
│   │   ├── KeyEvent.h                  KeyPressed, KeyReleased, KeyTyped
│   │   └── MouseEvent.h                MouseMoved, MouseScrolled, MouseButton*
│   ├── renderer/
│   │   ├── Renderer2D.h / .cpp         Batch renderer, instanced draw calls
│   │   ├── RenderCommand.h             Static forwarder → RendererAPI
│   │   ├── RendererAPI.h               Abstract GPU commands
│   │   └── RenderPass.h                RAII camera/viewport scope
│   ├── graphics/
│   │   ├── Buffer.h / .cpp             VertexBuffer, IndexBuffer, BufferLayout
│   │   ├── VertexArray.h / .cpp        VAO abstraction
│   │   ├── Shader.h / .cpp             Abstract shader + factory
│   │   ├── Texture.h / .cpp            2D texture + factory
│   │   ├── SubTexture2D.h / .cpp       Atlas sub-region helper
│   │   ├── Material.h / .cpp           Typed uniform bag
│   │   └── FrameBuffer.h / .cpp        Offscreen FBO
│   ├── scene/
│   │   ├── Scene.h / .cpp              EnTT registry, system dispatch
│   │   ├── Entity.h                    Lightweight EnTT handle
│   │   ├── Components.h                TransformComponent, SpriteRendererComponent, TagComponent
│   │   ├── System.h                    Serial system base
│   │   └── ComponentRegistry.h         CS_REGISTER_COMPONENT macro
│   ├── jobs/
│   │   ├── JobSystem.h / .cpp          Thread pool singleton
│   │   ├── ParallelSystem.h            4-pass parallel system base
│   │   ├── SystemQuery.h               ReadWriteQuery<T>, ReadOnlyQuery<T>
│   │   ├── ParallelFor.h               6 free-function parallel helpers
│   │   ├── DoubleBuffer.h              Read/write double-buffer
│   │   └── ComponentArray.h            ComponentArray<T>, FlatComponentArray<T>
│   ├── camera/
│   │   ├── OrthographicCamera.h / .cpp Low-level camera matrices
│   │   └── OrthographicCameraController.h  WASD + zoom controller
│   ├── layers/
│   │   ├── ImGuiLayer.h / .cpp         ImGui/ImPlot initialization
│   │   └── WorkspaceLayer.h / .cpp     Docked panel shell, DLL bridge
│   ├── platform/
│   │   └── OpenGL/
│   │       ├── OpenGLRendererAPI.h/.cpp    glDraw* calls
│   │       ├── OpenGLShader.h/.cpp         GLSL compilation, uniform cache
│   │       ├── OpenGLBuffer.h/.cpp         VBO/IBO
│   │       ├── OpenGLVertexArray.h/.cpp    VAO + attrib pointers
│   │       ├── OpenGLTexture.h/.cpp        stb_image, GL texture objects
│   │       └── OpenGLFrameBuffer.h/.cpp    FBO + color attachment
│   ├── serial/
│   │   └── SerialPort.h / .cpp         Windows HANDLE-based COM port
│   └── utils/
│       └── FileSystem.h / .cpp         VFS: engine:// and project://
└── templates/
    └── ExampleProject/                 Canonical client template
        └── src/
            ├── TemplateProject.h/.cpp  Root manager layer
            ├── TemplateRenderLayer.*   Shader/material demo layer
            ├── TemplateSpriteLayer.*   ECS sprite demo layer
            ├── TemplateRenderBenchmarkLayer.*  Instanced rendering benchmark
            ├── BallPhysicsSystem.h     ParallelSystem example
            └── Components.h           PhysicsBody component definition
```

---

## §26 Hot-Reloadable DLL Architecture

### Overview

Cosmic separates the engine host (the `.exe`) from client workspaces (`.dll` files). The engine compiles once; client projects are rebuilt and hot-reloaded without restarting the host process.

### Required DLL Exports

Every client DLL must export exactly two C-linkage functions:

```cpp
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::TemplateProject();
    }
}
```

`HostContext` is defined in `Cosmic.h`:

```cpp
struct HostContext
{
    ImGuiContext*  ImGuiCtx;
    ImPlotContext* ImPlotCtx;
};
```

Both ImGui and ImPlot store global state in a per-module pointer. Because the `.dll` is a separate module from the `.exe`, each has its own default context pointer — initially null in the DLL. `InitializePluginContexts` copies the host's live context pointer into the DLL's module-local global, making all subsequent `ImGui::*` calls in the DLL write to the same draw list the host will render.

### Load Sequence (`Application::LoadProjectDLL`)

```
LoadLibraryA(dllPath)
  └─ GetProcAddress("InitializePluginContexts")  → call immediately
  └─ GetProcAddress("CreatePluginLayer")          → call to get Layer*
       └─ WorkspaceLayer::SetViewportLayer(layer)
            └─ layer->OnAttach()                  ← GPU resources created here
```

All steps occur inside the **Safe Zone** — the end of a frame loop iteration after the LayerStack iterator has been destroyed and before the next iteration begins. This ensures no iterator invalidation occurs.

### Unload Sequence (`Application::UnloadProjectDLL`)

```
WorkspaceLayer::ClearViewportLayer()
  └─ layer->OnDetach()                            ← GPU resources freed here
Window::ClearFullscreenHotkeyOverride()            ← lambda lifetime ends
delete m_ActivePluginLayer                         ← destructor runs in DLL code
FreeLibrary(hModule)                               ← DLL code unmapped after delete
```

**Critical ordering:** `delete` must happen **before** `FreeLibrary`. The destructor body lives in DLL code. Freeing the library first would unmap that code, causing an access violation when the destructor executes.

### Component Type ID Stability

EnTT assigns type IDs via a static counter. If the engine and a DLL see different counter values for the same component type (because each module has its own static storage), registry lookups silently corrupt.

`CS_REGISTER_COMPONENT(T)` forces a deterministic hash:

```cpp
#define CS_REGISTER_COMPONENT(T) \
    template<> struct entt::type_hash<T> { \
        static constexpr entt::id_type value() noexcept { \
            return entt::hashed_string::value(#T); \
        } \
    };
```

Every component used across the DLL boundary must appear in a `CS_REGISTER_COMPONENT` call **in a header shared by both the engine and the client**.

---

## §27 Top-Down Time Propagation Waterfall

The engine applies a two-level time system. Understanding the exact multiplication order is essential for building correct per-layer timelines.

### Hardware Timer

`Application::Run()` uses `glfwGetTime()` (a monotonically increasing double, seconds since GLFW init):

```cpp
double time         = glfwGetTime();
float  rawTimestep  = static_cast<float>(time - m_LastFrameTime);
m_LastFrameTime     = time;
```

`rawTimestep` is always a positive wall-clock delta — unaffected by `TimeScale`.

### Global Scaling

```cpp
float scaledTs = rawTimestep * m_TimeScale;   // m_TimeScale set by SetTimeScale()
m_AbsoluteTime += scaledTs;
```

`scaledTs` is what flows down to layers. `m_AbsoluteTime` accumulates scaled time (so it pauses when `TimeScale == 0` and rewinds when `TimeScale < 0`).

### Fixed Accumulator

```cpp
m_FixedAccumulator += scaledTs;
constexpr float fixedDt = 1.0f / 60.0f;
constexpr float maxAccumulation = 0.25f;       // spiral-of-death clamp

if (m_FixedAccumulator > maxAccumulation)
    m_FixedAccumulator = maxAccumulation;

while (m_FixedAccumulator >= fixedDt)
{
    // OnFixedUpdate fired for all layers
    m_FixedAccumulator -= fixedDt;
}
```

The 0.25 s clamp means a frame that stalls for 500 ms will still only dispatch at most 15 fixed steps (0.25 / 0.016̄ ≈ 15), keeping physics stable at the cost of simulated time dilation.

### Layer Dispatch

Variable-rate:
```
Application::Run()
  → scaledTs  →  Layer::OnUpdate(scaledTs)
                   → Layer::UpdateLayerTime(scaledTs)
                       m_LocalTime += scaledTs * m_LocalTimeScale
```

Fixed-rate:
```
Application::Run()
  → fixedDt (constant 1/60)  →  Layer::OnFixedUpdate(fixedDt)
```

`fixedDt` is **already scaled** — it was accumulated from `scaledTs`. A client layer does not need to apply `GetTimeScale()` again inside `OnFixedUpdate`.

### Full Waterfall Diagram

```
Wall clock (glfwGetTime)
   │
   ▼
rawTimestep
   │  × m_TimeScale  (Application global)
   ▼
scaledTs  ──────────────────────────────────────► OnUpdate(scaledTs)
   │                                                  │  × m_LocalTimeScale  (per-layer)
   │  (accumulated)                                   ▼
   ▼                                              m_LocalTime
fixedDt (constant 1/60, fire when ready)
   │
   ▼
OnFixedUpdate(fixedDt)
```

---

## §28 The Double-Tick Trap

### What It Is

If a child layer is both **pushed onto the engine `LayerStack`** and **driven manually** by a parent layer, every hook fires twice per frame. This produces doubled physics integration, doubled renderer draw calls, and doubled ImGui widget registration (which crashes ImGui with duplicate IDs).

### How It Happens

```cpp
// WRONG — child layer pushed onto LayerStack AND driven by parent
void TemplateProject::OnAttach()
{
    auto child = std::make_shared<TemplateRenderLayer>(m_SharedMaterial);
    Cosmic::Application::Get().PushLayer(child.get());  // ← engine will call OnUpdate
    m_Modes.push_back(child);                           // ← OnUpdate called again by parent
}
```

### The Correct Pattern

Child layers must **never** be pushed onto the engine `LayerStack`. The root manager layer owns them and drives them exclusively:

```cpp
// CORRECT — child layers are invisible to the engine
void TemplateProject::OnAttach()
{
    m_Modes.push_back(std::make_shared<TemplateRenderLayer>(m_SharedMaterial));
    // NOT pushed onto the engine LayerStack

    for (auto& mode : m_Modes)
        mode->OnAttach();   // ← manual GPU resource init
}

void TemplateProject::OnUpdate(float ts)
{
    m_Modes[m_ActiveModeIndex]->OnUpdate(ts);   // ← manual drive
}
```

The engine sees exactly one `Layer` object (`TemplateProject`). All child layers are an implementation detail of that object.

### Application-Event Broadcast Exception

Input events route only to the active child layer. Application events (resize, etc.) must broadcast to **all** child layers so inactive cameras don't accumulate stale projection matrices:

```cpp
void TemplateProject::OnEvent(Cosmic::Event& e)
{
    if (e.IsInCategory(Cosmic::EventCategoryApplication))
    {
        for (auto& mode : m_Modes)
            mode->OnEvent(e);   // all modes receive resize
        return;
    }

    // Input — active mode only
    if (e.Handled) return;
    m_Modes[m_ActiveModeIndex]->OnEvent(e);
}
```

---

## §29 The OpenGL Graphics Pipeline

### Command Flow

```
Client code
  └─ Renderer2D::DrawQuad(...)         [high-level batch API]
       └─ RenderCommand::DrawIndexed(vertexArray, count)
            └─ s_RendererAPI->DrawIndexed(vertexArray, count)   [virtual dispatch]
                 └─ OpenGLRendererAPI::DrawIndexed(...)
                      └─ glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr)
```

`RenderCommand` is a pure static class — all methods forward to a single `RendererAPI*` stored as a static member. The abstraction allows a future DirectX backend to be swapped in without touching any call site above `RenderCommand`.

### `RendererAPI` Virtual Interface

```cpp
class RendererAPI
{
public:
    enum class API { None = 0, OpenGL = 1, DirectX = 2 };

    virtual void Init() = 0;
    virtual void SetClearColor(const glm::vec4&) = 0;
    virtual void Clear() = 0;
    virtual void DrawIndexed(Ref<VertexArray>, uint32_t indexCount = 0) = 0;
    virtual void DrawLines(Ref<VertexArray>, uint32_t vertexCount) = 0;
    virtual void DrawIndexedInstanced(Ref<VertexArray>, uint32_t indexCount, uint32_t instanceCount) = 0;
    virtual void SetLineWidth(float) = 0;

    static API GetAPI();
};
```

`DrawIndexedInstanced` was added for the instanced rendering path (`DrawInstancedQuads` / `DrawInstancedCircles`).

### Frame Lifecycle

```
Application::Run() each frame:
  ├── RenderCommand::SetClearColor(...)
  ├── RenderCommand::Clear()
  ├── FrameBuffer::Bind()              ← offscreen FBO
  │
  ├── [LayerStack OnUpdate loop]
  │     └── Layer::OnUpdate(ts)
  │           └── RenderPass rp(camera, bounds)   ← PushRenderPass
  │                 └── Renderer2D::BeginScene(...)
  │                       └── ... draw calls ...
  │                 └── Renderer2D::EndScene()     ← flush batch
  │           └── [rp destructor]                 ← PopRenderPass
  │
  ├── FrameBuffer::Unbind()            ← back to default FBO
  ├── WorkspaceLayer renders FBO color attachment as ImGui image
  └── ImGui::Render() + SwapBuffers()
```

---

## §30 Hardware Abstraction Architecture

### Buffer Layout System

`BufferLayout` describes how vertex data is packed into a VBO:

```cpp
BufferLayout layout = {
    { ShaderDataType::Float3, "a_Position"                    },
    { ShaderDataType::Float4, "a_Color"                       },
    { ShaderDataType::Float2, "a_TexCoord"                    },
    { ShaderDataType::Float,  "a_TexIndex"                    },
    { ShaderDataType::Float,  "a_TilingFactor"                },
    // Instanced attributes set Instanced = true:
    { ShaderDataType::Float3, "a_InstancePosition", true      },
};
```

`BufferElement` carries `Name`, `Type`, `Size`, `Offset`, and an `Instanced` bool. When `Instanced == true`, `OpenGLVertexArray::AddVertexBuffer` calls `glVertexAttribDivisor(location, 1)`, enabling hardware instancing — the GPU advances that attribute once per instance rather than once per vertex.

`BufferLayout::CalculateOffsetsAndStride()` is called in the `BufferLayout` constructor and walks the element list to set per-element byte offsets and the total stride.

### `ShaderDataType` → GL Type Mapping

| `ShaderDataType` | GL type | Component count |
|---|---|---|
| `Float` | `GL_FLOAT` | 1 |
| `Float2` | `GL_FLOAT` | 2 |
| `Float3` | `GL_FLOAT` | 3 |
| `Float4` | `GL_FLOAT` | 4 |
| `Mat3` | `GL_FLOAT` | 3×3 = 9 |
| `Mat4` | `GL_FLOAT` | 4×4 = 16 |
| `Int` | `GL_INT` | 1 |
| `Bool` | `GL_BOOL` | 1 |

### `VertexBuffer::SetData`

Allows dynamic update of buffer contents without reallocating the GPU object:

```cpp
vertexBuffer->SetData(myVertexData, sizeof(myVertexData));
// Internally: glBufferSubData(GL_ARRAY_BUFFER, 0, size, data)
```

The batch renderer uses this each frame to upload the CPU-side vertex staging buffer to the GPU.

### Object Hierarchy

```
VertexArray  (owns)
  ├── vector<Ref<VertexBuffer>>   (one or more VBOs)
  └── Ref<IndexBuffer>            (one IBO)
```

The VAO stores the attrib-pointer configuration. Binding a `VertexArray` is sufficient to restore the full pipeline state for a draw call.

---

## §31 Batch Rendering Deep Dive

### Purpose

OpenGL draw calls carry significant CPU-side overhead (driver state validation, command encoding). The batch renderer accumulates many logical draw requests (quads, circles, lines) into a single VBO upload and a single `glDrawElements` call per flush.

### Quad Batch Internals

Each quad occupies 4 vertices and 6 indices (two triangles). The batch maintains:

- A CPU-side vertex staging array sized for `MaxQuads * 4` vertices
- A pre-computed index buffer for `MaxQuads * 6` indices (filled once at init, never changed)
- A flush counter tracking how many quads have been added this batch

`Renderer2D::DrawQuad` appends 4 `QuadVertex` structs to the staging array. When the staging array is full, `Renderer2D::FlushAndReset` uploads it via `VertexBuffer::SetData`, issues `RenderCommand::DrawIndexed`, and resets the pointer to the start of the staging buffer.

At `EndScene()`, any remaining unflushed geometry is flushed with a final `DrawIndexed` call.

### Texture Slot Management

OpenGL shaders access textures via integer indices (`a_TexIndex`). The batch pre-binds up to `MaxTextureSlots` textures (typically 32, queried from `GL_MAX_TEXTURE_IMAGE_UNITS`). When `DrawQuad` is called with a texture not currently bound, it:

1. Checks if the texture is already in the slot array — reuses the slot index if found.
2. Otherwise appends the texture to the slot array.
3. If the slot array is full, flushes the current batch first (triggering a draw call), then binds the new texture in slot 0 of the fresh batch.

Slot 0 is always reserved for a 1×1 white pixel texture used for untextured quads and solid-color fills.

### `Statistics` Struct

```cpp
struct Statistics
{
    uint32_t DrawCalls = 0;
    uint32_t QuadCount = 0;
    uint32_t LineCount = 0;

    uint32_t GetTotalVertexCount() const { return QuadCount * 4 + LineCount * 2; }
    uint32_t GetTotalIndexCount()  const { return QuadCount * 6; }
};
```

`ResetStats()` zeros all fields. Call it once per frame (typically at the start of `OnUpdate`) so the displayed counts reflect the current frame only.

### Instanced Rendering Path

For `N` identical-geometry objects (e.g., 100,000 physics balls), the standard quad batch still submits `ceil(N / MaxQuads)` draw calls. The instanced path submits **one** draw call regardless of N:

```
DrawInstancedQuads(data, count)
  └─ upload instance VBO  (VertexBuffer::SetData)
  └─ RenderCommand::DrawIndexedInstanced(vao, 6, count)
       └─ glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, count)
```

The vertex shader reads per-instance attributes (Position, Scale, Color, etc.) from the instance VBO via `glVertexAttribDivisor(location, 1)`. The six static quad indices are shared — the GPU replays them `count` times, incrementing the instance attribute pointer once per replay.

`InstanceQuadData` layout (60 bytes / 15 floats):

| Field | Type | Bytes |
|---|---|---|
| Position | vec3 | 12 |
| Scale | vec2 | 8 |
| Color | vec4 | 16 |
| TexCoordOffset | vec2 | 8 |
| TexCoordScale | vec2 | 8 |
| TexIndex | float | 4 |
| TilingFactor | float | 4 |

`InstanceCircleData` layout:

| Field | Type | Bytes |
|---|---|---|
| Position | vec3 | 12 |
| Scale | vec2 | 8 |
| Color | vec4 | 16 |
| Thickness | float | 4 |
| Fade | float | 4 |

---

## §32 Shader Preprocessing System

### Three Input Formats

`OpenGLShader::PreProcess` recognizes three input formats and normalizes them to `{GLenum → source}` maps:

**Format 1 — Explicit split (`#type` directives)**
```glsl
#type vertex
#version 330 core
// vertex source ...

#type fragment
#version 330 core
// fragment source ...
```
The preprocessor splits on `#type` tokens. Both stages are compiled independently.

**Format 2 — Fragment-only**
```glsl
#version 330 core
// fragment source only (no #type directives present)
```
Detected when no `#type` token is found. A hardcoded passthrough vertex shader is injected automatically.

**Format 3 — Shadertoy-style**
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) { ... }
```
Detected by the presence of `mainImage`. The preprocessor wraps it with a `main()` stub and a Shadertoy-compatible uniform block, then injects the passthrough vertex shader.

### Auto-Injected Uniforms

After preprocessing and before compilation, the system inspects the fragment source for these uniforms and injects declarations if they are absent:

| Uniform | Type | Injected value source |
|---|---|---|
| `u_ViewProjection` | `mat4` | `RenderPass` camera matrix |
| `u_Time` | `float` | Active layer's `GetLocalTime()` |
| `u_ViewportSize` | `vec2` | Framebuffer width/height |

This means a minimal fragment shader can use `u_Time` without declaring it — the preprocessor will add the `uniform float u_Time;` line before the GLSL compiler sees the source.

### Compilation Pipeline (`OpenGLShader::Compile`)

```
PreProcess(source)
  └─ returns map<GLenum, string>

For each {GL_VERTEX_SHADER, source} and {GL_FRAGMENT_SHADER, source}:
  glCreateShader(type)
  glShaderSource(...)
  glCompileShader(...)
  glGetShaderiv(GL_COMPILE_STATUS)
    → on failure: glGetShaderInfoLog → CS_CORE_ERROR + glDeleteShader

glCreateProgram()
  glAttachShader(program, vertexShader)
  glAttachShader(program, fragmentShader)
  glLinkProgram(program)
  glGetProgramiv(GL_LINK_STATUS)
    → on failure: glGetProgramInfoLog → CS_CORE_ERROR + glDeleteProgram

glDetachShader(program, each)
glDeleteShader(each)
```

`m_UniformLocationCache` (an `unordered_map<string, GLint>`) caches `glGetUniformLocation` results. The first `SetFloat("u_Time", ...)` call queries GL and stores the location; subsequent calls use the cached value directly.

### Debug Helper

`DumpPreprocessedShader()` writes the post-preprocessed, pre-compilation GLSL to the log at `CS_CORE_TRACE` level. Useful for debugging auto-injected uniforms or Shadertoy conversion.

---

## §33 RenderPass Stack — Implementation Details

### RAII Contract

`RenderPass` is a non-copyable, non-movable RAII guard:

```cpp
class RenderPass
{
public:
    RenderPass(const OrthographicCamera& camera,
               std::optional<glm::vec4> viewportBounds = std::nullopt);
    ~RenderPass();

    RenderPass(const RenderPass&)            = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&&)                 = delete;
    RenderPass& operator=(RenderPass&&)      = delete;
};
```

Constructor calls `Renderer2D::PushRenderPass(camera.GetViewProjectionMatrix(), viewportBounds)`.
Destructor calls `Renderer2D::PopRenderPass()`.

Because copy and move are both deleted, `RenderPass` objects cannot be transferred. They must be stack-allocated and will always be destroyed in LIFO order — maintaining the stack invariant.

### Stack Semantics

`Renderer2D` maintains an internal stack of `{viewProjectionMatrix, viewportBounds}` pairs. `PushRenderPass` pushes; `PopRenderPass` pops and restores the previous camera state.

This allows nested render passes (e.g., render a scene to a texture inside a larger scene pass) without manual state save/restore.

### `viewportBounds`

When `viewportBounds` is supplied (`glm::vec4{x, y, width, height}`), the renderer calls `glViewport` on push and restores the previous viewport on pop. When it is `std::nullopt`, the viewport is left unchanged — useful when the caller has already set the viewport explicitly.

---

## §34 Parallel Pipeline Architecture

### Thread Pool — `JobSystem`

`JobSystem` is a singleton initialized before any other engine subsystem in `Application::Initialize()`:

```cpp
JobSystem::Get().Initialize();
```

`Initialize()` queries `GetSystemInfo().dwNumberOfProcessors` and spawns `coreCount - 1` worker threads (reserving one core for the main thread). Workers block on a condition variable (`m_WorkAvailable`) until a job is submitted.

```cpp
using Job = std::function<void()>;

void Submit(Job job);   // thread-safe: locks m_QueueMutex, notifies m_WorkAvailable
void WaitIdle();        // blocks caller on m_AllIdle until m_ActiveJobs == 0
```

`WaitIdle()` is the synchronization point between the parallel execute phase and the merge phase. The `m_ActiveJobs` counter is an `atomic<uint32_t>` incremented on submit and decremented (with `m_AllIdle.notify_all()`) when a worker finishes a job and the queue is empty.

`Shutdown()` is called first in `Application::Shutdown()`. It sets `m_Stopping = true`, notifies all workers, and joins every thread. Shutdown before any other subsystem teardown prevents jobs from accessing freed resources.

### `ParallelSystem` 4-Pass Pipeline

`Scene` separates serial and parallel systems internally. `AddSystem<T>()` uses `dynamic_cast<ParallelSystem*>` to detect parallel systems and adds them to a separate `m_ParallelSystems` vector (non-owning) alongside the owning `m_Systems` vector.

Each fixed-step frame the `Scene` executes four passes in order:

```
Pass A — Serial systems:
  for each System in m_Systems (serial only):
    system->OnFixedUpdate(scene, fixedDt)

Pass B — Parallel prepare (main thread):
  for each ParallelSystem:
    system->StageQueries(scene)     ← snapshots all registered queries
    system->OnFixedPrepare(scene, fixedDt)

Pass C — Parallel execute (worker threads):
  for each ParallelSystem:
    system->OnFixedParallelExecute(scene, fixedDt)   ← submits jobs, returns immediately
  JobSystem::Get().WaitIdle()       ← single barrier for ALL systems

Pass D — Merge (main thread):
  for each ParallelSystem:
    system->OnFixedMerge(scene, fixedDt)
    system->CommitQueries(scene)    ← writes staged data back to registry
```

The single `WaitIdle()` after all systems have submitted means systems can overlap their parallel work — system B's workers run while system A's workers are still running, maximizing thread utilization.

### `SystemQuery<T>` — Staged Snapshot Protocol

`ReadWriteQuery<T>` and `ReadOnlyQuery<T>` implement `ISystemQuery`. Registering a query:

```cpp
Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };
// passes `this` (the ParallelSystem*) to RegisterQuery(&m_Bodies)
```

`RegisterQuery` appends the query pointer to `m_Queries`. The engine calls `StageQueries` → each query's `Stage(Scene&)` before Pass B, and `CommitQueries` → each query's `Commit(Scene&)` after Pass D.

**`ReadWriteQuery<T>::Stage(Scene&)`**
1. Iterates the registry view for `T`.
2. Copies component values and entity handles into internal `std::vector<T>` and `std::vector<entt::entity>` arrays.
3. Sets a dirty flag.

**`ReadWriteQuery<T>::Commit(Scene&)`**
1. Iterates the snapshot arrays.
2. For each entity, if still valid in the registry, patches the registry component with the staged value.

**`ReadOnlyQuery<T>::Commit`** — no-op. The snapshot is never written back.

### `ForEachAsync` Internals

```cpp
m_Bodies.ForEachAsync([](PhysicsBody& body) { ... }, /*minChunkSize=*/32);
```

`ForEachAsync` divides `Count()` elements across worker count, with chunks of at least `minChunkSize`. For each chunk, one `Job` lambda is submitted to `JobSystem::Submit`. The lambda captures a raw pointer range into the staging vector (safe because the vector is stable for the lifetime of the parallel execute phase).

Serial fallback: when `JobSystem::GetWorkerCount() <= 1`, `ForEachAsync` runs the lambda synchronously inline rather than submitting to the (empty) pool.

### `DoubleBuffer<T>` — Inter-Entity Parallel Safety

When a system needs to read one entity's component while writing another's — a pattern that creates data races with a single buffer — `DoubleBuffer<T>` provides read/write separation:

```cpp
DoubleBuffer<glm::vec2> velocities;
velocities.Resize(entityCount);

// Each tick, main thread:
velocities.CopyReadToWrite();   // seed write buffer from last frame's results
velocities.Swap();              // O(1): m_ReadIndex ^= 1u

// Workers read from GetReadBuffer() and write to GetWriteBuffer() — no overlap
```

`Swap()` is an XOR on a single index: `m_ReadIndex ^= 1u`. It is O(1) and does not move any data. It must be called on the main thread before workers are dispatched.

### `ComponentArray<T>` vs `FlatComponentArray<T>`

EnTT stores component data in paged arrays (default page size 1024). `ComponentArray<T>` gets a non-owning pointer to the **first page only**:

```cpp
auto arr = ComponentArray<PhysicsBody>::From(registry);
// arr.Data()  — pointer into registry's first storage page
// arr.Count() — count of entities on that page (≤ 1024 for small counts)
```

Safe for entity counts ≤ ~1024. Cheaper than `FlatComponentArray` (zero allocation, zero copy).

`FlatComponentArray<T>` copies **all pages** into a single contiguous buffer:

```cpp
auto flat = FlatComponentArray<PhysicsBody>::From(registry);
// flat.Data()  — owned contiguous copy
// flat.Count() — total entity count across all pages

// After parallel mutation:
flat.WriteBack(registry);   // patches registry component-by-component
```

Required when entity count exceeds one page, or when writing back mutated values.

### `ParallelFor` Free Functions

Six functions in `ParallelFor.h`:

**Synchronous (block until done):**
```cpp
ParallelFor(count, [](size_t i) { ... }, minChunkSize);
ParallelForEach<T>(span<T>, [](T& item) { ... }, minChunkSize);
ParallelForEachIndexed<T>(span<T>, [](T& item, size_t i) { ... }, minChunkSize);
```

**Async (submit and return immediately — caller must WaitIdle):**
```cpp
ParallelForAsync(count, [](size_t i) { ... }, minChunkSize);
ParallelForEachAsync<T>(span<T>, [](T& item) { ... }, minChunkSize);
ParallelForEachIndexedAsync<T>(span<T>, [](T& item, size_t i) { ... }, minChunkSize);
```

Chunk size: `max(minChunkSize, ceil(totalCount / workerCount))`. This ensures at most one job per worker thread, avoiding over-subscription on small datasets.

---

## §35 Build System

### `CMakeLists.txt` Structure

```cmake
cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

MSVC compiler flags applied globally:
```cmake
add_compile_options(/utf-8 /std:c++20)
```

Platform definitions:
```cmake
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
```

`WIN32_LEAN_AND_MEAN` suppresses rarely-used Windows headers. `NOMINMAX` prevents the Windows SDK from defining `min`/`max` macros that conflict with `std::min`/`std::max`.

### SDK Path Cache Variable

```cmake
set(COSMIC_SDK_DIR "" CACHE PATH "Path to Cosmic SDK installation")
```

Client project `CMakeLists.txt` files use `COSMIC_SDK_DIR` to locate engine headers and import libraries. If unset, the build system falls back to in-tree paths (for development builds where the engine and projects share a repository).

### Engine-Only Mode

```cmake
option(COSMIC_BUILD_ENGINE_ONLY "Build only the engine, skip all Projects/" OFF)
```

When `ON`, the automated project scanner (below) is skipped. Used in CI to validate the engine compiles without client code.

### Automated Project Scanner

```cmake
file(GLOB PROJECT_SUBDIRS RELATIVE "${CMAKE_SOURCE_DIR}" "Projects/*")

foreach(SUBDIR ${PROJECT_SUBDIRS})
    if(EXISTS "${CMAKE_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt")
        add_subdirectory(${SUBDIR})
    endif()
endforeach()
```

Any directory placed under `Projects/` with a `CMakeLists.txt` is automatically included in the build. No manual registration in the root `CMakeLists.txt` is required — dropping a new project folder into `Projects/` is sufficient.

### Client Project `CMakeLists.txt` Template

A minimal client project CMake file:

```cmake
project(MyProject LANGUAGES CXX)

add_library(MyProject SHARED
    src/MyProject.cpp
    src/MyRenderLayer.cpp
)

target_include_directories(MyProject PRIVATE
    ${COSMIC_SDK_DIR}/include
    src/
)

target_link_libraries(MyProject PRIVATE
    CosmicEngine
    opengl32
)

set_target_properties(MyProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Projects/MyProject"
)
```

The `SHARED` keyword produces a `.dll`. The output directory convention places the DLL where the engine launcher expects to find it.

---

## §36 Event System — Implementation Details

### `Event` Base Class

```cpp
class Event
{
public:
    bool Handled = false;

    virtual EventType       GetEventType()   const = 0;
    virtual const char*     GetName()        const = 0;
    virtual int             GetCategoryFlags() const = 0;
    virtual std::string     ToString()       const { return GetName(); }

    bool IsInCategory(EventCategory category) const
    {
        return GetCategoryFlags() & category;
    }
};
```

`Handled` is a public mutable flag. An event handler sets `Handled = true` to stop propagation. Subsequent handlers in the dispatch chain check `e.Handled` and bail out early.

### `EventDispatcher` Template

```cpp
class EventDispatcher
{
public:
    explicit EventDispatcher(Event& event) : m_Event(event) {}

    template<typename T, typename F>
    bool Dispatch(const F& func)
    {
        if (m_Event.GetEventType() == T::GetStaticType())
        {
            m_Event.Handled |= func(static_cast<T&>(m_Event));
            return true;
        }
        return false;
    }

private:
    Event& m_Event;
};
```

Key details:
- `T::GetStaticType()` is a static method on every concrete event class (generated by macro) that returns the `EventType` enum value.
- `static_cast<T&>` is safe because `GetEventType()` has already confirmed the dynamic type.
- `m_Event.Handled |= func(...)` — the `|=` means a handler that returns `false` cannot un-handle an event already marked handled by a previous dispatch call on the same dispatcher.
- `Dispatch` returns `true` if the type matched (regardless of `func`'s return value), `false` if the type did not match. This return value is rarely used but allows the caller to distinguish "wrong type" from "handled/not handled."

### Event Propagation — `LayerStack` Order

Events enter `Application::OnEvent()` and are dispatched to the `LayerStack` in **reverse order** (overlays first, base layers last):

```cpp
void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>(CS_BIND_EVENT_FN(Application::OnWindowClose));
    dispatcher.Dispatch<WindowResizeEvent>(CS_BIND_EVENT_FN(Application::OnWindowResize));

    for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
    {
        if (e.Handled) break;
        (*it)->OnEvent(e);
    }
}
```

Overlays (pushed via `PushOverlay`) sit at the end of the `LayerStack` container and are therefore visited first during reverse iteration. This mirrors the rendering order inversion: overlays render on top visually, and intercept events first logically.

### `EventCategory` Flags

```cpp
enum EventCategory
{
    EventCategoryApplication = BIT(0),   // WindowClose, WindowResize, ...
    EventCategoryInput       = BIT(1),   // All key and mouse events
    EventCategoryKeyboard    = BIT(2),   // KeyPressed, KeyReleased, KeyTyped
    EventCategoryMouse       = BIT(3),   // MouseMoved, MouseScrolled
    EventCategoryMouseButton = BIT(4),   // MouseButtonPressed, MouseButtonReleased
};
```

`BIT(x)` expands to `(1 << (x))`. A single event can belong to multiple categories — `KeyPressedEvent` sets flags for `Input | Keyboard`. `IsInCategory` tests with bitwise AND.

The `TemplateProject::OnEvent` broadcast pattern uses `IsInCategory(EventCategoryApplication)` to route resize events to all child layers without inspecting specific event types.

### Concrete Event Macros

Every concrete event class uses two macros:

```cpp
#define EVENT_CLASS_TYPE(type) \
    static  EventType GetStaticType()           { return EventType::type; } \
    virtual EventType GetEventType()  const override { return GetStaticType(); } \
    virtual const char* GetName()     const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual int GetCategoryFlags() const override { return category; }
```

Example usage in `KeyPressedEvent`:

```cpp
class KeyPressedEvent : public Event
{
public:
    EVENT_CLASS_TYPE(KeyPressed)
    EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)
    // ...
};
```

This pattern provides `GetStaticType()` (for `EventDispatcher`'s type comparison), `GetEventType()` (virtual, for polymorphic dispatch), and `GetName()` (for logging/`ToString()`), all without virtual table overhead for the type comparison path.

---

*End of Part 2 — Engine Internals*

*See [Part 1 — Client Developer Guide](Part1_ClientGuide.md) for the full client-facing API reference.*
