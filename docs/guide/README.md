# Cosmic Developer Guide

> **One chapter per topic, task-oriented, written from the source.** These answer *"how do I do X
> in my project?"* — with worked, compiling examples. For *"what exactly does this call do"* see
> the [API Reference](../reference/README.md); for *"how does it work and why is it built that
> way"* see the [System Explainers](../systems/README.md); for orientation and the command
> reference see the root [README](../../README.md).

The root README stays the **overview** — it tours every subsystem and links here for the detail.
This tier is where the detail lives.

## The authoring contract

**Every chapter is written from scratch against the current source.** This is not an extraction of
the old README, and that is a deliberate decision (doc 12 §1, decision 1c, 2026-07-25).

**Why.** The root README's client guide was written for the Phase 1–13 engine and barely touched
since; **sixteen phases landed after it**. Grepping all 4,875 lines returns **zero** occurrences of
`PhysicsWorld`, `CharacterController`, `SceneRenderer`, `Terrain`, `ParticleEmitter`,
`VoxelVolume`, `NavMesh`, `Animator`, `AudioEngine`, `Config::`, `Gamepad`, `FlyCamera`,
`OrbitCamera`, `Light2D`, `Tilemap`, `UiSystem`, `FlowMachine`, `StoryGraph`, `AssetLibrary`,
`ScriptableEntity`, `ScriptHost`, `SceneSerializer`, `Prefab` or `CommandStack`. Auditing text
that stale, line by line, costs more than writing correct text — and produces a worse chapter,
because it inherits the old document's shape instead of the engine's.

### The five rules

1. **The headers are the source of truth. Always.** Read the header, then the `.cpp`, then the
   tests. Comments go stale silently — the telemetry docstrings still say "v3" while the code
   writes v1. A claim you cannot point at in source does not go in the chapter.
2. **The old README is a quarry, not a source.** Mine it for anything still good — a clear
   explanation, a worked example, a hard-won pitfall, a table that's still right. **Verify every
   borrowed line against source before reusing it**, and rewrite freely around what you keep. Never
   copy a paragraph because it exists; copy it because you checked it and it earns its place.
3. **Chapters are derived from the engine, not from the README's table of contents.** If a
   subsystem exists and a project can use it, it gets covered — whether or not the README ever
   mentioned it. The chapter list below is built that way.
4. **Retire the README sections your chapter replaces, in the same work order.** Each chapter names
   the sections it retires; those bodies are replaced by a 2–4 paragraph overview — *newly
   written*, not the old intro — plus a link. **Headings and their numbers stay** (frozen
   numbering, doc 12 note 6). Nothing ends up in two places.
5. **Say what you found.** Every work order reports: what the chapter covers, **what the old
   README got wrong or omitted** (this is signal for the rest of the effort), what you deliberately
   left out and why, and anything you could not verify.

### Verification bar

- **Every example compiles** against the current API: real namespaces (`Cosmic::`), `Ref<T>`
  factories, VFS paths through `FileSystem::Resolve`, no `...` hand-waving. Model them on real
  usage in `Projects/` — the template project, Engine3DDemo, Frontier, ForgePong, ViperSim,
  SF_Telem — not on imagination.
- **Every failure mode is stated.** Cosmic's conventions vary on purpose: `Shader::Create` returns
  `nullptr`, `Texture2D::Create` returns a degraded non-null object, some calls log and continue.
  The guide says which.
- **Configuration-aware.** The engine builds in two configurations (root README §1.6). Any chapter
  covering something absent from the 2D build says so up front and links
  [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).
- **Don't duplicate the reference** (link the entry for the full signature) **or the explainer**
  (link it for internals and rationale). This tier stays on the outside of the API, showing usage.

## Document format (mandatory — every chapter uses this shape)

```markdown
# <Topic> — Guide

**What this covers:** <one sentence>
**Source of truth:** `Cosmic/src/<dir>/…`
**API Reference:** ../reference/<chapter>.md · **How it works:** ../systems/<explainer>.md
**Configuration:** both · or · 3D only (see ../systems/build-2d-3d-split.md)

## Quick start          ← the smallest thing that works, copy-pasteable, in the first screenful
## <Task sections>      ← one per thing a developer wants to DO, in the order they'd hit them
## Common patterns      ← the idioms this codebase actually uses; what to reach for when
## Pitfalls             ← symptom first, so it's greppable ("Nothing draws and there's no error")
## See also             ← reference entries, the systems explainer, related chapters
```

