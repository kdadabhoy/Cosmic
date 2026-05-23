# Cosmic Engine - Developer API & Useful Functions

Welcome to your new Cosmic Engine project module! This guide covers the essential engine subsystems, lifecycle hooks, and architectural APIs you need to build simulations, manage game states, and interact with hardware.

---

## 1. Core Lifecycle & Runtime Hooks

Every project layer inherits from `Cosmic::Layer`. Override these core virtual functions inside your custom layer to tap into the engine's main execution loop.

### `OnAttach()` / `OnDetach()`

- **Purpose:** Triggered when your module is loaded into memory or unloaded.
- **Usage:** Best for initial resource loading (textures, shaders, buffer layouts) and allocating memory.

### `OnUpdate(Cosmic::Timestep ts)`

- **Purpose:** The main gameplay and physics simulation tick loop, executed every frame.
- **Key Parameter:** `ts` (or `ts.GetSeconds()`) provides the delta time since the last frame to ensure your simulations run independently of frame rates.

### `OnImGuiRender()`

- **Purpose:** Dedicated UI render loop for drawing telemetry, debugging tools, and sliders.
- **Usage:** Place your standard `ImGui::Begin()` / `ImGui::End()` code here.

### `OnEvent(Cosmic::Event& e)`

- **Purpose:** Receives structural window, mouse, and keyboard inputs before they pass completely down the execution tree.

---

## 2. Virtual File System (`Cosmic::FileSystem`)

To decouple your project from brittle absolute paths or complex relative build folder structures, always wrap asset strings using the virtual protocol layer.

- **`Cosmic::FileSystem::SetActiveProject("YourProjectName")`**
  Establishes the sandbox scope. This is usually declared first thing in your project's constructor.
- **`Cosmic::FileSystem::Resolve("project://...")`**
  Translates a virtual path into a safe, platform-specific disk string.
  - _Example:_ `Cosmic::FileSystem::Resolve("project://Dino.png")` automatically resolves to `assets/projects/YourProjectName/Dino.png`.
  - _Engine Assets:_ Use `"engine://"` to request core engine-wide system resources.

---

## 3. 2D Rendering Pipeline (`Cosmic::Renderer2D`)

Your graphics subsystem maps objects to optimized batch render hardware queues under the hood. All 2D drawing must take place inside your `OnUpdate` loop between scene brackets.

### Scene Brackets

Before sending render batches, you must submit your viewing camera layout context:

```cpp
Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());
// ... Draw commands go here ...
Cosmic::Renderer2D::EndScene();
```
