# 3D Plan — Sim-Grade 3D Viewport (not a full 3D engine)

> **Decision (2026-07-01):** Cosmic gets a **sim-grade 3D viewport**, not a full 3D engine tier.
> The driver is the Viper UAV simulator ([`04-uav-sim-app-plan.md`](04-uav-sim-app-plan.md)): it needs
> to *see* a 6DOF vehicle — attitude, trajectory, ground reference, an FPV inset — with engineering
> clarity, not visual fidelity. Meshes + lines + grid + depth + perspective camera cover 100% of that.
> Lighting/PBR/shadows/glTF/skeletal animation are explicitly **out of scope** (revisit only if a
> future project needs them; a "Stage 4" sketch is included so the door stays open).

## Why this is the right call

- **Everything below the renderer is already 3D-capable** (verified):
  - `RendererAPI` is a clean abstract backend (`Cosmic/src/renderer/RendererAPI.h`); `Renderer3D` slots beside `Renderer2D` exactly like `Renderer2D` sits on it today.
  - `BufferLayout`/`VertexArray` support arbitrary vertex formats + instancing (`Cosmic/src/graphics/Buffer.h` — `Instanced` flag).
  - Depth testing is already enabled globally (`OpenGLRendererAPI.cpp:20`) and the FBO already has a `GL_DEPTH24_STENCIL8` attachment (`OpenGLFrameBuffer.cpp:88–91`) — it just isn't exposed through `FrameBuffer.h`.
  - `TransformComponent` already stores `vec3 Position` + `vec3 Rotation` (Euler, degrees) and computes a full 3D matrix (`Components.h:47–56`); only `Scale` is `vec2`.
  - glm ships with `gtc/quaternion` — nothing new to vendor.
- **What's genuinely missing** is thin: a perspective camera, a small mesh/line pipeline, shaders, and an orbit controller. That's weeks, not months.
- The 2D pipeline, all existing apps, and the docked ImGui workspace remain untouched. 3D renders into the same framebuffer/RenderPass flow the 2D path uses.

---

## Stage 1 — Perspective camera + 3D immediate lines (the foundation)

**New files** (mirror existing naming/style):

| File | Contents |
| --- | --- |
| `Cosmic/src/camera/PerspectiveCamera.h/.cpp` | fov/aspect/near/far projection; position + orientation (store a quaternion internally, expose `SetPosition`, `LookAt`, `GetViewProjection`). Mirror `OrthographicCamera`'s interface shape so `RenderPass` can take either. |
| `Cosmic/src/camera/OrbitCameraController.h/.cpp` | target + distance + yaw/pitch; LMB-drag orbit, RMB/MMB pan, scroll zoom; consumes the same events `OrthographicCameraController` does. This is the "editor" camera for the sim viewport. |
| `Cosmic/src/renderer/Renderer3D.h/.cpp` | `BeginScene(const PerspectiveCamera&)` / `EndScene`; Stage 1 draws: `DrawLine(vec3 a, vec3 b, vec4 color)`, `DrawGrid(plane, extent, step, color)`, `DrawAxes(mat4 transform, float size)`, `DrawWireBox(mat4, vec4)`. Batched line pipeline copied from `Renderer2D`'s line batch (same staging pattern, `MaxLines`, flush-on-full). |
| `Cosmic/assets/shaders/Line3D.glsl` | trivial VP-transform vertex + flat color fragment, `#type` blocks per the shader contract (README §10). |

**Engine touch-points (small, surgical):**
1. `RenderPass`/`Renderer2D::PushRenderPass` currently take `OrthographicCamera` — **do not generalize them yet**. `Renderer3D::BeginScene` manages its own VP matrix (mirroring how `Renderer2D` does); a shared `Camera` base class is a Stage-4 refactor.
2. Expose depth control on `RenderCommand`: `SetDepthTest(bool)` already-on is fine, but add `Clear` variants if a 3D pass inside an ImGui-composited frame needs its own depth clear.
3. `FrameBuffer.h`: add `GetDepthAttachmentRendererID()` (the attachment already exists — one getter).

