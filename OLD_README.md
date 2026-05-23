# Cosmic Engine - Developer API & Useful Functions

## General Useful Information

### Application Run()

The `Application::Run()` function drives the root execution cycle of Cosmic. It controls window polling, hosts an asset rendering loop, and uses a multi-pass architecture to separate deterministic logic simulations from variable frame visual state passes.

```
   [Hardware Clock Step]
             │
             ▼
 ┌───────────────────────┐
 │  m_Window->Poll()     │
 └───────────┬───────────┘
             │ (Raw Timestep Calculation)
             ▼
   ┌───────────────────────┐
┌─►│   Fixed Step Loop     │◄─ [Accumulator Clamped at 0.25s]
│  │  OnFixedUpdate(1/60s) │    (Spiral-of-Death Protection)
│  └───────────┬───────────┘
└─── [While Accumulator >= dt]
               │
               ▼
┌───────────────────────┐
│   OnUpdate(scaled dt) │◄─── [Multiplied by m_TimeScale]
└───────────┬───────────┘
            │
            ▼
┌───────────────────────┐
│    ImGui Layout Pass  │
│ Begin() -> Render()   │
└───────────┬───────────┘
            │
            ▼
┌───────────────────────┐
│  Window->SwapBuffers()│
└───────────┬───────────┘
            │
            ▼
┌───────────────────────┐
│   THE SAFE ZONE       │◄─── [Zero-Iteration Stack Window]
│ Deferred Plugin Swap  │     (Safe Alloc/Delete Operations)
└───────────────────────┘

```

#### Loop Sequencing Mechanics

1. **Hardware Polling:** Every iteration executes `m_Window->PollEvents()` to grab platform-level hardware states.
2. **Deterministic Fixed Pass:** If `m_UseFixedTimestep` is active, the engine tracks physical time using an internal accumulation buffer.
   - **Spiral-of-Death Protection:** If a frame takes longer than `0.25` seconds (e.g., breakpoint debugging or extreme lag), the physics step time clamps to `0.25` to prevent the engine from freezing.
   - Layers update at a rigid rate ($60\text{ Hz}$ or $1/60\text{s}$) via `layer->OnFixedUpdate()`.
3. **Variable Update Pass:** System visual updates run via `layer->OnUpdate()`, using a frame-variable `Timestep` modified by `m_TimeScale` for debugging slowdowns or speedups.
4. **UI Render Pass:** The loop initiates the master user interface layout context via `m_ImGuiLayer->Begin()`, updates layer-level diagnostic UI structures through `layer->OnImGuiRender()`, flushes commands to the GPU, and calls `m_Window->SwapBuffers()`.
5. **The Safe Zone Execution Window:** Modifying layer allocations during active update iterations will break memory vector indexing. The bottom of the application loop features a dedicated **Safe Zone** where no iterators are active on `m_LayerStack`. This zone safely handles requests to unload project DLL assemblies, reset ImGui dockspace panels, destroy unmanaged layer allocations, and swap execution states between the engine launcher and custom project spaces.

---

### How Shaders are Processed (.glsl files)

Cosmic provides a single-file asset processing model for shader authoring. Instead of forcing developers to sync separate vertex and fragment files on disk, the engine processes single `.glsl` configurations using explicit preprocessors.

#### Type Splitting & Preprocessing

The engine scans the source code file from top to bottom looking for the keyword token `#type`. When it finds this token, it divides and splits the subsequent text stream blocks into specific asset modules for compilation:

```glsl
#type vertex
#version 450 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_ViewProjection;

void main() {
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core
layout(location = 0) out vec4 color;

void main() {
    color = vec4(1.0, 0.5, 0.2, 1.0);
}

```

#### Automated Engine Injection

During translation, the preprocessor automatically injects core engine uniform schemas if your code references them. You do not need to manually declare global configuration factors like `uniform float u_Time;` or `uniform vec2 u_ViewportSize;`—if the source references these uniform tokens, Cosmic updates and handles them behind the scenes.

#### Shadertoy Pixel Asset Wrapping

If the preprocessor detects a `void mainImage(out vec4 fragColor, in vec2 fragCoord)` signature inside the file, it automatically flags the asset as a specialized pixel canvas. The engine injects a boilerplate vertex stage and wraps input constants (like `iTime`, `iResolution`, and `iMouse`) to match Shadertoy's format, making it easy to drop in procedural math sketches.

---

### How Events are Processed (and helpful overrides)

Cosmic uses a non-polling, top-down event dispatching architecture. When the operating system catches an input or window signal, it converts that data into an engine-specific `Event` and routes it through the active engine layer stack.

```
       [OS Native Window Input Signal]
                      │
                      ▼
          Application::OnEvent(Event& e)
                      │
                      ├─► [Dispatches WindowClose / WindowResize]
                      ▼
        ┌───────────────────────────┐
        │ LayerStack Top-Down Pass  │
        └─────────────┬─────────────┘
                      │
        ┌─────────────▼─────────────┐
        │     Workspace Layer       │ ───► Processes Framebuffer Viewport
        └─────────────┬─────────────┘
                      │ [If e.Handled == true -> Intercept & Break]
                      ▼
        ┌───────────────────────────┐
        │   Active Game DLL Layer   │ ───► Processes Core Gameplay Elements
        └─────────────┬─────────────┘
                      │ [If e.Handled == true -> Intercept & Break]
                      ▼
        ┌───────────────────────────┐
        │      Underlying Layers    │
        └───────────────────────────┘

```

#### Event Propagation and Interception Mechanics

Events stream into the layer vector from top to bottom (the highest overlay handles inputs first, trailing down to bottom game scenes).

Every module can inspect and interact with the event. If a layer processes an input and wants to block it from trickling further down the stack, it sets `Handled = true`. This prevents lower modules from seeing the input (for example, clicking an ImGui panel blocks the click from firing a game weapon behind that panel).

#### Utilizing the Event Dispatcher

To isolate specific event types within `OnEvent()`, use the engine's `EventDispatcher` tool along with the `GLCORE_BIND_EVENT_FN` tracking macro to bind your target functions:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    // Check if the event matches a KeyPressed type and route it to OnKeyPress
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPress));

    // Check if the event matches a MouseButtonPress type and route it to OnMouseClick
    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(GLCORE_BIND_EVENT_FN(MyLayer::OnMouseClick));
}

bool MyLayer::OnKeyPress(Cosmic::KeyPressedEvent& e)
{
    if (e.GetKeyCode() == Cosmic::Key::Escape)
    {
        // Toggle menu states or trigger logic...
        return true; // Returns true to mark the input event as completely handled
    }
    return false; // Continues streaming down the layer hierarchy pipeline
}

```

---

### Core Lifecycle & Runtime Hooks

Every project layer must inherit from `Cosmic::Layer`. Override these virtual hooks to embed custom systems directly into the application heartbeat:

#### `OnAttach()`

- **Purpose:** Executed immediately when the module is pushed into the active engine `LayerStack`.
- **Usage:** Allocate persistent memory, instantiate game scenes, load engine assets, and configure initial uniform material maps.

#### `OnDetach()`

- **Purpose:** Executed when the module is removed from the active `LayerStack` or during runtime shutdown sequence.
- **Usage:** Manually clear or `.reset()` smart pointers, dump caches, and close asset handles to prevent memory leak cascades across DLL boundaries.

#### `OnUpdate(float deltaTime)`

- **Purpose:** The standard simulation frame loop, executed once every rendering pass.
- **Parameter:** `deltaTime` provides the precise floating-point variance in seconds since the last frame step to ensure gameplay logic runs speed-independently of raw GPU frame rates.

#### `OnFixedUpdate(float deltaFixedTime)`

- **Purpose:** A dedicated simulation loop executed at a rigid, fixed time interval regardless of hardware speed.
- **Usage:** Critical physics steps, velocity integrations, or hardware communication loops (such as serial tracking) that experience instability under variable framerates.

#### `OnImGuiRender()`

- **Purpose:** An isolated interface drawing loop called after world rendering completes.
- **Usage:** Wrap your `ImGui::Begin()` / `ImGui::End()` debug tools, diagnostics counters, component properties sliders, and telemetry hooks here.

#### `OnEvent(Cosmic::Event& e)`

- **Purpose:** The input entry-point routing structural operating system window, mouse, and keyboard inputs straight into your active layer module before it falls down the rest of the engine pipeline stack.

---

### Required .dll Linking C Script

Because the Cosmic launcher links to project modules dynamically at runtime, your custom gameplay layer project must expose its internal entry-point factories. Add this explicit block to the bottom of your primary game project implementation file to ensure the engine host can map your memory workspace and forward its active ImGui context handles across the binary boundary:

```cpp
#include <Cosmic.h>
#include "MyCustomLayer.h"

