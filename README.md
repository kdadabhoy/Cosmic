<div align="center">

# Cosmic (Mini Game Engine)

## by Kaden Dadabhoy

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