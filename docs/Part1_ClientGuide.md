# Cosmic Engine — Part I: Client Developer Guide

> **How to use this document:** This is the complete client-facing reference for building projects with Cosmic. It covers every API you'll interact with, from the minimal project skeleton to the parallel job system. All code is verified against the current source.

---

## Table of Contents

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

| Script | When to use |
|---|---|
| `build_all.bat` | Full CMake reconfigure + compile of the engine and all projects. Use after cloning or adding a new project. |
| `build_all_quick.bat` | Incremental build — skips CMake reconfigure. Use for iterating on your project code when you haven't changed the project structure. |
| `build_engine.bat` | Builds the engine host only, skipping all project DLLs. Useful when validating engine changes in isolation. |

Outputs land in `build/Runtime/Debug/`. The engine executable and all project DLLs are placed here so the launcher can discover them.

### Creating a New Project from the Launcher

The easiest way to start a new project is from inside the engine itself. Launch the engine executable, and the Launcher screen gives you a **New Project** button. Fill in your project name and the target directory (a git repo of your choosing, for example), and the launcher will:

1. Copy the full `ExampleProject` template into your chosen directory.
2. Rename all files and class names to match your project name.
3. Generate a correct `CMakeLists.txt` wired to your local SDK path.
4. Generate a `build.bat` inside the project directory.

After generation, run `build_all.bat` once from the SDK root to register and compile the new project. Subsequent iterations can use `build_all_quick.bat` or the project's own `build.bat`.

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
├── build_all_quick.bat         ← Incremental build (no CMake reconfigure)
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

| Window name | Position | Best used for |
|---|---|---|
| `"Project Inspector Top"` | Top section of the left sidebar | Mode selector, global controls (time scale, primary state) |
| `"Project Inspector"` | Middle section | Per-mode parameters, object properties |
| `"Project Inspector Bottom"` | Bottom section | Stats, telemetry, debug toggles |

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

| Alias        | Underlying Type        | Rule                                                                                                    |
| ------------ | ---------------------- | ------------------------------------------------------------------------------------------------------- |
| `Scope<T>`   | `std::unique_ptr<T>`   | **Single owner.** One system holds and destroys this. Use for windows, layers, dedicated sub-modules.   |
| `Ref<T>`     | `std::shared_ptr<T>`   | **Shared owner.** Destroyed when the last holder releases it. Use for textures, shaders, materials, scenes. |

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

---

## 3. Application Lifecycle

The `Application` singleton drives the entire engine. You interact with it through `Cosmic::Application::Get()`.

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

The legacy macro forms (`CS_BIND_EVENT_FN(fn)` and `GLCORE_BIND_EVENT_FN(fn)`) both expand identically to `std::bind(&fn, this, std::placeholders::_1)` and are defined in `Core.h`. They still compile and produce correct behavior; prefer lambdas in new code.

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

| Event Class                | Useful Accessors                   | Notes                                      |
| -------------------------- | ---------------------------------- | ------------------------------------------ |
| `KeyPressedEvent`          | `GetKeyCode()`, `GetRepeatCount()` | `RepeatCount > 0` = key held               |
| `KeyReleasedEvent`         | `GetKeyCode()`                     | Fired once on key release                  |
| `KeyTypedEvent`            | `GetKeyCode()`                     | Character input for text fields            |
| `MouseButtonPressedEvent`  | `GetMouseButton()`                 | Use `CS_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE`    |
| `MouseButtonReleasedEvent` | `GetMouseButton()`                 |                                            |
| `MouseMovedEvent`          | `GetX()`, `GetY()`                 | Screen-space coordinates, top-left origin  |
| `MouseScrolledEvent`       | `GetXOffset()`, `GetYOffset()`     | Y is typically ±1.0 per scroll tick        |
| `WindowResizeEvent`        | `GetWidth()`, `GetHeight()`        | Pixel dimensions of the new window size    |
| `WindowCloseEvent`         | —                                  | Consumed by `Application` before layers    |

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