extern "C"
{
    // Synchronizes the host engine UI contexts with your plugin assembly
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    // Instantiates your root layer and hands back the raw base class pointer to the launcher loop
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::MyCustomLayer(); // Replace with your true namespace and class name
    }
}

```

---

## Virtual File System Pathing

The Cosmic Engine features a static Virtual File System (VFS) utility, encapsulated by the `FileSystem` class. It manages path resolution by abstracting physical disk locations into decoupled, high-level protocols (`engine://` and `project://`). This ensures asset references remain persistent across runtime project swaps and deployments.

```
  High-Level Asset Request
```

```
┌─────────────────────────────┐
│ "project://textures/sky.png"│
└──────────────┬──────────────┘
               │
               ▼
FileSystem::Resolve()
               │
[Evaluates Protocols via Prefixes]
│
├─► "engine://" ──► "assets/" + Substring
▼
└─► "project://" ──► "assets/projects/" + s_ActiveProjectName + "/" + Substring
│
▼
Resulting Runtime Local Path
┌──────────────────────────────────────────────────┐
│ "assets/projects/MySandbox/textures/sky.png" │
└──────────────────────┬───────────────────────────┘
                       │
                       ▼
Physical Disk Verification
(Maps directly to CMake POST_BUILD copied directory)
📂 build/Runtime//assets/projects/MySandbox/...

```

---

### Key Capabilities

- **Protocol Decoupling:** Isolates internal asset-loading code blocks from literal, relative, or hardcoded absolute OS paths.
- **Seamless Project Swapping:** Allows the engine to shift its targeting window to entirely different internal workspaces dynamically by remapping a single string token (`s_ActiveProjectName`) at runtime.
- **CMake Post-Build Integration:** Seamlessly maps directly to the structure created by the engine's build pipelines, guaranteeing that localized folder expansions look for content exactly where CMake deploys assets during post-build synchronization events.

---

### Reference Table: VFS Protocol Layout

| Virtual Protocol Target  | Translated System Path             | Core Purpose & Scope                                                                                                           |
| :----------------------- | :--------------------------------- | :----------------------------------------------------------------------------------------------------------------------------- |
| **`engine://`**          | `assets/`                          | Points directly to internal, global workspace requirements (e.g., core shaders, default fallback fonts, engine icons).         |
| **`project://`**         | `assets/projects/<ActiveProject>/` | Points into the isolated data sandbox of the currently running game module.                                                    |
| **Raw/Unprefixed Paths** | _Unmodified Fallback_              | Bypasses protocol translations entirely, passing the string parameter straight to backend loaders as a standard fallback path. |

---

### VFS Static Interface Summary

#### `static std::string Resolve(const std::string& path)`

- **Pre-Conditions:** None.
- **Post-Conditions:** Returns a platform-specific relative string path.
- **Mechanics:** If the incoming parameter starts with a prefix token, it slices the prefix off and reformats the target string into a concrete file pathway. Otherwise, it safely bounces the original string back out unmodified.

```cpp
// Examples of Resolve evaluations at runtime:
std::string coreShader = FileSystem::Resolve("engine://shaders/Texture.glsl");
// Returns: "assets/shaders/Texture.glsl"

FileSystem::SetActiveProject("SpaceGame");
std::string gameAsset  = FileSystem::Resolve("project://textures/ship.png");
// Returns: "assets/projects/SpaceGame/textures/ship.png"

```

#### `static void SetActiveProject(const std::string& name)`

- **Pre-Conditions:** `name` must map to a valid folder existing under the local `assets/projects/` runtime output hierarchy.
- **Post-Conditions:** Overwrites the internal static configuration string state `s_ActiveProjectName`. All subsequent asset evaluations targeting `project://` route into this project folder immediately.

---

### Build Pipeline Synchronization (CMake Blueprint)

To ensure the virtual paths evaluated by `FileSystem::Resolve()` map to active files on disk, the build pipeline runs a `POST_BUILD` automation command. This command automatically copies and synchronizes the physical source `assets/` directory tree into your runtime binary execution workspace whenever the engine compiles:

```cmake
# Guardrails deployment paths across Multi-Config (VS) and Single-Config (Ninja) systems safely
set_target_properties(Cosmic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# Automated Synchronization: Clones asset directory trees straight to your target workspace
add_custom_command(TARGET Cosmic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets"
    COMMENT "Syncing Cosmic Engine core assets to output directory..."
)

```

> **Asset Directory Hierarchy Requirement:** To prevent deployment mismatches or missing asset crashes at runtime, game modules must structure their file trees to match the expected directory layout. Custom modules must be packaged within `assets/projects/<YourProjectName>/` so the post-build system can map them correctly into the build runtime directory.

```</CONFIG></CONFIG></CONFIG></Config>

```

---

## 2D Rendering & Entity Component System (ECS)

The Cosmic Engine integrates a high-performance 2D Batch Renderer with a data-driven Entity Component System (ECS) backed by EnTT. This dual-paradigm architecture separates game data from systemic execution. It handles massive entity draw sweeps automatically via sorting optimization pools while exposing a direct immediate-mode API for manual drawings, debug wireframes, and procedural primitive rendering.

---

### Structural Architecture Layout

The engine decouples structural scenes, raw hardware drawing pipelines, and client execution layers through a strict stratified rendering architecture:

```
  ┌────────────────────────────────────────────────────────┐
  │                   Game Loop Layer                      │
  │     (DinoStressLayer, SpaceGame, Custom Client)        │
  └───────────────────────────┬────────────────────────────┘
                              │
            ┌─────────────────┴─────────────────┐
            ▼                                   ▼
  ┌───────────────────┐               ┌───────────────────┐
  │    ECS Registry   │               │ Direct Immediate  │
  │ (Scene & Entities)│               │    API Drawing    │
  └─────────┬─────────┘               └─────────┬─────────┘
            │                                   │
            └─────────────────┬─────────────────┘
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                      Renderer2D                        │
  │     (Vertex/Index Staging Buffers & Material Buckets)   │
  └───────────────────────────┬────────────────────────────┘
                              │
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                    RenderCommand                       │
  │        (Abstract Hardware Independent Dispatcher)      │
  └───────────────────────────┬────────────────────────────┘
                              │ (s_RendererAPI Forwarding)
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                     RendererAPI                        │
  │       (Low-Level Hardware Driver: OpenGL backend)      │
  └────────────────────────────────────────────────────────┘

```

---

### Core Rendering Paradigms

#### Paradigm 1: Automated Material-Batched ECS Sweeps

When rendering a complex environment containing thousands of entities, using `Scene::OnRender()` triggers a specialized material sorting system. The scene parses its internal `entt::registry` pool, isolating all entities containing a `TransformComponent` and a `SpriteRendererComponent`.

To prevent costly texture binding state changes and constant GPU shader thrashing, the engine groups entities into **Material Sorting Buckets** before pushing the vertex arrays to the batch renderer:

```cpp
// Within Scene.cpp — The Engine isolates and batches draw calls under the hood:
void Scene::OnRender()
{
    // Gather all entities possessing transform and sprite configurations
    auto view = m_Registry.view<TransformComponent, SpriteRendererComponent>();

    std::unordered_map<Material*, std::vector<entt::entity>> materialBuckets;
    std::vector<entt::entity> flatColorFallbackBucket;

    // Allocate entity buckets based on active material pointers
    view.each([&](auto entity, const auto& transform, const auto& sprite) {
        if (sprite.ActiveMaterial)
            materialBuckets[sprite.ActiveMaterial.get()].push_back(entity);
        else
            flatColorFallbackBucket.push_back(entity);
    });

    // 1. Dispatch Material-Batched Quads (One continuous loop pass per distinct material)
    for (const auto& [materialPtr, entities] : materialBuckets) {
        Ref<Material> activeMaterial = view.get<SpriteRendererComponent>(entities[0]).ActiveMaterial;
        for (auto entity : entities) {
            auto& transform = view.get<TransformComponent>(entity);
            Renderer2D::DrawRotatedQuad(transform.Position, transform.Scale, transform.Rotation.z, activeMaterial);
        }
    }

    // 2. Dispatch Fallback Flat-Color Quads (Batched together using a fallback 1x1 white texture)
    for (auto entity : flatColorFallbackBucket) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& sprite = view.get<SpriteRendererComponent>(entity);
        Renderer2D::DrawRotatedQuad(transform.Position, transform.Scale, transform.Rotation.z, sprite.Color);
    }
}

```

##### Client Implementation Example:

```cpp
// Instantiating and rendering an optimized entity layout via a Client Layer
void DinoStressLayer::RegenerateGrid()
{
    size_t index = 0;
    for (int x = -m_GridSize; x < m_GridSize; x++) {
        for (int y = -m_GridSize; y < m_GridSize; y++) {
            Cosmic::Entity entity = m_Scene->CreateEntity("StressCell");[cite: 5]

            // Allocate and configure spatial properties
            auto& t = entity.GetComponent<Cosmic::TransformComponent>();[cite: 5]
            t.Position = { x * 0.1f, y * 0.1f, 0.0f };[cite: 5]
            t.Scale = { 0.08f, 0.08f };[cite: 5]

            // Inject renderer data and assign distinct materials
            auto& sprite = entity.AddComponent<Cosmic::SpriteRendererComponent>();[cite: 5]
            sprite.ActiveMaterial = (index % 2 == 0) ? m_CachedFireMaterial : m_CachedDinoMaterial;[cite: 5]
            index++;
        }
    }
}

void DinoStressLayer::OnRender()
{
    Cosmic::Renderer2D::BeginScene(m_CamController.GetCamera());[cite: 5]

    // Automatically executes sorted material grouping passes internally
    m_Scene->OnRender();[cite: 5]

    Cosmic::Renderer2D::EndScene();[cite: 5]
}

```

---

#### Paradigm 2: Direct Immediate Rendering (Single Draw Calls)

For custom runtime overlays, procedural maps, or engine wireframes, you can bypass the entity registry entirely. Invoking static methods inside `Renderer2D` instantly prepares and flushes vertex staging primitives.

```cpp
void DinoFlightLayer::OnRender()
{
    Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());[cite: 5]

    auto& trans = m_FlightDino.GetComponent<Cosmic::TransformComponent>();[cite: 5]
    auto& trail = m_FlightDino.GetComponent<FlightTrailComponent>();[cite: 5]

    // 1. Generate an immediate background checkerboard pattern on the fly
    float startX = floor(trans.Position.x) - 10.0f;[cite: 5]
    float startY = floor(trans.Position.y) - 10.0f;[cite: 5]
    for (float x = startX; x < startX + 20.0f; x += 1.0f) {
        for (float y = startY; y < startY + 20.0f; y += 1.0f) {
            bool isEven = (int(floor(x)) + int(floor(y))) % 2 == 0;[cite: 5]
            glm::vec4 color = isEven ? glm::vec4(0.2f, 0.2f, 0.25f, 1.0f) : glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);[cite: 5]

            // Direct quad injection passing implicit standard color variables
            Cosmic::Renderer2D::DrawQuad({ x, y, -0.1f }, { 1.0f, 1.0f }, color);[cite: 5]
        }
    }

    // 2. Submit immediate non-indexed layout line primitives for path debugging
    if (trail.Path.size() > 1) {
        for (size_t i = 0; i < trail.Path.size() - 1; i++) {
            Cosmic::Renderer2D::DrawLine(trail.Path[i], trail.Path[i+1], { 1.0f, 0.0f, 0.0f, 1.0f });[cite: 5]
        }
    }

    // 3. Render an isolated, asset-bound rotated quad primitive
    Cosmic::Renderer2D::DrawRotatedQuad(trans.Position, trans.Scale, trans.Rotation.z, m_Material);[cite: 5]

    Cosmic::Renderer2D::EndScene();[cite: 5]
}

```

---

### Core Structural Framework References

#### Multi-Binary DLL Boundaries & Type Hash Safety

Because Cosmic compiles as a shared dynamic library (`COSMIC_BUILD_DLL`), passing templated EnTT component registries across explicit binary boundaries poses a risk of type ID fragmentation. The framework avoids this runtime hazard by declaring explicit `consteval id_type` hash overrides at the bottom of `Components.h`:

```cpp
namespace entt
{
    template<> struct type_hash<Cosmic::TransformComponent> final {
        [[nodiscard]] static consteval id_type value() noexcept { return hashed_string::value("TransformComponent"); }
    };
    template<> struct type_hash<Cosmic::SpriteRendererComponent> final {
        [[nodiscard]] static consteval id_type value() noexcept { return hashed_string::value("SpriteRendererComponent"); }
    };
}

```

This forces compiler instances—regardless of whether they are processing the core engine module or external user layers—to resolve unified, matching component type tokens.

---

### Comprehensive API Reference Tables

#### 1. Entity Component System Management (`Cosmic::Entity`)

This wrapper class interfaces directly with the scene's internal EnTT registry mapping.

| Function Prototype                                                           | Description                                                                                          | Practical Code Example |
| ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ---------------------- |
| `template<typename T, typename... Args> T& AddComponent(Args&&... args)`<br> | Constructs a component in-place inside the registry pool. Asserts if the entity already contains it. |

| `auto& sprite = entity.AddComponent<SpriteRendererComponent>(myMaterial);`<br> |
| `template<typename T> T& GetComponent()`<br> | Returns a reference to a component type held by this entity. Asserts if the entity does not possess it.

| `auto& transform = entity.GetComponent<TransformComponent>();`<br> |
| `template<typename T> bool HasComponent()`<br> | Checks if the entity contains the specified component type.

| `if (entity.HasComponent<SpriteRendererComponent>()) { /* ... */ }` |
| `template<typename T> void RemoveComponent()`<br> | Safely removes a component type from the underlying registry pool.

| `entity.RemoveComponent<SpriteRendererComponent>();`<br> |
| `operator bool() const`<br> | Implicit type conversion operator validating that the internal handle is valid and active.

| `if (m_SelectedEntity) { m_SelectedEntity.GetComponent<TransformComponent>(); }` |

---

### Comprehensive API Reference Tables

#### 1. Entity Component System Management (`Cosmic::Entity`)

This wrapper class interfaces directly with the scene's internal EnTT registry mapping[cite: 5].

| Function Prototype                                                                | Description                                                                                                      | Practical Code Example                                                              |
| :-------------------------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------------- |
| `template<typename T, typename... Args> T& AddComponent(Args&&... args)`[cite: 5] | Constructs a component in-place inside the registry pool. Asserts if the entity already contains it[cite: 5].    | `auto& sprite = entity.AddComponent<SpriteRendererComponent>(myMaterial);`[cite: 5] |
| `template<typename T> T& GetComponent()`[cite: 5]                                 | Returns a reference to a component type held by this entity. Asserts if the entity does not possess it[cite: 5]. | `auto& transform = entity.GetComponent<TransformComponent>();`[cite: 5]             |
| `template<typename T> bool HasComponent()`[cite: 5]                               | Checks if the entity contains the specified component type[cite: 5].                                             | `if (entity.HasComponent<SpriteRendererComponent>()) { /* ... */ }`                 |
| `template<typename T> void RemoveComponent()`[cite: 5]                            | Safely removes a component type from the underlying registry pool[cite: 5].                                      | `entity.RemoveComponent<SpriteRendererComponent>();`[cite: 5]                       |
| `operator bool() const`[cite: 5]                                                  | Implicit type conversion operator validating that the internal handle is valid and active[cite: 5].              | `if (m_SelectedEntity) { m_SelectedEntity.GetComponent<TransformComponent>(); }`    |

