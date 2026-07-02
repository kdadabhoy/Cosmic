# 3D Plan — Sim Viewport Now, Full 3D Engine Trajectory

> **Decision (updated 2026-07-01):** Cosmic's 3D work starts as a **sim-grade viewport** (what the
> Viper simulator needs), but every piece is designed as a **permanent foundation of a full 3D
> engine tier** — nothing throwaway, nothing that has to be rewritten to get to lighting, materials,
> model import, or a 3D scene system. Stages S1–S3 serve the simulator; S4–S5 are the full-engine
> build-out and are now a real plan, not parked.

## Forward-compatibility contract (binding on all implementation)

Everything implemented in S1–S3 must obey these rules so it survives into the full engine:

1. **Camera-agnostic core.** `Renderer3D::BeginScene` has a `(const glm::mat4& viewProjection,
   const glm::vec3& cameraPos)` overload as the primitive; typed overloads
   (`PerspectiveCamera`, later `EditorCamera`, unified `Camera`) are sugar on top. Any future camera
   works without touching the renderer.
2. **Generic render-state verbs on `RenderCommand`** (`SetDepthTest`, `SetDepthWrite`, later
   `SetCullMode`) — never GL calls sprinkled in `Renderer3D`.
3. **`Mesh` is a first-class GPU resource** (`Ref<Mesh>`, factory-created like `Texture2D`) holding
   `VertexArray` + a documented vertex layout (`position, normal, uv`). UVs are in the layout from
   day one even while unused — retrofitting a vertex layout is the expensive mistake.
4. **Shaders follow the existing shader contract** (`#type` blocks, README §10) and declare the
   canonical mesh attribute layout; the Lambert shader is the *first* mesh shader, not a special case.
5. **Draw calls take transforms as `glm::mat4`** — attitude comes from quaternions upstream
   (`math/Spatial.h`), so ECS integration later is a call-site change only.
6. **No 2D regressions:** `Renderer3D` never mutates state `Renderer2D` depends on without restoring
   it (depth mask, active shader, blend). Both coexist in one frame (3D world + 2D overlays).

## Stage S1 — Perspective camera + 3D lines/grid ✅ *(done 2026-07-01)*

| File | Contents |
| --- | --- |
| `Cosmic/src/camera/PerspectiveCamera.h/.cpp` | fov/aspect/near/far; position + quaternion orientation; `LookAt`; `GetViewProjectionMatrix()`. Interface shape mirrors `OrthographicCamera`. |
| `Cosmic/src/camera/OrbitCameraController.h/.cpp` | target/distance/yaw/pitch; LMB orbit, RMB pan, scroll zoom; same event pattern as `OrthographicCameraController`. |
| `Cosmic/src/renderer/Renderer3D.h/.cpp` | `Init/Shutdown` (called from `Renderer::Init/Shutdown`), `BeginScene(mat4 vp, vec3 camPos)` + camera overload, `EndScene`; batched `DrawLine(a,b,color)`, `DrawPolyline`, `DrawGrid`, `DrawAxes(mat4,size)`, `DrawWireBox(mat4,color)`. Line batch cloned from `Renderer2D`'s line pipeline. |
| `Cosmic/assets/shaders/Line3D.glsl` | VP transform + vertex color. |
| `RenderCommand`/`RendererAPI` additions | `SetDepthTest(bool)`, `SetDepthWrite(bool)`. |
| `FrameBuffer.h` addition | `GetDepthAttachmentRendererID()` (attachment already exists in the GL layer). |

## Stage S2 — Meshes + Lambert ✅ *(done 2026-07-01)*

| File | Contents |
| --- | --- |
| `Cosmic/src/graphics/Mesh.h/.cpp` | `MeshVertex {pos, normal, uv}`; `Mesh::Create(vertices, indices)`; primitives `CreateBox/Cylinder/Cone/Plane/UVSphere`; `Mesh::CreateFromOBJ(path)` (positions+normals+uvs, triangulated faces). |
| `Renderer3D` additions | `DrawMesh(const Ref<Mesh>&, const glm::mat4&, const glm::vec4& color)`; one draw per mesh (sim scenes are tens of meshes). Future `DrawMesh(..., Ref<Material>)` overload slot documented. |
| `Cosmic/assets/shaders/Mesh3D.glsl` | one directional light, N·L Lambert + ambient floor, per-draw color uniform. Named/structured so S4 material shaders extend rather than replace it. |

