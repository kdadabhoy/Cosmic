# 3D Engine Plan — Full 3D Capability Roadmap

> **Rewritten 2026-07-01** (supersedes the "sim viewport + parked full tier" version; S1/S2 history
> preserved below). **Goal:** take Cosmic from today's sim-grade viewport to a genuine 3D engine
> tier: PBR lighting, terrain, water, snow/weather, volcanic/emissive effects, CAD-style navigation
> (SolidWorks feel), gizmos, and a credible path toward Unity-class editor workflows — **without
> ever breaking the 2D pipeline or the shipped apps.**
>
> Every stage below is broken into PR-sized items with acceptance criteria so a single item can be
> handed to an AI as one prompt. Items marked **[filler]** are safe to do out of order.

---

## 0. Graphics API decision — OpenGL now, Vulkan behind a gate

**DECIDED (2026-07-01): stay on OpenGL 4.5 core through stage S12. No Vulkan rewrite.**

**Why:** "Realistic volcanoes / water / snow" is a *techniques and content* problem, not an API
problem. Every technique in this plan (PBR+IBL, CSM shadows, FFT water, GPU particles, volumetric
fog, terrain clipmaps) shipped in AAA titles on GL4/D3D11-class hardware. A Vulkan port would
freeze visible feature work for ~2–3 months to reproduce what `platform/OpenGL/` already does,
with zero visual payoff. Vulkan pays off for *CPU-bound many-draw-call scenes* and multi-threaded
command recording — problems Cosmic does not have yet (batched 2D + tens-to-hundreds of 3D draws).

**What we adopt NOW so a future backend stays cheap (binding rules):**
1. **No raw `gl*` calls outside `platform/OpenGL/`.** Everything goes through
   `RendererAPI`/`RenderCommand` verbs (`SetDepthTest`, `SetCullMode`, `DispatchCompute`, …).
   Audit item: S13.1.
2. **No GL enums/types in public engine headers.** Formats, blend modes, texture params are engine
   enums translated in the platform layer (the `FrameBuffer`/`Texture2D` specs already do this —
   keep it that way).
3. **Shaders keep the `#type` contract** (README §10) and the canonical attribute layout; when we
   need post-GL portability we transpile — we do not hand-write two shader dialects.
4. **New GPU features land as `RendererAPI` verbs first** (compute, SSBO, indirect draw, timer
   queries), never as one-off GL calls in feature code.

**Reopen the decision (S13 gate) only when one of these is true:**
- Render-thread CPU time becomes the measured frame limiter after S12's culling/instancing/sorting.
- A required feature is GL-impossible or driver-broken (mesh shaders, hardware ray tracing,
  reliable multi-threaded resource streaming).
- A non-Windows target matters (then evaluate Vulkan-native vs. an RHI like bgfx/Diligent vs. ANGLE).

Alternatives considered: **Vulkan now** (rejected: months of parity work, kills momentum);
**bgfx/Diligent adoption now** (rejected: replaces a working renderer with a dependency and its
abstractions before we know our own requirements); **D3D11 backend** (rejected: same cost as
Vulkan, fewer future options).

---

## 1. Forward-compatibility contract (binding on all stages)

Carried forward from the original plan and extended — everything already shipped (S1/S2) obeys it:

1. **Camera-agnostic core.** `Renderer3D::BeginScene(const glm::mat4& viewProjection, const
   glm::vec3& cameraPos)` is the primitive; typed camera overloads are sugar.
2. **Generic render-state verbs on `RenderCommand`** — never GL calls in feature code (rule 0.1).
3. **`Mesh` is a first-class GPU resource** (`Ref<Mesh>`, factory-created) with the documented
   `position, normal, uv` layout. Extensions (tangents for normal mapping, skinning weights) are
   *additive* layout versions, decided per-mesh at creation.