---

#### 2. Specialized High-Level Batch Rendering (`Cosmic::Renderer2D`)

Manages internal vertex buffers, texture multi-slots, and scene-wide global uniforms[cite: 9, 10].

| Function Prototype                                                                                                                | Description                                                                                                                           | Practical Code Example                                                              |
| :-------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------ | :---------------------------------------------------------------------------------- |
| `static void BeginScene(const OrthographicCamera& camera)`[cite: 10]                                                              | Resets batch counters, initializes vertex offsets, and caches camera projection matrices[cite: 9, 10].                                | `Renderer2D::BeginScene(m_CameraController.GetCamera());`[cite: 5]                  |
| `static void EndScene()`[cite: 10]                                                                                                | Finalizes calculations and triggers an immediate internal buffer `Flush()` command to the GPU[cite: 9, 10].                           | `Renderer2D::EndScene();`[cite: 5]                                                  |
| `static void UpdateTimeline(float ts, uint32_t width, uint32_t height)`[cite: 10]                                                 | Updates internal engine constants (e.g., incremental time vectors, viewport resolutions) for custom fragment shaders[cite: 5, 9, 10]. | `Renderer2D::UpdateTimeline(deltaTime, 1280, 720);`[cite: 5]                        |
| `static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)`[cite: 10]                        | Injects an untextured flat geometry quad directly into the active vertex batch array[cite: 9, 10].                                    | `Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f});`[cite: 5] |
| `static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material)`[cite: 10]                 | Injects a quad using the custom properties, bindings, and shader routines attached to an active Material[cite: 9, 10].                | `Renderer2D::DrawQuad(transform.Position, transform.Scale, myMaterial);`            |
| `static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)`[cite: 10] | Transforms a quad along the Z-axis by a specified angle (in radians) before pushing it onto the batch stack[cite: 9, 10].             | `Renderer2D::DrawRotatedQuad(pos, scale, glm::radians(45.0f), color);`              |
| `static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)`[cite: 10]                                | Batches a non-indexed line segment primitive using designated world-space coordinates[cite: 9, 10].                                   | `Renderer2D::DrawLine({0.f, 0.f, 0.f}, {5.f, 5.f, 0.f}, color);`[cite: 5]           |
| `static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)`[cite: 10]                        | Convenience function that executes four `DrawLine` calls to render a wireframe outline box[cite: 9, 10].                              | `Renderer2D::DrawRect({0.f, 0.f, 0.f}, {2.f, 2.f}, {0.f, 1.f, 0.f, 1.f});`          |
| `static Statistics GetStats()`[cite: 10]                                                                                          | Returns a copy of performance telemetry data (active draw calls, total processed quads)[cite: 9, 10].                                 | `auto stats = Renderer2D::GetStats();`[cite: 5]                                     |
| `static void ResetStats()`[cite: 10]                                                                                              | Clears the hardware performance monitoring fields back to zero at the start of a frame loop[cite: 5, 9, 10].                          | `Renderer2D::ResetStats();`[cite: 5]                                                |

---

#### 3. Abstract Independent Hardware Commands (`Cosmic::RenderCommand`)

The low-level hardware gateway. It abstracts execution details for specific graphics backends behind a unified, static dispatching interface[cite: 6].

| Function Prototype                                                                 | Description                                                                                        | Practical Code Example                                                     |
| :--------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------- |
| `static void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h)`[cite: 6] | Maps normalized device space coordinates directly onto window-space pixel coordinates[cite: 6].    | `RenderCommand::SetViewport(0, 0, width, height);`[cite: 7]                |
| `static void SetClearColor(const glm::vec4& color)`[cite: 6]                       | Updates the stored background fill values used during hardware buffer wipe requests[cite: 6].      | `RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.12f, 1.0f });`               |
| `static void Clear()`[cite: 6]                                                     | Instructs the active GPU graphics driver to instantly clear both color and depth buffers[cite: 6]. | `RenderCommand::Clear();`[cite: 6]                                         |
| `static void DrawIndexed(const Ref<VertexArray>& va, uint32_t count = 0)`[cite: 6] | Sends a final hardware execution command to draw optimized index-buffered structures[cite: 6].     | `RenderCommand::DrawIndexed(s_Data.QuadVertexArray, indexCount);`[cite: 9] |
| `static void DrawLines(const Ref<VertexArray>& va, uint32_t vertexCount)`[cite: 6] | Sends a final hardware execution command to render non-indexed raw array line segments[cite: 6].   | `RenderCommand::DrawLines(s_Data.LineVertexArray, vertexCount);`[cite: 9]  |

---

## Core, Logging, & Profiling

The structural core of the Cosmic Engine acts as its master orchestration layer, managing lifecycle loops, dynamic guest assemblies, thread-safe diagnostics, and precision timeline mechanics[cite: 13, 14, 15]. The foundation encapsulates memory management wrappers to enforce clear data ownership profiles across sub-systems[cite: 13].

---

### Core Architecture & Memory Management

The engine maintains a strict multi-binary dynamic link library (`COSMIC_BUILD_DLL`) architecture on Windows x64 platform environments[cite: 13]. To manage execution boundaries cleanly without raw pointer leaks or reference-counting errors, Cosmic abstracts explicit smart pointer semantics[cite: 13]:

- **`Scope<T>`**: An alias for `std::unique_ptr<T>`[cite: 13]. It denotes **strict, single ownership** over isolated sub-systems or window wrappers[cite: 13].
- **`Ref<T>`**: An alias for `std::shared_ptr<T>`[cite: 13]. It manages **shared resources** bound to graphics drivers or assets that require multi-subsystem access[cite: 13].

#### The Main Loop Heartbeat & The Safe Zone

The central operational driver inside `Application::Run()` runs a dual-timestep frame model[cite: 14]. It tracks raw processing speeds via the `Timestep` proxy, routing deterministic updates down a discrete accumulator window while passing continuous visual intervals to presentation methods[cite: 14].

Crucially, the bottom loop sequence implements a dedicated execution **Safe Zone**[cite: 14]. This structure isolates adjustments to volatile iterators out of the active runtime pipeline, safely pushing or deleting unmanaged layer allocations without inducing heap exceptions or racing ongoing frame sweeps[cite: 14].

```cpp
// Simulated client loop execution demonstrating Timestep conversion and Core integration
void CustomLayer::OnUpdate(Cosmic::Timestep ts)
{
    // Prevent unit confusion by accessing converted floats directly
    float deltaSeconds = ts.GetSeconds();[cite: 13]
    float deltaMs = ts.GetMilliseconds();[cite: 13]

    // Engine Assert validation checking structural integrity inside a debug profile
    GLCORE_ASSERT(deltaSeconds > 0.0f, "Frame time calculation reversed!");[cite: 13]

    if (m_SimulationActive)
    {
        m_EntityPhysicsTimer += deltaSeconds;
    }
}

```

---

### Core & Diagnostics API Reference

#### 1. Core Structural Foundations & Memory Sub-systems

Unified macro behaviors and memory aliasing hooks handled inside `core/Core.h`[cite: 13].

| Function / Macro Prototype                          | Description                                                                                                          | Practical Code Example                                        |
| :-------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------ |
| `CreateScope<T, Args...>(Args&&... args)`[cite: 13] | Instantiates a heap-allocated unique-ownership pointer wrapped in a `Scope<T>` context[cite: 13].                    | `auto window = CreateScope<Window>(1280, 720);`               |
| `CreateRef<T, Args...>(Args&&... args)`[cite: 13]   | Instantiates a heap-allocated shared-ownership pointer wrapped in a `Ref<T>` context[cite: 13].                      | `auto texture = CreateRef<Texture>("dino.png");`              |
| `BIT(x)`[cite: 13]                                  | Evaluates an integer expression into a bitwise flag configuration (shift left by $x$ units)[cite: 13].               | `uint32_t category = BIT(2); // Evaluates to 4`[cite: 13]     |
| `GLCORE_BIND_EVENT_FN(fn)`[cite: 13]                | Automatically handles `std::bind` syntax parameters to hook member functions into the Event dispatchers[cite: 13].   | `SetEventCallback(GLCORE_BIND_EVENT_FN(OnEvent));`[cite: 14]  |
| `GLCORE_ASSERT(condition, ...)`[cite: 13]           | Development tool checking critical logic assumptions. Triggers a hardware debug break if validation fails[cite: 13]. | `GLCORE_ASSERT(ptr != nullptr, "Null reference!");`[cite: 13] |

