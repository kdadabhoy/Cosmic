# Frame Lifecycle & GPU Resource Contract (S13.2)

> **Status:** accepted 2026-07-03 (Phase 12). This is the internals spec named by
> doc 05 §12 S13.2 — the document a second rendering backend (Vulkan native or an
> RHI) would implement. It records what the OpenGL backend *promises*, not how it
> is implemented. When engine behavior and this doc disagree, one of them is a bug:
> fix the code or amend this doc in the same PR.
>
> Companion references: `Cosmic/src/renderer/BindingPoints.h` (the binding
> registry — the seed of a future descriptor-set layout), doc 05 §0 (the API
> decision + binding rules), `docs/design/water-rendering-notes.md` (water pass
> internals), README §10 (`#type` shader contract) and §24 (teardown ordering).

---

## 1. Layering — who may talk to the GPU

```
apps / Projects/*        — engine verbs only (Renderer2D/3D, SceneRenderer, systems)
engine feature code      — RenderCommand verbs + GPU-resource objects only
RendererAPI/RenderCommand— the verb seam (SetDepthTest, DrawIndexed, DispatchCompute, …)
platform/OpenGL/         — the ONLY code allowed to name gl*/GL_* tokens
```

- **Rule 0.1 (audited):** zero raw `gl*` calls / `GL_*` enums outside
  `Cosmic/src/platform/OpenGL/`. Enforced by `tests/check_gl_conformance.ps1`
  (CI step "GL conformance audit", added S13.1). Known, deliberate exemptions:
  the windowing layer (GLFW is the *windowing* API, not the graphics API) and
  vendored dependencies (GLAD, ImGui's GL backend) — a second backend replaces
  those wholesale.
- **Rule 0.2:** no GL enums in public engine headers. Formats, filters, blend
  modes are engine enums (`TextureFilter`, `RendererAPI::BlendMode`, …)
  translated inside the platform layer.
- **New GPU features land as `RendererAPI` verbs first** (compute, SSBO, timer
  queries, instanced draw all did); feature code composes verbs.

## 2. GPU resource model

**Creation.** Every GPU resource is a factory-created `Ref<T>`:
`Shader::Create`, `Texture2D::Create`, `FrameBuffer::Create`, `Mesh::Create*`,
`UniformBuffer::Create`, `StorageBuffer::Create`, `InstanceSet::Create`,
`TextureCube`/`EnvironmentMap`/`ShadowMap`/`CoverageCapture`/`Water`/`Terrain`/
`ParticleEmitter` follow the same pattern. Factories switch on
`RendererAPI::GetAPI()` and return `nullptr` for `API::None` (headless tests);
callers must tolerate a null return by disabling the feature, never by crashing.

- Creation requires a **live GL context** (the engine creates it before
  `Renderer::Init`; anything created during `OnAttach` or later is safe).
- Value-type owners (`InstanceSet`, `SceneRenderer`'s members, post stacks) are
  **non-copyable**; copying would alias GPU ownership (Phase 9 hardening rule).

**Destruction.** Releasing the last `Ref` deletes the GPU object **immediately
on the calling thread** — there is no deferred-destruction queue. Two rules
follow:

1. Never drop the last `Ref` to a resource the current frame has already
   recorded a draw with (in practice: release in `OnDetach`, not mid-frame).
2. **Teardown ordering (README §24):** all engine/app `Ref`s must release while
   the context is still current — `Application` shuts layers down before the
   window dies. `OpenGL*` destructors guard with `HasCurrentContext()` as a
   last resort, but correctness relies on ordered shutdown, not the guard.

**Uploads.** Buffer updates are synchronous verb calls (`SetData` =
`glBufferSubData`-class). The one intra-frame rewrite hazard is documented at
its site: Renderer3D's auto-instancing never reuses a scratch `InstanceSet`
within a scene (one per run, pooled per scene) so an upload cannot touch a
buffer a just-issued draw still reads.

## 3. Binding-point registry (descriptor-set seed)

`renderer/BindingPoints.h` is the single source of truth. Summary as of Phase 12:

| Kind | Slot | Owner |
| --- | --- | --- |
| UBO 0 | `LightsUbo` | `Renderer3D::SetLights` ↔ `LightsBlock` |
| UBO 1 | `CameraUbo` | `Renderer3D::BeginScene` ↔ `CameraBlock` (`u_Camera`) |
| SSBO 0–7 | `AppSsbo0`+ | app/demo scratch (engine never binds these) |
| SSBO 8 | `ParticlesSsbo` | GPU particle pool (S10.1) |
| SSBO 9 | `InstancesSsbo` | `InstanceSet` `{ mat4 Model; vec4 Tint; }` (F5/S12.3) |
| Tex unit 8–10 | IBL set | irradiance / prefilter / BRDF LUT (S6.3) |
| Tex unit 11 | shadow map | directional sun depth (S6.4) |
| Tex unit 12 | snow mask | coverage capture (S11.1/F8) |
| Tex units 0..n | material-owned | `Material::BindFull` binds cached textures upward from 0 |

**Sampler-unit portability rule (binding on every shader):** every declared
sampler uniform is assigned its unit **unconditionally**, even when the feature
is off — two sampler *types* left aliasing default unit 0 is a draw-time
`INVALID_OPERATION` on strict (Mesa/ANGLE-class) drivers. `ApplySceneBindings`
implements this for the scene set; new shaders must follow it for their own.

## 4. Render-state contract

Engine defaults, asserted by `RenderCommand::Init` and **restored by anyone who
changes them** before their scope ends:

| State | Default | Changers restore because |
| --- | --- | --- |
| Depth test | ON | 2D overlay + next 3D pass assume it |
| Depth write | ON | Renderer2D batches assume it (Renderer3D's transparent stage restores it itself) |
| Cull mode | **None** | 2D sprites flip winding via FlipX/FlipY |
| Blend mode | Alpha (src-over) | Renderer2D batches assume it |

`SceneRenderer::Render`'s POST-condition restates this: the caller's FBO
re-bound, viewport `(0,0,w,h)`, depth ON/ON, cull None, blend Alpha.

## 5. The frame, pass by pass

Owner of orchestration: `renderer/SceneRenderer` (F2). Engine3DDemo remains the
low-level rig that drives the same passes by hand. Sequence (each in an F3 GPU
zone; `GpuFrameMark` at entry):

1. **Environment bake** (dirty-flag no-op normally) — `EnvironmentMap::Bake`
   renders the analytic sky into the environment cube + IBL set. Runs *between*
   passes only, never mid-pass; leaves the default FBO bound.
2. **Shadow depth** — `ShadowMap::BeginDepthPass` (own FBO, front-face cull,
   depth-only). Casters route through `SceneDrawContext` → `DrawCaster` /
   `DrawCasterInstanced` (+ terrain `RenderDepth`). Immediate draws — the S12.2
   queue is NOT in play here (no Renderer3D scene is open).
3. **Coverage capture** (F8, optional) — same shape as the shadow pass into the
   snow `CoverageCapture` target, then the coverage mask advances.
4. **Planar reflection** (optional, one primary water body) —
   `Water::BeginReflection` binds the reflection FBO and mirrors the camera with
   an oblique near plane; a full Renderer3D scene (sky → terrain → opaque
   callback) renders into it. `Renderer3D::EndScene` flushes the queue *before*
   `EndReflection` unbinds the target.
5. **Opaque HDR** — `PostProcessStack::BeginHDR` binds the `RGBA16F +
   DEPTH24STENCIL8` scene target. Skybox draws background-first, then terrain,
   then the app's opaque callback, then the ECS scene (its own Renderer3D
   scene). All mesh submissions go through the S12 queue (see §6).
6. **Transparents** (HDR target still bound) — water bodies far→near (each does
   its own refraction grab via `BlitCopy` + re-asserts the FBO), then particle
   emitters/ribbons (own SSBO pipelines), then the app's transparent callback in
   its own Renderer3D scene.
7. **Post + composite** — SSAO, bloom, god rays, heat haze, fog, underwater
   grading resolve inside the post stack; the tonemap (ACES + exposure +
   gamma) writes to the caller's LDR FBO; lens flare composites after. 2D/UI
   renders after this (contract 7: UI is LDR, post never touches it).

**FBO ownership:** each subsystem owns its targets outright (`PostProcessStack`
the HDR/post chain, `ShadowMap`/`CoverageCapture`/`Water` their depth and
reflection targets, the app/workspace the final viewport FBO). Nobody binds a
target it does not own; the pass boundary re-binds via handles captured with
`GetBoundFramebuffer`/`BindFramebufferHandle`.

## 6. Mesh submission semantics (S12 queue — the API contract)

Since Phase 12, `Renderer3D::DrawMesh`/`DrawModel` **record**; execution happens
at `Flush()`/`EndScene()`:

- **Frustum culling (S12.1):** world AABB (mesh local bounds × transform) vs.
  the pass frustum at submit. Global opt-out: `SetFrustumCullingEnabled(false)`
  (for vertex-displacing shaders that can leave the static AABB).
- **Sort (S12.2):** opaque = shader → material → mesh → near-to-far → sequence;
  transparent (`Material::SetTransparent`) = far-to-near → sequence, drawn after
  all opaques with depth writes OFF (test ON), then restored.
- **Value capture:** transform/color/entityID per call; the material by
  reference — **its values are read at flush**. Per-draw variation requires
  `Material::Clone` (Unity material-instance semantics). Scene state (lights,
  IBL/shadow/snow) is likewise flush-time.
- **State islands:** code needing custom render state around draws submits,
  calls `Renderer3D::Flush()` under that state, restores. Prefer the transparent
  flag — it removes the common case.
- **Auto-instancing (S12.3):** sorted runs ≥ 4 of identical (mesh, material)
  with `entityID == -1` and a registered instancing twin
  (`Material::SetInstancingShader`, e.g. `PBRInstanced.glsl`) collapse into one
  instanced draw through pooled scratch `InstanceSet`s. Per-instance entity IDs
  are not in the instance SSBO — ID-picked draws never auto-batch, so
  `ScenePicker` stays exact. Uniform-scale caveat: the twin derives normals from
  `mat3(Model)` (same documented limitation as `InstanceSet`).
- Lines (`DrawLine`/grid/axes) remain a separate batch flushed after the mesh
  queue at `EndScene` — debug overlay, depth-tested against the meshes.
- `Renderer3D::Statistics` (reset per frame by the app) reports submissions,
  cull count, draw calls, and both instancing paths — the S12 acceptance
  numbers surfaced in Engine3DDemo's "Performance (S12)" section and Frontier's
  GPU profiler panel.

## 7. Texture pipeline policy (S12.6 — audited 2026-07-03)

**Mip generation.**

| Path | Mips | Min filter |
| --- | --- | --- |
| `Texture2D::Create(path)` (file) | full chain, regenerated at load | trilinear |
| `Texture2D::Create(bytes)` (glTF embedded) | full chain | trilinear |
| `Texture2D::Create(w, h)` procedural | none (legacy contract) | linear |
| `Texture2D::Create(w, h, mipmapped=true)` | full chain, regenerated on `SetData` | trilinear |
| FBO attachments | never | nearest/linear per spec |

Policy: *file-loaded and decoded textures are always mipmapped; procedural
textures opt in* (the opt-in exists for tiling maps sampled at distance — the
Phase 11 water detail normals were the motivating case). `Texture::SetSampling`
overrides filtering/wrap per use (SDF font atlas). Anisotropic filtering is
deliberately absent: the 4.5-core loader ships no extensions, and no current
content is aniso-limited.

**sRGB correctness — the audited state.**

- Color texture *maps* (PBR/PBRInstanced albedo + emissive) are decoded
  sRGB→linear **in-shader** (`SrgbToLinear`, the 2.2 approximation) exactly
  once; normal/metallic-roughness/AO maps are correctly treated as linear data.
  No double conversion exists in the 3D path.
- All GPU color *storage* is `RGBA8` (gamma-encoded authored data) or
  `RGBA16F`/`RGB16F` (linear working space); hardware `GL_SRGB8_ALPHA8` views
  are **not** used. Consequence: `glGenerateMipmap` averages gamma-encoded
  texels (slightly dark mips) — accepted, invisible at current content scale,
  and the switch to sRGB storage rides the BCn decision below since both change
  the same allocation sites.
- Authored *factors* (`u_Albedo`, light colors, terrain layer colors) are
  defined to be linear-space values; they were tuned against the ACES output
  and are not converted. This is the policy the Tonemap.glsl S12.6 note
  anticipated — the HDR-on vs HDR-off A/B brightness difference is expected
  and closed as by-design.
- The 2D/UI path is gamma-space end-to-end (textures sampled and blended as
  authored, composited after tonemap) — correct for UI by construction.

**BCn compression — decision: PARKED with an unlock.** Every current texture is
procedural, engine-generated, or a small glTF sample; VRAM and bandwidth are
nowhere near the frame budget (S12.5 profiler evidence). Vendoring stb_dxt plus
a bake step buys nothing measurable today and adds an import pipeline to
maintain. **Unlock:** the first texture-heavy content project (editor-imported
photo-textured assets) *or* a measured VRAM/bandwidth limit on target hardware.
When unlocked, implement as a `package.bat` bake step to `.dds`/KTX2 with
`GL_SRGB` -capable compressed formats, and fold the sRGB-storage switch into the
same PR.

## 8. What a second backend must reproduce

The checklist a Vulkan/RHI implementation is graded against:

1. The factory seams in §2 (per-API concrete classes behind `Create`).
2. The verb set of `RendererAPI` (state, draws, compute, barriers, timer
   queries) with the §4 defaults.
3. The binding table in §3 as a descriptor-set layout; std140/std430 blocks are
   already Vulkan-compatible layouts.
4. The pass graph in §5 (render passes + attachment ownership map 1:1).
5. The queue semantics in §6 (backend-neutral by design — sorting/culling/
   instancing live above the verb seam).
6. The `#type`-contract GLSL as the single shader source (transpile, don't
   fork — doc 05 §0 rule 3).

Deliberately *not* promised yet (S13.3 records why): command-buffer recording,
pipeline objects, render-pass objects, shader reflection, multi-threaded
submit. They are Vulkan-shaped abstractions with no GL payoff; adding them
before a second backend exists would be speculative API.