4. **Shaders follow the `#type` contract** and declare the canonical attribute layout.
5. **Draw calls take `glm::mat4` transforms**; attitude math stays upstream (`math/Spatial.h`).
6. **No 2D regressions.** `Renderer3D` restores any state `Renderer2D` depends on; both run in one
   frame. Every stage's acceptance re-verifies a 2D overlay renders correctly.
7. **HDR-ready:** from S6 onward the 3D scene renders into a float target and tonemaps to the
   swapchain; 2D/UI composite after tonemap.
8. **The engine ships generic systems (terrain, water, particles); apps own scenarios** (the
   volcano is a *demo app*, not an engine feature).

---

## 2. Stage map

| Stage | Theme | Status |
| --- | --- | --- |
| S1 | Perspective camera, orbit controller, 3D lines/grid/axes | ✅ done 2026-07-01 |
| S2 | Meshes + primitives + OBJ + Lambert | ✅ done 2026-07-01 |
| S3 | Sim-viewport conveniences (FPV inset, ribbon, horizon, labels) | S3.1 + S3.2 ✅ 2026-07-02 (ViperSim P5); S3.3–S3.5 unpulled |
| S4 | 3D engine foundations (cameras, materials, scene, glTF, lights, MRT, compute) | planned |
| S5 | CAD navigation, ViewCube, gizmos, 3D picking | planned — **S5.1 nav is [filler], do any time** |
| S6 | Visual realism core: HDR, PBR+IBL, shadows, SSAO, bloom, AA | planned |
| S7 | Sky, atmosphere, fog, time-of-day | planned |
| S8 | Terrain system | planned |
| S9 | Water system | planned |
| S10 | GPU particles + volumetrics | planned |
| S11 | Weather/nature systems + flagship demos (volcano, snow, ocean) | planned |
| S12 | Performance & scale (culling, sorting, instancing, LOD, profiler) | planned |
| S13 | RHI hardening + Vulkan decision gate | gate |
| S14 | Game-engine tier backlog (animation, physics, editor app, …) | parking lot with unlock conditions |

Stages are ordered by dependency, not calendar — the sim track (S3) and the realism track (S4+)
interleave freely with the ViperSim phases in the master roadmap.

### S1 + S2 — shipped foundation *(reference)*

`PerspectiveCamera`, `OrbitCameraController` (LMB orbit / RMB pan / scroll zoom),
`Renderer3D` (batched lines, `DrawGrid/DrawAxes/DrawWireBox`, `DrawMesh` + Lambert `Mesh3D.glsl`),
`Mesh` primitives (`CreateBox/Cylinder/Cone/Plane/UVSphere`) + `CreateFromOBJ`,
`RenderCommand::SetDepthTest/SetDepthWrite`, `FrameBuffer::GetDepthAttachmentRendererID()`.
Acceptance app: `Projects/Engine3DDemo`. Details: git history of this file.

### S3 — Sim-viewport conveniences *(pull-as-needed; ViperSim P4–P5 pulled two)*

| Item | Contents |
| --- | --- |
| S3.1 FPV inset ✅ 2026-07-02 | second `Renderer3D` pass into its own `FrameBuffer`, shown via `ImGui::Image` — shipped app-side in ViperSim's FlightScreen (belly camera; FPV pass renders first, then rebinds the viewport FBO + viewport) |
| S3.2 Trajectory ribbon ✅ 2026-07-02 | `DrawPolyline` over a ring buffer (ViperSim flight trail); fade-by-age color option still open |
| S3.3 Horizon/sky gradient | full-screen gradient pass behind the scene (replaced by S7 later) |
| S3.4 Ground texture | textured ground plane (checker/asphalt) — needs `DrawMesh` + texture path (S4.2 material or a minimal textured-mesh shader) |
| S3.5 `Renderer3D::WorldToScreen(vec3)` | for SDF-font labels drawn by the 2D pass |

---

## 3. S4 — 3D engine foundations

Ordered; each item is one PR.