#### 2. Thread-Safe Diagnostic Sub-system (`Cosmic::Log`)

Pre-initialized logging mechanisms built around an unmanaged multi-threaded console sink configuration (`spdlog` execution)[cite: 13, 14].

| Function / Macro Prototype                            | Description                                                                                                  | Practical Code Example                          |
| :---------------------------------------------------- | :----------------------------------------------------------------------------------------------------------- | :---------------------------------------------- |
| `static void Init()`[cite: 13]                        | Initializes the diagnostic sinks, formats patterns, and registers engine vs. app color categories[cite: 13]. | `Log::Init();`[cite: 14]                        |
| `CS_CORE_TRACE(...)` / `CS_CORE_ERROR(...)`[cite: 13] | Dispatches prioritized informational strings straight to the engine internal logger (`"COSMIC"`)[cite: 13].  | `CS_CORE_ERROR("Graphics API error: {0}", id);` |
| `CS_TRACE(...)` / `CS_ERROR(...)`[cite: 13]           | Dispatches prioritized informational strings straight to the user application logger (`"APP"`)[cite: 13].    | `CS_TRACE("Loading level chunk: {0}", index);`  |

#### 3. Delta Time Management Wrappers (`Cosmic::Timestep`)

Lightweight time proxies encapsulating frame generation measurements to prevent structural unit-confusion errors[cite: 13].

| Function Prototype                        | Description                                                                                                  | Practical Code Example                              |
| :---------------------------------------- | :----------------------------------------------------------------------------------------------------------- | :-------------------------------------------------- |
| `Timestep(float time = 0.0f)`[cite: 13]   | Constructs a temporal boundary tracking variable initialized in raw seconds[cite: 13].                       | `Timestep ts = currentFrame - lastFrame;`[cite: 14] |
| `float GetSeconds() const`[cite: 13]      | Extracts the encapsulated time window duration represented explicitly as seconds[cite: 13].                  | `float sec = ts.GetSeconds();`[cite: 13]            |
| `float GetMilliseconds() const`[cite: 13] | Extracts the encapsulated time window duration scaled to milliseconds ($s \times 1000.0$)[cite: 13].         | `float ms = ts.GetMilliseconds();`[cite: 13]        |
| `operator float() const`[cite: 13]        | Implicit type conversion operator that unloads the raw seconds scalar value within a math context[cite: 13]. | `float speedModifier = m_BaseVelocity * ts;`        |

---

## Input Polling

The Cosmic Engine separates input tracking into two paradigms: a **Reactive Event System** for top-level window layout modifications and an asynchronous, proactive **Static Input Polling Sub-system** designed for gameplay iteration frames. Input polling samples hardware tracking registers directly, completely bypassing event routing latency.

---

### Proactive Polling vs. Reactive Handling

While the Event Nervous System acts retroactively when keys change state, continuous actions like character mechanics, custom editor translation tools, or viewport tracking require real-time hardware status answers.

The static `Input` class queries the engine's root window context directly using underlying hardware handles (`GLFWwindow`), instantly pulling values from peripheral buffers during execution sweeps.

```cpp
// Direct usage within a variable game loop context layer
void SpaceGameLayer::OnUpdate(Cosmic::Timestep ts)
{
    // 1. Proactive keyboard state queries for layout transformations
    if (Cosmic::Input::IsKeyPressed(CS_KEY_W))
    {
        m_ShipPosition.y += m_EngineThrust * ts;
    }
    if (Cosmic::Input::IsKeyPressed(CS_KEY_S))
    {
        m_ShipPosition.y -= m_EngineThrust * ts;
    }

    // 2. Immediate mouse click coordinate calculations
    if (Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
    {
        glm::vec2 rawMouse = Cosmic::Input::GetMousePosition();
        CastScreenToWorldRay(rawMouse);
    }
}

```

---

### Input Mapping API Reference

#### 1. Global State Queries (`Cosmic::Input`)

Static data gateway providing global hardware verification checks across any software layer component[cite: 17].

| Function Prototype                                       | Description                                                                                                           | Practical Code Example                                                  |
| :------------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------- |
| `static bool IsKeyPressed(int keycode)`[cite: 17]        | Returns `true` if the specific keyboard scan token matches a pressed or repeated flag state[cite: 16].                | `bool spacePressed = Input::IsKeyPressed(CS_KEY_SPACE);`                |
| `static bool IsMouseButtonPressed(int button)`[cite: 17] | Returns `true` if the specific mouse hardware peripheral token matches an active click state[cite: 16].               | `bool leftClicked = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);` |
| `static glm::vec2 GetMousePosition()`[cite: 17]          | Queries screen-space coordinates $(x, y)$ relative to the upper-left boundary of the viewport window frame[cite: 16]. | `glm::vec2 mousePos = Input::GetMousePosition();`[cite: 17]             |
| `static float GetMouseX()`[cite: 17]                     | Isolated helper returning only the horizontal component of the viewport cursor coordinate[cite: 16, 17].              | `float rawX = Input::GetMouseX();`                                      |
| `static float GetMouseY()`[cite: 17]                     | Isolated helper returning only the vertical component of the viewport cursor coordinate[cite: 16, 17].                | `float rawY = Input::GetMouseY();`                                      |

#### 2. Key Code Constants Mapping Layout

Platform-agnostic key reference mappings managed inside `codes/KeyCodes.h`[cite: 18].

| Engine Constant Hash                  | Value Match         | Representation Category                       |
| :------------------------------------ | :------------------ | :-------------------------------------------- |
| `CS_KEY_SPACE`[cite: 18]              | 32[cite: 18]        | Alpha-Numeric / Standard Layout Primitives    |
| `CS_KEY_A` to `CS_KEY_Z`[cite: 18]    | 65 to 90[cite: 18]  | Alpha-Numeric Standard Workspace Layout       |
| `CS_KEY_0` to `CS_KEY_9`[cite: 18]    | 48 to 57[cite: 18]  | Sequential Numerical Row Indices              |
| `CS_KEY_ESCAPE`[cite: 18]             | 256[cite: 18]       | System Command / Navigation Control Functions |
| `CS_KEY_LEFT` / `CS_KEY_UP`[cite: 18] | 263 / 265[cite: 18] | Directional Positional Scanning Arrays        |
| `CS_KEY_LEFT_CONTROL`[cite: 18]       | 341[cite: 18]       | Layout Action State Modifiers                 |

#### 3. Mouse Code Constants Mapping Layout

Platform-agnostic mouse button references managed inside `codes/MouseButtonCodes.h`.

| Engine Constant Hash     | Value Match         | System Alias Designation                       |
| :----------------------- | :------------------ | :--------------------------------------------- |
| `CS_MOUSE_BUTTON_1`      | 0                   | Standard Binary Button Registry Token          |
| `CS_MOUSE_BUTTON_LEFT`   | `CS_MOUSE_BUTTON_1` | Master Context Action Interaction Mapping      |
| `CS_MOUSE_BUTTON_RIGHT`  | `CS_MOUSE_BUTTON_2` | Secondary Workspace Inspection Overlay Request |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` | Canvas Tracking and Spatial Pan Modification   |

---

### Event System Overview

The Cosmic Engine utilizes a **Reactive Event System** that functions as the engine's "Nervous System". When OS or hardware actions occur (such as key strokes, window sizing modifications, or mouse clicks), the native wrapper captures the signal, packages it into a specialized `Event` object, and immediately routes it through the engine's active `LayerStack`.

Understanding how this architecture executes is critical for client developers creating custom gameplay logic or user interface layers.

---

### The Event Propagation Pipeline

Cosmic's event distribution operates on a **top-down, short-circuiting flow**:

1. **Hardware Capture**: Native hardware window loops poll OS signals via GLFW.

2. **Translation & Injection**: Glfw callback lambda functions transform raw hardware data into specific `Event` subclasses and pass them directly to the main `Application::OnEvent` entry point.

3. **Layer Stack Traversal**: The event is propagated through the `LayerStack` in reverse order—from the topmost layer (typically user overlays or ImGui menus) down to the bottommost logic layer (the game world simulation).

4. **Short-Circuit Handling**: Every layer exposes an `OnEvent(Event& e)` lifecycle hook. If an overlay consumes an action (for example, clicking a button on an editor pane), the layer sets `Event::Handled = true`. This terminates propagation instantly, preventing the click from leaking down into underlying layers and triggering unintended world interactions.

```cpp
// Example of short-circuiting input within an unmanaged UI overlay layer
void EditorUILayer::OnEvent(Cosmic::Event& e)
{
    // If the mouse cursor is hovering over an active ImGui panel context,
    // intercept the click and halt propagation to keep the game world safe.
    if (m_PanelIsHovered && e.IsInCategory(Cosmic::EventCategoryMouse))
    {
        e.Handled = true; // Stops the event from trickling down the LayerStack!
    }
}