## Stage S3 — Sim-viewport conveniences *(next, driven by ViperSim P4–P5)*

Render-to-texture FPV inset (second `Renderer3D` pass into its own `FrameBuffer` + `ImGui::Image`) ·
trajectory ribbon (`DrawPolyline` over a ring buffer) · horizon/sky gradient pass · ground-plane
texture · `Renderer3D::WorldToScreen(vec3)` for SDF-font labels via the 2D pass.

## Stage S4 — 3D engine foundations *(the "convert to a real 3D engine" stage)*

Ordered; each item is a PR-sized unit:

1. **Unified camera hierarchy.** `Camera` base (projection + view accessors); `OrthographicCamera`,
   `PerspectiveCamera` derive; `RenderPass`/`Renderer2D::PushRenderPass` take `const Camera&`.
   (Deliberately deferred from S1 — it touches every 2D call site, so it lands as one focused refactor.)
2. **Material-driven meshes.** `DrawMesh(mesh, transform, Ref<Material>)` using the *existing*
   `Material` class (it is shader-agnostic already); `Material::BindFull()` (bug-audit WO-11) is the
   binding path. Uniform conventions documented (`u_Model`, `u_ViewProjection`, `u_CameraPos`).
3. **3D scene integration.** `TransformComponent::Scale` → `vec3` (ABI break: rebuild all project
   DLLs); `MeshRendererComponent { Ref<Mesh>, Ref<Material>, color }`; `Scene::OnRender3D(camera)`
   rendering registry meshes; quaternion option on transforms (`RotationQuat` alongside Euler, or a
   conversion policy — decide at implementation with a migration note).
4. **Asset pipeline.** Asset cache (IMPROVEMENTS §5.1) extended to meshes; **glTF 2.0 import**
   (vendor `cgltf`, single header) as the interchange format — OBJ stays for quick primitives.
5. **Lighting v1.** Light components (directional + N point lights), Blinn-Phong, uniform-buffer
   light block. Explicitly *not* PBR yet.
6. **Multi-attachment framebuffer** (engine_analysis §5.6): color+color+depth MRT — unlocks entity-ID
   picking (replaces CPU AABB picker in 3D) and post-processing.

## Stage S5 — Full 3D engine tier *(after S4; order by need)*

Shadow mapping (single cascade → CSM) · PBR metallic-roughness + IBL/skybox · post-processing stack
(tonemap, bloom, FXAA/MSAA — closes IMPROVEMENTS §5.4) · frustum culling + render queue sorting
(opaque front-to-back, transparent back-to-front) · instanced mesh rendering (swarms/particles) ·
skeletal animation (last; only with a driving use case).

## What stays true throughout

The 2D pipeline, all existing apps, and the docked ImGui workspace remain untouched at every stage.
The engine ships generic verbs (`DrawMesh`, `DrawLine`, `SetDepthTest`, `Mesh::CreateFromOBJ`);
aircraft/domain logic stays in apps ([doc 04](04-uav-sim-app-plan.md)).

| Step | Depends on | Status |
| --- | --- | --- |
| S1 camera + lines + grid | bugfix pass | ✅ **done 2026-07-01** |
| S2 meshes + Lambert | S1 | ✅ **done 2026-07-01** |
| S3 FPV inset / ribbon / horizon | S1–S2, ViperSim P4 | next |
| S4.1–S4.6 engine foundations | S2 (any time after) | planned, ordered |
| S5 full tier | S4 | planned |

**Acceptance evidence (S1+S2):** `Projects/Engine3DDemo` — orbit camera (LMB/RMB/scroll + auto-orbit)
around a placeholder aircraft built from `CreateBox/Cylinder/Cone/Plane/UVSphere`, Lambert-shaded over
a major/minor grid, with trajectory `DrawPolyline`, `DrawAxes`, `DrawWireBox`, and a `Renderer2D`
overlay in the same frame (contract rule 6).