1. **S4.1 Unified camera hierarchy.** `Camera` base (view/projection accessors);
   `OrthographicCamera`/`PerspectiveCamera` derive; `RenderPass` takes `const Camera&`.
   Touches every 2D call site — one focused refactor, no behavior change.
   *Acceptance:* all existing apps compile & render identically.
2. **S4.2 Material-driven meshes.** `Renderer3D::DrawMesh(mesh, transform, Ref<Material>)` using
   the existing shader-agnostic `Material` (binding via `Material::BindFull()`); uniform
   conventions documented (`u_Model`, `u_ViewProjection`, `u_CameraPos`, `u_NormalMatrix`).
   *Acceptance:* demo draws one mesh with a custom shader material + one Lambert fallback.
3. **S4.3 3D scene integration.** `TransformComponent::Scale` → `vec3` (ABI break: rebuild project
   DLLs); optional `RotationQuat` alongside Euler with a documented conversion policy;
   `MeshRendererComponent { Ref<Mesh>, Ref<Material>, color, castShadows }`;
   `Scene::OnRender3D(const Camera&)`.
   *Acceptance:* ECS scene renders meshes; 2D scene rendering unaffected.
4. **S4.4 Asset cache + glTF import.** `AssetLibrary` keyed by resolved VFS path returning shared
   `Ref<Texture2D>/Shader/Mesh` (closes the old IMPROVEMENTS §5.1 item); vendor **cgltf** (single
   header) for glTF 2.0 meshes (positions/normals/uvs/tangents, submesh→material slots). OBJ stays
   for quick primitives.
   *Acceptance:* loading the same path twice returns the same `Ref`; a Sketchfab glTF renders.
5. **S4.5 Lighting v1.** `DirectionalLightComponent`, `PointLightComponent` (N ≤ 16 forward),
   Blinn-Phong `MeshLit.glsl`, lights uploaded via a **uniform buffer** verb
   (`UniformBuffer::Create/SetData` — new RendererAPI-backed resource).
   *Acceptance:* sun + two colored point lights on the Engine3DDemo aircraft.
6. **S4.6 Multi-attachment framebuffer (MRT).** `FrameBufferSpec` grows N color attachments with
   formats (RGBA8, RGBA16F, **R32I entity-ID**); `ReadPixel(attachment, x, y)`;
   `Clear(attachment, value)`.
   *Acceptance:* scene renders color + entity-ID; reading the ID under the cursor returns the
   entity. (Unlocks S5.4 picking and all S6 post-processing.)
7. **S4.7 Compute + storage buffers.** `ComputeShader` (`#type compute` block),
   `RenderCommand::DispatchCompute(x,y,z)` + memory-barrier verb, `StorageBuffer` (SSBO wrapper).
   GL 4.5 has all of it; this is the infrastructure S9 (FFT water) and S10 (GPU particles) build on.
   *Acceptance:* compute shader animates 1M points into a vertex buffer at 60 fps.

---

## 4. S5 — CAD navigation & editor interaction *(SolidWorks feel; gizmos)*

1. **S5.1 Navigation presets on `OrbitCameraController`** **[filler — only needs S1, do any time]**
   `SetNavigationStyle(NavStyle::Classic | NavStyle::CAD)`:
   - **CAD style (SolidWorks bindings):** **MMB drag = orbit**, **Ctrl+MMB = pan**,
     **Shift+MMB = dolly**, **scroll = zoom toward cursor** (not toward center), LMB stays free
     for selection.
   - **Orbit about point under cursor:** on MMB-down, ray/depth-probe the scene
     (`FrameBuffer` depth or entity AABBs until S4.6 exists) and orbit about the hit point;
     fall back to the current target on miss. This single behavior is most of the "SolidWorks feel."
   - Optional inertial damping (exponential smoothing on yaw/pitch/pan velocities; off by default).
   - Existing apps keep `Classic` unless they opt in.
   *Acceptance:* Engine3DDemo toggles styles at runtime; zoom-to-cursor keeps the point under the
   cursor stationary; MMB-orbit pivots about the model surface point.