```

---

### Macro Infrastructure & Event Dispatching

To minimize structural boilerplate across specialized classes, Cosmic implements a pair of reflection-like tracking macros:

- `EVENT_CLASS_TYPE(type)`: Standardizes virtual typing hooks (`GetStaticType`, `GetEventType`, and `GetName`) used to identify structural types at runtime without incurring costly overhead.

- `EVENT_CLASS_CATEGORY(category)`: Implements bitwise flag masks so that a singular event instance can simultaneously respond to broad system queries (e.g., matching both `EventCategoryInput` and `EventCategoryKeyboard`).

#### Direct Processing with the EventDispatcher

Inside `OnEvent(Event& event)`, developers utilize a stack-allocated `EventDispatcher`. This utility compares the `EventType` of the incoming packet against a targeted subscriber method signature. If a match is validated, it casts the generic base references to the concrete event subclass, executes the bound method, and flags the event status automatically.

```cpp
// Typical client layer event filtering block
void SimulationLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    // Dynamic type checking automatically registers handlers without manual casting
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(GLCORE_BIND_EVENT_FN(SimulationLayer::OnKeyPress));
    dispatcher.Dispatch<Cosmic::MouseMovedEvent>(GLCORE_BIND_EVENT_FN(SimulationLayer::OnMouseMovement));
}

bool SimulationLayer::OnKeyPress(Cosmic::KeyPressedEvent& e)
{
    if (e.GetKeyCode() == CS_KEY_ESCAPE)
    {
        TogglePauseMenu();
        return true; // Mark handled: event consumption succeeds
    }
    return false; // Propagate further down the stack
}

```

---

### Core Event Infrastructure API Reference

#### 1. Event Type Descriptors (`Cosmic::EventType`)

Strongly typed identifiers designating exactly which native peripheral device or application instance generated the structural event packet[cite: 19].

| Enum Name                       | Category Group Assignment                               | Triggering Scenario Description                                                 |
| :------------------------------ | :------------------------------------------------------ | :------------------------------------------------------------------------------ |
| `WindowClose`[cite: 19]         | `EventCategoryApplication`[cite: 20]                    | The native application OS frame is closed or terminated[cite: 20].              |
| `WindowResize`[cite: 19]        | `EventCategoryApplication`[cite: 20]                    | The host display canvas size changes pixel width/height[cite: 20].              |
| `KeyPressed`[cite: 19]          | `EventCategoryKeyboard \| EventCategoryInput`[cite: 21] | A keyboard switch is engaged or held down (generating repeat states)[cite: 21]. |
| `KeyReleased`[cite: 19]         | `EventCategoryKeyboard \| EventCategoryInput`[cite: 21] | An active keyboard switch is released by the user[cite: 21].                    |
| `KeyTyped`[cite: 19]            | `EventCategoryKeyboard \| EventCategoryInput`[cite: 21] | Character input text is processed (ideal for text field text logic)[cite: 21].  |
| `MouseButtonPressed`[cite: 19]  | `EventCategoryMouse \| EventCategoryInput`[cite: 19]    | A peripheral mouse button is clicked down[cite: 19].                            |
| `MouseButtonReleased`[cite: 19] | `EventCategoryMouse \| EventCategoryInput`[cite: 19]    | An active peripheral mouse button click is released[cite: 19].                  |
| `MouseMoved`[cite: 19]          | `EventCategoryMouse \| EventCategoryInput`[cite: 19]    | The mouse cursor moves coordinates inside the focused viewport area[cite: 19].  |
| `MouseScrolled`[cite: 19]       | `EventCategoryMouse \| EventCategoryInput`[cite: 19]    | A mouse scroll wheel rotation or touchpad scroll gesture occurs[cite: 19].      |

#### 2. Event Base Functionality (`Cosmic::Event`)

The abstract blueprint interface for all messaging blocks routing through the operational pipeline[cite: 19].

| Public Method / Field Prototype                    | Description                                                                                         | Practical Code Example                            |
| :------------------------------------------------- | :-------------------------------------------------------------------------------------------------- | :------------------------------------------------ |
| `bool Handled`[cite: 19]                           | State variable tracking if propagation should terminate immediately. Defaults to `false`[cite: 19]. | `e.Handled = true;`[cite: 19]                     |
| `virtual EventType GetEventType() const`[cite: 19] | Returns the individual `EventType` designation value tracking the event type[cite: 19].             | `if (e.GetEventType() == EventType::WindowClose)` |
| `virtual const char* GetName() const`[cite: 19]    | Debug helper returning a string presentation matching the event type signature[cite: 19].           | `CS_CORE_TRACE("Dispatched: {0}", e.GetName());`  |
| `virtual int GetCategoryFlags() const`[cite: 19]   | Gathers bitwise mask settings representing every broad category assignment[cite: 19].               | `int masks = e.GetCategoryFlags();`               |
| `bool IsInCategory(EventCategory cat)`[cite: 19]   | Validates if an instance belongs inside a structural category via bitwise `&` testing[cite: 19].    | `if (e.IsInCategory(EventCategoryInput))`         |

#### 3. Execution Routing Utilities (`Cosmic::EventDispatcher`)

A stack-allocated filtering harness matching active events against concrete callback targets[cite: 14, 19].

| Public Method Prototype                                                        | Description                                                                                         | Practical Code Example                                   |
| :----------------------------------------------------------------------------- | :-------------------------------------------------------------------------------------------------- | :------------------------------------------------------- |
| `EventDispatcher(Event& event)`[cite: 19]                                      | Constructs a tracking dispatcher instance bound directly to an active base event target[cite: 19].  | `EventDispatcher dispatcher(e);`[cite: 14]               |
| `template<typename T, typename F>`<br>`bool Dispatch(const F& func)`[cite: 19] | Safely checks template matching types. If true, down-casts data and triggers the handler[cite: 19]. | `dispatcher.Dispatch<WindowResizeEvent>(...);`[cite: 14] |

---

### Window Overview

The `Window` class serves as the direct software link between the Cosmic Engine and the underlying operating system. It abstracts window creation, platform-specific graphics contexts, and native hardware input polling into a clean, unified wrapper. Cosmic leverages GLFW to achieve a platform-agnostic layer that translates native OS window messaging loops into the engine's internal reactive event pipeline.

#### Window Architecture and Lifecycle

Every native application window instance explicitly owns its lifecycle and rendering surface through two critical private handles:

- **`GLFWwindow* m_Handle`**: A tracking handle pointer to the underlying desktop window instance managed by GLFW.

- **`GraphicsContext* m_Context`**: An abstract polymorphic interface mapping structural context behaviors to the operational execution thread. In our baseline setup, this resolves to an `OpenGLContext` instance.

During initialization, the window creates its platform-specific graphics context and runs `Init()`, which makes the context current and loads essential GPU driver hooks via GLAD. To decouple operational window metadata from raw platform handles, Cosmic structures its runtime metadata inside an internal `WindowData` container. This state container is registered as a custom GLFW user pointer callback bridge:

```cpp
struct WindowData
{
    std::string Title;
    unsigned int Width = 0;
    unsigned int Height = 0;
    bool VSync = false;
    EventCallbackFn EventCallback; // Bound to Application::OnEvent
};