| Category Constant          | Covers                            |
| -------------------------- | --------------------------------- |
| `EventCategoryApplication` | Window resize, close, tick        |
| `EventCategoryInput`       | All keyboard + all mouse          |
| `EventCategoryKeyboard`    | Key press, release, typed         |
| `EventCategoryMouse`       | Mouse move, scroll, button        |
| `EventCategoryMouseButton` | Mouse button press/release only   |

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

| Use `Input::`                               | Use `OnEvent`                                     |
| ------------------------------------------- | ------------------------------------------------- |
| Continuous hold checks (movement, camera)   | Single-press reactions (menu toggle, fire once)   |
| Per-frame polling loops                     | State change notifications                        |
| "Is this key held right now?"               | "Did the user just press Escape?"                 |

### Key Code Constants

| Constant              | Value  | Constant                 | Value   |
| --------------------- | ------ | ------------------------ | ------- |
| `CS_KEY_SPACE`        | 32     | `CS_KEY_ESCAPE`          | 256     |
| `CS_KEY_A`–`CS_KEY_Z` | 65–90  | `CS_KEY_ENTER`           | 257     |
| `CS_KEY_0`–`CS_KEY_9` | 48–57  | `CS_KEY_TAB`             | 258     |
| `CS_KEY_RIGHT`        | 262    | `CS_KEY_BACKSPACE`       | 259     |
| `CS_KEY_LEFT`         | 263    | `CS_KEY_LEFT_SHIFT`      | 340     |
| `CS_KEY_DOWN`         | 264    | `CS_KEY_LEFT_CONTROL`    | 341     |
| `CS_KEY_UP`           | 265    | `CS_KEY_LEFT_ALT`        | 342     |
| `CS_KEY_F1`–`CS_KEY_F12` | 290–301 | `CS_KEY_Q` / `CS_KEY_E` | 81 / 69 |

### Mouse Button Constants

| Constant                 | Alias               | Button               |
| ------------------------ | ------------------- | -------------------- |
| `CS_MOUSE_BUTTON_LEFT`   | `CS_MOUSE_BUTTON_1` | Primary action       |
| `CS_MOUSE_BUTTON_RIGHT`  | `CS_MOUSE_BUTTON_2` | Secondary / context  |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` | Pan / zoom           |

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

When `TimeScale` is negative, `GetAbsoluteTime()` decreases each frame. Any shader reading `u_Time` will naturally scrub backward, making animated effects appear to reverse without any code changes on your end.

### Per-Layer Local Time

Every `Layer` has its own local timeline accumulator. The engine calls `layer->UpdateLayerTime(scaledDelta)` once per frame before `OnUpdate`. Call these helpers inside your layer body directly:

```cpp
float t  = GetLocalTime();    // accumulated scaled time in seconds
float s  = GetTimeScale();    // this layer's own scale multiplier (default 1.0)
SetLocalTime(0.0f);           // reset (e.g. on level restart)
SetTimeScale(0.5f);           // slow this layer independently of the global scale
```

> **Critical:** `GetLocalTime()` is an **instance method** on the base `Layer` class. Always call it as `GetLocalTime()` inside your derived class body — never as `Cosmic::Layer::GetLocalTime()`. The latter form performs static scope resolution and will either fail to compile or invoke the wrong context.

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

| Aspect              | `OnUpdate(float ts)`                          | `OnFixedUpdate(float dt)`                        |
| ------------------- | --------------------------------------------- | ------------------------------------------------ |
| **Purpose**         | Visual updates, animation, camera             | Physics, collision, deterministic simulation     |
| **Rate**            | Variable — depends on monitor refresh rate    | Fixed at 60 Hz regardless of frame rate          |
| **Input**           | Scaled variable delta-time in seconds         | Constant 1/60s interval (also globally scaled)   |
| **Rendering calls** | Yes — call `BeginScene`/`EndScene` here       | No — never issue draw calls here                 |
| **Shader uniforms** | Yes — update `u_Time`, `u_Color` etc. here    | No — GPU state should not be touched here        |
| **Anti-pattern**    | Running collision math that breaks at 144Hz   | Running sprite rotation or lerp animations       |
| **Timeline guards** | `ts` is pre-scaled, no manual multiplication  | Check `dt <= 0.0f` to guard pause/rewind states  |

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

**Manual accumulation ignoring time scale:** Writing `m_Time += ts;` inside your layer bypasses both the global `TimeScale` and the layer's own scale. Use `GetLocalTime()` instead — it is already correctly accumulated by the engine.

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
ImGui::Text("Quads:      %d", stats.QuadCount);
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
glm::vec4     color     = material->GetVector4("u_Color");
Ref<Cosmic::Texture2D> tex = material->GetTexture("u_NoiseTex");

// Check presence before reading
if (material->HasFloat("u_Roughness")) { ... }
if (material->HasFloat2("u_Offset"))   { ... }
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

| Uniform            | Type    | Trigger Keywords                                                | Stage          |
| ------------------ | ------- | --------------------------------------------------------------- | -------------- |
| `u_ViewProjection` | `mat4`  | `u_ViewProjection`                                              | Vertex only    |
| `u_Time`           | `float` | `u_Time`, `iTime`, `TIME`, `_Time`                              | Any stage      |
| `u_ViewportSize`   | `vec2`  | `u_ViewportSize`, `iResolution`, `BUFFER_SIZE`, `_ScreenParams` | Any stage      |

**To bypass injection entirely:** Declare the uniform explicitly in your source. The preprocessor sees the `uniform` keyword on that line and skips injection. Your source is compiled verbatim after the `#type` split.

