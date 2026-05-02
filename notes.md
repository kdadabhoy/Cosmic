


# Cosmic Engineering Workspace Architecture

This repository uses a **Host/Client architecture** to separate the core rendering engine from individual engineering simulations. This allows for modular development, where new projects can be added or swapped at runtime without modifying the engine's core pipeline.

## 🧱 Core Components

### 1. `Main.cpp` (The Entry Point)
`Main.cpp` is the bootstrap for the entire application. Its primary responsibility is to:
*   **Initialize the Engine:** Inherits from `Cosmic::Application` to start the low-level subsystems (Windowing, Graphics API, Input).
*   **Push the Workspace:** It pushes the `WorkspaceLayer` onto the application's layer stack. 
*   **Execution:** It starts the engine's heartbeat via `app->Run()`.

### 2. `WorkspaceLayer` (The Host)
The `WorkspaceLayer` acts as the **Main Window** and **Project Orchestrator**. 
*   **Docking & UI:** Implements the ImGui DockSpace, allowing for snap-on windows like the Viewport, Project Inspector, and Monitor.
*   **Framebuffer Management:** It owns the `Framebuffer`. It binds the buffer before a simulation renders and unbinds it after, ensuring the simulation's output is captured and displayed within an ImGui window rather than directly to the screen.
*   **Project Switching:** It handles the logic for loading and unloading `Simulation` objects.

### 3. `Simulation.h` (The Interface)
This is an **Abstract Base Class** (Interface). Every project must inherit from this class to be compatible with the `WorkspaceLayer`. It defines the lifecycle of a simulation:
*   `OnUpdate(float ts)`: Handle physics, input, and logic.
*   `OnRender()`: Dispatch draw calls to the `Renderer2D` or `Renderer3D`.
*   `OnImGuiRender()`: Define custom debug sliders and buttons for the "Project Inspector" panel.
*   `SetViewportSize()`: Ensure the project's camera projection matches the UI window size.

---

## 📂 Project Requirements

When creating a new project (e.g., `FluidDynamics` or `StructuralAnalysis`), every project must consist of at least two parts: a **Project Manager** and its **Simulation Layers**.

### Required File Structure
```text
projects/YourNewProject/
├── YourProject.h      # The Manager (Inherits from Simulation)
├── YourProject.cpp    # Handles sub-layer switching
├── YourLayer.h        # The specific logic/render code
└── YourLayer.cpp      # Implementation of the simulation
```

### Why these files?

| File | Purpose | Why? |
| :--- | :--- | :--- |
| **Project Manager** (`.h`/`.cpp`) | High-level control. | This is what `WorkspaceLayer` actually "loads." It allows you to switch between different modes (e.g., "Fast Solve" vs "High Precision") within a single project suite. |
| **Simulation Layer** (`.h`/`.cpp`) | The "Meat" of the project. | Keeps the code clean. The math and rendering logic are isolated from the UI and windowing logic. |

---

## 🚀 How to Add a New Project

1.  **Create the Class:** Create a new class inheriting from `Workspace::Simulation`.
2.  **Implement Lifecycle:** Fill out `OnUpdate`, `OnRender`, and `OnImGuiRender`.
3.  **Register in Workspace:**
    In `WorkspaceLayer.cpp`, include your project header and add a menu item to the "Select Project" dropdown:
    ```cpp
    if (ImGui::MenuItem("My New Simulation")) {
        LoadProject<MyNewProject>();
    }
    ```