2. **S5.2 Frame & view shortcuts.** `F` frames selection (or whole scene bounds) with a smooth
   distance/target blend; `Home` = default iso view; numpad-style snap views
   (Front/Back/Top/Bottom/Left/Right/Iso) as an API (`SnapView(ViewPreset)`).
   *Acceptance:* framing a selected entity fills ~70% of the viewport height at any aspect.
3. **S5.3 ViewCube / axis triad overlay.** Corner widget showing orientation; clicking a
   face/edge/corner calls `SnapView` with an animated transition. Implementation: small
   `Renderer3D` pass with its own tiny viewport + ray-vs-box picking (no ImGui hacks).
   *Acceptance:* cube tracks the camera; clicking "Front" animates to the front view.
4. **S5.4 3D picking & selection outline.** Entity-ID MRT attachment (S4.6) + `ReadPixel` →
   `EntitySelection` (reuses the existing 2D selection bus); hover highlight + selected outline
   (ID-buffer edge detect in a small post pass — no stencil complexity).
   *Acceptance:* click selects the exact mesh under the cursor incl. partial occlusion;
   outline renders behind UI.
5. **S5.5 Transform gizmos.** Vendor **ImGuizmo** (single file, MIT, ImGui-native — matches our
   stack; hand-rolling parity is weeks of math for no gain). Wrap it:
   `Gizmo::Manipulate(camera, transformComponent, Mode::Translate|Rotate|Scale, Space::Local|World,
   snap)`. Keyboard: `W/E/R` mode cycle (only when viewport hovered & no ImGui text focus).
   *Acceptance:* move/rotate/scale a `MeshRendererComponent` entity with snapping; undo hook
   deferred to S14 editor work (documented).

---

## 5. S6 — Visual realism core

Ordered; this stage is the prerequisite for anything called "realistic."

1. **S6.1 HDR pipeline.** Scene renders to RGBA16F; final **tonemap pass** (ACES + exposure
   uniform) to the target; UI composites after. Post-pass framework: `PostProcessStack` running
   fullscreen-triangle passes with ping-pong buffers.
   *Acceptance:* overbright (>1.0) values roll off instead of clipping; exposure slider works.
2. **S6.2 PBR metallic-roughness.** `PBRMaterial` params/textures: albedo, metallic, roughness,
   normal (needs tangents — extend `MeshVertex` additively per contract rule 3), AO, emissive.
   Cook-Torrance GGX + Schlick Fresnel, matching the glTF 2.0 material model so S4.4 imports map 1:1.
   *Acceptance:* glTF DamagedHelmet-class sample renders comparably to a reference viewer.
3. **S6.3 IBL + skybox.** HDRI equirect → cubemap; irradiance convolution + prefiltered specular
   mip chain + BRDF LUT (offline-at-load via S4.7 compute); skybox pass.
   *Acceptance:* metallic sphere grid (roughness 0→1) shows correct reflections under an HDRI.
4. **S6.4 Shadow mapping.** Directional sun: single 2k map + PCF first, then **3-split CSM** with
   stable texel snapping. `castShadows` on `MeshRendererComponent`; depth-only render path in
   `Renderer3D`.
   *Acceptance:* Engine3DDemo aircraft shadows the grid; no shimmer during orbit; slope bias documented.
5. **S6.5 SSAO.** Half-res hemisphere SSAO + blur, from the depth (+ normal reconstruct or a
   normals attachment via S4.6).
   *Acceptance:* contact darkening in crevices; toggleable; < 1.5 ms at 1080p on the dev GPU.
6. **S6.6 Bloom.** Threshold + downsample chain + upsample (CoD-style) on the HDR buffer.
   Emissive materials (S6.2) glow — **this is the lava enabler.**
   *Acceptance:* emissive mesh blooms; no flicker while orbiting.