```

Whenever hardware input registers or the window dimensions fluctuate, the active GLFW lambda callbacks extract this `WindowData` reference directly from the window handle, construct a corresponding Cosmic `Event`, and dispatch it up the chain.

---

### Camera System Overview

The camera infrastructure acts as the foundational coordinate system wrapper for Cosmic's 2D rendering world. It manages orthographic orthographic projections to eliminate perspective distortion—ensuring world space elements retain absolute scale proportions regardless of their depth axis coordinate settings.

#### 1. The Matrix Math Matrix Blueprint (`OrthographicCamera`)

The structural camera relies on strict matrix operations to position scenes within rendering viewports. It maintains three key 4x4 transform matrices inside `OrthographicCamera.h`:

- **Projection Matrix ($P$)**: Defines the frustum boundary constraints (`left`, `right`, `bottom`, `top`).

- **View Matrix ($V$)**: Represents the inverse spatial transform of the camera. In graphics development, cameras are stationary mathematical definitions; to simulate movement, the world is transformed in the exact opposite direction.

- **View-Projection Matrix ($VP$)**: The pre-multiplied matrix configuration ($VP = P \times V$) updated on the CPU and pushed to the GPU vertex shaders to position objects into scene coordinates.

```cpp
// Internal math translation whenever position or rotation is altered
void OrthographicCamera::UpdateViewMatrix()
{
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));

    m_ViewMatrix = glm::inverse(transform);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix; // Ready for shader upload
}

```

#### 2. Proportional Input Wrappers (`OrthographicCameraController`)

To facilitate smooth viewport control, Cosmic provides an automated utility class named `OrthographicCameraController`. This component intercepts inbound scroll or sizing events (`MouseScrolledEvent`, `WindowResizeEvent`) and controls frame-rate independent camera tracking. It features key integration safeguards:

- **Proportional Movement Speed**: The translation speed is multiplied by the active zoom coefficient ($Speed_{\text{actual}} = Speed_{\text{base}} \times Zoom$). This keeps movement handling feeling uniform and fine-tuned, whether zoomed out across a large scene or zoomed in closely on a pixel element.

- **Asymptotic Zoom Interpolation**: Rather than snapping raw position coordinates instantly upon mouse-wheel updates, the controller tracks a target zoom layer and smoothly interpolates the view matrix using delta-time step thresholds to prevent frame hiccups.

- **Aspect Ratio Synchronization**: When windows or custom ImGui viewports fluctuate in size, the controller recomputes coordinate boundaries automatically, protecting aspects against horizontal or vertical image stretching.

---

### Framebuffer Architecture

A critical feature of Cosmic's modern rendering setup is the ability to handle off-screen rendering targets through the `FrameBuffer` base interface. Instead of forcing draw commands to print straight to the primary display buffer, developers can reroute the graphics output pipeline into a GPU-resident color texture.

This abstract pipeline layer forms the cornerstone of the engine's Editor/Sandbox UI configuration. It enables complex viewports to render directly within an isolated ImGui container, allows for custom post-processing filter logic, and supports dynamic downsampling or upscaling independent of native monitor display resolutions.

#### Allocation Lifecycles and Viewport Management

When an `OpenGLFrameBuffer` instance is constructed, it executes an internal configuration function called `Invalidate()`. This routine handles the execution loops required to generate hardware resources:

1. **Resource Reset**: Purges old color texture allocations and depth buffers from the GPU memory to prevent memory leaks during real-time resolution resizing.

2. **FBO Generation**: Requests a unique Framebuffer Object ID from the active graphics driver (`glGenFramebuffers`).

3. **Color Canvas Texture**: Allocates an explicit texture resource matching the user specification parameters—typically a standard 24-bit `GL_RGBA8` linear filter block.

4. **Depth & Stencil Attachment**: Chains an auxiliary `GL_DEPTH24_STENCIL8` render buffer target texture alongside the color target. This step is vital for ensuring precise Z-testing and masking evaluation during off-screen scene rendering.

```cpp
// Activating an off-screen viewport surface target
void OpenGLFrameBuffer::Bind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
    // CRITICAL: Remap the hardware viewport boundaries to fit the FBO specifications!
    glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

```

Whenever a viewport window minimizes or stretches, client layers call `Resize(w, h)`. This function updates the underlying specification properties and forces a call to `Invalidate()`. This workflow ensures texture buffers perfectly match viewport dimensions, eliminating visual sampling artifacts and texture distortion.

---

### Core Window, Camera, & Framebuffer API Reference

#### 1. Core Window Management Interface (`Cosmic::Window`)

Abstract boundary contract managing native desktop windows, operating system polling loops, and frame buffer presenting switches[cite: 19].

| Public Method / Accessor Prototype                           | Description                                                                                                 | Practical Client Code Context Example                              |
| :----------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------- |
| `unsigned int GetWidth() const`[cite: 19]                    | Returns the current cached pixel width of the active window surface[cite: 14, 19].                          | `uint32_t width = window->GetWidth();`                             |
| `unsigned int GetHeight() const`[cite: 19]                   | Returns the current cached pixel height of the active window surface[cite: 14, 19].                         | `uint32_t height = window->GetHeight();`                           |
| `void SetEventCallback(const EventCallbackFn& cb)`[cite: 19] | Registers the target function destination responsible for intercepting window events[cite: 14, 19].         | `window->SetEventCallback(CS_BIND_FN(App::OnEvent));`              |
| `void SetVSync(bool enabled)`[cite: 19]                      | Toggles vertical sync refresh alignment via the active graphics context backend[cite: 14, 19].              | `window->SetVSync(true); // Cap at display refresh rate`[cite: 14] |
| `bool ShouldClose() const`[cite: 19]                         | Queries the underlying window driver layer to see if an OS exit signal was triggered[cite: 14, 19].         | `while (!m_Window->ShouldClose()) { RunLoop(); }`                  |
| `void GetSize(int* w, int* h) const`[cite: 19]               | Queries the platform wrapper directly for the precise real-time framebuffer dimension bounds[cite: 14, 19]. | `m_Window->GetSize(&fbWidth, &fbHeight);`                          |

#### 2. Orthographic Camera Instance (`Cosmic::OrthographicCamera`)

A structural 2D camera system managing projection transformations and shader matrix parameters[cite: 19].

| Public Method / Accessor Prototype                                 | Description                                                                                          | Practical Client Code Context Example                         |
| :----------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------- | :------------------------------------------------------------ |
| `void SetProjection(float l, float r, float b, float t)`[cite: 19] | Redefines the camera's view boundaries and updates the projection metrics[cite: 18, 19].             | `camera.SetProjection(-aspect, aspect, -1.0f, 1.0f);`         |
| `void SetPosition(const glm::vec3& pos)`[cite: 19]                 | Manually overrides the camera's world coordinates and recalculates the View matrix[cite: 19].        | `camera.SetPosition({ 5.0f, -2.0f, 0.0f });`                  |
| `void SetRotation(float rotation)`[cite: 19]                       | Adjusts the flat rotation angle around the Z-axis vector (specified in degrees)[cite: 18, 19].       | `camera.SetRotation(45.0f); // Rotate view counter-clockwise` |
| `const glm::mat4& GetViewProjectionMatrix() const`[cite: 19]       | Returns the pre-multiplied View-Projection ($P \times V$) matrix ready for shader use[cite: 18, 19]. | `auto mvp = camera.GetViewProjectionMatrix();`                |

#### 3. High-Level Camera Controller Wrapper (`Cosmic::OrthographicCameraController`)

Automated input handling script wrapper implementing aspect ratio synchronization and proportional smooth zoom interpolation[cite: 21].