Section titles are **things a developer wants to do** ("Draw a sprite sheet", "Persist settings to
`user://`", "Make a character walk up stairs"), never class names — class-by-class ordering belongs
in the reference.

## The chapters

29 chapters, derived from the engine's actual public surface. **All 29 written (Phase C closed
2026-07-26).** The *Retires* column is the README sections each one replaces; where it says
*nothing*, the topic has **never had client documentation**. Flip the *Status* cell to
`✅ YYYY-MM-DD` in the same work order that writes the chapter (doc 12 execution note 8).

### Foundations

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`getting-started.md`](getting-started.md) | Install, build, create a project, project layout, the minimal skeleton, both build configurations | §1 | D46 | ✅ 2026-07-25 |
| [`project-anatomy.md`](project-anatomy.md) | The plugin-DLL model, `Application` lifecycle, the layer system, `Ref`/`Scope` and the shared-allocator rule, the Safe Zone | §2, §3, §4 | D47 | ✅ 2026-07-25 |
| [`logging-and-diagnostics.md`](logging-and-diagnostics.md) | `Log` core vs client, sinks, `user://logs`, the Console panel, stats counters, the GPU profiler | §19 | D47 | ✅ 2026-07-25 |
| [`events-and-input.md`](events-and-input.md) | Event objects and propagation, `EventDispatcher`, `Input` polling, keyboard/mouse/**gamepad** | §5, §6 | D48 | ✅ 2026-07-25 |
| [`time-and-ticks.md`](time-and-ticks.md) | `Timestep`, the timeline, pause vs `TimeScale(0)`, per-layer local time, fixed vs variable | §7 | D48 | ✅ 2026-07-25 |

### Scene & logic

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`entities-and-components.md`](entities-and-components.md) | The ECS, **the full component catalogue**, hierarchy, Active/Enabled, `System`, queries | §15 | D49 | ✅ 2026-07-25 |
| [`scenes-and-serialization.md`](scenes-and-serialization.md) | `.cscene`, prefabs, `SceneManager` async load, reflection, undo/`CommandStack`, opaque preservation | §23 | D50 | ✅ 2026-07-25 |
| [`scripting.md`](scripting.md) | `ScriptableEntity`, `SystemScript`, `ScriptHost`, module registration, hot reload, and **all eight** proxies — `Physics()`/`Character()`/`Flow()`/`Signals()`/`Telemetry()`/`Nav()`/`Animator()`/`Voxels()` | *nothing* | D50 | ✅ 2026-07-25 |
| [`flow-and-story.md`](flow-and-story.md) | `.cflow` screen flow, `.cstory` dialogue, variables, guards, `EventBus` signals | *nothing* | D53 | ✅ 2026-07-26 |

### 2D

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`rendering-2d.md`](rendering-2d.md) | `Renderer2D` draw API, batching, **every batch limit**, `RenderPass`, `SubTexture2D`, text | §8, §11–§14 | D51 | ✅ 2026-07-26 |
| [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) | 2D authoring: sprite components, sprite animation, tilemaps, 2D lights, the 2D camera rig | *nothing* | D52 | ✅ 2026-07-26 |
| [`game-ui.md`](game-ui.md) | UI entities, canvas, anchors and layout, buttons, world-anchored UI, render-to-texture | *nothing* | D52 | ✅ 2026-07-26 |

### Graphics core

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`materials-and-shaders.md`](materials-and-shaders.md) | `Material`, `.cmat` assets, the shader contract, uniforms, framebuffers, `BindingPoints` | §9, §10, §18 | D51 | ✅ 2026-07-26 |
| [`cameras.md`](cameras.md) | Camera classes, orthographic/orbit/fly/2D controllers, CAD navigation, the nav cube, gizmos, picking | §16 | D53 | ✅ 2026-07-26 |

### 3D

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`rendering-3d.md`](rendering-3d.md) | `Renderer3D` submit/cull/sort/instance/LOD, meshes and models, **material-read-at-flush + `Material::Clone`** | *nothing* | D54 | ✅ 2026-07-26 |
| [`lighting-and-environment.md`](lighting-and-environment.md) | The `SceneRenderer` pass graph, PBR/IBL, sky and time-of-day, shadows, the post chain | *nothing* | D55 | ✅ 2026-07-26 |
| [`world-systems.md`](world-systems.md) | Terrain (incl. the `32·2^k+1` rule), water, GPU particles | *nothing* | D55 | ✅ 2026-07-26 |
| [`voxels.md`](voxels.md) | Voxel volumes, chunks, meshing, editing, generation, collision | *nothing* | D56 | ✅ 2026-07-26 |
| [`animation.md`](animation.md) | Skeletons, clips, `Animator`, crossfade, joint sockets, GPU skinning | *nothing* | D56 | ✅ 2026-07-26 |

### Simulation

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`physics.md`](physics.md) | Rigid bodies, colliders, the character controller, queries, triggers, contact events, swapping the backend | *nothing* | D57 | ✅ 2026-07-26 |
| [`navigation-and-ai.md`](navigation-and-ai.md) | Navmesh bake, `.cnav`, agents and crowds, the `Nav()` proxy | *nothing* | D57 | ✅ 2026-07-26 |
| [`sim-math-toolkit.md`](sim-math-toolkit.md) | Integrators, filters, lookup tables, noise, deterministic RNG, spatial frames | *nothing* | D58 | ✅ 2026-07-26 |

### Platform & tooling

| Chapter | Covers | Retires | WO | Status |
| --- | --- | --- | --- | --- |
| [`assets-and-vfs.md`](assets-and-vfs.md) | `AssetLibrary`, the `engine://`/`project://`/`user://` VFS, model/texture import, TOML config | §17 | D58 | ✅ 2026-07-26 |
| [`audio.md`](audio.md) | One-shots, loops, groups, headless behaviour | *nothing* | D58 | ✅ 2026-07-26 |
| [`serial-and-telemetry.md`](serial-and-telemetry.md) | `SerialPort`/`SerialLink`, COBS framing, channels, recording, replay | §20, §26 | D59 | ✅ 2026-07-26 |
| [`jobs-and-parallelism.md`](jobs-and-parallelism.md) | `JobSystem`, parallel systems, `ParallelFor`, `SystemQuery`, double buffering | §22 | D59 | ✅ 2026-07-26 |
| [`windowing-and-viewport.md`](windowing-and-viewport.md) | The window, borderless chrome, DPI, fullscreen, responsive render/pause, viewport visibility and center docking | §24, §29 | D60 | ✅ 2026-07-26 |
| [`editor-ui-and-theming.md`](editor-ui-and-theming.md) | `ImGuiLayer`, the docking model, `ThemeManager`, fonts and Lucide icons, `Widgets`, `PlotStyle` | §27, §28 | D60 | ✅ 2026-07-26 |
| [`building-and-shipping.md`](building-and-shipping.md) | The two build configurations, every CMake option, the build scripts, packaging, the installer, the exe icon and `VERSIONINFO`, what a shipped folder contains | §40 (Part II), §25 | D61 | ✅ 2026-07-26 |

**README §21 (The Template Project)** and **§25 (Complete API Reference Tables)** are retired
without successors: the template is covered by `getting-started.md`, and the API tables are the
reference tier's job. §21 was retired by **D50**; its heading keeps a pointer to
`getting-started.md` + `project-anatomy.md`, and **§21.5 (Homescreen + Screens) was left live** —
the multi-screen app shape has no chapter on this list. §25 was retired by **D61**, whose
replacement body is a four-row routing table into the reference, guide and systems tiers plus §1.5,
with a note that a skeleton reference chapter means the *guide* chapter is the client-facing source.

**Part I is fully retired as of D61.** Every §1–§29 heading is now an overview plus a chapter link,
except the three that were always meant to stay in the README: **§1.5** (the command reference, with
its own upkeep contract), **§1.6** (the two engine configurations) and **§21.5** (the multi-screen
homescreen shape, which has no chapter). Part II (§30–§43) stays in the README until Phase D moves
its internals to `docs/systems/`; D61 gave it a pass — see the *What D61 changed in Part II* note
below.

**Not covered by any reference chapter yet.** `scene/SceneSerializer.h`, `scene/SceneManager.h`,
`core/CommandStack.h`, `core/UUID.h`, `scene/EventBus.h`, `scene/FlowMachine.h`,
`scene/StoryGraph.h`, `scene/ui/UiComponents.h`, `scene/ui/UiSystem.h`,
`camera/Camera2DController.h`, `renderer/Light2DRenderer.h`, the whole `scripting/` tier and the
whole `reflect/` tier have **no row in the reference manifest** and therefore no reference chapter.
Until D5 closes that gap, `scenes-and-serialization.md`, `scripting.md`, `flow-and-story.md`,
`game-ui.md` and `sprites-and-tilemaps.md` are the client-facing source for those headers and say so
in their header blocks. (The last four were found by D52: the whole in-game-UI tier is unlisted, and
`Camera2DController` is the only camera controller missing from a manifest that names the other
five.) **D55 adds two more**, both already flagged in `reference/README.md`'s own prose as having no
row: `scene/WorldSystemRecipes.h` — the E18 recipe→spec layer every scene-authored terrain, water
body and emitter goes through — and `water/Presets.h`, the only camera-free preset header of the
three (`particles/Presets.h` *is* listed). `world-systems.md` is the client-facing source for both.
**D56 adds seven more, of a third kind** — reachable from `<Cosmic.h>` *transitively* rather than
by a direct include, which is why the manifest (a `Cosmic.h`-include table, D54) never saw them:
`graphics/Skeleton.h` and `graphics/AnimationClip.h` arrive via `scene/Components3D.h`, and
`voxel/VoxelVolume.h` + `voxel/BlockPalette.h` arrive via `scripting/ScriptableEntity.h`. All four
are squarely client surface — `AnimatorComponent` exposes both animation types as public members,
and the `Voxels()` proxy hands out `VoxelVolume`/`VoxelRayHit` directly. The remaining three
(`voxel/VoxelMesher.h`, `voxel/VoxelGenerator.h`, `voxel/VoxelRender.h`) need an explicit include
and are named in shipped sample code. `voxels.md` and `animation.md` are the client-facing source
for all seven — and both are also the *only* documentation of their subsystem anywhere, because
neither has a `docs/systems/` explainer either (no row in the systems index).
**D53 adds a mis-routing, not a gap:** `scene/ScenePicker.h` *is* in the manifest but points
at [`../reference/ecs.md`](../reference/ecs.md), while the guide chapter covering it is
`cameras.md` — worth re-pointing when D5 runs.
**D57 adds four more, and one of them is a fourth kind.** `nav/NavWorld.h`, `nav/NavTypes.h` and
`scene/SceneNav.h` are the third kind again — transitively reachable through
`scripting/ScriptableEntity.h`, exactly like D56's voxel headers, and unambiguously client surface
(`NavWorld`, `NavPath` and `SceneNavRuntime` are all named in the `Nav()` proxy's public signatures).
`navigation-and-ai.md` is the client-facing source for all three, and — like `voxels.md` and
`animation.md` — it is also the *only* documentation of its subsystem anywhere, because navigation
has no `docs/systems/` explainer either (`systems/cameras-navigation.md` is about **camera**
navigation). The fourth kind is **`physics/ScenePhysics.h`: a header with no manifest row that a
**written** reference chapter already covers.** `reference/physics.md`'s scope block names it, its
five siblings all have rows, and `ScenePhysics::BuildColliderDesc` is deliberately public and static
— the manifest simply missed it, because `Cosmic.h` reaches it through `ScriptableEntity.h` rather
than directly. That is a one-line fix rather than a coverage question, and D5 should treat it as
such.
**D58 adds four more, and they are a FIFTH kind — the plainest one yet.**
`utils/FileWatcher.h`, `utils/FileDialog.h`, `utils/ImageIO.h` and `utils/ExeResources.h` are
included by `Cosmic.h` **directly and unfenced** (lines 167–170) and simply have no manifest row.
Not transitive, not fenced, not mis-routed — omitted. Every earlier gap needed an explanation for
why a `Cosmic.h`-include table missed it; these four are exactly what such a table is *for*, which
makes them the strongest argument yet for finishing D5's checker instead of auditing by hand.
[`assets-and-vfs.md`](assets-and-vfs.md) is the client-facing source for the first three; all three
belong under [`../reference/assets-io.md`](../reference/assets-io.md), and `ExeResources.h` is
packaging surface that belongs with D61's `building-and-shipping.md`.
**D58 also surfaces a different flavour of gap.** Every header its three chapters cover *is*
correctly routed in the manifest — but all three target chapters
([`../reference/math.md`](../reference/math.md),
[`../reference/assets-io.md`](../reference/assets-io.md),
[`../reference/audio.md`](../reference/audio.md)) are still **skeletons** (D15/D16), and so are all
three matching `docs/systems/` explainers (D32). Nothing has been written at the other end. So
`assets-and-vfs.md`, `audio.md` and `sim-math-toolkit.md` are, for now, the only documentation of
their subsystems anywhere — the same position `voxels.md`, `animation.md` and `navigation-and-ai.md`
are in, reached from the opposite direction.
**D59 adds NO manifest gap at all — the first work order since D49 that doesn't.** All fifteen
headers its two chapters cover (`serial/*` ×3, `telemetry/*` ×6, `jobs/*` ×6) have correct manifest
rows, plus `scene/SelectableComponent.h` routed to `ecs.md`. What D59 hits is D58's *other* flavour:
[`../reference/serial-telemetry.md`](../reference/serial-telemetry.md) and
[`../reference/jobs.md`](../reference/jobs.md) are skeletons (D17), and so are
[`../systems/serial-telemetry.md`](../systems/serial-telemetry.md) and
[`../systems/jobs-parallelism.md`](../systems/jobs-parallelism.md) (D33). Both chapters are
therefore the only written documentation of their subsystems, and all four skeletons now carry a
*don't re-derive* note plus a pointer at the built diagram.
**D60 adds two, both D56's third kind (transitively reachable), and confirms that the manifest's
footnote already covers this chapter pair's main headers.** `core/Window.h` and
`layers/WorkspaceLayer.h` are **correctly** listed in
[`../reference/README.md`](../reference/README.md)'s *"Not in `Cosmic.h` but client-reachable"*
footnote, so `windowing-and-viewport.md` and `editor-ui-and-theming.md` have real routing targets
(`core.md` and `ui.md`). What is missing is **`layers/ImGuiThemes.h`** — reachable through
`layers/ImGuiLayer.h`, which *is* in the manifest, and the home of `enum class ImGuiTheme` (the
parameter type of the exported `ImGuiLayer::SetTheme` / `Cosmic::SetImGuiTheme` overloads) plus
`GetBuiltInThemes()` and `NameForTheme()` — and **`utils/Branding.h`**, which `Cosmic.h` does not
include at all yet is `COSMIC_API`-exported and called from a project DLL
(`Projects/Starforge/src/StarforgeApp.cpp:2617`, `:2649`); it is the same sub-flavour as D56's
`voxel/VoxelMesher.h` trio — client-reachable only by explicit include. Both belong under
[`../reference/ui.md`](../reference/ui.md) and
[`../reference/assets-io.md`](../reference/assets-io.md) respectively. As with D59, the bigger issue
is the *other* end: [`../reference/ui.md`](../reference/ui.md) (D18),
[`../systems/windowing.md`](../systems/windowing.md) (D26) and
[`../systems/ui-theming.md`](../systems/ui-theming.md) (D34) are all still skeletons, so both D60
chapters are the only written documentation of their subsystems.
**D61 adds no NEW gap — it inherits two already-named ones and confirms the routing D58/D60
proposed.** `utils/ExeResources.h` is D58's fifth kind (included by `Cosmic.h` **directly and
unfenced**, no manifest row) and `utils/Branding.h` is D60's find (`COSMIC_API`-exported, called
from a project DLL, and not included by `Cosmic.h` at all). Both are packaging surface;
[`building-and-shipping.md`](building-and-shipping.md) is now the client-facing source for
`ExeResources` and shares `Branding` with
[`windowing-and-viewport.md`](windowing-and-viewport.md). Both belong under
[`../reference/assets-io.md`](../reference/assets-io.md), which is itself a skeleton (D16) — as is
[`../systems/build-plugin-packaging.md`](../systems/build-plugin-packaging.md) (D34). The one
written explainer this chapter leans on,
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md), is the reason the chapter does
**not** re-derive the exclusion table.

**What D61 changed in Part II** (§30–§43), so a later Phase D session does not re-derive it:
**§30** was rewritten against the current tree (the old map was Phase 1–13 era — no `physics/`,
`scripting/`, `reflect/`, `voxel/`, `nav/`, `terrain/`, `water/`, `particles/`, `ui/`, `math/`,
`assets/`, `audio/` or `telemetry/` at all), gained **DG-2** and gained a *2D partition*
subsection that summarises and links `build-2d-3d-split.md` rather than forking it. **§34** had its
`RendererAPI` sketch replaced with the real interface and its GLAD paragraph corrected (the loader
is `gl=4.5` core — glad 0.1.36, `GLAD_GL_VERSION_4_5` is the ceiling — and the `CS_CORE_ASSERT` it
claimed would terminate is compiled out in every configuration). **§35** gained **DG-6**. **§31**'s
unload ASCII had its step order corrected against `Application.cpp`. **§40** became an overview.
**§42.5** is new: a one-line-per-document directory of `docs/systems/`. **§43** became a pointer to
the roadmap and `FEATURE-MATRIX.md`.

## Configuration coverage

`rendering-3d.md`, `lighting-and-environment.md`, `world-systems.md`, `voxels.md`, `animation.md`
and `navigation-and-ai.md` cover subsystems that **do not exist in the 2D engine build**. Each
states that up front and links [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

**"3D only" is per header, not per chapter** (D54). `rendering-3d.md`'s scope splits: `Renderer3D`,
`InstanceSet` and `Model` are filtered out of the 2D build *and* fenced in `Cosmic.h`, while
`graphics/Mesh.h` and the header-only `math/Frustum.h` are unfenced and compile in a 2D tree — there
is simply nothing there that draws a mesh. Say which half you mean; the reference manifest now marks
the fenced ones ³ᴰ and leaves those two unmarked.

**`lighting-and-environment.md` is the sharpest case of that** (D55). Its two central classes,
`SceneRenderer` and `PostProcessStack`, ship in **both** configurations — a 2D frame runs the same
compositor (`BeginHDR` → sprites via `DrawTransparent` → tonemap/FXAA/bloom/vignette →
`DrawOverlay2D`), which is exactly why the pass contract in
[`../design/frame-lifecycle.md`](../design/frame-lifecycle.md) §5 holds verbatim on both engines.
What fences out is everything the chapter is *about*: `EnvironmentMap`, `ShadowMap`,
`CoverageCapture`, `desc.Lights`, the routed `DrawOpaque` and the whole world-content half of
`SceneRenderDesc`. The chapter states that split rather than calling itself flatly 3D-only.
`world-systems.md` needs no such nuance — all three subsystems are excluded outright.

`physics.md` covers a subsystem that **does** ship in both — only mesh and terrain-heightfield
colliders are 3D-only. Say so; it is a common wrong assumption.

`cameras.md` is the mixed case: every camera and every controller ships in **both**, and so does
`Gizmo`, but `NavigationCube` and `ScenePicker` are filtered out of the 2D build. The chapter has a
dedicated section for the two exclusions, because they fail differently — `ScenePicker`'s include is
fenced in `Cosmic.h` and `NavigationCube`'s is not, so the latter compiles and fails at **link**
time.

`assets-and-vfs.md` splits **inside one class** (D58). `AssetLibrary` itself ships in both — a 2D
game loads textures, shaders and materials — but `GetMesh`, `GetModel`, `GetAnimationClip` and
`GetAnimationClipNames` are fenced in `AssetLibrary.h`, and their backends (`MeshImport.cpp`,
`graphics/Model.*`, `graphics/AnimationClip.*`) are excluded from the 2D build, so there would be
nothing left for them to call. `assets/MeshImport.h` is fenced in `Cosmic.h` outright. Everything
else in the chapter — the whole VFS, `Config` and the `utils/` tier — is unfenced, and so are
`audio.md` and `sim-math-toolkit.md` in full.