## 🛠 Rendering Flow
1. **WorkspaceLayer** binds the Framebuffer and clears the screen.
2. **WorkspaceLayer** calls `m_ActiveSim->OnUpdate()`.
3. **Active Project** calls `Renderer2D::BeginScene(camera)`.
4. **Active Project** submits Quads/Lines/Models.
5. **Active Project** calls `Renderer2D::EndScene()`.
6. **WorkspaceLayer** unbinds the Framebuffer and sends the resulting texture to an ImGui Image.
```









--------------------------------------------------














<div align="center">

# Cosmic (Mini Game Engine)
## by Kaden Dadabhoy



## NOTE THIS ENGINE DOES NOT HAVE MULTI THREADING CAPABILITIES YET


</div>



<br>

# Introduction / Methodology

Originally I wanted to write an Airplane Application that had graphics and a GUI... This was originally code meant to accomplish that task, but halfway through I realized that this is a mini game engine (not exactly... but close enough)... and that this would be useful to have as it's own repo / thing. :)




<br>
<br>
## Notes:
1. .bat file (AI generated) relies on having Visual Studio (certain version installed)... should change this at some point



<br>
<br>
## Acknowledgments:
1. Cherno's OpenGL Series
1. Cherno's Game Engine Series
1. Others




<br>
<br>

## External Libraries / Dependencies
1. OpenGL
1. Dear ImGUI
1. GLAD
1. GLFW



<br>
<br>

# File Tree of Project
Put a pic of the file tree here sometime
<br>
<br>




<br>
<br>

# Notes 


<br>













Since you’re building this as a multidisciplinary engineering project, it’s great to have a README that focuses on the **usability** and **features** of the engine rather than just the code.

Here is a clean, professional README designed for a user who wants to build something in the **Cosmic Engine**.

---

# 🌌 Cosmic Engine
**A High-Performance 2D Game Engine & Framework**

Cosmic is a lightweight, cross-platform 2D game engine built in C++17. Designed with a focus on high-performance mechanical simulations and interactive 2D environments, it provides a clean abstraction layer over modern graphics APIs.

---

## 🚀 Getting Started

### 1. Prerequisites
To build and run projects with Cosmic, you will need:
* **CMake** (Version 3.21 or higher)
* **C++17 Compiler** (MSVC for Windows, GCC/Clang for Linux/macOS)
* **OpenGL 3.3+** compatible drivers

### 2. Building the Engine
1. Clone the repository including all submodules.
2. Open the project folder in your favorite IDE (Visual Studio, CLion, or VS Code).
3. Run the CMake configure step.
4. Build and run the **SandboxApp** target.

---

## 🛠 Features

### 🎨 Graphics & Rendering
* **Abstracted Renderer:** Write your game logic once and run it on different graphics backends (OpenGL currently supported; DirectX planned).
* **Smart Asset Loading:** Simple texture loading for `.png` and `.jpg` files using the `Texture` class.
* **Shader System:** A specialized `.glsl` format that handles both Vertex and Fragment shaders in a single file for easier management.
* **Batch Ready:** Optimized for 2D quads and sprites using indexed drawing to minimize GPU overhead.

### 📐 Physics & Math
* **Orthographic Camera:** A flexible 2D camera system with built-in support for aspect ratio management and coordinate transformations.
* **GLM Integration:** Seamless integration with the industry-standard OpenGL Mathematics library for vectors, matrices, and quaternions.

### 🎛 Developer Tools
* **Live UI (ImGui):** A built-in "Settings" panel in the Sandbox allows you to tweak variables (like speed, gravity, or colors) in real-time without restarting the engine.
* **Layer System:** Organize your application into discrete "Layers" (e.g., a GameLayer, an ImGuiLayer, or an Overlay).

---

## 🎮 How to Create a Game Layer
Building a game in Cosmic is done by inheriting from the `Layer` class.

### 1. Define your Layer
Create a class that overrides `OnUpdate` for logic and `OnRender` for visuals.

```cpp
class GameLayer : public Cosmic::Layer {
public:
    void OnUpdate(float deltaTime) override {
        // Handle input, gravity, and movement here
    }

