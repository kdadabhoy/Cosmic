# Cosmic Engine — Part I: Client Developer Guide

> **How to use this document:** This is the complete client-facing reference for building projects with Cosmic. It covers every API you'll interact with, from the minimal project skeleton to the parallel job system. All code is verified against the current source.

---

## Table of Contents

### Part 1: Client Developer Guide

1. [Getting Started](#1-getting-started)
2. [Memory Management](#2-memory-management)
3. [Application Lifecycle](#3-application-lifecycle)
4. [The Layer System](#4-the-layer-system)
5. [The Event System](#5-the-event-system)
6. [Input Polling](#6-input-polling)
7. [Time & Timeline System](#7-time--timeline-system)
8. [2D Rendering API](#8-2d-rendering-api)
9. [Materials and Shaders](#9-materials-and-shaders)
10. [The Shader Contract](#10-the-shader-contract)
11. [Sprite Sheets and SubTexture2D](#11-sprite-sheets-and-subtexture2d)
12. [SDF Circles](#12-sdf-circles)
13. [Instanced Rendering](#13-instanced-rendering)
14. [RenderPass and Multi-Camera Rendering](#14-renderpass-and-multi-camera-rendering)
15. [Entity Component System](#15-entity-component-system)
16. [Camera System](#16-camera-system)
17. [Virtual File System](#17-virtual-file-system)
18. [Framebuffer](#18-framebuffer)
19. [Logging](#19-logging)
20. [Serial Communication](#20-serial-communication)
21. [The Template Project](#21-the-template-project)
22. [Job System & Parallel Pipeline](#22-job-system--parallel-pipeline)
23. [Scene System](#23-scene-system)
24. [Window System](#24-window-system)
25. [Complete API Reference Tables](#25-complete-api-reference-tables)
26. [Telemetry System](#26-telemetry-system)

---

### Part 2: Engine Internals

26. [Source File Map](#26-source-file-map)
27. [Hot-Reloadable DLL Architecture](#27-hot-reloadable-dll-architecture)
28. [Top-Down Time Propagation Waterfall](#28-top-down-time-propagation-waterfall)
29. [The Double-Tick Trap](#29-the-double-tick-trap)
30. [The OpenGL Graphics Pipeline](#30-the-opengl-graphics-pipeline)
31. [Hardware Abstraction Architecture](#31-hardware-abstraction-architecture)
32. [Batch Rendering Deep Dive](#32-batch-rendering-deep-dive)
33. [Shader Preprocessing System](#33-shader-preprocessing-system)
34. [RenderPass Stack — Implementation Details](#34-renderpass-stack--implementation-details)
35. [Parallel Pipeline Architecture](#35-parallel-pipeline-architecture)
36. [Build System](#36-build-system)
37. [Event System — Implementation Details](#37-event-system--implementation-details)
38. [Telemetry System — Implementation Details](#38-telemetry-system--implementation-details)

---

# Part I — Client Developer Guide

---

## 1. Getting Started

### What is Cosmic?

Cosmic is a C++20, OpenGL-backed 2D game and simulation engine. It runs on Windows x64 and is built around a **plugin model** — your project compiles into a `.dll` that the engine loads at runtime. This means you can iterate on your project code without recompiling the engine itself.

The entry point for any project is a `Layer` class. You subclass it, implement the hooks you care about, and export it from your DLL.

### First-Time Setup

Before building anything for the first time, run the setup script from the SDK root. This registers the `COSMIC_SDK_DIR` environment variable that all project CMakeLists files rely on:

```
setup.bat
```

You only need to do this once per machine. If you skip it, CMake will not be able to find the engine headers and the build will fail with missing-include errors.

### Building

After setup, you have two main build scripts:

| Script                | When to use                                                                                                                         |
| --------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `build_all.bat`       | Full CMake reconfigure + compile of the engine and all projects. Use after cloning or adding a new project.                         |
| `build.bat` | Incremental build — skips CMake reconfigure. Use for iterating on your project code when you haven't changed the project structure. |
| `build_engine.bat`    | Builds the engine host only, skipping all project DLLs. Useful when validating engine changes in isolation.                         |

Outputs land in `build/Runtime/Debug/`. The engine executable and all project DLLs are placed here so the launcher can discover them.

### Creating a New Project from the Launcher

The easiest way to start a new project is from inside the engine itself. Launch the engine executable, and the Launcher screen gives you a **New Project** button. Fill in your project name and the target directory (a git repo of your choosing, for example), and the launcher will:

1. Copy the full `ExampleProject` template into your chosen directory.
2. Rename all files and class names to match your project name.
3. Generate a correct `CMakeLists.txt` wired to your local SDK path.
4. Generate a `build.bat` inside the project directory.

After generation, run `build_all.bat` once from the SDK root to register and compile the new project. Subsequent iterations can use `build.bat` (root-level incremental) or the project's own `build.bat`.

### Project Structure

```
YourSDKRoot/
├── Cosmic/                     ← Engine source, assets, and dependencies
│   └── templates/
│       └── ExampleProject/     ← The template the launcher copies from
├── Runtime/                    ← Engine host executable (Main.cpp)
├── Projects/                   ← Your project folders live here (CMake scans this automatically)
├── docs/                       ← Engine documentation
├── build_all.bat               ← Full CMake reconfigure + build engine + all projects
├── build.bat                   ← Incremental build (no CMake reconfigure)
├── build_engine.bat            ← Engine only
├── setup.bat                   ← First-time environment variable setup (run once)
└── build/
    └── Runtime/
        └── Debug/              ← All compiled outputs land here
```

Any subdirectory placed under `Projects/` that contains a `CMakeLists.txt` is picked up automatically — you do not need to edit the root `CMakeLists.txt` to register a new project.

### Why Client Layers Don't Go on the Engine LayerStack

When you build a project, your code lives inside a `.dll`. The engine executable and your DLL are two separate modules with separate memory spaces. This creates a fundamental constraint: **the engine must control the lifetime of all objects it allocates**, and your DLL must control the lifetime of objects it allocates.

For this reason, the DLL boundary is crossed at exactly two points:

1. **`CreatePluginLayer()`** — your DLL allocates a `Layer*` and hands it to the engine.
2. **`OnAttach()`/`OnDetach()`** — the engine calls into your DLL code to set up and tear down resources.

The engine takes ownership of the `Layer*` returned by `CreatePluginLayer()`. It stores it, calls its hooks, and `delete`s it when the DLL is unloaded — in the correct order, before `FreeLibrary` is called so the destructor runs while DLL code is still mapped.

**You must never:**

- Push child layers onto `Application`'s `LayerStack` from inside your DLL. The engine would take ownership of a DLL-allocated object and `delete` it after `FreeLibrary` — executing the destructor against unmapped memory and crashing.
- `delete` any layer pointer yourself. The engine owns and destroys everything returned from `CreatePluginLayer()`.
- Call `app.PushLayer()` / `app.PushOverlay()` with child layers you intend to drive manually.

The correct pattern for multi-mode projects is the **Composite Layer Pattern**: the root layer the engine knows about owns all child layers internally as `std::shared_ptr<Layer>` inside an `std::vector`. Child layers are driven manually by the root layer's hooks — never registered with the engine. See [Section 21 — The Template Project](#21-the-template-project) and [Section 4 — The Layer System](#4-the-layer-system) for the complete pattern.

### Minimal Project Skeleton

The fastest way to start is to use the Launcher's New Project generator. If you prefer to set up manually, copy `Cosmic/templates/ExampleProject/` and rename it. The template ships with a working build script, shader, and three demo layers. Below is the minimal structure to understand what every new project needs.

**YourProject.h**

```cpp
#pragma once
#include <Cosmic.h>

namespace Workspace
{
    class YourProject : public Cosmic::Layer
    {
    public:
        YourProject();
        virtual ~YourProject() override = default;

        virtual void OnAttach()                          override;
        virtual void OnDetach()                          override;
        virtual void OnUpdate(float ts)                  override;
        virtual void OnFixedUpdate(float deltaFixedTime) override;
        virtual void OnImGuiRender()                     override;
        virtual void OnEvent(Cosmic::Event& e)           override;

    private:
        Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f };
    };
}
```

**YourProject.cpp**

```cpp
#include "YourProject.h"
#include <imgui.h>

namespace Workspace
{
    YourProject::YourProject() : Cosmic::Layer("YourProject")
    {
        Cosmic::FileSystem::SetActiveProject("YourProject");
    }

    void YourProject::OnAttach()  { /* load textures, create scenes */ }
    void YourProject::OnDetach()  { /* reset Ref<> handles, free GPU resources */ }

    void YourProject::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f});
        Cosmic::Renderer2D::EndScene();
    }

    void YourProject::OnFixedUpdate(float dt) { /* physics, serial polling */ }

    void YourProject::OnImGuiRender()
    {
        ImGui::Begin("Project Inspector");
        ImGui::Text("Hello, Cosmic!");
        ImGui::End();
    }

    void YourProject::OnEvent(Cosmic::Event& e) { m_Camera.OnEvent(e); }
}

// ============================================================
// REQUIRED: DLL export entry points — do not rename or remove
// ============================================================
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::YourProject();
    }
}
```

Build with `build.bat` (inside your project directory), then launch the engine. Your project will appear in the Launcher. The engine calls `InitializePluginContexts` first to synchronize the ImGui/ImPlot context pointers across the DLL boundary — omitting this causes an immediate crash on any `ImGui::Begin` call.

### ImGui Panel Naming Convention

The workspace shell (`WorkspaceLayer`) reserves three **pre-docked slots** in the Project Inspector sidebar. Name your panels to match these exact strings and they will automatically appear in the correct position without any extra docking configuration:

| Window name                  | Position                        | Best used for                                              |
| ---------------------------- | ------------------------------- | ---------------------------------------------------------- |
| `"Project Inspector Top"`    | Top section of the left sidebar | Mode selector, global controls (time scale, primary state) |
| `"Project Inspector"`        | Middle section                  | Per-mode parameters, object properties                     |
| `"Project Inspector Bottom"` | Bottom section                  | Stats, telemetry, debug toggles                            |

```cpp
// Root manager — mode selector and global controls
ImGui::Begin("Project Inspector Top");
ImGui::Text("Active Mode: %s", m_Modes[m_ActiveModeIndex]->GetName().c_str());
ImGui::End();

// Active mode's detail panel
ImGui::Begin("Project Inspector");
ImGui::SliderFloat("Speed", &m_Speed, 0.f, 10.f);
ImGui::End();

// Stats panel
ImGui::Begin("Project Inspector Bottom");
ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
ImGui::End();
```

You only need to use the slots you actually want — unused slots are simply absent from the layout. Any window name that doesn't match one of the three reserved strings creates a **floating panel** that the user can dock manually.

To request additional pre-docked panels beyond the three standard slots, call `WorkspaceLayer::RequestExtraDockedPanel()` from `OnAttach` — see [Section 24 — Window System](#24-window-system).

---

## 2. Memory Management

Cosmic wraps standard C++ smart pointers into two named aliases to enforce explicit ownership rules.

| Alias      | Underlying Type      | Rule                                                                                                        |
| ---------- | -------------------- | ----------------------------------------------------------------------------------------------------------- |
| `Scope<T>` | `std::unique_ptr<T>` | **Single owner.** One system holds and destroys this. Use for windows, layers, dedicated sub-modules.       |
| `Ref<T>`   | `std::shared_ptr<T>` | **Shared owner.** Destroyed when the last holder releases it. Use for textures, shaders, materials, scenes. |

Always use the factory helpers — never construct a smart pointer directly from `new`:

```cpp
// Single-owner creation
Scope<MySystem> sys = Cosmic::CreateScope<MySystem>(arg1, arg2);

// Shared resource creation
Ref<Cosmic::Texture2D> tex      = Cosmic::Texture2D::Create("assets/sprite.png");
Ref<Cosmic::Scene>     scene    = Cosmic::Scene::Create();
Ref<Cosmic::Material>  material = Cosmic::Material::Create(shader, "MyMaterial");
```

### Why this matters across DLL boundaries

When your project compiles as a separate `.dll`, both sides share the same `Ref<T>` reference count as long as they link against the same `Cosmic.dll`. Raw `Layer*` pointers are intentionally used at DLL entry points because they cross compilation boundaries — the engine takes full ownership and is responsible for `delete`.

> **Rule:** Never `delete` a layer pointer yourself. The engine owns and destroys everything returned from `CreatePluginLayer()`.

> **Critical — shared `Cosmic.dll` requirement:** The `Ref<T>` (`std::shared_ptr<T>`) safety model only holds when all DLLs (engine + your project) link dynamically against the same `Cosmic.dll`. This ensures a single shared allocator and a single `shared_ptr` control block per resource. If your project accidentally statically links the engine (e.g. via a CMake misconfiguration), each side gets its own allocator, and releasing a shared `Ref<Texture2D>` or `Ref<Material>` from both sides will double-free and crash — often silently during shutdown. Always verify your project links `Cosmic.lib` (the import library for `Cosmic.dll`), not a static `.lib` build of the engine.

---

## 3. Application Lifecycle

The `Application` singleton drives the entire engine. You interact with it through `Cosmic::Application::Get()`.

> **Singleton ordering note:** `s_Instance` is assigned at the top of the `Application` constructor, before `Initialize()` runs. This is intentional — subsystems called during `Initialize()` (notably `ImGuiLayer::OnAttach()`) reach back through `Application::Get()` to access the window. Moving the assignment to after `Initialize()` returns would leave `s_Instance` null during those callbacks and crash. The tradeoff is that a caller who invokes `Application::Get()` from a static initializer or very early constructor — before the `Application` object is constructed at all — will receive a null dereference. The correct long-term fix is to pass subsystem references (e.g. `Window&`) explicitly into `OnAttach()` rather than routing through the singleton, but that requires a broader refactor.

### The Frame Loop

Every frame executes four sequential passes:

```
PollEvents()
    │
    ▼
Pass 1A — Fixed Timestep (60 Hz, deterministic)
    for each layer: layer->OnFixedUpdate(scaledFixedDelta)
    │
    ▼
Pass 1B — Variable Timestep (frame-rate dependent)
    for each layer: layer->UpdateLayerTime(scaledDelta)
                    layer->OnUpdate(scaledDelta)
    │
    ▼
Pass 2 — ImGui Render
    for each layer: layer->OnImGuiRender()
    │
    ▼
SwapBuffers()
    │
    ▼
THE SAFE ZONE
    (no iterators active — DLL transitions, push/pop/delete layers here)
```

**Fixed vs. Variable Timestep:** Use `OnFixedUpdate` for physics, collision, and serial I/O — anything that breaks under inconsistent frame timing. Use `OnUpdate` for animation, visual state, and camera movement. Never issue draw calls from `OnFixedUpdate`.

**Spiral-of-Death Protection:** If a single frame takes longer than 250ms (e.g., during a debugger pause), the fixed timestep accumulator is clamped so the engine won't attempt to simulate hundreds of ticks to catch up.

### Application Control API

```cpp
Cosmic::Application& app = Cosmic::Application::Get();

// Time control
app.UseFixedTimeStep(true);       // enable or disable the 60Hz physics pass
app.SetTimeScale(0.5f);           // 0.5 = half speed
app.SetTimeScale(0.0f);           // pause
app.SetTimeScale(-1.0f);          // rewind
float scale = app.GetTimeScale(); // read the current global time scale
float t     = app.GetAbsoluteTime(); // raw engine uptime in seconds (unaffected by scale)

// Window
app.GetWindow().GetWidth();
app.GetWindow().GetHeight();

// Transitions (queued for the Safe Zone — safe to call from anywhere)
app.TransitionFromLauncherToWorkspace("MyProject.dll");
app.TransitionToLauncher();

// Framebuffer (the main render target)
Ref<Cosmic::FrameBuffer> fb = app.GetFrameBuffer();

// Workspace shell access
Cosmic::WorkspaceLayer* ws = app.GetWorkspaceLayer(); // nullptr before transition

// Shutdown
app.Close(); // sets m_Running = false, exits the loop cleanly
```

---

## 4. The Layer System

A `Layer` is the fundamental building block of any Cosmic project. Every game world, simulation mode, editor panel, and UI overlay is a `Layer`. The `LayerStack` manages their update and event dispatch order.

### Layer Hooks

Override only what you need:

```cpp
class MyLayer : public Cosmic::Layer
{
public:
    MyLayer() : Layer("MyLayer") {}

    // Called once when pushed onto the LayerStack.
    // Load textures, initialize scenes, allocate GPU resources here.
    void OnAttach() override { ... }

    // Called once when popped from the LayerStack or on engine shutdown.
    // Reset Ref<> handles, close files, release resources here.
    void OnDetach() override { ... }

    // Variable timestep — called once per frame.
    // Use for rendering, camera updates, animation.
    void OnUpdate(float deltaTime) override { ... }

    // Fixed timestep — called at 60 Hz regardless of frame rate.
    // Use for physics, collision detection, serial polling.
    void OnFixedUpdate(float deltaFixedTime) override { ... }

    // Traditional world-space draw commands (alternative rendering path).
    void OnRender() override { ... }

    // Separate pass for ImGui UI calls.
    void OnImGuiRender() override { ... }

    // Receives events from the top of the stack downward.
    void OnEvent(Cosmic::Event& e) override { ... }
};
```

### LayerStack Ordering

```
Index:  [0]    [1]    ...   [N-1]  [N]
        ┌──────────────────────────────┐
        │  Layer  Layer  ...  Overlay  │
        └──────────────────────────────┘
                              ▲
                    m_LayerInsertIndex

Update / Render order:   Left → Right  (layer 0 first, overlays last)
Event propagation:       Right → Left  (overlays see events first)
```

Overlays (pushed with `PushOverlay`) always sit on top of regular layers and receive events before them. This is how ImGui intercepts mouse clicks before the game world does.

```cpp
// Call inside Application::Initialize or from the Safe Zone
app.PushLayer(new MyGameLayer());
app.PushOverlay(new MyDebugOverlay()); // always on top
```

> **Memory ownership:** `Application` owns all layer pointers and is responsible for `delete`. Never delete a layer pointer registered in the stack.

### Composite Layer Pattern

For multi-mode projects, the recommended pattern is a **root manager layer** that owns a `std::vector<std::shared_ptr<Layer>>` of child layers internally. The children are **never pushed onto the engine's `LayerStack`** — the root layer drives them manually by forwarding its hooks. This keeps the engine's iteration clean and gives you precise control over which mode runs. See [Section 21 — The Template Project](#21-the-template-project) for a complete example.

---

## 5. The Event System

The event system is **reactive and propagating**. When the OS fires a hardware signal — a key press, a mouse click, a window resize — the engine packages it into a typed `Event` object and walks it down the `LayerStack` from top to bottom. Any layer can mark an event as "handled" (`e.Handled = true`) to stop it from reaching layers below.

This is distinct from [Input Polling (Section 6)](#6-input-polling), which queries the current hardware state on demand. Use events for one-shot reactions ("the user just pressed Escape"), use polling for continuous per-frame checks ("is W held down right now?").

### How Events Flow Through the Stack

```
OS Hardware Signal
        │
        ▼
Application::OnEvent(e)
        │
        ├── WindowCloseEvent  ──► Application::OnWindowClose()   (consumed here)
        ├── WindowResizeEvent ──► Application::OnWindowResize()  (still propagates!)
        │
        ▼  rbegin() → rend()  (TOP OF STACK FIRST)
┌───────────────────┐
│   ImGuiLayer      │  ← receives events first (it's an overlay)
│  (overlay)        │    blocks mouse/keyboard when ImGui panels are focused
└────────┬──────────┘
         │ e.Handled == true? → STOP
         ▼
┌───────────────────┐
│  WorkspaceLayer   │  ← forwards to your DLL layer
│  (layer)          │
└────────┬──────────┘
         │ e.Handled == true? → STOP
         ▼
┌───────────────────┐
│  Your Layer       │  ← your OnEvent() runs here
│  (inside DLL)     │
└───────────────────┘
```

### Handling Events in Your Layer

Override `OnEvent` and use `EventDispatcher` to route specific event types to dedicated handler functions. The preferred style uses lambdas — they capture `this` explicitly and avoid the macro boilerplate:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    // Preferred: lambda style
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        [this](Cosmic::KeyPressedEvent& event) { return OnKeyPressed(event); });

    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
        [this](Cosmic::MouseButtonPressedEvent& event) { return OnMouseClicked(event); });

    dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
        [this](Cosmic::WindowResizeEvent& event) { return OnWindowResize(event); });
}

// Handler signature: takes the specific event type, returns bool.
// true  = consumed (stops propagating to layers below)
// false = not consumed (passes through to lower layers)
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    if (e.GetKeyCode() == CS_KEY_ESCAPE)
    {
        TogglePauseMenu();
        return true;
    }
    return false;
}

bool MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    m_Camera.OnResize((float)e.GetWidth(), (float)e.GetHeight());
    return false; // don't consume — other layers also need resize
}
```

The legacy macro form `CS_BIND_EVENT_FN(fn)` expands to `std::bind(&fn, this, std::placeholders::_1)` and is defined in `Core.h`. It still compiles and produces correct behavior; prefer lambdas in new code.

### Forwarding Events to Sub-Systems

If your layer owns sub-systems that need events (like a camera controller), forward the event to them first:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    m_CameraController.OnEvent(e);
    if (e.Handled) return;

    Cosmic::EventDispatcher dispatcher(e);
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        [this](Cosmic::KeyPressedEvent& event) { return OnKeyPressed(event); });
}
```

Application events (window resize) must always be forwarded to **all** sub-layers, not just the active one, so that inactive cameras don't accumulate stale projection matrices:

```cpp
void MyRootLayer::OnEvent(Cosmic::Event& e)
{
    if (e.IsInCategory(Cosmic::EventCategoryApplication))
    {
        for (auto& mode : m_Modes)
            mode->OnEvent(e);
        return;
    }

    // Input events go only to the active mode
    if (e.Handled) return;
    m_Modes[m_ActiveModeIndex]->OnEvent(e);
}
```

### Common Event Patterns

**Jump on Space, ignore held repeats:**

```cpp
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    // GetRepeatCount() > 0 means the key is being held, not freshly pressed
    if (e.GetKeyCode() == CS_KEY_SPACE && e.GetRepeatCount() == 0)
    {
        Jump();
        return true;
    }
    return false;
}
```

**Guard input against paused or reversed timelines:**

```cpp
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    if (e.GetRepeatCount() > 0) return false;

    if (Cosmic::Application::Get().GetTimeScale() <= 0.0f)
    {
        if (e.GetKeyCode() == CS_KEY_R) { Reset(); return true; }
        return false;
    }

    if (e.GetKeyCode() == CS_KEY_SPACE) { Jump(); return true; }
    return false;
}
```

### Event Type Quick Reference

| Event Class                | Useful Accessors                   | Notes                                     |
| -------------------------- | ---------------------------------- | ----------------------------------------- |
| `KeyPressedEvent`          | `GetKeyCode()`, `GetRepeatCount()` | `RepeatCount > 0` = key held              |
| `KeyReleasedEvent`         | `GetKeyCode()`                     | Fired once on key release                 |
| `KeyTypedEvent`            | `GetKeyCode()`                     | Character input for text fields           |
| `MouseButtonPressedEvent`  | `GetMouseButton()`                 | Use `CS_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE`   |
| `MouseButtonReleasedEvent` | `GetMouseButton()`                 |                                           |
| `MouseMovedEvent`          | `GetX()`, `GetY()`                 | Screen-space coordinates, top-left origin |
| `MouseScrolledEvent`       | `GetXOffset()`, `GetYOffset()`     | Y is typically ±1.0 per scroll tick       |
| `WindowResizeEvent`        | `GetWidth()`, `GetHeight()`        | Pixel dimensions of the new window size   |
| `WindowCloseEvent`         | —                                  | Consumed by `Application` before layers   |

### Category Filtering

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    // Block all input during a cutscene
    if (m_CutscenePlaying && e.IsInCategory(Cosmic::EventCategoryInput))
    {
        e.Handled = true;
        return;
    }
}
```

| Category Constant          | Covers                          |
| -------------------------- | ------------------------------- |
| `EventCategoryApplication` | Window resize, close, tick      |
| `EventCategoryInput`       | All keyboard + all mouse        |
| `EventCategoryKeyboard`    | Key press, release, typed       |
| `EventCategoryMouse`       | Mouse move, scroll, button      |
| `EventCategoryMouseButton` | Mouse button press/release only |

---

## 6. Input Polling

For continuous per-frame input (movement, camera pan), use the static `Input` class instead of the event system.

```cpp
void MyLayer::OnUpdate(float ts)
{
    if (Cosmic::Input::IsKeyPressed(CS_KEY_W))
        m_Position.y += m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_S))
        m_Position.y -= m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_A))
        m_Position.x -= m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_D))
        m_Position.x += m_Speed * ts;

    if (Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
        SpawnEntityAt(Cosmic::Input::GetMousePosition());

    glm::vec2 cursor = Cosmic::Input::GetMousePosition(); // screen-space (x, y)
    float mouseX     = Cosmic::Input::GetMouseX();
    float mouseY     = Cosmic::Input::GetMouseY();
}
```

### When to Use Input vs. Events

| Use `Input::`                             | Use `OnEvent`                                   |
| ----------------------------------------- | ----------------------------------------------- |
| Continuous hold checks (movement, camera) | Single-press reactions (menu toggle, fire once) |
| Per-frame polling loops                   | State change notifications                      |
| "Is this key held right now?"             | "Did the user just press Escape?"               |

### Key Code Constants

| Constant                 | Value   | Constant                | Value   |
| ------------------------ | ------- | ----------------------- | ------- |
| `CS_KEY_SPACE`           | 32      | `CS_KEY_ESCAPE`         | 256     |
| `CS_KEY_A`–`CS_KEY_Z`    | 65–90   | `CS_KEY_ENTER`          | 257     |
| `CS_KEY_0`–`CS_KEY_9`    | 48–57   | `CS_KEY_TAB`            | 258     |
| `CS_KEY_RIGHT`           | 262     | `CS_KEY_BACKSPACE`      | 259     |
| `CS_KEY_LEFT`            | 263     | `CS_KEY_LEFT_SHIFT`     | 340     |
| `CS_KEY_DOWN`            | 264     | `CS_KEY_LEFT_CONTROL`   | 341     |
| `CS_KEY_UP`              | 265     | `CS_KEY_LEFT_ALT`       | 342     |
| `CS_KEY_F1`–`CS_KEY_F12` | 290–301 | `CS_KEY_Q` / `CS_KEY_E` | 81 / 69 |

### Mouse Button Constants

| Constant                 | Alias               | Button              |
| ------------------------ | ------------------- | ------------------- |
| `CS_MOUSE_BUTTON_LEFT`   | `CS_MOUSE_BUTTON_1` | Primary action      |
| `CS_MOUSE_BUTTON_RIGHT`  | `CS_MOUSE_BUTTON_2` | Secondary / context |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` | Pan / zoom          |

---

## 7. Time & Timeline System

Cosmic has a two-level time architecture: a **global application timeline** controlled by `Application`, and a **per-layer local timeline** each layer owns independently. Understanding both is essential for simulations that respond correctly to pause, slow-motion, and rewind.

### The Global Time Scale

`Application::SetTimeScale` multiplies every delta time the engine passes to layers:

```cpp
Cosmic::Application::Get().SetTimeScale(1.0f);   // normal
Cosmic::Application::Get().SetTimeScale(0.5f);   // half speed
Cosmic::Application::Get().SetTimeScale(0.0f);   // pause
Cosmic::Application::Get().SetTimeScale(-1.0f);  // rewind
float scale = Cosmic::Application::Get().GetTimeScale();
```

`GetAbsoluteTime()` is always monotonically increasing — it accumulates raw wall-clock time and is unaffected by `TimeScale`. Use it for profiling, session duration, or any clock that must not pause or rewind. Shaders reading `u_Time` receive this raw value. For a time value that does rewind with negative scale, use `GetLocalTime()` from within a layer.

### Per-Layer Local Time

Every `Layer` has its own local timeline accumulator. The engine calls `layer->UpdateLayerTime(scaledDelta)` once per frame before `OnUpdate`, which does:

```
m_LocalTime += scaledDelta × m_LocalTimeScale
```

Call these helpers inside your layer body directly:

```cpp
float t  = GetLocalTime();    // accumulated (global scale × layer scale) time in seconds
float s  = GetTimeScale();    // this layer's own scale multiplier (default 1.0)
SetLocalTime(0.0f);           // reset (e.g. on level restart)
SetTimeScale(0.5f);           // slow this layer independently of the global scale
```

> **Critical:** `GetLocalTime()` is an **instance method** on the base `Layer` class. Always call it as `GetLocalTime()` inside your derived class body — never as `Cosmic::Layer::GetLocalTime()`. The latter form performs static scope resolution and will either fail to compile or invoke the wrong context.

### What ts and dt Actually Contain

Understanding the two-level time architecture is essential before using `SetTimeScale`:

| Source | Contains | Use for |
| --- | --- | --- |
| `ts` in `OnUpdate(float ts)` | `rawDelta × globalTimeScale` — **global scale only** | Movement, camera, any per-frame delta |
| `dt` in `OnFixedUpdate(float dt)` | `(1/60) × globalTimeScale` — **global scale only** | Physics, collision, fixed-step integration |
| `GetLocalTime()` | accumulated `rawDelta × globalScale × layerScale` — **both scales** | Shader `u_Time`, particle age, any accumulated value |

`ts` and `dt` do **not** automatically include the layer's own scale. The engine only applies the layer scale when accumulating `GetLocalTime()` internally. This has two practical consequences:

**1. For shaders and accumulated values — always use `GetLocalTime()`.** It is already double-scaled with no extra work required. Setting the layer scale to 0.5 will immediately halve the rate at which `GetLocalTime()` grows, and your shader animation slows down automatically.

**2. For movement and physics inside a standalone layer — apply the layer scale manually if you want per-layer time control:**

```cpp
// Standalone layer that supports its own independent SetTimeScale():
void MyLayer::OnUpdate(float ts)
{
    // ts is global-scaled only. Multiply by GetTimeScale() to also apply the layer scale.
    const float localTs = ts * GetTimeScale();

    m_Camera.OnUpdate(localTs);          // camera pan speed respects layer scale
    m_MySystem.Tick(localTs);            // system tick respects layer scale
    // GetLocalTime() for shaders is already correct — don't touch it
}

void MyLayer::OnFixedUpdate(float dt)
{
    const float localDt = dt * GetTimeScale();
    if (localDt <= 0.0f) return;         // pause / rewind guard still works
    m_Body.Position += m_Velocity * localDt;
}
```

If your layer is driven by a **root manager layer** (the Composite Layer Pattern), the root manager should apply the scale once before dispatching — see [Section 21](#21-the-template-project). Child layers then receive a pre-scaled delta and use `ts`/`dt` directly without multiplying again.

If your layer is pushed directly onto the engine's `LayerStack` and you never call `SetTimeScale()` on it, `GetTimeScale()` always returns `1.0` and the multiplication is a no-op — skip it.

### How to Feed Time to Shaders

```cpp
void MyLayer::OnUpdate(float ts)
{
    if (m_Material)
        m_Material->Set("u_Time", GetLocalTime());

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, m_Material);
    Cosmic::Renderer2D::EndScene();
}
```

Because `GetLocalTime()` is pre-scaled by both the global `TimeScale` and this layer's own scale, your shaders automatically respond to pause and rewind without any extra code.

### Fixed vs. Variable Timestep — Dual-Rate Simulation Matrix

| Aspect              | `OnUpdate(float ts)`                         | `OnFixedUpdate(float dt)`                       |
| ------------------- | -------------------------------------------- | ----------------------------------------------- |
| **Purpose**         | Visual updates, animation, camera            | Physics, collision, deterministic simulation    |
| **Rate**            | Variable — depends on monitor refresh rate   | Fixed at 60 Hz regardless of frame rate         |
| **Input**           | Globally-scaled variable delta — `rawDelta × globalScale` | Globally-scaled fixed interval — `(1/60) × globalScale` |
| **Rendering calls** | Yes — call `BeginScene`/`EndScene` here      | No — never issue draw calls here                |
| **Shader uniforms** | Yes — update `u_Time`, `u_Color` etc. here   | No — GPU state should not be touched here       |
| **Anti-pattern**    | Running collision math that breaks at 144Hz  | Running sprite rotation or lerp animations      |
| **Timeline guards** | Use `GetLocalTime()` for shaders; `ts * GetTimeScale()` only if the layer needs its own independent scale | Guard `dt <= 0.0f` to detect pause and rewind |

### Timeline Guards in Fixed Update

When `TimeScale` is zero (paused) or negative (rewinding), `OnFixedUpdate` receives a zero or negative delta. Always guard against this:

```cpp
void MyLayer::OnFixedUpdate(float dt)
{
    if (dt == 0.0f) return; // paused — freeze simulation

    if (dt < 0.0f)
    {
        // Optional rewind behavior
        for (auto& obs : m_Obstacles)
        {
            auto& t = obs.GetComponent<Cosmic::TransformComponent>();
            t.Position.x += m_Speed * std::abs(dt); // move backwards
        }
        return;
    }

    // Normal forward simulation
    m_Score += dt * 10.0f;
}
```

### Common Time Pitfalls

**Manual time accumulation:** Writing `m_Time += ts;` inside your layer accumulates time yourself instead of using the engine's built-in clock. The problem is that `ts` does not include the layer's own scale — your manual counter only gets the global scale. Use `GetLocalTime()` instead: it is accumulated automatically with both the global scale and the layer's own scale applied, and it rewinds correctly when `TimeScale` is negative.

**Animating in `OnFixedUpdate`:** Sprite rotation, camera lerp, and uniform uploads driven from `OnFixedUpdate` produce micro-stuttering at high refresh rates because they only update 60 times per second. Move visual updates to `OnUpdate`.

**Unit confusion:** `ts` and `dt` are always in **seconds**. Multiplying a velocity (m/s) by `ts` gives correct displacement. For display values in milliseconds, compute `ts * 1000.f` explicitly.

---

## 8. 2D Rendering API

`Renderer2D` is the primary drawing interface. It batches geometry internally to minimize GPU draw calls.

### Frame Structure

Every render pass must be wrapped in `BeginScene` / `EndScene`:

```cpp
void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

    // --- all draw calls go here ---

    Cosmic::Renderer2D::EndScene(); // flushes all batched geometry to GPU
}
```

### Flat Color Quads

Both `vec2` and `vec3` position overloads are available. The `vec2` overloads insert `z = 0.0f` automatically:

```cpp
// vec3 position — explicit z-layering (higher z = in front)
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.0f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}); // red
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.5f}, {1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}); // green, in front

// vec2 position — z inserted as 0
Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {1.f, 1.f}, {0.f, 0.f, 1.f, 1.f}); // blue
```

### Textured Quads

> **Load failure behavior:** `Texture2D::Create` always returns a non-null `Ref` — on failure it returns a degraded object with zero width, zero height, and a zero GPU handle. Calling `Bind` on such a texture emits a core warning and binds nothing (the slot renders black). Always check whether the file exists before calling `Create`; the engine does not throw or return `nullptr` on a missing file.

```cpp
Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create("assets/sprite.png");

Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex);
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 2.0f);                            // 2× UV tiling
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 1.0f, {1.f, 0.5f, 0.5f, 1.f});  // tint

// vec2 convenience overloads
Cosmic::Renderer2D::DrawQuad({0.f, 1.f}, {1.f, 1.f}, tex);
Cosmic::Renderer2D::DrawQuad({0.f, 1.f}, {1.f, 1.f}, tex, 2.0f, {1.f, 1.f, 1.f, 1.f});
```

### Material Quads (Shader-Driven)

```cpp
auto shader   = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
auto material = Cosmic::Material::Create(shader, "FireMaterial");
material->Set("u_Color", glm::vec4(1.f, 0.5f, 0.2f, 1.f));

void MyLayer::OnUpdate(float ts)
{
    material->Set("u_Time", GetLocalTime());

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, material);  // vec3
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f},       {2.f, 2.f}, material);  // vec2 convenience
    Cosmic::Renderer2D::EndScene();
}
```

### Rotated Quads

Rotation is always in **radians**:

```cpp
// Color
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, glm::radians(45.f), {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f},       {1.f, 1.f}, glm::radians(45.f), {1.f, 1.f, 0.f, 1.f});

// Texture
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, tex);
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f},       {1.f, 1.f}, rotation, tex);

// Material (vec3 only)
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, material);
```

### Debug Geometry

```cpp
Cosmic::Renderer2D::DrawLine({-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRect({0.f, 0.f, 0.f}, {2.f, 1.f}, {0.f, 1.f, 1.f, 1.f}); // wireframe rectangle
```

### Performance Statistics

```cpp
Cosmic::Renderer2D::ResetStats();

Cosmic::Renderer2D::Statistics stats = Cosmic::Renderer2D::GetStats();
ImGui::Text("Draw Calls: %d", stats.DrawCalls);
ImGui::Text("Quads:      %d", stats.QuadCount);   // batched + instanced quads only
ImGui::Text("Circles:    %d", stats.CircleCount); // batched + instanced circles only
ImGui::Text("Lines:      %d", stats.LineCount);
ImGui::Text("Vertices:   %d", stats.GetTotalVertexCount());
ImGui::Text("Indices:    %d", stats.GetTotalIndexCount());

Cosmic::Renderer2D::SetStatsStatus(false); // disable stat recording when not needed
```

---

## 9. Materials and Shaders

A **Shader** is a GPU program. A **Material** is a shader plus a named set of parameter values (uniforms). Materials let multiple objects share the same shader program with different visual properties.

### Loading a Shader

Always resolve paths through the VFS before passing them to `Shader::Create`:

```cpp
std::string path   = Cosmic::FileSystem::Resolve("project://shaders/MyShader.glsl");
Ref<Cosmic::Shader> shader = Cosmic::Shader::Create(path);
// Shader::Create returns nullptr on compilation or link failure — always null-check before use.
if (!shader) { CS_ERROR("Failed to load shader: {}", path); return; }
```

### Creating and Configuring a Material

```cpp
Ref<Cosmic::Material> material = Cosmic::Material::Create(shader, "MyMaterial");

// Set uniforms into the material's cache
material->Set("u_Color",    glm::vec4(1.f, 0.5f, 0.2f, 1.f));
material->Set("u_Roughness", 0.4f);
material->Set("u_Offset",   glm::vec2(0.1f, 0.0f));

// Bind a texture
Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create("assets/noise.png");
material->Set("u_NoiseTex", tex);
```

### Reading Cached Uniform Values

```cpp
float         roughness = material->GetFloat("u_Roughness");
glm::vec2     offset    = material->GetVector2("u_Offset");
glm::vec4     color     = material->GetVector4("u_Color"); // missing key returns glm::vec4(1.0f) — opaque white, not zero
Ref<Cosmic::Texture2D> tex = material->GetTexture("u_NoiseTex");

// Check presence before reading
if (material->HasFloat("u_Roughness"))  { ... }
if (material->HasFloat2("u_Offset"))    { ... }
if (material->HasFloat3("u_Dir"))       { ... }
if (material->HasFloat4("u_Color"))     { ... }
if (material->HasTexture("u_NoiseTex")) { ... }
```

### Cloning a Material

`Clone` deep-copies all cached uniform values into a new material instance. Both materials share the same compiled `Ref<Shader>` — the shader is not duplicated:

```cpp
Ref<Cosmic::Material> clone = Cosmic::Material::Clone(material, "MyMaterial_Red");
clone->Set("u_Color", glm::vec4(1.f, 0.f, 0.f, 1.f)); // independent from source
```

### Updating Material Uniforms Per Frame

```cpp
void MyLayer::OnUpdate(float ts)
{
    material->Set("u_Time",  GetLocalTime());
    material->Set("u_Color", m_CurrentColor);

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, material);
    Cosmic::Renderer2D::EndScene();
}
```

---

## 10. The Shader Contract

Understanding how the shader preprocessor works lets you write shaders that compile predictably every time.

### The Three Processing Paths

When `Shader::Create(filepath)` is called, the preprocessor reads the file and routes it down one of three paths:

**Path 1 — Full multi-stage file**

The file contains at least one `#type vertex` and one `#type fragment` directive. The preprocessor splits at these boundaries, handles each block independently, and injects any missing engine uniforms per-stage. This is the correct path for every shader you write from scratch.

**Path 2 — Fragment-only file**

The file contains `#type fragment` but no `#type vertex`. A complete boilerplate vertex shader is generated and prepended automatically. Your fragment block is compiled with the same uniform injection rules. Use this for quick fragment-stage experiments where you're comfortable with the standard vertex pass-through.

The auto-generated vertex shader provides these varyings to your fragment stage:

```glsl
out vec4 v_Color;
out vec2 v_TexCoord;
```

**Path 3 — Shadertoy-style file**

The file contains no `#type` directives but has `mainImage` or `iTime` in the source. A full vertex shader is generated and your source is wrapped in a fragment stage with these compatibility aliases injected:

```glsl
#define iTime       u_Time
#define iResolution vec3(u_ViewportSize, 1.0)
```

`iMouse` is **not** injected. If your Shadertoy shader uses it, declare `uniform vec4 iMouse = vec4(0.0);` yourself.

If the file has no `#type` tags and no Shadertoy signatures, the preprocessor logs a critical error and `Shader::Create` returns `nullptr`.

### Auto-Injected Uniforms

Three engine uniforms are candidates for injection, evaluated per-stage. Injection only fires when a uniform is referenced in a stage's source but not already declared in it:

| Uniform            | Type    | Trigger Keywords                                                | Stage       |
| ------------------ | ------- | --------------------------------------------------------------- | ----------- |
| `u_ViewProjection` | `mat4`  | `u_ViewProjection`                                              | Vertex only |
| `u_Time`           | `float` | `u_Time`, `iTime`, `TIME`, `_Time`                              | Any stage   |
| `u_ViewportSize`   | `vec2`  | `u_ViewportSize`, `iResolution`, `BUFFER_SIZE`, `_ScreenParams` | Any stage   |

**To bypass injection entirely:** Declare the uniform explicitly in your source. The preprocessor sees the `uniform` keyword on that line and skips injection. Your source is compiled verbatim after the `#type` split.

**Comment safety:** Block comments (`/* */`) and line comments (`//`) are stripped from a working copy before the scan. A commented-out declaration does **not** count as a declaration — the preprocessor will still inject a live one if the name appears in live code.

### The Canonical Boilerplate

Copy this as the starting point for any new shader. It declares everything explicitly and matches the `Renderer2D` batch vertex attribute layout:

```glsl
#type vertex
#version 450 core

// Renderer2D batch layout — do not reorder or rename.
// The VAO attribute pointers are fixed at engine init time.
layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec4  a_Color;
layout(location = 2) in vec2  a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

uniform mat4 u_ViewProjection; // declared explicitly — no injection

out vec4  v_Color;
out vec2  v_TexCoord;
out float v_TexIndex;
out float v_TilingFactor;

void main()
{
    v_Color        = a_Color;
    v_TexCoord     = a_TexCoord;
    v_TexIndex     = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    gl_Position    = u_ViewProjection * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
in float v_TexIndex;
in float v_TilingFactor;

// Declare every engine uniform you use — preprocessor skips injection for these.
uniform float       u_Time;
uniform vec2        u_ViewportSize;
uniform vec4        u_Color;

// Required if you sample textures routed through the Renderer2D batch system.
uniform sampler2D u_Textures[32];

void main()
{
    color = texture(u_Textures[int(v_TexIndex)], v_TexCoord * v_TilingFactor) * v_Color * u_Color;
}
```

### Vertex Attribute Layout Contract

Any shader rendering geometry through `Renderer2D` must match the layout above exactly. The VAO attribute pointers are configured once at engine init and never change. If your vertex shader uses different locations or types, vertex data will be silently misread — there is no runtime error.

---

## 11. Sprite Sheets and SubTexture2D

`SubTexture2D` extracts a UV-bounded tile from a parent texture atlas, allowing you to draw individual sprites from a single sprite sheet without creating separate texture objects per tile.

### Creating SubTextures from a Grid Atlas

```cpp
Ref<Cosmic::Texture2D> atlas = Cosmic::Texture2D::Create("assets/sprites/sheet.png");

// CreateFromCoords(texture, gridCoords, cellSizePixels, spriteSizeInCells = {1,1})
// gridCoords = (column, row) zero-indexed from bottom-left
Ref<Cosmic::SubTexture2D> tile  = Cosmic::SubTexture2D::CreateFromCoords(atlas, {2, 0}, {64, 64});
Ref<Cosmic::SubTexture2D> big   = Cosmic::SubTexture2D::CreateFromCoords(atlas, {4, 1}, {64, 64}, {2, 2}); // 2×2 tile
```

**UV math performed by `CreateFromCoords`:**

```
min.x = (coords.x * cellSize.x) / textureWidth
min.y = (coords.y * cellSize.y) / textureHeight
max.x = ((coords.x + spriteSize.x) * cellSize.x) / textureWidth
max.y = ((coords.y + spriteSize.y) * cellSize.y) / textureHeight
```

### Creating SubTextures from Raw UV Coordinates

If you already know the normalized UV bounds:

```cpp
// SubTexture2D(texture, min, max) — both in normalized [0,1] texture space
Ref<Cosmic::SubTexture2D> tile = Cosmic::CreateRef<Cosmic::SubTexture2D>(atlas, glm::vec2{0.0f, 0.0f}, glm::vec2{0.25f, 0.25f});
```

### Drawing SubTextures

```cpp
// vec3 overloads
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tile);
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tile, {1.f, 1.f, 1.f, 1.f}); // tint

// vec2 convenience
Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {1.f, 1.f}, tile);

// Rotated
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, glm::radians(45.f), tile);
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f},       {1.f, 1.f}, glm::radians(45.f), tile);
```

### SubTexture2D API

| Function / Constructor | Parameters                                                          | Description                                                                        |
| ---------------------- | ------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `CreateFromCoords`     | `Ref<Texture2D>, vec2 coords, vec2 cellSize, vec2 spriteSize={1,1}` | Static factory. `coords` is (column, row) in grid units from the bottom-left.      |
| `SubTexture2D`         | `Ref<Texture2D>, vec2 min, vec2 max`                                | Direct UV-range constructor. `min`/`max` in normalized [0,1] texture space.        |
| `GetTexture()`         | —                                                                   | Returns `const Ref<Texture2D>&` — the parent atlas.                                |
| `GetTexCoords()`       | —                                                                   | Returns `const glm::vec2*` — pointer to the 4-element UV corner array (CCW order). |

**UV corner order** (counter-clockwise, matching stb_image vertical flip):

| Index | Corner       |
| ----- | ------------ |
| 0     | Bottom-Left  |
| 1     | Bottom-Right |
| 2     | Top-Right    |
| 3     | Top-Left     |

---

## 12. SDF Circles

`DrawCircle` uses a Signed Distance Field approach to render resolution-independent circles with configurable thickness and anti-aliased edges. The geometry is a quad; the circle is computed in the fragment shader.

```cpp
// Solid disk (thickness = 1.0 = full fill)
Cosmic::Renderer2D::DrawCircle(
    {0.f, 0.f, 0.f},           // position (vec3)
    {2.f, 2.f},                 // size (world units — controls the bounding quad)
    {0.2f, 0.8f, 1.f, 1.f},    // color (RGBA)
    1.0f,                       // thickness (1.0 = solid disk)
    0.005f                      // fade (AA edge softness)
);

// Hollow ring
Cosmic::Renderer2D::DrawCircle(
    {0.f, 0.f, 0.f},
    {2.f, 2.f},
    {1.f, 0.5f, 0.f, 0.9f},
    0.05f,                      // thickness < 1.0 = ring; 0.05 = thin wall
    0.005f
);

// vec2 overload (z = 0 inserted automatically) — thickness and fade have defaults
Cosmic::Renderer2D::DrawCircle({0.f, 2.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f});
// equivalent to thickness=1.0, fade=0.005
```

### Thickness and Fade Reference

| `thickness` | Visual result                          |
| ----------- | -------------------------------------- |
| `1.0`       | Solid filled disk                      |
| `0.5`       | Half-thickness ring (50% outer radius) |
| `0.1`       | Narrow ring                            |
| `0.01`      | Very thin ring / orbit indicator       |

| `fade`  | Visual result                      |
| ------- | ---------------------------------- |
| `0.005` | Default — crisp, anti-aliased edge |
| `0.05`  | Soft glow edge                     |
| `0.2`   | Very blurry / neon glow effect     |

### Custom Circle Shader

All `DrawCircle` overloads accept an optional `Ref<Shader>` override parameter. Pass a custom shader to replace the built-in SDF circle shader for that draw call:

```cpp
Cosmic::Renderer2D::DrawCircle(
    {0.f, 0.f, 0.f}, {2.f, 2.f}, {1.f, 1.f, 1.f, 1.f},
    1.0f, 0.005f,
    myCustomCircleShader  // optional — nullptr uses the built-in
);
```

---

## 13. Instanced Rendering

For large numbers of identical-geometry objects (particles, grids, crowds), Cosmic provides two instanced draw paths that submit all data in a single GPU draw call. These are more efficient than batched `DrawQuad`/`DrawCircle` calls when the count exceeds a few hundred objects.

### Instanced Quads

```cpp
// Build the instance array (typically in OnUpdate or a compute step)
std::vector<Cosmic::Renderer2D::InstanceQuadData> instances;
instances.reserve(count);

for (int i = 0; i < count; ++i)
{
    Cosmic::Renderer2D::InstanceQuadData inst;
    inst.Position      = { x, y, 0.f };   // world-space centre
    inst.Scale         = { 0.1f, 0.1f };  // full width and height
    inst.Color         = { r, g, b, 1.f };
    inst.TexCoordOffset = { 0.f, 0.f };   // {0,0} for solid color
    inst.TexCoordScale  = { 1.f, 1.f };   // {1,1} for solid color
    inst.TexIndex      = 0.f;             // 0 = white texture fallback
    inst.TilingFactor  = 1.f;
    instances.push_back(inst);
}

// Single GPU draw call for the entire array
Cosmic::Renderer2D::DrawInstancedQuads(instances.data(), (uint32_t)instances.size());

// Optional custom shader
Cosmic::Renderer2D::DrawInstancedQuads(instances.data(), (uint32_t)instances.size(), myShader);
```

`InstanceQuadData` is exactly 60 bytes (15 floats), matching the attribute stride in `QuadInstance.glsl`. Call `DrawInstancedQuads` inside a `BeginScene`/`EndScene` block — it flushes any pending batched geometry before submitting the instanced draw.

### Instanced Circles

```cpp
std::vector<Cosmic::Renderer2D::InstanceCircleData> circles;
circles.reserve(count);

for (int i = 0; i < count; ++i)
{
    Cosmic::Renderer2D::InstanceCircleData c;
    c.Position  = { x, y, 0.f };
    c.Scale     = { radius * 2.f, radius * 2.f };
    c.Color     = { r, g, b, 1.f };
    c.Thickness = 1.0f;
    c.Fade      = 0.005f;
    circles.push_back(c);
}

Cosmic::Renderer2D::DrawInstancedCircles(circles.data(), (uint32_t)circles.size());
Cosmic::Renderer2D::DrawInstancedCircles(circles.data(), (uint32_t)circles.size(), myShader);
```

---

## 14. RenderPass and Multi-Camera Rendering

`RenderPass` is an RAII scoping mechanism for isolated camera contexts. On construction it flushes any pending geometry and pushes a new viewport/VP-matrix pair onto `Renderer2D`'s internal stack. On destruction it flushes remaining geometry and pops the stack, restoring the prior pass state.

### Simple Sequential Multi-Camera

```cpp
void MyLayer::OnUpdate(float ts)
{
    // Pass 1: main world view — full window
    {
        Cosmic::RenderPass mainPass(m_MainCamera.GetCamera(), {0, 0, 1280, 720});
        Cosmic::Renderer2D::DrawQuad(...);
        // ... more draw calls ...
    } // ← auto-flushes and restores on scope exit

    // Pass 2: minimap — a small corner region
    {
        Cosmic::RenderPass minimapPass(m_OverviewCamera.GetCamera(), {900, 500, 380, 220});
        Cosmic::Renderer2D::DrawQuad(...);
    }
}
```

### With an Explicit Framebuffer Target

```cpp
m_SideFramebuffer->Bind();
{
    Cosmic::RenderPass sidePass(m_SideCamera.GetCamera(), {0, 0, fboW, fboH});
    Cosmic::Renderer2D::DrawQuad(...);
} // flushes to m_SideFramebuffer
m_SideFramebuffer->Unbind();
```

### Rules

- `RenderPass` is **non-copyable and non-movable**. Each instance must be owned by exactly one scope.
- Do not nest two `RenderPass` instances targeting the same viewport bounds.
- The viewport bounds `vec4` is `{x_offset, y_offset, width, height}` in pixels, measured from the **bottom-left** (OpenGL convention).
- You can also call `Renderer2D::PushRenderPass` / `PopRenderPass` directly if you need manual control.

**Viewport bounds convention for a 1280×720 window split into quadrants:**

| Quadrant     | Bounds `{x, y, w, h}`  |
| ------------ | ---------------------- |
| Top-left     | `{0, 360, 640, 360}`   |
| Top-right    | `{640, 360, 640, 360}` |
| Bottom-left  | `{0, 0, 640, 360}`     |
| Bottom-right | `{640, 0, 640, 360}`   |

---

## 15. Entity Component System

Cosmic uses [EnTT](https://github.com/skypjack/entt) for its ECS. Entities are lightweight handles wrapping an `entt::entity` integer and a pointer to their owning `Scene`. Components are plain structs with no required base class. The `Scene` owns the registry and is the factory for creating entities.

### Creating Entities

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// CreateEntity always auto-adds TransformComponent and TagComponent
Cosmic::Entity player = m_Scene->CreateEntity("Player");
Cosmic::Entity enemy  = m_Scene->CreateEntity("Enemy");
Cosmic::Entity bullet = m_Scene->CreateEntity(); // tag defaults to "GenericEntity"
```

### Adding and Reading Components

```cpp
// AddComponent constructs in-place — asserts if the component already exists
auto& sprite = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);
auto& body   = player.AddComponent<MyRigidBodyComponent>(1.0f, 0.3f);

// GetComponent returns a mutable reference — asserts if the component is absent
auto& transform = player.GetComponent<Cosmic::TransformComponent>();
transform.Position   = { 2.0f, 0.5f, 0.0f };
transform.Rotation.z = 45.0f;   // Z-axis rotation in DEGREES
transform.Scale      = { 1.0f, 1.0f };

// Check before access when existence is uncertain
if (player.HasComponent<MyRigidBodyComponent>())
{
    auto& rb = player.GetComponent<MyRigidBodyComponent>();
    rb.Velocity = { 3.0f, 0.0f };
}

// Remove a component
player.RemoveComponent<MyRigidBodyComponent>();

// Entity handle boolean — true when the handle is valid, scene-bound, and the
// underlying registry slot is still alive. Returns false after DestroyEntity.
if (player) { /* handle is valid */ }
```

> **Dangling-handle warning:** `Entity` is a lightweight value type. Any copy of a handle held after `Scene::DestroyEntity(e)` becomes invalid — `operator bool` returns `false` and all `GetComponent`/`HasComponent` calls will assert. Always discard `Entity` handles after calling `DestroyEntity`, and do not cache handles across frames without re-validating with `if (handle)` each frame.

### Built-in Components

**`TransformComponent`**

```cpp
struct TransformComponent {
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll rotation in DEGREES
    glm::vec2 Scale    { 1.f, 1.f };

    glm::mat4 GetTransform() const; // full TRS matrix for shader upload
};
```

**`SpriteRendererComponent`**

```cpp
struct SpriteRendererComponent {
    Ref<Material> ActiveMaterial;                   // shader-driven rendering
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f };   // flat-color fallback / tint
    bool FlipX = false;
    bool FlipY = false;
};
```

**`TagComponent`**

```cpp
struct TagComponent { std::string Tag; };
```

### Querying Entities

Use `Scene::View<Components...>()` to iterate entities that have all the listed component types:

```cpp
// Iterate all entities with both TransformComponent and SpriteRendererComponent
for (auto [entity, transform, sprite] : m_Scene->View<Cosmic::TransformComponent, Cosmic::SpriteRendererComponent>().each())
{
    transform.Position.x += speed * ts;
}
```

### Registering Custom Components Across DLL Boundaries

EnTT normally assigns type IDs using sequential static counters. Across a DLL boundary the engine and the client DLL have separate data segments, so the same component type can get different IDs on each side — breaking component storage. The `CS_REGISTER_COMPONENT` macro fixes this by forcing a stable compile-time string hash:

```cpp
// In your component header file — once per component type
#include <scene/ComponentRegistry.h>

struct MyPhysicsBody { ... };

CS_REGISTER_COMPONENT(Workspace::MyPhysicsBody)
```

Omitting this macro for any component type that crosses the DLL boundary will cause silent data corruption. Built-in engine components (`TagComponent`, `TransformComponent`, `SpriteRendererComponent`) are already registered in `Components.h`.

---

## 16. Camera System

### OrthographicCameraController

`OrthographicCameraController` is the high-level camera wrapper. It handles WASD panning, smooth scroll-wheel zoom interpolation, and aspect-ratio correction on window resize.

```cpp
// Construction
Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f };           // no rotation
Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f, true };     // enable Q/E rotation

// Per-frame update (must be called from OnUpdate)
m_Camera.OnUpdate(ts);

// Event forwarding (must be called from OnEvent)
m_Camera.OnEvent(e);

// Window resize (call from OnWindowResize handler)
m_Camera.OnResize((float)e.GetWidth(), (float)e.GetHeight());

// Pass to Renderer2D
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

// Get/set position
const glm::vec3& pos = m_Camera.GetPosition();
m_Camera.SetPosition({0.f, 0.f, 0.f});

// Zoom
// Two zoom methods — choose based on whether you want instant or animated:
//   SetZoomLevel      — hard-snaps both current and target zoom instantly, no interpolation.
//                       Use for initialization, teleports, or ImGui slider driven overrides.
//   SetTargetZoomLevel — sets only the target; OnUpdate's asymptotic blend animates toward it.
//                       Use when focusing on a game object or responding to a game event smoothly.
float z = m_Camera.GetZoomLevel();
m_Camera.SetZoomLevel(2.0f);           // hard snap — bypasses interpolation entirely
m_Camera.SetTargetZoomLevel(2.0f);     // smooth animated zoom toward 2.0 over subsequent frames
m_Camera.SetZoomLimits(0.25f, 10.0f); // clamp scroll range (these are the defaults)
m_Camera.SetZoomSpeed(0.25f);          // world units per scroll tick (default)

// Pan speed (scales with zoom so panning feels consistent at any zoom level)
m_Camera.SetTranslationSpeed(5.0f);   // default
m_Camera.GetTranslationSpeed();

// Rotation (only active when rotation=true was passed to the constructor)
m_Camera.SetRotationSpeed(180.0f);    // degrees per second (default)
m_Camera.GetRotationSpeed();

// Pan bounds
m_Camera.SetPositionLimits(-50.f, 50.f, -30.f, 30.f); // minX, maxX, minY, maxY

// Disable keyboard panning for scripted cameras / cutscenes
m_Camera.SetManualMovementEnabled(false);
m_Camera.IsManualMovementEnabled();

// Custom key bindings
Cosmic::OrthographicCameraController::CameraKeyBindings bindings;
bindings.MoveLeft  = CS_KEY_LEFT;
bindings.MoveRight = CS_KEY_RIGHT;
bindings.MoveUp    = CS_KEY_UP;
bindings.MoveDown  = CS_KEY_DOWN;
m_Camera.SetKeyBindings(bindings);
```

### OrthographicCamera (Low-Level)

If you need direct matrix control, bypass the controller and use `OrthographicCamera` directly:

```cpp
Cosmic::OrthographicCamera cam(-8.f, 8.f, -4.5f, 4.5f); // left, right, bottom, top

cam.SetPosition({1.f, 0.f, 0.f});
cam.SetRotation(45.f); // Z-axis rotation in degrees

const glm::mat4& vp  = cam.GetViewProjectionMatrix();
const glm::mat4& v   = cam.GetViewMatrix();
const glm::mat4& p   = cam.GetProjectionMatrix();
const glm::vec3& pos = cam.GetPosition();
float rot            = cam.GetRotation();

// Update frustum bounds (e.g. after resize)
cam.SetProjection(-aspect, aspect, -1.f, 1.f);
```

> **Implementation note:** `UpdateViewMatrix` uses the closed-form inverse for a translate+rotate
> transform (`transpose(R) * T(-pos)`) rather than `glm::inverse()`. For a pure Z-rotation camera,
> `transpose(R) == inverse(R)` because rotation matrices have orthonormal columns. This eliminates
> the general-purpose Cramer's rule path (~80 ops) in favour of roughly 10 multiplications,
> called on every `SetPosition` / `SetRotation` mutation.

---

## 17. Virtual File System

`FileSystem` is a static utility that translates URI-style paths into physical disk paths. It keeps asset references independent of absolute disk locations.

### Protocol Prefixes

| Prefix       | Resolves to                              | Use for                       |
| ------------ | ---------------------------------------- | ----------------------------- |
| `engine://`  | `assets/{path}`                          | Engine-owned shaders, sprites |
| `project://` | `assets/projects/{activeProject}/{path}` | Your project's own assets     |
| _(none)_     | Returned unchanged (raw path fallback)   | Absolute or already-resolved  |

### Setting the Active Project

Call this once in your root layer's constructor or `OnAttach`. It sets the `project://` prefix for all subsequent `Resolve` calls from this DLL:

```cpp
YourProject::YourProject() : Cosmic::Layer("YourProject")
{
    Cosmic::FileSystem::SetActiveProject("YourProject");
}
```

The name must match the project subfolder under `assets/projects/`.

### Resolving Paths

```cpp
std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl");
// → "assets/projects/YourProject/shaders/Fire.glsl"

std::string engineTex = Cosmic::FileSystem::Resolve("engine://textures/white.png");
// → "assets/textures/white.png"

auto shader  = Cosmic::Shader::Create(shaderPath);
auto texture = Cosmic::Texture2D::Create(shaderPath); // same pattern
```

> **Path separator normalization:** `Resolve` uses `std::filesystem::path` internally and always returns forward-slash-separated paths on all platforms. Mixed separators in the suffix (e.g. `engine://shaders\\MyShader.glsl`) are normalized automatically. The result is accepted directly by `std::ifstream`, `Shader::Create`, and `Texture2D::Create`.

> **Thread-safety:** `SetActiveProject` is not thread-safe. It must only be called from the main thread. No worker thread may call `Resolve` with a `project://` path concurrently with a `SetActiveProject` call. If background asset loading is introduced, guard both with a `shared_mutex`.

### Log Directory Relocation

The log system can be redirected into your project's asset folder at attach time:

```cpp
void YourProject::OnAttach()
{
    Cosmic::FileSystem::SetActiveProject("YourProject");
    std::string logPath = Cosmic::FileSystem::Resolve("project://logs");
    Cosmic::Log::SetLogDirectory(logPath);
}

void YourProject::OnDetach()
{
    // Restore engine default on exit
    Cosmic::Log::SetLogDirectory("logs");
}
```

---

## 18. Framebuffer

### Why Framebuffers Exist

By default, every OpenGL draw call writes pixels directly to the screen's back buffer — the surface that gets swapped to the monitor at the end of each frame. This is the fastest path for full-screen rendering, but it creates a problem when you want to display your scene _inside_ a UI panel rather than directly on the screen.

Cosmic's workspace shell renders the entire engine UI (the sidebar, toolbar, and viewport panels) using ImGui, which itself draws into the screen's back buffer. If your scene also draws into the back buffer at the same time, the two sets of draw calls fight over the same pixels with no way to composite them correctly.

The solution is a **Framebuffer Object (FBO)** — a GPU-resident render target that acts like a virtual screen. Instead of writing pixels to the display surface, you redirect all your draw calls into an FBO. The FBO's color output is stored as a regular GPU texture. Once rendering is complete, ImGui can display that texture as an image inside any panel — the scene becomes just a `ImGui::Image` call, composited correctly over the rest of the UI.

```
Without FBO:                     With FBO:
Your draw calls  ──────────►  Back buffer (screen)     Your draw calls  ────►  FBO texture
ImGui draw calls ──────────►  Back buffer (screen)     ImGui draw calls ────►  Back buffer
                                                            └── ImGui::Image(fbo_texture) ──► Back buffer
```

This is exactly what Cosmic's `WorkspaceLayer` does: it creates a main FBO at startup, calls your layer's `OnUpdate` with the FBO bound, then takes the FBO's color texture and displays it inside the viewport panel using `ImGui::Image`. From your layer's perspective, you just issue draw calls normally — you don't need to manage the main FBO yourself.

You only need to create your own FBOs when you want **secondary render targets**: a minimap, a portal view, a render-to-texture effect, or a post-processing pass that composites multiple scenes.

### Creating a Framebuffer

```cpp
Cosmic::FramebufferSpecification spec;
spec.Width          = 1280;
spec.Height         = 720;
spec.Samples        = 1;           // Reserved — MSAA not yet implemented; leave at 1
spec.SwapChainTarget = false;      // Reserved — not yet implemented; leave at false

Ref<Cosmic::FrameBuffer> fbo = Cosmic::FrameBuffer::Create(spec);
```

### Using a Framebuffer

```cpp
// Redirect rendering into the FBO
m_Fbo->Bind();

Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
// ... draw calls ...
Cosmic::Renderer2D::EndScene();

// Restore the default (screen) framebuffer
m_Fbo->Unbind();

// Handle window resize
void MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    m_Fbo->Resize(e.GetWidth(), e.GetHeight());
}
```

### Displaying in ImGui

```cpp
void MyLayer::OnImGuiRender()
{
    ImGui::Begin("Viewport");

    // GetColorAttachmentRendererID() returns the OpenGL texture handle
    uint32_t texID = m_Fbo->GetColorAttachmentRendererID();
    ImVec2   size  = ImGui::GetContentRegionAvail();
    ImGui::Image((void*)(intptr_t)texID, size, ImVec2{0,1}, ImVec2{1,0}); // flip Y

    ImGui::End();
}
```

### Accessing the Engine's Main Framebuffer

```cpp
Ref<Cosmic::FrameBuffer> engineFbo = Cosmic::Application::Get().GetFrameBuffer();
uint32_t w = engineFbo->GetWidth();
uint32_t h = engineFbo->GetHeight();
const Cosmic::FramebufferSpecification& spec = engineFbo->GetSpecification();
```

---

## 19. Logging

Cosmic wraps spdlog into a set of format-string macros. Use the `CS_` prefix in all client code. Engine-internal code uses `CS_CORE_`.

```cpp
CS_TRACE("Entering state: {}", stateName);
CS_INFO("Entity spawned at ({:.2f}, {:.2f})", x, y);
CS_WARN("Shader path not found: {}", path);
CS_ERROR("Failed to load texture: {}", filename);
CS_CRITICAL("Out of GPU memory — aborting.");
```

All macros accept spdlog-style format strings (equivalent to `{fmt}`). Positional `{0}`, `{1}` syntax and format specifiers like `{:.2f}` (two decimal places) are both supported.

| Macro         | Level    | When to use                                      |
| ------------- | -------- | ------------------------------------------------ |
| `CS_TRACE`    | Trace    | High-frequency per-frame diagnostics             |
| `CS_INFO`     | Info     | Normal lifecycle events (attach, load, connect)  |
| `CS_WARN`     | Warning  | Recoverable issues (missing file, fallback used) |
| `CS_ERROR`    | Error    | Non-fatal failures                               |
| `CS_CRITICAL` | Critical | Unrecoverable failures                           |

The logger is thread-safe and protected by a shared mutex, so it can be called from job worker threads (e.g., inside a `ParallelSystem`).

---

## 20. Serial Communication

`Cosmic::SerialPort` provides thread-safe RS-232 serial communication on Windows. A background thread continuously polls the hardware port and accumulates data in a mutex-protected buffer.

```cpp
Cosmic::SerialPort port;

// Discover available ports
std::vector<std::string> ports = Cosmic::SerialPort::GetAvailablePorts();
// → {"COM3", "COM7", ...}

// Open — configures 8N1, spawns the background read thread
if (port.Open("COM3", 115200))
    CS_INFO("Connected to COM3");

// Poll from OnFixedUpdate — deterministic rate matches serial timing
void MyLayer::OnFixedUpdate(float dt) override
{
    if (!port.IsOpen()) return;
    std::string data = port.FlushBuffer(); // thread-safe; clears the buffer
    if (!data.empty())
        ParseTelemetry(data);
}

// Disconnect — signals the read thread to stop and joins it
port.Close();
```

> **Windows only.** `SerialPort` uses Win32 APIs (`CreateFile`, OVERLAPPED I/O, Registry enumeration). It will not compile on Linux/macOS without a platform implementation.

> **Device disconnect handling.** `ReadLoop` now treats any `ReadFile` error other than `ERROR_TIMEOUT` (e.g., a USB pull or I/O fault) as a fatal disconnect: it logs a warning, sets `m_Connected` to `false`, and exits. Callers polling `IsOpen()` will observe the disconnection automatically without needing to call `Close()`.

> **Write support (planned).** The port is opened with `GENERIC_READ | GENERIC_WRITE`. A `Write(const std::string&)` method is planned but not yet implemented or exposed in the API.

> **Bounded `Close()`.** `Close()` calls `CancelIoEx` before joining the read thread, so any in-flight `ReadFile` call is unblocked immediately. Destruction is bounded and does not stall the caller for a full timeout interval (up to 50 ms).

> **Job system compatibility.** The serial read thread is a raw `std::thread` created by `Open()` — it is entirely separate from the job system's worker pool. The job system consuming all of its threads has no effect on the serial thread, which keeps running and filling its buffer regardless. The safe integration point is `OnFixedUpdate`: by the time that hook fires, the parallel job pass for that frame has not yet dispatched, so any component state you write from `FlushBuffer()` data is already settled before job workers read it. This ordering is automatic — no extra synchronization is required.

---

## 21. The Template Project

The Template Project (`Cosmic/templates/ExampleProject/`) is the canonical reference implementation for building multi-mode projects with Cosmic. It demonstrates the correct patterns for time management, material-driven rendering, ECS integration, parallel physics, and the composite layer architecture.

### Generating a Project from the Launcher

The recommended way to create a new project based on this template is through the engine's Launcher screen. Click **New Project**, enter your project name and target directory, and the Launcher copies the full template, renames every file and class, and generates a wired `CMakeLists.txt` and `build.bat` for you. Run `build_all.bat` once afterward to register and compile the new project.

You can also copy `Cosmic/templates/ExampleProject/` manually and rename things yourself — the Launcher just automates that process.

### Project Structure

```
ExampleProject/
├── src/
│   ├── TemplateProject.h / .cpp          ← Root manager layer (DLL entry point)
│   ├── TemplateRenderLayer.h / .cpp      ← Material + camera demo
│   ├── TemplateSpriteLayer.h / .cpp      ← Sprite sheet + ECS demo
│   ├── TemplateRenderBenchmarkLayer.h / .cpp  ← Instanced rendering stress test
│   ├── BallPhysicsSystem.h               ← Parallel physics (ParallelSystem example)
│   └── Components.h                      ← Custom component definitions + CS_REGISTER_COMPONENT
├── assets/
│   ├── shaders/                          ← Project-specific GLSL files
│   └── sprites/                          ← Sprite sheets
├── CMakeLists.txt
└── build.bat
```

### Composite Layer Architecture

`TemplateProject` is a root manager `Layer` that owns a `std::vector<std::shared_ptr<Layer>> m_Modes`. The three child layers are **never pushed onto the engine's `LayerStack`** — they are driven exclusively by the root layer's hooks:

```cpp
// In TemplateProject::OnUpdate
auto& activeMode = m_Modes[m_ActiveModeIndex];

// Drive the mode's local clock. ts is globally-scaled; UpdateLayerTime multiplies
// it again by the mode's own scale so GetLocalTime() reflects both.
activeMode->UpdateLayerTime(ts);

if (m_SharedMaterial)
    m_SharedMaterial->Set("u_Time", activeMode->GetLocalTime());

// Pass a fully-scaled delta (global × layer) to the child layer so that movement,
// camera, and any sub-system ticks inside it respond to the Layer TimeScale slider
// without each child needing to re-apply GetTimeScale() manually.
activeMode->OnUpdate(ts * activeMode->GetTimeScale());

// In TemplateProject::OnFixedUpdate
auto& active = m_Modes[m_ActiveModeIndex];
// Same principle: apply layer scale here so physics also slows/pauses independently.
// Child layers guard dt <= 0 to detect pause and rewind; they do not call GetTimeScale() again.
active->OnFixedUpdate(deltaFixedTime * active->GetTimeScale());

// In TemplateProject::OnEvent — resize goes to ALL modes; input goes only to active
if (e.IsInCategory(Cosmic::EventCategoryApplication))
{
    for (auto& mode : m_Modes)
        mode->OnEvent(e);
    return;
}
m_Modes[m_ActiveModeIndex]->OnEvent(e);
```

> **The Double-Tick Trap:** Do **not** push child layers onto `Application`'s `LayerStack` while also driving them manually from the root layer. Doing so causes every hook (`OnUpdate`, `OnFixedUpdate`, `OnEvent`) to fire twice per frame — once from the engine stack and once from your manual forward call. This produces double-speed simulation, double rendering, and subtly corrupted accumulated times.

### Global Time Scale Control

The Inspector panel exposes a `TimeScale` slider that modifies the host engine's global time directly:

```cpp
float hostScale = Cosmic::Application::Get().GetTimeScale();
if (ImGui::SliderFloat("Global TimeScale", &hostScale, -2.0f, 3.0f, "%.2fx"))
    Cosmic::Application::Get().SetTimeScale(hostScale);
```

Setting this to zero pauses all modes simultaneously. Setting it negative reverses time — the shared material's `u_Time` scrubs backward automatically because it's driven by `GetLocalTime()`, which reflects both the global and per-layer scale. The per-layer **Layer TimeScale** slider controls only the active mode, independently of the global scale.

### Shared Material Pattern

A single `Ref<Material>` (`m_SharedMaterial`) is created once in `OnAttach`, passed by `Ref<>` to child layers that need it, and updated from the root layer:

```cpp
// OnAttach
auto shader     = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/TemplateShader.glsl"));
m_SharedMaterial = Cosmic::Material::Create(shader, "TemplateMaterial");
m_SharedMaterial->Set("u_Color", glm::vec4(1.f, 0.6f, 0.2f, 1.f));

// OnUpdate — time from the active mode's local clock
if (m_SharedMaterial)
    m_SharedMaterial->Set("u_Time", m_Modes[m_ActiveModeIndex]->GetLocalTime());
```

Because the material is a `Ref<>`, all child layers holding it automatically see the updated `u_Time` uniform without any additional plumbing.

### Adding Your Own Mode

1. Create a class inheriting `Cosmic::Layer`.
2. Implement `OnAttach`, `OnDetach`, `OnUpdate`, `OnFixedUpdate`, `OnImGuiRender`, `OnEvent`.
3. In the root layer's `OnAttach`, add it to the modes vector:

```cpp
m_Modes.push_back(std::make_shared<MyNewMode>(m_Scene, m_SharedMaterial));
m_Modes.back()->OnAttach();
```

4. **Do not** push it onto `Application`'s `LayerStack`.

---

## 22. Job System & Parallel Pipeline

### Why Parallelism?

Modern CPUs have many cores — a typical development machine has 8–16 logical processors. A serial simulation loop uses exactly one of them. The other 7–15 cores sit idle while your physics, collision, and AI updates grind through thousands of entities one at a time.

This matters most for particle systems, physics simulations, and agent AI where you have many independent entities that all need the same per-frame update. Consider a simulation with 50,000 physics balls. Each ball's velocity and position for this frame depends only on its own state from last frame — not on any other ball's current-frame position. That independence is the key: if no ball's update needs to wait for another ball's result, all 50,000 can be computed simultaneously across all available cores.

The cost of that parallelism is **coordination**: you need to guarantee that workers read from a stable copy of the data (not from a buffer another worker is currently writing), collect all results after the workers finish, and write them back in a thread-safe way. Cosmic's parallel pipeline handles all of that bookkeeping for you through the `ParallelSystem` + `SystemQuery` abstraction.

### The Four Passes — Why Each Exists

`Scene` ticks systems through a guaranteed four-pass pipeline each frame. Understanding why each pass is separate is more important than memorizing its name:

**Pass A — Sequential Systems (`System::OnUpdate` / `System::OnFixedUpdate`)**

Some logic fundamentally cannot run in parallel: spawning or destroying entities modifies the registry's internal data structures; logic that reads one entity's component while writing another's has data races; systems with strict ordering dependencies (A must run before B) break if interleaved. Pass A is the safe, single-threaded space for all of that. Run your spawners, your state machines, your serial game logic here. Nothing in this pass can conflict with anything else because it is strictly ordered and single-threaded.

**Pass B — Parallel Prepare (`ParallelSystem::OnFixedPrepare`)**

Before worker threads can process component data, the engine needs a **stable snapshot** of that data — a copy that won't change while workers are running. Pass B runs on the main thread and the engine automatically calls `Stage()` on all registered `SystemQuery` members before your `OnPrepare` override runs. By the time `OnPrepare` executes, your queries hold a frozen copy of the registry data. Use this pass for per-tick setup that needs to happen before the parallel work: recording start timestamps for profiling, computing derived constants the workers will read (e.g., pre-scaling gravity by `dt`), or validating that there's work to do.

**Pass C — Parallel Execute (`ParallelSystem::OnFixedParallelExecute`)**

This is where the actual parallel work happens. All systems have their staged data ready. Each `ParallelSystem` submits jobs to the thread pool and returns immediately — it does **not** wait for its jobs to finish before returning. After every system has submitted its work, the Scene calls a single `JobSystem::WaitIdle()` barrier. This means different systems' worker jobs can overlap in time, maximizing core utilization. Inside this pass you must only read from staged query data and write to staged output buffers — never touch the EnTT registry, never create or destroy entities.

**Pass D — Parallel Merge (`ParallelSystem::OnFixedMerge`)**

`WaitIdle()` has returned — every worker job is done. This pass runs on the main thread and is the place to collect results: sync computed positions back to `TransformComponent`, resolve collisions that were detected in parallel, create or destroy entities based on the simulation output. After your `OnMerge` override returns, the engine automatically calls `Commit()` on all `ReadWriteQuery` members, writing the staged component data back to the live registry. Structural changes (create/destroy) are safe here because you're back on the main thread with no workers running.

```
FRAME TICK:
  │
  ├── PASS A  (main thread, sequential)
  │   Serial systems: OnUpdate / OnFixedUpdate
  │   Safe for: entity create/destroy, ordered logic, registry writes
  │
  ├── PASS B  (main thread, sequential)
  │   [engine stages all SystemQuery snapshots]
  │   ParallelSystems: OnFixedPrepare
  │   Safe for: per-tick setup, profiling start, constant pre-computation
  │
  ├── PASS C  (worker threads, concurrent)
  │   ParallelSystems: OnFixedParallelExecute  ← submit jobs, return immediately
  │   [Scene calls JobSystem::WaitIdle() once after ALL systems submit]
  │   Safe for: read staged data, write staged output — no registry access
  │
  └── PASS D  (main thread, sequential)
      ParallelSystems: OnFixedMerge
      [engine commits all ReadWriteQuery results to registry]
      Safe for: sync results to transform/render components, entity create/destroy
```

Variable-rate equivalents (`OnPrepare`, `OnParallelExecute`, `OnMerge`) follow the same pattern and run inside `Scene::OnUpdate`.

### The Three Abstraction Levels

```
JobSystem             ← raw thread pool; submit arbitrary callables
ParallelFor           ← distribute an index range or typed array across all workers
ParallelSystem        ← structured 4-pass parallel integration within the ECS
  └── SystemQuery<T>  ← automatic component staging/commit for ParallelSystem
```

### JobSystem — Raw Thread Pool

```cpp
Cosmic::JobSystem& js = Cosmic::JobSystem::Get();

// Submit independent work units
for (auto& chunk : chunks)
    js.Submit([&chunk]{ ProcessChunk(chunk); });

// Block the calling thread until all enqueued jobs are complete
js.WaitIdle();

// Query the number of worker threads
uint32_t workers = js.GetWorkerCount(); // = logical cores - 1
```

The pool is created once at engine init and the threads persist until shutdown. Use it for ad-hoc parallelism outside of the ECS (e.g., asset streaming, data preprocessing).

### ParallelFor — Index Range Distribution

`ParallelFor` splits a range across all worker threads and synchronizes before returning. Use it for standalone parallel work that doesn't belong inside an ECS system:

```cpp
// Synchronous — submits AND calls WaitIdle before returning. Safe to use anywhere.
Cosmic::ParallelFor(entityCount, [&transforms, dt](size_t begin, size_t end)
{
    for (size_t i = begin; i < end; ++i)
        transforms[i].x += velocities[i].x * dt;
});

// Typed array variant
Cosmic::ParallelForEach(transforms.data(), transforms.size(),
    [dt](TransformData* begin, TransformData* end)
    {
        for (auto* t = begin; t != end; ++t)
            t->Position.x += t->Velocity.x * dt;
    });

// Element + index variant (useful when you need the entity ID alongside the component)
Cosmic::ParallelForEachIndexed(bodies.data(), bodies.size(),
    [](PhysicsBody& body, size_t i)
    {
        ApplyGravity(body, 9.8f);
    });
```

**Async variants** (`ParallelForAsync`, `ParallelForEachAsync`, `ParallelForEachIndexedAsync`) submit jobs without calling `WaitIdle`. Use these exclusively inside `ParallelSystem::OnParallelExecute` — the `Scene` issues a single `WaitIdle` barrier after all systems submit, maximizing overlap.

### ParallelSystem — Structured ECS Parallel Integration

Subclass `ParallelSystem` when your simulation logic can be expressed as a per-entity transformation that runs in parallel on component data. The Scene ticks all parallel systems through a guaranteed four-pass pipeline each frame:

```
PASS A — Sequential Systems (main thread)
    for each System* s  → s->OnUpdate(scene, dt)
    for each System* s  → s->OnFixedUpdate(scene, dt)

PASS B — Parallel Prepare (main thread, single-threaded)
    for each ParallelSystem* ps → [stage queries] → ps->OnPrepare(scene, dt)

PASS C — Parallel Execute (worker threads simultaneously)
    for each ParallelSystem* ps → ps->OnParallelExecute(scene, dt)
    JobSystem::WaitIdle()   ← main thread blocks here

PASS D — Parallel Merge (main thread, single-threaded)
    for each ParallelSystem* ps → ps->OnMerge(scene, dt)
    [commit queries]
```

Fixed-step equivalents (`OnFixedPrepare`, `OnFixedParallelExecute`, `OnFixedMerge`) run through the same pipeline inside the fixed timestep pass.

**Key rules:**

- Inside `OnParallelExecute`: use only the **Async** `ParallelFor` variants. The synchronous variants call `WaitIdle` internally and serialize all systems against each other.
- Do **not** call `JobSystem::WaitIdle()` yourself from `OnParallelExecute` — the Scene calls it once after all systems have submitted.
- Do **not** modify the EnTT registry from a worker thread. Read from staged data; write to staged output buffers.
- Structural changes (create/destroy entities) must happen in `OnMerge` after `WaitIdle`, never in `OnParallelExecute`.

### SystemQuery — Automatic Component Staging

`ReadWriteQuery<T>` and `ReadOnlyQuery<T>` are the primary data-access API for `ParallelSystem` subclasses. Declare them as member variables and pass `this` — the engine stages and commits them automatically around the parallel passes. No manual snapshot or writeback code is needed.

```cpp
class BallPhysicsSystem : public Cosmic::ParallelSystem
{
    // Declare as member — pass `this` to self-register with this system.
    // Stage (snapshot from registry) happens before OnFixedPrepare.
    // Commit (write results back) happens after OnFixedMerge.
    Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };

public:
    float Gravity = -9.8f;
    float Damping = 0.85f;

    void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
    {
        if (m_Bodies.IsEmpty()) return;

        const float gravity = Gravity;
        const float damping = Damping;
        const float dt      = fixedDt;

        // ForEachAsync captures by value — safe across thread lifetime
        m_Bodies.ForEachAsync([gravity, damping, dt](PhysicsBody& body)
        {
            body.Velocity.y += gravity * dt;
            body.Velocity   *= glm::clamp(1.0f - damping * dt, 0.0f, 1.0f);
            body.Position   += body.Velocity * dt;
        });
        // Do NOT call WaitIdle here — the Scene does it after all systems submit
    }

    void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
    {
        // All jobs are done. Sync computed positions to TransformComponent.
        auto& reg = scene.GetRegistry();
        m_Bodies.ForEachWithEntity([&reg](PhysicsBody& body, entt::entity entity)
        {
            if (!reg.valid(entity)) return;
            auto& t   = reg.get<Cosmic::TransformComponent>(entity);
            t.Position = { body.Position.x, body.Position.y, t.Position.z };
        });
        // Engine commits m_Bodies → PhysicsBody registry AFTER this returns
    }
};
```

**Register with the Scene:**

```cpp
m_Scene->AddSystem<BallPhysicsSystem>();

// Or capture a reference to configure it after creation
auto& phys = m_Scene->AddSystem<BallPhysicsSystem>();
phys.Gravity = -15.f;
phys.Damping = 0.9f;
```

### ReadOnlyQuery — Stable Cross-Entity Snapshots

Use `ReadOnlyQuery<T>` when your algorithm needs to read the whole component dataset (including elements being written by other workers) without race conditions — e.g., collision detection, flocking, or influence fields:

```cpp
class CollisionSystem : public Cosmic::ParallelSystem
{
    Cosmic::ReadOnlyQuery<PhysicsBody>  m_ReadBodies{ this };  // stable snapshot
    Cosmic::ReadWriteQuery<PhysicsBody> m_WriteBodies{ this }; // output

    void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
    {
        const PhysicsBody* stable = m_ReadBodies.Data();
        size_t count = m_WriteBodies.Count();

        m_WriteBodies.DispatchAsync([stable, count, dt](PhysicsBody* begin, PhysicsBody* end)
        {
            for (auto* body = begin; body != end; ++body)
            {
                // Safe: reads from stable snapshot, writes to separate output
                for (size_t j = 0; j < count; ++j)
                    ResolveCollision(*body, stable[j]);
            }
        });
    }
};
```

### SystemQuery API Summary

| Method                    | Available In        | Description                                                     |
| ------------------------- | ------------------- | --------------------------------------------------------------- |
| `ForEachAsync(func)`      | OnParallelExecute   | Submit parallel per-element jobs — `func(T& item)`              |
| `DispatchAsync(func)`     | OnParallelExecute   | Submit parallel range jobs — `func(T* begin, T* end)`           |
| `ForEach(func)`           | OnPrepare / OnMerge | Sequential iteration — `func(T& item)`                          |
| `ForEachWithEntity(func)` | OnMerge             | Sequential with entity handle — `func(T& item, entt::entity e)` |
| `Data()`                  | Any phase           | Raw pointer to the staged array                                 |
| `Count()`                 | Any phase           | Number of staged elements                                       |
| `IsEmpty()`               | Any phase           | True if no components of type T exist in the scene              |
| `operator[](i)`           | Any phase           | Indexed access to a staged element                              |
| `EntityAt(i)`             | Any phase           | Entity handle for element at index `i`                          |

---

## 23. Scene System

`Scene` owns the EnTT registry and is the central coordinator for entities, components, and systems.

### Creating and Using a Scene

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// Tick systems from your layer hooks
void MyLayer::OnUpdate(float ts)
{
    m_Scene->OnUpdate(ts);         // runs all sequential system OnUpdate passes
    m_Scene->OnRender(m_Camera.GetCamera()); // draws all SpriteRendererComponent entities
}

void MyLayer::OnFixedUpdate(float dt)
{
    m_Scene->OnFixedUpdate(dt);    // sequential + parallel fixed-step passes
}
```

### Registering Systems

```cpp
// Sequential system — single-threaded, pointer-based ECS logic
class SpawnSystem : public Cosmic::System
{
public:
    void OnUpdate(Cosmic::Scene& scene, float dt) override
    {
        // use scene.View<...>() to iterate entities
    }
};

m_Scene->AddSystem<SpawnSystem>();

// Parallel system — see Section 22 for the full API
m_Scene->AddSystem<BallPhysicsSystem>();

// Get a reference to an already-registered system.
// WARNING — O(n): GetSystem<T> performs a dynamic_cast loop over all registered
// systems. Do NOT call this per-frame. Cache the result in OnAttach and reuse it.
BallPhysicsSystem* phys = m_Scene->GetSystem<BallPhysicsSystem>(); // call once, in OnAttach
if (phys) phys->Gravity = -15.f;

// Remove all systems (e.g. on level unload)
m_Scene->RemoveAllSystems();
```

### Scene::OnRender

`OnRender` handles the full render pass autonomously — it calls `BeginScene` with the provided camera, iterates all entities that have both `TransformComponent` and `SpriteRendererComponent`, groups them by material bucket to minimize draw calls, sorts each bucket by ascending `Position.z` for correct depth order, then calls `EndScene`. Do **not** wrap a `Scene::OnRender` call inside your own `BeginScene`/`EndScene` pair.

> **Performance note — per-bucket z-sort:** Entities within each material bucket are sorted by `Position.z` every frame using `std::sort` (O(n log n) per bucket). For typical bucket sizes this cost is negligible. If you have thousands of sprites sharing a single material, the sort can become measurable. Two mitigations to consider if profiling reveals it as a hotspot: (1) a **dirty flag** — skip the sort in frames where no z-values changed; (2) a **pre-sorted container** (e.g. `std::multiset`) maintained incrementally rather than rebuilt each frame.

```cpp
// Correct
m_Scene->OnRender(m_Camera.GetCamera());

// Wrong — double BeginScene/EndScene
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
m_Scene->OnRender(m_Camera.GetCamera()); // ← don't do this
Cosmic::Renderer2D::EndScene();
```

### Direct Registry Access

For advanced usage that doesn't fit `View<>`, access the underlying EnTT registry directly:

```cpp
entt::registry& reg = m_Scene->GetRegistry();

// Delete an entity
m_Scene->DestroyEntity(entity);
```

---

## 24. Window System

`Window` is accessed through `Application::Get().GetWindow()`. It abstracts GLFW and provides the surface for rendering, event callbacks, and fullscreen control.

### Basic Queries

```cpp
Cosmic::Window& win = Cosmic::Application::Get().GetWindow();

uint32_t w = win.GetWidth();
uint32_t h = win.GetHeight();

win.SetVSync(true);   // enable vertical sync
win.SetVSync(false);  // disable

GLFWwindow* handle = win.GetHandle(); // raw GLFW handle for API-specific calls
```

### Fullscreen

Fullscreen is implemented as **borderless-windowed fullscreen** — the window's style bits are stripped and it is stretched to cover the target monitor without switching display modes. This keeps Alt+Tab functional and avoids DWM interaction issues.

```cpp
win.SetFullscreen(true);   // go fullscreen on the current monitor
win.SetFullscreen(false);  // restore windowed mode
bool fs = win.IsFullscreen();
```

The default engine hotkey is **F11**. Client DLLs can override this with a custom key combination:

```cpp
// In OnAttach — register a custom hotkey (e.g. Alt+Enter)
auto& win = Cosmic::Application::Get().GetWindow();
win.SetFullscreenHotkeyOverride([](int key, int action, int mods) -> bool
{
    // key=257 (ENTER), action=1 (PRESS), mods=0x0002 (ALT)
    if (key == 257 && action == 1 && (mods & 0x0002))
    {
        auto& app = Cosmic::Application::Get();
        app.GetWindow().SetFullscreen(!app.GetWindow().IsFullscreen());
        return true; // consumed — prevents the engine's F11 handler from firing
    }
    return false; // not our combo — let the engine handle it
});

// In OnDetach — always clear before DLL unload
win.ClearFullscreenHotkeyOverride();
```

> Always call `ClearFullscreenHotkeyOverride()` from `OnDetach`. The callback captures a lambda that lives inside the DLL — if the DLL is unloaded before the engine clears the callback, the next keypress will invoke a dangling function pointer and crash.

### OpenGL Version Requirement

The engine requires **OpenGL 4.5** or higher. The GLFW context hint is set to 4.5 at window creation time, so the driver will refuse context creation and `glfwCreateWindow` will return null on hardware that does not support OpenGL 4.5. If that happens, a `CS_CORE_CRITICAL` log message is emitted and the constructor returns early without creating a graphics context.

### GLFW Single-Window Constraint

`glfwTerminate()` is called inside `Window::~Window()`. This is safe for the current single-window architecture, but it is a global operation — it destroys all remaining GLFW resources, not just those of the window being destructed. If a second `Window` instance is ever introduced, the first window's destructor will terminate GLFW and invalidate the second window's handles, crashing on the next `glfwPollEvents` call. The correct long-term fix is to move `glfwTerminate()` to `Application::Shutdown()`, balanced against the single `glfwInit()` call in `Window::Window()`. Until then, only one `Window` instance may exist at a time.

### Extra Pre-Docked Inspector Panels

Request additional pre-docked panels from `OnAttach` before the first ImGui frame:

```cpp
void YourProject::OnAttach()
{
    auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
    if (ws)
    {
        ws->RequestExtraDockedPanel({
            "Timeline",         // must match the ImGui::Begin("...") call in your layer
            ImGuiDir_Down,      // direction to split from the main viewport
            0.25f               // fraction of the viewport area to give to this panel
        });
    }
}
```

---

## 25. Complete API Reference Tables

### Renderer2D

| Function               | Parameters                                                                               | Description                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| `BeginScene`           | `const OrthographicCamera&`                                                              | Start a batch pass; cache VP matrix, reset buffers             |
| `EndScene`             | —                                                                                        | Flush all batched geometry to GPU                              |
| `PushRenderPass`       | `const glm::mat4& viewProj, const glm::vec4& viewportBounds`                             | Flush pending geometry; push new VP matrix + viewport          |
| `PopRenderPass`        | —                                                                                        | Flush; pop current pass; restore prior pass state              |
| `Flush`                | —                                                                                        | Submit all staged quads, lines, and circles to GPU immediately |
| `SetViewportSize`      | `uint32_t width, uint32_t height`                                                        | Update `u_ViewportSize` uniform source                         |
| `DrawQuad`             | `vec2/vec3 pos, vec2 size, vec4 color`                                                   | Flat-color quad; `vec2` inserts z=0                            |
| `DrawQuad`             | `vec2/vec3 pos, vec2 size, Ref<Texture>, float tiling=1, vec4 tint=white`                | Textured quad                                                  |
| `DrawQuad`             | `vec2/vec3 pos, vec2 size, Ref<Material>`                                                | Material/shader-driven quad                                    |
| `DrawQuad`             | `vec2/vec3 pos, vec2 size, Ref<SubTexture2D>, vec4 tint=white`                           | Sprite-atlas tile                                              |
| `DrawRotatedQuad`      | `vec2/vec3 pos, vec2 size, float rot, vec4 color`                                        | Rotated flat quad (rot in radians)                             |
| `DrawRotatedQuad`      | `vec2/vec3 pos, vec2 size, float rot, Ref<Texture>, float tiling=1, vec4 tint=white`     | Rotated textured quad                                          |
| `DrawRotatedQuad`      | `vec3 pos, vec2 size, float rot, Ref<Material>`                                          | Rotated material quad (vec3 only)                              |
| `DrawRotatedQuad`      | `vec2/vec3 pos, vec2 size, float rot, Ref<SubTexture2D>, vec4 tint=white`                | Rotated sprite-atlas tile                                      |
| `DrawCircle`           | `vec3 pos, vec2 size, vec4 color, float thickness, float fade, Ref<Shader>=nullptr`      | SDF circle; explicit thickness+fade required for vec3 overload |
| `DrawCircle`           | `vec2 pos, vec2 size, vec4 color, float thickness=1, float fade=0.005, Ref<Shader>=null` | SDF circle; z=0; thickness+fade have defaults                  |
| `DrawLine`             | `vec3 p0, vec3 p1, vec4 color`                                                           | Line segment between two world-space points                    |
| `DrawRect`             | `vec3 pos, vec2 size, vec4 color`                                                        | Wireframe rectangle (4 lines)                                  |
| `DrawInstancedQuads`   | `const InstanceQuadData* instances, uint32_t count, Ref<Shader>=nullptr`                 | Single GPU instanced draw call for N quads                     |
| `DrawInstancedCircles` | `const InstanceCircleData* instances, uint32_t count, Ref<Shader>=nullptr`               | Single GPU instanced draw call for N circles                   |
| `ResetStats`           | —                                                                                        | Clear draw call and geometry counters                          |
| `GetStats`             | —                                                                                        | Returns `Statistics` struct                                    |
| `SetStatsStatus`       | `bool enabled`                                                                           | Toggle stats recording                                         |

**`Statistics` struct:**

| Field                   | Type       | Description                         |
| ----------------------- | ---------- | ----------------------------------- |
| `DrawCalls`             | `uint32_t` | Number of GPU draw calls this frame |
| `QuadCount`             | `uint32_t` | Number of batched quads submitted   |
| `LineCount`             | `uint32_t` | Number of batched lines submitted   |
| `GetTotalVertexCount()` | `uint32_t` | `QuadCount * 4 + LineCount * 2`     |
| `GetTotalIndexCount()`  | `uint32_t` | `QuadCount * 6`                     |

---

### Material

| Function           | Parameters                   | Description                                                 |
| ------------------ | ---------------------------- | ----------------------------------------------------------- |
| `Material::Create` | `Ref<Shader>, string name`   | Factory — creates a new material                            |
| `Clone`            | `Ref<Material>, string name` | Deep-copy all uniform caches; shares the same `Ref<Shader>` |
| `Set`              | `string name, float`         | Set a scalar float uniform                                  |
| `Set`              | `string name, vec2`          | Set a 2-component vector uniform                            |
| `Set`              | `string name, vec3`          | Set a 3-component vector uniform                            |
| `Set`              | `string name, vec4`          | Set a 4-component vector uniform                            |
| `Set`              | `string name, Ref<Texture>`  | Bind a texture to a named slot                              |
| `GetFloat`         | `string name`                | Retrieve cached float (0.0 if missing)                      |
| `GetVector2`       | `string name`                | Retrieve cached vec2 (zero if missing)                      |
| `GetVector3`       | `string name`                | Retrieve cached vec3 (zero if missing)                      |
| `GetVector4`       | `string name`                | Retrieve cached vec4 (**`glm::vec4(1.0f)` — white — if missing**, not zero) |
| `GetTexture`       | `string name`                | Retrieve cached texture (`nullptr` if missing)              |
| `Bind`             | —                            | Bind shader and upload all cached uniforms                  |
| `GetShader`        | —                            | Returns the underlying `Ref<Shader>`                        |
| `GetName`          | —                            | Returns the material's debug name string                    |
| `HasFloat`         | `string name`                | True if the float uniform is set                            |
| `HasFloat2`        | `string name`                | True if the vec2 uniform is set                             |
| `HasFloat3`        | `string name`                | True if the vec3 uniform is set                             |
| `HasFloat4`        | `string name`                | True if the vec4 uniform is set                             |
| `HasTexture`       | `string name`                | True if the texture slot is set                             |

---

### Shader

| Function         | Parameters                     | Description                          |
| ---------------- | ------------------------------ | ------------------------------------ |
| `Shader::Create` | `string filepath`              | Load and compile from a `.glsl` file; returns `nullptr` on compilation or link failure — always null-check the result |
| `Bind`           | —                              | Activate in the GPU pipeline         |
| `Unbind`         | —                              | Deactivate                           |
| `SetInt`         | `string, int`                  | Upload integer uniform               |
| `SetIntArray`    | `string, int*, uint32_t count` | Upload integer array uniform         |
| `SetFloat`       | `string, float`                | Upload float uniform                 |
| `SetFloat2`      | `string, vec2`                 | Upload 2-component float uniform     |
| `SetFloat3`      | `string, vec3`                 | Upload 3-component float uniform     |
| `SetFloat4`      | `string, vec4`                 | Upload 4-component float uniform     |
| `SetMat3`        | `string, mat3`                 | Upload 3×3 matrix uniform            |
| `SetMat4`        | `string, mat4`                 | Upload 4×4 matrix uniform            |

---

### SubTexture2D

| Function / Constructor | Parameters                                                          | Description                                                                        |
| ---------------------- | ------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `CreateFromCoords`     | `Ref<Texture2D>, vec2 coords, vec2 cellSize, vec2 spriteSize={1,1}` | Static factory. `coords` is (column, row) in grid units from bottom-left.          |
| `SubTexture2D`         | `Ref<Texture2D>, vec2 min, vec2 max`                                | Direct UV-range constructor. `min`/`max` in normalized [0,1] texture space.        |
| `GetTexture()`         | —                                                                   | Returns `const Ref<Texture2D>&` — the parent atlas.                                |
| `GetTexCoords()`       | —                                                                   | Returns `const glm::vec2*` — pointer to the 4-element UV corner array (CCW order). |

---

### OrthographicCameraController

| Function                       | Parameters                                       | Description                                                                                 |
| ------------------------------ | ------------------------------------------------ | ------------------------------------------------------------------------------------------- |
| `OrthographicCameraController` | `float aspectRatio, bool rotation=false`         | Constructor. Enables Q/E rotation support if `rotation=true`.                               |
| `OnUpdate`                     | `float ts`                                       | Poll WASD input, advance zoom interpolation, update camera transform. Call from `OnUpdate`. |
| `OnEvent`                      | `Event&`                                         | Route `MouseScrolledEvent` to zoom, `WindowResizeEvent` to `OnResize`.                      |
| `OnResize`                     | `float width, float height`                      | Recalculate aspect ratio and projection. Call when the render target changes size.          |
| `GetCamera`                    | —                                                | Returns `OrthographicCamera&` for `BeginScene` or `RenderPass`.                             |
| `GetZoomLevel`                 | —                                                | Current interpolated zoom scalar.                                                           |
| `SetZoomLevel`                 | `float level`                                    | Hard-snap zoom, bypassing interpolation.                                                    |
| `SetZoomLimits`                | `float min, float max`                           | Clamp the scroll-wheel zoom range. Default: 0.25–10.0.                                      |
| `SetZoomSpeed`                 | `float speed`                                    | World units per scroll tick. Default: 0.25.                                                 |
| `GetZoomSpeed`                 | —                                                | Returns current zoom speed.                                                                 |
| `SetTranslationSpeed`          | `float speed`                                    | Pan speed in world units/second. Scales with zoom level. Default: 5.0.                      |
| `GetTranslationSpeed`          | —                                                | Returns current translation speed.                                                          |
| `SetRotationSpeed`             | `float speed`                                    | Degrees/second for Q/E rotation. Only active when `rotation=true`. Default: 180.0.          |
| `GetRotationSpeed`             | —                                                | Returns current rotation speed.                                                             |
| `SetPositionLimits`            | `float minX, float maxX, float minY, float maxY` | Hard-clamp pan bounds in world space. Default: ±1000 on both axes.                          |
| `SetPosition`                  | `const glm::vec3& position`                      | Directly set world position, bypassing keyboard input.                                      |
| `GetPosition`                  | —                                                | Returns `const glm::vec3&` — current camera world position.                                 |
| `SetManualMovementEnabled`     | `bool enabled`                                   | Enable/disable WASD panning. Disable for code-driven cameras. Default: true.                |
| `IsManualMovementEnabled`      | —                                                | Returns true if keyboard panning is active.                                                 |
| `SetKeyBindings`               | `const CameraKeyBindings& bindings`              | Replace the default WASD+QE key mapping.                                                    |
| `GetKeyBindings`               | —                                                | Returns a mutable reference to the active key bindings.                                     |

---

### Layer Timeline API

| Function                    | Description                                                            |
| --------------------------- | ---------------------------------------------------------------------- |
| `GetLocalTime()`            | Returns the accumulated scaled time for this layer (seconds).          |
| `SetLocalTime(float)`       | Directly set the time accumulator (e.g. for level reset).              |
| `GetTimeScale()`            | Returns this layer's local time scale multiplier.                      |
| `SetTimeScale(float)`       | Set the layer's local scale (0=paused, 0.5=half speed, -1=rewind).     |
| `UpdateLayerTime(float dt)` | Called by the engine each frame. Do not call manually in normal usage. |

---

### Application

| Function                                        | Description                                                      |
| ----------------------------------------------- | ---------------------------------------------------------------- |
| `Application::Get()`                            | Static singleton accessor.                                       |
| `GetWindow()`                                   | Returns `Window&`.                                               |
| `GetFrameBuffer()`                              | Returns `Ref<FrameBuffer>` — the main render target.             |
| `GetWorkspaceLayer()`                           | Returns `WorkspaceLayer*` (nullptr before a DLL transition).     |
| `PushLayer(Layer*)`                             | Add a layer to the stack.                                        |
| `PushOverlay(Layer*)`                           | Add an overlay (always above layers) to the stack.               |
| `TransitionFromLauncherToWorkspace(string dll)` | Queue a DLL load transition for the Safe Zone.                   |
| `TransitionToLauncher()`                        | Queue a return to the Launcher for the Safe Zone.                |
| `UseFixedTimeStep(bool)`                        | Enable/disable the 60Hz fixed update pass.                       |
| `SetTimeScale(float)`                           | Set the global time scale multiplier.                            |
| `GetTimeScale()`                                | Returns the current global time scale.                           |
| `GetAbsoluteTime()`                             | Returns raw engine uptime in seconds (unaffected by time scale). |
| `Close()`                                       | Signal the engine to exit the run loop cleanly.                  |

---

### FrameBuffer

| Function                       | Parameters                        | Description                                                            |
| ------------------------------ | --------------------------------- | ---------------------------------------------------------------------- |
| `FrameBuffer::Create`          | `const FramebufferSpecification&` | Factory — creates a platform-specific FBO instance.                    |
| `Bind`                         | —                                 | Redirect subsequent draw calls to this FBO.                            |
| `Unbind`                       | —                                 | Restore the default screen framebuffer.                                |
| `Resize`                       | `uint32_t width, uint32_t height` | Reallocate GPU textures at new dimensions.                             |
| `GetWidth`                     | —                                 | Returns current FBO width in pixels.                                   |
| `GetHeight`                    | —                                 | Returns current FBO height in pixels.                                  |
| `GetColorAttachmentRendererID` | —                                 | Returns the OpenGL texture ID for the color buffer (for ImGui::Image). |
| `GetSpecification`             | —                                 | Returns `const FramebufferSpecification&`.                             |

**`FramebufferSpecification` fields:**

| Field             | Type       | Default | Description                                        |
| ----------------- | ---------- | ------- | -------------------------------------------------- |
| `Width`           | `uint32_t` | 0       | Width in pixels.                                   |
| `Height`          | `uint32_t` | 0       | Height in pixels.                                  |
| `Samples`         | `uint32_t` | 1       | MSAA sample count (1 = no MSAA).                   |
| `SwapChainTarget` | `bool`     | false   | True if targeting the system back buffer directly. |

---

### Scene

| Function                              | Description                                                                                                  |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Scene::Create()`                     | Static factory — returns `Ref<Scene>`.                                                                       |
| `CreateEntity(string name)`           | Instantiate entity with `TransformComponent` + `TagComponent`. Default tag = `"GenericEntity"`.              |
| `DestroyEntity(Entity)`               | Remove entity and all its components from the registry.                                                      |
| `OnUpdate(float dt)`                  | Tick all sequential system `OnUpdate` passes + parallel prepare/execute/merge passes.                        |
| `OnFixedUpdate(float dt)`             | Tick all sequential system `OnFixedUpdate` passes + parallel fixed-step passes.                              |
| `OnRender(const OrthographicCamera&)` | Full render pass — `BeginScene`, draw all `SpriteRendererComponent` entities by material bucket, `EndScene`. |
| `AddSystem<T>(args...)`               | Allocate and attach a system. Returns `T&`. Automatically registers `ParallelSystem`s.                       |
| `GetSystem<T>()`                      | Returns `T*` if a system of that type is registered, `nullptr` otherwise.                                    |
| `RemoveAllSystems()`                  | Clear all registered systems.                                                                                |
| `View<Components...>()`               | Returns an EnTT view for iterating entities with all listed component types.                                 |
| `GetRegistry()`                       | Returns `entt::registry&` for direct registry access.                                                        |

---

### Window

| Function                          | Description                                                                                       |
| --------------------------------- | ------------------------------------------------------------------------------------------------- |
| `GetWidth()`                      | Current window client-area width in pixels.                                                       |
| `GetHeight()`                     | Current window client-area height in pixels.                                                      |
| `SetVSync(bool)`                  | Enable/disable vertical synchronization.                                                          |
| `GetHandle()`                     | Returns `GLFWwindow*` for API-specific calls.                                                     |
| `SetFullscreen(bool)`             | Toggle borderless-windowed fullscreen on the current monitor.                                     |
| `IsFullscreen()`                  | Returns the current fullscreen state.                                                             |
| `SetFullscreenHotkeyOverride(fn)` | Register a callback `(int key, int action, int mods) -> bool` to intercept key events before F11. |
| `ClearFullscreenHotkeyOverride()` | Remove the registered callback. Call from `OnDetach` before DLL unload.                           |

---

## 26. Telemetry System

Five subsystems work together to record, export, and replay per-entity float-channel data. The complete working reference is [`TemplateTelemetryLayer`](Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.cpp), which wires all five for 20 simulated agents.

| Subsystem | File | Role |
| --------- | ---- | ---- |
| `DataRecorder` | `telemetry/DataRecorder.h/.cpp` | Thread-safe multi-entity capture |
| `DataPlayer` | `telemetry/DataPlayer.h/.cpp` | Binary playback with seek and interpolation |
| `TelemetryPanel` | `telemetry/TelemetryPanel.h/.cpp` | ImGui/ImPlot UI; owns the replay lifecycle |
| `EntitySelection` | `telemetry/EntitySelection.h/.cpp` | Global "which entity is selected" service |
| `EntityPicker` | `telemetry/EntityPicker.h` | Left-click → world-space AABB hit test |

### Recording

**Step 1 — Register entities** (main-thread only, before any parallel jobs):

```cpp
// Returns a stable uint32_t ID. Store it on the component that will call Record.
uint32_t id = m_Recorder.Register(
    "Agent_00", "Agent",
    { "PosX", "PosY", "Speed", "Heading", "Power" }
);
```

**Step 2 — Pre-allocate buffers** (once, after all `Register` calls):

```cpp
// 5 minutes at 60 Hz — zero heap allocation in the hot path after this call.
m_Recorder.ReserveCapacity(static_cast<size_t>(60.0f * 300.0f));
```

**Step 3 — Record each tick** (thread-safe; call from `OnFixedParallelExecute` or the main thread):

```cpp
recorder->Record(agent.recordId, {
    agent.position.x, agent.position.y,
    vLen, agent.heading, agent.power
});
```

**Step 4 — Advance the recorder clock** (once per fixed tick while recording is active):

```cpp
void MyLayer::OnFixedUpdate(float dt)
{
    if (m_Recording)
        m_Recorder.Tick(dt);
}
```

**Step 5 — Stop and export** (non-blocking; background thread writes the files):

```cpp
m_Recording = false;
m_Recorder.Flush("logs", "my_session", 60.0f);
// Poll IsFlushing() each frame, or call WaitForFlush() before shutdown.
```

Output layout:
```
logs/my_session/
├── scene.bin      ← all entities, v3 binary
├── Agent_00.csv
├── Agent_01.csv
...
```

If `sessionName` is empty, Flush uses an ISO-8601 timestamp for the folder name.

> **Between recording sessions:** Call `m_Recorder.Clear()` to drop all frames without losing registrations or reserved capacity. Re-calling `Register` is not required.

### Replay

```cpp
Cosmic::DataPlayer m_Player;

if (m_Player.Load("logs/my_session")) // directory or single .bin path
{
    m_Player.SetSpeed(1.0f);   // negative = reverse
    m_Player.Play();
}

// Drive from OnUpdate
m_Player.Tick(ts);

// Query the interpolated state for any entity at the current playhead
Cosmic::TelemetryFrame frame;
if (m_Player.GetFrame("Agent_00", frame))
{
    float x = frame.values[0]; // PosX
    float y = frame.values[1]; // PosY
}

// Sample a historical position without moving the playhead
Cosmic::TelemetryFrame historical;
m_Player.SampleAt("Agent_00", 12.5f, historical);
```

### Making Entities Selectable

Add the empty `SelectableComponent` tag to any entity you want `EntityPicker` to test:

```cpp
entity.AddComponent<Cosmic::SelectableComponent>();
CS_REGISTER_COMPONENT(Workspace::MyComponent) // required for any component crossing the DLL boundary
```

No fields, no configuration. Adding the tag is the only requirement.

### Entity Picking on Left-Click

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);
    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
        [this](Cosmic::MouseButtonPressedEvent& ev) -> bool
        {
            if (ev.GetMouseButton() != CS_MOUSE_BUTTON_LEFT) return false;

            // Skip picking during replay — live entity handles are invalid.
            if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay)
                return false;

            glm::vec2 mousePos = Cosmic::Input::GetMousePosition();
            glm::vec2 vpSize   = {
                (float)Cosmic::Application::Get().GetWindow().GetWidth(),
                (float)Cosmic::Application::Get().GetWindow().GetHeight()
            };

            glm::vec2 worldPos = Cosmic::EntityPicker::ScreenToWorld(
                m_Camera.GetCamera(), mousePos, vpSize);

            Cosmic::Entity hit = Cosmic::EntityPicker::Pick(m_Scene, worldPos);
            if (hit)
            {
                const std::string& name = hit.GetComponent<Cosmic::TagComponent>().Tag;
                Cosmic::EntitySelection::Set(hit, name, "Agent");
                ev.Handled = true;
                return true;
            }
            return false;
        });
}
```

`Pick` iterates only entities with both `TransformComponent` and `SelectableComponent`. It tests the 2D AABB (`Position ± Scale/2`) and returns the first hit, or an invalid `Entity{}` if nothing was hit.

### EntitySelection — Subscribing to Selection Changes

```cpp
// Subscribe — returns a handle you must store.
m_SubHandle = Cosmic::EntitySelection::OnChanged(
    [this](const std::string& name, const std::string& tag)
    {
        m_SelectedName = name;
        RebuildCharts(name, tag);
    });

// Always unsubscribe in the destructor to prevent dangling captures.
MySystem::~MySystem()
{
    Cosmic::EntitySelection::Unsubscribe(m_SubHandle);
}
```

Read the current selection from any thread:

```cpp
std::string name = Cosmic::EntitySelection::GetName();
std::string tag  = Cosmic::EntitySelection::GetTag();
bool        has  = Cosmic::EntitySelection::HasSelection();

// Live handle — invalid during replay (SetByName was called, not Set).
Cosmic::Entity e = Cosmic::EntitySelection::GetEntity();
if (e) { /* valid live handle */ }
```

### Wiring TelemetryPanel

Set up in `OnAttach`. Both data sources are attached regardless of the starting mode:

```cpp
// Panel starts in Live mode automatically when a non-null recorder is attached.
m_Panel.SetRecorder(&m_Recorder);
m_Panel.SetPlayer(&m_Player);

// Register a custom inspector for entities whose tag matches "Agent".
// Priority: entity name inspector > tag inspector > auto raw-value fallback.
m_Panel.RegisterTagInspector("Agent",
    [](const std::string& name, const Cosmic::TelemetryFrame& f)
    {
        if (f.values.size() >= 5)
        {
            ImGui::Text("Position : (%.2f, %.2f)", f.values[0], f.values[1]);
            ImGui::Text("Speed    : %.3f u/s",     f.values[2]);
            ImGui::Text("Heading  : %.2f rad",     f.values[3]);
            ImGui::Text("Power    : %.3f",         f.values[4]);
        }
    });
```

Drive from layer hooks:

```cpp
void MyLayer::OnUpdate(float ts)
{
    // Advances player clock (replay) and pushes the current frame into ring buffers.
    m_Panel.OnUpdate(ts);
}

void MyLayer::OnImGuiRender()
{
    // Transport controls are decoupled from the chart panel — embed them anywhere.
    ImGui::Begin("Project Inspector Top");
    m_Panel.DrawTransportControls(); // no-op when no recording is loaded
    ImGui::End();

    // Replay loader, entity selector, ImPlot charts, inspector.
    ImGui::Begin("Telemetry");
    m_Panel.OnImGuiRender();
    ImGui::End();
}
```

### Mode Transitions

| Mode | Active when | Data source |
| ---- | ----------- | ----------- |
| `Mode::None` | No sources attached | — |
| `Mode::Live` | `SetRecorder()` called, or `SetMode(Mode::Live)` explicit | `DataRecorder` |
| `Mode::Replay` | User clicks Load in the panel and file loads successfully | `DataPlayer` |

Gate simulation on mode to prevent the physics pass from running during replay:

```cpp
void MyLayer::OnFixedUpdate(float dt)
{
    const float localDt = dt * GetTimeScale();
    if (localDt <= 0.0f) return;

    // Simulation only runs in Live mode — player drives positions in Replay mode.
    if (m_Panel.GetMode() != Cosmic::TelemetryPanel::Mode::Replay)
        m_Scene->OnFixedUpdate(localDt);

    if (m_Recording)
        m_Recorder.Tick(localDt);
}
```

Override `TransformComponent` positions from player data during replay so entities move with the playhead:

```cpp
// In OnUpdate, after m_Panel.OnUpdate(ts):
if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay && m_Player.IsLoaded())
{
    auto view = m_Scene->View<Cosmic::TagComponent, Cosmic::TransformComponent>();
    for (auto rawE : view)
    {
        const std::string& tag = view.get<Cosmic::TagComponent>(rawE).Tag;
        Cosmic::TelemetryFrame frame;
        if (m_Player.GetFrame(tag, frame) && frame.values.size() >= 2)
        {
            auto& t = view.get<Cosmic::TransformComponent>(rawE);
            t.Position.x = frame.values[0];
            t.Position.y = frame.values[1];
        }
    }
}
```

### DataRecorder API Summary

| Function | Thread | Description |
| -------- | ------ | ----------- |
| `Register(name, tag, channels)` | Main only | Register entity; returns stable `uint32_t` ID. Must complete before any `Record` calls. |
| `ReserveCapacity(frames)` | Main only | Pre-allocate columnar storage. Eliminates all hot-path allocations. |
| `Record(id, {values...})` | Any | Per-entity lock held <1 µs. Zero-alloc after `ReserveCapacity`. |
| `Tick(dt)` | Main | Advance elapsed-time counter. Call once per fixed tick while recording. |
| `GetCurrentFrame(name, out)` | Any | Latest recorded frame for display (copy, safe after lock). |
| `GetRecordedDuration()` | Any | Total simulated time recorded, in seconds. |
| `Flush(folder, session, rate)` | Main | Non-blocking snapshot + background write to `scene.bin` + CSVs. |
| `WaitForFlush()` | Main | Block until background write thread has finished. |
| `IsFlushing()` | Any | True while background thread is active. |
| `Clear()` | Main | Drop all frames; keep registrations and reserved capacity. Resets elapsed time to zero. |

### DataPlayer API Summary

| Function | Description |
| -------- | ----------- |
| `Load(folderOrPath)` | Load directory (prefers `scene.bin`) or single `.bin`. Returns `true` on success. |
| `Unload()` | Clear all data and reset playback state. |
| `Play()` / `Pause()` | Start/stop advancing the playhead on `Tick`. |
| `SetSpeed(float)` | Playback multiplier. Negative values play in reverse. |
| `SetPosition(seconds)` | Seek to an arbitrary timestamp. |
| `GetPosition()` / `GetDuration()` | Current playhead position and total recording duration in seconds. |
| `Tick(dt)` | Advance by `dt × speed`. Auto-stops at both endpoints. No-op when paused or not loaded. |
| `GetFrame(name, out)` | Linearly interpolated frame at the current playhead position. |
| `SampleAt(name, seconds, out)` | Interpolated frame at an arbitrary position without moving the playhead. |
| `IsLoaded()` / `IsPlaying()` | State queries. |
| `GetSampleRate()` | Sample rate from the file header (Hz). Returns 60 if not loaded. |

---

# Cosmic Engine — Part 2: Engine Internals

> **Audience:** Engine contributors and advanced client developers who need to understand how Cosmic works under the hood. Assumes familiarity with [Part 1 — Client Developer Guide](Part1_ClientGuide.md).

---

## §26 Source File Map

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

## §27 Hot-Reloadable DLL Architecture

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

> **`CreatePluginLayer()` null guard:** If the plugin's `CreatePluginLayer` export returns `nullptr` (e.g. internal allocation failure), the engine logs an error, frees the library, and aborts the load. `SetViewportLayer` is never called with a null pointer.

> **Launcher discovery:** The launcher's project scanner only lists DLLs that actually export `CreatePluginLayer`. Each DLL in the executable directory is probed with `LoadLibraryExA(..., DONT_RESOLVE_DLL_REFERENCES)` + `GetProcAddress("CreatePluginLayer")` before appearing in the project list. Engine DLLs (`Cosmic.dll`), renderer backends, and third-party libraries are silently excluded without requiring a hardcoded name list.

### WorkspaceLayer ownership note

`m_WorkspaceLayer` is tracked by two places simultaneously: as a typed raw pointer on `Application` (for direct access) and as a `Layer*` inside the `LayerStack` (for iteration). This is intentional — the `LayerStack` is a non-owning borrow container. `Application` holds the sole ownership and is responsible for both `PopLayer` and `delete` in the correct order. These two operations are always paired in the codebase; separating them would cause either a leak (`PopLayer` without `delete`) or a dangling iterator (`delete` without `PopLayer`). The long-term fix would mirror how `m_ImGuiLayer` is managed — a `Scope<WorkspaceLayer>` whose raw pointer is lent to the stack — but that requires restructuring the shutdown sequence and is deferred.

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

## §28 Top-Down Time Propagation Waterfall

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
m_AbsoluteTime += rawTimestep;                // always raw — monotonically increasing
```

`scaledTs` is what flows down to layers. `m_AbsoluteTime` accumulates raw wall-clock time (unaffected by `TimeScale`), so it never pauses or rewinds.

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

**Layers on the Application LayerStack** (engine-internal, non-plugin):

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
  → fixedDt * sign(TimeScale)  →  Layer::OnFixedUpdate(signedDt)
```

`fixedDt` is `1/60` in magnitude, but its sign matches `TimeScale`: positive during normal play, negative during rewind. A client layer can read the sign of `dt` in `OnFixedUpdate` to know whether to simulate forward or backward — no need to query `GetTimeScale()` separately.

**Plugin layers loaded via WorkspaceLayer** (DLL client layers):

The WorkspaceLayer applies an additional per-layer time scale multiplication before forwarding to the plugin. Both `OnUpdate` and `OnFixedUpdate` respect the plugin layer's own `SetTimeScale`:

Variable-rate:
```
WorkspaceLayer::OnUpdate(scaledTs)
  → pluginLayer->UpdateLayerTime(scaledTs)         // local time accumulates as normal
      m_LocalTime += scaledTs * m_LocalTimeScale
  → pluginLayer->OnUpdate(scaledTs * pluginLayer->GetTimeScale())
```

Fixed-rate:
```
WorkspaceLayer::OnFixedUpdate(fixedDt)
  → pluginLayer->OnFixedUpdate(fixedDt * pluginLayer->GetTimeScale())
```

This means a plugin layer that calls `SetTimeScale(0.5f)` will receive half-speed deltas in both its `OnUpdate` and `OnFixedUpdate` — in addition to any global time scale already applied. Layers on the engine's own LayerStack do not get this treatment; their `OnUpdate`'s `ts` argument is global-scaled only, and `m_LocalTimeScale` only affects `GetLocalTime()`.

### Full Waterfall Diagram

```
Wall clock (glfwGetTime)
   │
   ▼
rawTimestep
   │  × m_TimeScale  (Application global)
   ▼
scaledTs
   │
   ├──► LayerStack layers: OnUpdate(scaledTs)
   │       UpdateLayerTime(scaledTs)
   │           m_LocalTime += scaledTs × m_LocalTimeScale
   │
   └──► Plugin layer (via WorkspaceLayer):
           UpdateLayerTime(scaledTs)
               m_LocalTime += scaledTs × m_LocalTimeScale
           OnUpdate(scaledTs × pluginLayer.GetTimeScale())

fixedDt (constant 1/60 × sign(TimeScale), fire when ready)
   │
   ├──► LayerStack layers: OnFixedUpdate(fixedDt)
   │
   └──► Plugin layer: OnFixedUpdate(fixedDt × pluginLayer.GetTimeScale())
```

---

## §29 The Double-Tick Trap

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

## §30 The OpenGL Graphics Pipeline

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

### OpenGL Context Initialization and GLAD

`OpenGLContext::Init()` calls `gladLoadGLLoader` to load all OpenGL function pointers from the driver. If GLAD fails (the driver does not support the requested OpenGL version, or returns null proc addresses), `Init()` fires a `CS_CORE_ASSERT` and terminates immediately. Every GL call made through GLAD function pointers (`glDrawElements`, `glGenVertexArrays`, etc.) is a null-pointer dereference if GLAD did not load successfully — there is no recoverable fallback. This means the engine requires a driver and GPU that support at least OpenGL 4.5.

### `DrawLines` VAO Binding

`OpenGLRendererAPI::DrawLines` binds the provided `vertexArray` internally before issuing `glDrawArrays`. This makes the VAO parameter active, not merely advisory — the bound array at draw time is always the one passed to the function, regardless of what was previously current on the GPU.

Note: `DrawIndexed` and `DrawIndexedInstanced` do **not** bind the vertex array internally; callers are responsible for binding before those calls. `DrawLines` is the exception because its parameter was previously unused (a latent bug), and the fix adds the bind for robustness.

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

## §31 Hardware Abstraction Architecture

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

| `ShaderDataType` | GL type    | Component count |
| ---------------- | ---------- | --------------- |
| `Float`          | `GL_FLOAT` | 1               |
| `Float2`         | `GL_FLOAT` | 2               |
| `Float3`         | `GL_FLOAT` | 3               |
| `Float4`         | `GL_FLOAT` | 4               |
| `Mat3`           | `GL_FLOAT` | 3×3 = 9         |
| `Mat4`           | `GL_FLOAT` | 4×4 = 16        |
| `Int`            | `GL_INT`   | 1               |
| `Bool`           | `GL_BOOL`  | 1               |

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

## §32 Batch Rendering Deep Dive

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

| Field          | Type  | Bytes |
| -------------- | ----- | ----- |
| Position       | vec3  | 12    |
| Scale          | vec2  | 8     |
| Color          | vec4  | 16    |
| TexCoordOffset | vec2  | 8     |
| TexCoordScale  | vec2  | 8     |
| TexIndex       | float | 4     |
| TilingFactor   | float | 4     |

`InstanceCircleData` layout:

| Field     | Type  | Bytes |
| --------- | ----- | ----- |
| Position  | vec3  | 12    |
| Scale     | vec2  | 8     |
| Color     | vec4  | 16    |
| Thickness | float | 4     |
| Fade      | float | 4     |

---

## §33 Shader Preprocessing System

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

| Uniform            | Type    | Injected value source           |
| ------------------ | ------- | ------------------------------- |
| `u_ViewProjection` | `mat4`  | `RenderPass` camera matrix      |
| `u_Time`           | `float` | Active layer's `GetLocalTime()` |
| `u_ViewportSize`   | `vec2`  | Framebuffer width/height        |

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

## §34 RenderPass Stack — Implementation Details

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

## §35 Parallel Pipeline Architecture

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

> **Trivial-copyability requirement:** `DoubleBuffer<T>` uses `std::memcpy` internally (`CopyReadToWrite`). `T` must be trivially copyable — plain structs of scalars, `glm::vec*`, or similar POD types. Types that contain `std::string`, `std::shared_ptr`, `Ref<>`, or any other non-trivially-copyable member will compile but produce double-frees or corrupted reference counts at runtime. A `static_assert` fires at instantiation time if `T` is not trivially copyable. Use `ReadWriteQuery<T>` for component types with non-trivial copy semantics.

### `ComponentArray<T>` vs `FlatComponentArray<T>`

EnTT stores component data in paged arrays (default page size 1024). `ComponentArray<T>` gets a non-owning pointer to the **first page only**:

```cpp
auto arr = ComponentArray<PhysicsBody>::From(registry);
// arr.Data()  — pointer into registry's first storage page
// arr.Count() — count of entities on that page (≤ 1024 for small counts)
```

Safe for entity counts that fit within a single EnTT storage page (≤ ~1024 by default). Cheaper than `FlatComponentArray` (zero allocation, zero copy).

> **Single-page limit:** `ComponentArray<T>::Data()` points only to EnTT's first storage page. `Count()` reflects the total component count across all pages, so accessing indices past the first page is undefined behaviour. A debug assert (`CS_CORE_ASSERT`) fires at `From()` time if the pool spans more than one page. Use `FlatComponentArray<T>` whenever component count may exceed one page.

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

## §36 Build System

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

### Build Scripts

There are five batch files in the SDK root and inside each project. Here is what each one does and when to use it.

---

#### `setup.bat` — run once per machine

Permanently registers the `COSMIC_SDK` environment variable pointing at the SDK root using `setx`. This is required before any project `build.bat` can locate the engine headers and import library. Run it once after cloning. Restart any open terminals afterward for the variable to take effect.

---

#### `build_all.bat` — clean full rebuild

```bat
build_all.bat [Debug|Release]     :: defaults to Debug
```

Deletes the entire `build/` directory, re-runs CMake configure from scratch, then builds the engine and every project under `Projects/`. Use this when:
- You have just cloned the repo for the first time
- A `CMakeLists.txt` has changed in a way that left the cache stale
- You want a guaranteed clean state (CI, release packaging)

Because it deletes `build/`, **all incremental state is lost** — the next compile is a full rebuild of everything. Do not use this for day-to-day iteration.

---

#### `build.bat` — incremental full build

```bat
build.bat [Debug|Release]     :: defaults to Debug
```

The script you will use most often. Calls `cmake --build` directly without re-running configure. CMake automatically re-runs configure if any `CMakeLists.txt` has changed. Use this for iterating on engine source or any project when you want to rebuild everything in one step.

On first run (no `build/` directory yet), it detects the missing cache and runs a full configure automatically before building — so it is safe to run even on a fresh clone.

It also detects if the cache was left in engine-only mode by a prior `build_engine.bat` run (`COSMIC_BUILD_ENGINE_ONLY=ON`) and re-configures with `OFF` before building, so switching between the two scripts never produces a silently wrong build.

---

#### `build_engine.bat` — engine-only incremental build

```bat
build_engine.bat [Debug|Release]     :: defaults to Debug
```

Runs `cmake --build` targeting only the `Cosmic` and `CosmicApp` targets. Project DLLs under `Projects/` are skipped entirely. Use this when you are making changes to the engine itself and want the fastest possible turnaround.

Like `build.bat`, it handles the first-run case automatically. It also detects if the cache was left in full-build mode by a prior `build.bat` run (`COSMIC_BUILD_ENGINE_ONLY=OFF`) and re-configures with `ON` before building.

---

#### `Projects/<name>/build.bat` — single project build

```bat
build.bat [Debug|Release]     :: defaults to Debug
```

Builds one project DLL in isolation without touching the engine. It has its own `build/` subdirectory and its own CMake cache, separate from the root build tree. Configure only runs on first use (when `CMakeCache.txt` does not yet exist) — subsequent runs go straight to `cmake --build`. Use this when:
- You are iterating on a single project and do not need to rebuild anything else
- Your project lives outside the SDK repo (standalone workflow)

The script reads `COSMIC_SDK` to locate the engine headers and import library. If the variable is not set, it falls back to the hardcoded path written by the Launcher at project generation time.

---

#### When to use which script

| Situation | Script |
| --- | --- |
| First clone / clean slate needed | `build_all.bat` |
| Changing engine source, rebuild everything | `build.bat` |
| Changing engine source only | `build_engine.bat` |
| Changing one project | `Projects/<name>/build.bat` |
| Building Release for distribution | Any script with `Release` argument |

### Debug vs Release

All scripts default to `Debug`. To build Release, either double-click `build_all_release.bat` or pass `Release` as the first argument to any script from a terminal:

```bat
build.bat Release
build_all.bat Release
build_engine.bat Release
```

`build_all_release.bat` is the recommended path for distribution — it deletes the build directory and rebuilds everything from scratch, so there is no risk of stale incremental state or a mismatched cache flag making it into your release output.

#### What actually changes between the two

**Debug** — what you use during development:
- Full debug symbols (`.pdb` files) — the debugger can show you exact line numbers, variable values, and call stacks
- No optimizations — code runs slower but executes predictably; the compiler does not reorder, inline, or eliminate anything, so stepping through code in a debugger behaves exactly as written
- `assert()` and `CS_CORE_ASSERT` are active — contract violations crash immediately with a message rather than silently corrupting state

**Release** — what you use for distribution or performance measurement:
- Full compiler optimizations (`/O2` on MSVC) — the compiler can inline functions, reorder instructions, eliminate dead code, and vectorize loops. Typically 2–5× faster than Debug for compute-heavy code
- No debug symbols by default — stack traces in a crash are less readable
- `assert()` and `CS_CORE_ASSERT` are compiled out — a failing assert silently does nothing

#### The important rule: never benchmark in Debug

If you are measuring frame time, job throughput, or serial throughput, always run Release. Debug builds can be 5–10× slower than Release for hot loops due to the lack of optimization and the overhead of iterator debug checks in the MSVC standard library. A result that looks slow in Debug may be perfectly fine in Release.

#### Outputs land in separate directories

Both configurations build into the same `build/Runtime/` tree but in separate subdirectories:

```
build/Runtime/Debug/      ← Debug DLLs and exe
build/Runtime/Release/    ← Release DLLs and exe
```

They do not overwrite each other, so you can keep both around and switch by launching the exe from the appropriate folder.

### Precompiled Headers

The engine target uses a precompiled header (`Cosmic/src/CosmicPCH.h`) to avoid re-parsing heavy third-party headers on every translation unit. It is registered in `Cosmic/CMakeLists.txt`:

```cmake
target_precompile_headers(Cosmic PRIVATE src/CosmicPCH.h)
```

CMake injects the PCH into every engine `.cpp` automatically via the compiler's force-include flag (`/FI` on MSVC). **You do not add `#include "CosmicPCH.h"` to any source file** — the injection happens at the compiler command line level.

#### What belongs in the PCH

The PCH only contains headers that are **stable** — headers you will never edit during development. If any header inside the PCH changes, the compiler invalidates the cached parse result and rebuilds it, then recompiles every translation unit in the target. One bad choice in the PCH turns any small change into a full engine rebuild.

| Belongs in PCH | Stays as explicit includes |
| --- | --- |
| Standard library (`<string>`, `<vector>`, `<windows.h>`, etc.) | Engine headers (`core/Log.h`, `renderer/Renderer2D.h`, etc.) |
| Third-party libraries you do not modify (glm, spdlog) | Any header that changes during active development |
| Platform headers (`<windows.h>`) | Headers with ordering constraints (e.g. `<glad/glad.h>`) |

#### glad ordering note

`<glad/glad.h>` is intentionally excluded from the PCH. Glad must be included before any code that would pull in `<GL/gl.h>` from the system SDK. The PCH force-includes `<windows.h>` first, but `<windows.h>` with `WIN32_LEAN_AND_MEAN` does not include `<GL/gl.h>`, so there is no conflict. Files that use glad (`OpenGLBuffer.cpp`, `OpenGLVertexArray.cpp`, etc.) continue to list `#include <glad/glad.h>` as their first explicit include and this remains correct.

#### Build scripts

All three build scripts accept an optional configuration argument (default: `Debug`):

```bat
build_all.bat Release           :: clean reconfigure + Release build
build.bat Release               :: incremental Release build
build_engine.bat                :: engine-only Debug build
```

`build.bat` does not invoke `cmake ..` — it calls `cmake --build` directly. CMake re-runs configure automatically when any `CMakeLists.txt` changes, so the explicit configure step is unnecessary for day-to-day iteration and only costs time.

---

## 37 Event System — Implementation Details

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
            if (func(static_cast<T&>(m_Event)))
                m_Event.Handled = true;
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
- `Handled` is only ever set to `true` — never cleared. A handler that returns `false` leaves `Handled` unchanged, so a prior handler that already set it `true` is not overridden.
- `Dispatch` returns `true` if the type matched (regardless of `func`'s return value), `false` if the type did not match. This return value is rarely used but allows the caller to distinguish "wrong type" from "handled/not handled."

### Event Propagation — `LayerStack` Order

Events enter `Application::OnEvent()` and are dispatched to the `LayerStack` in **reverse order** (overlays first, base layers last):

```cpp
void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });
    dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) { return OnWindowResize(e); });

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

`BIT(x)` expands to `(1u << (x))` (unsigned shift, avoids UB for bits ≥ 31). A single event can belong to multiple categories — `KeyPressedEvent` sets flags for `Input | Keyboard`. `IsInCategory` tests with bitwise AND.

`MouseButtonPressedEvent` and `MouseButtonReleasedEvent` carry all three flags: `EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton`. This means `IsInCategory(EventCategoryMouseButton)` correctly returns `true` for button clicks and `false` for `MouseMovedEvent` / `MouseScrolledEvent`, which only carry `EventCategoryMouse | EventCategoryInput`.

> **Rebuild note — `IsInCategory` is `const`:** `IsInCategory` reads no state and is correctly declared `const`, allowing it to be called on `const Event&` references. Because `Event` is `COSMIC_API`-exported, MSVC encodes `const`-ness into the mangled DLL symbol name (`QEAA` non-const → `QEBA` const). If you see an "entry point not found" error for `IsInCategory` when loading a client DLL, it means the client was compiled against an older header before this change. **The fix is a clean rebuild of both `Cosmic.dll` and the client DLL together** — the mismatch only occurs when the two sides were compiled against different versions of `Event.h`.

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

## 38 Telemetry System — Implementation Details

### Source Files

```
Cosmic/src/telemetry/
├── TelemetryChannel.h      Shared POD — TelemetryFrame, EntityTelemetryInfo
├── EntitySelection.h/.cpp  Global selection service, subscription callbacks
├── EntityPicker.h          Header-only AABB picker + screen-to-world math
├── DataRecorder.h/.cpp     Columnar capture engine, binary v3 writer
├── DataPlayer.h/.cpp       Binary reader (v1/v2/v3), linear interpolation
└── TelemetryPanel.h/.cpp   Mode state machine, ring buffer, ImPlot UI

Cosmic/src/scene/
└── SelectableComponent.h   Empty EnTT tag — marks entities as pickable

Cosmic/templates/ExampleProject/src/
├── AgentSystem.h           ParallelSystem + DataRecorder integration example
└── TemplateTelemetryLayer.h/.cpp   End-to-end wiring of all five subsystems
```

The telemetry subsystem has no dependency on any renderer or GPU state — only `scene/`, `core/`, ImGui, and ImPlot. All telemetry headers can be included from both engine-side and DLL-side code without pulling in OpenGL.

### DataRecorder — Columnar Storage

Earlier iterations stored data as `vector<TelemetryFrame>`, where each frame contained a `vector<float>`. Every `Record()` call allocated memory to grow that inner vector. At 60 Hz across 20 entities this was 1,200 allocations per second.

The current design flips the layout from **row-major** (one object per frame) to **columnar** (one contiguous array per channel):

```cpp
struct EntityRecord
{
    std::vector<float>              timestamps;  // [frame_index]
    std::vector<std::vector<float>> columns;     // [channel_index][frame_index]
    mutable std::mutex              mutex;       // per-entity; held <1 µs in Record()
};
```

After `ReserveCapacity(N)` each inner vector is pre-allocated for N floats. Subsequent `push_back` calls never trigger a reallocation — `Record()` performs **zero heap allocations** in the hot path. Multiple threads can record to different entities simultaneously because each entity owns an independent mutex; there is no global contention point.

`m_Records` is a `vector<unique_ptr<EntityRecord>>` that is populated exclusively on the main thread during `Register()` and never resized afterward. This means `m_Records[id]` is safe to dereference lock-free from any worker thread.

### DataRecorder — Thread Safety Model

| Variable | Writer | Readers | Synchronization |
| -------- | ------ | ------- | --------------- |
| `columns`, `timestamps` | Worker via `Record()` | Main via `GetCurrentFrame()`, `Flush()` | Per-entity `mutex` |
| `m_Records` (vector of ptrs) | Main via `Register()` | All threads via `Record()` | None — never resized after registration is complete |
| `m_ElapsedTime` | Main via `Tick()` | Worker via `RecordImpl()` | `std::atomic<float>`, relaxed ordering |
| `m_Flushing` | Flush thread | Main via `IsFlushing()` | `std::atomic<bool>` |

Relaxed ordering on `m_ElapsedTime` is sufficient on x86-64 because the TSO memory model makes stores from one core visible to other cores in program order regardless of the C++ memory order tag. The practical consequence is that a worker thread may see a timestamp that is one tick stale — acceptable for telemetry data.

### DataRecorder — Binary v3 Format

```
Offset   Size    Field
[0]      4       char    magic[4]       = "CSMC"
[4]      4       uint32  version        = 3
[8]      4       uint32  entity_count
[12]     4       float   sample_rate

── descriptor table, entity_count entries ──
For each entity:
  [+0]    64     char    entity_name[64]
  [+64]   64     char    entity_tag[64]
  [+128]  4      uint32  channel_count
  [+132]  4      uint32  sample_count     ← per-entity (moved from global header in v2)
  [+136]  channel_count × 32  char channel_name[32]

── data table, entity_count contiguous blocks ──
For each entity:
  sample_count × channel_count × sizeof(float)
  float32, row-major (all channels for frame 0, then all channels for frame 1, …)
```

The key change from v2 to v3 is the promotion of `sample_count` from a single global field in the file header to a per-entity field in the descriptor. This allows entities registered at different times — or that recorded for different durations — to have different frame counts without corrupting the data offsets for subsequent entities.

### DataPlayer — Format Compatibility

`DataPlayer` reads all three versions transparently:

| Version | Identifies via | `sample_count` location | Produced by |
| ------- | -------------- | ----------------------- | ----------- |
| v1 | Single entity per `.bin` (no `"CSMC"` magic) | Derived from file size | Legacy single-entity recorder |
| v2 | `magic == "CSMC"`, `version == 2` | Single global field in header | Older `scene.bin` sessions |
| v3 | `magic == "CSMC"`, `version == 3` | Per-entity in descriptor table | Current `DataRecorder::Flush()` |

`Load(directory)` prefers `scene.bin` (v2/v3) and skips any individual `.bin` files in the same folder. This deduplication prevents double-loading entities when a session directory contains both a `scene.bin` and leftover per-entity files from an older format.

### DataPlayer — Linear Interpolation

Given playback position `P` seconds and sample rate `R` Hz:

```
t    = P × R
i    = clamp(floor(t), 0, sample_count − 2)   // lower frame index
frac = t − float(i)                            // interpolation weight in [0, 1)

out.values[ch] = frames[i].values[ch] × (1 − frac)
               + frames[i+1].values[ch] × frac
```

`SampleAt(name, seconds, out)` runs this identical calculation at an arbitrary position without modifying `m_Position`. The template layer uses it for **trail reconstruction**: when the user scrubs the playhead, the trail is rebuilt by sampling the entity's position at evenly-spaced past timestamps rather than replaying all intermediate frames in order.

### TelemetryPanel — Mode State Machine

```
         SetRecorder(non-null)
                 │
         ┌───────▼──────┐
         │  Mode::Live   │ ◄─── SetMode(Live) — called by layer when a new
         └───────┬───────┘       recording session starts while replay was loaded
                 │
     User clicks Load → file succeeds
                 │
         ┌───────▼───────┐
         │  Mode::Replay  │
         └───────────────┘
```

`OnUpdate` branches on `m_Mode` so that:
- In `Live` mode: `DataRecorder::GetCurrentFrame()` is polled and pushed into the ring buffer.
- In `Replay` mode: `DataPlayer::Tick(dt)` advances the playhead; the ring buffer is updated only if the position moved by more than 1 µs — keeping plots frozen while paused and updating correctly during scrub.

Frozen recorder data never bleeds into replay plots because the mode gate is exclusive.

### TelemetryPanel — Ring Buffer

Up to 512 samples per channel are retained for ImPlot. The buffer is described by two indices rather than a single write pointer:

```
m_PlotOffset  — index of the oldest valid sample (read head)
m_PlotCount   — number of valid samples currently in the ring
write index   = (m_PlotOffset + m_PlotCount) % k_PlotCapacity
```

**Write (push one frame):**
```
if m_PlotCount == k_PlotCapacity:
    m_PlotOffset = (m_PlotOffset + 1) % k_PlotCapacity  // evict oldest slot
else:
    m_PlotCount++
m_PlotBuffers[ch][writeIndex] = value
m_PlotTimes[writeIndex]       = timestamp
```

**Read (for ImPlot):** samples are not contiguous in memory. ImPlot is given two spans described by `(m_PlotOffset, k_PlotCapacity)` then `(0, writeIndex)`, covering all `m_PlotCount` valid samples from oldest to newest.

Y-axis limits are computed over only the `m_PlotCount` valid slots — not all 512. This prevents zero-initialized slots from collapsing the Y scale during the first few seconds after a buffer rebuild.

### EntitySelection — Subscription Model and Notify Safety

```cpp
SubscriptionHandle OnChanged(Callback cb);  // returns opaque handle
void Unsubscribe(SubscriptionHandle id);     // call from subscriber's destructor
```

`TelemetryPanel` stores its handle as a member (`m_SubHandle`) and calls `Unsubscribe(m_SubHandle)` in its destructor. If this is omitted, the captured `this` pointer inside the lambda becomes dangling after the panel is destroyed; the next selection change fires the dead callback and crashes.

`Notify` snapshots the subscriber list before firing to prevent re-entrant deadlock:

```cpp
// Simplified from EntitySelection.cpp
static void Notify(const std::string& name, const std::string& tag)
{
    std::vector<Subscription> snapshot;
    {
        std::lock_guard lock(s_Mutex);
        snapshot = s_Callbacks;      // copy under lock
    }
    for (auto& sub : snapshot)       // fire outside lock
        sub.cb(name, tag);
}
```

Without the snapshot, if a callback called `EntitySelection::Set()` or `Unsubscribe()`, that call would attempt to acquire `s_Mutex` on the same thread that already holds it — a deadlock. The snapshot releases the lock before any callback runs, making re-entrant calls from inside callbacks safe.

### EntityPicker — Screen-to-World Math

GLFW reports mouse coordinates with (0,0) at the **top-left** of the window. OpenGL NDC places (0,0) at the **bottom-left** of the clip volume. The Y-axis must be flipped during unprojection:

```
// 1. Normalize to [0, 1]
normX = screenPos.x / viewportSize.x
normY = screenPos.y / viewportSize.y

// 2. Map to NDC in [−1, +1]; flip Y
ndcX =  normX * 2.0 − 1.0
ndcY =  1.0 − normY * 2.0      ← flip here

// 3. Inverse view-projection
world = inverse(VP) × vec4(ndcX, ndcY, 0, 1)

// 4. Perspective divide (w == 1 for orthographic; kept for correctness)
world.xy /= world.w
```

Once in world space, `Pick` tests each entity with `TransformComponent + SelectableComponent`:

```
hitX = |worldPos.x − entity.Position.x| ≤ entity.Scale.x × 0.5
hitY = |worldPos.y − entity.Position.y| ≤ entity.Scale.y × 0.5
```

`EntityPicker` is header-only and carries no `COSMIC_API` export — the class is never instantiated, only called through its two static members.

### AgentSystem — Component Layout and Parallel Integration

`AgentComponent` is exactly 48 bytes — one cache line on common x86-64 hardware:

```
Offset  Bytes  Field
0       8      glm::vec2 velocity   worker-owned XY velocity
8       8      glm::vec2 target     current steering target in world space
16      8      glm::vec2 position   worker-owned; merged to TransformComponent in OnFixedMerge
24      4      float     speed      max movement speed (units/s)
28      4      float     power      sinusoidal cosmetic channel
32      4      float     heading    atan2(vy, vx) in radians
36      4      uint32_t  recordId   DataRecorder registration ID
40      8      float[2]  _pad       explicit alignment pad → 48 bytes total
```

Storing `recordId` on `AgentComponent` means each worker thread reads and records data from the same 48-byte cache line — no second cache miss to retrieve the ID.

`AgentSystem` uses `ReadWriteQuery<AgentComponent>` with `ForEachAsync` (the async variant that does not call `WaitIdle`). The scene issues a single `WaitIdle` barrier after all systems have submitted their parallel work, allowing `AgentSystem` jobs to overlap with any other `ParallelSystem` registered in the same scene.

`OnFixedMerge` then walks the staged results with `ForEachWithEntity` and copies `agent.position` back to `TransformComponent`. This two-buffer discipline — `AgentComponent.position` written in parallel, `TransformComponent.Position` written on the main thread in merge — is what makes the renderer safe: the renderer reads `TransformComponent` in `OnUpdate`, which follows the fixed-update pass where `OnFixedMerge` has already completed and the engine has committed the query back to the registry.

---
