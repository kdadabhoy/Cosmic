# Cosmic Engine — Complete Developer Reference

> **How to use this document:** This README is split into three major parts. **Part I** is for anyone building games or simulations with Cosmic — it covers every API you'll interact with, with concrete examples and a full command reference table. **Part II** is an internal technical deep-dive covering the architecture, the OpenGL graphics pipeline, the DLL plugin system, and how time flows through the engine. **Part III** is a code review with items flagged for refactor, missing implementations, and technical debt.

---

## Table of Contents

### Part I — Client Developer Guide

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
13. [RenderPass and Multi-Camera Rendering](#13-renderpass-and-multi-camera-rendering)
14. [Entity Component System](#14-entity-component-system)
15. [Camera System](#15-camera-system)
16. [Virtual File System](#16-virtual-file-system)
17. [Framebuffer](#17-framebuffer)
18. [Logging](#18-logging)
19. [Serial Communication](#19-serial-communication)
20. [The Showcase Project](#20-the-showcase-project)
21. Update [Complete API Reference Tables](#21-complete-api-reference-tables)
22. Reorder Eventually - [Scene System](#22-scene-system)
23. Reorder Eventually - [Window System](#23)

### Part II — Engine Internals

22. [Source File Map](#22-source-file-map)
23. [Hot-Reloadable DLL Architecture](#23-hot-reloadable-dll-architecture)
24. [Top-Down Time Propagation Waterfall](#24-top-down-time-propagation-waterfall)
25. [The Double-Tick Trap](#25-the-double-tick-trap)
26. [The OpenGL Graphics Pipeline](#26-the-opengl-graphics-pipeline)
27. [Hardware Abstraction Architecture](#27-hardware-abstraction-architecture)
28. [Batch Rendering Deep Dive](#28-batch-rendering-deep-dive)
29. Update [Shader Preprocessing System](#29-shader-preprocessing-system)
30. [RenderPass Stack — Implementation Details](#30-renderpass-stack--implementation-details)
31. [Build System](#31-build-system)
32. [Event System — Implementation Details](#32-event-system--implementation-details)

### Part III — Code Review (Outdated)

33. [Refactor Candidates](#33-refactor-candidates)
34. [Missing Implementations](#34-missing-implementations)
35. [Technical Debt & Open Issues](#35-technical-debt--open-issues)

---

# Part I — Client Developer Guide

---

## 1. Getting Started

### What is Cosmic?

Cosmic is a C++20, OpenGL-backed 2D game and simulation engine. It runs on Windows (x64) and is structured around a **plugin model** — your game compiles into a `.dll` that the engine loads at runtime. This means you can iterate on your game code without recompiling the engine itself.

The entry point for any project is a `Layer` class. You subclass it, implement the hooks you care about, and export it from your DLL.

### Project Structure

```
YourSDKRoot/
├── Cosmic/                 ← Engine source and dependencies
├── Runtime/                ← Engine host executable (Main.cpp)
├── Projects/               ← Your game projects live here
│   └── YourProject/
│       ├── src/            ← Your C++ source files
│       ├── assets/         ← Textures, shaders, etc.
│       ├── CMakeLists.txt  ← Build configuration
│       └── build.bat       ← One-click build script
└── build/
    └── Runtime/
        └── Debug/          ← All compiled outputs land here
```

### Minimal Project Skeleton

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
        virtual ~YourProject() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(float ts) override;
        virtual void OnImGuiRender() override;

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
    YourProject::YourProject() : Layer("YourProject")
    {
        Cosmic::FileSystem::SetActiveProject("YourProject");
    }

    void YourProject::OnAttach() { /* load assets */ }
    void YourProject::OnDetach() { /* free assets */ }

    void YourProject::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f});
        Cosmic::Renderer2D::EndScene();
    }

    void YourProject::OnImGuiRender()
    {
        ImGui::Begin("My Panel");
        ImGui::Text("Hello, Cosmic!");
        ImGui::End();
    }
}

// ============================================================
// REQUIRED: DLL export signatures — do not remove or rename
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

Build with `build.bat`, then launch the engine and select your project from the Launcher.

---

## 2. Memory Management

Cosmic wraps standard C++ smart pointers into two named aliases to enforce explicit ownership rules and prevent ambiguity.

| Alias      | Underlying Type      | Rule                                                                                                                                           |
| ---------- | -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `Scope<T>` | `std::unique_ptr<T>` | **Single owner.** One system holds and destroys this. Use for windows, layers, dedicated sub-modules.                                          |
| `Ref<T>`   | `std::shared_ptr<T>` | **Shared owner.** Multiple systems hold a reference; destroyed when the last holder releases it. Use for textures, shaders, materials, scenes. |

Always use the factory helpers — never call `new` into a smart pointer directly:

```cpp
// Single-owner creation
Scope<MySystem> sys = CreateScope<MySystem>(arg1, arg2);

// Shared resource creation
Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create("assets/sprite.png");
Ref<Cosmic::Scene>     scene = Cosmic::Scene::Create();
```

### Why this matters across DLL boundaries

When your game compiles as a separate `.dll`, both sides share the same `Ref<T>` reference count as long as they link against the same `Cosmic.dll`. Raw `Layer*` pointers are intentionally used at DLL entry points because they cross compilation boundaries — the engine takes full ownership and is responsible for `delete`.

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
    for each layer: layer->OnFixedUpdate(1/60s * timeScale)
    │
    ▼
Pass 1B — Variable Timestep (frame rate dependent)
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
    (no iterators active — push/pop/delete layers here)
```

**Fixed vs. Variable Timestep:** Use `OnFixedUpdate` for physics, collision, and serial I/O — anything that breaks under inconsistent frame timing. Use `OnUpdate` for animation, visual state, and camera movement.

**Spiral-of-Death Protection:** If a single frame takes longer than 250ms (e.g., during a debugger pause), the fixed timestep accumulator is clamped so the engine won't attempt to simulate hundreds of ticks to "catch up."

### Application Control API

```cpp
Cosmic::Application& app = Cosmic::Application::Get();

// Time control
app.UseFixedTimeStep(true);       // enable/disable 60Hz physics pass
app.SetTimeScale(0.5f);           // 0.5 = half speed slow motion
app.SetTimeScale(0.0f);           // pause
app.SetTimeScale(-1.0f);          // rewind

// Window
app.GetWindow().GetWidth();
app.GetWindow().GetHeight();

// Transitions (queued for the Safe Zone — safe to call from anywhere)
app.TransitionFromLauncherToWorkspace("MyProject.dll");
app.TransitionToLauncher();

// Framebuffer access
Ref<Cosmic::FrameBuffer> fb = app.GetFrameBuffer();

// Shutdown
app.Close(); // sets m_Running = false, exits the loop
```

---

## 4. The Layer System

A `Layer` is the fundamental building block. Every game world, simulation mode, editor panel, and UI overlay is a `Layer`. The `LayerStack` manages their update and event dispatch order.

### Layer Hooks

Override only what you need:

```cpp
class MyLayer : public Cosmic::Layer
{
public:
    MyLayer() : Layer("MyLayer") {}

    // Called once when pushed onto the LayerStack
    // Load textures, initialize scenes, allocate GPU resources here
    void OnAttach() override { ... }

    // Called once when popped from the LayerStack or on engine shutdown
    // Reset Ref<> handles, close files, release resources here
    void OnDetach() override { ... }

    // Variable timestep — called once per frame
    // Use for rendering, camera, animation
    void OnUpdate(float deltaTime) override { ... }

    // Fixed timestep — called at 60 Hz regardless of frame rate
    // Use for physics, collision detection, serial polling
    void OnFixedUpdate(float fixedDeltaTime) override { ... }

    // Separate pass for ImGui UI calls
    void OnImGuiRender() override { ... }

    // Receives events from the top of the stack downward
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
// Push inside Application::Initialize or from the Safe Zone
app.PushLayer(new MyGameLayer());
app.PushOverlay(new MyDebugOverlay()); // always on top
```

> **Memory ownership:** The `LayerStack` borrows raw pointers. `Application` owns them and is responsible for `delete`. Never delete a layer pointer registered in the stack.

---

## 5. The Event System

The event system is **reactive and propagating**. When the OS fires a hardware signal — a key press, a mouse click, a window resize — the engine packages it into a typed `Event` object and walks it down the `LayerStack` from top to bottom. Any layer can mark an event as "handled" (`e.Handled = true`) to stop it from reaching layers below.

This is different from [Input Polling (Section 6)](#6-input-polling), which queries the current hardware state on demand. Use events for one-shot reactions ("the user just pressed Escape"), use polling for continuous per-frame checks ("is W held down right now?").

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
│  WorkspaceLayer   │  ← forwards to client DLL layer
│  (layer)          │
└────────┬──────────┘
         │ e.Handled == true? → STOP
         ▼
┌───────────────────┐
│  Your Game Layer  │  ← your OnEvent() runs here
│  (inside DLL)     │
└───────────────────┘
```

> **Key rule:** Events flow top-to-bottom through the stack. The engine reverses the layer iteration order for events (overlays first) so UI layers can intercept clicks before the game world sees them. Set `e.Handled = true` to stop propagation.

### Handling Events in Your Layer

Override `OnEvent` and use `EventDispatcher` to route specific event types to dedicated handler functions. **The preferred modern style uses lambdas** — they are more readable, avoid the macro boilerplate, and capture `this` explicitly rather than implicitly:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    // Preferred: lambda captures this explicitly, no macro plumbing
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        [this](Cosmic::KeyPressedEvent& event) { return OnKeyPressed(event); });

    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
        [this](Cosmic::MouseButtonPressedEvent& event) { return OnMouseClicked(event); });

    dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
        [this](Cosmic::WindowResizeEvent& event) { return OnWindowResize(event); });
}

// Handler signature: takes the specific event type, returns bool
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

The legacy macro form (`GLCORE_BIND_EVENT_FN` / `CS_BIND_EVENT_FN`) still compiles and produces correct behavior. The macros expand to a `std::bind` call with `std::placeholders::_1`. Prefer the lambda form in new code.

| Style                  | Syntax                                        | Status               |
| ---------------------- | --------------------------------------------- | -------------------- |
| Lambda (modern)        | `[this](EventType& e) { return Handler(e); }` | **Preferred**        |
| `CS_BIND_EVENT_FN`     | `CS_BIND_EVENT_FN(MyLayer::Handler)`          | Alias — still valid  |
| `GLCORE_BIND_EVENT_FN` | `GLCORE_BIND_EVENT_FN(MyLayer::Handler)`      | Legacy — still valid |

Both macro names expand identically to `std::bind(&fn, this, std::placeholders::_1)` and are defined in `Core.h`. They exist purely as aliases of each other.

### Forwarding Events to Sub-Systems

If your layer owns sub-systems that need events (like a camera controller or a simulation sub-layer), forward the event to them first, then check `e.Handled` before doing further work:

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

If you have multiple simulation modes but only the active one should receive input, always check `e.Handled` and route carefully. However, window resize events must be forwarded to **all** modes to keep cameras correct:

```cpp
bool MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    float w = (float)e.GetWidth();
    float h = (float)e.GetHeight();
    // Update ALL sims, not just the active one
    for (auto& mode : m_Modes)
        mode->GetCamera().OnResize(w, h);
    return false;
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
        if (m_IsGrounded)
        {
            m_VelocityY = 8.0f;
            m_IsGrounded = false;
            return true;
        }
    }
    return false;
}
```

**Guard input against paused or reversed timelines:**

```cpp
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    if (e.GetRepeatCount() > 0) return false;

    // Don't process gameplay input if time is paused or rewinding
    if (GetTimeScale() <= 0.0f)
    {
        // Still allow reset even while paused
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
| `KeyPressedEvent`          | `GetKeyCode()`, `GetRepeatCount()` | RepeatCount > 0 = key held                |
| `KeyReleasedEvent`         | `GetKeyCode()`                     | Fired once on key release                 |
| `KeyTypedEvent`            | `GetKeyCode()`                     | Character input for text fields           |
| `MouseButtonPressedEvent`  | `GetMouseButton()`                 | Use `CS_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE`   |
| `MouseButtonReleasedEvent` | `GetMouseButton()`                 |                                           |
| `MouseMovedEvent`          | `GetX()`, `GetY()`                 | Screen-space coordinates, top-left origin |
| `MouseScrolledEvent`       | `GetXOffset()`, `GetYOffset()`     | Y is typically ±1.0 per scroll tick       |
| `WindowResizeEvent`        | `GetWidth()`, `GetHeight()`        | Pixel dimensions of new window size       |
| `WindowCloseEvent`         | —                                  | Consumed by Application before layers     |

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

    glm::vec2 cursor = Cosmic::Input::GetMousePosition();
}
```

### When to use Input vs. Events

| Use `Input::`                             | Use `OnEvent`                                   |
| ----------------------------------------- | ----------------------------------------------- |
| Continuous hold checks (movement, camera) | Single-press reactions (menu toggle, fire once) |
| Per-frame polling loops                   | State change notifications                      |
| "Is this key held right now?"             | "Did the user just press Escape?"               |

### Key Code Constants

| Constant              | Value | Constant                 | Value   |
| --------------------- | ----- | ------------------------ | ------- |
| `CS_KEY_SPACE`        | 32    | `CS_KEY_ESCAPE`          | 256     |
| `CS_KEY_A`–`CS_KEY_Z` | 65–90 | `CS_KEY_ENTER`           | 257     |
| `CS_KEY_0`–`CS_KEY_9` | 48–57 | `CS_KEY_LEFT_CONTROL`    | 341     |
| `CS_KEY_UP`           | 265   | `CS_KEY_LEFT_SHIFT`      | 340     |
| `CS_KEY_DOWN`         | 264   | `CS_KEY_LEFT_ALT`        | 342     |
| `CS_KEY_LEFT`         | 263   | `CS_KEY_F1`–`CS_KEY_F12` | 290–301 |
| `CS_KEY_RIGHT`        | 262   | `CS_KEY_TAB`             | 258     |

### Mouse Button Constants

| Constant                 | Alias               | Button              |
| ------------------------ | ------------------- | ------------------- |
| `CS_MOUSE_BUTTON_LEFT`   | `CS_MOUSE_BUTTON_1` | Primary action      |
| `CS_MOUSE_BUTTON_RIGHT`  | `CS_MOUSE_BUTTON_2` | Secondary / context |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` | Pan / zoom          |

---

## 7. Time & Timeline System

Cosmic has a two-level time architecture: a **global application timeline** controlled by the host, and a **per-layer local timeline** each layer owns independently. Understanding both is critical for building simulations that respond correctly to pause, slow-motion, and rewind.

### The Global Time Scale

The `Application` singleton exposes a `TimeScale` multiplier that affects how fast the engine passes time to all layers:

```cpp
Cosmic::Application::Get().SetTimeScale(1.0f);   // normal
Cosmic::Application::Get().SetTimeScale(0.5f);   // half speed
Cosmic::Application::Get().SetTimeScale(0.0f);   // pause
Cosmic::Application::Get().SetTimeScale(-1.0f);  // rewind
```

When `TimeScale` is negative, `m_AbsoluteTime` decreases each frame. Any shader reading `u_Time` will naturally scrub backward, making effects like animated fire appear to reverse.

### Per-Layer Local Time

Every `Layer` has its own local timeline accumulator:

```cpp
// These are instance methods — call without the class prefix inside your layer
float t  = GetLocalTime();       // accumulated time in seconds
float ts = GetTimeScale();       // this layer's scale multiplier (default 1.0)
SetLocalTime(0.0f);              // reset (e.g. on level restart)
SetTimeScale(0.5f);              // slow this layer independently
```

The engine calls `layer->UpdateLayerTime(scaledDelta)` once per frame before `OnUpdate`. You do not need to call it yourself in normal usage.

> **Critical:** `GetLocalTime()` is an **instance method** on the base class. Always call it as `GetLocalTime()` — never as `Cosmic::Layer::GetLocalTime()`. The latter is static scope resolution and will either fail to compile or call into the wrong context.

### How to Feed Time to Shaders

```cpp
void MyLayer::OnUpdate(float ts)
{
    // Feed the layer's local time directly to the material
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
| **Input**           | Scaled variable delta-time in seconds        | Constant 1/60s interval (also scaled)           |
| **Rendering calls** | Yes — call `BeginScene`/`EndScene` here      | No — never issue draw calls here                |
| **Shader uniforms** | Yes — update `u_Time`, `u_Color` etc. here   | No — GPU state should not be touched here       |
| **Anti-pattern**    | Running collision math that breaks at 144Hz  | Running sprite rotation or lerp animations      |
| **Timeline guards** | `ts` is pre-scaled, no manual multiplication | Check `dt <= 0.0f` to guard pause/rewind states |

### Timeline Guards in Fixed Update

When the global `TimeScale` is zero (paused) or negative (rewinding), `OnFixedUpdate` receives a zero or negative `deltaFixedTime`. Always guard against this:

```cpp
void MyLayer::OnFixedUpdate(float dt)
{
    if (dt == 0.0f) return; // Paused — freeze simulation

    if (dt < 0.0f)
    {
        // Optional rewind behavior — move things backwards
        for (auto& obs : m_Obstacles)
        {
            auto& t = obs.GetComponent<Cosmic::TransformComponent>();
            t.Position.x += m_Speed * std::abs(dt); // push back
        }
        return;
    }

    // Normal forward simulation
    m_Score += dt * 10.0f;
}
```

### Common Time Pitfalls

**Scope resolution misuse:** Calling `Cosmic::Layer::GetLocalTime()` with an explicit class prefix inside a derived layer body is incorrect — it either resolves to the wrong context or fails. Drop the prefix and call `GetLocalTime()` directly.

**Animating in `OnFixedUpdate`:** Sprite rotations, camera lerp, and uniform uploads driven from `OnFixedUpdate` produce micro-stuttering at high refresh rates because they only update 60 times per second regardless of monitor speed. Move visual updates to `OnUpdate`.

**Manual accumulation ignoring time scale:** Writing `m_Time += ts;` inside your layer bypasses both the global `TimeScale` and the layer's own scale. Use `GetLocalTime()` instead, which is already correctly accumulated by the engine via `UpdateLayerTime`.

**Unit confusion:** `ts` and `dt` passed to layer hooks are always in **seconds**. Multiplying a velocity in `m/s` by `ts` gives correct displacement. If you need milliseconds for a display value, compute `ts * 1000.f` explicitly.

---

## 8. 2D Rendering API

`Renderer2D` is the primary drawing interface. It batches geometry internally to minimize GPU draw calls.

### Frame Structure

Every render pass must be wrapped in `BeginScene` / `EndScene`:

```cpp
void MyLayer::OnUpdate(float ts)
{
    Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

    // --- submit all draw calls here ---

    Cosmic::Renderer2D::EndScene(); // flushes all batched data to GPU
}
```

### Flat Color Quads

Both `vec2` and `vec3` position overloads are available. The `vec2` shims insert `z = 0.0f` automatically:

```cpp
// vec3 position — explicit z-layering
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.0f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}); // red
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.5f}, {1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}); // green, in front

// vec2 position — z is 0.0 automatically
Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {1.f, 1.f}, {0.f, 0.f, 1.f, 1.f}); // blue
```

### Textured Quads

```cpp
Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create("assets/sprite.png");

// vec3 overloads
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex);
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 2.0f);                           // 2x tiling
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 1.0f, {1.f, 0.5f, 0.5f, 1.f}); // tint

// vec2 convenience overloads
Cosmic::Renderer2D::DrawQuad({0.f, 1.f}, {1.f, 1.f}, tex);
Cosmic::Renderer2D::DrawQuad({0.f, 1.f}, {1.f, 1.f}, tex, 2.0f, {1.f, 1.f, 1.f, 1.f});
```

### Material Quads (Shader-driven)

```cpp
auto shader   = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
auto material = Cosmic::Material::Create(shader, "FireMaterial");
material->Set("u_Color", glm::vec4(1.f, 0.5f, 0.2f, 1.f));

void MyLayer::OnUpdate(float ts)
{
    material->Set("u_Time", GetLocalTime());

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

    // vec3 form
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, material);

    // vec2 convenience form
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {2.f, 2.f}, material);

    Cosmic::Renderer2D::EndScene();
}
```

### Rotated Quads

Both `vec2` and `vec3` position overloads exist for all `DrawRotatedQuad` variants. Rotation is always in **radians**:

```cpp
// Color
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, glm::radians(45.f), {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f},       {1.f, 1.f}, glm::radians(45.f), {1.f, 1.f, 0.f, 1.f}); // vec2

// Texture
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, texture);
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f},       {1.f, 1.f}, rotation, texture); // vec2

// Material
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, material);
```

### SDF Circles

```cpp
// Solid disk
Cosmic::Renderer2D::DrawCircle({0.f, 0.f, 0.f}, {2.f, 2.f}, {0.2f, 0.8f, 1.f, 1.f});

// Hollow ring — thin wall
Cosmic::Renderer2D::DrawCircle({0.f, 0.f, 0.f}, {2.f, 2.f}, {1.f, 0.5f, 0.f, 0.9f}, 0.05f, 0.005f);

// vec2 overload — z inserted as 0
Cosmic::Renderer2D::DrawCircle({0.f, 2.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f});
```

See [Section 12 — SDF Circles](#12-sdf-circles) for the full thickness/fade reference.

### Debug Geometry

```cpp
Cosmic::Renderer2D::DrawLine({-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRect({0.f, 0.f, 0.f}, {2.f, 1.f}, {0.f, 1.f, 1.f, 1.f});
```

### Performance Statistics

```cpp
Cosmic::Renderer2D::ResetStats();

Cosmic::Renderer2D::Statistics stats = Cosmic::Renderer2D::GetStats();
ImGui::Text("Draw Calls: %d", stats.DrawCalls);
ImGui::Text("Quads:      %d", stats.QuadCount);
ImGui::Text("Vertices:   %d", stats.GetTotalVertexCount());
ImGui::Text("Indices:    %d", stats.GetTotalIndexCount());
```

---

## 9. Materials and Shaders

A **Shader** is a GPU program. A **Material** is a shader plus a named set of parameter values (uniforms). Materials allow multiple objects to share the same shader with different visual properties.

### Loading a Shader

```cpp
std::string path = Cosmic::FileSystem::Resolve("project://shaders/MyShader.glsl");
Ref<Cosmic::Shader> shader = Cosmic::Shader::Create(path);
```

### Creating and Using a Material

The material uniform cache supports `float`, `vec2`, `vec3`, `vec4`, and `Ref<Texture>`. All setter types have corresponding getters:

```cpp
auto material = Cosmic::Material::Create(shader, "SpriteMaterial");

// Scalar and vector uniforms
material->Set("u_Color",    glm::vec4(1.f, 0.8f, 0.2f, 1.f));
material->Set("u_Offset",   glm::vec2(0.5f, 0.0f));   // vec2 uniform
material->Set("u_Scale3",   glm::vec3(1.f, 1.f, 1.f));
material->Set("u_Texture",  myTexture);
material->Set("u_Time",     GetLocalTime());            // always use layer's local time

// Read values back (useful for ImGui editors)
glm::vec4 color   = material->GetVector("u_Color");    // returns vec4; white if missing
glm::vec2 offset  = material->GetVector2("u_Offset");  // returns vec2; zero if missing
float     elapsed = material->GetFloat("u_Time");

Cosmic::Renderer2D::DrawQuad(position, scale, material);
```

Note that `GetVector` is a legacy alias for `GetVector4` — both return `glm::vec4`. Use `GetVector2` when reading back a uniform that was set as a `vec2`.

### Shader Single-File Format

```glsl
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    v_Color    = a_Color;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;

uniform float u_Time;
uniform vec4  u_Color;

void main()
{
    color = u_Color * v_Color;
}
```

### Auto-Injected Engine Uniforms

These uniforms are automatically declared by the preprocessor if your shader uses them without declaring them yourself:

| Uniform            | Type          | Source                                               |
| ------------------ | ------------- | ---------------------------------------------------- |
| `u_ViewProjection` | `mat4`        | Camera VP matrix, updated per `BeginScene`           |
| `u_Time`           | `float`       | Set by your layer via `material->Set("u_Time", ...)` |
| `u_ViewportSize`   | `vec2`        | Viewport pixel size, updated per `Flush`             |
| `u_Textures[32]`   | `sampler2D[]` | Batch renderer texture slots, auto-initialized       |

### Shadertoy Compatibility

If your file contains `void mainImage(out vec4 fragColor, in vec2 fragCoord)`, the preprocessor automatically wraps it to run on the engine's batch quad system:

```glsl
// No #type tags needed — the engine detects mainImage and handles everything
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

`iTime` maps to `u_Time` and `iResolution` maps to `vec3(u_ViewportSize, 1.0)` automatically.

---

## 10. The Shader Contract

The Cosmic shader preprocessor exists so that Shadertoy ports and fragment-only
experiments work out of the box without any boilerplate. For production shaders
you write yourself, you need to understand exactly what the preprocessor does,
when it fires, and how to opt out of it entirely — so your shader always compiles
predictably regardless of what the scanner decides to inject.

---

### The Three Processing Paths

When `Shader::Create(filepath)` is called, `OpenGLShader::PreProcess` reads the
entire file and routes it down one of three paths.

**Path 1 — Full multi-stage file**

The file contains at least one `#type vertex` and one `#type fragment` directive.
The preprocessor splits the source at these boundaries and handles each block
independently. For each block it strips comments, scans for the three engine
uniforms, and injects any that are used but not already declared. This is the
path you should be on for every shader you write from scratch. It gives you the
most predictable behavior and the clearest compile errors.

**Path 2 — Fragment-only file**

The file contains `#type fragment` but no `#type vertex`. The preprocessor
generates a complete boilerplate vertex shader that matches the `Renderer2D`
quad layout and prepends it automatically. Your fragment block is compiled
with the same uniform injection rules as Path 1. Use this when you only care
about the fragment stage and are comfortable relying on the standard vertex
pass-through.

The auto-generated vertex shader provides these outputs to your fragment stage:

```glsl
out vec4 v_Color;
out vec2 v_TexCoord;
```

**Path 3 — Shadertoy-style file**

The file contains no `#type` directives at all, but the source contains
`mainImage` or `iTime`. The preprocessor generates a full vertex shader, then
wraps your source in a fragment stage that adds:

```glsl
#define iTime       u_Time
#define iResolution vec3(u_ViewportSize, 1.0)

void main() {
    vec2 shadertoyFragCoord = v_TexCoord * u_ViewportSize;
    vec4 shadertoyFragColor;
    mainImage(shadertoyFragColor, shadertoyFragCoord);
    color = shadertoyFragColor * v_Color;
}
```

`iMouse` is **not** injected automatically. If your Shadertoy shader uses it,
declare `uniform vec4 iMouse = vec4(0.0);` yourself at the top of the file and
drive it from your layer if needed.

If the file has no `#type` tags and no Shadertoy signatures, the preprocessor
logs a critical error and returns an empty shader map. `Shader::Create` will
return `nullptr` and `Material::Create` will produce an inactive material.

---

### What the Preprocessor Injects and When

Three engine uniforms are candidates for auto-injection. Injection is evaluated
**per stage** and only triggers when a uniform is used in that stage's source
but not already declared in it.

| Uniform            | Type    | Trigger keywords                                                | Stage restriction |
| ------------------ | ------- | --------------------------------------------------------------- | ----------------- |
| `u_ViewProjection` | `mat4`  | `u_ViewProjection`                                              | Vertex stage only |
| `u_Time`           | `float` | `u_Time`, `iTime`, `TIME`, `_Time`                              | Any stage         |
| `u_ViewportSize`   | `vec2`  | `u_ViewportSize`, `iResolution`, `BUFFER_SIZE`, `_ScreenParams` | Any stage         |

The duplicate-detection scan works by finding the uniform name in the
comment-stripped source and scanning backwards to the start of that line looking
for the keyword `uniform`. If it finds `uniform` on the same line before any
newline, it considers the uniform already declared and skips injection.

---

### Comment Safety

The preprocessor strips `/* */` block comments and `//` line comments from a
working copy of the source before scanning for uniform names and declarations.
There are two important consequences:

1. A uniform name that appears **only inside a comment** will not trigger
   injection. If `u_Time` only appears in a comment, the preprocessor will not
   inject `uniform float u_Time;`.

2. A **commented-out declaration** does not count as a declaration. If you have
   `// uniform float u_Time;` in your source and `u_Time` is used in live code,
   the preprocessor will inject a live declaration because the commented-out
   version was stripped before the scan. Keep your declarations uncommented.

---

### How to Bypass the Preprocessor Entirely

Declare every uniform you use. The preprocessor's injection loop finds the
declaration, sees the `uniform` keyword on that line, and skips injection. Your
source is compiled verbatim after the `#type` split. There is nothing else
to do — **explicit declaration is the complete bypass**.

This also means you get a proper GLSL compile error if you misspell a uniform
name, rather than silent injection of the wrong declaration.

---

### The Guaranteed-Clean Boilerplate

Copy this template as the starting point for any new shader. It declares
everything explicitly, matches the `Renderer2D` vertex attribute layout, and
will compile identically regardless of what the preprocessor scanner decides.

```glsl
#type vertex
#version 450 core

// Renderer2D batch layout — do not reorder or rename these attributes.
// The VAO attribute pointers are fixed at engine init time.
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

// Declared explicitly — preprocessor will not inject a duplicate.
uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    v_Color     = a_Color;
    v_TexCoord  = a_TexCoord;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;

// Declare every engine uniform you use — preprocessor skips injection for these.
uniform float u_Time;
uniform vec2  u_ViewportSize;
uniform vec4  u_Color;

// Required if you sample textures routed through the Renderer2D batch system.
uniform sampler2D u_Textures[32];

void main()
{
    // Your logic here.
    color = u_Color * v_Color;
}
```

---

### Vertex Attribute Layout Contract

Any shader that runs geometry through `Renderer2D` must match this layout
exactly. The VAO attribute pointers are configured once at engine init and
never change. If your vertex shader uses different locations or types, vertex
data will be misread and rendering will be silently corrupted — there is no
runtime error.

| Location | Attribute Name   | Type    | Description                                             |
| -------- | ---------------- | ------- | ------------------------------------------------------- |
| 0        | `a_Position`     | `vec3`  | World-space position                                    |
| 1        | `a_Color`        | `vec4`  | Per-vertex tint color; carries `u_Color` for flat quads |
| 2        | `a_TexCoord`     | `vec2`  | UV coordinate in [0, 1]                                 |
| 3        | `a_TexIndex`     | `float` | Texture slot index 0–31; slot 0 is the white fallback   |
| 4        | `a_TilingFactor` | `float` | UV tiling multiplier passed through to fragment         |

If you write a custom vertex shader and only use some of these attributes, you
must still declare all five in the layout — the stride and offset math in the
VAO does not change based on which attributes your shader reads.

---

### Porting a Shadertoy Shader With Full Control

If you want to port a Shadertoy shader and control the `iTime` mapping yourself
rather than relying on the Path 3 auto-wrapper, use the full multi-stage format
with explicit declarations:

```glsl
#type fragment
#version 450 core

layout(location = 0) out vec4 color;

// Inputs from the vertex stage
in vec4 v_Color;
in vec2 v_TexCoord;

// Declare engine uniforms explicitly — no injection will occur
uniform float u_Time;
uniform vec2  u_ViewportSize;

// Map Shadertoy names yourself — the rest of the ported code stays unchanged
#define iTime       u_Time
#define iResolution vec3(u_ViewportSize, 1.0)

// Paste your mainImage here unchanged
void mainImage(out vec4 fragColor, in vec2 fragCoord) { /* ... */ }

void main()
{
    vec4 result;
    mainImage(result, v_TexCoord * u_ViewportSize);
    color = result * v_Color;
}
```

Because `u_Time` and `u_ViewportSize` are declared, the preprocessor will not
inject duplicates. Because you added `#type fragment`, the preprocessor will not
generate the Shadertoy auto-wrapper `main()`. You get exactly the source you wrote.

---

### Debugging Preprocessor Output

When a shader stage fails to compile, `OpenGLShader::DumpPreprocessedShader`
prints the complete post-injection source to the engine log with per-line
numbers. Check the `[COSMIC]` log output after a shader load failure — the dump
will show you the exact source that was submitted to the GLSL compiler, including
any injected preamble lines. This is the fastest way to diagnose unexpected
injection or malformed declarations.

---

### Quick Decision Guide

| Situation                                          | Path to use                                             |
| -------------------------------------------------- | ------------------------------------------------------- |
| New shader written from scratch                    | Path 1 — full multi-stage with boilerplate above        |
| Only need fragment math, fine with standard vertex | Path 2 — add `#type fragment`, let vertex auto-generate |
| Pasting a Shadertoy directly with no modification  | Path 3 — drop the file in, no `#type` tags needed       |
| Porting Shadertoy with custom `iTime`/`iMouse`     | Path 1 with manual `#define iTime u_Time`               |
| Shader produces garbled output after a paste       | Check the dump in the engine log                        |

---

---

## 11. Sprite Sheets and SubTexture2D

`SubTexture2D` extracts a UV-bounded tile from a larger texture atlas. It stores
four UV corner coordinates computed from grid cell indices and a cell pixel size.
The parent `Texture2D` is shared via `Ref<Texture2D>` — no pixel data is copied
or duplicated. Creating a new `SubTexture2D` is inexpensive and safe to call
every frame when frame-switching is needed.

---

### Loading the Atlas

Load the full sprite sheet once, typically in `OnAttach`:

```cpp
Ref<Cosmic::Texture2D> m_Atlas;

void MyLayer::OnAttach() override
{
    std::string path = Cosmic::FileSystem::Resolve(
        "project://sprites/DinoSprites - vita.png");

    m_Atlas = Cosmic::Texture2D::Create(path);

    if (!m_Atlas)
        CS_ERROR("Failed to load sprite atlas!");
}
```

---

### Creating a SubTexture from Grid Coordinates

```cpp
// static Ref<SubTexture2D> SubTexture2D::CreateFromCoords(
//     const Ref<Texture2D>& texture,
//     const glm::vec2& coords,              // grid column and row, zero-based
//     const glm::vec2& cellSize,            // pixel size of one tile
//     const glm::vec2& spriteSize = {1,1}   // optional: tile span in grid units
// );

// Single tile at column 0, row 0
Ref<Cosmic::SubTexture2D> idleFrame = Cosmic::SubTexture2D::CreateFromCoords(
    m_Atlas,
    { 0.0f, 0.0f },    // column 0, row 0
    { 24.0f, 24.0f }   // each cell is 24x24 pixels
);

// Single tile at column 4, row 0 — the start of the run cycle
Ref<Cosmic::SubTexture2D> runFrame0 = Cosmic::SubTexture2D::CreateFromCoords(
    m_Atlas,
    { 4.0f, 0.0f },
    { 24.0f, 24.0f }
);
```

`coords.x` is the column index and `coords.y` is the row index, both zero-based.
The UV rectangle is computed internally as:

```
min.x = (coords.x * cellSize.x) / textureWidth
min.y = (coords.y * cellSize.y) / textureHeight
max.x = ((coords.x + spriteSize.x) * cellSize.x) / textureWidth
max.y = ((coords.y + spriteSize.y) * cellSize.y) / textureHeight
```

The four UV corners are stored in counter-clockwise order (Bottom-Left →
Bottom-Right → Top-Right → Top-Left) to match OpenGL's coordinate system after
the stb_image vertical flip applied during texture load.

---

### Multi-Tile Spans

The optional `spriteSize` parameter lets a single `SubTexture2D` cover multiple
grid cells. Pass `{ 2.0f, 1.0f }` for a sprite that is two columns wide and one
row tall:

```cpp
// A sprite that spans 2 columns and 1 row
Ref<Cosmic::SubTexture2D> wideSprite = Cosmic::SubTexture2D::CreateFromCoords(
    m_Atlas,
    { 4.0f, 0.0f },    // top-left corner of the span
    { 24.0f, 24.0f },
    { 2.0f, 1.0f }     // 2 columns wide, 1 row tall
);

// A sprite that spans 1 column and 2 rows
Ref<Cosmic::SubTexture2D> tallSprite = Cosmic::SubTexture2D::CreateFromCoords(
    m_Atlas,
    { 0.0f, 1.0f },
    { 24.0f, 24.0f },
    { 1.0f, 2.0f }     // 1 column wide, 2 rows tall
);
```

The resulting quad will be drawn at whatever world-unit `size` you pass to
`DrawQuad` — `spriteSize` only affects which UV region is sampled, not the
on-screen size.

---

### Drawing a SubTexture

Both `DrawQuad` and `DrawRotatedQuad` accept `Ref<SubTexture2D>` directly. An
optional `vec4` tint color can be appended as the final argument.

```cpp
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

// Axis-aligned, no tint
Cosmic::Renderer2D::DrawQuad(
    { 0.0f, 0.0f, 0.0f },   // vec3 position
    { 1.5f, 1.5f },          // world-unit size
    idleFrame                 // Ref<SubTexture2D>
);

// Axis-aligned with a red tint
Cosmic::Renderer2D::DrawQuad(
    { 2.0f, 0.0f, 0.0f },
    { 1.5f, 1.5f },
    runFrame0,
    { 1.0f, 0.6f, 0.6f, 1.0f }   // RGBA tint
);

// Rotated 45 degrees
Cosmic::Renderer2D::DrawRotatedQuad(
    { -2.0f, 0.0f, 0.0f },
    { 1.5f, 1.5f },
    glm::radians(45.0f),     // rotation in radians
    idleFrame
);

// vec2 position overload — z is inserted as 0.0
Cosmic::Renderer2D::DrawQuad({ 0.0f, 1.0f }, { 1.0f, 1.0f }, idleFrame);

Cosmic::Renderer2D::EndScene();
```

SubTextures are batched using the same texture slot system as regular textures.
Up to 32 unique parent textures can be active in a single batch. If you load
multiple sprite sheets and draw from all of them in the same frame, each unique
atlas consumes one texture slot.

---

### Runtime Frame Switching

Store the current `SubTexture2D` as a member and call `CreateFromCoords` when
the frame changes. The call computes four floats — it is safe and inexpensive
to call every frame:

```cpp
class SpriteLayer : public Cosmic::Layer
{
public:
    void OnAttach() override
    {
        m_Atlas = Cosmic::Texture2D::Create(
            Cosmic::FileSystem::Resolve("project://sprites/DinoSprites - vita.png"));
        SetFrame(0);  // start on the first idle frame
    }

    void SetFrame(int col)
    {
        m_CurrentFrame = Cosmic::SubTexture2D::CreateFromCoords(
            m_Atlas,
            { (float)(m_AnimStartCol + col), 0.0f },
            { 24.0f, 24.0f }
        );
    }

    void OnUpdate(float ts) override
    {
        // Advance animation timer
        m_FrameTimer += ts;
        if (m_FrameTimer >= m_FrameDuration)
        {
            m_FrameTimer = 0.0f;
            m_CurrentCol = (m_CurrentCol + 1) % m_FrameCount;
            SetFrame(m_CurrentCol);
        }

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::DrawQuad(m_Position, { 1.5f, 1.5f }, m_CurrentFrame);
        Cosmic::Renderer2D::EndScene();
    }

private:
    Ref<Cosmic::Texture2D>    m_Atlas;
    Ref<Cosmic::SubTexture2D> m_CurrentFrame;

    float m_FrameTimer    = 0.0f;
    float m_FrameDuration = 0.1f;   // seconds per frame
    int   m_FrameCount    = 6;
    int   m_CurrentCol    = 0;
    int   m_AnimStartCol  = 4;      // column where the run cycle begins

    glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
    Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f };
};
```

---

### Flipping Sprites

Pass a negative X or Y scale component to flip the rendered image. The UV
coordinates themselves are not changed — the flip is applied by the vertex
transform.

```cpp
// Manual flip via negative scale
bool facingLeft = m_VelocityX < 0.0f;

glm::vec2 drawScale = {
    1.5f * (facingLeft ? -1.0f : 1.0f),
    1.5f
};

Cosmic::Renderer2D::DrawQuad(m_Position, drawScale, m_CurrentFrame);
```

When using `SpriteRendererComponent` with `Scene::OnRender`, set the component
flags and let the scene handle the negation automatically:

```cpp
auto& sprite = entity.GetComponent<Cosmic::SpriteRendererComponent>();
sprite.FlipX = facingLeft;
sprite.FlipY = false;
```

`Scene::OnRender` applies the flip by multiplying `transform.Scale.x` by `-1.0f`
and `transform.Scale.y` by `-1.0f` when building the draw scale before passing to
`Renderer2D::DrawRotatedQuad`. The `TransformComponent` itself is never modified.

---

### Dino Atlas Frame Reference

The Showcase project uses 24×24 pixel cells in a single horizontal row. These
column ranges apply to all four Dino variants (doux, mort, tard, vita):

| Columns | Animation           |
| ------- | ------------------- |
| 0 – 2   | Idle / blink cycle  |
| 4 – 9   | Run cycle           |
| 11 – 13 | Kick / hurt stance  |
| 14 – 16 | Shocked / eyes open |
| 18 – 23 | Sneak / crouch      |

Keep `coords.y = 0` for all frames — all animations are on row 0 in these
sheets.

---

---

## 12. SDF Circles

`Renderer2D::DrawCircle` renders a mathematically exact circle using a Signed
Distance Field evaluated per-pixel in the fragment shader (`Circle.glsl`). There
is no polygon approximation — the result is smooth at any zoom level and any
size without increasing the vertex count. Thickness, hollow rings, and
anti-aliased edges are all controlled via shader uniforms, not geometry.

Circles use their own vertex buffer and VAO but share the same index buffer
topology as quads. Up to 2,000 circles can be batched per flush call before
a `FlushAndReset` is triggered.

---

### Signature

```cpp
// vec3 position overload — standard form
static void DrawCircle(
    const glm::vec3& position,   // world-space center
    const glm::vec2& size,       // bounding quad size in world units (not radius)
    const glm::vec4& color,      // RGBA; channels > 1.0 are auto-normalized from 0–255
    float thickness = 1.0f,      // 1.0 = solid disk; < 1.0 = hollow ring
    float fade      = 0.005f     // anti-aliasing softness at both edges
);

// vec2 position overload — inserts z = 0.0 automatically
static void DrawCircle(
    const glm::vec2& position,
    const glm::vec2& size,
    const glm::vec4& color,
    float thickness = 1.0f,
    float fade      = 0.005f
);
```

`size` is the full extent of the bounding quad in world units. A circle at
`{ 0, 0, 0 }` with `size = { 2.0f, 2.0f }` has an outer radius of 1 world
unit. The SDF math uses the local quad coordinate space (`[-1, 1]` on each
axis), so the circle always fills its bounding quad regardless of size.

---

### Basic Usage

```cpp
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

// Solid filled disk
Cosmic::Renderer2D::DrawCircle(
    { 0.0f, 0.0f, 0.0f },
    { 2.0f, 2.0f },
    { 0.2f, 0.8f, 1.0f, 1.0f }
);

// Hollow ring — thin wall, sharp edges
Cosmic::Renderer2D::DrawCircle(
    { 3.0f, 0.0f, 0.0f },
    { 2.0f, 2.0f },
    { 1.0f, 0.5f, 0.0f, 0.9f },
    0.05f,   // thin wall
    0.005f   // sharp edge
);

// Soft glowing disk — large fade creates glow effect
Cosmic::Renderer2D::DrawCircle(
    { -3.0f, 0.0f, 0.0f },
    { 3.0f, 3.0f },
    { 1.0f, 0.3f, 0.8f, 0.6f },
    1.0f,
    0.25f
);

// vec2 overload — z inserted as 0
Cosmic::Renderer2D::DrawCircle({ 0.0f, 2.0f }, { 1.0f, 1.0f }, { 1.f, 1.f, 1.f, 1.f });

Cosmic::Renderer2D::EndScene();
```

---

### Thickness Reference

Thickness controls the inner cutout. The outer edge is always at the full extent
of `size`. With `thickness = 1.0` there is no inner cutout — the entire disk is
filled. With `thickness < 1.0`, an inner hole is cut whose inner radius is
`outerRadius * (1.0 - thickness)`.

| Thickness | Visual result                                  |
| --------- | ---------------------------------------------- |
| `1.0`     | Solid filled disk — no cutout                  |
| `0.5`     | Ring where the wall is 50% of the total radius |
| `0.1`     | Thin orbital ring with a large hollow center   |
| `0.05`    | Very thin procedural ring / path indicator     |
| `0.02`    | Near-pixel-width outline at typical game zoom  |

---

### Fade Reference

Fade controls the width of the anti-aliased transition zone at both the inner
and outer edges of the ring wall. It is applied via `smoothstep` in the fragment
shader and is independent of world-unit scale.

| Fade          | Visual result                                             |
| ------------- | --------------------------------------------------------- |
| `0.001–0.005` | Sharp, hard-edged ring — use for UI indicators            |
| `0.01–0.03`   | Standard smooth anti-aliased edge                         |
| `0.05–0.1`    | Soft visible transition — slight glow                     |
| `0.15–0.3`    | Heavy feathering — glow disk, radar pulse, area indicator |

---

### The Ellipse Trick

Because `size` is a `vec2`, passing unequal X and Y values produces an ellipse.
This is the standard technique for tracking rings drawn beneath 2D sprites, where
you want the ring to appear flat on the ground plane:

```cpp
glm::vec3 ringPos   = playerTransform.Position;
ringPos.y          -= 0.5f;   // shift below the sprite's feet
ringPos.z          += 0.05f;  // render in front of the ground layer

Cosmic::Renderer2D::DrawCircle(
    ringPos,
    { 1.2f, 0.4f },              // wide on X, flat on Y — looks ground-projected
    { 0.0f, 0.95f, 0.85f, 0.85f },
    0.08f,
    0.01f
);
```

---

### Pulsing Animation

Drive `size` from `GetLocalTime()` to produce animated radar-pulse or heartbeat
effects:

```cpp
float t     = GetLocalTime();
float pulse = 1.0f + std::sin(t * 4.0f) * 0.08f;

Cosmic::Renderer2D::DrawCircle(
    { 0.0f, 0.0f, -0.1f },
    { 8.0f * pulse, 8.0f * pulse },
    { 0.0f, 0.6f, 0.9f, 0.25f },
    0.02f,
    0.005f
);
```

---

### Layered Radar Zone Pattern

The `ShowcaseDinoLayer` uses two concentric circles for a radar-zone background.
The z-offset difference prevents z-fighting between the two layers:

```cpp
// Soft filled zone — large fade, low alpha
Cosmic::Renderer2D::DrawCircle(
    { 0.0f, 0.0f, -0.15f },
    glm::vec2(8.0f + radarPulse),
    { 0.15f, 0.3f, 0.4f, 0.08f },
    1.0f,
    0.15f
);

// Crisp border ring — thin wall, near-zero fade, higher alpha
Cosmic::Renderer2D::DrawCircle(
    { 0.0f, 0.0f, -0.14f },   // slightly in front
    glm::vec2(8.0f),
    { 0.0f, 0.6f, 0.9f, 0.25f },
    0.02f,
    0.005f
);
```

---

### Automatic Color Normalization

If any channel of the `color` argument exceeds `1.0f`, `DrawCircle` divides all
four channels by `255.0f` before writing to the vertex buffer. This lets you pass
byte-range colors directly:

```cpp
// Both of these produce the same output
Cosmic::Renderer2D::DrawCircle(pos, size, { 0.0f, 0.95f, 0.85f, 0.85f });
Cosmic::Renderer2D::DrawCircle(pos, size, { 0.0f, 242.25f, 216.75f, 216.75f });
```

This normalization is applied inside `DrawCircle` only — it does not affect any
other draw call.

---

---

## 13. RenderPass and Multi-Camera Rendering

### The Problem

`Renderer2D` maintains a single internal View-Projection matrix stored in
`s_Data.ViewProjectionMatrix`. Every `BeginScene(camera)` call installs the
camera's VP matrix and resets the batch counters. If you call
`BeginScene(cameraA)`, submit geometry, then call `BeginScene(cameraB)`, the
second `BeginScene` triggers a flush of the pending geometry — but at that point
the VP matrix is already being replaced by camera B. The flush uploads camera B's
matrix to the shader, and the geometry you staged under camera A is drawn with
camera B's transform.

The result is off-screen or distorted geometry with no compile error and no
runtime warning. The same bug appears if two layers both call `BeginScene` in the
same frame without perfectly synchronized `EndScene` calls between them.

---

### The Solution: RenderPass RAII

`RenderPass` is a scoped RAII wrapper over `Renderer2D::PushRenderPass` and
`Renderer2D::PopRenderPass`. Constructing a `RenderPass` starts a camera context;
letting it go out of scope ends it cleanly.

```cpp
#include "renderer/RenderPass.h"

{
    Cosmic::RenderPass pass(camera.GetCamera(), viewportBounds);
    // All draw calls here use camera's VP matrix
} // <- destructor fires: flushes geometry, restores prior VP matrix and viewport
```

**On construction `PushRenderPass` does the following in order:**

1. Checks for pending geometry in the current batch (quads, lines, circles). If
   any exist, calls `Flush()` to submit them to the GPU before changing state.
2. Pushes a `RenderPassState` struct onto `s_Data.RenderPassStack` containing
   the new VP matrix and viewport bounds.
3. Installs the new VP matrix into `s_Data.ViewProjectionMatrix`.
4. Calls `glViewport` with the provided bounds.
5. Updates `s_Data.ViewportDimensions` so `u_ViewportSize` is correct for this
   pass.
6. Resets all batch counters (quad index count, line vertex count, circle index
   count, texture slot index, current material).

**On destruction `PopRenderPass` does the following in order:**

1. Checks for remaining pending geometry and flushes if any exist.
2. Pops the top entry from `s_Data.RenderPassStack`.
3. If the stack is non-empty, restores the prior pass's VP matrix and calls
   `glViewport` with the prior bounds.
4. Resets batch counters for whatever draw calls come next.

`RenderPass` is non-copyable and non-movable. Each instance must be owned by
exactly one scope.

---

### Viewport Bounds

The `glm::vec4` viewport parameter is `{ x_offset, y_offset, width, height }` in
pixels measured from the **bottom-left** corner of the framebuffer. This is the
standard OpenGL convention — `y = 0` is the bottom of the screen, not the top.

For a 1280×720 framebuffer split into four equal quadrants:

```
Top-left:     { 0,    360,  640, 360 }
Top-right:    { 640,  360,  640, 360 }
Bottom-left:  { 0,    0,    640, 360 }
Bottom-right: { 640,  0,    640, 360 }
```

---

### Minimal Two-Camera Example

```cpp
void MyLayer::OnUpdate(float ts)
{
    m_CamMain.OnUpdate(ts);
    m_CamMinimap.OnUpdate(ts);

    auto fb = Cosmic::Application::Get().GetFrameBuffer();
    float w = static_cast<float>(fb->GetWidth());
    float h = static_cast<float>(fb->GetHeight());

    // Main view covers the entire framebuffer
    {
        Cosmic::RenderPass mainPass(m_CamMain.GetCamera(), { 0.f, 0.f, w, h });
        DrawWorld();
    }

    // Minimap — top-right corner at 25% of full size
    {
        float mw = w * 0.25f;
        float mh = h * 0.25f;
        Cosmic::RenderPass minimapPass(
            m_CamMinimap.GetCamera(),
            { w - mw, h - mh, mw, mh }
        );
        DrawWorld();   // same content, different camera
    }
}
```

Each block draws the same scene through a completely independent camera. No
geometry leaks between passes.

---

### Full Four-Camera Split-Screen

This pattern is taken directly from `ShowcaseMultiViewportLayer` and shows the
complete production structure for split-screen multi-camera rendering.

```cpp
// MyMultiCamLayer.h
#pragma once
#include <Cosmic.h>

class MyMultiCamLayer : public Cosmic::Layer
{
public:
    MyMultiCamLayer();

    void OnAttach()  override;
    void OnDetach()  override;
    void OnUpdate(float ts) override;
    void OnFixedUpdate(float dt) override;
    void OnImGuiRender() override;
    void OnEvent(Cosmic::Event& e) override;

private:
    bool OnWindowResize(Cosmic::WindowResizeEvent& e);

    void DrawGrid(const glm::vec4& color, float spacing, float extent);
    void DrawEntities(bool wireframe, const glm::vec4& tint);
    void DrawOrbitPaths();
    void DrawVelocityVectors();

    // Four independent cameras — each produces a different view of the same scene
    Cosmic::OrthographicCameraController m_CamTL;   // Top-left:     close-up follow
    Cosmic::OrthographicCameraController m_CamTR;   // Top-right:    overhead overview
    Cosmic::OrthographicCameraController m_CamBL;   // Bottom-left:  tinted mirror
    Cosmic::OrthographicCameraController m_CamBR;   // Bottom-right: debug wireframe

    glm::vec2 m_ViewportSize = { 1280.f, 720.f };
    float     m_CamMainAngle = 0.0f;    // for auto-pan on TL camera
    bool      m_AnimateCam   = true;
};
```

```cpp
// MyMultiCamLayer.cpp
#include "MyMultiCamLayer.h"
#include <imgui.h>
#include <cmath>

MyMultiCamLayer::MyMultiCamLayer()
    : Cosmic::Layer("MyMultiCamLayer")
    , m_CamTL(1280.f / 720.f, false)
    , m_CamTR(1280.f / 720.f, false)
    , m_CamBL(1280.f / 720.f, false)
    , m_CamBR(1280.f / 720.f, false)
{
}

void MyMultiCamLayer::OnAttach()
{
    // Configure each camera's zoom and behavior independently
    m_CamTL.SetZoomLevel(2.5f);
    m_CamTL.SetZoomLimits(0.5f, 20.0f);
    m_CamTL.SetManualMovementEnabled(false); // driven from code, not WASD

    m_CamTR.SetZoomLevel(8.0f);
    m_CamTR.SetZoomLimits(2.0f, 30.0f);
    m_CamTR.SetManualMovementEnabled(false);

    m_CamBL.SetZoomLevel(3.0f);
    m_CamBL.SetZoomLimits(0.5f, 20.0f);
    m_CamBL.SetManualMovementEnabled(false);

    m_CamBR.SetZoomLevel(5.0f);
    m_CamBR.SetZoomLimits(1.0f, 20.0f);
    m_CamBR.SetManualMovementEnabled(false);
}

void MyMultiCamLayer::OnDetach()
{
    // Release scene/entity resources here
}

void MyMultiCamLayer::OnUpdate(float ts)
{
    // -----------------------------------------------------------------------
    // Step 1: Sync viewport size from the active framebuffer.
    // Always read from the framebuffer — never cache window size directly,
    // as the ImGui viewport panel may be smaller than the OS window.
    // -----------------------------------------------------------------------
    auto fb = Cosmic::Application::Get().GetFrameBuffer();
    float w = static_cast<float>(fb->GetWidth());
    float h = static_cast<float>(fb->GetHeight());

    if (m_ViewportSize.x != w || m_ViewportSize.y != h)
    {
        m_ViewportSize = { w, h };
        float hw = w * 0.5f;
        float hh = h * 0.5f;

        // CRITICAL: resize each camera to QUADRANT dimensions, not full framebuffer.
        // The orthographic projection is computed from the aspect ratio you pass here.
        // Passing full-window dimensions to a half-window camera will stretch the image.
        m_CamTL.OnResize(hw, hh);
        m_CamTR.OnResize(hw, hh);
        m_CamBL.OnResize(hw, hh);
        m_CamBR.OnResize(hw, hh);
    }

    // -----------------------------------------------------------------------
    // Step 2: Drive camera positions BEFORE the render passes.
    // Position updates must happen before constructing any RenderPass —
    // the VP matrix is captured at construction time.
    // -----------------------------------------------------------------------
    if (m_AnimateCam)
    {
        m_CamMainAngle += ts * 0.12f;
        float cx = std::cos(m_CamMainAngle) * 1.5f;
        float cy = std::sin(m_CamMainAngle) * 0.6f;
        m_CamTL.SetPosition({ cx, cy, 0.0f });

        float lagAngle = m_CamMainAngle - 0.4f;
        m_CamBL.SetPosition({ std::cos(lagAngle) * 1.5f, std::sin(lagAngle) * 0.6f, 0.0f });
    }

    m_CamTR.SetPosition({ 0.0f, 0.0f, 0.0f });
    m_CamBR.SetPosition({ 0.0f, 0.0f, 0.0f });

    m_CamTL.OnUpdate(ts);
    m_CamTR.OnUpdate(ts);
    m_CamBL.OnUpdate(ts);
    m_CamBR.OnUpdate(ts);

    // -----------------------------------------------------------------------
    // Step 3: Compute quadrant bounds.
    // OpenGL y=0 is the BOTTOM of the framebuffer.
    //
    //   TOP-LEFT:     x=0,    y=hh,  w=hw, h=hh
    //   TOP-RIGHT:    x=hw,   y=hh,  w=hw, h=hh
    //   BOTTOM-LEFT:  x=0,    y=0,   w=hw, h=hh
    //   BOTTOM-RIGHT: x=hw,   y=0,   w=hw, h=hh
    // -----------------------------------------------------------------------
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    Cosmic::Renderer2D::ResetStats();

    // -----------------------------------------------------------------------
    // TOP-LEFT — main close-up follow camera
    // -----------------------------------------------------------------------
    {
        Cosmic::RenderPass pass(m_CamTL.GetCamera(), { 0.f, hh, hw, hh });

        DrawGrid({ 0.12f, 0.12f, 0.15f, 1.0f }, 1.0f, 12.0f);
        DrawEntities(false, { 1.f, 1.f, 1.f, 1.f });
    }

    // -----------------------------------------------------------------------
    // TOP-RIGHT — overhead overview with orbit path rings
    // -----------------------------------------------------------------------
    {
        Cosmic::RenderPass pass(m_CamTR.GetCamera(), { hw, hh, hw, hh });

        DrawGrid({ 0.1f, 0.1f, 0.12f, 1.0f }, 2.0f, 20.0f);
        DrawEntities(false, { 1.f, 1.f, 1.f, 1.f });
        DrawOrbitPaths();   // extra overlay rendered only in this pass
    }

    // -----------------------------------------------------------------------
    // BOTTOM-LEFT — slow-motion tinted mirror of the main camera
    // -----------------------------------------------------------------------
    {
        Cosmic::RenderPass pass(m_CamBL.GetCamera(), { 0.f, 0.f, hw, hh });

        // Full-coverage background quad gives this pass a cold blue tint
        Cosmic::Renderer2D::DrawQuad(
            { 0.f, 0.f, -0.5f }, { 100.f, 100.f },
            { 0.04f, 0.06f, 0.14f, 1.0f }
        );
        DrawGrid({ 0.1f, 0.15f, 0.3f, 1.0f }, 1.0f, 12.0f);
        DrawEntities(false, { 0.5f, 0.7f, 1.0f, 1.0f }); // blue tint on all geometry
    }

    // -----------------------------------------------------------------------
    // BOTTOM-RIGHT — debug wireframe with velocity vectors
    // -----------------------------------------------------------------------
    {
        Cosmic::RenderPass pass(m_CamBR.GetCamera(), { hw, 0.f, hw, hh });

        DrawGrid({ 0.08f, 0.08f, 0.08f, 1.0f }, 1.0f, 20.0f);
        DrawEntities(true, { 1.f, 1.f, 1.f, 1.f });  // wireframe=true
        DrawVelocityVectors();
    }
}

void MyMultiCamLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);
    dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
        [this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
}

bool MyMultiCamLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    float w  = static_cast<float>(e.GetWidth());
    float h  = static_cast<float>(e.GetHeight());
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    m_ViewportSize = { w, h };

    // Resize ALL cameras, even ones not currently active.
    // A camera that misses a resize event will produce a stretched projection
    // the next time it is used.
    m_CamTL.OnResize(hw, hh);
    m_CamTR.OnResize(hw, hh);
    m_CamBL.OnResize(hw, hh);
    m_CamBR.OnResize(hw, hh);

    return false; // do not consume — other systems also need the resize event
}

void MyMultiCamLayer::OnImGuiRender()
{
    ImGui::Begin("Multi-Camera Inspector");

    auto stats = Cosmic::Renderer2D::GetStats();
    ImGui::Text("Draw Calls this frame: %u", stats.DrawCalls);
    ImGui::Text("Total Quads:           %u", stats.QuadCount);

    ImGui::Separator();
    ImGui::Checkbox("Animate Main Camera", &m_AnimateCam);

    if (ImGui::CollapsingHeader("Camera Zoom Controls"))
    {
        float z;
        z = m_CamTL.GetZoomLevel();
        if (ImGui::SliderFloat("TL Zoom", &z, 0.5f, 10.f)) m_CamTL.SetZoomLevel(z);

        z = m_CamTR.GetZoomLevel();
        if (ImGui::SliderFloat("TR Zoom", &z, 2.0f, 30.f)) m_CamTR.SetZoomLevel(z);

        z = m_CamBL.GetZoomLevel();
        if (ImGui::SliderFloat("BL Zoom", &z, 0.5f, 10.f)) m_CamBL.SetZoomLevel(z);

        z = m_CamBR.GetZoomLevel();
        if (ImGui::SliderFloat("BR Zoom", &z, 1.0f, 20.f)) m_CamBR.SetZoomLevel(z);
    }

    ImGui::End();
}
```

The structural points to take from this pattern:

- Read viewport dimensions from the **framebuffer**, not from the window or
  ImGui, so the size is always the actual render target size.
- Resize cameras to **quadrant dimensions** (`hw × hh`), not full framebuffer.
- Drive **all camera positions before** the first `RenderPass` block. The VP
  matrix is captured at construction time.
- Each pass is a self-contained `{}` block. Draw calls inside block N cannot
  land in block N+1.
- Resize **all** cameras in `OnWindowResize`, even those not currently rendered.
- `ResetStats()` is called once before all passes so telemetry covers the whole
  frame.

---

### RenderPass With a Separate Framebuffer

`RenderPass` controls only the VP matrix and the `glViewport` call. Framebuffer
binding is always your responsibility:

```cpp
// Bind the off-screen target before constructing the RenderPass
m_SideFramebuffer->Bind();
Cosmic::RenderCommand::SetClearColor({ 0.05f, 0.05f, 0.08f, 1.0f });
Cosmic::RenderCommand::Clear();

{
    Cosmic::RenderPass sidePass(
        m_SideCam.GetCamera(),
        { 0.f, 0.f, (float)sideW, (float)sideH }
    );
    DrawSideContent();
}

m_SideFramebuffer->Unbind();

// Use the result as a texture in the main pass or in ImGui
uint32_t texID = m_SideFramebuffer->GetColorAttachmentRendererID();
ImGui::Image((void*)(uintptr_t)texID, { (float)sideW, (float)sideH }, { 0, 1 }, { 1, 0 });
```

---

### Lower-Level API

If you need manual control without the RAII wrapper:

```cpp
Cosmic::Renderer2D::PushRenderPass(
    camera.GetViewProjectionMatrix(),
    { 0.f, 0.f, w, h }
);

// draw calls...

Cosmic::Renderer2D::PopRenderPass();
```

These are the exact calls `RenderPass` makes internally. Prefer the RAII wrapper
in production code — a mismatched push/pop pair triggers `CS_CORE_ASSERT` in
debug builds and produces undefined batch state in release builds.

---

### Backward Compatibility

`BeginScene` and `EndScene` are preserved as shims and work correctly for
single-camera rendering. `BeginScene(camera)` derives a full-window viewport
from `s_Data.ViewportDimensions` and pushes a pass covering the entire
framebuffer. Existing code that uses `BeginScene`/`EndScene` requires no changes
unless you are adding a second camera:

```cpp
// Old form — still fully correct for single-camera rendering
Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
DrawWorld();
Cosmic::Renderer2D::EndScene();

// Equivalent new form — use this when you need sub-viewport targeting
{
    Cosmic::RenderPass pass(m_Camera.GetCamera(), { 0.f, 0.f, w, h });
    DrawWorld();
}
```

---

### Rules

**Do not nest two `RenderPass` instances targeting the same viewport and
framebuffer.** They will overwrite each other's `glViewport` state and produce
incorrect rendering. Each pass must target a unique pixel region or a different
framebuffer.

**Resize every camera to its target viewport quadrant dimensions.** The
orthographic projection is built from the aspect ratio you pass to `OnResize`.
Passing incorrect dimensions silently stretches all geometry in that camera's
output.

**Always resize all cameras on window resize, including inactive ones.** A camera
that misses a resize event retains a stale aspect ratio. The first frame it is
used again it will produce stretched or distorted geometry with no warning. Resize
all cameras in your `OnWindowResize` handler unconditionally.

**Drive camera positions and `OnUpdate` calls before constructing any
`RenderPass`.** The VP matrix is snapshotted at `RenderPass` construction time.
Calling `SetPosition` or `OnUpdate` after construction changes the camera's
internal state but does not update the VP matrix that was already pushed to the
stack.

**`RenderPass` is not thread-safe.** All draw calls and pass construction must
happen on the same thread that called `Renderer2D::Init`.

---

---

## 14. Entity Component System

Cosmic uses [EnTT](https://github.com/skypjack/entt) for its ECS. Entities are
lightweight handles wrapping an `entt::entity` integer and a pointer to their
owning `Scene`. Components are plain structs with no required base class. The
`Scene` owns the registry and is the factory for creating entities.

---

### Creating Entities

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// CreateEntity always auto-adds TransformComponent and TagComponent
Cosmic::Entity player = m_Scene->CreateEntity("Player");
Cosmic::Entity enemy  = m_Scene->CreateEntity("Enemy");
Cosmic::Entity bullet = m_Scene->CreateEntity(); // name defaults to "GenericEntity"
```

---

### Adding and Reading Components

```cpp
// AddComponent constructs in-place — asserts if the component already exists
auto& sprite = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);
auto& body   = player.AddComponent<MyRigidBodyComponent>(1.0f, 0.3f);

// GetComponent returns a mutable reference — asserts if the component is absent
auto& transform = player.GetComponent<Cosmic::TransformComponent>();
transform.Position   = { 2.0f, 0.5f, 0.0f };
transform.Rotation.z = 45.0f;   // degrees on the Z axis
transform.Scale      = { 1.0f, 1.0f };

// Check before access when existence is uncertain
if (player.HasComponent<MyRigidBodyComponent>())
{
    auto& rb = player.GetComponent<MyRigidBodyComponent>();
    rb.Velocity = { 3.0f, 0.0f };
}

// Remove a component
player.RemoveComponent<MyRigidBodyComponent>();

// Entity handle boolean — true when valid and scene-bound
if (player) { /* handle is valid */ }
```

---

### Built-in Components

**`TransformComponent`**

```cpp
struct TransformComponent {
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll in degrees
    glm::vec2 Scale    { 1.f, 1.f };
    glm::mat4 GetTransform() const;        // full TRS matrix
};
```

**`SpriteRendererComponent`**

```cpp
struct SpriteRendererComponent {
    Ref<Material> ActiveMaterial;
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f }; // flat-color fallback
    bool FlipX = false;
    bool FlipY = false;
};
```

**`TagComponent`**

```cpp
struct TagComponent { std::string Tag; };
```

---

### Scene Update and Render

```cpp
void MyLayer::OnUpdate(float ts) override
{
    // Ticks all registered systems in registration order
    m_Scene->OnUpdate(ts);

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

    // Groups entities by material bucket, then dispatches to Renderer2D.
    // All entities sharing the same ActiveMaterial are drawn in a single batch.
    m_Scene->OnRender();

    Cosmic::Renderer2D::EndScene();
}

void MyLayer::OnFixedUpdate(float dt) override
{
    m_Scene->OnFixedUpdate(dt);
}
```

`Scene::OnRender` sorts entities into material buckets using a
`std::unordered_map<Material*, std::vector<entt::entity>>` keyed on the raw
pointer. All entities in one bucket are drawn before any entity in the next
bucket, minimizing `FlushAndReset` calls caused by material state changes.
Entities with no `ActiveMaterial` fall back to flat-color rendering using
`SpriteRendererComponent::Color`.

---

### Writing ECS Systems

`System` is an abstract base class for broad update logic that spans many
entities. Systems are owned by the scene and dispatched automatically through
`Scene::OnUpdate` and `Scene::OnFixedUpdate`.

```cpp
// Declare the system
class GravitySystem : public Cosmic::System
{
public:
    void OnFixedUpdate(Cosmic::Scene& scene, float dt) override
    {
        // View queries only entities that have BOTH of these components
        auto view = scene.View<Cosmic::TransformComponent, MyRigidBodyComponent>();
        view.each([dt](auto& transform, auto& body)
        {
            if (!body.IsGrounded)
            {
                body.VelocityY       += -9.8f * dt;
                transform.Position.y += body.VelocityY * dt;
            }
        });
    }
};

class RotationSystem : public Cosmic::System
{
public:
    void OnUpdate(Cosmic::Scene& scene, float dt) override
    {
        auto view = scene.View<Cosmic::TransformComponent, MySpinComponent>();
        view.each([dt](auto& transform, auto& spin)
        {
            transform.Rotation.z += spin.DegreesPerSecond * dt;
        });
    }
};
```

```cpp
// Register in OnAttach — systems run in registration order
void MyLayer::OnAttach() override
{
    m_Scene = Cosmic::Scene::Create();
    m_Scene->AddSystem<GravitySystem>();
    m_Scene->AddSystem<RotationSystem>();
}
```

Systems receive a `Scene&` and use `scene.View<...>()` to query the registry.
They have no access to layer state by default — pass data through the system
constructor if needed. Use systems for logic that is generic across entity types.
Use layer `OnUpdate` for logic that is specific to a layer's own state machine
(game-over detection, input mapping, camera follow).

---

### DLL-Safe Component Registration

EnTT assigns component type IDs using sequential static counters that start
from zero independently in each compiled binary. When your game compiles as a
separate `.dll`, a component type registered as ID 3 in `Cosmic.dll` may be
registered as ID 1 in `MyProject.dll`. Any `AddComponent<T>` or
`GetComponent<T>` call that crosses the DLL boundary will then operate on the
wrong component pool. The failure is completely silent — no exception, no log
message, just corrupted or missing component data.

**The fix is `CS_REGISTER_COMPONENT`:**

```cpp
// In your component header file — must be included by both the engine and the DLL
CS_REGISTER_COMPONENT(MyNamespace::MyComponent)
```

This macro specializes `entt::type_hash<T>` to return a compile-time
`hashed_string` of the fully-qualified type name instead of the sequential
counter. The hash value is identical in every binary that compiles that header,
regardless of link order or how many other components were registered first.

The macro expands to:

```cpp
template<> struct entt::type_hash<MyNamespace::MyComponent> final {
    [[nodiscard]] static consteval entt::id_type value() noexcept {
        return entt::hashed_string::value("MyNamespace::MyComponent");
    }
};
```

**Rules for using `CS_REGISTER_COMPONENT`:**

- Register every component type that is used across the engine/plugin DLL
  boundary. If a component is only created and queried inside your DLL and
  never touched by engine code or another DLL, registration is optional but
  still recommended for safety.

- Always use the **fully-qualified name with namespace** as the string key.
  `CS_REGISTER_COMPONENT(Showcase::RunnerFlameComponent)` is correct.
  `CS_REGISTER_COMPONENT(RunnerFlameComponent)` is wrong — the string would
  not match the engine-side name and ID collision could still occur.

- Place the macro in the component header file at file scope, outside any
  class or function body. The header must be compiled by both the engine
  translation units and the plugin translation units.

- The built-in engine components (`TagComponent`, `TransformComponent`,
  `SpriteRendererComponent`) are already registered in `Components.h`. Do
  not register them again.

- Missing registration does **not** produce a compile error. The only symptom
  is silent runtime data corruption.

```cpp
// ShowcaseRunLayer.h — register all components defined in this header
struct RunnerFlameComponent { float Score = 0.f; /* ... */ };
struct ObstacleComponent    { float Speed = 3.5f; /* ... */ };

CS_REGISTER_COMPONENT(Showcase::RunnerFlameComponent)
CS_REGISTER_COMPONENT(Showcase::ObstacleComponent)
```

```cpp
// ShowcaseFlightLayer.h
struct FlightFlameComponent { float Speed = 1.f; /* ... */ };

CS_REGISTER_COMPONENT(Showcase::FlightFlameComponent)
```

---

### Destroying Entities

```cpp
m_Scene->DestroyEntity(entity);
// The handle is now invalid — do not call GetComponent on it afterward.
```

Destroying entities while iterating a view is supported by EnTT but should be
deferred where possible. The Showcase `ShowcaseRunLayer` uses `std::remove_if`
followed by a separate erase pass to avoid invalidating iterators in multi-view
scenarios:

```cpp
m_Obstacles.erase(
    std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
        [this](Cosmic::Entity ent) mutable
        {
            if (ent.GetComponent<Cosmic::TransformComponent>().Position.x < -4.0f)
            {
                m_Scene->DestroyEntity(ent);
                return true;
            }
            return false;
        }),
    m_Obstacles.end()
);
```

---

---

## 15. Camera System

### Camera Controller (Recommended)

`OrthographicCameraController` is the standard interface for all 2D camera work.
It wraps an `OrthographicCamera` and handles keyboard panning, smooth
scroll-wheel zoom interpolation, aspect ratio management on resize, and event
routing. Construct one in your layer and call `OnUpdate`, `OnEvent`, and
`OnResize` in the matching layer hooks.

```cpp
class MyLayer : public Cosmic::Layer
{
    // 1280/720 aspect ratio, rotation disabled
    Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f };

    void OnUpdate(float ts) override
    {
        m_Camera.OnUpdate(ts);  // polls input, advances zoom interpolation

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        // draw calls
        Cosmic::Renderer2D::EndScene();
    }

    void OnEvent(Cosmic::Event& e) override
    {
        m_Camera.OnEvent(e); // handles MouseScrolledEvent and WindowResizeEvent
    }
};
```

Zoom interpolation uses asymptotic blending with a smoothness factor of 10.
`OnMouseScrolled` updates `m_TargetZoomLevel`; each `OnUpdate` call moves
`m_ZoomLevel` toward the target by `clamp(10.0f * ts, 0.0f, 1.0f)`. This
produces a smooth ease-out feel without overshooting.

---

### Constructor

```cpp
OrthographicCameraController(float aspectRatio, bool rotation = false);
```

Pass `rotation = true` to enable Q/E key rotation. With `rotation = false`, the
camera cannot be rotated via keyboard but can still be positioned programmatically
using `SetPosition`.

---

### Viewport Resize

Always forward `WindowResizeEvent` to the camera so the orthographic projection
updates when the window changes size. Return `false` to allow the event to
propagate to other systems:

```cpp
bool MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    m_Camera.OnResize((float)e.GetWidth(), (float)e.GetHeight());
    return false;
}
```

When using multiple cameras with `RenderPass`, resize each camera to its
**quadrant dimensions**, not the full window dimensions:

```cpp
bool MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    float hw = (float)e.GetWidth()  * 0.5f;
    float hh = (float)e.GetHeight() * 0.5f;
    m_CamTL.OnResize(hw, hh);
    m_CamTR.OnResize(hw, hh);
    m_CamBL.OnResize(hw, hh);
    m_CamBR.OnResize(hw, hh);
    return false;
}
```

---

### Full API Reference

| Function                       | Parameters                                       | Description                                                                                                                                       |
| ------------------------------ | ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `OrthographicCameraController` | `float aspectRatio, bool rotation = false`       | Constructor. Initializes camera with the given aspect ratio and optional Z-rotation support.                                                      |
| `OnUpdate`                     | `float ts`                                       | Polls WASD/arrow input, advances smooth zoom interpolation, updates the underlying camera transform. Must be called once per frame.               |
| `OnEvent`                      | `Event&`                                         | Routes `MouseScrolledEvent` to the zoom system and `WindowResizeEvent` to `OnResize`.                                                             |
| `OnResize`                     | `float width, float height`                      | Recalculates aspect ratio and orthographic projection. Call whenever the render target changes size.                                              |
| `GetCamera`                    | —                                                | Returns `OrthographicCamera&` for passing to `BeginScene` or `RenderPass`.                                                                        |
| `GetZoomLevel`                 | —                                                | Returns the current interpolated zoom scalar.                                                                                                     |
| `SetZoomLevel`                 | `float level`                                    | Hard-snap to a zoom level, bypassing interpolation. Sets both the current and target zoom immediately.                                            |
| `SetZoomLimits`                | `float min, float max`                           | Clamps the scroll-wheel zoom range. Default: 0.25 – 10.0.                                                                                         |
| `SetZoomSpeed`                 | `float speed`                                    | World units zoomed per scroll tick. Default: 0.25.                                                                                                |
| `SetTranslationSpeed`          | `float speed`                                    | Pan speed in world units per second. Scaled internally by the current zoom level so panning feels consistent at all zoom levels. Default: 5.0.    |
| `GetTranslationSpeed`          | —                                                | Returns the current translation speed setting.                                                                                                    |
| `SetRotationSpeed`             | `float speed`                                    | Degrees per second for Q/E rotation. Only active when `rotation = true` was passed to the constructor. Default: 180.0.                            |
| `GetRotationSpeed`             | —                                                | Returns the current rotation speed setting.                                                                                                       |
| `SetPositionLimits`            | `float minX, float maxX, float minY, float maxY` | Hard-clamps the camera pan bounds in world space. Default: ±1000 on both axes.                                                                    |
| `SetPosition`                  | `const glm::vec3& position`                      | Directly sets the camera world position, bypassing keyboard input. Also updates the underlying `OrthographicCamera` immediately.                  |
| `GetPosition`                  | —                                                | Returns the current camera world position as `const glm::vec3&`.                                                                                  |
| `SetManualMovementEnabled`     | `bool enabled`                                   | Enable or disable WASD/arrow key panning. Disable when driving the camera from code (entity follow, cutscene track, scripted pan). Default: true. |
| `IsManualMovementEnabled`      | —                                                | Returns true if keyboard panning is currently active.                                                                                             |
| `SetKeyBindings`               | `const CameraKeyBindings& bindings`              | Replace the default WASD+QE key mapping with a custom layout.                                                                                     |
| `GetKeyBindings`               | —                                                | Returns a mutable reference to the active key bindings for per-key adjustment.                                                                    |

---

### CameraKeyBindings

`CameraKeyBindings` is a struct of key codes for all six camera input directions.
Set any field to `0` to disable that direction:

```cpp
struct CameraKeyBindings {
    uint32_t MoveLeft  = CS_KEY_A;  // 65
    uint32_t MoveRight = CS_KEY_D;  // 68
    uint32_t MoveUp    = CS_KEY_W;  // 87
    uint32_t MoveDown  = CS_KEY_S;  // 83
    uint32_t RotateQ   = CS_KEY_Q;  // 81
    uint32_t RotateE   = CS_KEY_E;  // 69
};
```

---

### Custom Key Bindings Example

Remap the camera to arrow keys and disable rotation:

```cpp
void MyLayer::OnAttach() override
{
    Cosmic::OrthographicCameraController::CameraKeyBindings bindings;
    bindings.MoveLeft  = CS_KEY_LEFT;
    bindings.MoveRight = CS_KEY_RIGHT;
    bindings.MoveUp    = CS_KEY_UP;
    bindings.MoveDown  = CS_KEY_DOWN;
    bindings.RotateQ   = 0;  // 0 = disabled
    bindings.RotateE   = 0;
    m_Camera.SetKeyBindings(bindings);
}
```

---

### Entity Follow Camera

When you want the camera to follow an entity, disable manual movement and set
the position from your entity's transform each frame. The zoom interpolation
and event handling in `OnUpdate` and `OnEvent` continue to function normally:

```cpp
void MyLayer::OnUpdate(float ts) override
{
    // Disable keyboard panning — we drive position from code
    m_Camera.SetManualMovementEnabled(false);

    if (m_Player)
    {
        auto& transform = m_Player.GetComponent<Cosmic::TransformComponent>();

        // Lock the camera directly onto the player position
        m_Camera.SetPosition({ transform.Position.x, transform.Position.y, 0.0f });
    }

    // OnUpdate still runs zoom interpolation and applies the position we set above
    m_Camera.OnUpdate(ts);

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    // draw calls
    Cosmic::Renderer2D::EndScene();
}
```

For a camera that leads the player slightly in the direction of movement, offset
the target position before calling `SetPosition`:

```cpp
auto& transform = m_Player.GetComponent<Cosmic::TransformComponent>();
auto& body      = m_Player.GetComponent<MyRigidBodyComponent>();

// Lead 0.5 world units in the direction of horizontal velocity
float leadX = body.VelocityX * 0.5f;

m_Camera.SetPosition({
    transform.Position.x + leadX,
    transform.Position.y,
    0.0f
});
```

---

### Zoom Configuration Patterns

```cpp
// Tight zoom for a side-scroller — small world, sharp zoom limits
m_Camera.SetZoomLevel(1.5f);
m_Camera.SetZoomLimits(0.5f, 5.0f);
m_Camera.SetZoomSpeed(0.15f);

// Wide view for a strategy or simulation layer
m_Camera.SetZoomLevel(8.0f);
m_Camera.SetZoomLimits(2.0f, 30.0f);
m_Camera.SetZoomSpeed(0.5f);

// Fixed zoom — user cannot scroll
m_Camera.SetZoomLevel(3.0f);
m_Camera.SetZoomLimits(3.0f, 3.0f);  // min == max locks scroll

// Bounded pan — prevent camera from leaving the game world
m_Camera.SetPositionLimits(-20.0f, 20.0f, -10.0f, 10.0f);
```

---

### The Underlying OrthographicCamera

If you need direct matrix access — for example to compute a screen-to-world
mouse position — use the camera reference returned by `GetCamera()`:

```cpp
const Cosmic::OrthographicCamera& cam = m_Camera.GetCamera();

// VP matrix for submitting to a shader manually
glm::mat4 vp = cam.GetViewProjectionMatrix();

// Inverse VP for unprojecton (screen → world)
glm::mat4 invVP = glm::inverse(vp);

glm::vec2 screenPos = Cosmic::Input::GetMousePosition();
float ndcX =  (screenPos.x / viewportWidth)  * 2.0f - 1.0f;
float ndcY = -(screenPos.y / viewportHeight) * 2.0f + 1.0f;

glm::vec4 worldPos = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
glm::vec2 mouseWorld = { worldPos.x, worldPos.y };
```

`OrthographicCamera` also exposes `GetProjectionMatrix()` and `GetViewMatrix()`
separately if you need them for shadow mapping or other multi-matrix techniques.
`SetProjection(left, right, bottom, top)` recalculates the projection and
View-Projection matrices directly without going through the controller aspect
ratio logic — use this only when you have a specific frustum in mind rather than
an aspect ratio.

---

## 16. Virtual File System

The `FileSystem` utility maps short protocol strings to real disk paths so your asset references survive directory restructuring and build configuration changes.

| Protocol         | Resolves to                            |
| ---------------- | -------------------------------------- |
| `engine://path`  | `assets/path`                          |
| `project://path` | `assets/projects/<ActiveProject>/path` |
| Raw path         | Returned unchanged                     |

```cpp
Cosmic::FileSystem::SetActiveProject("MyProject");

std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl");

if (!std::filesystem::exists(shaderPath))
{
    CS_ERROR("Asset not found: {0}", shaderPath);
    return;
}
auto shader = Cosmic::Shader::Create(shaderPath);
```

### Asset Deployment

Assets are copied to the runtime output by CMake's `POST_BUILD` command in your `CMakeLists.txt`:

```cmake
add_custom_command(TARGET MyProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/MyProject"
)
```

---

## 17. Framebuffer

The `WorkspaceLayer` handles framebuffer management automatically — your layer just calls `BeginScene` / `EndScene` normally and the output appears in the viewport panel.

For manual framebuffer control:

```cpp
Cosmic::FramebufferSpecification spec;
spec.Width = 1920; spec.Height = 1080;
auto fb = Cosmic::FrameBuffer::Create(spec);

fb->Bind();
Cosmic::RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.f});
Cosmic::RenderCommand::Clear();
// ... draw calls ...
fb->Unbind();

uint32_t texID = fb->GetColorAttachmentRendererID();
ImGui::Image((void*)(uintptr_t)texID, panelSize, {0, 1}, {1, 0});
```

---

## 18. Logging

Cosmic uses [spdlog](https://github.com/gabime/spdlog) with separate channels for engine internals and client code.

```cpp
// Engine-internal code
CS_CORE_TRACE("Texture loaded: {0}", path);
CS_CORE_INFO("Window created: {0}x{1}", width, height);
CS_CORE_WARN("Asset missing, using fallback");
CS_CORE_ERROR("Shader compile failure: {0}", filepath);
CS_CORE_CRITICAL("Out of GPU memory!");

// Your game/plugin code
CS_TRACE("Entity spawned at ({0:.2f}, {1:.2f})", x, y);
CS_INFO("Level loaded: {0}", levelName);
CS_ERROR("Save file corrupt");
```

---

## 19. Serial Communication

`Cosmic::SerialPort` provides thread-safe RS-232 serial communication on Windows. A background thread continuously polls the hardware port and accumulates data in a mutex-protected buffer.

```cpp
Cosmic::SerialPort port;

std::vector<std::string> ports = Cosmic::SerialPort::GetAvailablePorts();

if (port.Open("COM3", 115200))
    CS_INFO("Connected to COM3");

void OnFixedUpdate(float dt) override
{
    if (!port.IsOpen()) return;
    std::string data = port.FlushBuffer();
    if (!data.empty()) ParseTelemetry(data);
}

port.Close();
```

> **Windows only.** The `SerialPort` class uses Win32 APIs and will not compile on Linux/macOS without a platform implementation.

---

## 20. The Showcase Project

The Showcase project (`Projects/Showcase/`) is the canonical reference implementation for building multi-mode simulations with Cosmic. It ships with the engine SDK and demonstrates the correct patterns for time management, material-driven rendering, ECS integration, and composite layer architecture.

### Overview

Showcase compiles to `Showcase.dll` and loads through the engine's standard DLL plugin system. When selected from the Launcher, it mounts into the `WorkspaceLayer` viewport and presents four interactive simulation modes switchable at runtime from the Inspector panel.

### Simulation Modes

| Mode                | Layer Class           | Demonstrates                                            |
| ------------------- | --------------------- | ------------------------------------------------------- |
| **Flight**          | `ShowcaseFlightLayer` | Entity selection via mouse click, trail rendering, ECS  |
| **Runner**          | `ShowcaseRunLayer`    | Fixed-timestep physics, procedural obstacle generation  |
| **Shader Browser**  | `ShowcaseShaderLayer` | Runtime shader hot-reload, VFS directory scanning       |
| **ECS Stress Test** | `ShowcaseStressLayer` | Material bucket batching, 10,000+ entity grid rendering |

### Composite Layer Architecture

`ShowcaseProject` itself is a `Layer` that contains a `std::vector<std::shared_ptr<Layer>> m_Modes`. Only the active mode receives `OnUpdate`, `OnFixedUpdate`, and `OnImGuiRender` forwarded from the root layer. This is a clean pattern for multi-mode applications:

```cpp
// In ShowcaseProject::OnUpdate
auto& activeMode = m_Modes[m_ActiveModeIndex];
activeMode->UpdateLayerTime(ts); // drives the mode's local clock
activeMode->OnUpdate(ts);        // visual updates

// In ShowcaseProject::OnFixedUpdate
m_Modes[m_ActiveModeIndex]->OnFixedUpdate(deltaFixedTime); // physics
```

> **Critical design note:** The simulation modes are **not pushed onto the engine's `LayerStack`**. They are internal composites owned by `ShowcaseProject`. Pushing them onto the global stack in addition to keeping them as internal references is the "Double-Tick Trap" — see [Section 21](#21-the-double-tick-trap) for details.

### Global Time Scale Control

The Showcase ImGui panel exposes a `TimeScale` slider that modifies the host engine's global time directly:

```cpp
float hostTimeScale = Cosmic::Application::Get().GetTimeScale();
if (ImGui::SliderFloat("Simulation TimeScale", &hostTimeScale, -2.0f, 3.0f, "%.2fx"))
    Cosmic::Application::Get().SetTimeScale(hostTimeScale);
```

Setting this to zero pauses all simulation modes simultaneously. Setting it negative reverses time, causing shader effects and material animations to visibly scrub backward. The `ShowcaseRunLayer` handles this correctly by checking `deltaFixedTime <= 0.0f` in its physics loop and skipping or reversing as appropriate.

### Material Sharing Across Modes

A single `Ref<Material>` (`m_DinoMaterial`) is shared across all modes. Its `u_Time` uniform is updated by the root `ShowcaseProject::OnUpdate` using the active mode's local clock:

```cpp
if (m_DinoMaterial)
    m_DinoMaterial->Set("u_Time", activeMode->GetLocalTime());
```

This ensures that the fire shader animates in lockstep with the active simulation's timeline, not the global absolute time.

### Adding Your Own Mode

To add a new simulation mode to a Showcase-style project:

1. Create a new class inheriting `Cosmic::Layer`
2. Implement `OnAttach`, `OnDetach`, `OnUpdate`, `OnFixedUpdate`, `OnImGuiRender`, `OnEvent`
3. In your root layer's `OnAttach`, push it into the modes vector:
   ```cpp
   m_Modes.push_back(std::make_shared<MyNewMode>(m_Scene, m_Material));
   m_Modes.back()->OnAttach();
   ```
4. **Do not** push it onto `Application`'s `LayerStack` — it will be driven by the parent

---

## 21. Complete API Reference Tables

### Renderer2D

| Function          | Parameters                                                                   | Description                                                      |
| ----------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| `BeginScene`      | `const OrthographicCamera&`                                                  | Starts a batch pass, caches VP matrix, resets buffers            |
| `EndScene`        | —                                                                            | Flushes all batched geometry to GPU                              |
| `PushRenderPass`  | `const glm::mat4& viewProj, const glm::vec4& viewportBounds`                 | Push a scoped camera pass; flushes pending geometry first        |
| `PopRenderPass`   | —                                                                            | Flush, pop current pass, restore prior pass state                |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, vec4 color`                                       | Flat-color quad; `vec2` inserts z=0                              |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, Ref<Texture>, float tiling, vec4 tint`            | Textured quad; `vec2` inserts z=0                                |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, Ref<Material>`                                    | Material/shader-driven quad; `vec2` inserts z=0                  |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, Ref<SubTexture2D>, vec4 tint`                     | Sprite-atlas tile; `vec2` inserts z=0                            |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, vec4 color`                            | Rotated flat quad (rot in radians); `vec2` inserts z=0           |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, Ref<Texture>, float tiling, vec4 tint` | Rotated textured quad; `vec2` inserts z=0                        |
| `DrawRotatedQuad` | `vec3 pos, vec2 size, float rot, Ref<Material>`                              | Rotated material quad                                            |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, Ref<SubTexture2D>, vec4 tint`          | Rotated sprite-atlas tile; `vec2` inserts z=0                    |
| `DrawCircle`      | `vec3 pos, vec2 size, vec4 color, float thickness=1.0, float fade=0.005`     | SDF circle; thickness 1.0=disk, <1.0=ring; fade=AA edge softness |
| `DrawCircle`      | `vec2 pos, vec2 size, vec4 color, float thickness=1.0, float fade=0.005`     | SDF circle convenience overload; inserts z=0                     |
| `DrawLine`        | `vec3 p0, vec3 p1, vec4 color`                                               | Line segment between two world-space points                      |
| `DrawRect`        | `vec3 pos, vec2 size, vec4 color`                                            | Wireframe rectangle (4 lines)                                    |
| `SetViewportSize` | `uint32_t width, uint32_t height`                                            | Update internal `ViewportDimensions`; used for `u_ViewportSize`  |
| `ResetStats`      | —                                                                            | Clears draw call and quad counters                               |
| `GetStats`        | —                                                                            | Returns `Statistics` struct                                      |
| `SetStatsStatus`  | `bool enabled`                                                               | Toggle stats recording                                           |
| `Flush`           | —                                                                            | Submit all staged quads, lines, and circles to GPU immediately   |

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
| `GetVector4`       | `string name`                | Retrieve cached vec4 (white if missing)                     |
| `GetVector`        | `string name`                | Legacy alias for `GetVector4`; returns `glm::vec4`          |
| `GetTexture`       | `string name`                | Retrieve cached texture (nullptr if missing)                |
| `Bind`             | —                            | Binds shader and uploads all cached uniforms                |
| `GetShader`        | —                            | Returns the underlying `Ref<Shader>`                        |
| `GetName`          | —                            | Returns the material's debug name string                    |
| `HasFloat`         | `string name`                | Returns true if the float uniform is set                    |
| `HasFloat2`        | `string name`                | Returns true if the vec2 uniform is set                     |

### SubTexture2D

| Function           | Parameters                                                          | Description                                                                             |
| ------------------ | ------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `CreateFromCoords` | `Ref<Texture2D>, vec2 coords, vec2 cellSize, vec2 spriteSize={1,1}` | Static factory. `coords` is (column, row) in grid units; `cellSize` is tile pixel size. |
| `SubTexture2D`     | `Ref<Texture2D>, vec2 min, vec2 max`                                | Direct UV-range constructor. `min`/`max` in normalized [0,1] texture space.             |
| `GetTexture`       | —                                                                   | Returns `const Ref<Texture2D>&` — the parent atlas.                                     |
| `GetTexCoords`     | —                                                                   | Returns `const glm::vec2*` — pointer to the 4-element UV corner array (CCW order).      |

**UV corner order** (counter-clockwise, matching stb_image vertical flip):

| Index | Corner       |
| ----- | ------------ |
| 0     | Bottom-Left  |
| 1     | Bottom-Right |
| 2     | Top-Right    |
| 3     | Top-Left     |

**`CreateFromCoords` UV math:**

```
min.x = (coords.x * cellSize.x) / textureWidth
min.y = (coords.y * cellSize.y) / textureHeight
max.x = ((coords.x + spriteSize.x) * cellSize.x) / textureWidth
max.y = ((coords.y + spriteSize.y) * cellSize.y) / textureHeight
```

### RenderPass

`RenderPass` is an RAII wrapper. Constructing it calls `PushRenderPass`; destroying it calls `PopRenderPass`. It is non-copyable and non-movable — each instance must be owned by exactly one scope.

| Function / Construct       | Parameters                                                          | Description                                                                                                                                                          |
| -------------------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RenderPass` (constructor) | `const OrthographicCamera& camera, const glm::vec4& viewportBounds` | Calls `PushRenderPass(camera.GetViewProjectionMatrix(), viewportBounds)`. Flushes pending geometry, pushes new VP matrix, calls `glViewport`, resets batch counters. |
| `~RenderPass` (destructor) | —                                                                   | Calls `PopRenderPass()`. Flushes remaining geometry, pops the stack, restores prior VP matrix and viewport if a previous pass exists.                                |
| `PushRenderPass` (static)  | `const glm::mat4& viewProj, const glm::vec4& viewportBounds`        | Low-level push. `viewportBounds = {x_offset, y_offset, width, height}` in pixels from bottom-left.                                                                   |
| `PopRenderPass` (static)   | —                                                                   | Low-level pop. Asserts (debug) on empty stack.                                                                                                                       |

**Viewport bounds convention:** `{x, y, width, height}` in pixels, OpenGL bottom-left origin. For a 1280×720 framebuffer split into equal quadrants:

| Quadrant     | Bounds `{x, y, w, h}`  |
| ------------ | ---------------------- |
| Top-left     | `{0, 360, 640, 360}`   |
| Top-right    | `{640, 360, 640, 360}` |
| Bottom-left  | `{0, 0, 640, 360}`     |
| Bottom-right | `{640, 0, 640, 360}`   |

### OrthographicCameraController

| Function                       | Parameters                                       | Description                                                                                                                                       |
| ------------------------------ | ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `OrthographicCameraController` | `float aspectRatio, bool rotation = false`       | Constructor. Initializes camera with the given aspect ratio and optional Z-rotation support.                                                      |
| `OnUpdate`                     | `float ts`                                       | Polls WASD/arrow input, advances smooth zoom interpolation, updates the underlying camera transform. Must be called once per frame.               |
| `OnEvent`                      | `Event&`                                         | Routes `MouseScrolledEvent` to the zoom system and `WindowResizeEvent` to `OnResize`.                                                             |
| `OnResize`                     | `float width, float height`                      | Recalculates aspect ratio and orthographic projection. Call whenever the render target changes size.                                              |
| `GetCamera`                    | —                                                | Returns `OrthographicCamera&` for passing to `BeginScene` or `RenderPass`.                                                                        |
| `GetZoomLevel`                 | —                                                | Returns the current interpolated zoom scalar.                                                                                                     |
| `SetZoomLevel`                 | `float level`                                    | Hard-snap to a zoom level, bypassing interpolation. Sets both the current and target zoom immediately.                                            |
| `SetZoomLimits`                | `float min, float max`                           | Clamps the scroll-wheel zoom range. Default: 0.25 – 10.0.                                                                                         |
| `SetZoomSpeed`                 | `float speed`                                    | World units zoomed per scroll tick. Default: 0.25.                                                                                                |
| `SetTranslationSpeed`          | `float speed`                                    | Pan speed in world units per second. Scaled internally by the current zoom level so panning feels consistent at all zoom levels. Default: 5.0.    |
| `GetTranslationSpeed`          | —                                                | Returns the current translation speed setting.                                                                                                    |
| `SetRotationSpeed`             | `float speed`                                    | Degrees per second for Q/E rotation. Only active when `rotation = true` was passed to the constructor. Default: 180.0.                            |
| `GetRotationSpeed`             | —                                                | Returns the current rotation speed setting.                                                                                                       |
| `SetPositionLimits`            | `float minX, float maxX, float minY, float maxY` | Hard-clamps the camera pan bounds in world space. Default: ±1000 on both axes.                                                                    |
| `SetPosition`                  | `const glm::vec3& position`                      | Directly sets the camera world position, bypassing keyboard input. Also updates the underlying `OrthographicCamera` immediately.                  |
| `GetPosition`                  | —                                                | Returns the current camera world position as `const glm::vec3&`.                                                                                  |
| `SetManualMovementEnabled`     | `bool enabled`                                   | Enable or disable WASD/arrow key panning. Disable when driving the camera from code (entity follow, cutscene track, scripted pan). Default: true. |
| `IsManualMovementEnabled`      | —                                                | Returns true if keyboard panning is currently active.                                                                                             |
| `SetKeyBindings`               | `const CameraKeyBindings& bindings`              | Replace the default WASD+QE key mapping with a custom layout.                                                                                     |
| `GetKeyBindings`               | —                                                | Returns a mutable reference to the active key bindings for per-key adjustment.                                                                    |

### Shader

| Function         | Parameters                     | Description                        |
| ---------------- | ------------------------------ | ---------------------------------- |
| `Shader::Create` | `string filepath`              | Load and compile from `.glsl` file |
| `Bind`           | —                              | Activate in GPU pipeline           |
| `Unbind`         | —                              | Deactivate                         |
| `SetInt`         | `string, int`                  | Upload integer uniform             |
| `SetIntArray`    | `string, int*, uint32_t count` | Upload integer array               |
| `SetFloat`       | `string, float`                | Upload float                       |
| `SetFloat2`      | `string, vec2`                 | Upload 2-component float           |
| `SetFloat3`      | `string, vec3`                 | Upload 3-component float           |
| `SetFloat4`      | `string, vec4`                 | Upload 4-component float           |
| `SetMat3`        | `string, mat3`                 | Upload 3×3 matrix                  |
| `SetMat4`        | `string, mat4`                 | Upload 4×4 matrix                  |

### Layer Timeline API

| Function                    | Description                                                           |
| --------------------------- | --------------------------------------------------------------------- |
| `GetLocalTime()`            | Returns accumulated scaled time for this layer (seconds)              |
| `SetLocalTime(float)`       | Directly set the time accumulator (e.g. for level reset)              |
| `GetTimeScale()`            | Returns this layer's local time scale multiplier                      |
| `SetTimeScale(float)`       | Set per-layer time scale (independent of global `Application` scale)  |
| `UpdateLayerTime(float dt)` | Called by the engine each frame — accumulates `dt * m_LocalTimeScale` |

### Application Time API

| Function                       | Description                                |
| ------------------------------ | ------------------------------------------ |
| `Get().SetTimeScale(float)`    | Set global time scale affecting all layers |
| `Get().GetTimeScale()`         | Read current global scale                  |
| `Get().GetAbsoluteTime()`      | Total unscaled elapsed time in seconds     |
| `Get().UseFixedTimeStep(bool)` | Enable/disable 60Hz fixed update pass      |

### Scene / Entity

| Function                     | Parameters                      | Description                                              |
| ---------------------------- | ------------------------------- | -------------------------------------------------------- |
| `Scene::Create`              | —                               | Factory — creates a new scene                            |
| `Scene::CreateEntity`        | `string name = "GenericEntity"` | Creates entity; auto-adds Transform + Tag                |
| `Scene::DestroyEntity`       | `Entity`                        | Removes entity from registry                             |
| `Scene::OnUpdate`            | `float dt`                      | Tick scene systems                                       |
| `Scene::OnFixedUpdate`       | `float dt`                      | Fixed-step tick for scene systems                        |
| `Scene::OnRender`            | —                               | Dispatches all entities to Renderer2D by material bucket |
| `Entity::AddComponent<T>`    | `Args...`                       | Construct and attach component (asserts if duplicate)    |
| `Entity::GetComponent<T>`    | —                               | Returns reference (asserts if missing)                   |
| `Entity::HasComponent<T>`    | —                               | Returns bool                                             |
| `Entity::RemoveComponent<T>` | —                               | Removes component (asserts if missing)                   |
| `Entity::operator bool`      | —                               | True if handle is valid and scene-bound                  |

### RenderCommand

| Function        | Parameters                               | Description                     |
| --------------- | ---------------------------------------- | ------------------------------- |
| `SetClearColor` | `vec4 color`                             | Set RGBA background clear color |
| `Clear`         | —                                        | Clear color + depth buffers     |
| `Clear`         | `float r, float g, float b`              | Set color and clear in one call |
| `SetViewport`   | `uint32_t x, y, w, h`                    | Map NDC to window pixels        |
| `DrawIndexed`   | `Ref<VertexArray>, uint32_t count = 0`   | Indexed draw call               |
| `DrawLines`     | `Ref<VertexArray>, uint32_t vertexCount` | Non-indexed line draw           |

### FileSystem

| Function           | Parameters    | Description                                    |
| ------------------ | ------------- | ---------------------------------------------- |
| `SetActiveProject` | `string name` | Set active project for `project://` resolution |
| `Resolve`          | `string path` | Translate VFS protocol to real disk path       |

### SerialPort

| Function            | Parameters                                    | Description                                          |
| ------------------- | --------------------------------------------- | ---------------------------------------------------- |
| `Open`              | `string portName, uint32_t baudRate = 115200` | Open port, spawn read thread                         |
| `Close`             | —                                             | Signal thread, join, release handle                  |
| `IsOpen`            | —                                             | Returns bool                                         |
| `FlushBuffer`       | —                                             | Thread-safe extract + clear accumulated data         |
| `GetAvailablePorts` | —                                             | Static — queries Windows Registry, returns port list |

### Logging Macros

| Macro                   | Level   | Channel     |
| ----------------------- | ------- | ----------- |
| `CS_CORE_TRACE(...)`    | Verbose | Engine      |
| `CS_CORE_INFO(...)`     | Info    | Engine      |
| `CS_CORE_WARN(...)`     | Warning | Engine      |
| `CS_CORE_ERROR(...)`    | Error   | Engine      |
| `CS_CORE_CRITICAL(...)` | Fatal   | Engine      |
| `CS_TRACE(...)`         | Verbose | Client/Game |
| `CS_INFO(...)`          | Info    | Client/Game |
| `CS_WARN(...)`          | Warning | Client/Game |
| `CS_ERROR(...)`         | Error   | Client/Game |
| `CS_CRITICAL(...)`      | Fatal   | Client/Game |

---

## 22. Scene System

### Overview

`Scene` is the container for all entity and component data. It owns an `entt::registry`, drives registered `System` objects, and manages the full render pass for sprite-bearing entities.

The key architectural rule introduced in the current implementation:

> **`Scene::OnRender(camera)` owns its own `BeginScene` / `EndScene`.** Callers must never wrap it in their own `BeginScene` / `EndScene` — doing so will double-push the render pass stack and corrupt the View-Projection matrix state.

If you need to render scene entities alongside manual `Renderer2D` draw calls in the same frame, make two separate passes: one via `Scene::OnRender` and one wrapped in your own `BeginScene` / `EndScene`. They will each flush independently and both draw correctly.

---

### Creating a Scene

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();
```

Always use `Scene::Create()` — it returns a `Ref<Scene>` (`shared_ptr`) and keeps ownership consistent with the rest of the engine.

---

### Creating and Managing Entities

```cpp
// CreateEntity always auto-adds TransformComponent and TagComponent
Cosmic::Entity player = m_Scene->CreateEntity("Player");
Cosmic::Entity enemy  = m_Scene->CreateEntity("Enemy");
Cosmic::Entity bullet = m_Scene->CreateEntity(); // name defaults to "GenericEntity"
```

```cpp
// Add components
auto& body   = player.AddComponent<MyRigidBodyComponent>(1.0f, 0.3f);
auto& sprite = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);

// Read and modify
auto& transform = player.GetComponent<Cosmic::TransformComponent>();
transform.Position = { 2.0f, 0.5f, 0.0f };
transform.Rotation.z = 45.0f;  // always in DEGREES — Scene::OnRender converts internally
transform.Scale = { 1.0f, 1.0f };

// Safe conditional access
if (player.HasComponent<MyRigidBodyComponent>())
    player.GetComponent<MyRigidBodyComponent>().Velocity = { 3.0f, 0.0f };

// Remove
player.RemoveComponent<MyRigidBodyComponent>();

// Validity check
if (player) { /* handle is valid and scene-bound */ }

// Destroy
m_Scene->DestroyEntity(player);
// Do not call GetComponent on a destroyed handle afterward
```

---

### Built-in Components

**`TransformComponent`**

```cpp
struct TransformComponent {
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll, stored in DEGREES
    glm::vec2 Scale    { 1.f, 1.f };
    glm::mat4 GetTransform() const;        // full TRS matrix (converts Z to radians internally)
};
```

> **Degrees vs. Radians:** `TransformComponent::Rotation.z` is stored and set in **degrees**. `Scene::OnRender` applies `glm::radians(transform.Rotation.z)` before passing to `Renderer2D::DrawRotatedQuad`. If you call `DrawRotatedQuad` yourself (outside of `Scene::OnRender`), you are responsible for the conversion.

**`SpriteRendererComponent`**

```cpp
struct SpriteRendererComponent {
    Ref<Material> ActiveMaterial;
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f }; // flat-color fallback when no material
    bool FlipX = false;
    bool FlipY = false;
};
```

**`TagComponent`**

```cpp
struct TagComponent { std::string Tag; };
```

---

### Scene Update and Render

```cpp
void MyLayer::OnUpdate(float ts) override
{
    m_Scene->OnUpdate(ts);         // ticks all registered Systems

    // Scene::OnRender owns BeginScene and EndScene.
    // Do NOT wrap this in your own BeginScene / EndScene.
    m_Scene->OnRender(m_Camera.GetCamera());
}

void MyLayer::OnFixedUpdate(float dt) override
{
    m_Scene->OnFixedUpdate(dt);    // fixed-step tick for Systems
}
```

### Mixed Manual + Scene Rendering

If you need to draw both scene entities and manual primitives (lines, circles, custom quads) in the same frame, use two separate passes. They are fully independent:

```cpp
void MyLayer::OnUpdate(float ts) override
{
    m_Scene->OnUpdate(ts);

    // Pass 1: scene entities (owns its own BeginScene/EndScene)
    m_Scene->OnRender(m_Camera.GetCamera());

    // Pass 2: manual overlay geometry
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawCircle(playerPos, { 1.2f, 0.4f }, ringColor, 0.05f, 0.005f);
    Cosmic::Renderer2D::DrawLine(start, end, { 1.f, 1.f, 0.f, 1.f });
    Cosmic::Renderer2D::EndScene();
}
```

### Scene Render Internals

`Scene::OnRender` groups all entities that have both a `TransformComponent` and a `SpriteRendererComponent` into material buckets before submitting to `Renderer2D`. All entities sharing the same `ActiveMaterial` are drawn in a single batch, minimizing `FlushAndReset` calls caused by material state changes.

Entities with no `ActiveMaterial` fall back to flat-color rendering using `SpriteRendererComponent::Color`.

`SpriteRendererComponent::FlipX` and `FlipY` are applied by negating the corresponding scale component in the draw call — the `TransformComponent` itself is never modified.

---

### Writing ECS Systems

`System` is an abstract base class for broad update logic that spans many entities. Systems are owned by the scene and dispatched automatically.

```cpp
class GravitySystem : public Cosmic::System
{
public:
    void OnFixedUpdate(Cosmic::Scene& scene, float dt) override
    {
        auto view = scene.View<Cosmic::TransformComponent, MyRigidBodyComponent>();
        view.each([dt](auto& transform, auto& body)
        {
            if (!body.IsGrounded)
            {
                body.VelocityY       += -9.8f * dt;
                transform.Position.y += body.VelocityY * dt;
            }
        });
    }
};
```

```cpp
// Register in OnAttach
void MyLayer::OnAttach() override
{
    m_Scene = Cosmic::Scene::Create();
    m_Scene->AddSystem<GravitySystem>();
}
```

---

### DLL-Safe Component Registration

EnTT assigns component type IDs using sequential static counters that reset independently in each compiled binary. When your game compiles as a separate `.dll`, a component registered as ID 3 in `Cosmic.dll` may be ID 1 in `MyProject.dll` — any `AddComponent` or `GetComponent` that crosses the boundary will silently operate on the wrong pool.

**The fix is `CS_REGISTER_COMPONENT`:**

```cpp
// In your component header — must be compiled by both the engine and the DLL
CS_REGISTER_COMPONENT(MyNamespace::MyComponent)
```

This specializes `entt::type_hash<T>` to return a compile-time hash of the fully-qualified type name, which is identical in every binary that includes the header.

**Rules:**

- Always use the fully-qualified name with namespace: `CS_REGISTER_COMPONENT(Workspace::BallComponent)` — not just `CS_REGISTER_COMPONENT(BallComponent)`.
- Place the macro at file scope in the header, outside any class or function body.
- Built-in engine components (`TagComponent`, `TransformComponent`, `SpriteRendererComponent`) are already registered in `Components.h` — do not register them again.
- Missing registration produces no compile error; the only symptom is silent runtime data corruption.

```cpp
// Example — register all components in your header
struct MyPhysicsComponent { float Mass = 1.0f; glm::vec2 Velocity = { 0.f, 0.f }; };
CS_REGISTER_COMPONENT(Workspace::MyPhysicsComponent)
```

---

### Complete Scene + Entity API Reference

| Function                     | Parameters                      | Description                                                                                      |
| ---------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------ |
| `Scene::Create`              | —                               | Static factory — returns `Ref<Scene>`                                                            |
| `Scene::CreateEntity`        | `string name = "GenericEntity"` | Creates entity; auto-adds `TransformComponent` + `TagComponent`                                  |
| `Scene::DestroyEntity`       | `Entity`                        | Removes entity and all its components from the registry                                          |
| `Scene::OnUpdate`            | `float dt`                      | Ticks all registered Systems via `System::OnUpdate`                                              |
| `Scene::OnFixedUpdate`       | `float dt`                      | Fixed-step tick for all registered Systems via `System::OnFixedUpdate`                           |
| `Scene::OnRender`            | `const OrthographicCamera&`     | Calls `BeginScene(camera)`, dispatches all sprite entities grouped by material, calls `EndScene` |
| `Scene::AddSystem<T>`        | `Args...`                       | Construct and attach a System; returns `T&`                                                      |
| `Scene::RemoveAllSystems`    | —                               | Clears all registered systems                                                                    |
| `Scene::View<Components...>` | —                               | Returns an EnTT view for querying entities with the specified component set                      |
| `Entity::AddComponent<T>`    | `Args...`                       | Construct and attach component (asserts in debug if already present)                             |
| `Entity::GetComponent<T>`    | —                               | Returns mutable reference (asserts in debug if absent)                                           |
| `Entity::HasComponent<T>`    | —                               | Returns bool                                                                                     |
| `Entity::RemoveComponent<T>` | —                               | Removes component (asserts in debug if absent)                                                   |
| `Entity::operator bool`      | —                               | True if handle is valid and scene-bound                                                          |

---

## 23. Window System

### Overview

`Window` wraps GLFW and provides the engine's connection to the OS windowing system. It handles window creation, the OpenGL context, event callbacks, VSync, and fullscreen toggling. Most client code interacts with the window indirectly through `Application::Get().GetWindow()`.

The window is created and owned by `Application` — it is a `Scope<Window>` (unique_ptr) and is destroyed during `Application::Shutdown`.

---

### Fullscreen

Cosmic uses **borderless windowed fullscreen** rather than exclusive fullscreen or GLFW's monitor-switch path. The technique strips Win32 window decoration style bits and stretches the window to cover the target monitor without changing the display mode.

**Why borderless windowed instead of exclusive fullscreen:**

- No black-screen flash on entry/exit (no DWM mode switch)
- Win+Shift+S, screen capture tools, and hardware overlays (Discord, Xbox Game Bar) continue working
- Alt+Tab works naturally — the app remains a normal window in the z-order
- No `ClipCursor` needed, so multi-monitor mouse movement is unaffected

**Entering fullscreen:**

1. Current windowed position and size are saved.
2. `WS_OVERLAPPEDWINDOW` style bits are stripped via `SetWindowLong`.
3. `SetWindowPos` stretches the window to cover the monitor that currently contains the window center.
4. `glfwSetWindowMonitor` is **not** called — that path triggers a display-mode switch.

**Exiting fullscreen:**

1. `WS_OVERLAPPEDWINDOW` bits are restored.
2. `SetWindowPos` restores the saved position and size.

On non-Windows platforms, a `glfwSetWindowMonitor` fallback is used.

---

### Toggling Fullscreen

The engine default hotkey is **F11** (press, not repeat). This is handled inside the GLFW key callback before the event is dispatched to the engine's layer system — F11 never reaches `OnEvent` in your layers.

```cpp
// Programmatic toggle from anywhere
Cosmic::Application::Get().GetWindow().SetFullscreen(true);
Cosmic::Application::Get().GetWindow().SetFullscreen(false);

// Query current state
bool fs = Cosmic::Application::Get().GetWindow().IsFullscreen();
```

---

### Custom Fullscreen Hotkey Override

A plugin DLL can replace the default F11 behavior with its own key combination. The override receives raw GLFW key/action/mods before any engine logic runs. Return `true` to consume the key (prevents F11 default and engine event dispatch); return `false` to pass through.

```cpp
// Register in OnAttach
void MyLayer::OnAttach() override
{
    auto& window = Cosmic::Application::Get().GetWindow();

    window.SetFullscreenHotkeyOverride([](int key, int action, int mods) -> bool
    {
        // Alt + Enter toggles fullscreen
        // GLFW_KEY_ENTER = 257, GLFW_PRESS = 1, GLFW_MOD_ALT = 0x0004
        if (key == 257 && action == 1 && (mods & 0x0004))
        {
            auto& app = Cosmic::Application::Get();
            app.GetWindow().SetFullscreen(!app.GetWindow().IsFullscreen());
            return true; // consumed — do not dispatch as a KeyPressedEvent
        }
        return false;
    });
}
```

The override is stored directly on `Window`, not in the GLFW user pointer, so `Application` can always reach it. **Always clear the override before your DLL is unloaded.** `Application::UnloadProjectDLL` calls `m_Window->ClearFullscreenHotkeyOverride()` automatically, so if you use the standard DLL lifecycle you do not need to clear it manually. If you register an override from a non-DLL context, clear it in `OnDetach`.

```cpp
void MyLayer::OnDetach() override
{
    // Only needed if you registered the override yourself outside the DLL lifecycle
    Cosmic::Application::Get().GetWindow().ClearFullscreenHotkeyOverride();
}
```

---

### Monitor Detection

When entering fullscreen, the engine finds the monitor that currently contains the window center and covers that monitor — not necessarily the primary display. This is handled internally and requires no client code.

If the window is repositioned before fullscreen is entered (e.g., dragged to a second monitor), fullscreen will correctly target the second monitor.

---

### Window API Reference

| Function                        | Parameters                           | Description                                                                                                        |
| ------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------ |
| `GetWidth`                      | —                                    | Returns cached client-area width in pixels                                                                         |
| `GetHeight`                     | —                                    | Returns cached client-area height in pixels                                                                        |
| `GetSize`                       | `int* width, int* height`            | Queries framebuffer size directly from GLFW — use this for FBO resize matching                                     |
| `GetHandle`                     | —                                    | Returns raw `GLFWwindow*` for API-specific operations                                                              |
| `SetEventCallback`              | `const EventCallbackFn&`             | Binds the function that receives all engine events from this window                                                |
| `SetVSync`                      | `bool enabled`                       | Enables or disables vertical sync (`glfwSwapInterval`)                                                             |
| `IsVSync`                       | —                                    | Returns current VSync state                                                                                        |
| `ShouldClose`                   | —                                    | Returns true when the OS has signalled the window should close                                                     |
| `PollEvents`                    | —                                    | Processes pending OS input; dispatches via EventCallback                                                           |
| `SwapBuffers`                   | —                                    | Presents the rendered frame (calls `glfwSwapBuffers` through the graphics context)                                 |
| `SetFullscreen`                 | `bool enabled`                       | Toggles borderless windowed fullscreen on the monitor containing the window center                                 |
| `IsFullscreen`                  | —                                    | Returns current fullscreen state                                                                                   |
| `SetFullscreenHotkeyOverride`   | `const FullscreenToggleActionFn& fn` | Register a delegate that intercepts raw key events before F11 handling. `fn(key, action, mods) -> bool (consumed)` |
| `ClearFullscreenHotkeyOverride` | —                                    | Remove any registered delegate. Call before unloading a plugin DLL that registered an override                     |

---

### FullscreenToggleActionFn Signature

```cpp
// Defined in Window.h
using FullscreenToggleActionFn = std::function<bool(int key, int action, int mods)>;
```

| Parameter | Type  | Values                                                                       |
| --------- | ----- | ---------------------------------------------------------------------------- |
| `key`     | `int` | GLFW key code (e.g. `257` = Enter, `290` = F1, `294` = F5)                   |
| `action`  | `int` | `0` = release, `1` = press, `2` = repeat                                     |
| `mods`    | `int` | Bitmask: `0x0001` = Shift, `0x0002` = Ctrl, `0x0004` = Alt, `0x0008` = Super |

Return `true` to consume the key (no engine event dispatched, F11 default suppressed). Return `false` to pass through normally.

---

### VSync

VSync is enabled by default at engine startup (`Application::Initialize` calls `m_Window->SetVSync(true)`). This locks the frame present rate to the monitor refresh rate, preventing screen tearing and reducing GPU load when the simulation is less demanding than the maximum frame rate.

```cpp
// Disable for uncapped frame rate (useful for benchmarking)
Cosmic::Application::Get().GetWindow().SetVSync(false);

// Re-enable
Cosmic::Application::Get().GetWindow().SetVSync(true);
```

VSync state is preserved across fullscreen transitions.

---

### Events Generated by Window

The GLFW callbacks translate OS signals into engine events. These are dispatched through `Application::OnEvent` and propagate down the `LayerStack`:

| OS Signal         | Engine Event               | Key Data                           |
| ----------------- | -------------------------- | ---------------------------------- |
| Window resized    | `WindowResizeEvent`        | `GetWidth()`, `GetHeight()`        |
| Window closed     | `WindowCloseEvent`         | —                                  |
| Key pressed/held  | `KeyPressedEvent`          | `GetKeyCode()`, `GetRepeatCount()` |
| Key released      | `KeyReleasedEvent`         | `GetKeyCode()`                     |
| Character typed   | `KeyTypedEvent`            | `GetKeyCode()` (Unicode codepoint) |
| Mouse button down | `MouseButtonPressedEvent`  | `GetMouseButton()`                 |
| Mouse button up   | `MouseButtonReleasedEvent` | `GetMouseButton()`                 |
| Mouse moved       | `MouseMovedEvent`          | `GetX()`, `GetY()`                 |
| Scroll wheel      | `MouseScrolledEvent`       | `GetXOffset()`, `GetYOffset()`     |

Fullscreen hotkey keys (F11 by default, or whatever your override intercepts) are **consumed before** any engine event is generated — they never appear as `KeyPressedEvent` in your layers.

`WindowCloseEvent` is consumed by `Application::OnWindowClose` and does not propagate to layers.

`WindowResizeEvent` is handled by `Application::OnWindowResize` (resizes the framebuffer) but is **not consumed** — it continues propagating to all layers so camera controllers can update their aspect ratios.

---

# Part II — Engine Internals

---

## 22. Source File Map

### `src/core/`

| File                 | Purpose                                                                                                                                                                                                |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Core.h`             | Universal foundation — `Scope<T>`, `Ref<T>`, `COSMIC_API` DLL macros, `BIT()`, `GLCORE_ASSERT`, `GLCORE_BIND_EVENT_FN`. Included first in nearly every file.                                           |
| `Application.h/.cpp` | Root singleton. Owns the window, renderer, framebuffer, ImGui, LayerStack. Drives the frame loop with fixed + variable timestep passes. Manages launcher↔workspace transitions and DLL plugin loading. |
| `Layer.h`            | Abstract base for all engine components. Declares all lifecycle hooks and owns the per-layer timeline state (`m_LocalTime`, `m_LocalTimeScale`).                                                       |
| `LayerStack.h/.cpp`  | Ordered container of `Layer*` borrows. Manages insertion boundary between game layers and overlays.                                                                                                    |
| `Window.h/.cpp`      | GLFW window wrapper. Handles creation, event callbacks, VSync, buffer swapping.                                                                                                                        |
| `Input.h/.cpp`       | Static polling interface. Wraps GLFW key and mouse queries.                                                                                                                                            |
| `Log.h/.cpp`         | spdlog wrapper. Initializes engine + client loggers.                                                                                                                                                   |
| `Timestep.h`         | Thin float wrapper for delta-time. Prevents seconds/milliseconds confusion.                                                                                                                            |

### `src/layers/`

| File                    | Purpose                                                                                                                                                                 |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ImGuiLayer.h/.cpp`     | Initializes ImGui + ImPlot, GLFW backend, OpenGL3 backend. Manages event blocking.                                                                                      |
| `WorkspaceLayer.h/.cpp` | The editor shell. Hosts an ImGui dockspace with Viewport + Inspector panels. Owns the client DLL layer slot. Drives the framebuffer render loop and forwards time down. |
| `LauncherLayer.h/.cpp`  | Startup hub. Scans for `.dll` files, renders project list, hosts the project creation wizard.                                                                           |

### `src/renderer/`

| File                   | Purpose                                                                                                                                                      |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `RendererAPI.h/.cpp`   | Abstract backend interface. Holds static API selection flag.                                                                                                 |
| `RenderCommand.h/.cpp` | Static dispatcher forwarding to the active `RendererAPI*` instance.                                                                                          |
| `Renderer.h/.cpp`      | High-level 3D/static orchestrator. Manages per-scene VP matrix.                                                                                              |
| `Renderer2D.h/.cpp`    | High-performance 2D batch renderer. Manages vertex/index staging buffers (up to 10,000 quads), texture slots (up to 32), material state, and line rendering. |

### `src/graphics/`

| File                 | Purpose                                                                                                                      |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `Buffer.h/.cpp`      | `ShaderDataType`, `BufferElement`, `BufferLayout` (stride calculator). Abstract `VertexBuffer` and `IndexBuffer` interfaces. |
| `VertexArray.h/.cpp` | Abstract VAO interface.                                                                                                      |
| `Shader.h/.cpp`      | Abstract GPU program interface. Uniform set methods. Factory `Create` from filepath.                                         |
| `Texture.h/.cpp`     | `Texture` base and `Texture2D` interfaces. Factory `Create`.                                                                 |
| `Material.h/.cpp`    | Pairs `Ref<Shader>` with named uniform caches. `Bind()` uploads all cached uniforms.                                         |
| `FrameBuffer.h/.cpp` | Abstract FBO interface. Factory `Create`.                                                                                    |
| `GraphicsContext.h`  | Two-method interface: `Init()` and `SwapBuffers()`.                                                                          |

### `src/platform/opengl/`

| File                       | Purpose                                                                                                                   |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `OpenGLContext.h/.cpp`     | `glfwMakeContextCurrent` + GLAD initialization. `SwapBuffers` via `glfwSwapBuffers`.                                      |
| `OpenGLRendererAPI.h/.cpp` | Concrete `RendererAPI` for OpenGL. Init enables blending + depth test.                                                    |
| `OpenGLBuffer.h/.cpp`      | Dynamic (`GL_DYNAMIC_DRAW`) and static (`GL_STATIC_DRAW`) vertex buffers. Index buffer.                                   |
| `OpenGLVertexArray.h/.cpp` | Generates VAO, calls `glVertexAttribPointer` for each layout element.                                                     |
| `OpenGLShader.h/.cpp`      | Reads `.glsl` from disk, preprocesses `#type` directives, compiles and links. Uniform location cache.                     |
| `OpenGLTexture.h/.cpp`     | File-based loader using stb_image. Procedural empty texture. `SetData` via `glTexSubImage2D`.                             |
| `OpenGLFrameBuffer.h/.cpp` | FBO with RGBA8 color attachment + Depth24_Stencil8 depth attachment. `Invalidate()` tears down and reallocates on resize. |

### `src/scene/`

| File           | Purpose                                                                                                                              |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `Scene.h/.cpp` | Owns `entt::registry`. `OnRender` sorts entities into material buckets before dispatching to `Renderer2D`.                           |
| `Entity.h`     | Lightweight handle (`entt::entity` + `Scene*`). Template `AddComponent`, `GetComponent`, `HasComponent`, `RemoveComponent`.          |
| `Components.h` | `TagComponent`, `TransformComponent`, `SpriteRendererComponent`. Includes `entt::type_hash` specializations for DLL boundary safety. |

---

## 23. Hot-Reloadable DLL Architecture

### The Isolated Dockspace Shell

`WorkspaceLayer` acts as a persistent editor shell that decouples the engine host environment from transient guest project layers. The host application window, ImGui dockspace, and framebuffer all belong to `WorkspaceLayer` and persist indefinitely. The guest — your game code — lives exclusively in a dynamically loaded DLL that can be mounted and unmounted without restarting the engine.

```
┌────────────────────────────────────────────────────┐
│  Engine Host (CosmicApp.exe + Cosmic.dll)          │
│  ┌──────────────────────────────────────────────┐  │
│  │  WorkspaceLayer (persistent editor shell)    │  │
│  │  ├── ImGui dockspace                         │  │
│  │  ├── Framebuffer (render target)             │  │
│  │  └── m_ClientViewportLayer  ─────────────┐  │  │
│  └─────────────────────────────────────────────│──┘  │
│                                               │       │
│   ┌───────────────────────────────────────────▼──┐   │
│   │  Guest DLL (MyProject.dll)                   │   │
│   │  ShowcaseProject / YourProject (Layer*)       │   │
│   │  Loaded via LoadLibraryA, GetProcAddress      │   │
│   └──────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────┘
```

### Host Context Sharing

When a DLL is loaded, global singletons like ImGui's internal context pointer are not shared across compilation boundaries by default — each DLL gets its own data segment. The `HostContext` mechanism solves this explicitly:

```cpp
// In engine host (Application.cpp):
HostContext ctx;
ctx.ImGuiCtx  = ImGui::GetCurrentContext();
ctx.ImPlotCtx = ImPlot::GetCurrentContext();
initContexts(ctx); // pointer passed into DLL

// In guest DLL (your project .cpp):
void InitializePluginContexts(Cosmic::HostContext context)
{
    ImGui::SetCurrentContext(context.ImGuiCtx);   // sync context pointer
    ImPlot::SetCurrentContext(context.ImPlotCtx);
}
```

After this call, both the engine and the game DLL write to the same ImGui command list. Without it, any `ImGui::Begin()` call in the DLL writes to a null context, causing an immediate crash.

### Load Sequence

```
1. LoadLibraryA("MyProject.dll")
2. GetProcAddress("InitializePluginContexts")
   GetProcAddress("CreatePluginLayer")
3. initContexts(ctx)                     ← sync ImGui/ImPlot across boundary
4. m_ActivePluginLayer = createLayer()   ← engine takes ownership of raw Layer*
5. m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer)
   → calls m_ActivePluginLayer->OnAttach()
```

### Unload Sequence (Order is Critical)

```
1. m_WorkspaceLayer->ClearViewportLayer()
   → calls m_ActivePluginLayer->OnDetach()
   → removes from render pipeline
2. delete m_ActivePluginLayer
   → destructor releases all GPU resources
   → MUST happen BEFORE FreeLibrary!
   → if FreeLibrary runs first, the vtable is gone → crash
3. FreeLibrary(m_PluginHandle)
4. m_PluginHandle = nullptr
   m_ActivePluginLayer = nullptr
```

The unload order is not optional. Any GPU resources (textures, shaders, framebuffers) allocated by the DLL must be freed while the DLL's code is still mapped in memory — otherwise the destructors cannot run.

### The Safe Zone

All DLL transitions happen in a guaranteed "Safe Zone" at the end of each frame, where no iteration over the `LayerStack` is active:

```cpp
// Application::Run — Safe Zone
if (!m_PendingProjectDLL.empty())
{
    // 1. Pop LauncherLayer
    // 2. Create and push WorkspaceLayer
    // 3. LoadProjectDLL(m_PendingProjectDLL)
    m_PendingProjectDLL = "";
}
```

This prevents iterator invalidation, double-free conditions, and context destruction race conditions that would occur if layers were swapped mid-frame.

---

## 24. Top-Down Time Propagation Waterfall

Time flows from a single hardware source down through multiple indirection layers to reach your simulation. Understanding this cascade is essential for correct time-sensitive code.

```
┌─────────────────────────────────────────────────────┐
│  glfwGetTime()  (hardware timer, absolute seconds)  │
│  rawDelta = currentTime - lastFrameTime             │
│  clamped to 0.25s max (spiral-of-death protection)  │
└──────────────────────┬──────────────────────────────┘
                       │ × m_TimeScale  (global, set via App::SetTimeScale)
                       ▼
┌─────────────────────────────────────────────────────┐
│  Application::Run                                   │
│  scaledDelta = rawDelta * m_TimeScale               │
│  m_AbsoluteTime += scaledDelta                      │
│                                                     │
│  Fixed pass:    accumulator += scaledDelta          │
│                 while (acc >= 1/60):                │
│                     layer->OnFixedUpdate(1/60s)     │
│                                                     │
│  Variable pass: layer->UpdateLayerTime(scaledDelta) │
│                 layer->OnUpdate(scaledDelta)         │
└──────────────────────┬──────────────────────────────┘
                       │ (already scaled)
                       ▼
┌─────────────────────────────────────────────────────┐
│  WorkspaceLayer::OnUpdate(ts)                       │
│  fb->Bind()                                         │
│  m_ClientViewportLayer->UpdateLayerTime(ts)         │
│  m_ClientViewportLayer->OnUpdate(ts)                │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  ShowcaseProject::OnUpdate(ts)                      │
│  activeMode->UpdateLayerTime(ts)  ← drives the      │
│  m_DinoMaterial->Set("u_Time",      mode's clock    │
│      activeMode->GetLocalTime()) ← uses mode's time │
│  activeMode->OnUpdate(ts)                           │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  ShowcaseRunLayer / FlightLayer / etc.              │
│  m_Material->Set("u_Time", GetLocalTime())          │
│  Renderer2D::BeginScene / DrawQuad / EndScene       │
└─────────────────────────────────────────────────────┘
```

### Why Each Step Matters

**`Application::Run` applies the global scale once.** By multiplying at the top of the chain, every downstream consumer — fixed updates, variable updates, and material uniforms — automatically respects pause and rewind without any per-system code.

**`WorkspaceLayer` passes the already-scaled delta directly.** It does not re-scale. The delta arriving at `WorkspaceLayer::OnUpdate` is the same value `Application::Run` computed.

**`ShowcaseProject` is a composite layer, not on the engine stack.** It receives time through the normal `OnUpdate` path and redistributes it only to the active mode. Inactive modes do not accumulate time, which prevents their clocks from drifting while they're hidden.

**`UpdateLayerTime(ts)` is the accumulation point.** Each layer's `m_LocalTime += ts * m_LocalTimeScale`. If a layer's own `m_LocalTimeScale` is 0.5, its clock runs at half speed even when the global scale is 1.0. This enables independent per-layer slow-motion effects.

---

## 25. The Double-Tick Trap

This section documents a critical architectural failure pattern and explains the design decision that prevents it in Cosmic.

### What is the Double-Tick Trap?

The trap occurs when a layer is simultaneously registered in two update chains — typically by being pushed onto the global engine `LayerStack` **and** also held as an internal reference inside a parent layer that manually calls its update methods.

```
BAD PATTERN:
Application::Run calls layer->OnUpdate(ts)     ← tick #1
ShowcaseProject also calls mode->OnUpdate(ts)  ← tick #2
```

### Catastrophic Symptoms

When a layer receives two `OnUpdate` calls per frame:

- **Physics accumulate at 2×.** A runner character moves twice as fast. Gravity applies twice per frame. Collision detection fires at double frequency.
- **Shader time doubles.** `u_Time` is incremented twice per frame, making animated effects run at twice the intended speed.
- **ImGui duplicate IDs.** Dear ImGui assigns stable IDs by hashing the widget label against the current window stack. When `OnImGuiRender` is called twice per frame, the same widget is pushed twice, causing internal ID hash collisions, rendering artifacts, and potential crashes in complex layouts.
- **Fixed update spiral.** The fixed-step accumulator fires the double-ticked layer's `OnFixedUpdate` twice as many times per real second, making deterministic simulation completely unpredictable.

### How Cosmic Prevents It

The engine's DLL plugin system routes execution through a single path: `WorkspaceLayer::OnUpdate` → `m_ClientViewportLayer->OnUpdate`. The guest `Layer*` is **never** pushed onto `Application`'s `LayerStack` directly. `WorkspaceLayer` is the registered layer; it owns and drives the client layer internally.

Similarly, `ShowcaseProject`'s simulation modes (`ShowcaseRunLayer`, `ShowcaseFlightLayer`, etc.) are held in a `std::vector<shared_ptr<Layer>>` and driven manually by `ShowcaseProject::OnUpdate`. They are not registered on any `LayerStack`.

The rule: **a layer must appear in exactly one update chain**. If you manually call a layer's update methods, do not also register it on the `LayerStack`.

---

## 26. The OpenGL Graphics Pipeline

### Overview

```
CPU Side                              GPU Side
────────                              ────────
Renderer2D writes vertex data ──────► VBO (Vertex Buffer Object)
BufferLayout defines schema ─────────► VAO (Vertex Array Object)
Index data ──────────────────────────► IBO (Index Buffer Object)
Shader::Bind() + SetMat4 ────────────► Vertex Shader Stage
                                        transforms world → clip space
                                           │
                                           ▼
                                        Primitive Assembly + Rasterization
                                           │
                                           ▼
                                        Fragment Shader Stage
                                        samples textures, applies uniforms
                                           │
                                           ▼
                                        OpenGLFrameBuffer
                                        color + depth attachments
                                           │
                                           ▼
                                        glfwSwapBuffers() → screen
```

### The Vertex Layout

```
QuadVertex layout:
┌──────────────────────────────────────────────────────┐
│  Offset 0:   a_Position     (Float3 = 12 bytes)      │
│  Offset 12:  a_Color        (Float4 = 16 bytes)      │
│  Offset 28:  a_TexCoord     (Float2 = 8 bytes)       │
│  Offset 36:  a_TexIndex     (Float  = 4 bytes)       │
│  Offset 40:  a_TilingFactor (Float  = 4 bytes)       │
│  Stride: 44 bytes total per vertex                   │
└──────────────────────────────────────────────────────┘
```

`OpenGLVertexArray::AddVertexBuffer` records these offsets into the VAO via `glVertexAttribPointer`. A single `glBindVertexArray` restores all attribute pointers.

### Why OpenGL?

OpenGL was chosen as the initial backend for portability and simplicity. The entire abstraction layer (`RendererAPI` → `RenderCommand` → `Renderer2D`) is backend-agnostic — adding DirectX or Vulkan requires only implementing the `RendererAPI` and `OpenGLXxx` classes without touching any high-level renderer code.

---

## 27. Hardware Abstraction Architecture

Cosmic uses the **Factory Pattern** throughout its graphics layer. Every GPU resource is created through a static `Create` method that queries `RendererAPI::GetAPI()` and instantiates the correct platform class:

```cpp
Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
{
    switch (RendererAPI::GetAPI())
    {
        case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(size);
        case RendererAPI::API::DirectX: return nullptr; // future
        case RendererAPI::API::None:    return nullptr;
    }
}
```

`Renderer2D`, `Scene`, and your game code never include any OpenGL headers. The OpenGL implementation is completely isolated in `src/platform/opengl/`.

`RenderCommand` holds a single `static RendererAPI* s_RendererAPI` initialized at startup. All static methods forward to this pointer — one virtual dispatch per draw call, essentially free compared to GPU work.

---

## 28. Batch Rendering Deep Dive

### The Batch State Machine

```
BeginScene(camera)
    → cache VP matrix
    → reset QuadVertexPtr to QuadVertexBufferBase
    → reset QuadIndexCount to 0
    → reset TextureSlotIndex to 1 (slot 0 = white texture)
    │
    ▼
DrawQuad() × N
    → write 4 QuadVertex structs to QuadVertexPtr
    → QuadVertexPtr += 4; QuadIndexCount += 6
    → assign texture slot, record float index in vertex
    │
    ├── if QuadIndexCount >= MaxIndices → FlushAndReset()
    ├── if TextureSlotIndex >= 32 → FlushAndReset()
    └── if material changes → FlushAndReset()
    │
    ▼
EndScene() → Flush()
    → glBufferSubData (upload only filled portion)
    → bind all active texture slots
    → bind material shader and upload uniforms
    → glDrawElements(GL_TRIANGLES, QuadIndexCount, ...)
```

### Texture Batching

Up to 32 simultaneous textures per draw call via `sampler2D u_Textures[32]`. Each quad vertex stores a `float TexIndex` that the fragment shader uses to index into this array.

### Material State Changes

When a draw call uses a different `Material` than the previous one, `FlushAndReset` submits the current batch before switching. `Scene::OnRender` sorts entities into material buckets specifically to minimize these mid-frame flushes.

### The White Texture Trick

A 1×1 white texture occupies slot 0. Flat-color quads use `TexIndex = 0` and rely on vertex color — the fragment shader samples white and multiplies by vertex color, producing the correct solid color without a separate code path.

---

## 29. Shader Preprocessing System

`OpenGLShader::PreProcess` handles four cases:

**Case 1: Standard multi-stage file** (has `#type vertex` and `#type fragment`) — splits source at `#type` boundaries, injects missing uniform declarations per stage.

**Case 2: Fragment-only file** — generates a boilerplate vertex shader matching Renderer2D's quad layout and combines it with the fragment stage.

**Case 3: Shadertoy-style file** — detects `mainImage` or `iTime`. Generates the vertex shader, injects `#define iTime u_Time` and `#define iResolution vec3(u_ViewportSize, 1.0)`, and wraps `mainImage` with a `void main()` bridge function.

**Case 4: Error fallback** — if no `#type` tags and no Shadertoy signatures, logs an error and returns an empty map.

### Uniform Location Caching

All `glGetUniformLocation` results are cached in `m_UniformLocationCache`. The first call per uniform name hits the driver; subsequent calls return the cached `GLint`. This eliminates per-frame driver queries for animated uniforms like `u_Time`.

---

## 30. RenderPass Stack — Implementation Details

This section is the internal companion to [Section 13 — RenderPass and Multi-Camera Rendering](#13-renderpass-and-multi-camera-rendering). It describes the data structures, ordering guarantees, and exact sequencing of every operation inside `PushRenderPass`, `PopRenderPass`, `FlushAndReset`, and the `BeginScene`/`EndScene` shims.

### The RenderPassState Struct

The pass stack is built from `Renderer2D::RenderPassState`, a plain struct defined in `Renderer2D.h` and stored as a member of `Renderer2DData`:

```cpp
struct RenderPassState
{
    glm::mat4 ViewProjectionMatrix { 1.0f };
    glm::vec4 ViewportBounds       { 0.0f, 0.0f, 1280.0f, 720.0f }; // x, y, width, height
};
```

The field `s_Data.RenderPassStack` is a `std::vector<RenderPassState>` used as a LIFO stack — entries are pushed to the back and popped from the back. The vector is pre-allocated at startup and cleared (but not deallocated) during `Renderer2D::Shutdown`.

The separate field `s_Data.ViewProjectionMatrix` is the **live** VP matrix that `Flush` reads when uploading `u_ViewProjection` to the shader. It is kept synchronized with `RenderPassStack.back().ViewProjectionMatrix` at all times.

Similarly, `s_Data.ViewportDimensions` (`glm::vec2`) is the live width/height used for `u_ViewportSize`. It is derived from the z/w components of `ViewportBounds` on every push and pop.

### PushRenderPass — Step-by-Step

```cpp
void Renderer2D::PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds)
```

The function executes the following in strict order:

**Step 1 — Flush pending geometry.**
Check whether any geometry from the currently active pass is staged but not yet submitted:

```cpp
bool hasPendingGeometry = (s_Data.QuadIndexCount   > 0 ||
                           s_Data.CircleIndexCount  > 0 ||
                           s_Data.LineVertexCount   > 0);
if (hasPendingGeometry) Flush();
```

This is the mechanism that guarantees isolation. Geometry submitted under Camera A is flushed with Camera A's VP matrix **before** Camera B's matrix is installed. Without this step, Camera B's matrix would be uploaded to the shader while Camera A's vertex data is still in the staging buffer.

**Step 2 — Build and push the new state.**

```cpp
RenderPassState newState;
newState.ViewProjectionMatrix = viewProj;
newState.ViewportBounds       = viewportBounds;
s_Data.RenderPassStack.push_back(newState);
```

**Step 3 — Install the new VP matrix.**

```cpp
s_Data.ViewProjectionMatrix = viewProj;
```

This directly updates the live field that `Flush` reads.

**Step 4 — Call `glViewport`.**

```cpp
glViewport(
    static_cast<int>(viewportBounds.x),
    static_cast<int>(viewportBounds.y),
    static_cast<int>(viewportBounds.z),
    static_cast<int>(viewportBounds.w)
);
```

The GPU's rasterization region is updated immediately. All draw calls inside this pass will rasterize into this pixel region of the bound framebuffer.

**Step 5 — Update `ViewportDimensions`.**

```cpp
s_Data.ViewportDimensions = { viewportBounds.z, viewportBounds.w };
```

This feeds the `u_ViewportSize` uniform in `Flush`. Shaders that use `iResolution` (mapped to `vec3(u_ViewportSize, 1.0)`) will correctly report the quadrant dimensions for this pass, not the full framebuffer size.

**Step 6 — Reset all batch counters.**

```cpp
s_Data.QuadIndexCount   = 0;
s_Data.QuadVertexPtr    = s_Data.QuadVertexBufferBase;
s_Data.TextureSlotIndex = 1;  // slot 0 is always the white texture
s_Data.CurrentMaterial  = s_Data.DefaultMaterial;

s_Data.LineVertexCount    = 0;
s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

s_Data.CircleIndexCount    = 0;
s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
```

The staging pointers and counters are reset to empty so the new pass starts accumulating from the beginning of the pre-allocated CPU buffers.

### PopRenderPass — Step-by-Step

```cpp
void Renderer2D::PopRenderPass()
```

**Step 1 — Assert on empty stack.**

```cpp
CS_CORE_ASSERT(!s_Data.RenderPassStack.empty(),
    "PopRenderPass called with an empty stack! Mismatched Push/Pop.");
```

This fires as a debug-build assert. In release builds there is no guard — a mismatched pop causes undefined batch state.

**Step 2 — Flush remaining geometry.**
Any draw calls submitted during this pass that have not yet been flushed (because `MaxIndices` was not hit) are submitted now:

```cpp
bool hasPendingGeometry = (s_Data.QuadIndexCount   > 0 ||
                           s_Data.CircleIndexCount  > 0 ||
                           s_Data.LineVertexCount   > 0);
if (hasPendingGeometry) Flush();
```

**Step 3 — Pop the current entry.**

```cpp
s_Data.RenderPassStack.pop_back();
```

**Step 4 — Restore prior state (if any).**
If the stack is non-empty after the pop, the previous pass's VP matrix and viewport bounds are reinstated:

```cpp
if (!s_Data.RenderPassStack.empty())
{
    const RenderPassState& restored = s_Data.RenderPassStack.back();
    s_Data.ViewProjectionMatrix = restored.ViewProjectionMatrix;

    glViewport(
        static_cast<int>(restored.ViewportBounds.x),
        static_cast<int>(restored.ViewportBounds.y),
        static_cast<int>(restored.ViewportBounds.z),
        static_cast<int>(restored.ViewportBounds.w)
    );

    s_Data.ViewportDimensions = { restored.ViewportBounds.z, restored.ViewportBounds.w };
}
```

If the stack is empty after the pop (the outermost pass just ended), the live VP matrix and viewport dimensions are left at their last-known values until the next `PushRenderPass` overwrites them. This is safe because no further `Flush` calls will occur in that state.

**Step 5 — Reset batch counters.**
Identical to the reset in `PushRenderPass`. The counters are zeroed so any geometry that arrives after the pop (in a subsequent pass or a new `BeginScene`) starts from a clean state.

### BeginScene and EndScene as Shims

`BeginScene` and `EndScene` are thin backward-compatibility wrappers over `PushRenderPass` / `PopRenderPass`. They are the correct choice for single-camera rendering and require no changes to existing code.

```cpp
void Renderer2D::BeginScene(const OrthographicCamera& camera)
{
    // Derive full-window bounds from the current tracked viewport dimensions
    glm::vec4 fullWindowBounds = {
        0.0f,
        0.0f,
        s_Data.ViewportDimensions.x,
        s_Data.ViewportDimensions.y
    };
    PushRenderPass(camera.GetViewProjectionMatrix(), fullWindowBounds);
}

void Renderer2D::EndScene()
{
    PopRenderPass();
}
```

The key detail: `BeginScene` derives the viewport bounds from `s_Data.ViewportDimensions`, which is kept in sync with the framebuffer size via `Renderer2D::SetViewportSize`. `Renderer::OnWindowResize` calls `SetViewportSize` whenever the window changes. This means `BeginScene` always produces a full-framebuffer pass targeting the current render target size without needing the caller to supply explicit bounds.

### FlushAndReset — Material Preservation Across Mid-Batch Flushes

`FlushAndReset` is called internally when a batch limit is hit (index count, texture slot count, or material change) mid-frame — before `EndScene` / `PopRenderPass`. It must not alter the render pass stack or the current VP matrix. It only flushes and resets the geometry staging buffers.

Critically, it **preserves `s_Data.CurrentMaterial`** across the reset:

```cpp
void Renderer2D::FlushAndReset()
{
    // Capture the active material before the reset wipes it
    Ref<Material> activeMaterial = s_Data.CurrentMaterial;

    Flush();

    // Reset geometry counters and staging pointers
    s_Data.QuadIndexCount    = 0;
    s_Data.QuadVertexPtr     = s_Data.QuadVertexBufferBase;
    s_Data.TextureSlotIndex  = 1;

    s_Data.LineVertexCount     = 0;
    s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

    s_Data.CircleIndexCount      = 0;
    s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

    // Restore the material so the next DrawQuad call continues into the
    // same material bucket without an unnecessary second FlushAndReset
    s_Data.CurrentMaterial = activeMaterial;
}
```

Without this restoration, the first `DrawQuad` after the mid-batch flush would see `CurrentMaterial == DefaultMaterial` and immediately trigger another `FlushAndReset` to "change" back to the actual material, even though no material change occurred. The preservation means `DrawQuad(material)` calls can span multiple underlying `FlushAndReset` cycles transparently.

Note that `FlushAndReset` does **not** call `BeginScene`, `EndScene`, `PushRenderPass`, or `PopRenderPass`. It operates entirely within the currently active render pass. The pass stack remains untouched.

### Stack Lifecycle Across a Multi-Camera Frame

To make the sequencing concrete, here is the full stack state for a two-camera frame:

```
Initial:  RenderPassStack = []

{
    RenderPass passA(camA, boundsA);
        // PushRenderPass:
        //   hasPendingGeometry = false → no flush
        //   push RenderPassState{camA.VP, boundsA}
        //   s_Data.ViewProjectionMatrix = camA.VP
        //   glViewport(boundsA)
        //   s_Data.ViewportDimensions = {boundsA.z, boundsA.w}
        //   reset all counters
        //   Stack: [ {camA.VP, boundsA} ]

    DrawQuad(...); DrawQuad(...); DrawLine(...);
        // Stack: [ {camA.VP, boundsA} ]   counters: QuadIndexCount=12, LineVertexCount=2

}   // ~RenderPass → PopRenderPass:
    //   hasPendingGeometry = true → Flush() with camA.VP active → draw calls go to GPU
    //   pop → Stack: []
    //   stack empty → no restoration
    //   reset all counters

{
    RenderPass passB(camB, boundsB);
        // PushRenderPass:
        //   hasPendingGeometry = false → no flush
        //   push RenderPassState{camB.VP, boundsB}
        //   s_Data.ViewProjectionMatrix = camB.VP
        //   glViewport(boundsB)
        //   reset all counters
        //   Stack: [ {camB.VP, boundsB} ]

    DrawQuad(...);

}   // ~RenderPass → PopRenderPass:
    //   Flush() with camB.VP → draw calls go to GPU
    //   pop → Stack: []
    //   reset all counters

Final: RenderPassStack = []
```

Geometry submitted under `passA` is always flushed with `camA.VP`. Geometry submitted under `passB` is always flushed with `camB.VP`. The two sets never mix.

---

## 31. Build System

The project uses CMake 3.21+ with a three-tier structure:

**Root `CMakeLists.txt`:** Sets global standards, defines `COSMIC_SDK_DIR`, adds `Cosmic/` and `Runtime/` subdirectories. Automatically discovers any project under `Projects/*/CMakeLists.txt` and adds it as a subdirectory.

**`Cosmic/CMakeLists.txt`:** Builds `Cosmic.dll`. Manages external dependencies (GLFW, GLAD, GLM, EnTT, ImGui, ImPlot, spdlog, stb_image). Sets `COSMIC_BUILD_DLL` so `Core.h` exports symbols.

**`Runtime/CMakeLists.txt`:** Builds `CosmicApp.exe`. Links against `Cosmic`.

**Project `CMakeLists.txt`:** Builds your game DLL. Uses an "agnostic linking" pattern — if `Cosmic` is a known CMake target (building from source), links against it directly. Otherwise imports `Cosmic.lib`/`Cosmic.dll` from the SDK build output.

`build_engine.bat` compiles only the engine core (`-DCOSMIC_BUILD_ENGINE_ONLY=ON`). Project `build.bat` files detect MSVC via `vswhere.exe`, resolve `COSMIC_SDK`, and run `cmake --build --config Debug --parallel`.

---

## 32. Event System — Implementation Details

### Event Generation: GLFW → Cosmic

Events originate from GLFW's C callback system. `Window.cpp` registers a callback for each hardware signal type. Each callback constructs the appropriate Cosmic event on the stack and calls `WindowData.EventCallback(event)`, which is bound to `Application::OnEvent`. The GLFW layer has no knowledge of layers or the dispatcher.

### Application::OnEvent — The Router

```cpp
void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowClose));
    dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowResize));

    for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
    {
        if (e.Handled) break;
        (*it)->OnEvent(e);
    }
}
```

Note that `OnWindowResize` returns `false`, so `WindowResizeEvent` still propagates to all layers after being handled here. This is intentional — all camera controllers need the resize notification.

### EventDispatcher Internals

`EventDispatcher::Dispatch<T>` is a function template that accepts any callable whose signature matches `bool(T&)`. This includes free functions, member function pointers bound with `std::bind`, and lambdas. Lambdas are the preferred form in client code because they are more readable and capture context explicitly.

```cpp
// Equivalent forms — all produce identical behavior
dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
    [this](Cosmic::KeyPressedEvent& e) { return OnKeyPressed(e); });       // lambda (preferred)

dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
    CS_BIND_EVENT_FN(MyLayer::OnKeyPressed));                               // CS_ macro

dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
    GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPressed));                           // GLCORE_ legacy alias
```

`Dispatch<T>()` returns `true` if the runtime event type matched `T::GetStaticType()`, regardless of whether your handler consumed it. What determines whether the event continues propagating down the stack is `e.Handled`, which is set to your handler's return value. Multiple `Dispatch` calls on the same dispatcher are fully independent — each checks the event type independently.

```cpp
template<typename T, typename F>
bool Dispatch(const F& func)
{
    if (m_Event.GetEventType() == T::GetStaticType())
    {
        m_Event.Handled = func(static_cast<T&>(m_Event));
        return true;  // type matched
    }
    return false;     // type did not match — event untouched
}
```

Because `F` is a template parameter resolved at compile time, the lambda form carries zero additional runtime overhead compared to the macro form.

### ImGuiLayer Event Handling

```cpp
void ImGuiLayer::OnEvent(Event& event)
{
    if (m_BlockEvents)
    {
        ImGuiIO& io = ImGui::GetIO();
        event.Handled |= event.IsInCategory(EventCategoryMouse)    & io.WantCaptureMouse;
        event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }
}
```

`m_BlockEvents` is toggled by `WorkspaceLayer` based on viewport focus. When the game viewport is focused, `BlockEvents(false)` — ImGui does not intercept anything. When an inspector panel is focused, `BlockEvents(true)` — mouse and keyboard events are swallowed.

### Known Event System Issues

**`WorkspaceLayer` forwards consumed events.** `WorkspaceLayer::OnEvent` does not check `e.Handled` before forwarding to the client layer. A consumed event should not reach further handlers. The fix:

```cpp
void WorkspaceLayer::OnEvent(Cosmic::Event& e)
{
    if (e.Handled) return; // should be added
    if (m_ClientViewportLayer)
        m_ClientViewportLayer->OnEvent(e);
}
```

**Camera controller never consumes events.** Both `OnMouseScrolled` and `OnWindowResized` in `OrthographicCameraController` return `false`. This is intentional — the owning layer decides whether to consume. If you have two cameras and want only one to respond to scroll, consume the event in your layer after forwarding.

**`WindowResizeEvent` is not consumed by Application.** `Application::OnWindowResize` does real work but returns `false`. The event propagates to all layers, which is intentional for camera controllers but means "application handled" and "propagation stopped" are not synonymous.

---

# Part III — Code Review

---

## 33. Refactor Candidates

### High Priority

**`Renderer2D` — code duplication across overloads.** `DrawQuad(material)` and `DrawRotatedQuad(material)` share ~80% identical code (texture key fallback sweep, slot lookup, vertex write loop). Should be refactored into a shared internal `WriteQuadVertices(transform, color, texIndex)` helper.

**`Application::Run` — length and responsibility.** The `Run()` method is ~150 lines and handles the frame loop, fixed timestep, layer iteration, ImGui, and Safe Zone DLL transition logic. The DLL transition logic should be extracted into `ProcessPendingTransitions()`.

**`LayerStack` documentation mismatch.** The header documentation for `PushLayer` describes `PushOverlay`'s behavior. Needs a documentation pass.

### Medium Priority

**`Scene::OnRender` — raw pointer key in hash map.** `std::unordered_map<Material*, ...>` uses raw pointer equality. This is functionally correct since materials are `Ref<>`-owned, but is fragile if material ownership ever changes.

**`WorkspaceLayer` — `firstTime` static local in `OnImGuiRender`.** When `WorkspaceLayer` is destroyed and recreated (which happens during transitions), the dockspace layout will not reinitialize on the second creation because the static persists. Should be a member variable.

**`LauncherLayer::GenerateProjectTemplate` — Win32 process spawning.** Uses `command.data()` (non-const) for `CreateProcessA` and silently proceeds if the process fails to launch. Should add proper error handling for the process exit code.

### Low Priority

**`RenderCommand` — raw pointer ownership.** `s_RendererAPI` is allocated with `new` and never `delete`d. Should use `Scope<RendererAPI>` to match the rest of the engine's ownership model.

**`OpenGLContext::Init` — no GLAD status check.** `gladLoadGLLoader` returns a status code that is stored but never checked. If GLAD fails, every subsequent `gl*` call dereferences null. Should assert on failure.

**`Window.cpp` — `glfwInit` without guard.** If `glfwCreateWindow` fails after a successful `glfwInit`, the destructor calls `glfwDestroyWindow(nullptr)`. Should add null checks.

---

## 34. Missing Implementations

### Missing Features

**`Renderer::EndScene` is empty.** Placeholder for future command sorting and batch submission optimization. (has a comment)

**`RendererAPI::DirectX` is a stub.** Returns `nullptr` everywhere; expected for now but worth tracking. (future thing to add)

### Missing Quality-of-Life Features

**No asset cache.** Loading the same path twice creates two separate GPU objects. A `std::unordered_map<string, Ref<Texture2D>>` cache would prevent redundant GPU allocations.

**No texture atlas / sprite sheet support.** No `SubTexture2D` or UV region API. Using a sprite sheet requires a custom material with adjusted UV uniforms.

**No audio system.** No audio interface exists.

**No scene serialization.** Entity/component data cannot be persisted to disk.

**No screen-to-world utility function.** Converting a mouse position to world-space coordinates via the camera's inverse VP matrix is a very commonly needed operation. Its absence forces every project to re-implement the math. A `Camera::ScreenToWorld(vec2)` helper would be valuable.

**No input action mapping.** Key codes are hardcoded. No rebindable action map.

**Linux/macOS support.** `SerialPort` is Windows-only. The DLL plugin system uses Win32 `LoadLibraryA`. Cross-platform support requires platform abstraction for dynamic library loading and serial I/O.

---

## 35. Technical Debt & Open Issues

**`Log.h` TODO items.** The header contains explicit TODO notes for writing logs to an output file and for better separation of client vs. engine log commands that have not been addressed.

**`Cosmic.h` extern "C" block.** The public header declares export symbols as if any file including it is implementing them. This should be moved to a separate `CosmicPlugin.h` that only plugin root files include.

**Static `s_SceneData` in `Renderer`.** `Renderer::s_SceneData` is allocated with `new Renderer::SceneData` at global scope and never `delete`d. Fine in practice, but violates the engine's ownership model.

**Hardcoded viewport dimensions.** Several simulation layers pass hardcoded `1280, 720` to viewport-dependent calculations. When the viewport is resized, `u_ViewportSize` becomes stale. The correct width/height should be tracked and passed dynamically from `OnWindowResize`.... Still partially true but most layers now use the framebuffer sync pattern

---

_README last updated to reflect codebase state as of May 2026._

## Newer Bugs

SpriteRendererComponent rotation degrees/radians mismatch in Scene::OnRender — transform.Rotation.z is stored in degrees but passed directly to DrawRotatedQuad which expects radians.

WorkspaceLayer::OnEvent forwards consumed events — no e.Handled check before forwarding to m_ClientViewportLayer.

Log.h TODO items still unaddressed — file sink not implemented, client/core log separation incomplete.

## The Cleaned Up Summary

| Entry                                  | Action                    |
| -------------------------------------- | ------------------------- |
| FlushAndReset calls EndScene           | DELETE — fixed            |
| Scene::OnUpdate is empty               | DELETE — fixed            |
| No DrawQuad vec2 material overload     | DELETE — fixed            |
| No SubTexture2D / sprite sheet support | DELETE — fixed            |
| Line.glsl preprocessor failure         | DELETE — fixed            |
| WorkspaceLayer firstTime static        | KEEP                      |
| LayerStack doc mismatch                | KEEP                      |
| Renderer2D overload duplication        | KEEP                      |
| Application::Run length                | KEEP                      |
| Scene::OnRender no camera              | KEEP + expand             |
| s_SceneData raw new                    | KEEP                      |
| Cosmic.h extern C block                | KEEP                      |
| Hardcoded viewports                    | UPDATE — mostly fixed     |
| Renderer::EndScene empty               | KEEP, clarify intentional |
| Degrees/radians bug in Scene           | ADD NEW                   |
| WorkspaceLayer event forwarding        | ADD NEW                   |
| Log file sink missing                  | ADD NEW                   |
