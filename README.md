Coming soon
![UML Diagram (Main Engine)](CosmicUML.png)



# Cosmic Engine — Developer Reference

> **How to use this document:** Each section is self-contained. You can jump to any topic and understand it fully without reading the rest of the document. Cross-references are provided where one system interacts with another.

---

## Table of Contents

1. [Memory Management & Smart Pointers](#1-memory-management--smart-pointers)
2. [Application Lifecycle & The Main Loop](#2-application-lifecycle--the-main-loop)
3. [Layer System](#3-layer-system)
4. [Event System](#4-event-system)
5. [Input Polling](#5-input-polling)
6. [Virtual File System](#6-virtual-file-system)
7. [Shaders & GLSL Preprocessing](#7-shaders--glsl-preprocessing)
8. [2D Rendering — Immediate API](#8-2d-rendering--immediate-api)
9. [Entity Component System (ECS)](#9-entity-component-system-ecs)
10. [Camera System](#10-camera-system)
11. [Framebuffer](#11-framebuffer)
12. [Window Management](#12-window-management)
13. [Logging & Diagnostics](#13-logging--diagnostics)
14. [Dynamic DLL Plugin System](#14-dynamic-dll-plugin-system)
15. [Low-Level Render Commands](#15-low-level-render-commands)

---

## 1. Memory Management & Smart Pointers

**What it is:** Cosmic wraps standard C++ smart pointers into two engine-wide aliases to enforce explicit ownership rules across subsystems and DLL boundaries.

| Alias | Underlying Type | When to Use |
| :---- | :-------------- | :---------- |
| `Scope<T>` | `std::unique_ptr<T>` | Single-owner allocations: windows, layers, dedicated sub-modules. Only one system holds and destroys this resource. |
| `Ref<T>` | `std::shared_ptr<T>` | Shared resources referenced by multiple subsystems simultaneously: textures, shaders, materials. |

**Always use the factory templates** to instantiate these — never call `new` directly into a smart pointer:

```cpp
// Single-owner allocation
Scope<Window> myWindow = CreateScope<Window>(1280, 720, "My App");

// Shared resource
Ref<Texture2D> myTexture = CreateRef<Texture2D>("assets/sprite.png");
// or, using the class's own static factory (preferred for engine types):
Ref<Texture2D> myTexture = Texture2D::Create("assets/sprite.png");
```

**Why this matters across DLL boundaries:** When game logic compiles into a separate `.dll`, passing raw pointers across that boundary is safe as long as the *host* application (the engine) owns the lifetime. `Ref<T>` handles shared ownership cleanly as long as both sides link against the same runtime. `Scope<T>` is used for systems that should never be shared.

> **Core macro helpers** (defined in `core/Core.h`):
> - `BIT(x)` — Produces a bitwise flag: `BIT(3)` evaluates to `8`. Used for event category masks.
> - `GLCORE_ASSERT(condition, msg)` — Debug-only assertion. Triggers a hardware breakpoint if `condition` is false.
> - `GLCORE_BIND_EVENT_FN(fn)` — Wraps `std::bind` to connect member functions to the event dispatcher (see [Event System](#4-event-system)).

---

## 2. Application Lifecycle & The Main Loop

**What it is:** `Application` is the engine's root singleton. It owns all subsystems, drives the frame loop, and manages safe transitions between engine states (Launcher ↔ Workspace).

### Startup Sequence

When `Application` is constructed, `Initialize()` runs in this order:

1. Create the `Window` (GLFW + OpenGL context)
2. Initialize the `Renderer` (sets up GPU pipeline state)
3. Create the `FrameBuffer` (off-screen render target)
4. Push `ImGuiLayer` as an overlay
5. Push `LauncherLayer` as the default entry point

### The Frame Loop (`Application::Run()`)

```
[Every Frame]
       │
       ▼
 PollEvents()          ← Grab OS hardware signals
       │
       ▼
 ┌─────────────────────────────────────────────────┐
 │  PASS 1A — Fixed Timestep (if enabled)          │
 │  accumulator += frameTime * m_TimeScale         │
 │  while (accumulator >= 1/60s):                  │
 │      layer->OnFixedUpdate(1/60s)  ← All layers  │
 │      accumulator -= 1/60s                       │
 └─────────────────────────────────────────────────┘
       │
       ▼
 ┌─────────────────────────────────────────────────┐
 │  PASS 1B — Variable Timestep                    │
 │  layer->OnUpdate(scaledDeltaTime)  ← All layers │
 └─────────────────────────────────────────────────┘
       │
       ▼
 ┌─────────────────────────────────────────────────┐
 │  PASS 2 — ImGui UI Render                       │
 │  ImGuiLayer::Begin()                            │
 │  layer->OnImGuiRender()  ← All layers           │
 │  ImGuiLayer::End()                              │
 └─────────────────────────────────────────────────┘
       │
       ▼
 SwapBuffers()
       │
       ▼
 ╔═════════════════════════════════════════════════╗
 ║  THE SAFE ZONE                                  ║
 ║  (No iterators active on m_LayerStack here)     ║
 ║  • Process pending DLL loads/unloads            ║
 ║  • Handle launcher ↔ workspace transitions      ║
 ║  • Delete unmanaged layer allocations safely    ║
 ╚═════════════════════════════════════════════════╝
```

### Key Loop Mechanics

**Fixed Timestep (Pass 1A):** When `m_UseFixedTimestep` is `true`, physics and deterministic logic run at a locked 60 Hz regardless of GPU frame rate. An accumulator tracks leftover time between frames.

- **Spiral-of-Death Protection:** If a frame takes longer than `0.25s` (e.g., during a debugger breakpoint), `frameTime` is clamped to `0.25s`. This prevents the engine from trying to catch up on hundreds of physics ticks at once, which would cause a freeze cascade.

**Variable Timestep (Pass 1B):** Visual updates and gameplay logic that don't require determinism use `OnUpdate(dt)`, where `dt` is the real frame time multiplied by `m_TimeScale`. Set `m_TimeScale` to values other than `1.0f` to create slow-motion or fast-forward effects for debugging.

**The Safe Zone:** Modifying the `LayerStack` (pushing, popping, or deleting layers) while an iterator is active over it will corrupt memory. The bottom of `Run()` is a guaranteed iterator-free window where all deferred layer operations execute safely.

### Application Control API

```cpp
// Access the singleton from anywhere in the engine
Application& app = Cosmic::Application::Get();

// Timing control
app.UseFixedTimeStep(true);       // Enable/disable 60Hz physics pass
app.SetTimeScale(0.5f);           // Half-speed (slow motion debug)

// State transitions (queued for the Safe Zone)
app.TransitionFromLauncherToWorkspace("MyProject.dll");
app.TransitionToLauncher();

// Shutdown
app.Close(); // Sets m_Running = false; exits loop gracefully
```

### Shutdown Sequence

`Application::Shutdown()` follows a strict ordered teardown to prevent GPU resource leaks:

1. Unload any active project DLL
2. Pop the ImGui overlay (prevents double-delete of `Scope<ImGuiLayer>`)
3. Snapshot remaining unmanaged layer pointers
4. Clear the `LayerStack` immediately (prevents stale iterator access)
5. Call `delete` on each snapshotted layer (triggers destructors while OpenGL context is alive)
6. Reset `m_ImGuiLayer`, call `Renderer::Shutdown()`, reset `m_Window`

> **Critical:** GPU resources (textures, shaders, framebuffers) must be destroyed *before* the OpenGL context closes. The ordered shutdown above guarantees this.

---

## 3. Layer System

**What it is:** A `Layer` is the fundamental building block of engine logic. Every game world, editor panel, or UI system is a `Layer`. They are managed by the `LayerStack`, which controls update order and event propagation order.

### Creating a Layer

Inherit from `Cosmic::Layer` and override whichever hooks you need:

```cpp
class MyGameLayer : public Cosmic::Layer
{
public:
    MyGameLayer() : Layer("MyGameLayer") {}

    void OnAttach() override
    {
        // Called when pushed onto the LayerStack.
        // Load textures, create scenes, initialize buffers here.
        m_Scene = Cosmic::Scene::Create();
    }

    void OnDetach() override
    {
        // Called when popped from the LayerStack or during shutdown.
        // Release resources: reset Ref<> handles, close file handles.
        m_Scene.reset();
    }

    void OnUpdate(float deltaTime) override
    {
        // Called once per frame. Use deltaTime (seconds) to keep
        // logic frame-rate independent.
        m_Player.Position.x += m_Speed * deltaTime;
    }

    void OnFixedUpdate(float deltaFixedTime) override
    {
        // Called at a fixed 60Hz interval. Use for physics, serial I/O,
        // or anything that breaks under variable frame rates.
    }

    void OnRender() override
    {
        // Submit draw calls here. Called within the variable update pass.
        // (For most 2D layers, rendering is done inside OnUpdate.)
    }

    void OnImGuiRender() override
    {
        // ImGui calls go here, isolated from the main render pass.
        ImGui::Begin("My Panel");
        ImGui::Text("Hello from MyGameLayer!");
        ImGui::End();
    }

    void OnEvent(Cosmic::Event& e) override
    {
        // Handle input or window events. See the Event System section.
    }
};
```

### LayerStack Ordering

The `LayerStack` holds two conceptual zones: **Layers** (game logic, scenes) and **Overlays** (ImGui, debug UI). Overlays always sit on top of layers.

```
Index:  [0]  [1]  ...  [N-1] [N]
        ┌─────────────────────────┐
        │ Layer  Layer  ...  Over │
        └─────────────────────────┘
                          ▲
                     m_LayerInsertIndex boundary

Update/Render order:  Left → Right  (index 0 first)
Event propagation:    Right → Left  (overlays see events first)
```

**Pushing layers:**

```cpp
// Game logic layers (inserted before overlays)
Application::Get().PushLayer(new MyGameLayer());

// UI overlays (always on top, see events first)
Application::Get().PushOverlay(new MyDebugOverlay());
```

> **Memory note:** The `LayerStack` does *not* own the layer pointers — it only borrows them. The `Application` class is the true owner and is responsible for `delete`-ing them. Never delete a layer pointer yourself unless you know it isn't registered in the stack.

---

## 4. Event System

**What it is:** A reactive, top-down event architecture. When the OS fires an input signal or window change, the engine packages it into a typed `Event` object and routes it through the `LayerStack` from top to bottom. Any layer can consume an event to stop it from reaching layers below.

### Event Flow Diagram

```
[OS Hardware Signal]
         │
         ▼
Application::OnEvent(Event& e)
         │
         ├──► Dispatch<WindowCloseEvent>   → Application::OnWindowClose
         ├──► Dispatch<WindowResizeEvent>  → Application::OnWindowResize
         │
         ▼  (remaining events propagate down the stack)
  ┌──────────────────────┐
  │  WorkspaceLayer      │  ← Topmost overlay sees it first
  └──────────┬───────────┘
             │  e.Handled == true? → STOP propagation
             ▼
  ┌──────────────────────┐
  │  Active Plugin Layer │  ← Your game DLL layer
  └──────────┬───────────┘
             │  e.Handled == true? → STOP propagation
             ▼
  ┌──────────────────────┐
  │  ...lower layers...  │
  └──────────────────────┘
```

### Handling Events in Your Layer

Use `EventDispatcher` with `GLCORE_BIND_EVENT_FN` to route specific event types to dedicated handler methods:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPressed));

    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnMouseClicked));
}

// Handler must return bool: true = consumed, false = keep propagating
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    if (e.GetKeyCode() == CS_KEY_ESCAPE)
    {
        TogglePauseMenu();
        return true; // Consumed — stops propagating down the stack
    }
    return false; // Not consumed — let lower layers handle it
}
```

**Consuming an event to block lower layers:**

```cpp
void EditorUILayer::OnEvent(Cosmic::Event& e)
{
    // If the mouse is over an ImGui panel, eat the click so the
    // game world doesn't fire a weapon or move the camera behind the UI.
    if (m_PanelIsHovered && e.IsInCategory(Cosmic::EventCategoryMouse))
    {
        e.Handled = true;
    }
}
```

### Event Types Reference

| EventType | Category | Trigger |
| :-------- | :------- | :------ |
| `WindowClose` | `EventCategoryApplication` | OS window is closed or terminated |
| `WindowResize` | `EventCategoryApplication` | Window pixel dimensions change |
| `KeyPressed` | `EventCategoryKeyboard \| EventCategoryInput` | Key pressed or held (generates repeats) |
| `KeyReleased` | `EventCategoryKeyboard \| EventCategoryInput` | Key released |
| `KeyTyped` | `EventCategoryKeyboard \| EventCategoryInput` | Text character input (for text fields) |
| `MouseButtonPressed` | `EventCategoryMouse \| EventCategoryInput` | Mouse button clicked down |
| `MouseButtonReleased` | `EventCategoryMouse \| EventCategoryInput` | Mouse button released |
| `MouseMoved` | `EventCategoryMouse \| EventCategoryInput` | Cursor moved inside the viewport |
| `MouseScrolled` | `EventCategoryMouse \| EventCategoryInput` | Scroll wheel or touchpad scroll |

### Event Base API

| Method / Field | Description |
| :------------- | :---------- |
| `bool Handled` | Set to `true` to stop propagation. Defaults to `false`. |
| `EventType GetEventType() const` | Returns the specific event type enum value. |
| `int GetCategoryFlags() const` | Returns bitwise category mask. |
| `bool IsInCategory(EventCategory cat)` | Checks membership via bitwise `&`. |

### EventDispatcher API

| Method | Description |
| :----- | :---------- |
| `EventDispatcher(Event& e)` | Binds to an incoming event reference. |
| `bool Dispatch<T>(const F& func)` | If event type matches `T`, casts and calls `func`. Returns `true` on match. |

---

## 5. Input Polling

**What it is:** A static, immediate-mode input system for querying hardware state *right now*, bypassing the event queue. Use this for continuous actions (movement, camera control) rather than one-shot reactions (which use the event system).

### When to Use Polling vs. Events

| Use Polling (`Input::`) | Use Events (`OnEvent`) |
| :---------------------- | :--------------------- |
| "Is W held down this frame?" | "Did the player just press Escape?" |
| Character movement, camera pan | Menu toggle, weapon fire |
| Anything checked every frame | One-shot state changes |

### Usage Example

```cpp
void SpaceGameLayer::OnUpdate(float ts)
{
    // Keyboard: continuous movement
    if (Cosmic::Input::IsKeyPressed(CS_KEY_W))
        m_ShipPosition.y += m_Thrust * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_S))
        m_ShipPosition.y -= m_Thrust * ts;

    // Mouse: click position
    if (Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
    {
        glm::vec2 pos = Cosmic::Input::GetMousePosition();
        FireProjectileToward(pos);
    }
}
```

### Input API Reference

| Function | Returns | Description |
| :------- | :------ | :---------- |
| `Input::IsKeyPressed(int keycode)` | `bool` | `true` if key is currently pressed or repeating |
| `Input::IsMouseButtonPressed(int button)` | `bool` | `true` if mouse button is currently held |
| `Input::GetMousePosition()` | `glm::vec2` | Cursor position in screen-space (origin = top-left) |
| `Input::GetMouseX()` | `float` | Horizontal cursor coordinate only |
| `Input::GetMouseY()` | `float` | Vertical cursor coordinate only |

### Key Code Constants (`codes/KeyCodes.h`)

| Constant | Value | Group |
| :------- | :---- | :---- |
| `CS_KEY_SPACE` | 32 | Standard layout |
| `CS_KEY_A` … `CS_KEY_Z` | 65–90 | Alphabet |
| `CS_KEY_0` … `CS_KEY_9` | 48–57 | Number row |
| `CS_KEY_ESCAPE` | 256 | System / navigation |
| `CS_KEY_LEFT`, `CS_KEY_RIGHT`, `CS_KEY_UP`, `CS_KEY_DOWN` | 263, 262, 265, 264 | Arrow keys |
| `CS_KEY_LEFT_CONTROL` | 341 | Modifier |
| `CS_KEY_LEFT_SHIFT` | 340 | Modifier |

### Mouse Button Constants (`codes/MouseButtonCodes.h`)

| Constant | Alias / Value | Use |
| :------- | :------------ | :-- |
| `CS_MOUSE_BUTTON_LEFT` | `CS_MOUSE_BUTTON_1` (0) | Primary action |
| `CS_MOUSE_BUTTON_RIGHT` | `CS_MOUSE_BUTTON_2` (1) | Context / inspect |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` (2) | Pan / scroll |

---

## 6. Virtual File System

**What it is:** A static path-resolution utility (`Cosmic::FileSystem`) that maps short protocol strings to real disk paths. This keeps asset references consistent across project swaps, build configurations, and deployment directories.

### Path Protocols

| Protocol | Resolves To |
| :------- | :---------- |
| `engine://filename` | `assets/filename` |
| `project://filename` | `assets/projects/<ActiveProjectName>/filename` |

```
"project://textures/sky.png"
            │
            ▼
   FileSystem::Resolve()
            │
   (s_ActiveProjectName = "MySandbox")
            │
            ▼
"assets/projects/MySandbox/textures/sky.png"
            │
            ▼
   Physical disk path (copied by CMake post-build)
   → build/Runtime/<Config>/assets/projects/MySandbox/textures/sky.png
```

### Usage

```cpp
// Set the active project once at startup
Cosmic::FileSystem::SetActiveProject("DinoProject");

// Resolve virtual paths to real paths before passing to loaders
std::string texPath    = Cosmic::FileSystem::Resolve("project://Dino.png");
std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl");
std::string engineFont = Cosmic::FileSystem::Resolve("engine://fonts/Default.ttf");

// Always verify assets exist before allocating GPU resources
if (!std::filesystem::exists(texPath))
{
    CS_ERROR("Asset not found: {0}", texPath);
    return;
}
auto texture = Cosmic::Texture2D::Create(texPath);
```

### CMake Asset Deployment

Assets are copied to the build output via a CMake `POST_BUILD` command. Your project's `CMakeLists.txt` should include something like:

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets"
    COMMENT "Syncing assets to output directory..."
)
```

> **Required directory structure:** Game module assets must live under `assets/projects/<YourProjectName>/` for the VFS protocol to resolve them correctly at runtime.

---

## 7. Shaders & GLSL Preprocessing

**What it is:** Cosmic uses a single-file `.glsl` shader format with a custom preprocessor. Instead of maintaining separate vertex and fragment files, one `.glsl` file holds all stages, separated by `#type` directives. The preprocessor also handles automatic uniform injection and Shadertoy compatibility.

### Single-File Format

```glsl
#type vertex
#version 450 core

layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec2  a_TexCoord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord  = a_TexCoord;
    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 FragColor;

in vec2 v_TexCoord;

uniform sampler2D u_Texture;
uniform vec4      u_Color;

void main()
{
    FragColor = texture(u_Texture, v_TexCoord) * u_Color;
}
```

Loading a shader is a single call:

```cpp
Ref<Shader> myShader = Cosmic::Shader::Create("project://shaders/MyShader.glsl");
```

### Automatic Uniform Injection

The preprocessor scans your source for references to the following engine-managed uniforms. If you *use* them without declaring them, the engine injects the declarations automatically:

| Uniform | Type | Purpose |
| :------ | :--- | :------ |
| `u_ViewProjection` | `mat4` | Camera view-projection matrix, updated per scene |
| `u_Time` | `float` | Accumulated engine time in seconds, updated per frame |
| `u_ViewportSize` | `vec2` | Current viewport pixel dimensions |
| `u_Textures` | `sampler2D[N]` | Batch renderer texture slots (up to 32, hardware-queried) |

You do **not** need to write `uniform float u_Time;` if the preprocessor detects `u_Time` in your source.

### Shadertoy Compatibility

If the preprocessor finds a `void mainImage(out vec4 fragColor, in vec2 fragCoord)` signature in the file, it automatically:

1. Flags the file as a Shadertoy-style pixel shader
2. Injects a boilerplate vertex stage (a full-screen quad pass)
3. Maps Shadertoy input constants to engine equivalents:

| Shadertoy Uniform | Engine Source |
| :---------------- | :------------ |
| `iTime` | `u_Time` |
| `iResolution` | `u_ViewportSize` |
| `iMouse` | Mouse position (injected) |

This lets you paste Shadertoy sketches directly into the engine with minimal modification.

### OpenGLShader API Reference

The `OpenGLShader` class is the concrete backend implementation. You normally interact with it through the abstract `Shader` interface via `Ref<Shader>`.

> **Ownership rule:** `OpenGLShader` instances delete their GPU program on destruction (`glDeleteProgram`). Copy construction and assignment are disabled. Always store shaders in `Ref<Shader>`.

| Function | Pre-condition | Effect |
| :------- | :------------ | :----- |
| `Shader::Create(filepath)` | Valid `.glsl` file with `#type` or Shadertoy signature | Reads, preprocesses, compiles, and links. Returns `Ref<Shader>`. |
| `void Bind()` | Shader linked successfully | Activates this program for subsequent draw calls |
| `void Unbind()` | — | Resets active program to `0` |
| `void SetInt(name, int)` | Shader bound; name exists in source | Uploads integer uniform |
| `void SetIntArray(name, int*, count)` | Shader bound; array is at least `count` long | Uploads integer array (e.g., sampler indices) |
| `void SetFloat(name, float)` | Shader bound | Uploads scalar float |
| `void SetFloat2(name, glm::vec2)` | Shader bound | Uploads 2-component vector |
| `void SetFloat3(name, glm::vec3)` | Shader bound | Uploads 3-component vector (RGB, position) |
| `void SetFloat4(name, glm::vec4)` | Shader bound | Uploads 4-component vector (RGBA, clip space) |
| `void SetMat3(name, glm::mat3)` | Shader bound | Uploads 3×3 matrix |
| `void SetMat4(name, glm::mat4)` | Shader bound | Uploads 4×4 matrix (MVP transforms) |

**Uniform location caching:** All `SetXxx` calls cache their `glGetUniformLocation` results internally. Repeated calls for the same uniform name do not hit the driver.

---

## 8. 2D Rendering — Immediate API

**What it is:** `Renderer2D` provides a static, immediate-mode API for submitting 2D geometry directly to the GPU without going through the ECS. It manages internal vertex/index staging buffers, texture batching, and camera projection. Use this for procedural backgrounds, debug overlays, wireframes, or any geometry that isn't attached to an ECS entity.

> **For ECS-driven rendering**, see [Section 9](#9-entity-component-system-ecs). Both paradigms submit through `Renderer2D` internally.

### Frame Structure

Every render pass must be wrapped in `BeginScene` / `EndScene`:

```cpp
void MyLayer::OnRender()
{
    // 1. Start a scene — caches camera matrices, resets batch counters
    Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

    // 2. Submit geometry (all calls between Begin and End are batched)
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f});
    Cosmic::Renderer2D::DrawRotatedQuad(m_Pos, m_Scale, m_Angle, m_Material);

    // 3. End scene — flushes all batched geometry to GPU
    Cosmic::Renderer2D::EndScene();
}
```

Call `UpdateTimeline` once per frame before `BeginScene` if your shaders use `u_Time` or `u_ViewportSize`:

```cpp
Cosmic::Renderer2D::UpdateTimeline(deltaTime, viewportWidth, viewportHeight);
```

### Draw API Reference

| Function | Description |
| :------- | :---------- |
| `BeginScene(const OrthographicCamera& camera)` | Starts a new batch pass. Uploads camera VP matrix, resets vertex/index offsets. |
| `EndScene()` | Finalizes and flushes all staged geometry to the GPU. |
| `UpdateTimeline(float ts, uint32_t w, uint32_t h)` | Advances `u_Time` and updates `u_ViewportSize` for shader uniforms. |
| `DrawQuad(position, size, color)` | Submits a flat-color quad to the batch. No texture. |
| `DrawQuad(position, size, Ref<Material>)` | Submits a material-shaded quad. Texture state is handled by the material. |
| `DrawRotatedQuad(position, size, rotationRadians, color)` | Same as `DrawQuad` with a Z-axis rotation applied. |
| `DrawRotatedQuad(position, size, rotationRadians, Ref<Material>)` | Material-shaded rotated quad. |
| `DrawLine(p0, p1, color)` | Batches a non-indexed line segment between two world-space points. |
| `DrawRect(position, size, color)` | Convenience wrapper that calls `DrawLine` four times to produce a wireframe box. |
| `Statistics GetStats()` | Returns a struct with `DrawCalls` and `QuadCount` from the current frame. |
| `ResetStats()` | Clears performance counters. Call at the start of each frame. |

### Example: Procedural Background + Debug Lines

```cpp
void DinoFlightLayer::OnRender()
{
    Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

    // Procedural checkerboard background
    float startX = floor(m_PlayerPos.x) - 10.0f;
    float startY = floor(m_PlayerPos.y) - 10.0f;
    for (float x = startX; x < startX + 20.0f; x += 1.0f)
    {
        for (float y = startY; y < startY + 20.0f; y += 1.0f)
        {
            bool even   = (int(x) + int(y)) % 2 == 0;
            glm::vec4 c = even ? glm::vec4(0.2f, 0.2f, 0.25f, 1.f)
                               : glm::vec4(0.15f, 0.15f, 0.18f, 1.f);
            Cosmic::Renderer2D::DrawQuad({x, y, -0.1f}, {1.f, 1.f}, c);
        }
    }

    // Debug trail lines
    for (size_t i = 0; i + 1 < m_Trail.size(); ++i)
        Cosmic::Renderer2D::DrawLine(m_Trail[i], m_Trail[i+1], {1.f, 0.f, 0.f, 1.f});

    // Player sprite (material with texture)
    Cosmic::Renderer2D::DrawRotatedQuad(m_PlayerPos, m_PlayerScale, m_PlayerAngle, m_Material);

    Cosmic::Renderer2D::EndScene();
}
```

---

## 9. Entity Component System (ECS)

**What it is:** Cosmic integrates EnTT for data-driven entity management. Entities are handles into an `entt::registry` owned by a `Scene`. Components are plain structs attached to entities. The `Scene::OnRender()` system automatically sorts entities into material buckets before dispatching draw calls to minimize GPU state changes.

### Core Components

**`TransformComponent`** — Spatial placement in world space:

```cpp
struct TransformComponent
{
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll rotation (degrees)
    glm::vec2 Scale    { 1.f, 1.f };

    glm::mat4 GetTransform() const; // Computes TRS matrix
};
```

**`SpriteRendererComponent`** — Visual data for rendering:

```cpp
struct SpriteRendererComponent
{
    Ref<Material> ActiveMaterial;              // If set, material shader is used
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f }; // Fallback flat color (no material)
};
```

**`TagComponent`** — Debug identity string:

```cpp
struct TagComponent { std::string Tag; };
```

### Entity API

Every entity is a lightweight handle (`entt::entity` + `Scene*`). Components are managed through template methods:

```cpp
// Creating an entity
Cosmic::Entity player = m_Scene->CreateEntity("Player");

// Adding components (auto-added: TransformComponent, TagComponent)
auto& sprite  = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);
auto& physics = player.AddComponent<MyPhysicsComponent>(mass, drag);

// Reading components
auto& t = player.GetComponent<Cosmic::TransformComponent>();
t.Position = { 0.f, 2.f, 0.f };
t.Rotation.z = 45.f; // degrees

// Checking presence before access
if (player.HasComponent<MyPhysicsComponent>())
    player.GetComponent<MyPhysicsComponent>().ApplyForce({0.f, 9.8f, 0.f});

// Removing
player.RemoveComponent<MyPhysicsComponent>();

// Validity check (safe to use as bool)
if (player) { /* handle is valid */ }
```

| Method | Asserts | Description |
| :----- | :------ | :---------- |
| `AddComponent<T>(args...)` | Entity does NOT already have `T` | Constructs component in-place |
| `GetComponent<T>()` | Entity DOES have `T` | Returns reference to component |
| `HasComponent<T>()` | — | Returns `bool` |
| `RemoveComponent<T>()` | Entity DOES have `T` | Removes component from registry |
| `operator bool()` | — | `true` if handle is valid and bound to a scene |

### Scene Management

```cpp
// Create a scene
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// Drive the scene from your layer
void MyLayer::OnUpdate(float dt) { m_Scene->OnUpdate(dt); }
void MyLayer::OnRender()
{
    Cosmic::Renderer2D::BeginScene(m_Camera);
    m_Scene->OnRender(); // Handles material sorting and batch dispatch internally
    Cosmic::Renderer2D::EndScene();
}

// Cleanup
void MyLayer::OnDetach() { m_Scene.reset(); }
```

### Material Batching (How `Scene::OnRender` Works)

When `Scene::OnRender()` is called, it performs two passes to minimize GPU state changes:

1. **Material Bucket Pass:** All entities sharing the same `Material*` are grouped together and drawn in a continuous loop — one material bind per group.
2. **Flat Color Fallback Pass:** Entities with no `ActiveMaterial` are drawn together using the batch renderer's built-in white-pixel texture, tinted by `SpriteRendererComponent::Color`.

This happens automatically — you just call `m_Scene->OnRender()`.

### DLL Boundary Safety for EnTT

Because Cosmic compiles as a DLL and game projects compile into separate DLLs, EnTT's runtime type hashing can produce different IDs for the same component type across compilation units. `Components.h` fixes this by declaring explicit `consteval` hash overrides:

```cpp
namespace entt
{
    template<> struct type_hash<Cosmic::TransformComponent> final {
        [[nodiscard]] static consteval id_type value() noexcept {
            return hashed_string::value("TransformComponent");
        }
    };
    // Same pattern for TagComponent and SpriteRendererComponent
}
```

> **If you add a new component type** that crosses the DLL boundary, add a corresponding `type_hash` specialization in `Components.h` or the component will fail to look up correctly in the guest DLL's registry view.

---

## 10. Camera System

**What it is:** An orthographic camera system for 2D scenes. It provides the View-Projection matrix uploaded to shaders every frame, and an optional controller wrapper for input-driven panning and zooming.

### The Three Matrices

`OrthographicCamera` maintains three matrices:

- **Projection Matrix (P):** Defines the view frustum boundaries (`left`, `right`, `bottom`, `top`). Maps world units to clip space.
- **View Matrix (V):** The inverse of the camera's transform. Since the camera is a math abstraction (not a real object), moving the camera is achieved by transforming the world in the opposite direction.
- **View-Projection Matrix (VP = P × V):** Pre-multiplied on the CPU and uploaded to vertex shaders as `u_ViewProjection`.

```cpp
void OrthographicCamera::UpdateViewMatrix()
{
    glm::mat4 transform = glm::translate(glm::mat4(1.f), m_Position) *
                          glm::rotate(glm::mat4(1.f), glm::radians(m_Rotation), {0, 0, 1});

    m_ViewMatrix           = glm::inverse(transform);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}
```

### Direct Camera API

```cpp
// Constructing with aspect-ratio-derived bounds
float aspect = (float)width / (float)height;
Cosmic::OrthographicCamera camera(-aspect, aspect, -1.f, 1.f);

// Manual control
camera.SetPosition({ 5.f, -2.f, 0.f });
camera.SetRotation(45.f);               // Degrees, Z-axis only
camera.SetProjection(-aspect, aspect, -1.f, 1.f); // Re-derive after resize

// Retrieve VP matrix for custom shader uploads
const glm::mat4& vp = camera.GetViewProjectionMatrix();
```

| Method | Description |
| :----- | :---------- |
| `SetProjection(l, r, b, t)` | Redefines frustum boundaries. Call after viewport resize. |
| `SetPosition(glm::vec3)` | Moves the camera in world space. Recalculates View matrix. |
| `SetRotation(float degrees)` | Rotates around the Z-axis. Recalculates View matrix. |
| `GetViewProjectionMatrix()` | Returns pre-multiplied VP matrix ready for GPU upload. |

### Camera Controller Wrapper

`OrthographicCameraController` handles input-driven movement, aspect-ratio sync, and smooth zoom. Use this in-game layers instead of driving `OrthographicCamera` directly.

```cpp
// In your layer header
Cosmic::OrthographicCameraController m_CamController{ aspectRatio };

// In OnUpdate
m_CamController.OnUpdate(deltaTime); // WASD move, scroll zoom

// In OnEvent (handles MouseScrolledEvent and WindowResizeEvent automatically)
m_CamController.OnEvent(e);

// Use the underlying camera for rendering
Cosmic::Renderer2D::BeginScene(m_CamController.GetCamera());
```

| Method | Description |
| :----- | :---------- |
| `OnUpdate(float ts)` | WASD panning + smooth zoom interpolation |
| `OnResize(float w, float h)` | Recalculates aspect ratio to prevent stretching |
| `SetZoomLevel(float level)` | Hard-snaps both the active zoom and target zoom |
| `SetZoomLimits(float min, float max)` | Clamps scroll zoom to a defined range |

**Design notes:**
- Movement speed is multiplied by the current zoom level, so panning feels consistent whether viewing a large area or zoomed in on a small detail.
- Zoom uses asymptotic interpolation (exponential smoothing toward a target value) to avoid jarring snap-jumps on scroll events.
- Viewport resize events automatically recalculate projection bounds.

---

## 11. Framebuffer

**What it is:** An off-screen GPU render target. Instead of drawing directly to the display window, you redirect the render pipeline into a framebuffer's texture attachment. The WorkspaceLayer then samples that texture and displays it inside an ImGui viewport panel.

### Why Off-Screen Rendering

- **ImGui viewport embedding:** The rendered scene is displayed as an ImGui `Image`, not directly on the window, allowing the editor UI to surround it.
- **Post-processing:** Render to a texture, apply effects, sample the result.
- **Resolution independence:** The render target can be a different resolution from the display window.

### Lifecycle

```cpp
// Creation (usually done in Application::Initialize)
Cosmic::FramebufferSpecification spec;
spec.Width  = 1280;
spec.Height = 720;
Ref<FrameBuffer> fb = Cosmic::FrameBuffer::Create(spec);

// In your render loop (WorkspaceLayer does this automatically):
fb->Bind();                         // Redirect GPU output to this buffer
RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.f});
RenderCommand::Clear();
// ... draw your scene ...
fb->Unbind();                       // Resume drawing to the window

// Display inside ImGui:
uint32_t texID = fb->GetColorAttachmentRendererID();
ImGui::Image((void*)(uintptr_t)texID, panelSize, {0, 1}, {1, 0});
```

### Resize Handling

The framebuffer's texture attachments must match the viewport size. The `WorkspaceLayer` checks each frame:

```cpp
if (m_ViewportSize.x > 0 &&
    (fb->GetWidth() != m_ViewportSize.x || fb->GetHeight() != m_ViewportSize.y))
{
    fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
}
```

`Resize()` calls `Invalidate()` internally, which tears down and reallocates the color texture and depth/stencil renderbuffer. This is safe to call every frame when dimensions change.

### Framebuffer API

| Method | Description |
| :----- | :---------- |
| `Bind()` | Activates this FBO as the render target. Also calls `glViewport` to match FBO dimensions. |
| `Unbind()` | Restores the default framebuffer (the display window). |
| `Resize(w, h)` | Re-allocates texture attachments to the new dimensions. |
| `GetColorAttachmentRendererID()` | Returns the OpenGL texture ID for use as an ImGui image source. |
| `GetWidth()` / `GetHeight()` | Returns current FBO dimensions. |

---

## 12. Window Management

**What it is:** The `Window` class wraps GLFW to manage the OS window, graphics context (OpenGL via GLAD), and all hardware input callbacks. It translates GLFW's raw signals into Cosmic `Event` objects and fires them through the application's event pipeline.

### Internal Architecture

The window holds two critical handles:

- **`GLFWwindow* m_Handle`** — The native OS window instance.
- **`GraphicsContext* m_Context`** — The active graphics context (resolves to `OpenGLContext`). Owns the `glfwMakeContextCurrent` call and GLAD initialization.

A `WindowData` struct is registered as the GLFW user pointer so that C-style GLFW callbacks can access engine state without global variables:

```cpp
struct WindowData {
    std::string     Title;
    unsigned int    Width = 0, Height = 0;
    bool            VSync = false;
    EventCallbackFn EventCallback; // Bound to Application::OnEvent
};
```

All GLFW callbacks extract this struct, construct the appropriate Cosmic `Event`, and call `EventCallback`.

### Window API

| Method | Description |
| :----- | :---------- |
| `GetWidth()` | Cached pixel width of the window surface. |
| `GetHeight()` | Cached pixel height of the window surface. |
| `GetSize(int* w, int* h)` | Queries GLFW framebuffer dimensions directly (most accurate). |
| `SetEventCallback(fn)` | Registers the application-level event handler. |
| `SetVSync(bool)` | Calls `glfwSwapInterval(1)` or `(0)`. |
| `ShouldClose()` | Returns `true` if the OS has requested window closure. |
| `SwapBuffers()` | Delegates to the graphics context to present the rendered frame. |
| `PollEvents()` | Calls `glfwPollEvents()` to process the OS event queue. |

---

## 13. Logging & Diagnostics

**What it is:** A thread-safe logging system built on `spdlog` with two separate channels — one for engine internals and one for client/game code — each color-coded in the console.

### Log Macros

**Engine-internal code** (inside `Cosmic::` namespace, engine source files):

```cpp
CS_CORE_TRACE("Renderer initialized, backend: {0}", apiName);
CS_CORE_INFO("Window created: {0}x{1}", width, height);
CS_CORE_WARN("Missing asset, using fallback: {0}", path);
CS_CORE_ERROR("Failed to compile shader: {0}", filepath);
CS_CORE_CRITICAL("Out of GPU memory — aborting!");
```

**Client/game code** (your plugin layer, game logic):

```cpp
CS_TRACE("Entity spawned at ({0}, {1})", x, y);
CS_INFO("Level loaded: {0}", levelName);
CS_WARN("Physics timestep exceeded budget");
CS_ERROR("Save file corrupt: {0}", savePath);
```

Format strings use `spdlog`'s `{0}`, `{1}`, … positional syntax, which accepts any type with a `<<` operator or `fmt` formatter.

### Timestep Wrapper

`Cosmic::Timestep` is a thin float wrapper that prevents unit confusion (`seconds` vs `milliseconds`) in frame update code:

```cpp
void MyLayer::OnUpdate(Cosmic::Timestep ts)
{
    float dt  = ts.GetSeconds();      // Most common — use for velocity, physics
    float dtMs = ts.GetMilliseconds(); // Useful for profiling / UI display

    // Also works as a raw float implicitly:
    m_Position.x += m_Speed * ts;     // ts converts to seconds automatically
}
```

| Method | Returns |
| :----- | :------ |
| `GetSeconds()` | `float` — duration in seconds |
| `GetMilliseconds()` | `float` — duration × 1000 |
| `operator float()` | Implicit seconds value |

---

## 14. Dynamic DLL Plugin System

**What it is:** The engine can load and unload game project DLLs at runtime without restarting. This powers the Launcher → Workspace transition. Your game project compiles into a `.dll`, which the engine maps into memory, hooks its export functions, and mounts as a live `Layer`.

### Required DLL Exports

Every game project DLL **must** export exactly these two C functions. Add this block to the bottom of your root project `.cpp` file:

```cpp
#include <Cosmic.h>
#include "MyProjectLayer.h"

extern "C"
{
    // Synchronizes ImGui and ImPlot context pointers across the DLL boundary.
    // The engine cannot share global singletons across DLL boundaries automatically —
    // this call fixes that.
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    // Instantiates your root layer. The engine takes ownership of the returned pointer.
    // Do NOT wrap this in a smart pointer — the engine manages its lifetime.
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::MyProjectLayer();
    }
}
```

### Launching a Project from the Launcher

```cpp
// Called from LauncherLayer when the user selects a project
Cosmic::Application::Get().TransitionFromLauncherToWorkspace("MyProject.dll");

// The engine queues this for the Safe Zone:
// 1. Pops the LauncherLayer
// 2. Pushes a WorkspaceLayer
// 3. Calls LoadProjectDLL("MyProject.dll")
// 4. Mounts the returned Layer* inside the WorkspaceLayer's viewport slot
```

### DLL Load/Unload Sequence

**Load (`LoadProjectDLL`):**
1. `LoadLibraryA` maps the `.dll` into the engine's virtual address space
2. `GetProcAddress` locates `InitializePluginContexts` and `CreatePluginLayer`
3. `InitializePluginContexts` is called with the engine's ImGui/ImPlot context pointers
4. `CreatePluginLayer` is called — returns a raw `Layer*`
5. The layer is mounted into `WorkspaceLayer::SetViewportLayer`

**Unload (`UnloadProjectDLL`):**
1. `WorkspaceLayer::ClearViewportLayer()` removes the layer from the viewport
2. `delete m_ActivePluginLayer` — **must run before `FreeLibrary`** so the vtable still exists
3. `FreeLibrary` releases the module from memory
4. All tracking pointers are set to `nullptr`

> **Critical ordering:** If you call `FreeLibrary` before `delete`-ing the plugin layer, the vtable for `~MyProjectLayer()` no longer exists in memory and the destructor call crashes. The engine enforces the correct order in `UnloadProjectDLL()`.

### Returning to the Launcher

```cpp
// From a menu item in WorkspaceLayer or from game code:
Cosmic::Application::Get().TransitionToLauncher();

// Safe Zone processes this as:
// 1. UnloadProjectDLL()
// 2. WorkspaceLayer begins multi-stage ImGui dockspace cleanup
// 3. WorkspaceLayer is popped and deleted
// 4. LauncherLayer is pushed
// 5. SynchronizeRenderingState() re-fires a resize event to realign UI layout
```

---

## 15. Low-Level Render Commands

**What it is:** `RenderCommand` is a thin static dispatcher that forwards hardware calls to the active `RendererAPI` backend (OpenGL by default). It is the lowest-level rendering interface you should ever need to call directly. `Renderer2D` uses it internally; you only call it directly for viewport setup and clear operations.

### Common Direct Usage

```cpp
// In WorkspaceLayer::OnUpdate, wrapping a framebuffer render pass:
fb->Bind();

Cosmic::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
Cosmic::RenderCommand::Clear();
Cosmic::RenderCommand::SetViewport(0, 0, (uint32_t)viewportWidth, (uint32_t)viewportHeight);

// ... your scene draws here ...

fb->Unbind();

// Clear the main window (no color, just depth/stencil):
Cosmic::RenderCommand::Clear(0.f, 0.f, 0.f);
```

### RenderCommand API Reference

| Function | Description |
| :------- | :---------- |
| `SetViewport(x, y, w, h)` | Maps normalized device coordinates to window-space pixels. Must be called after FBO bind and after window resize. |
| `SetClearColor(glm::vec4)` | Sets the RGBA fill value used on the next `Clear()` call. |
| `Clear()` | Clears both the color buffer and depth buffer using the current clear color. |
| `DrawIndexed(Ref<VertexArray>, uint32_t count)` | Issues a `glDrawElements` call. `count = 0` uses the VA's full index count. |
| `DrawLines(Ref<VertexArray>, uint32_t vertexCount)` | Issues a `glDrawArrays(GL_LINES, ...)` call for non-indexed line geometry. |

### Backend Selection

The active graphics API is chosen at startup via `RendererAPI::GetAPI()`. The selection determines which concrete implementation `RenderCommand::s_RendererAPI` points to:

```
RendererAPI::API::OpenGL  →  new OpenGLRendererAPI()   ← Current default
RendererAPI::API::DirectX →  nullptr (future)
RendererAPI::API::None    →  nullptr (headless)
```

To add a new backend, implement `RendererAPI`, add a case to `CreateRendererAPI()` in `RenderCommand.cpp`, and update the API selection logic.