7. **S6.7 Anti-aliasing.** FXAA post pass first (cheap, fits the stack); MSAA resolve path for the
   3D pass as an option (closes the old "reserved spec fields" question); TAA parked until motion
   vectors exist.
   *Acceptance:* grid/edge crawl visibly reduced; screenshot comparison committed.

---

## 6. S7 — Sky, atmosphere & time-of-day

1. **S7.1 Procedural sky v1.** Sun-disk + Preetham/Hosek-style analytic sky driven by a sun
   direction; replaces S3.3's gradient. Sun direction also drives the directional light + IBL
   ambient approximation (re-capture irradiance on big sun moves, amortized).
2. **S7.2 Height fog + aerial perspective.** Exponential height fog with inscatter color from the
   sky model; applied in the tonemap/post chain (depth-based).
3. **S7.3 Time-of-day.** `Environment` scene object: sun elevation/azimuth from time; app-drivable
   (sim time ↔ visual time). Night = stars/moon texture (cheap).
4. **S7.4 (later) Volumetric clouds** — park until a consumer exists; note: raymarched noise
   clouds are S10-adjacent and expensive; revisit after S12 profiling exists.

*Acceptance (stage):* Engine3DDemo under a morning→noon→sunset scrub looks continuously plausible;
fog hides the grid horizon.

---

## 7. S8 — Terrain system *(engine component; the volcano's foundation)*

1. **S8.1 `TerrainComponent` + renderer.** Heightmap-based: source = image (R16) or procedural
   (engine `math/Noise.h` — see doc 03 E14). Chunked quadtree with distance-based LOD and skirt
   stitching (chosen over geo-clipmaps for implementation simplicity; revisit at S12 scale-up).
   CPU-generated normals at load; world size/height scale in the component.
2. **S8.2 Terrain materials.** Splat-map blended PBR layers (4 first, 8 later): grass/rock/
   snow/ash…; **triplanar** projection on steep slopes; per-layer tiling; height-based +
   slope-based auto-splat option (rock above slope threshold, snow above altitude — parameterized,
   *not* hardcoded to any scenario).
3. **S8.3 Terrain queries.** `Terrain::SampleHeight(x, z)` and `SampleNormal(x, z)` on the CPU —
   the generic verb sims use for ground contact (ViperSim landing legs) and demos use for placing
   objects/particles.
4. **S8.4 (later) GPU tessellation displacement** for near-field detail; **holes** (caves/craters
   — the caldera) via per-chunk mask. Editor sculpting brushes belong to S14's editor app.

*Acceptance (stage):* 4×4 km, 1 m-resolution terrain at 60 fps with LOD transitions free of pops
at walking distance; splat layers blend by height/slope; `SampleHeight` matches rendered surface
within 1 cm in tests.

---

## 8. S9 — Water system

Two tiers; Tier 1 is most of the visual payoff for lakes/rivers.

1. **S9.1 `WaterComponent` Tier 1 (lake/river).** Flat plane (or terrain-carved region):
   dual scrolling normal maps + 2–4 **Gerstner waves** for swell; **depth-fade** absorption color
   (soft shorelines, using scene depth); refraction via scene-color grab pass with distorted UVs;
   **planar reflection** (render-to-texture with clip plane) *or* SSR — planar first (simpler,
   robust for one water plane); Fresnel blend; shoreline **foam** from depth delta + noise.
2. **S9.2 Buoyancy/height query.** `Water::SampleHeight(x, z, t)` evaluating the same Gerstner set
   on CPU — generic verb for floating objects (and a Viper water-landing someday).
3. **S9.3 Tier 2 ocean (FFT).** Tessendorf FFT spectrum (JONSWAP) on compute (S4.7):
   displacement + choppiness, whitecaps from the Jacobian, cascaded (2–3) spectra for detail;
   projected-grid or clipmesh LOD. This is the "realistic ocean" checkbox — schedule only after
   Tier 1 ships and S12 profiling exists.