**Comment safety:** Block comments (`/* */`) and line comments (`//`) are stripped from a working copy before the scan. A commented-out declaration does **not** count as a declaration — the preprocessor will still inject a live one if the name appears in live code.

### The Canonical Boilerplate

Copy this as the starting point for any new shader. It declares everything explicitly and matches the `Renderer2D` batch vertex attribute layout:

```glsl
#type vertex
#version 330 core

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
#version 330 core

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

| Function / Constructor | Parameters                                                            | Description                                                                      |
| ---------------------- | --------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| `CreateFromCoords`     | `Ref<Texture2D>, vec2 coords, vec2 cellSize, vec2 spriteSize={1,1}`   | Static factory. `coords` is (column, row) in grid units from the bottom-left.    |
| `SubTexture2D`         | `Ref<Texture2D>, vec2 min, vec2 max`                                  | Direct UV-range constructor. `min`/`max` in normalized [0,1] texture space.      |
| `GetTexture()`         | —                                                                     | Returns `const Ref<Texture2D>&` — the parent atlas.                              |
| `GetTexCoords()`       | —                                                                     | Returns `const glm::vec2*` — pointer to the 4-element UV corner array (CCW order). |

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

| `thickness` | Visual result                           |
| ----------- | --------------------------------------- |
| `1.0`       | Solid filled disk                       |
| `0.5`       | Half-thickness ring (50% outer radius)  |
| `0.1`       | Narrow ring                             |
| `0.01`      | Very thin ring / orbit indicator        |

| `fade`    | Visual result                              |
| --------- | ------------------------------------------ |
| `0.005`   | Default — crisp, anti-aliased edge         |
| `0.05`    | Soft glow edge                             |
| `0.2`     | Very blurry / neon glow effect             |

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

| Quadrant     | Bounds `{x, y, w, h}`   |
| ------------ | ----------------------- |
| Top-left     | `{0, 360, 640, 360}`    |
| Top-right    | `{640, 360, 640, 360}`  |
| Bottom-left  | `{0, 0, 640, 360}`      |
| Bottom-right | `{640, 0, 640, 360}`    |

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

// Entity handle boolean — true when the handle is valid and scene-bound
if (player) { /* handle is valid */ }
```

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
float z = m_Camera.GetZoomLevel();
m_Camera.SetZoomLevel(2.0f);           // hard snap (bypasses interpolation)
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

