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
10. [Entity Component System](#10-entity-component-system)
11. [Camera System](#11-camera-system)
12. [Virtual File System](#12-virtual-file-system)
13. [Framebuffer](#13-framebuffer)
14. [Logging](#14-logging)
15. [Serial Communication](#15-serial-communication)
16. [The Showcase Project](#16-the-showcase-project)
17. [Complete API Reference Tables](#17-complete-api-reference-tables)

### Part II — Engine Internals

18. [Source File Map](#18-source-file-map)
19. [Hot-Reloadable DLL Architecture](#19-hot-reloadable-dll-architecture)
20. [Top-Down Time Propagation Waterfall](#20-top-down-time-propagation-waterfall)
21. [The Double-Tick Trap](#21-the-double-tick-trap)
22. [The OpenGL Graphics Pipeline](#22-the-opengl-graphics-pipeline)
23. [Hardware Abstraction Architecture](#23-hardware-abstraction-architecture)
24. [Batch Rendering Deep Dive](#24-batch-rendering-deep-dive)
25. [Shader Preprocessing System](#25-shader-preprocessing-system)
26. [Build System](#26-build-system)
27. [Event System — Implementation Details](#27-event-system--implementation-details)

### Part III — Code Review

28. [Refactor Candidates](#28-refactor-candidates)
29. [Missing Implementations](#29-missing-implementations)
30. [Technical Debt & Open Issues](#30-technical-debt--open-issues)

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

Override `OnEvent` and use `EventDispatcher` to route specific event types to dedicated handler functions:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPressed));

    dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnMouseClicked));

    dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnWindowResize));
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

### Forwarding Events to Sub-Systems

If your layer owns sub-systems that need events (like a camera controller or a simulation sub-layer), forward the event to them first, then check `e.Handled` before doing further work:

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    m_CameraController.OnEvent(e);
    if (e.Handled) return;

    Cosmic::EventDispatcher dispatcher(e);
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPressed));
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

| Event Class                | Useful Accessors                   | Notes                                   |
| -------------------------- | ---------------------------------- | --------------------------------------- |
| `KeyPressedEvent`          | `GetKeyCode()`, `GetRepeatCount()` | RepeatCount > 0 = key held              |
| `KeyReleasedEvent`         | `GetKeyCode()`                     | Fired once on key release               |
| `KeyTypedEvent`            | `GetKeyCode()`                     | Character input for text fields         |
| `MouseButtonPressedEvent`  | `GetMouseButton()`                 | Use `CS_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE` |
| `MouseButtonReleasedEvent` | `GetMouseButton()`                 |                                         |
| `MouseMovedEvent`          | `GetX()`, `GetY()`                 | Screen-space coordinates, top-left origin |
| `MouseScrolledEvent`       | `GetXOffset()`, `GetYOffset()`     | Y is typically ±1.0 per scroll tick     |
| `WindowResizeEvent`        | `GetWidth()`, `GetHeight()`        | Pixel dimensions of new window size     |
| `WindowCloseEvent`         | —                                  | Consumed by Application before layers  |

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

| Aspect                 | `OnUpdate(float ts)`                          | `OnFixedUpdate(float dt)`                        |
| ---------------------- | --------------------------------------------- | ------------------------------------------------ |
| **Purpose**            | Visual updates, animation, camera             | Physics, collision, deterministic simulation     |
| **Rate**               | Variable — depends on monitor refresh rate    | Fixed at 60 Hz regardless of frame rate          |
| **Input**              | Scaled variable delta-time in seconds         | Constant 1/60s interval (also scaled)            |
| **Rendering calls**    | Yes — call `BeginScene`/`EndScene` here       | No — never issue draw calls here                 |
| **Shader uniforms**    | Yes — update `u_Time`, `u_Color` etc. here    | No — GPU state should not be touched here        |
| **Anti-pattern**       | Running collision math that breaks at 144Hz   | Running sprite rotation or lerp animations       |
| **Timeline guards**    | `ts` is pre-scaled, no manual multiplication  | Check `dt <= 0.0f` to guard pause/rewind states  |

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

```cpp
Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}); // red
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.0f}, {1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}); // green, z-layered
```

### Textured Quads

```cpp
Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create("assets/sprite.png");

Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex);
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 2.0f); // 2x tiling
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, tex, 1.0f, {1.f, 0.5f, 0.5f, 1.f}); // tint
```

### Material Quads (Shader-driven)

```cpp
auto shader   = Cosmic::Shader::Create(Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl"));
auto material = Cosmic::Material::Create(shader, "FireMaterial");
material->Set("u_Color",  glm::vec4(1.f, 0.5f, 0.2f, 1.f));

// Update time every frame using layer's local clock
void MyLayer::OnUpdate(float ts)
{
    material->Set("u_Time", GetLocalTime());

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, material);
    Cosmic::Renderer2D::EndScene();
}
```

### Rotated Quads

```cpp
// rotation is in radians
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, glm::radians(45.f), {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, texture);
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotation, material);
```

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

```cpp
auto material = Cosmic::Material::Create(shader, "SpriteMaterial");

material->Set("u_Color",   glm::vec4(1.f, 0.8f, 0.2f, 1.f));
material->Set("u_Texture", myTexture);
material->Set("u_Time",    GetLocalTime()); // always use layer's local time

Cosmic::Renderer2D::DrawQuad(position, scale, material);
```

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

| Uniform            | Type          | Source                                                |
| ------------------ | ------------- | ----------------------------------------------------- |
| `u_ViewProjection` | `mat4`        | Camera VP matrix, updated per `BeginScene`            |
| `u_Time`           | `float`       | Set by your layer via `material->Set("u_Time", ...)`  |
| `u_ViewportSize`   | `vec2`        | Viewport pixel size, updated per `Flush`              |
| `u_Textures[32]`   | `sampler2D[]` | Batch renderer texture slots, auto-initialized        |

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

## 10. Entity Component System

Cosmic uses [EnTT](https://github.com/skypjack/entt) for its ECS. Entities are lightweight handles; components are plain structs. The `Scene` owns the entity registry.

### Creating Entities

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// CreateEntity auto-adds TransformComponent and TagComponent
Cosmic::Entity player = m_Scene->CreateEntity("Player");
Cosmic::Entity enemy  = m_Scene->CreateEntity("Enemy");
```

### Adding and Reading Components

```cpp
auto& sprite = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);
player.AddComponent<MyPhysicsComponent>(mass, drag);

auto& transform = player.GetComponent<Cosmic::TransformComponent>();
transform.Position   = {2.f, 0.5f, 0.f};
transform.Rotation.z = 45.f; // degrees, Z-axis
transform.Scale      = {1.f, 1.f};

if (player.HasComponent<MyPhysicsComponent>())
    player.RemoveComponent<MyPhysicsComponent>();
```

### Built-in Components

**`TransformComponent`**

```cpp
struct TransformComponent {
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll, degrees
    glm::vec2 Scale    { 1.f, 1.f };
    glm::mat4 GetTransform() const; // returns full TRS matrix
};
```

**`SpriteRendererComponent`**

```cpp
struct SpriteRendererComponent {
    Ref<Material> ActiveMaterial;
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f };
};
```

**`TagComponent`**

```cpp
struct TagComponent { std::string Tag; };
```

### Scene Update and Render

```cpp
void MyLayer::OnUpdate(float ts)
{
    m_Scene->OnUpdate(ts);

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    m_Scene->OnRender(); // batches entities by material, dispatches draw calls
    Cosmic::Renderer2D::EndScene();
}
```

### DLL Boundary Safety for Custom Components

If you define a custom component type that must be shared across the engine/game DLL boundary, add an `entt::type_hash` specialization in `Components.h`:

```cpp
namespace entt {
    template<>
    struct type_hash<MyCustomComponent> final {
        [[nodiscard]] static consteval id_type value() noexcept {
            return hashed_string::value("MyCustomComponent");
        }
    };
}
```

This ensures component IDs are stable regardless of compiler or translation unit.

---

## 11. Camera System

### Camera Controller (Recommended)

```cpp
Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f };

void OnUpdate(float ts) override
{
    m_Camera.OnUpdate(ts);
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    // draw...
    Cosmic::Renderer2D::EndScene();
}

void OnEvent(Cosmic::Event& e) override
{
    m_Camera.OnEvent(e); // handles scroll zoom + resize automatically
}
```

### Camera Controller Configuration

```cpp
m_Camera.SetZoomLimits(0.1f, 50.f);
m_Camera.SetZoomSpeed(0.15f);
m_Camera.SetTranslationSpeed(8.f);
m_Camera.SetZoomLevel(2.f);           // hard-snap (bypasses interpolation)
m_Camera.SetPositionLimits(-20.f, 20.f, -10.f, 10.f);
m_Camera.SetPosition({entity.x, entity.y, 0.f}); // entity follow
```

### Viewport Resize

Always handle window resize to prevent stretching:

```cpp
bool MyLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
{
    m_Camera.OnResize((float)e.GetWidth(), (float)e.GetHeight());
    return false; // don't consume
}
```

---

## 12. Virtual File System

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

## 13. Framebuffer

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

## 14. Logging

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

## 15. Serial Communication

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

## 16. The Showcase Project

The Showcase project (`Projects/Showcase/`) is the canonical reference implementation for building multi-mode simulations with Cosmic. It ships with the engine SDK and demonstrates the correct patterns for time management, material-driven rendering, ECS integration, and composite layer architecture.

### Overview

Showcase compiles to `Showcase.dll` and loads through the engine's standard DLL plugin system. When selected from the Launcher, it mounts into the `WorkspaceLayer` viewport and presents four interactive simulation modes switchable at runtime from the Inspector panel.

### Simulation Modes

| Mode                    | Layer Class              | Demonstrates                                              |
| ----------------------- | ------------------------ | --------------------------------------------------------- |
| **Flight**              | `ShowcaseFlightLayer`    | Entity selection via mouse click, trail rendering, ECS    |
| **Runner**              | `ShowcaseRunLayer`       | Fixed-timestep physics, procedural obstacle generation    |
| **Shader Browser**      | `ShowcaseShaderLayer`    | Runtime shader hot-reload, VFS directory scanning         |
| **ECS Stress Test**     | `ShowcaseStressLayer`    | Material bucket batching, 10,000+ entity grid rendering   |

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

## 17. Complete API Reference Tables

### Renderer2D

| Function          | Parameters                                                                   | Description                                           |
| ----------------- | ---------------------------------------------------------------------------- | ----------------------------------------------------- |
| `BeginScene`      | `const OrthographicCamera&`                                                  | Starts a batch pass, caches VP matrix, resets buffers |
| `EndScene`        | —                                                                            | Flushes all batched geometry to GPU                   |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, vec4 color`                                       | Flat-color quad                                       |
| `DrawQuad`        | `vec2/vec3 pos, vec2 size, Ref<Texture>, float tiling, vec4 tint`            | Textured quad                                         |
| `DrawQuad`        | `vec3 pos, vec2 size, Ref<Material>`                                         | Material/shader-driven quad                           |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, vec4 color`                            | Rotated flat quad (rot in radians)                    |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, Ref<Texture>, float tiling, vec4 tint` | Rotated textured quad                                 |
| `DrawRotatedQuad` | `vec3 pos, vec2 size, float rot, Ref<Material>`                              | Rotated material quad                                 |
| `DrawLine`        | `vec3 p0, vec3 p1, vec4 color`                                               | Line segment between two world-space points           |
| `DrawRect`        | `vec3 pos, vec2 size, vec4 color`                                            | Wireframe rectangle (4 lines)                         |
| `ResetStats`      | —                                                                            | Clears draw call and quad counters                    |
| `GetStats`        | —                                                                            | Returns `Statistics` struct                           |
| `SetStatsStatus`  | `bool enabled`                                                               | Toggle stats recording                                |

### Material

| Function           | Parameters                  | Description                                    |
| ------------------ | --------------------------- | ---------------------------------------------- |
| `Material::Create` | `Ref<Shader>, string name`  | Factory — creates a new material               |
| `Set`              | `string name, float`        | Set a scalar float uniform                     |
| `Set`              | `string name, vec3`         | Set a 3-component vector uniform               |
| `Set`              | `string name, vec4`         | Set a 4-component vector uniform               |
| `Set`              | `string name, Ref<Texture>` | Bind a texture to a named slot                 |
| `GetFloat`         | `string name`               | Retrieve cached float (0.0 if missing)         |
| `GetVector`        | `string name`               | Retrieve cached vec4 (white if missing)        |
| `GetTexture`       | `string name`               | Retrieve cached texture (nullptr if missing)   |
| `Bind`             | —                           | Binds shader and uploads all cached uniforms   |
| `GetShader`        | —                           | Returns the underlying `Ref<Shader>`           |
| `HasFloat`         | `string name`               | Returns true if the float uniform is set       |

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

| Function                    | Description                                                          |
| --------------------------- | -------------------------------------------------------------------- |
| `GetLocalTime()`            | Returns accumulated scaled time for this layer (seconds)            |
| `SetLocalTime(float)`       | Directly set the time accumulator (e.g. for level reset)            |
| `GetTimeScale()`            | Returns this layer's local time scale multiplier                    |
| `SetTimeScale(float)`       | Set per-layer time scale (independent of global `Application` scale) |
| `UpdateLayerTime(float dt)` | Called by the engine each frame — accumulates `dt * m_LocalTimeScale` |

### Application Time API

| Function                    | Description                                          |
| --------------------------- | ---------------------------------------------------- |
| `Get().SetTimeScale(float)` | Set global time scale affecting all layers           |
| `Get().GetTimeScale()`      | Read current global scale                            |
| `Get().GetAbsoluteTime()`   | Total unscaled elapsed time in seconds               |
| `Get().UseFixedTimeStep(bool)` | Enable/disable 60Hz fixed update pass             |

### Scene / Entity

| Function                     | Parameters                      | Description                                              |
| ---------------------------- | ------------------------------- | -------------------------------------------------------- |
| `Scene::Create`              | —                               | Factory — creates a new scene                            |
| `Scene::CreateEntity`        | `string name = "GenericEntity"` | Creates entity; auto-adds Transform + Tag                |
| `Scene::DestroyEntity`       | `Entity`                        | Removes entity from registry                             |
| `Scene::OnUpdate`            | `float dt`                      | Tick scene logic                                         |
| `Scene::OnRender`            | —                               | Dispatches all entities to Renderer2D by material bucket |
| `Entity::AddComponent<T>`    | `Args...`                       | Construct and attach component (asserts if duplicate)    |
| `Entity::GetComponent<T>`    | —                               | Returns reference (asserts if missing)                   |
| `Entity::HasComponent<T>`    | —                               | Returns bool                                             |
| `Entity::RemoveComponent<T>` | —                               | Removes component (asserts if missing)                   |
| `Entity::operator bool`      | —                               | True if handle is valid and scene-bound                  |

### OrthographicCameraController

| Function                       | Parameters                                 | Description                             |
| ------------------------------ | ------------------------------------------ | --------------------------------------- |
| `OrthographicCameraController` | `float aspectRatio, bool rotation = false` | Constructor                             |
| `OnUpdate`                     | `float ts`                                 | WASD + smooth zoom interpolation        |
| `OnEvent`                      | `Event&`                                   | Routes scroll and resize events         |
| `OnResize`                     | `float w, float h`                         | Recalculate aspect ratio                |
| `SetZoomLevel`                 | `float`                                    | Hard-snap zoom (bypasses interpolation) |
| `SetZoomLimits`                | `float min, float max`                     | Clamp scroll zoom range                 |
| `SetZoomSpeed`                 | `float`                                    | Speed per scroll tick                   |
| `SetTranslationSpeed`          | `float`                                    | Pan speed (multiplied by zoom level)    |
| `SetPositionLimits`            | `float minX, maxX, minY, maxY`             | Camera pan bounds                       |
| `SetPosition`                  | `vec3`                                     | Force camera position                   |
| `GetCamera`                    | —                                          | Returns `OrthographicCamera&`           |

### RenderCommand

| Function        | Parameters                               | Description                                        |
| --------------- | ---------------------------------------- | -------------------------------------------------- |
| `SetClearColor` | `vec4 color`                             | Set RGBA background clear color                    |
| `Clear`         | —                                        | Clear color + depth buffers                        |
| `Clear`         | `float r, float g, float b`              | Set color and clear in one call                    |
| `SetViewport`   | `uint32_t x, y, w, h`                    | Map NDC to window pixels                           |
| `DrawIndexed`   | `Ref<VertexArray>, uint32_t count = 0`   | Indexed draw call                                  |
| `DrawLines`     | `Ref<VertexArray>, uint32_t vertexCount` | Non-indexed line draw                              |

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

# Part II — Engine Internals

---

## 18. Source File Map

### `src/core/`

| File                 | Purpose                                                                                                                                                          |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Core.h`             | Universal foundation — `Scope<T>`, `Ref<T>`, `COSMIC_API` DLL macros, `BIT()`, `GLCORE_ASSERT`, `GLCORE_BIND_EVENT_FN`. Included first in nearly every file.     |
| `Application.h/.cpp` | Root singleton. Owns the window, renderer, framebuffer, ImGui, LayerStack. Drives the frame loop with fixed + variable timestep passes. Manages launcher↔workspace transitions and DLL plugin loading. |
| `Layer.h`            | Abstract base for all engine components. Declares all lifecycle hooks and owns the per-layer timeline state (`m_LocalTime`, `m_LocalTimeScale`).                  |
| `LayerStack.h/.cpp`  | Ordered container of `Layer*` borrows. Manages insertion boundary between game layers and overlays.                                                              |
| `Window.h/.cpp`      | GLFW window wrapper. Handles creation, event callbacks, VSync, buffer swapping.                                                                                  |
| `Input.h/.cpp`       | Static polling interface. Wraps GLFW key and mouse queries.                                                                                                      |
| `Log.h/.cpp`         | spdlog wrapper. Initializes engine + client loggers.                                                                                                             |
| `Timestep.h`         | Thin float wrapper for delta-time. Prevents seconds/milliseconds confusion.                                                                                     |

### `src/layers/`

| File                    | Purpose                                                                                                                                                               |
| ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ImGuiLayer.h/.cpp`     | Initializes ImGui + ImPlot, GLFW backend, OpenGL3 backend. Manages event blocking.                                                                                    |
| `WorkspaceLayer.h/.cpp` | The editor shell. Hosts an ImGui dockspace with Viewport + Inspector panels. Owns the client DLL layer slot. Drives the framebuffer render loop and forwards time down. |
| `LauncherLayer.h/.cpp`  | Startup hub. Scans for `.dll` files, renders project list, hosts the project creation wizard.                                                                         |

### `src/renderer/`

| File                   | Purpose                                                                                                        |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| `RendererAPI.h/.cpp`   | Abstract backend interface. Holds static API selection flag.                                                   |
| `RenderCommand.h/.cpp` | Static dispatcher forwarding to the active `RendererAPI*` instance.                                            |
| `Renderer.h/.cpp`      | High-level 3D/static orchestrator. Manages per-scene VP matrix.                                               |
| `Renderer2D.h/.cpp`    | High-performance 2D batch renderer. Manages vertex/index staging buffers (up to 10,000 quads), texture slots (up to 32), material state, and line rendering. |

### `src/graphics/`

| File                 | Purpose                                                                                                                                     |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `Buffer.h/.cpp`      | `ShaderDataType`, `BufferElement`, `BufferLayout` (stride calculator). Abstract `VertexBuffer` and `IndexBuffer` interfaces.                |
| `VertexArray.h/.cpp` | Abstract VAO interface.                                                                                                                     |
| `Shader.h/.cpp`      | Abstract GPU program interface. Uniform set methods. Factory `Create` from filepath.                                                        |
| `Texture.h/.cpp`     | `Texture` base and `Texture2D` interfaces. Factory `Create`.                                                                               |
| `Material.h/.cpp`    | Pairs `Ref<Shader>` with named uniform caches. `Bind()` uploads all cached uniforms.                                                       |
| `FrameBuffer.h/.cpp` | Abstract FBO interface. Factory `Create`.                                                                                                   |
| `GraphicsContext.h`  | Two-method interface: `Init()` and `SwapBuffers()`.                                                                                        |

### `src/platform/opengl/`

| File                       | Purpose                                                                                                                        |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `OpenGLContext.h/.cpp`     | `glfwMakeContextCurrent` + GLAD initialization. `SwapBuffers` via `glfwSwapBuffers`.                                           |
| `OpenGLRendererAPI.h/.cpp` | Concrete `RendererAPI` for OpenGL. Init enables blending + depth test.                                                         |
| `OpenGLBuffer.h/.cpp`      | Dynamic (`GL_DYNAMIC_DRAW`) and static (`GL_STATIC_DRAW`) vertex buffers. Index buffer.                                        |
| `OpenGLVertexArray.h/.cpp` | Generates VAO, calls `glVertexAttribPointer` for each layout element.                                                          |
| `OpenGLShader.h/.cpp`      | Reads `.glsl` from disk, preprocesses `#type` directives, compiles and links. Uniform location cache.                         |
| `OpenGLTexture.h/.cpp`     | File-based loader using stb_image. Procedural empty texture. `SetData` via `glTexSubImage2D`.                                  |
| `OpenGLFrameBuffer.h/.cpp` | FBO with RGBA8 color attachment + Depth24_Stencil8 depth attachment. `Invalidate()` tears down and reallocates on resize.       |

### `src/scene/`

| File           | Purpose                                                                                                                                                          |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Scene.h/.cpp` | Owns `entt::registry`. `OnRender` sorts entities into material buckets before dispatching to `Renderer2D`.                                                       |
| `Entity.h`     | Lightweight handle (`entt::entity` + `Scene*`). Template `AddComponent`, `GetComponent`, `HasComponent`, `RemoveComponent`.                                      |
| `Components.h` | `TagComponent`, `TransformComponent`, `SpriteRendererComponent`. Includes `entt::type_hash` specializations for DLL boundary safety.                            |

---

## 19. Hot-Reloadable DLL Architecture

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

## 20. Top-Down Time Propagation Waterfall

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

## 21. The Double-Tick Trap

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

## 22. The OpenGL Graphics Pipeline

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

## 23. Hardware Abstraction Architecture

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

## 24. Batch Rendering Deep Dive

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

## 25. Shader Preprocessing System

`OpenGLShader::PreProcess` handles four cases:

**Case 1: Standard multi-stage file** (has `#type vertex` and `#type fragment`) — splits source at `#type` boundaries, injects missing uniform declarations per stage.

**Case 2: Fragment-only file** — generates a boilerplate vertex shader matching Renderer2D's quad layout and combines it with the fragment stage.

**Case 3: Shadertoy-style file** — detects `mainImage` or `iTime`. Generates the vertex shader, injects `#define iTime u_Time` and `#define iResolution vec3(u_ViewportSize, 1.0)`, and wraps `mainImage` with a `void main()` bridge function.

**Case 4: Error fallback** — if no `#type` tags and no Shadertoy signatures, logs an error and returns an empty map.

### Uniform Location Caching

All `glGetUniformLocation` results are cached in `m_UniformLocationCache`. The first call per uniform name hits the driver; subsequent calls return the cached `GLint`. This eliminates per-frame driver queries for animated uniforms like `u_Time`.

---

## 26. Build System

The project uses CMake 3.21+ with a three-tier structure:

**Root `CMakeLists.txt`:** Sets global standards, defines `COSMIC_SDK_DIR`, adds `Cosmic/` and `Runtime/` subdirectories. Automatically discovers any project under `Projects/*/CMakeLists.txt` and adds it as a subdirectory.

**`Cosmic/CMakeLists.txt`:** Builds `Cosmic.dll`. Manages external dependencies (GLFW, GLAD, GLM, EnTT, ImGui, ImPlot, spdlog, stb_image). Sets `COSMIC_BUILD_DLL` so `Core.h` exports symbols.

**`Runtime/CMakeLists.txt`:** Builds `CosmicApp.exe`. Links against `Cosmic`.

**Project `CMakeLists.txt`:** Builds your game DLL. Uses an "agnostic linking" pattern — if `Cosmic` is a known CMake target (building from source), links against it directly. Otherwise imports `Cosmic.lib`/`Cosmic.dll` from the SDK build output.

`build_engine.bat` compiles only the engine core (`-DCOSMIC_BUILD_ENGINE_ONLY=ON`). Project `build.bat` files detect MSVC via `vswhere.exe`, resolve `COSMIC_SDK`, and run `cmake --build --config Debug --parallel`.

---

## 27. Event System — Implementation Details

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

`Dispatch<T>()` returns `true` if the event type matched, regardless of whether your handler consumed it. What matters for propagation is `e.Handled`, which is set to your handler's return value. Multiple `Dispatch` calls on the same dispatcher are independent.

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

## 28. Refactor Candidates

### High Priority

**`OpenGLShader::PreProcess` — complexity and fragility.** The preprocessor is doing too much in one 300-line function. The comment-stripping logic (removing `/* */` and `//` before scanning for uniform names) is hand-written parsing that will break on edge cases (multi-line strings, conditional compilation). Consider splitting into `StripComments`, `ExtractStage`, `InjectPreamble` functions.

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

## 29. Missing Implementations

### Critical Missing Features

**`Scene::OnUpdate` is empty.** There is no ECS system dispatch or component-driven update loop. All game logic currently runs in the layer's `OnUpdate` by manually fetching components. A proper ECS system (visitor/view-based update callbacks) is missing.

**`Renderer2D` — no `DrawQuad(vec2, vec2, Ref<Material>)` overload.** The 3D vec3 overload exists but the 2D convenience overload for materials is missing, unlike the color and texture variants which have both.

**`Renderer::EndScene` is empty.** Placeholder for future command sorting and batch submission optimization.

**`RendererAPI::DirectX` is a stub.** Returns `nullptr` everywhere; expected for now but worth tracking.

### Missing Quality-of-Life Features

**No asset cache.** Loading the same path twice creates two separate GPU objects. A `std::unordered_map<string, Ref<Texture2D>>` cache would prevent redundant GPU allocations.

**No texture atlas / sprite sheet support.** No `SubTexture2D` or UV region API. Using a sprite sheet requires a custom material with adjusted UV uniforms.

**No audio system.** No audio interface exists.

**No scene serialization.** Entity/component data cannot be persisted to disk.

**No screen-to-world utility function.** Converting a mouse position to world-space coordinates via the camera's inverse VP matrix is a very commonly needed operation. Its absence forces every project to re-implement the math. A `Camera::ScreenToWorld(vec2)` helper would be valuable.

**No input action mapping.** Key codes are hardcoded. No rebindable action map.

**Linux/macOS support.** `SerialPort` is Windows-only. The DLL plugin system uses Win32 `LoadLibraryA`. Cross-platform support requires platform abstraction for dynamic library loading and serial I/O.

---

## 30. Technical Debt & Open Issues

**`Log.h` TODO items.** The header contains explicit TODO notes for writing logs to an output file and for better separation of client vs. engine log commands that have not been addressed.

**`Cosmic.h` extern "C" block.** The public header declares export symbols as if any file including it is implementing them. This should be moved to a separate `CosmicPlugin.h` that only plugin root files include.

**`Line.glsl` preprocessor failure.** The `Line.glsl` built-in shader in the Showcase project has `// #type vertex` commented out, which means it hits the preprocessor's Shadertoy fallback path and will fail silently. The `//` prefixes on `#type` directives must be removed.

**`Renderer2D::FlushAndReset` calls `EndScene` instead of `Flush`.** `FlushAndReset` calls `EndScene()` which calls `Flush()`. It works, but `EndScene` is being invoked mid-batch, which is semantically confusing. Should call `Flush()` directly.

**Static `s_SceneData` in `Renderer`.** `Renderer::s_SceneData` is allocated with `new Renderer::SceneData` at global scope and never `delete`d. Fine in practice, but violates the engine's ownership model.

**Hardcoded viewport dimensions.** Several simulation layers pass hardcoded `1280, 720` to viewport-dependent calculations. When the viewport is resized, `u_ViewportSize` becomes stale. The correct width/height should be tracked and passed dynamically from `OnWindowResize`.

---

_README last updated to reflect codebase state as of May 2026._