4. **S9.4 Underwater** (fog volume + tinted post + caustics texture) — optional, demo-driven.

*Acceptance (Tier 1):* lake demo — shoreline foam, soft depth edges, sun + sky reflections, boat
(box) bobbing via `SampleHeight`, 2D overlay intact. *(Tier 2):* open-ocean demo at 60 fps.

---

## 9. S10 — GPU particles & volumetrics

1. **S10.1 GPU particle system.** SSBO pool + compute update (S4.7) + indirect draw; emitter
   component: spawn shape (point/sphere/cone/box), rate/burst, over-lifetime curves (size, color,
   velocity), gravity/drag/wind, world/local space; soft-particle depth fade; texture **flipbook**
   animation with frame blending; CPU fallback path for tiny emitters. Sorting: per-emitter
   back-to-front within the transparent queue (S12 ties in).
2. **S10.2 Ribbons/trails.** Camera-facing ribbon emitter (rocket exhaust, tire tracks in snow,
   wingtip vortices for the sim).
3. **S10.3 Froxel volumetric fog + light shafts.** Clustered froxel grid, sun shadow-map sampling
   → god rays; density from height fog + local fog volumes (box/sphere components).
4. **S10.4 Raymarched local volumes.** 3D-noise raymarch inside a bounded volume for **smoke
   plumes** (the volcano column): flipbook-billboard hybrid first, true raymarch second; lit by
   sun + N strongest point lights.
5. **S10.5 Heat-haze distortion.** Screen-space UV distortion post pass masked by "distortion"
   particles/volumes (refraction vector in a small RT).

*Acceptance (stage):* 200k live GPU particles at 60 fps; a lit smoke plume casts plausible
self-shading; heat shimmer above an emissive surface.

---

## 10. S11 — Weather/nature systems + flagship demo apps

Engine grows **generic** systems; each flagship scenario is a **`Projects/` demo app** and the
stage's acceptance test. (Rule 8: the engine never gains a `Volcano` class.)

1. **S11.1 Snow system (engine).**
   - **Snow overlay material feature:** world-up-facing snow blend on any PBR material
     (mask = N·up smoothstep + altitude band), sparkle micro-glint (jittered specular), usable on
     terrain layers (S8.2) and meshes.
   - **Accumulation mask v1:** top-down orthographic depth capture → coverage buffer that fades in
     over exposed surfaces (drivable rate), sampled by the overlay feature.
   - **Deformation trails v1:** RTT height-stamp buffer (objects stamp as they move) sampled as
     displacement/normal perturbation on snow surfaces — decal-quality first; tessellated
     displacement upgrade later.
   - Falling snow = S10 emitter preset (the engine ships the preset as an example asset).
2. **S11.2 Lava/emissive-flow material feature (engine).** Flow-map-scrolled emissive PBR layer
   (temperature gradient LUT → emissive intensity, crust/glow bands), animated by time — generic
   "glowing flowing surface" usable for lava, neon, tron-floors. Blooms via S6.6.
3. **S11.3 `Projects/VolcanoDemo`.** Terrain w/ caldera (S8 + hole mask), lava flows (S11.2 on
   terrain decal meshes), smoke column (S10.4), ash particles + embers (S10.1), heat haze (S10.5),
   point-light glow, night mode (S7), rumble (doc 08 A3 positional audio) — **the "realistic
   volcano" acceptance scene.**
4. **S11.4 `Projects/WinterDemo`.** Snowy terrain + falling snow + accumulation on a cabin/trees
   (glTF), footprint/vehicle trails, grey sky preset — **the "realistic snow" acceptance scene.**
5. **S11.5 `Projects/OceanDemo`** (or fold into WinterDemo lake): S9 showcase + buoyant objects —
   **the "realistic water" acceptance scene.**

---

## 11. S12 — Performance & scale

1. **S12.1 Frustum culling** — AABB per `MeshRendererComponent` (mesh-local bounds × transform),
   camera frustum test before submit; stats counter proves cull rate.