---

## 17. Virtual File System

`FileSystem` is a static utility that translates URI-style paths into physical disk paths. It keeps asset references independent of absolute disk locations.

### Protocol Prefixes

| Prefix        | Resolves to                                                | Use for                        |
| ------------- | ---------------------------------------------------------- | ------------------------------ |
| `engine://`   | `assets/{path}`                                            | Engine-owned shaders, sprites  |
| `project://`  | `assets/projects/{activeProject}/{path}`                  | Your project's own assets      |
| *(none)*      | Returned unchanged (raw path fallback)                     | Absolute or already-resolved   |

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

By default, every OpenGL draw call writes pixels directly to the screen's back buffer — the surface that gets swapped to the monitor at the end of each frame. This is the fastest path for full-screen rendering, but it creates a problem when you want to display your scene *inside* a UI panel rather than directly on the screen.

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
spec.Samples        = 1;           // MSAA sample count (1 = no MSAA)
spec.SwapChainTarget = false;      // true = render directly to the screen

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

| Macro         | Level       | When to use                                          |
| ------------- | ----------- | ---------------------------------------------------- |
| `CS_TRACE`    | Trace       | High-frequency per-frame diagnostics                 |
| `CS_INFO`     | Info        | Normal lifecycle events (attach, load, connect)      |
| `CS_WARN`     | Warning     | Recoverable issues (missing file, fallback used)     |
| `CS_ERROR`    | Error       | Non-fatal failures                                   |
| `CS_CRITICAL` | Critical    | Unrecoverable failures                               |

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
activeMode->UpdateLayerTime(ts);         // drive the mode's local clock
if (m_SharedMaterial)
    m_SharedMaterial->Set("u_Time", activeMode->GetLocalTime());
activeMode->OnUpdate(ts);                // visual updates

// In TemplateProject::OnFixedUpdate
m_Modes[m_ActiveModeIndex]->OnFixedUpdate(deltaFixedTime); // physics

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

Setting this to zero pauses all modes simultaneously. Setting it negative reverses time — the shared material's `u_Time` scrubs backward automatically because it's driven by `GetLocalTime()`, which reflects the scaled accumulation.

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

| Method                    | Available In    | Description                                                          |
| ------------------------- | --------------- | -------------------------------------------------------------------- |
| `ForEachAsync(func)`      | OnParallelExecute | Submit parallel per-element jobs — `func(T& item)`                  |
| `DispatchAsync(func)`     | OnParallelExecute | Submit parallel range jobs — `func(T* begin, T* end)`               |
| `ForEach(func)`           | OnPrepare / OnMerge | Sequential iteration — `func(T& item)`                           |
| `ForEachWithEntity(func)` | OnMerge         | Sequential with entity handle — `func(T& item, entt::entity e)`      |
| `Data()`                  | Any phase       | Raw pointer to the staged array                                      |
| `Count()`                 | Any phase       | Number of staged elements                                            |
| `IsEmpty()`               | Any phase       | True if no components of type T exist in the scene                   |
| `operator[](i)`           | Any phase       | Indexed access to a staged element                                   |
| `EntityAt(i)`             | Any phase       | Entity handle for element at index `i`                               |

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

// Get a reference to an already-registered system
BallPhysicsSystem* phys = m_Scene->GetSystem<BallPhysicsSystem>();
if (phys) phys->Gravity = -15.f;