| Public Method / Accessor Prototype                   | Description                                                                                                     | Practical Client Code Context Example                         |
| :--------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------ |
| `void OnUpdate(float ts)`[cite: 21]                  | Updates camera positioning via WASD input tracking and handles smooth zoom damping interpolation[cite: 20, 21]. | `m_CameraController.OnUpdate(frameTimeStep);`                 |
| `void OnResize(float width, float height)`[cite: 21] | Forces an aspect ratio correction update to prevent visual object stretching[cite: 20, 21].                     | `m_CameraController.OnResize(viewportWidth, viewportHeight);` |
| `void SetZoomLevel(float level)`[cite: 21]           | Instantly snaps both target targets and active tracking zooms to a hard override value[cite: 20, 21].           | `m_CameraController.SetZoomLevel(1.5f);`                      |
| `void SetZoomLimits(float min, float max)`[cite: 21] | Defines the minimum and maximum boundaries for mouse-wheel zooming[cite: 20, 21].                               | `m_CameraController.SetZoomLimits(0.5f, 5.0f);`               |

#### 4. Off-Screen Framebuffer Resource (`Cosmic::FrameBuffer`)

Abstract GPU frame surface interface managing custom drawing canvases and viewport graphics texture attachments[cite: 19].

| Public Method / Accessor Prototype                        | Description                                                                                            | Practical Client Code Context Example                             |
| :-------------------------------------------------------- | :----------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------- |
| `void Bind()`[cite: 19]                                   | Redirects the graphics pipeline target into this off-screen buffer allocation[cite: 19, 25].           | `m_FrameBuffer->Bind(); // Start scene pass`                      |
| `void Unbind()`[cite: 19]                                 | Resets the active draw target buffer back to the native desktop platform window context[cite: 19, 25]. | `m_FrameBuffer->Unbind(); // Resume desktop UI render`            |
| `void Resize(uint32_t width, uint32_t height)`[cite: 19]  | Re-allocates underlying GPU texture attachments to match new display dimensions[cite: 19, 25].         | `m_FrameBuffer->Resize(newWidth, newHeight);`                     |
| `uint32_t GetColorAttachmentRendererID() const`[cite: 19] | Returns the raw backend GPU texture identifier handle (e.g., OpenGL texture ID)[cite: 19, 25].         | `uint32_t texID = m_FrameBuffer->GetColorAttachmentRendererID();` |

---

# Other... Cosmic Engine - Developer API & Useful Functions

## General Useful Information

### Memory Management & Resource Ownership

Cosmic wraps standard C++ smart pointers into concise engine-wide semantics to enforce explicit memory ownership rules and eliminate raw pointer tracking:

- `Scope<T>`: An alias for `std::unique_ptr<T>`. Used for strict, single-ownership allocations (such as windows, unique sub-modules, or dedicated single-layer stacks).
- `Ref<T>`: An alias for `std::shared_ptr<T>`. Used for shared resources that multiple objects point to simultaneously (such as texture maps, base shaders, and shared asset materials).

To instantiate these wrappers, always use the accompanying factory templates:

```cpp
Scope<MyClass> uniqueObj = CreateScope<MyClass>(arguments);
Ref<Texture2D> sharedTex = CreateRef<Texture2D>("path/to/asset.png");
```

---

# OpenGL Implementations

## OpenGL Shader

The `OpenGLShader` class is the concrete implementation of the `Shader` interface for the OpenGL graphics backend[cite: 2]. It encapsulates the complete GPU program lifecycle—from reading raw GLSL files from disk to multi-stage preprocessing, compilation, error diagnostics, and automated hardware resource tracking[cite: 2, 3].

### Key Features

- **Single-File Multi-Stage Parsing:** Automatically separates vertex and fragment shader source code within a single `.glsl` file using the custom `#type` directive (e.g., `#type vertex` or `#type fragment`)[cite: 2, 3].
- **Shadertoy Compatibility:** Seamlessly interprets and wraps raw pixel shaders utilizing Shadertoy syntax (e.g., `mainImage`, `iTime`, `iResolution`) without requiring manual setup wrappers[cite: 3].
- **Automatic Uniform Injection:** Dynamically injects engine preambles and standard uniforms (such as `u_Time`, `u_ViewportSize`, and `u_ViewProjection`) if they are used but not declared in the source code[cite: 3].
- **Dynamic Sampler Batching:** Queries the host GPU hardware limits at runtime to automatically map texture unit arrays (`u_Textures`) up to 32 slots for 2D batch rendering[cite: 3].
- **Location Caching Gateway:** Prevents expensive, repeating driver lookups by caching uniform string-to-location mappings internally[cite: 2, 3].

---

### Reference Table: Core Shader Functions

| Function Prototype                                                                       | Pre-Conditions                                                                                                            | Post-Conditions / Side Effects                                                                                               |
| :--------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------ | :--------------------------------------------------------------------------------------------------------------------------- |
| **`OpenGLShader(const std::string& filepath)`**[cite: 2]                                 | `filepath` must point to a valid, reachable GLSL file containing `#type` definitions or Shadertoy signatures[cite: 2, 3]. | Reads, preprocesses, compiles, and links the source code into a valid GPU program ID. Caches the file stem name[cite: 2, 3]. |
| **`~OpenGLShader()`**[cite: 2]                                                           | The `OpenGLShader` instance and its underlying OpenGL graphics context must be alive[cite: 1, 2].                         | Permanently deletes the compiled shader program resource from GPU memory (`glDeleteProgram`)[cite: 2].                       |
| **`void Bind() const`**[cite: 1, 2]                                                      | A valid GPU shader program must be successfully linked and allocated (`m_RendererID` != 0)[cite: 2, 3].                   | Activates this shader program in the active OpenGL state machine for upcoming draw calls[cite: 2, 3].                        |
| **`void Unbind() const`**[cite: 1, 2]                                                    | None.                                                                                                                     | Deactivates the current shader pipeline by restoring the active OpenGL program state to `0`[cite: 2, 3].                     |
| **`void SetInt(const std::string& name, int value)`**[cite: 1, 2]                        | The shader program should be bound for reliable execution. `name` must match a uniform in the shader[cite: 2, 3].         | Uploads a single integer value to the targeted GPU uniform register[cite: 2, 3].                                             |
| **`void SetIntArray(const std::string& name, int* values, uint32_t count)`**[cite: 1, 2] | The shader should be active. `values` must point to an array matching or exceeding `count` elements[cite: 2, 3].          | Streams an array of integer values (e.g., sampler indexing bounds) directly to the shader uniform array[cite: 2, 3].         |
| **`void SetFloat(const std::string& name, float value)`**[cite: 1, 2]                    | The shader program should be bound. `name` must be present in the shader source[cite: 2, 3].                              | Uploads a single floating-point scalar value to the GPU[cite: 2, 3].                                                         |
| **`void SetFloat2(const std::string& name, const glm::vec2& value)`**[cite: 1, 2]        | The shader program should be bound.                                                                                       | Uploads a 2-component float vector (`glm::vec2`), typically used for viewports or positions[cite: 2, 3].                     |
| **`void SetFloat3(const std::string& name, const glm::vec3& value)`**[cite: 1, 2]        | The shader program should be bound.                                                                                       | Uploads a 3-component float vector (`glm::vec3`), ideal for RGB color tokens or 3D positions[cite: 2, 3].                    |
| **`void SetFloat4(const std::string& name, const glm::vec4& value)`**[cite: 1, 2]        | The shader program should be bound.                                                                                       | Uploads a 4-component float vector (`glm::vec4`), used frequently for RGBA colors or clip space vectors[cite: 2, 3].         |
| **`void SetMat3(const std::string& name, const glm::mat3& value)`**[cite: 1, 2]          | The shader program should be bound.                                                                                       | Uploads a $3 \times 3$ transformation matrix matrix to the GPU shader registers[cite: 2, 3].                                 |
| **`void SetMat4(const std::string& name, const glm::mat4& value)`**[cite: 1, 2]          | The shader program should be bound.                                                                                       | Uploads a $4 \times 4$ transformation matrix (e.g., model-view-projection matrices) to the GPU[cite: 2, 3].                  |

> **Note on Memory Ownership:** Copy constructors and assignment operators are explicitly `delete`d for this class[cite: 2]. This prevents dangerous shallow copies of GPU resource handles, which can cause premature asset destruction or dangling reference crashes when copies fall out of scope[cite: 2]. Always wrap your shader instances inside reference-counted engine pointer handles (e.g., `Ref<Shader>`)[cite: 1].
