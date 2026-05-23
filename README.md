# Cosmic Engine — Complete Developer Reference

> **How to use this document:** This README is split into two major parts. **Part I** is for anyone building games or simulations with Cosmic — it covers every API you'll interact with, with concrete examples and a full command reference table. **Part II** is an internal technical deep-dive covering every source file, the OpenGL graphics pipeline, and architectural decisions. At the end is a code review with items flagged for refactor, missing implementations, and technical debt.

<!-- RECOMMENDED GRAPHIC: Full engine architecture overview diagram — boxes for Application, LayerStack, Renderer2D, RenderCommand, RendererAPI, OpenGL Backend, Scene/ECS, Window/GLFW, and DLL Plugin System, with arrows showing data flow -->

---

## Table of Contents

### Part I — Client Developer Guide
1. [Getting Started](#1-getting-started)
2. [Memory Management](#2-memory-management)
3. [Application Lifecycle](#3-application-lifecycle)
4. [The Layer System](#4-the-layer-system)
5. [The Event System](#5-the-event-system)
6. [Input Polling](#6-input-polling)
7. [2D Rendering API](#7-2d-rendering-api)
8. [Materials and Shaders](#8-materials-and-shaders)
9. [Entity Component System](#9-entity-component-system)
10. [Camera System](#10-camera-system)
11. [Virtual File System](#11-virtual-file-system)
12. [Framebuffer](#12-framebuffer)
13. [Logging](#13-logging)
14. [Serial Communication](#14-serial-communication)
15. [Complete API Reference Tables](#15-complete-api-reference-tables)

### Part II — Engine Internals
16. [Source File Map](#16-source-file-map)
17. [The OpenGL Graphics Pipeline](#17-the-opengl-graphics-pipeline)
18. [Hardware Abstraction Architecture](#18-hardware-abstraction-architecture)
19. [Batch Rendering Deep Dive](#19-batch-rendering-deep-dive)
20. [The DLL Plugin System](#20-the-dll-plugin-system)
21. [Shader Preprocessing System](#21-shader-preprocessing-system)
22. [Build System](#22-build-system)

### Part III — Code Review
23. [Refactor Candidates](#23-refactor-candidates)
24. [Missing Implementations](#24-missing-implementations)
25. [Technical Debt & Open Issues](#25-technical-debt--open-issues)

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

| Alias | Underlying Type | Rule |
|-------|----------------|------|
| `Scope<T>` | `std::unique_ptr<T>` | **Single owner.** One system holds and destroys this. Use for windows, layers, dedicated sub-modules. |
| `Ref<T>` | `std::shared_ptr<T>` | **Shared owner.** Multiple systems hold a reference; destroyed when the last holder releases it. Use for textures, shaders, materials, scenes. |

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
    for each layer: layer->OnFixedUpdate(1/60s)
    │
    ▼
Pass 1B — Variable Timestep (frame rate dependent)
    for each layer: layer->OnUpdate(scaledDeltaTime)
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

Events are reactive signals fired by OS hardware and propagated top-to-bottom through the `LayerStack`. Any layer can consume an event to stop it propagating further.

<!-- RECOMMENDED GRAPHIC: Event flow diagram — OS signal → Application::OnEvent → LayerStack top-to-bottom, with "Handled?" check at each layer -->

### Handling Events

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher dispatcher(e);

    // Route specific event types to dedicated handlers
    dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnKeyPressed));

    dispatcher.Dispatch<Cosmic::MouseScrolledEvent>(
        GLCORE_BIND_EVENT_FN(MyLayer::OnMouseScrolled));
}

// Return true to consume the event (stops propagation to lower layers)
// Return false to pass it down
bool MyLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
{
    if (e.GetKeyCode() == CS_KEY_ESCAPE)
    {
        TogglePauseMenu();
        return true; // consumed
    }
    return false;
}

bool MyLayer::OnMouseScrolled(Cosmic::MouseScrolledEvent& e)
{
    m_Camera.Zoom(e.GetYOffset());
    return false;
}
```

### Blocking events from lower layers

```cpp
void UILayer::OnEvent(Cosmic::Event& e)
{
    // If the cursor is over an ImGui panel, eat mouse events
    // so the game world doesn't fire a weapon behind the UI
    if (m_PanelHovered && e.IsInCategory(Cosmic::EventCategoryMouse))
        e.Handled = true;
}
```

### Event Type Reference

| Event Class | Category Flags | Key Data |
|------------|---------------|----------|
| `WindowResizeEvent` | `EventCategoryApplication` | `GetWidth()`, `GetHeight()` |
| `WindowCloseEvent` | `EventCategoryApplication` | — |
| `KeyPressedEvent` | `EventCategoryKeyboard \| EventCategoryInput` | `GetKeyCode()`, `GetRepeatCount()` |
| `KeyReleasedEvent` | `EventCategoryKeyboard \| EventCategoryInput` | `GetKeyCode()` |
| `KeyTypedEvent` | `EventCategoryKeyboard \| EventCategoryInput` | `GetKeyCode()` (character value) |
| `MouseMovedEvent` | `EventCategoryMouse \| EventCategoryInput` | `GetX()`, `GetY()` |
| `MouseScrolledEvent` | `EventCategoryMouse \| EventCategoryInput` | `GetXOffset()`, `GetYOffset()` |
| `MouseButtonPressedEvent` | `EventCategoryMouse \| EventCategoryInput` | `GetMouseButton()` |
| `MouseButtonReleasedEvent` | `EventCategoryMouse \| EventCategoryInput` | `GetMouseButton()` |

---

## 6. Input Polling

For continuous per-frame input (movement, camera pan), use the static `Input` class instead of the event system.

```cpp
void MyLayer::OnUpdate(float ts)
{
    // Keyboard
    if (Cosmic::Input::IsKeyPressed(CS_KEY_W))
        m_Position.y += m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_S))
        m_Position.y -= m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_A))
        m_Position.x -= m_Speed * ts;
    if (Cosmic::Input::IsKeyPressed(CS_KEY_D))
        m_Position.x += m_Speed * ts;

    // Mouse button
    if (Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
        SpawnEntityAt(Cosmic::Input::GetMousePosition());

    // Mouse position
    glm::vec2 cursor = Cosmic::Input::GetMousePosition();
    float mx = Cosmic::Input::GetMouseX();
    float my = Cosmic::Input::GetMouseY();
}
```

### When to use Input vs. Events

| Use `Input::` | Use `OnEvent` |
|--------------|--------------|
| Continuous hold checks (movement, camera) | Single-press reactions (menu toggle, fire once) |
| Per-frame polling loops | State change notifications |
| "Is this key held right now?" | "Did the user just press Escape?" |

### Key Code Constants

Defined in `codes/KeyCodes.h`. Full list in the reference table in Section 15.

| Constant | Value | Constant | Value |
|----------|-------|----------|-------|
| `CS_KEY_SPACE` | 32 | `CS_KEY_ESCAPE` | 256 |
| `CS_KEY_A`–`CS_KEY_Z` | 65–90 | `CS_KEY_ENTER` | 257 |
| `CS_KEY_0`–`CS_KEY_9` | 48–57 | `CS_KEY_LEFT_CONTROL` | 341 |
| `CS_KEY_UP` | 265 | `CS_KEY_LEFT_SHIFT` | 340 |
| `CS_KEY_DOWN` | 264 | `CS_KEY_LEFT_ALT` | 342 |
| `CS_KEY_LEFT` | 263 | `CS_KEY_F1`–`CS_KEY_F12` | 290–301 |
| `CS_KEY_RIGHT` | 262 | `CS_KEY_TAB` | 258 |

### Mouse Button Constants

| Constant | Alias | Button |
|----------|-------|--------|
| `CS_MOUSE_BUTTON_LEFT` | `CS_MOUSE_BUTTON_1` | Primary action |
| `CS_MOUSE_BUTTON_RIGHT` | `CS_MOUSE_BUTTON_2` | Secondary / context |
| `CS_MOUSE_BUTTON_MIDDLE` | `CS_MOUSE_BUTTON_3` | Pan / zoom |

---

## 7. 2D Rendering API

`Renderer2D` is the primary drawing interface. It batches geometry internally to minimize GPU draw calls.

### Frame Structure

Every render pass must be wrapped in `BeginScene` / `EndScene`:

```cpp
void MyLayer::OnUpdate(float ts)
{
    // Required if your shaders use u_Time or u_ViewportSize
    Cosmic::Renderer2D::UpdateTimeline(ts, viewportWidth, viewportHeight);

    Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

    // --- submit all draw calls here ---

    Cosmic::Renderer2D::EndScene(); // flushes all batched data to GPU
}
```

### Flat Color Quads

```cpp
// 2D position overload
Cosmic::Renderer2D::DrawQuad({0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}); // red

// 3D position (z controls layering)
Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.0f}, {1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}); // green
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
auto shader   = Cosmic::Shader::Create("project://shaders/Fire.glsl");
auto material = Cosmic::Material::Create(shader, "FireMaterial");
material->Set("u_Color",  glm::vec4(1.f, 0.5f, 0.2f, 1.f));
material->Set("u_Time",   accumulatedTime);

Cosmic::Renderer2D::DrawQuad({0.f, 0.f, 0.f}, {2.f, 2.f}, material);
```

### Rotated Quads

```cpp
float rotationRadians = glm::radians(45.f);

Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotationRadians, {1.f, 1.f, 0.f, 1.f});
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotationRadians, tex);
Cosmic::Renderer2D::DrawRotatedQuad({0.f, 0.f, 0.f}, {1.f, 1.f}, rotationRadians, material);
```

### Debug Geometry

```cpp
// Line between two world-space points
Cosmic::Renderer2D::DrawLine({-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 1.f});

// Wireframe rectangle
Cosmic::Renderer2D::DrawRect({0.f, 0.f, 0.f}, {2.f, 1.f}, {0.f, 1.f, 1.f, 1.f});
```

### Performance Statistics

```cpp
// Reset at the start of each frame
Cosmic::Renderer2D::ResetStats();

// Read at the end of the frame / in OnImGuiRender
Cosmic::Renderer2D::Statistics stats = Cosmic::Renderer2D::GetStats();
ImGui::Text("Draw Calls: %d", stats.DrawCalls);
ImGui::Text("Quads:      %d", stats.QuadCount);
ImGui::Text("Vertices:   %d", stats.GetTotalVertexCount());
ImGui::Text("Indices:    %d", stats.GetTotalIndexCount());
```

---

## 8. Materials and Shaders

A **Shader** is a GPU program. A **Material** is a shader plus a named set of parameter values (uniforms). Materials allow multiple objects to share the same shader with different visual properties.

### Loading a Shader

```cpp
// Single-file .glsl with #type directives (see Section 21 for format)
Ref<Cosmic::Shader> shader = Cosmic::Shader::Create("project://shaders/MyShader.glsl");
```

### Creating and Using a Material

```cpp
auto shader   = Cosmic::Shader::Create("project://shaders/Sprite.glsl");
auto material = Cosmic::Material::Create(shader, "SpriteMaterial");

// Set parameters
material->Set("u_Color",   glm::vec4(1.f, 0.8f, 0.2f, 1.f));
material->Set("u_Texture", myTexture);
material->Set("u_Time",    accumulatedTime);    // update every frame

// Use in drawing
Cosmic::Renderer2D::DrawQuad(position, scale, material);
```

### Updating Per-Frame Uniforms

```cpp
void MyLayer::OnUpdate(float ts)
{
    m_AccumulatedTime += ts;
    m_Material->Set("u_Time", m_AccumulatedTime);

    Cosmic::Renderer2D::UpdateTimeline(ts, 1280, 720); // also feeds u_Time/u_ViewportSize
}
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

| Uniform | Type | Source |
|---------|------|--------|
| `u_ViewProjection` | `mat4` | Camera VP matrix, updated per `BeginScene` |
| `u_Time` | `float` | Accumulated engine time, updated via `UpdateTimeline` |
| `u_ViewportSize` | `vec2` | Viewport pixel size, updated via `UpdateTimeline` |
| `u_Textures[32]` | `sampler2D[]` | Batch renderer texture slots, auto-initialized |

### Shadertoy Compatibility

If your file contains `void mainImage(out vec4 fragColor, in vec2 fragCoord)`, the preprocessor automatically wraps it to run on the engine's batch quad system. Just paste Shadertoy code in a `.glsl` file and load it as a material.

```glsl
// No #type tags needed — the engine detects mainImage and handles everything
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
```

---

## 9. Entity Component System

Cosmic uses [EnTT](https://github.com/skypjack/entt) for its ECS. Entities are lightweight handles; components are plain structs. The `Scene` owns the entity registry.

<!-- RECOMMENDED GRAPHIC: ECS diagram showing Scene → entt::registry → Entity handles → Components (Transform, Sprite, Tag, custom) -->

### Creating Entities

```cpp
Ref<Cosmic::Scene> m_Scene = Cosmic::Scene::Create();

// CreateEntity auto-adds TransformComponent and TagComponent
Cosmic::Entity player  = m_Scene->CreateEntity("Player");
Cosmic::Entity enemy   = m_Scene->CreateEntity("Enemy");
Cosmic::Entity unnamed = m_Scene->CreateEntity(); // name defaults to "GenericEntity"
```

### Adding and Reading Components

```cpp
// Add a component (asserts if entity already has it)
auto& sprite = player.AddComponent<Cosmic::SpriteRendererComponent>(myMaterial);

// Add a custom component
player.AddComponent<MyPhysicsComponent>(mass, drag);

// Read a component (asserts if entity doesn't have it)
auto& transform = player.GetComponent<Cosmic::TransformComponent>();
transform.Position  = {2.f, 0.5f, 0.f};
transform.Rotation.z = 45.f; // degrees, Z-axis
transform.Scale      = {1.f, 1.f};

// Safe check before access
if (player.HasComponent<MyPhysicsComponent>())
    player.GetComponent<MyPhysicsComponent>().ApplyForce({0.f, 9.8f, 0.f});

// Remove
player.RemoveComponent<MyPhysicsComponent>();
```

### Built-in Components

**`TransformComponent`** — spatial placement:

```cpp
struct TransformComponent {
    glm::vec3 Position { 0.f, 0.f, 0.f };
    glm::vec3 Rotation { 0.f, 0.f, 0.f }; // Z = 2D roll, in degrees
    glm::vec2 Scale    { 1.f, 1.f };

    glm::mat4 GetTransform() const; // returns TRS matrix
};
```

**`SpriteRendererComponent`** — visual data:

```cpp
struct SpriteRendererComponent {
    Ref<Material> ActiveMaterial;               // shader-driven rendering
    glm::vec4     Color { 1.f, 1.f, 1.f, 1.f }; // flat-color fallback
};
```

**`TagComponent`** — debug identity string:

```cpp
struct TagComponent { std::string Tag; };
```

### Defining Custom Components

```cpp
// In your DLL source — these are private to your project
struct MyPhysicsComponent {
    float Mass  = 1.f;
    float Drag  = 0.1f;
    glm::vec3 Velocity { 0.f, 0.f, 0.f };
};

// Usage
player.AddComponent<MyPhysicsComponent>();
auto& phys = player.GetComponent<MyPhysicsComponent>();
phys.Velocity += {0.f, -9.8f * ts, 0.f};
```

> **DLL boundary note:** If you define a custom component and need to access it from both the engine DLL and your project DLL, you must add an `entt::type_hash` specialization in `Components.h`. See Section 16 for details.

### Scene Update and Render

```cpp
void MyLayer::OnUpdate(float ts)
{
    m_Scene->OnUpdate(ts); // runs OnUpdate logic (currently empty — hook your custom systems here)
}

void MyLayer::OnRender()
{
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    m_Scene->OnRender(); // automatically batches entities by material, dispatches draw calls
    Cosmic::Renderer2D::EndScene();
}

void MyLayer::OnDetach()
{
    m_Scene.reset(); // destroys the scene and all registered entities
}
```

### Entity Validity

```cpp
Cosmic::Entity e; // default-constructed, invalid

if (e)  // false — null entity
    e.GetComponent<...>(); // would assert

Cosmic::Entity player = m_Scene->CreateEntity("Player");
if (player) // true
    ...

m_Scene->DestroyEntity(player);
// player handle is now dangling — do not use it
```

---

## 10. Camera System

The orthographic camera maps 2D world coordinates to the screen. The controller wrapper adds input-driven panning and smooth zoom.

### Direct Camera Usage

```cpp
float aspect = (float)width / (float)height;
Cosmic::OrthographicCamera camera(-aspect, aspect, -1.f, 1.f);

camera.SetPosition({5.f, -2.f, 0.f});
camera.SetRotation(30.f); // degrees, Z-axis only

// Upload to shader via BeginScene (happens automatically)
Cosmic::Renderer2D::BeginScene(camera);
```

### Camera Controller (Recommended)

```cpp
// In your layer header
Cosmic::OrthographicCameraController m_Camera { 1280.f / 720.f }; // aspect ratio

// In OnUpdate — handles WASD panning, scroll zoom, smooth interpolation
void OnUpdate(float ts) override
{
    m_Camera.OnUpdate(ts);
    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    // draw...
    Cosmic::Renderer2D::EndScene();
}

// In OnEvent — handles MouseScrolledEvent and WindowResizeEvent automatically
void OnEvent(Cosmic::Event& e) override
{
    m_Camera.OnEvent(e);
}
```

### Camera Controller Configuration

```cpp
// Zoom limits (default: 0.25 – 10.0)
m_Camera.SetZoomLimits(0.1f, 50.f);

// Zoom speed per scroll tick (default: 0.25)
m_Camera.SetZoomSpeed(0.15f);

// Movement speed (default: 5.0)
m_Camera.SetTranslationSpeed(8.f);

// Hard-snap zoom (bypasses smooth interpolation)
m_Camera.SetZoomLevel(2.f);

// World-space position bounds (camera cannot pan outside this)
m_Camera.SetPositionLimits(-20.f, 20.f, -10.f, 10.f);

// Manually place the camera (useful for entity follow)
m_Camera.SetPosition({entity.Position.x, entity.Position.y, 0.f});
```

### Viewport Resize

Call `OnResize` whenever your viewport changes size to prevent stretching:

```cpp
void OnEvent(Cosmic::Event& e) override
{
    m_Camera.OnEvent(e); // handles WindowResizeEvent internally
    // or manually:
    if (e.GetEventType() == Cosmic::EventType::WindowResize)
    {
        auto& re = static_cast<Cosmic::WindowResizeEvent&>(e);
        m_Camera.OnResize((float)re.GetWidth(), (float)re.GetHeight());
    }
}
```

---

## 11. Virtual File System

The `FileSystem` utility maps short protocol strings to real disk paths so your asset references survive directory restructuring and build configuration changes.

| Protocol | Resolves to |
|----------|------------|
| `engine://path` | `assets/path` |
| `project://path` | `assets/projects/<ActiveProject>/path` |
| Raw path | Returned unchanged |

```cpp
// Set the active project once in your constructor
Cosmic::FileSystem::SetActiveProject("MyProject");

// Then resolve paths before passing to loaders
std::string texPath    = Cosmic::FileSystem::Resolve("project://sprites/player.png");
std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/Fire.glsl");
std::string fontPath   = Cosmic::FileSystem::Resolve("engine://fonts/default.ttf");

// Always verify before allocating GPU resources
if (!std::filesystem::exists(texPath))
{
    CS_ERROR("Asset not found: {0}", texPath);
    return;
}
auto texture = Cosmic::Texture2D::Create(texPath);
```

### Asset Deployment

Assets are copied to the runtime output directory by CMake's `POST_BUILD` command in your `CMakeLists.txt`:

```cmake
add_custom_command(TARGET MyProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/MyProject"
    COMMENT "Syncing assets..."
)
```

Your assets land at `build/Runtime/Debug/assets/projects/MyProject/` and the VFS `project://` protocol maps there automatically.

---

## 12. Framebuffer

A framebuffer redirects rendering into a GPU texture instead of the display window. The `WorkspaceLayer` handles this automatically — your layer just calls `BeginScene` and `EndScene` normally and the output appears in the viewport panel.

If you need manual framebuffer control:

```cpp
// Creating a framebuffer
Cosmic::FramebufferSpecification spec;
spec.Width  = 1920;
spec.Height = 1080;
auto fb = Cosmic::FrameBuffer::Create(spec);

// Redirect rendering into the framebuffer
fb->Bind();
Cosmic::RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.f});
Cosmic::RenderCommand::Clear();
// ... your draw calls ...
fb->Unbind();

// Display in ImGui
uint32_t texID = fb->GetColorAttachmentRendererID();
ImGui::Image((void*)(uintptr_t)texID, panelSize, {0, 1}, {1, 0});
```

> **Note:** The `WorkspaceLayer` automatically resizes the engine's global framebuffer to match its viewport panel each frame. Calling `fb->Resize()` on the engine framebuffer yourself is generally not needed.

---

## 13. Logging

Cosmic uses [spdlog](https://github.com/gabime/spdlog) with separate channels for engine internals and client code.

```cpp
// Engine-internal code (Cosmic:: namespace)
CS_CORE_TRACE("Texture loaded: {0}", path);
CS_CORE_INFO("Window created: {0}x{1}", width, height);
CS_CORE_WARN("Asset missing, using fallback");
CS_CORE_ERROR("Shader compile failure: {0}", filepath);
CS_CORE_CRITICAL("Out of GPU memory!");

// Your game/plugin code
CS_TRACE("Entity spawned at ({0:.2f}, {1:.2f})", x, y);
CS_INFO("Level loaded: {0}", levelName);
CS_WARN("Physics timestep exceeded budget");
CS_ERROR("Save file corrupt");
```

Format strings use `{}` positional syntax and accept any type with `<<` or `fmt::format` support.

### Timestep

`Cosmic::Timestep` is a thin float wrapper that prevents unit confusion between seconds and milliseconds:

```cpp
void OnUpdate(Cosmic::Timestep ts)
{
    float seconds = ts.GetSeconds();      // use for physics, velocity
    float ms      = ts.GetMilliseconds(); // use for profiling display

    // Also works as a raw float implicitly
    m_Position.x += m_Speed * ts; // ts converts to seconds
}
```

---

## 14. Serial Communication

`Cosmic::SerialPort` provides thread-safe RS-232 serial communication on Windows. A background thread continuously polls the hardware port and accumulates data in a mutex-protected buffer. The main thread flushes the buffer at its own pace.

```cpp
Cosmic::SerialPort port;

// Discover available COM ports
std::vector<std::string> ports = Cosmic::SerialPort::GetAvailablePorts();
// → {"COM3", "COM7"}

// Open a connection (spawns background read thread)
if (port.Open("COM3", 115200))
{
    CS_INFO("Connected to COM3");
}

// In OnFixedUpdate — poll for incoming data
void OnFixedUpdate(float dt) override
{
    if (!port.IsOpen()) return;

    std::string data = port.FlushBuffer(); // thread-safe, clears internal buffer
    if (!data.empty())
    {
        // parse your data here
        ParseTelemetry(data);
    }
}

// Clean up
port.Close();
```

> **Windows only.** The `SerialPort` class uses Win32 `CreateFileA`, `ReadFile`, and `RegOpenKeyExA` — it will not compile on Linux/macOS without a platform implementation.

---

## 15. Complete API Reference Tables

### Renderer2D

| Function | Parameters | Description |
|----------|-----------|-------------|
| `BeginScene` | `const OrthographicCamera&` | Starts a batch pass, caches VP matrix, resets buffers |
| `EndScene` | — | Flushes all batched geometry to GPU |
| `UpdateTimeline` | `float ts, uint32_t w, uint32_t h` | Advances `u_Time`, sets `u_ViewportSize` for shaders |
| `DrawQuad` | `vec2/vec3 pos, vec2 size, vec4 color` | Flat-color quad |
| `DrawQuad` | `vec2/vec3 pos, vec2 size, Ref<Texture>, float tiling, vec4 tint` | Textured quad |
| `DrawQuad` | `vec3 pos, vec2 size, Ref<Material>` | Material/shader-driven quad |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, vec4 color` | Rotated flat quad (rot in radians) |
| `DrawRotatedQuad` | `vec2/vec3 pos, vec2 size, float rot, Ref<Texture>, float tiling, vec4 tint` | Rotated textured quad |
| `DrawRotatedQuad` | `vec3 pos, vec2 size, float rot, Ref<Material>` | Rotated material quad |
| `DrawLine` | `vec3 p0, vec3 p1, vec4 color` | Line segment between two world-space points |
| `DrawRect` | `vec3 pos, vec2 size, vec4 color` | Wireframe rectangle (4 lines) |
| `ResetStats` | — | Clears draw call and quad counters |
| `GetStats` | — | Returns `Statistics` struct |
| `SetStatsStatus` | `bool enabled` | Toggle stats recording |

### Material

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Material::Create` | `Ref<Shader>, string name` | Factory — creates a new material |
| `Set` | `string name, float` | Set a scalar float uniform |
| `Set` | `string name, vec3` | Set a 3-component vector uniform |
| `Set` | `string name, vec4` | Set a 4-component vector uniform (color, RGBA) |
| `Set` | `string name, Ref<Texture>` | Bind a texture to a named slot |
| `GetFloat` | `string name` | Retrieve cached float (0.0 if missing) |
| `GetVector` | `string name` | Retrieve cached vec4 (white if missing) |
| `GetTexture` | `string name` | Retrieve cached texture (nullptr if missing) |
| `Bind` | — | Binds shader and uploads all cached uniforms |
| `GetShader` | — | Returns the underlying `Ref<Shader>` |
| `GetName` | — | Returns material debug name |

### Shader

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Shader::Create` | `string filepath` | Load and compile from `.glsl` file |
| `Bind` | — | Activate in GPU pipeline |
| `Unbind` | — | Deactivate |
| `SetInt` | `string, int` | Upload integer uniform |
| `SetIntArray` | `string, int*, uint32_t count` | Upload integer array |
| `SetFloat` | `string, float` | Upload float |
| `SetFloat2` | `string, vec2` | Upload 2-component float |
| `SetFloat3` | `string, vec3` | Upload 3-component float |
| `SetFloat4` | `string, vec4` | Upload 4-component float |
| `SetMat3` | `string, mat3` | Upload 3×3 matrix |
| `SetMat4` | `string, mat4` | Upload 4×4 matrix |

### Texture

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Texture2D::Create` | `string path` | Load image from disk (PNG, JPG, etc.) |
| `Texture2D::Create` | `uint32_t w, uint32_t h` | Create empty procedural texture |
| `Bind` | `uint32_t slot = 0` | Activate in hardware texture slot |
| `SetData` | `void* data, uint32_t size` | Upload pixel data (size must match w×h×bpp) |
| `GetWidth` / `GetHeight` | — | Pixel dimensions |
| `GetRendererID` | — | OpenGL texture ID (for ImGui::Image) |
| `operator==` | `const Texture&` | True if same GPU resource |

### Scene / Entity

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Scene::Create` | — | Factory — creates a new scene |
| `Scene::CreateEntity` | `string name = "GenericEntity"` | Creates entity; auto-adds Transform + Tag |
| `Scene::DestroyEntity` | `Entity` | Removes entity from registry |
| `Scene::OnUpdate` | `float dt` | Tick scene logic (currently hooks available) |
| `Scene::OnRender` | — | Dispatches all entities to Renderer2D by material bucket |
| `Entity::AddComponent<T>` | `Args...` | Construct and attach component (asserts if duplicate) |
| `Entity::GetComponent<T>` | — | Returns reference (asserts if missing) |
| `Entity::HasComponent<T>` | — | Returns bool |
| `Entity::RemoveComponent<T>` | — | Removes component (asserts if missing) |
| `Entity::operator bool` | — | True if handle is valid and scene-bound |

### OrthographicCameraController

| Function | Parameters | Description |
|----------|-----------|-------------|
| `OrthographicCameraController` | `float aspectRatio, bool rotation = false` | Constructor |
| `OnUpdate` | `float ts` | WASD + smooth zoom interpolation |
| `OnEvent` | `Event&` | Routes scroll and resize events |
| `OnResize` | `float w, float h` | Recalculate aspect ratio |
| `SetZoomLevel` | `float` | Hard-snap zoom (bypasses interpolation) |
| `SetZoomLimits` | `float min, float max` | Clamp scroll zoom range |
| `SetZoomSpeed` | `float` | Speed per scroll tick |
| `SetTranslationSpeed` | `float` | Pan speed (multiplied by zoom level) |
| `SetPositionLimits` | `float minX, maxX, minY, maxY` | Camera pan bounds |
| `SetPosition` | `vec3` | Force camera position |
| `GetCamera` | — | Returns `OrthographicCamera&` |
| `GetZoomLevel` | — | Current active zoom |

### RenderCommand (Low Level)

| Function | Parameters | Description |
|----------|-----------|-------------|
| `SetClearColor` | `vec4 color` | Set RGBA background clear color |
| `Clear` | — | Clear color + depth buffers |
| `Clear` | `float r, float g, float b` | Set color and clear in one call |
| `SetViewport` | `uint32_t x, y, w, h` | Map NDC to window pixels |
| `DrawIndexed` | `Ref<VertexArray>, uint32_t count = 0` | Indexed draw call (count 0 = use VAO's full count) |
| `DrawLines` | `Ref<VertexArray>, uint32_t vertexCount` | Non-indexed line draw |

### FileSystem

| Function | Parameters | Description |
|----------|-----------|-------------|
| `SetActiveProject` | `string name` | Set active project for `project://` resolution |
| `Resolve` | `string path` | Translate VFS protocol to real disk path |

### SerialPort

| Function | Parameters | Description |
|----------|-----------|-------------|
| `Open` | `string portName, uint32_t baudRate = 115200` | Open port, spawn read thread |
| `Close` | — | Signal thread, join, release handle |
| `IsOpen` | — | Returns bool |
| `FlushBuffer` | — | Thread-safe extract + clear accumulated data |
| `GetAvailablePorts` | — | Static — queries Windows Registry, returns port list |

### Logging Macros

| Macro | Level | Channel |
|-------|-------|---------|
| `CS_CORE_TRACE(...)` | Verbose | Engine |
| `CS_CORE_INFO(...)` | Info | Engine |
| `CS_CORE_WARN(...)` | Warning | Engine |
| `CS_CORE_ERROR(...)` | Error | Engine |
| `CS_CORE_CRITICAL(...)` | Fatal | Engine |
| `CS_TRACE(...)` | Verbose | Client/Game |
| `CS_INFO(...)` | Info | Client/Game |
| `CS_WARN(...)` | Warning | Client/Game |
| `CS_ERROR(...)` | Error | Client/Game |
| `CS_CRITICAL(...)` | Fatal | Client/Game |

---

# Part II — Engine Internals

---

## 16. Source File Map

This section documents every source file, its role, and how it fits into the engine.

<!-- RECOMMENDED GRAPHIC: Layered dependency graph — Core.h at the bottom, then graphics abstractions, then renderers, then application layer, then layer system at the top -->

### `src/core/`

| File | Purpose |
|------|---------|
| `Core.h` | Universal foundation — `Scope<T>`, `Ref<T>`, `COSMIC_API` DLL macros, `BIT()`, `GLCORE_ASSERT`, `GLCORE_BIND_EVENT_FN`. Included first in nearly every file. |
| `Application.h/.cpp` | Root singleton. Owns the window, renderer, framebuffer, ImGui, LayerStack. Drives the frame loop. Manages launcher↔workspace transitions and DLL plugin loading. |
| `Layer.h` | Abstract base for all engine components. Declares `OnAttach`, `OnDetach`, `OnUpdate`, `OnFixedUpdate`, `OnRender`, `OnImGuiRender`, `OnEvent`. |
| `LayerStack.h/.cpp` | Ordered container of `Layer*` borrows. Manages insertion boundary between game layers and overlays. Provides forward (render) and reverse (event) iterators. |
| `Window.h/.cpp` | GLFW window wrapper. Handles creation, event callbacks, VSync, buffer swapping. Owns `GraphicsContext`. Translates GLFW C callbacks into Cosmic `Event` objects. |
| `Input.h/.cpp` | Static polling interface. Wraps `glfwGetKey`, `glfwGetMouseButton`, `glfwGetCursorPos` through the Application singleton. |
| `Log.h/.cpp` | spdlog wrapper. Initializes two loggers (engine + client) with color-coded console output. |
| `Timestep.h` | Thin float wrapper for delta-time. Prevents seconds/milliseconds confusion. Implicit `float` conversion. |

### `src/events/`

| File | Purpose |
|------|---------|
| `Event.h` | Base `Event` class, `EventType` enum, `EventCategory` bitmask flags, `EventDispatcher` template. |
| `ApplicationEvent.h` | `WindowResizeEvent`, `WindowCloseEvent`, `AppTickEvent`, `AppUpdateEvent`, `AppRenderEvent`. |
| `KeyEvent.h` | `KeyEvent` base, `KeyPressedEvent` (with repeat count), `KeyReleasedEvent`, `KeyTypedEvent`. |
| `MouseEvent.h` | `MouseMovedEvent`, `MouseScrolledEvent`, `MouseButtonEvent` base, `MouseButtonPressedEvent`, `MouseButtonReleasedEvent`. |

### `src/renderer/`

| File | Purpose |
|------|---------|
| `RendererAPI.h/.cpp` | Abstract backend interface. Declares pure virtual methods: `Init`, `SetViewport`, `Clear`, `DrawIndexed`, `DrawLines`. Holds the static `API` enum (`None`, `OpenGL`, `DirectX`). Compile-time selection via `#ifdef COSMIC_PLATFORM_WINDOWS`. |
| `RenderCommand.h/.cpp` | Static dispatcher. Forwards calls to the `RendererAPI*` instance. Provides `SetClearColor`, `Clear`, `SetViewport`, `DrawIndexed`, `DrawLines`. The single point of contact between the high-level renderers and the hardware API. |
| `Renderer.h/.cpp` | High-level 3D/static orchestrator. Manages per-scene `SceneData` (VP matrix), provides `Submit` overloads for direct shader+VAO draws. Parent of `Renderer2D`. |
| `Renderer2D.h/.cpp` | High-performance 2D batch renderer. Manages vertex/index staging buffers (up to 10,000 quads per batch), texture slot tracking (up to 32), material state tracking, and line rendering. The primary draw interface for game code. |

### `src/graphics/`

| File | Purpose |
|------|---------|
| `Buffer.h/.cpp` | `ShaderDataType` enum + size utilities. `BufferElement` (name, type, offset, size). `BufferLayout` (stride calculator). Abstract `VertexBuffer` and `IndexBuffer` interfaces with factory `Create` methods. |
| `VertexArray.h/.cpp` | Abstract VAO interface. Links vertex buffers + their layouts to an index buffer. Factory `Create` method. |
| `Shader.h/.cpp` | Abstract GPU program interface. Uniform set methods. Factory `Create` from filepath. |
| `Texture.h/.cpp` | `Texture` base and `Texture2D` derived interfaces. Factory `Create` from path or dimensions. |
| `Material.h/.cpp` | Pairs a `Ref<Shader>` with named uniform caches (`float`, `vec3`, `vec4`, `Ref<Texture>`). `Bind()` uploads all cached uniforms. |
| `FrameBuffer.h/.cpp` | Abstract FBO interface. `FramebufferSpecification` config struct. Factory `Create`. |
| `GraphicsContext.h` | Two-method interface: `Init()` and `SwapBuffers()`. The bridge between the windowing library and the graphics API. |

### `src/platform/opengl/`

| File | Purpose |
|------|---------|
| `OpenGLContext.h/.cpp` | Calls `glfwMakeContextCurrent` and initializes GLAD. Implements `SwapBuffers` via `glfwSwapBuffers`. |
| `OpenGLRendererAPI.h/.cpp` | Concrete `RendererAPI` for OpenGL. Implements `Init` (enables blending + depth test), `SetViewport`, `SetClearColor`, `Clear`, `DrawIndexed` (`glDrawElements`), `DrawLines` (`glDrawArrays`). |
| `OpenGLBuffer.h/.cpp` | `OpenGLVertexBuffer` (dynamic via `GL_DYNAMIC_DRAW`, static via `GL_STATIC_DRAW`, streams via `glBufferSubData`). `OpenGLIndexBuffer` (`GL_ELEMENT_ARRAY_BUFFER`). |
| `OpenGLVertexArray.h/.cpp` | Generates a VAO, calls `glVertexAttribPointer` for each layout element, links VBOs and IBO. |
| `OpenGLShader.h/.cpp` | Reads `.glsl` from disk, preprocesses `#type` directives, compiles vertex + fragment stages, links program. Caches uniform locations. Includes Shadertoy compatibility wrapper. |
| `OpenGLTexture.h/.cpp` | File-based loader uses stb_image (handles RGB/RGBA, flips vertically). Procedural creates empty RGBA8 texture. Supports `SetData` via `glTexSubImage2D`. |
| `OpenGLFrameBuffer.h/.cpp` | Creates FBO with color attachment (RGBA8) and depth/stencil attachment (Depth24_Stencil8). `Invalidate()` tears down and reallocates on resize. |

### `src/camera/`

| File | Purpose |
|------|---------|
| `OrthographicCamera.h/.cpp` | Maintains P, V, VP matrices. `UpdateViewMatrix` computes V = inverse(TRS). Provides `SetPosition`, `SetRotation`, `SetProjection`. |
| `OrthographicCameraController.h/.cpp` | Wraps `OrthographicCamera` with WASD input, asymptotic zoom interpolation, aspect ratio sync, and position/zoom clamping. |

### `src/scene/`

| File | Purpose |
|------|---------|
| `Scene.h/.cpp` | Owns `entt::registry`. `CreateEntity` adds Transform + Tag automatically. `OnRender` sorts entities into material buckets before dispatching to `Renderer2D`. |
| `Entity.h` | Lightweight handle (`entt::entity` + `Scene*`). Template `AddComponent`, `GetComponent`, `HasComponent`, `RemoveComponent`. |
| `Components.h` | `TagComponent`, `TransformComponent` (with `GetTransform()`), `SpriteRendererComponent`. Includes `entt::type_hash` specializations for DLL boundary safety. |

### `src/layers/`

| File | Purpose |
|------|---------|
| `ImGuiLayer.h/.cpp` | Initializes ImGui + ImPlot contexts, GLFW backend, OpenGL3 backend. `Begin`/`End` frame control. Event blocking logic (mouse/keyboard capture flags). |
| `WorkspaceLayer.h/.cpp` | The editor host. Manages ImGui dockspace layout (Viewport + Inspector panels). Owns the client viewport slot. Handles framebuffer resize, viewport focus, deferred DLL mount/unmount. |
| `LauncherLayer.h/.cpp` | The startup hub. Scans for `.dll` files, renders project list, hosts the "Create New Project" wizard, reads template files, runs `build.bat` via `CreateProcessA`. |

### `src/serial/`

| File | Purpose |
|------|---------|
| `SerialPort.h/.cpp` | Win32 serial communication. Background `ReadLoop` thread polls `ReadFile`. `FlushBuffer` extracts data thread-safely. `GetAvailablePorts` queries Windows Registry `HARDWARE\DEVICEMAP\SERIALCOMM`. |

### `src/utils/`

| File | Purpose |
|------|---------|
| `FileSystem.h` | Static path resolver. Maps `engine://` and `project://` protocol prefixes to real disk paths. |

### `src/codes/`

| File | Purpose |
|------|---------|
| `KeyCodes.h` | `CS_KEY_*` constants (mirrors GLFW values). |
| `MouseButtonCodes.h` | `CS_MOUSE_BUTTON_*` constants. |

### `src/Cosmic.h`

The single-header public API aggregator. Include `<Cosmic.h>` in your project to access the entire engine surface. Also declares `HostContext` and the required DLL export signatures.

---

## 17. The OpenGL Graphics Pipeline

This section explains how Cosmic maps the OpenGL pipeline stages to its own architecture.

<!-- RECOMMENDED GRAPHIC: Full OpenGL pipeline diagram — CPU Data → VBO → VAO (with layout) → Vertex Shader → Rasterizer → Fragment Shader → Framebuffer → Screen -->

### Overview: What the GPU Does Each Frame

The GPU processes geometry through a fixed series of stages. Cosmic feeds data into this pipeline through its abstraction layers:

```
CPU Side                              GPU Side
────────                              ────────
Application / Renderer2D
    │
    ├── Writes vertex data ──────────► VBO (Vertex Buffer Object)
    │   (position, color, UV, etc.)    stored in VRAM
    │
    ├── BufferLayout defines ─────────► VAO (Vertex Array Object)
    │   what each vertex means          stores: "bytes 0-11 = position,
    │   (offsets, strides, types)        bytes 12-27 = color, ..."
    │
    ├── Index data ──────────────────► IBO (Index Buffer Object)
    │   (which vertices form triangles)
    │
    └── Shader::Bind() ──────────────► Vertex Shader Stage
        + SetMat4("u_ViewProjection")   transforms each vertex from
                                         world space → clip space
                                           │
                                           ▼
                                        Primitive Assembly
                                        (groups vertices into triangles)
                                           │
                                           ▼
                                        Rasterization
                                        (determines which pixels are covered)
                                           │
                                           ▼
                                        Fragment Shader Stage
                                        (computes pixel color from
                                         texture samples, uniforms, etc.)
                                           │
                                           ▼
                                        Framebuffer
                                        (written to color + depth attachments)
                                           │
                                           ▼
                                        glfwSwapBuffers()
                                        (presents to screen)
```

### Stage 1: Vertex Data (VBO)

`OpenGLVertexBuffer` allocates GPU memory via `glBufferData`. Cosmic uses two allocation strategies:

- **Static** (`GL_STATIC_DRAW`): For geometry that doesn't change. The driver stores it in the fastest VRAM possible.
- **Dynamic** (`GL_DYNAMIC_DRAW`): For `Renderer2D`'s batch buffers. Pre-allocated at max batch size, updated each frame via `glBufferSubData` — no reallocation.

```
Renderer2D allocates: MaxQuads(10000) × 4 vertices × sizeof(QuadVertex) bytes
→ This is the staging "arena" that gets partially filled each batch
→ glBufferSubData uploads only the filled portion, not the entire allocation
```

### Stage 2: Vertex Layout (VAO + BufferLayout)

A raw vertex buffer is just bytes. The GPU needs to know what those bytes mean. `BufferLayout` is the metadata that describes the schema:

```
QuadVertex layout:
┌──────────────────────────────────────────────────────┐
│  Offset 0:   a_Position   (Float3 = 12 bytes)        │
│  Offset 12:  a_Color      (Float4 = 16 bytes)        │
│  Offset 28:  a_TexCoord   (Float2 = 8 bytes)         │
│  Offset 36:  a_TexIndex   (Float  = 4 bytes)         │
│  Offset 40:  a_TilingFactor (Float = 4 bytes)        │
│  Stride: 44 bytes total per vertex                   │
└──────────────────────────────────────────────────────┘
```

`OpenGLVertexArray::AddVertexBuffer` calls `glVertexAttribPointer` for each element, recording these offsets in the VAO. Binding the VAO again later automatically restores all these attribute pointers — a single `glBindVertexArray` replaces many individual attribute setup calls.

### Stage 3: Index Buffer (IBO)

Instead of repeating the four vertices of a quad twice (six raw vertices for two triangles), Cosmic stores four unique vertices and an index buffer `[0,1,2, 2,3,0]` that tells the GPU which vertices form the two triangles. This pattern is pre-computed for the entire 10,000-quad max batch at startup.

### Stage 4: Vertex Shader

The vertex shader runs once per vertex on the GPU. Its primary job is multiplying the vertex position by the `u_ViewProjection` matrix to transform from world space to clip space:

```glsl
gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
```

The `u_ViewProjection` matrix is calculated by the `OrthographicCamera` on the CPU each frame and uploaded via `Shader::SetMat4`.

### Stage 5: Rasterization

The GPU takes the clip-space triangles and determines which pixels they cover. This is fully fixed-function — no code here. Interpolated values (color, UV, texture index) are produced for each covered pixel.

### Stage 6: Fragment Shader

Runs once per pixel covered by a triangle. The default batch shader (`Texture.glsl`) indexes into the `u_Textures[32]` sampler array using the interpolated `v_TexIndex` to sample the correct texture, then multiplies by the vertex color:

```glsl
color = texture(u_Textures[int(v_TexIndex)], v_TexCoord * v_TilingFactor) * v_Color;
```

Custom materials substitute this shader with your own GLSL.

### Stage 7: Framebuffer Output

Pixel colors are written to the active `OpenGLFrameBuffer`'s color attachment texture. Depth values go to the depth/stencil attachment for correct layering. After all draws, `fb->Unbind()` returns rendering to the default window buffer, and `glfwSwapBuffers` presents the frame.

### Why OpenGL?

OpenGL was chosen as the initial backend for portability and simplicity. The entire rendering abstraction layer (`RendererAPI` → `RenderCommand` → `Renderer2D`) was designed to be backend-agnostic — adding DirectX or Vulkan requires implementing the `RendererAPI` and `OpenGLXxx` platform classes without touching the high-level renderer code. The compile-time `s_API` flag in `RendererAPI.cpp` is the only place the selection is made.

---

## 18. Hardware Abstraction Architecture

<!-- RECOMMENDED GRAPHIC: Layered abstraction diagram — game code → Renderer2D → RenderCommand → RendererAPI (abstract) → OpenGLRendererAPI (concrete), with parallel branch for Buffers, Shaders, Textures following the same pattern -->

Cosmic uses the **Factory Pattern** throughout its graphics layer. Every GPU resource is created through a static `Create` method that queries `RendererAPI::GetAPI()` and instantiates the correct platform class:

```cpp
// This is what every factory method looks like
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

This means `Renderer2D`, `Scene`, and your game code never `#include` any OpenGL headers. They work entirely with the abstract `Shader`, `Texture`, `VertexBuffer`, `VertexArray`, and `FrameBuffer` interfaces. The OpenGL implementation is completely isolated in `src/platform/opengl/`.

### The `RenderCommand` Dispatcher

`RenderCommand` holds a single `static RendererAPI* s_RendererAPI` initialized at startup via `CreateRendererAPI()`. All static methods on `RenderCommand` forward to this pointer. This means the entire rendering path involves exactly one virtual dispatch per draw call — essentially free compared to the GPU work itself.

### DLL API Export Macros

The `COSMIC_API` macro in `Core.h` switches between `__declspec(dllexport)` when building `Cosmic.dll` and `__declspec(dllimport)` when building client projects. This ensures the linker can correctly locate all engine symbols across DLL boundaries.

---

## 19. Batch Rendering Deep Dive

<!-- RECOMMENDED GRAPHIC: Batch renderer state machine — BeginScene resets pointers, each Draw call writes to CPU staging buffer, Flush uploads and draws, FlushAndReset handles state changes mid-frame -->

The core performance feature of Cosmic is the batch renderer. Instead of issuing one draw call per object (which stalls the GPU command pipeline), it accumulates up to 10,000 quads into a single CPU-side staging buffer and uploads them all in one `glBufferSubData` + `glDrawElements` call.

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
    → QuadVertexPtr += 4
    → QuadIndexCount += 6
    → assign texture to TextureSlots[i], record float index in vertex
    │
    ├── if QuadIndexCount >= MaxIndices → FlushAndReset()
    ├── if TextureSlotIndex >= MaxTextureSlots → FlushAndReset()
    └── if material changes → FlushAndReset()
    │
    ▼
EndScene() → Flush()
    → glBufferSubData (upload only filled portion)
    → bind all active texture slots
    → bind material shader
    → upload u_ViewProjection, u_Time, u_ViewportSize
    → glDrawElements(GL_TRIANGLES, QuadIndexCount, ...)
    │
    ▼
Next frame: BeginScene resets everything
```

### Texture Batching

Cosmic supports up to 32 simultaneous textures in one draw call via GLSL `sampler2D u_Textures[32]`. Each quad vertex stores a `float TexIndex` that the fragment shader uses to index into this array. When a new texture is used, it's assigned the next available slot. When all 32 slots are full, `FlushAndReset` is called to drain the batch, then the new texture takes slot 1.

### Material State Changes

When a draw call uses a different `Material` than the previous one, `FlushAndReset` is called first. This submits the current batch with the old material's shader, then switches to the new material for subsequent draws. `Scene::OnRender` sorts entities into material buckets specifically to minimize these mid-frame flushes.

### The White Texture Trick

The batch renderer creates a 1×1 white texture at startup. When drawing flat-color quads (no texture), the quad vertices use `TexIndex = 0` (the white texture slot) and set `v_Color` to the desired color. The fragment shader samples white and multiplies by the vertex color, producing the correct solid color — no separate code path needed.

---

## 20. The DLL Plugin System

<!-- RECOMMENDED GRAPHIC: DLL lifecycle diagram — Launcher selects project → LoadLibraryA → GetProcAddress × 2 → InitializePluginContexts (ImGui sync) → CreatePluginLayer → WorkspaceLayer mounts layer → UnloadProjectDLL sequence on return -->

The DLL plugin system allows game projects to be compiled independently and loaded at runtime without restarting the engine.

### Why a DLL architecture?

- **Iteration speed:** Recompile your game DLL (seconds) without rebuilding the engine (minutes)
- **Isolation:** A crash in game code is isolated from the engine host process
- **Distribution:** The engine binary is stable; game content is swappable

### The Critical DLL Boundary Problem

When two DLLs are loaded, global singletons (like ImGui's context pointer, spdlog loggers) are not automatically shared — each DLL has its own copy of these globals in its data segment. If your game code calls `ImGui::Begin()` without first synchronizing the ImGui context pointer, it will write to the game DLL's null context, causing a crash.

`InitializePluginContexts` solves this by explicitly passing the engine's context pointers into the game DLL:

```cpp
// In engine host (Application.cpp):
HostContext ctx;
ctx.ImGuiCtx  = ImGui::GetCurrentContext();
ctx.ImPlotCtx = ImPlot::GetCurrentContext();
initContexts(ctx); // call into the DLL

// In game DLL (your project .cpp):
void InitializePluginContexts(Cosmic::HostContext context)
{
    ImGui::SetCurrentContext(context.ImGuiCtx);   // sync ImGui
    ImPlot::SetCurrentContext(context.ImPlotCtx); // sync ImPlot
}
```

After this call, both the engine and the game DLL write to the same ImGui command list.

### Load Sequence

```
1. LoadLibraryA("MyProject.dll")
      → Maps DLL into engine's virtual address space
      → DLL static initializers run (spdlog loggers initialize)

2. GetProcAddress("InitializePluginContexts")
   GetProcAddress("CreatePluginLayer")
      → Locate export table entries
      → If either is missing: FreeLibrary and log error

3. initContexts(ctx)
      → Sync ImGui and ImPlot context pointers across boundary

4. m_ActivePluginLayer = createPluginLayer()
      → Allocates your Layer subclass with `new`
      → Returns raw Layer* (engine takes ownership)

5. m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer)
      → Mounts layer into the editor viewport slot
```

### Unload Sequence (Order is Critical)

```
1. m_WorkspaceLayer->ClearViewportLayer()
      → Removes layer from rendering pipeline

2. delete m_ActivePluginLayer
      → Calls ~YourLayer() → OnDetach() → releases GPU resources
      → MUST happen before FreeLibrary!
      → If FreeLibrary runs first, the vtable is gone → crash

3. FreeLibrary(m_PluginHandle)
      → Unmap DLL from memory

4. m_PluginHandle = nullptr
   m_ActivePluginLayer = nullptr
```

### EnTT Type Hash Safety

Because the game DLL and engine DLL are separate compilation units, the `entt::type_hash` template (which generates component IDs by hashing `typeid(...).name()`) can produce different hashes for the same type if the mangled names differ between compilers or translation units.

The solution in `Components.h` is to override `type_hash` with a `consteval` string literal hash that is guaranteed identical regardless of compiler or DLL:

```cpp
template<> struct type_hash<Cosmic::TransformComponent> final {
    [[nodiscard]] static consteval id_type value() noexcept {
        return hashed_string::value("TransformComponent"); // stable, literal string
    }
};
```

Any component type you want to share across the DLL boundary needs this override.

---

## 21. Shader Preprocessing System

`OpenGLShader::PreProcess` handles four cases:

**Case 1: Standard multi-stage file** (has `#type vertex` and `#type fragment`)

The preprocessor splits the source at `#type` boundaries, then for each stage:
- Scans for usage of `u_ViewProjection`, `u_Time`, `u_ViewportSize`
- If used but not declared, injects the declaration just after the `#version` line
- Injects `in vec2 v_TexCoord; in vec4 v_Color; layout(...) out vec4 color;` for fragment stages missing them

**Case 2: Fragment-only file** (has `#type fragment`, no `#type vertex`)

The preprocessor generates a boilerplate vertex shader that matches `Renderer2D`'s quad vertex layout, combines it with the provided fragment stage.

**Case 3: Shadertoy-style file** (has `mainImage`, no `#type` tags)

Detects `mainImage` or `iTime` signatures. Generates the standard vertex shader, injects Shadertoy uniform aliases (`#define iTime u_Time`, `#define iResolution vec3(u_ViewportSize, 1.0)`), and wraps `mainImage` with a `void main()` bridge function.

**Case 4: Error fallback**

If no `#type` tags and no Shadertoy signatures are detected, logs an error and returns an empty map (shader will fail to compile with a clear error message).

### Uniform Location Caching

`OpenGLShader` caches all `glGetUniformLocation` results in `m_UniformLocationCache`. The first call per uniform name hits the driver; all subsequent calls return the cached `GLint`. This eliminates per-frame driver queries for animated uniforms like `u_Time`.

---

## 22. Build System

<!-- RECOMMENDED GRAPHIC: CMake dependency graph — CosmicRoot → Cosmic (DLL) + CosmicApp (EXE) + DinoProject (DLL) + YourProject (DLL), with arrows showing link dependencies -->

The project uses CMake 3.21+ with a three-tier structure:

**Root `CMakeLists.txt`:** Sets global standards, defines `COSMIC_SDK_DIR`, adds `Cosmic/` and `Runtime/` subdirectories. Automatically discovers any project under `Projects/*/CMakeLists.txt` and adds it as a subdirectory.

**`Cosmic/CMakeLists.txt`:** Builds `Cosmic.dll`. Manages all external dependencies (GLFW, GLAD, GLM, EnTT, ImGui, ImPlot, spdlog, stb_image). Sets `COSMIC_BUILD_DLL` so `Core.h` exports symbols. Copies `assets/` to the build output directory on post-build.

**`Runtime/CMakeLists.txt`:** Builds `CosmicApp.exe` — the host executable. Links against `Cosmic`. Sets up VS debugger working directory.

**Project `CMakeLists.txt`:** Builds your game DLL. Uses an "agnostic linking" pattern — if `Cosmic` is a known CMake target (building from source), links against it directly. Otherwise, imports `Cosmic.lib`/`Cosmic.dll` from the SDK build output. This allows projects to be built both within the monorepo and as standalone standalone out-of-tree projects.

### `build.bat` / `build_engine.bat`

`build_engine.bat` compiles only the engine core (`-DCOSMIC_BUILD_ENGINE_ONLY=ON`). Project `build.bat` files detect MSVC via `vswhere.exe`, resolve `COSMIC_SDK` from environment or heuristic path traversal, and run `cmake --build --config Debug --parallel`.

---

# Part III — Code Review

---

## 23. Refactor Candidates

### High Priority

**`OpenGLShader::PreProcess` — complexity and fragility**

The preprocessor is doing too much in one 300-line function. The comment-stripping logic (removing `/* */` and `//` before scanning for uniform names) is custom hand-written parsing that will break on edge cases (multi-line strings, conditional compilation, etc.). Consider:
- Split into `StripComments`, `ExtractStage`, `InjectPreamble` functions
- Use a proper tokenizer or a regex pass rather than manual `find`/`erase` loops
- The "already declared" check (`find("uniform")` in a 20-char context window before the name) is fragile — a false negative will inject a duplicate declaration and fail to compile

**`Renderer2D` — code duplication across overloads**

`DrawQuad(material)` and `DrawRotatedQuad(material)` share 80% identical code (texture key fallback sweep, slot lookup, vertex write loop). This should be refactored into a shared internal `WriteQuadVertices(transform, color, texIndex)` function.

**`Application::Run` — length and responsibility**

The `Run()` method is ~150 lines and handles the frame loop, fixed timestep, layer iteration, ImGui, and the Safe Zone DLL transition logic. The DLL transition logic in the Safe Zone should be extracted into `ProcessPendingTransitions()`.

**`LayerStack` documentation mismatch**

The header documentation for `PushLayer` says: *"Appends the overlay to the back of the stack (on top of logic)"* — this is the documentation for `PushOverlay`, not `PushLayer`. The docs need a pass for correctness.

### Medium Priority

**`LauncherLayer::GenerateProjectTemplate` — Win32 process spawning**

The project generation function uses `CreateProcessA` directly with `command.data()` (non-const data pointer). It also silently proceeds if `CreateProcessA` fails. Should use `const_cast` properly and add error handling for the process exit code.

**`SerialPort` — `void*` for Win32 `HANDLE`**

`m_Handle` is declared as `void*` in `SerialPort.h` to avoid pulling in `<windows.h>` in the header. This is correct practice, but the cast back to `HANDLE` in the `.cpp` is unchecked. Consider using `INVALID_HANDLE_VALUE` consistently and wrapping in a platform-specific compilation guard.

**`Scene::OnRender` — `materialBuckets` key is a raw pointer**

`std::unordered_map<Material*, ...>` uses raw pointer equality for hashing. This is functionally correct (same material = same pointer since materials are `Ref<>`-owned), but is fragile if material ownership ever changes. Using `std::unordered_map<Ref<Material>, ...>` with a custom hash would be safer.

**`WorkspaceLayer` — `firstTime` static local in `OnImGuiRender`**

The dockspace layout is initialized with a `static bool firstTime = true` inside `OnImGuiRender`. This means if `WorkspaceLayer` is destroyed and recreated (which happens during launcher↔workspace transitions), the layout will not reinitialize on the second creation because the static persists. Should be a member variable.

### Low Priority

**`OrthographicCameraController` — `m_TimeScale` / `m_ZoomSpeed` naming**

`m_ZoomSpeed` controls the target zoom delta per scroll tick, but is described as "speed" in comments, while `smoothnessFactor = 10.0f` is a hardcoded constant with no accessor. Consider exposing `SetSmoothnessFactor(float)` for tuning.

**`RenderCommand` — raw pointer ownership**

`RenderCommand::s_RendererAPI` is a raw `RendererAPI*` allocated with `new` in `CreateRendererAPI()` and never `delete`d. Since it lives for the entire application lifetime this is not a memory leak in practice, but it violates the `Scope<T>`/`Ref<T>` ownership model the engine uses everywhere else.

**`Window.cpp` — `glfwInit` called without corresponding `glfwTerminate` guard**

If `glfwCreateWindow` fails after a successful `glfwInit`, the function returns early and `glfwTerminate` is never called. The destructor calls `glfwTerminate` unconditionally — if `m_Handle` is null, `glfwDestroyWindow(nullptr)` is called. This should be guarded.

---

## 24. Missing Implementations

### Critical Missing Features

**`Renderer2D` — Missing `DrawQuad(vec2, vec2, Ref<Material>)` overload**

The `Renderer2D.h` header declares `DrawQuad(vec3, vec2, Ref<Material>)` but there is no `vec2` position overload for materials (unlike the color and texture overloads which have both). The 2D overload should be added for consistency.

**`Renderer2D` — No `DrawLine` with line width control**

`DrawLine` uses the default OpenGL line width (1 pixel). There is no `SetLineWidth` call anywhere. Modern OpenGL core profile only guarantees width 1 — debug lines are always 1px. Consider adding `RenderCommand::SetLineWidth(float)` backed by `glLineWidth`, with a note that values > 1 are implementation-defined.

**`Scene::OnUpdate` — Empty**

`Scene::OnUpdate(float deltaTime)` exists but does nothing. There is no ECS system dispatch, no component-driven update loop. Currently all game logic runs in the layer's `OnUpdate` by manually fetching components. A proper ECS system (visitor/view-based update callbacks) is missing.

**`RendererAPI::DirectX` — Stub only**

`RendererAPI::API::DirectX` exists in the enum and returns `nullptr` everywhere. This is expected for now but worth tracking.

**`Renderer::EndScene` — Empty**

`Renderer::EndScene()` has a comment: *"Future: Submit CommandQueue for sorting/optimization"* — this command buffer/deferred submission architecture has not been implemented.

### Missing Quality-of-Life Features

**Texture atlases / sprite sheets** — No `SubTexture2D` or UV region support. Currently the only way to use a sprite sheet is to pass a custom material with adjusted UV uniforms.

**Audio system** — No audio interface exists at all.

**Asset manager / cache** — Shaders and textures are not cached. Loading the same path twice creates two separate GPU objects. A simple `std::unordered_map<string, Ref<Texture2D>>` cache would prevent redundant GPU allocations.

**Scene serialization** — No save/load for scenes. Entity/component data cannot be persisted to disk.

**Multiple cameras / camera stack** — Only one camera view can be rendered per `BeginScene`. No support for split-screen, mini-maps, or render-to-texture from a second camera without manually managing framebuffers.

**Input mapping system** — Key codes are hardcoded. No rebindable action map.

**Linux/macOS platform support** — `SerialPort` is Windows-only. `Application.cpp` directly includes `<Windows.h>` for `LoadLibraryA`. The DLL plugin system is entirely Win32. Cross-platform support would require platform abstraction for dynamic library loading and serial I/O.

---

## 25. Technical Debt & Open Issues

### Architectural Issues

**`Log.h` TODO items** — The header contains explicit TODO notes for writing logs to an output file and for better separation of client vs. engine log commands. These have not been addressed.

**`Cosmic.h` extern "C" block** — The public header `Cosmic.h` contains `extern "C" { __declspec(dllexport) ... }` which declares export symbols as if any file including `Cosmic.h` is implementing them. This means if a second file in a DLL project includes `Cosmic.h`, the linker will see multiple definition errors unless the actual implementation file is the only translation unit that provides the function bodies. This should be moved to a separate `CosmicPlugin.h` that only plugin root files include.

**`Renderer2D::FlushAndReset` — calls `EndScene`**

`FlushAndReset()` calls `EndScene()` which calls `Flush()`. Then it manually resets the counters. This means `EndScene` is being invoked mid-batch. It works because `Flush` only draws, but the naming is semantically confusing — `FlushAndReset` should call `Flush()` directly, not `EndScene()`.

**`Line.glsl` — uses `// #type` with comment prefix**

The `Line.glsl` built-in shader has `// #type vertex` and `// #type fragment` commented out. This means the shader file has no `#type` directives and will hit the Shadertoy fallback path in the preprocessor (and fail since it has no `mainImage`). The line shader is loaded via `Shader::Create("assets/shaders/Line.glsl")` in `Renderer2D::Init()`. This likely either fails silently or produces an incorrect shader. The `//` prefixes should be removed.

**Static `s_SceneData` in `Renderer`**

`Renderer::s_SceneData` is allocated with `new Renderer::SceneData` at global scope and never `delete`d. It persists for the application lifetime, which is fine in practice, but it is a raw pointer leak.

**`OpenGLContext::Init` — no GLAD status check**

```cpp
int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
// Note: status check is critical here...
```

The comment acknowledges this but the check is not implemented. If GLAD fails to load, every subsequent `gl*` call will dereference a null function pointer. The check should be:

```cpp
GLCORE_ASSERT(status, "Failed to initialize GLAD!");
```

**`WorkspaceLayer` — `firstTime` layout issue (detailed in Section 23)**

**`DinoProject` — hardcoded aspect ratio**

`Renderer2D::UpdateTimeline(ts, 1280, 720)` is called with hardcoded dimensions in multiple simulation layers. When the viewport is resized, `u_ViewportSize` will be wrong. The correct width/height should be passed dynamically from `SetViewportSize`.

**`DinoRunLayer` — `rand()` used for RNG**

The obstacle spawner uses `std::uniform_real_distribution` correctly with `std::mt19937`, but earlier versions also called `rand()` in `DinoFlightLayer` for chaos mode. `rand()` is not seeded, not thread-safe, and has poor distribution quality. Both should use the C++ `<random>` facilities consistently.

**Missing viewport-to-world-space coordinate mapping**

There is no utility function to convert a screen-space mouse position (from `Input::GetMousePosition()`) to world-space coordinates using the camera's inverse VP matrix. This is a very commonly needed operation (clicking on entities, drawing tools) and its absence forces every project to re-implement the math.

---

*README last updated to reflect codebase state as of May 2026.*

*For questions about engine architecture or to report issues, see the project repository.*