// Remove all systems (e.g. on level unload)
m_Scene->RemoveAllSystems();
```

### Scene::OnRender

`OnRender` handles the full render pass autonomously — it calls `BeginScene` with the provided camera, iterates all entities that have both `TransformComponent` and `SpriteRendererComponent`, groups them by material bucket to minimize draw calls, then calls `EndScene`. Do **not** wrap a `Scene::OnRender` call inside your own `BeginScene`/`EndScene` pair.

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

| Function                | Parameters                                                                              | Description                                                             |
| ----------------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| `BeginScene`            | `const OrthographicCamera&`                                                             | Start a batch pass; cache VP matrix, reset buffers                      |
| `EndScene`              | —                                                                                       | Flush all batched geometry to GPU                                       |
| `PushRenderPass`        | `const glm::mat4& viewProj, const glm::vec4& viewportBounds`                            | Flush pending geometry; push new VP matrix + viewport                   |
| `PopRenderPass`         | —                                                                                       | Flush; pop current pass; restore prior pass state                       |
| `Flush`                 | —                                                                                       | Submit all staged quads, lines, and circles to GPU immediately          |
| `SetViewportSize`       | `uint32_t width, uint32_t height`                                                       | Update `u_ViewportSize` uniform source                                  |
| `DrawQuad`              | `vec2/vec3 pos, vec2 size, vec4 color`                                                  | Flat-color quad; `vec2` inserts z=0                                     |
| `DrawQuad`              | `vec2/vec3 pos, vec2 size, Ref<Texture>, float tiling=1, vec4 tint=white`               | Textured quad                                                           |
| `DrawQuad`              | `vec2/vec3 pos, vec2 size, Ref<Material>`                                               | Material/shader-driven quad                                             |
| `DrawQuad`              | `vec2/vec3 pos, vec2 size, Ref<SubTexture2D>, vec4 tint=white`                          | Sprite-atlas tile                                                       |
| `DrawRotatedQuad`       | `vec2/vec3 pos, vec2 size, float rot, vec4 color`                                       | Rotated flat quad (rot in radians)                                      |
| `DrawRotatedQuad`       | `vec2/vec3 pos, vec2 size, float rot, Ref<Texture>, float tiling=1, vec4 tint=white`    | Rotated textured quad                                                   |
| `DrawRotatedQuad`       | `vec3 pos, vec2 size, float rot, Ref<Material>`                                         | Rotated material quad (vec3 only)                                       |
| `DrawRotatedQuad`       | `vec2/vec3 pos, vec2 size, float rot, Ref<SubTexture2D>, vec4 tint=white`               | Rotated sprite-atlas tile                                               |
| `DrawCircle`            | `vec3 pos, vec2 size, vec4 color, float thickness, float fade, Ref<Shader>=nullptr`     | SDF circle; explicit thickness+fade required for vec3 overload          |
| `DrawCircle`            | `vec2 pos, vec2 size, vec4 color, float thickness=1, float fade=0.005, Ref<Shader>=null` | SDF circle; z=0; thickness+fade have defaults                          |
| `DrawLine`              | `vec3 p0, vec3 p1, vec4 color`                                                          | Line segment between two world-space points                             |
| `DrawRect`              | `vec3 pos, vec2 size, vec4 color`                                                       | Wireframe rectangle (4 lines)                                           |
| `DrawInstancedQuads`    | `const InstanceQuadData* instances, uint32_t count, Ref<Shader>=nullptr`                | Single GPU instanced draw call for N quads                              |
| `DrawInstancedCircles`  | `const InstanceCircleData* instances, uint32_t count, Ref<Shader>=nullptr`              | Single GPU instanced draw call for N circles                            |
| `ResetStats`            | —                                                                                       | Clear draw call and geometry counters                                   |
| `GetStats`              | —                                                                                       | Returns `Statistics` struct                                             |
| `SetStatsStatus`        | `bool enabled`                                                                          | Toggle stats recording                                                  |

**`Statistics` struct:**

| Field                   | Type       | Description                              |
| ----------------------- | ---------- | ---------------------------------------- |
| `DrawCalls`             | `uint32_t` | Number of GPU draw calls this frame      |
| `QuadCount`             | `uint32_t` | Number of batched quads submitted        |
| `LineCount`             | `uint32_t` | Number of batched lines submitted        |
| `GetTotalVertexCount()` | `uint32_t` | `QuadCount * 4 + LineCount * 2`          |
| `GetTotalIndexCount()`  | `uint32_t` | `QuadCount * 6`                          |

---

### Material

| Function           | Parameters                    | Description                                                      |
| ------------------ | ----------------------------- | ---------------------------------------------------------------- |
| `Material::Create` | `Ref<Shader>, string name`    | Factory — creates a new material                                 |
| `Clone`            | `Ref<Material>, string name`  | Deep-copy all uniform caches; shares the same `Ref<Shader>`      |
| `Set`              | `string name, float`          | Set a scalar float uniform                                       |
| `Set`              | `string name, vec2`           | Set a 2-component vector uniform                                 |
| `Set`              | `string name, vec3`           | Set a 3-component vector uniform                                 |
| `Set`              | `string name, vec4`           | Set a 4-component vector uniform                                 |
| `Set`              | `string name, Ref<Texture>`   | Bind a texture to a named slot                                   |
| `GetFloat`         | `string name`                 | Retrieve cached float (0.0 if missing)                           |
| `GetVector2`       | `string name`                 | Retrieve cached vec2 (zero if missing)                           |
| `GetVector3`       | `string name`                 | Retrieve cached vec3 (zero if missing)                           |
| `GetVector4`       | `string name`                 | Retrieve cached vec4 (white if missing)                          |
| `GetVector`        | `string name`                 | Legacy alias for `GetVector4`; returns `glm::vec4`               |
| `GetTexture`       | `string name`                 | Retrieve cached texture (`nullptr` if missing)                   |
| `Bind`             | —                             | Bind shader and upload all cached uniforms                       |
| `GetShader`        | —                             | Returns the underlying `Ref<Shader>`                             |
| `GetName`          | —                             | Returns the material's debug name string                         |
| `HasFloat`         | `string name`                 | True if the float uniform is set                                 |
| `HasFloat2`        | `string name`                 | True if the vec2 uniform is set                                  |

---

### Shader

| Function         | Parameters                       | Description                              |
| ---------------- | -------------------------------- | ---------------------------------------- |
| `Shader::Create` | `string filepath`                | Load and compile from a `.glsl` file     |
| `Bind`           | —                                | Activate in the GPU pipeline             |
| `Unbind`         | —                                | Deactivate                               |
| `SetInt`         | `string, int`                    | Upload integer uniform                   |
| `SetIntArray`    | `string, int*, uint32_t count`   | Upload integer array uniform             |
| `SetFloat`       | `string, float`                  | Upload float uniform                     |
| `SetFloat2`      | `string, vec2`                   | Upload 2-component float uniform         |
| `SetFloat3`      | `string, vec3`                   | Upload 3-component float uniform         |
| `SetFloat4`      | `string, vec4`                   | Upload 4-component float uniform         |
| `SetMat3`        | `string, mat3`                   | Upload 3×3 matrix uniform                |
| `SetMat4`        | `string, mat4`                   | Upload 4×4 matrix uniform                |

---

### SubTexture2D

| Function / Constructor | Parameters                                                                  | Description                                                                             |
| ---------------------- | --------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `CreateFromCoords`     | `Ref<Texture2D>, vec2 coords, vec2 cellSize, vec2 spriteSize={1,1}`         | Static factory. `coords` is (column, row) in grid units from bottom-left.               |
| `SubTexture2D`         | `Ref<Texture2D>, vec2 min, vec2 max`                                        | Direct UV-range constructor. `min`/`max` in normalized [0,1] texture space.             |
| `GetTexture()`         | —                                                                           | Returns `const Ref<Texture2D>&` — the parent atlas.                                     |
| `GetTexCoords()`       | —                                                                           | Returns `const glm::vec2*` — pointer to the 4-element UV corner array (CCW order).      |

---

### OrthographicCameraController

| Function                    | Parameters                                          | Description                                                                         |
| --------------------------- | --------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `OrthographicCameraController` | `float aspectRatio, bool rotation=false`         | Constructor. Enables Q/E rotation support if `rotation=true`.                       |
| `OnUpdate`                  | `float ts`                                          | Poll WASD input, advance zoom interpolation, update camera transform. Call from `OnUpdate`. |
| `OnEvent`                   | `Event&`                                            | Route `MouseScrolledEvent` to zoom, `WindowResizeEvent` to `OnResize`.              |
| `OnResize`                  | `float width, float height`                         | Recalculate aspect ratio and projection. Call when the render target changes size.  |
| `GetCamera`                 | —                                                   | Returns `OrthographicCamera&` for `BeginScene` or `RenderPass`.                     |
| `GetZoomLevel`              | —                                                   | Current interpolated zoom scalar.                                                   |
| `SetZoomLevel`              | `float level`                                       | Hard-snap zoom, bypassing interpolation.                                            |
| `SetZoomLimits`             | `float min, float max`                              | Clamp the scroll-wheel zoom range. Default: 0.25–10.0.                              |
| `SetZoomSpeed`              | `float speed`                                       | World units per scroll tick. Default: 0.25.                                         |
| `GetZoomSpeed`              | —                                                   | Returns current zoom speed.                                                         |
| `SetTranslationSpeed`       | `float speed`                                       | Pan speed in world units/second. Scales with zoom level. Default: 5.0.              |
| `GetTranslationSpeed`       | —                                                   | Returns current translation speed.                                                  |
| `SetRotationSpeed`          | `float speed`                                       | Degrees/second for Q/E rotation. Only active when `rotation=true`. Default: 180.0.  |
| `GetRotationSpeed`          | —                                                   | Returns current rotation speed.                                                     |
| `SetPositionLimits`         | `float minX, float maxX, float minY, float maxY`    | Hard-clamp pan bounds in world space. Default: ±1000 on both axes.                  |
| `SetPosition`               | `const glm::vec3& position`                         | Directly set world position, bypassing keyboard input.                              |
| `GetPosition`               | —                                                   | Returns `const glm::vec3&` — current camera world position.                         |
| `SetManualMovementEnabled`  | `bool enabled`                                      | Enable/disable WASD panning. Disable for code-driven cameras. Default: true.        |
| `IsManualMovementEnabled`   | —                                                   | Returns true if keyboard panning is active.                                         |
| `SetKeyBindings`            | `const CameraKeyBindings& bindings`                 | Replace the default WASD+QE key mapping.                                            |
| `GetKeyBindings`            | —                                                   | Returns a mutable reference to the active key bindings.                             |

---

### Layer Timeline API

| Function              | Description                                                                    |
| --------------------- | ------------------------------------------------------------------------------ |
| `GetLocalTime()`      | Returns the accumulated scaled time for this layer (seconds).                  |
| `SetLocalTime(float)` | Directly set the time accumulator (e.g. for level reset).                      |
| `GetTimeScale()`      | Returns this layer's local time scale multiplier.                              |
| `SetTimeScale(float)` | Set the layer's local scale (0=paused, 0.5=half speed, -1=rewind).             |
| `UpdateLayerTime(float dt)` | Called by the engine each frame. Do not call manually in normal usage.   |

---

### Application

| Function                                      | Description                                                                    |
| --------------------------------------------- | ------------------------------------------------------------------------------ |
| `Application::Get()`                          | Static singleton accessor.                                                     |
| `GetWindow()`                                 | Returns `Window&`.                                                             |
| `GetFrameBuffer()`                            | Returns `Ref<FrameBuffer>` — the main render target.                           |
| `GetWorkspaceLayer()`                         | Returns `WorkspaceLayer*` (nullptr before a DLL transition).                   |
| `PushLayer(Layer*)`                           | Add a layer to the stack.                                                      |
| `PushOverlay(Layer*)`                         | Add an overlay (always above layers) to the stack.                             |
| `TransitionFromLauncherToWorkspace(string dll)` | Queue a DLL load transition for the Safe Zone.                               |
| `TransitionToLauncher()`                      | Queue a return to the Launcher for the Safe Zone.                              |
| `UseFixedTimeStep(bool)`                      | Enable/disable the 60Hz fixed update pass.                                     |
| `SetTimeScale(float)`                         | Set the global time scale multiplier.                                          |
| `GetTimeScale()`                              | Returns the current global time scale.                                         |
| `GetAbsoluteTime()`                           | Returns raw engine uptime in seconds (unaffected by time scale).               |
| `Close()`                                     | Signal the engine to exit the run loop cleanly.                                |

---

### FrameBuffer

| Function                      | Parameters                          | Description                                                          |
| ----------------------------- | ----------------------------------- | -------------------------------------------------------------------- |
| `FrameBuffer::Create`         | `const FramebufferSpecification&`   | Factory — creates a platform-specific FBO instance.                  |
| `Bind`                        | —                                   | Redirect subsequent draw calls to this FBO.                          |
| `Unbind`                      | —                                   | Restore the default screen framebuffer.                              |
| `Resize`                      | `uint32_t width, uint32_t height`   | Reallocate GPU textures at new dimensions.                           |
| `GetWidth`                    | —                                   | Returns current FBO width in pixels.                                 |
| `GetHeight`                   | —                                   | Returns current FBO height in pixels.                                |
| `GetColorAttachmentRendererID` | —                                  | Returns the OpenGL texture ID for the color buffer (for ImGui::Image). |
| `GetSpecification`            | —                                   | Returns `const FramebufferSpecification&`.                           |

**`FramebufferSpecification` fields:**

| Field            | Type       | Default | Description                                        |
| ---------------- | ---------- | ------- | -------------------------------------------------- |
| `Width`          | `uint32_t` | 0       | Width in pixels.                                   |
| `Height`         | `uint32_t` | 0       | Height in pixels.                                  |
| `Samples`        | `uint32_t` | 1       | MSAA sample count (1 = no MSAA).                   |
| `SwapChainTarget`| `bool`     | false   | True if targeting the system back buffer directly. |

---

### Scene

| Function                         | Description                                                                            |
| -------------------------------- | -------------------------------------------------------------------------------------- |
| `Scene::Create()`                | Static factory — returns `Ref<Scene>`.                                                 |
| `CreateEntity(string name)`      | Instantiate entity with `TransformComponent` + `TagComponent`. Default tag = `"GenericEntity"`. |
| `DestroyEntity(Entity)`          | Remove entity and all its components from the registry.                                |
| `OnUpdate(float dt)`             | Tick all sequential system `OnUpdate` passes + parallel prepare/execute/merge passes. |
| `OnFixedUpdate(float dt)`        | Tick all sequential system `OnFixedUpdate` passes + parallel fixed-step passes.        |
| `OnRender(const OrthographicCamera&)` | Full render pass — `BeginScene`, draw all `SpriteRendererComponent` entities by material bucket, `EndScene`. |
| `AddSystem<T>(args...)`          | Allocate and attach a system. Returns `T&`. Automatically registers `ParallelSystem`s. |
| `GetSystem<T>()`                 | Returns `T*` if a system of that type is registered, `nullptr` otherwise.              |
| `RemoveAllSystems()`             | Clear all registered systems.                                                          |
| `View<Components...>()`          | Returns an EnTT view for iterating entities with all listed component types.           |
| `GetRegistry()`                  | Returns `entt::registry&` for direct registry access.                                  |

---

### Window

| Function                        | Description                                                                                          |
| ------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `GetWidth()`                    | Current window client-area width in pixels.                                                          |
| `GetHeight()`                   | Current window client-area height in pixels.                                                         |
| `SetVSync(bool)`                | Enable/disable vertical synchronization.                                                             |
| `GetHandle()`                   | Returns `GLFWwindow*` for API-specific calls.                                                        |
| `SetFullscreen(bool)`           | Toggle borderless-windowed fullscreen on the current monitor.                                        |
| `IsFullscreen()`                | Returns the current fullscreen state.                                                                |
| `SetFullscreenHotkeyOverride(fn)` | Register a callback `(int key, int action, int mods) -> bool` to intercept key events before F11.  |
| `ClearFullscreenHotkeyOverride()` | Remove the registered callback. Call from `OnDetach` before DLL unload.                            |