**Acceptance:** a demo layer shows a grid + axes + a wire box orbiting under mouse control, depth-correct, inside the existing Viewport panel.

## Stage 2 — Meshes

| File | Contents |
| --- | --- |
| `Cosmic/src/graphics/Mesh.h/.cpp` | `Mesh::Create(vertices, indices)` + `Mesh::CreateFromOBJ(path)` (positions + normals only; tinyobj-style minimal parser hand-rolled or vendor `tinyobjloader` single header into `Cosmic/dependencies/`). Owns a `VertexArray`. |
| `Renderer3D` additions | `DrawMesh(const Ref<Mesh>&, const glm::mat4& transform, const glm::vec4& color)` — one draw call per mesh (no batching; a sim scene has tens of meshes, not thousands). |
| `Cosmic/assets/shaders/Mesh3D.glsl` | VP transform + **single hardcoded directional light, N·L Lambert + ambient floor** (this is the entire "lighting system" — deliberately). |
| Primitive helpers | `Mesh::CreateBox/Cylinder/Cone/Plane` — enough to assemble a placeholder aircraft (fuselage cylinder + wing boxes + prop cones) before any OBJ exists. |

**Acceptance:** placeholder aircraft assembled from primitives, rendered with visible shading, orientable via a quaternion.

## Stage 3 — Sim-viewport conveniences (what the UAV app actually consumes)

1. **Render-to-texture viewports** — the FPV inset: render a second `Renderer3D` pass into its own `FrameBuffer` from the aircraft camera pose, show via `ImGui::Image` (the engine already renders the main scene this way — reuse the pattern; `FrameBuffer::Create` already supports independent instances).
2. **Trajectory ribbon** — `Renderer3D::DrawPolyline(span<vec3>, color)` on the line batch, for flight-path history (feed from a ring buffer).
3. **Horizon/sky** — a fullscreen gradient pass or giant sky-colored sphere; ground = textured `DrawGrid`/plane. No skybox assets needed.
4. **Overlay text/billboards** — labels in 3D space via existing SDF `Font` rendered on 2D pass using `Project(worldPos) → screen`; add `Renderer3D::WorldToScreen(vec3) → vec2` helper.
5. **ECS (optional, later):** `Scale` → `vec3` + a `MeshRendererComponent` and `Scene::OnRender3D`. **Defer** — the sim app can call `Renderer3D` directly from its layer; only promote to ECS when a second 3D app appears. (Changing `TransformComponent::Scale` to `vec3` is ABI-breaking for project DLLs — rebuild all projects when you do it.)

## Stage 4 — parked (only if ever needed)

Unified `Camera` base + `RenderPass` overloads · material-driven mesh shading · glTF · MSAA (IMPROVEMENTS §5.4) · shadows · instanced meshes for swarms.

---

## Order of work & estimates

| Step | Depends on | Size |
| --- | --- | --- |
| S1 camera + lines + grid | nothing (do after the bugfix pass) | ~3–5 focused sessions |
| S2 meshes + primitives + Lambert | S1 | ~3–4 sessions |
| S3.1 render-to-texture FPV | S1 | ~1–2 sessions |
| S3.2–3.4 ribbon/horizon/labels | S2 | ~2–3 sessions |
| ECS promotion / Stage 4 | a second 3D consumer exists | parked |

**API boundary principle (applies to all stages):** the engine ships generic verbs —
`DrawMesh/DrawLine/DrawGrid/DrawPolyline`, `PerspectiveCamera`, `OrbitCameraController`, `Mesh::CreateFromOBJ`.
Everything aircraft-shaped (attitude → transform, FPV camera pose, orbit-path preview, ROI markers)
lives in the app ([doc 04](04-uav-sim-app-plan.md)). If the app ever needs a loop like
"for each segment: DrawLine", that's fine; if it needs engine internals, the engine grows a verb instead.
