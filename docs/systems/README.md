# Cosmic System Explainers

> One document per engine subsystem. Each starts with a **plain-English overview a
> non-graphics-programmer can follow**, then descends into the technical implementation.
> These answer *"how does it work and why is it built that way"* — for *"what does this call
> do"* see the [API Reference](../reference/README.md); for *"how do I use it in my project"*
> see the root [Developer Guide](../../README.md).

## The documents

Read [`architecture-overview.md`](architecture-overview.md) first — it's the map; everything
else is a territory.

| Document | System | Status |
| --- | --- | --- |
| [Architecture Overview](architecture-overview.md) | The whole engine: module map, DLL plugin model, one frame end-to-end | SKELETON — D25 |
| [Core Runtime](core-runtime.md) | `Application` lifecycle, frame loop, layer stack, time & timeline system, the Safe Zone | SKELETON — D26 |
| [Windowing & Platform](windowing.md) | Win32 window, borderless chrome, DPI, fullscreen, responsive drag/resize | SKELETON — D26 |
| [Events & Input](events-input.md) | Event objects, propagation, polling, gamepad | SKELETON — D27 |
| [Cameras & CAD Navigation](cameras-navigation.md) | Camera hierarchy, orbit/fly controllers, SolidWorks-style nav, ViewCube, picking, gizmos | SKELETON — D27 |
| [2D Renderer](rendering-2d.md) | Batching, texture slots, SDF circles, instancing, text | SKELETON — D28 |
| [3D Renderer](rendering-3d.md) | Sorted render queue: submit → cull → sort → auto-instance → flush; transparency; LOD | SKELETON — D28 |
| [Frame Pipeline & Post-Processing](rendering-pipeline.md) | SceneRenderer pass graph, HDR, PBR + IBL, shadows, SSAO/bloom/FXAA, sky/fog/time-of-day | SKELETON — D29 |
| [Terrain](terrain.md) | Heightmap composition, quadtree LOD, splat/triplanar materials, CPU height queries | SKELETON — D30 |
| [Water](water.md) | Gerstner waves, planar reflection/refraction, underwater rendering, buoyancy | SKELETON — D30 |
| [Particles](particles.md) | GPU particle pools, compute-shader simulation, billboards/ribbons, presets | SKELETON — D31 |
| [ECS & Scenes](ecs-scene.md) | Entity-component model on entt, components, systems, scene rendering hooks | SKELETON — D31 |
| [Assets & Virtual File System](assets-vfs.md) | Asset cache, glTF import, `engine://`/`project://`/`user://` schemes, shader preprocessing | SKELETON — D32 |
| [Audio](audio.md) | miniaudio backend, one-shots, loops/groups | SKELETON — D32 |
| [Math & Simulation Toolkit](math-sim-toolkit.md) | Spatial frames (NED vs Y-up), integrators, filters, lookup tables, noise, deterministic RNG | SKELETON — D32 |
| [Jobs & Parallelism](jobs-parallelism.md) | Worker pool, parallel ECS systems, double buffering | SKELETON — D33 |
| [Serial & Telemetry](serial-telemetry.md) | Serial ports/links, COBS framing, columnar recording, replay | SKELETON — D33 |
| [UI & Theming](ui-theming.md) | ImGui integration, docking model, theme manager, fonts/icons, widgets | SKELETON — D34 |
| [Build System & Plugin Architecture](build-plugin-packaging.md) | CMake layout, hot-reloadable project DLLs, packaging/installer pipeline | SKELETON — D34 |
| [**The 2D / 3D Build Split**](build-2d-3d-split.md) | `COSMIC_2D_ONLY`: what each configuration excludes, the classification rule for new code, presets + `.bat` scripts + the worktree layout, the recorded build times, the `main` / `engine-2d` carry-over workflow | **✅ WRITTEN — D41** |
| [**Pluggable Physics Backends**](physics-backends.md) | `PhysicsWorld` as a dispatcher over `IPhysicsBackend`; the registry; the contracts a backend must honour; writing your own | **✅ WRITTEN — D42** |

> **Two things to know about this table.** (1) Most rows are still skeletons awaiting their D25–D34
> work order; the two written rows came out of Phase 29 W10 and are complete documents. The Status
> column is the only reliable signal of which is which. (2) **The engine has two build
> configurations since Phase 29.** Any explainer that describes 3D-only machinery should say so and
> link [`build-2d-3d-split.md`](build-2d-3d-split.md) rather than restating the exclusion rules —
> `rendering-2d.md`, `rendering-3d.md` and `ecs-scene.md` already carry that build note.

## Document format (mandatory — every explainer uses this shape)

```markdown
# <System> — How It Works

**One-liner:** <the system in one sentence>
**Source:** `Cosmic/src/<dir>/…` (+ shaders/assets if any)
**API Reference:** ../reference/<chapter>.md · **Guide:** root README §<n>

## 1. Overview — what and why            ← NO jargon; a smart non-programmer can follow
## 2. Mental model                        ← the one analogy/diagram that makes it click
## 3. How it works, step by step          ← plain-English walkthrough of the runtime path
## 4. Technical implementation            ← now the real thing: files, classes, data layout,
                                            algorithms, GPU state; assumes C++ literacy
## 5. Design decisions & trade-offs       ← why THIS way; rejected alternatives; links to
                                            design docs / plan-doc banners
## 6. Limits & future work                ← current tier deviations, parked upgrades
```

Writing bar (this is the part a rushed session gets wrong — hold the line):
- **§1–§3 must survive the "smart friend test":** a reader who has never written a shader
  should still come away understanding what the system does and roughly how. Introduce every
  term before using it ("a *framebuffer* — an off-screen image the GPU draws into").
- **§4 is source-grounded:** name real files, real class names, real binding points. Quote
  key constants (pool sizes, formats, thresholds) *with* the file they come from, so a reader
  can verify.
- **At least one Mermaid diagram per document** (see the diagram inventory in
  [`../plans/12-documentation-plan.md`](../plans/12-documentation-plan.md) §4 — your document's
  diagrams are pre-assigned there).
- **Don't duplicate the reference.** Method-by-method detail belongs in `docs/reference/`;
  here you explain flow and rationale, linking entries where a reader would want the signature.
- Where a design doc already exists (`docs/design/frame-lifecycle.md`,
  `water-rendering-notes.md`, `responsive-rendering-and-pause.md`), the explainer *summarizes
  and links* — it never forks the spec.