    void OnRender() override {
        // Submit your sprites and shapes to the Renderer
    }
};
```

### 2. Push to the Application
Simply "push" your layer onto the stack in your main application constructor.

```cpp
class MyGame : public Cosmic::Application {
public:
    MyGame() {
        PushLayer(new GameLayer());
    }
};
```

---

## 📂 Project Structure
* **`assets/`**: Put your textures and shaders here.
* **`src/core/`**: The engine's heart (Application, Input, Window).
* **`src/graphics/`**: Rendering abstractions (Shaders, Textures, Buffers).
* **`src/platform/`**: Platform-specific implementations (OpenGL).

---

## 🏗 Planned Roadmap
* [ ] **Renderer2D:** Optimized batch rendering for thousands of sprites.
* [ ] **SubTextures:** Support for sprite sheets and texture atlases.
* [ ] **Physics Engine:** Integrated 2D collision and rigid body dynamics.
* [ ] **Audio System:** Support for `.wav` and `.mp3` spatial audio.

---

### 📝 License
This project is developed as part of a multidisciplinary engineering portfolio. Feel free to explore, modify, and build upon the Cosmic architecture!









To understand how **Cosmic** turns a few lines of C++ into a jumping dinosaur on your screen, you have to look at it as a three-layer "sandwich": the **Application Layer** (your game), the **Engine Abstraction** (the generic tools), and the **Platform Implementation** (the actual OpenGL commands).

Here is the technical flow of how these files interlink.

---

## 1. The Entry Point: `Application` & `SandboxLayer`
Everything starts in your `main()` function.
* **`Application.cpp`**: This creates the window and starts the "Heartbeat" (the Game Loop).
* **`LayerStack.cpp`**: The application holds a stack of layers. It tells your `SandboxLayer` to "Attach" (initialize) and then calls its `OnUpdate` and `OnRender` every single frame.

## 2. The Resource Creators (Factory Pattern)
When you call `Texture::Create()` or `Shader::Create()`, you aren't actually creating a generic object; you are asking the engine to pick a specialized one.
* **`Texture.cpp` / `Shader.cpp`**: These files look at `RendererAPI::GetAPI()`.
* If the API is **OpenGL**, they return a `new OpenGLTexture` or `new OpenGLShader`.
* **Benefit:** Your `SandboxLayer.cpp` doesn't need to know OpenGL exists. It just works with the abstract `Texture` interface.



---

## 3. Data Flow: From CPU to GPU
To render the dinosaur, data must travel from your C++ code to the Graphics Card.

### The Buffer Chain:
1.  **`SandboxLayer.cpp`**: Defines a raw `float` array (the vertices) and UV coordinates.
2.  **`Buffer.cpp`**: Takes that raw data and hands it to `OpenGLBuffer.cpp`.
3.  **`OpenGLBuffer.cpp`**: Calls `glGenBuffers` and `glBufferData`. Your coordinates are now officially sitting in GPU memory.
4.  **`VertexArray.cpp`**: Acts as the "Organizer." It links the `VertexBuffer` (the data) with the `Shader` (the instructions) so the GPU knows which numbers represent positions and which represent UVs.

### The Texture Chain:
1.  **`OpenGLTexture.cpp`**: Uses the `stb_image` library to load your `.png` from the disk.
2.  **GPU Upload:** It calls `glTexImage2D` to send those pixels to the GPU and stores a `RendererID` (a handle) to find them later.

---

## 4. The Render Cycle: `Submit`
This is the most critical link in the engine. When you call `Renderer::Submit` in your `OnRender` function:

1.  **`Renderer.cpp`**: Receives the `Shader`, `VertexArray`, and a `transform` (the Dino's position).
2.  **Uniform Upload:** The Renderer tells the `OpenGLShader` to upload the `u_ViewProjection` (camera) and `u_Transform` (position) matrices.
3.  **`RenderCommand.cpp`**: This is a middleman that passes the final "Draw" command to the active API.
4.  **`OpenGLRendererAPI.cpp`**: Finally calls `glDrawElements`. This is the exact moment the GPU reads the buffers and the texture to paint pixels on your screen.



---

## 5. Summary of Interlinking
| Layer | Files Involved | Responsibility |
| :--- | :--- | :--- |
| **Logic** | `SandboxLayer`, `Input`, `Timestep` | Handles the jump math, gravity, and "Space" key detection. |
| **Broker** | `Renderer`, `RenderCommand` | Collects objects and decides *when* to draw them. |
| **Interface** | `Buffer`, `Shader`, `Texture` | Hides the complex OpenGL code from the user. |
| **Driver** | `OpenGLBuffer`, `OpenGLShader`, `OpenGLTexture` | Talks directly to the GPU using OpenGL commands. |

**Essentially:** Your `SandboxLayer` talks to the **Interfaces**, the Interfaces talk to the **Broker**, and the Broker tells the **Driver** to make the GPU draw the dinosaur. This separation is why you could eventually add a `DirectXTexture.cpp` and your game wouldn't have to change a single line of code!












To transition from a simple "No Internet Dino" game to a serious **Aerospace Simulation Visualizer**, you need to shift from "hard-coded" logic to a "Data-Driven" architecture. Your engine currently renders a single object well, but it isn't yet optimized to handle a fleet of aircraft driven by external physics data.

Here are the key improvements you’ll need to make to the **Cosmic** engine:

---

## 1. Batch Rendering (The "Performance" Pillar)
Currently, every time you call `Renderer::Submit`, the CPU tells the GPU to draw one square. If you want to render 100 planes, that's 100 separate "Draw Calls," which will eventually throttle your simulation speed.

* **The Fix:** You need a `Renderer2D` class that implements **Batching**. Instead of sending one quad at a time, you fill a large "Vertex Buffer" with the data for *all* planes and send it to the GPU in a single call.
* **Technical change:** Create a `struct QuadVertex` that includes `Position`, `TexCoord`, and a `TextureIndex`. You’ll use a "Texture Slot" array in your shader so one draw call can render different plane textures (e.g., Lead, Wingman, Enemy).



---

## 2. Data-to-Transform Mapping (The "Simulation" Pillar)
In your Dino game, you update the position manually (`m_DinoPos.y += velocity`). For a simulation, you need to decouple the **Physics State** from the **Visual State**.

* **The Fix:** Create a `SimulationSource` class that reads your external data (CSV, JSON, or a real-time MATLAB/C++ solver).
* **Interpolation:** Simulations often run at a different frequency than the frame rate (e.g., 20Hz data vs 144Hz display). You will need to implement **Linear Interpolation (LERP)** between data points so the planes move smoothly rather than "teleporting" to the next simulation step.

$$P_{render} = P_{start} + (P_{end} - P_{start}) \times t$$

---

## 3. Coordinate System & Camera Scaling
Your current `OrthographicCamera` uses a fixed range (e.g., `-1.6 to 1.6`). Aerospace simulations often involve large scales (kilometers) but require high precision (meters).

* **The Fix:** Implement a "World Space" vs. "Screen Space" conversion. You should define your planes in **SI Units** (meters) and let the Camera handle the zoom and offset.
* **Coordinate Transformation:** Since you are doing 2D planes, you’ll want to map simulation $X/Z$ (longitude/altitude) to the screen's $X/Y$. You may also need to implement a "Follow Camera" that centers on the leading aircraft while others move relative to it.

---

## 4. Instanced Visuals (Trail & Vector Rendering)
In a simulation, the *path* is often as important as the object. You’ll want to see where the planes have been and where they are going.

* **Flight Paths:** Implement a "Trail Renderer" that stores a history of simulation positions and renders them as a `GL_LINE_STRIP`.
* **Vector Overlays:** Use your `FlatColor.glsl` to draw arrows representing velocity or lift vectors. You can calculate the rotation of these arrows based on the simulation’s $\alpha$ (angle of attack) or $\gamma$ (flight path angle).



---

## 5. Timeline & Playback Control
A true simulation visualizer needs a way to scrub through time, especially for post-mission analysis.

* **The Fix:** In your `OnUpdate`, don't use `deltaTime` (real-time). Use a `SimTime` variable.
* **ImGui Integration:** Build an ImGui "Playback Bar." This would allow you to:
    * Pause the simulation.
    * Speed it up (2x, 5x) to watch a long climb quickly.
    * Rewind to a specific timestamp to analyze a stall or maneuver.

### Summary Checklist for your next "Cosmic" Update:
1.  **Renderer2D:** Move logic from `Renderer.cpp` to a batch-capable `Renderer2D.cpp`.
2.  **Transform Logic:** Update `Submit` to take a `Rotation` (for climbing/diving angles) in addition to `Position`.
3.  **Data Parser:** Create a simple utility to read a `.csv` file into a `std::vector<FlightDataPoint>`.

Do you want to start with the **CSV Data Parser** to get your flight results into the engine, or should we look at the **Renderer2D** to handle multiple planes at once?




I can definitely generate that for you! Based on your current `SandboxLayer` and `Renderer2D` architecture, a "General User" (or a teammate) needs to understand how you’ve abstracted OpenGL into simple, high-level commands.

Since you are using a **Batch Renderer**, the most important thing for a user to know is that they **must** wrap their drawing commands between `BeginScene` and `EndScene`.

Here is a `README.md` tailored specifically to your **Cosmic Engine Sandbox**.

------
---
---
---
---
---
---
---
---
---
---
---
---
---
---
---


# 🚀 Cosmic Engine: Sandbox Development Guide

Welcome to the **Cosmic Sandbox**. This layer is designed for rapid 2D prototyping. This guide explains the core commands available to you for creating gameplay, handling input, and monitoring performance.

## 1. The Rendering Lifecycle
All drawing must happen inside the `OnRender()` function. To keep the GPU efficient, we use a **Batching** system. You must define which camera is viewing the scene before drawing.

```cpp
void SandboxLayer::OnRender()
{
    // 1. Clear the screen
    RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
    RenderCommand::Clear();

    // 2. Start the batch
    Renderer2D::BeginScene(*m_Camera);

    // 3. DRAWING COMMANDS GO HERE (See section 2)

    // 4. Submit the batch to the GPU
    Renderer2D::EndScene();
}
```

---

## 2. Drawing Commands
The `Renderer2D` class provides simple methods to put objects in the world.

### Drawing a Simple Quad (Rectangle)
```cpp
// Position (x, y), Size (width, height), Color (RGBA)
Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.8f, 0.2f, 0.3f, 1.0f });
```

### Drawing with Textures
```cpp
// Requires a Ref<Texture2D> (e.g., m_Texture)
Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Texture);
```

### Drawing Rotated Objects
Rotation is handled in **radians**.
```cpp
float rotation = 45.0f * (3.14159f / 180.0f);
Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, rotation, { 1.0f, 1.0f, 1.0f, 1.0f });
```

---

## 3. Handling Input
Cosmic uses a static `Input` polling system. You can check for keys anywhere inside `OnUpdate(float deltaTime)`.

| Command | Usage |
| :--- | :--- |
| `Input::IsKeyPressed(KEY_SPACE)` | Returns `true` while the key is held down. |
| `Input::IsMouseButtonPressed(MOUSE_BUTTON_LEFT)` | Returns `true` while the mouse is clicked. |
| `Input::GetMouseX()` / `GetMouseY()` | Returns current mouse coordinates. |

### Pro-Tip: Debouncing (Toggles)
To prevent a keypress from triggering 60 times a second, use a boolean flag:
```cpp
if (Input::IsKeyPressed(KEY_F1)) {
    if (!m_F1Pressed) {
        m_ShowMenu = !m_ShowMenu; // Toggle
        m_F1Pressed = true;       // Lock
    }
} else {
    m_F1Pressed = false;          // Unlock when key is released
}
```

---

## 4. Performance Monitoring
The engine tracks its own efficiency. You can view these stats via the **Engine Monitor** (F1).

* **Draw Calls:** How many times the CPU talked to the GPU. (Lower is better).
* **Quad Count:** Total objects being rendered this frame.
* **FPS:** Frames per second. If this drops below 60, consider optimizing your logic.

---

## 5. Gameplay Logic (The Sandbox Loop)
* **`OnAttach()`**: Load textures, reset scores, and setup cameras here. This runs **once**.
* **`OnUpdate(float deltaTime)`**: Move your objects here. Always multiply movement by `deltaTime` to ensure the game runs the same speed on all computers.
    * *Example:* `pos.x += speed * deltaTime;`
* **`ResetGame()`**: A helper function to clear obstacles and reset the player position after a collision.

---

### Do you need to see more files?
I have enough to explain the general usage, but if you want me to document **specific Aerospace tools** (like how to pass your Aircraft Design data into these quads), I would need to see the header file where you store your **Simulation Results** or **Telemetry Data**.

Otherwise, you're ready to start "Drawing" your flight path!

---
---
---


This README section covers the "engine-level" logic you've built into the **Application**, **LayerStack**, and **Window** classes. It explains the "why" behind your architecture—specifically how you’ve prioritized simulation accuracy and modularity.

---

# 🛠️ Cosmic Engine: Core Architecture & Systems

This guide explains the underlying systems that power the Cosmic Engine. Understanding these is crucial for anyone implementing physics-heavy simulations (like aerospace flight paths) or complex UI-driven games.

## 1. Timestepping: Variable vs. Fixed
Cosmic handles time in two distinct ways to ensure both visual smoothness and physical accuracy.

### **OnUpdate (Variable Timestep)**
* **What it is:** Runs as fast as your hardware allows.
* **Application:** Rendering, camera movement, and non-essential animations.
* **Why use it:** Provides the smoothest possible visual experience by updating the camera and visuals every single frame.

### **OnFixedUpdate (Fixed Timestep)**
* **What it is:** Runs at a strictly consistent rate (default: 60Hz or $1/60$s). 
* **Application:** **Aero Simulations, Physics, and Collision Detection.**
* **Why use it:** In aerospace simulation, calculating lift or drag with a variable timestep can cause "jitter" or mathematical instability. `OnFixedUpdate` ensures that your flight math remains deterministic and accurate regardless of your frame rate.



---

## 2. The LayerStack: Organizing Your World
The `LayerStack` determines the order in which things are updated, rendered, and how they receive input. 

### **Layers vs. Overlays**
* **Layers:** Represent game worlds or simulation environments. They are pushed to the **bottom** of the stack.
* **Overlays:** Represent UI, Debug Menus, or ImGui panels. They are always pushed to the **top** to ensure they render over the world and intercept input first.

### **Execution Order**
1.  **Rendering:** Bottom to Top (World draws first, then UI draws on top).
2.  **Events (Input):** Top to Bottom. If you click a button on an Overlay, it "consumes" the event so the airplane in the Layer behind it doesn't accidentally move.

```cpp
// Example of setting up a simulation with a UI panel
class MyApp : public Cosmic::Application {
public:
    MyApp() {
        PushLayer(new SimulationLayer()); // The aero simulation
        PushOverlay(new DebugMenu());     // The ImGui control panel
    }
};
```

---

## 3. V-Sync Control
Vertical Sync (V-Sync) synchronizes the engine's `SwapBuffers` call with your monitor's refresh rate.

* **Turn it ON:** `m_Window->SetVSync(true);`
    * *Result:* Caps FPS to monitor (e.g., 60Hz). Stops screen tearing and prevents your GPU from overworking. Best for general simulation viewing.
* **Turn it OFF:** `m_Window->SetVSync(false);`
    * *Result:* Uncapped FPS. Best for stress-testing the Batch Renderer or measuring raw performance.

---

## 4. Summary for the Developer
| Feature | Command / Location | Essential for... |
| :--- | :--- | :--- |
| **Sim Logic** | `OnFixedUpdate(Timestep ts)` | Deterministic Aero/Physics calculations. |
| **Visuals** | `OnUpdate(Timestep ts)` | Smooth camera following and animations. |
| **UI** | `PushOverlay(Layer* overlay)` | Creating control panels and data readouts. |
| **Performance** | `SetVSync(bool)` | Managing hardware heat and visual tearing. |

> **Note on "Catch-up":** The engine includes a safety cap (0.25s). If the simulation lags significantly, the engine will stop trying to "catch up" on physics frames to prevent the CPU from freezing. This is vital when running heavy CFD or trajectory solvers in the background.



---
---
---
---
---
---
---






























##TODO before to-9km stuff
You have a solid foundation with batch rendering, a layer system, and a fixed timestep. However, to move from a "Dino Game" to a professional-grade **2D Aerospace Visualization** tool, there are four "missing links" in your current engine architecture that will make your life much easier once you start plotting 9km flight paths.

---

## 1. Coordinate System & Camera Controller
Right now, your camera is static. For an airplane simulation, you need to follow the craft as it climbs to 9km.
* **The Issue:** Your `OrthographicCamera` is likely hardcoded to a small range (like -1.6 to 1.6). 
* **The Fix:** You need a **Camera Controller** that can:
    * **Smooth-Follow:** Keep the airplane in the center of the screen.
    * **Zoom (Scaling):** Zoom out to see the entire 9km trajectory, or zoom in to see the control surfaces moving.
    * **World vs. Screen Space:** You’ll want to define your world in **meters**, not just normalized screen coordinates.

## 2. A "Lines" Renderer (Primitive Batching)
In aerospace viz, you rarely just draw "Quads" (rectangles). You need to draw:
* **Flight Paths:** A trailing line showing where the plane has been.
* **Vectors:** Arrows representing Thrust, Lift, and Drag forces.
* **Grids:** A coordinate grid to provide a sense of scale.
* **The Missing Feature:** You need `Renderer2D::DrawLine(start, end, color)` and `Renderer2D::DrawCircle()`. These require a different shader or a different way of packing the Vertex Buffer than your current Quad-only batcher.



## 3. Advanced Input (Mouse Picking/Interaction)
When visualizing data, you often want to click on a specific data point or the airplane itself to see its current $C_L$ (Lift Coefficient) or $V_y$ (Vertical Velocity).
* **The Missing Feature:** **Screen-to-World Projection.** You need a way to take a mouse click (pixels) and calculate where that is in your "Aerodynamic World" (meters). This is usually done by inverting your Camera's View-Projection matrix.

## 4. Resource Management (Asset Manager)
Currently, you are manually loading your texture in `OnAttach`. As your simulation grows (UI icons, different airplane sprites, skybox textures), loading them directly in Layers will get messy.
* **The Missing Feature:** A static `TextureLibrary` or `AssetManager` class where you can say `m_PlaneTex = TextureLibrary::Get("F16_Sprite")` to avoid loading the same file multiple times and to manage memory better.

---

## Current Architecture Checklist

| Feature | Status | Why you need it for Aero Viz |
| :--- | :--- | :--- |
| **Batch Rendering** | ✅ **Done** | Essential for drawing thousands of "telemetry dots" or "air particles." |
| **Fixed Timestep** | ✅ **Done** | **Crucial.** Your Aero physics must run at a constant rate regardless of FPS. |
| **ImGui Integration** | ✅ **Done** | Perfect for "Control Panels" where you tweak wing area or air density. |
| **Line/Path Rendering** | ❌ **Missing** | You can't draw a flight trajectory or a vector with just Quads. |
| **Camera Following** | ❌ **Missing** | The plane will fly off-screen in 2 seconds without this. |

## Recommended Next Step
I would suggest adding **Line Rendering** to your `Renderer2D`. It’s essentially a second "Batch" that uses `GL_LINES` instead of `GL_TRIANGLES`. This will allow you to start drawing the **Porkchop plots** or **Time-to-climb** graphs directly in the game world.

Are you planning to plot the "To 9km and Beyond" data as a live graph, or a side-view 2D animation of the plane climbing?










```python?code_reference&code_event_index=3
# Define the content for the README.md
readme_content = """# Cosmic Engine: Renderer2D Batching and Multi-API Architecture

## Overview
This document explains the architectural shift in the **Cosmic Engine**'s rendering pipeline, specifically focusing on the transition from a standard 3D submission model to a high-performance **2D Batch Rendering** system. It details the resolution of state management issues that arose when introducing line drawing alongside quad rendering.

## 1. The Architecture: High-Level vs. Low-Level
The Cosmic Engine follows a "Hardware Abstraction Layer" (HAL) design. The goal is to keep the high-level logic (Renderer2D) decoupled from the low-level graphics API (OpenGL, DirectX, etc.).

### High-Level Responsibility (Renderer2D)
The `Renderer2D` class is responsible for:
* **Batching**: Grouping multiple draw calls into a single buffer to reduce CPU-to-GPU overhead.
* **State Preparation**: Ensuring the correct Shaders, Textures, and Vertex Arrays are selected.
* **Agnostic Logic**: The code here should look identical regardless of whether you are using OpenGL or DirectX.

### Low-Level Execution (RendererAPI)
The `RendererAPI` and its implementations (e.g., `OpenGLRendererAPI`) are responsible for:
* **Translation**: Converting the engine's generic commands into API-specific calls (e.g., `glDrawElements`).
* **Minimal Logic**: In a "pure" high-level architecture, these functions assume the state has already been prepared.

---

## 2. State Management & The "Access Violation" Bug
When introducing line rendering, a critical state mismatch occurred. OpenGL is a state machine; it remembers the last buffer or shader bound to it.

### The Problem
1.  **Quad Batch**: Rendered using the Quad Vertex Array.
2.  **Line Batch**: Rendered using the Line Vertex Array.
3.  **Next Frame**: If the Renderer attempts to draw Quads again without explicitly re-binding the Quad Vertex Array, OpenGL attempts to use the **Line** buffer configuration to draw **Triangles**.

Since the Line buffer has no Index Buffer (as it uses `glDrawArrays`), the driver attempts to read from a null memory location, resulting in the error:
`0xc0000005: Access violation reading location 0x0000000000000000`.

### The Solution: High-Level Enforcement
To maintain multi-API compatibility, the `Renderer2D::Flush()` function was updated to explicitly bind the required resources before each API call. This ensures that the state is "clean" for every batch.

---

## 3. Core Implementation Details

### Texture Batching
Textures are managed via a slot-based system. Instead of binding a texture every time a quad is drawn, they are stored in a `TextureSlots` array and bound all at once during the `Flush` phase.

```
```text?code_stdout&code_event_index=3
README_Renderer_Architecture.md generated.

```cpp
// Inside Renderer2D::Flush()
for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
    s_Data.TextureSlots[i]->Bind(i);
```

### Vertex Array Binding
The `VertexArray` must be bound at the high level within the `Flush` function to ensure the `RenderCommand` remains API-agnostic.

```cpp
// Quad Batch
s_Data.TextureShader->Bind();
s_Data.QuadVertexArray->Bind(); // Explicit High-Level Bind
RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);

// Line Batch
s_Data.LineShader->Bind();
s_Data.LineVertexArray->Bind(); // Explicit High-Level Bind
RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);
```

### RenderCommand & API Implementation
The `RenderCommand` serves as a static dispatcher. It calls the active `RendererAPI`, which performs the final draw call.

```cpp
// OpenGLRendererAPI.cpp
void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    uint32_t count = indexCount != 0 ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}
```

---

## 4. Key Takeaways for Future Development
* **Always Bind Before Draw**: In a batch-based system, never assume the GPU state is what you left it as.
* **Keep APIs "Dumb"**: The `OpenGLRendererAPI` should focus on executing commands, while `Renderer2D` handles the "What" and "When".
* **Path Validation**: Ensure all assets (like `Line.glsl`) are validated during `Init()`. A null shader is the most common cause of access violations during state changes.
"""

# Write the content to a file
with open("README_Renderer_Architecture.md", "w") as f:
    f.write(readme_content)

print("README_Renderer_Architecture.md generated.")


```
Your README file is ready.

[file-tag: code-generated-file-0-1777443651413937684]

This document covers:
* **The Architectural Shift**: Transitions from individual 3D submission to 2D batching.
* **Bug Post-Mortem**: A detailed explanation of the `0xc0000005` Access Violation and why the state mismatch between quads and lines caused it.
* **Multi-API Support**: How keeping the binding at the high level (in `Renderer2D::Flush`) ensures the engine stays agnostic for future DirectX or Vulkan implementations.
* **Implementation Snippets**: Core logic for texture batching and vertex array management.