2. **S12.2 Render queue + sort keys** — opaque front-to-back (state-change key: shader→material→
   mesh), transparent back-to-front; replaces immediate-mode submission inside `Renderer3D`.
3. **S12.3 Instanced mesh path** — `DrawMeshInstanced(mesh, material, span<mat4>)` + automatic
   instancing for identical mesh/material pairs in the queue (rocks, trees, debris).
4. **S12.4 LOD groups** — `LODGroupComponent` (N meshes + switch distances, optional cross-fade).
5. **S12.5 GPU profiler** — timer-query verb (`RenderCommand::BeginGpuZone/EndGpuZone`) + a
   profiler HUD panel (per-pass ms: shadow, opaque, water, particles, post) — **build this early
   in the stage; every later item quotes its numbers.**
6. **S12.6 Texture pipeline** — mip generation policy, BCn compression at import (stb_dxt or a
   `package.bat` bake step), sRGB correctness audit.

*Acceptance (stage):* VolcanoDemo + 10k instanced meshes ≥ 60 fps at 1080p on the dev GPU, with
the profiler HUD screenshot committed.

---

## 12. S13 — RHI hardening + Vulkan gate

1. **S13.1 Conformance audit** — grep-verified: zero `gl*`/`GL_*` outside `platform/OpenGL/`;
   missing verbs promoted to `RendererAPI`. CI check added.
2. **S13.2 Frame-lifecycle doc** — one internals doc: resource creation/destruction rules, pass
   ordering, who owns which FBO — the spec a second backend would implement.
3. **S13.3 Decision gate** — evaluate against §0's reopen conditions with S12 profiler data.
   Outcomes: stay GL (default) / native Vulkan backend / adopt an RHI. A Vulkan backend, if
   chosen, lands per-subsystem (clear → 2D → 3D → compute) behind `RendererAPI::SetAPI`.

---

## 13. S14 — Game-engine tier backlog *(parking lot; each item lists its unlock)*

| Item | What | Unlocks when |
| --- | --- | --- |
| Skeletal animation | glTF skins/clips, GPU skinning, blend/state machine | a project needs characters |
| Physics middleware gate | vendored **Jolt** for contact-rich gameplay (the sim's 6DOF stays app-side per doc 03) | a game project needs stacks/ragdolls/queries |
| Scene serialization | save/load scenes (old E8) — prefab-ish text format | editor app or a content-heavy game |
| Editor app (`CosmicEd`) | scene view (S5 nav/gizmos/picking), hierarchy + inspector via `ComponentRegistry` reflection, play/pause using the responsive-rendering+pause design (docs/design/) | S5 done + a real content workflow need |
| Undo/redo command stack | editor-grade command pattern | CosmicEd |
| Scripting | staying C++-DLL-first is the story; optional Lua/AngelScript later | external users demand it |
| Positional audio | doc 08 A3 | any S11 demo wanting ambience |
| Decals | projected PBR decals | VolcanoDemo polish / games |

---

## 14. Order, dependencies, and how this interleaves with the sim

```
S3   ──────────────► driven by ViperSim P4–P5 (any time after S2)
S4.1→S4.7 (ordered) ─► S5.4/S5.5 (need S4.6)   S6 (needs S4.2/S4.5/S4.6/S4.7)
S5.1–S5.3 [filler] ──► any time after S1
S6 ─► S7 ─► S8 ─► S9 ─► S10 ─► S11 ─► S12 ─► S13
                 (S8/S9/S10 internally reorderable; S11 needs all three)
```

- **2D pipeline, shipped apps, and the docked workspace stay untouched at every stage** (contract 6).
- Every item lands as its own PR with its acceptance line demonstrated (screenshot or demo app
  committed under `Projects/`).
- Where a technique has competing implementations, this doc names the chosen one and the trade-off;
  implementers should not silently substitute alternatives.
