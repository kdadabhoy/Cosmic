# Cosmic API Reference

> **The complete public API, one entry per command.** If a project can call it, it is
> documented here. Chapters are split by domain; every chapter follows the same entry format
> (below). The root [`README.md`](../../README.md) is the *guide* ("how do I build things");
> this reference is the *lookup* ("what exactly does this call do").
>
> Command-line commands (build scripts, `CosmicApp.exe` flags, CMake options, hotkeys) are
> **not** here — they live in root [README §1.5](../../README.md#15-command-reference--every-command),
> which has its own upkeep contract.

## Chapters

| Chapter | Covers | Status |
| --- | --- | --- |
| [**Core Runtime**](core.md) | `Application`, `Layer`, `LayerStack`, `Window`, `Log`, `Timestep`, `UUID`, `CommandStack`, `PlayerLayer`, `Ref`/`Scope`, plugin exports | **✅ WRITTEN — D6 · 2026-07-26** |
| [**Events & Input**](events-input.md) | `Event` hierarchy, `EventDispatcher`, `Input` polling, key/mouse/gamepad codes | **✅ WRITTEN — D7 · 2026-07-26** |
| [**Graphics Resources**](graphics-resources.md) | `Shader`, `Material`, `MaterialAsset`, `Texture2D`, `TextureCube`, `FrameBuffer`, vertex/index/uniform/storage buffers, `RenderCommand`, [`BindingPoints`](graphics-resources.md#bindingpoints), `Renderer` init | **✅ WRITTEN — D8 · 2026-07-26** |
| [**2D Rendering**](rendering-2d.md) | `Renderer2D` draw API, `RenderPass` multi-camera, `SubTexture2D`, `Font` text, `Light2DRenderer` | **✅ WRITTEN — D9 · 2026-07-26** |
| [3D Rendering](rendering-3d.md) | `Renderer3D` (submit/cull/sort/instancing/transparency/LOD), `Mesh`, `Model`, `InstanceSet` — `Frustum` moved to [math.md](math.md) by D15 | SKELETON — D10 |
| [Frame Pipeline](rendering-pipeline.md) | `SceneRenderer` pass orchestration, `PostProcessStack`, `EnvironmentMap` (IBL/sky), `ShadowMap`, `CoverageCapture` | SKELETON — D11 |
| [World Systems](world-systems.md) | `Terrain`, `Water` + `GerstnerWave`, `ParticleEmitter`/`RibbonEmitter` + `Presets` | SKELETON — D12 |
| [**Entity Component System**](ecs.md) | `Scene`, `Entity`, all 34 components field-by-field (with their reflected names + Inspector ranges), `System`, `ComponentRegistry`, `SelectableComponent` | **✅ WRITTEN — D13 · 2026-07-26** |
| [**Physics**](physics.md) | `PhysicsWorld`, `PhysicsTypes` value types, `PhysicsBody`/`CharacterHandle`, `CharacterController`, `ScenePhysics`, `PhysicsBackendRegistry`, the `Physics()`/`Character()` script proxies | **✅ WRITTEN — D43 · 2026-07-25** |
| [**Cameras & Navigation**](cameras.md) | Camera classes, orthographic/orbit/fly controllers, `NavStyle`/`ViewPreset`, `NavigationCube`, `Gizmo`, `ScenePicker` | **✅ WRITTEN — D14 · 2026-07-26** |
| [**Math & Simulation Toolkit**](math.md) | `Spatial`, `Integrators`, `Filters`, `LookupTable`, `Noise`, `Random`, `Frustum` | **✅ WRITTEN — D15 · 2026-07-26** |
| [Assets, Files & Config](assets-io.md) | `AssetLibrary`, `FileSystem` VFS, `Config` (TOML), `DataExport` | SKELETON — D16 |
| [Audio](audio.md) | `AudioEngine`, `Sound` | SKELETON — D16 |
| [Serial & Telemetry](serial-telemetry.md) | `SerialPort`, `SerialLink`, `Framing`, `TelemetryChannel`, `DataRecorder`/`DataPlayer`, `TelemetryPanel`, entity selection | SKELETON — D17 |
| [Jobs & Parallelism](jobs.md) | `JobSystem`, `ParallelSystem`, `ParallelFor`, `SystemQuery`, `ComponentArray`, `DoubleBuffer` | SKELETON — D17 |
| [**UI & Theming**](ui.md) | `ImGuiLayer`, `HostContext`, `WorkspaceLayer` docking surface, `ThemeManager`, `ImGuiThemes`, `Fonts`, `Overlay`, `Widgets`, `PlotStyle`, Lucide icons | **✅ WRITTEN — D18 · 2026-07-26** |

## Entry format (mandatory — copy this shape)

Every documented command uses this exact structure. Classes get a short intro block first
(what the class is, ownership/lifetime, "declared in" path), then one entry per public method.
Free functions and macros get standalone entries.

~~~markdown
### `ClassName::MethodName`

```cpp
// signature copied VERBATIM from the header (keep defaults, keep const, keep Ref<>)
static void MethodName(const Ref<Thing>& thing, const glm::mat4& transform, int entityID = -1);
```

**What it does** — one to three sentences, present tense, no marketing.

**Why you'd use it** — the concrete situation that calls for this command, and what you'd
reach for instead in the neighboring situations (link the alternative).

**Example**

```cpp
// a minimal, COMPILING snippet — real namespaces, real setup, no "..." hand-waving
```

**Notes & pitfalls** *(omit the section if there are none)*
- Threading/lifetime/ordering constraints, error behavior (what happens on failure — nullptr? degraded object? log?), performance traps.

**See also** — [`Related::Command`](#relatedcommand), [systems explainer](../systems/foo.md)
~~~

Rules:
- **Signatures are copied from headers, never paraphrased.** If the header changes, the doc
  changes (see contract below).
- **Every entry states failure behavior** if the call can fail (Cosmic convention varies:
  `Shader::Create` returns `nullptr`, `Texture2D::Create` returns a degraded non-null object —
  the reference is where this is pinned down per call).
- **Examples must compile against the current API.** Follow root-README conventions
  (`Cosmic::` prefix, `Ref<T>` factories, VFS paths via `FileSystem::Resolve`).
- Group entries by class; order within a class: lifecycle (Create/ctor) → core verbs →
  queries → advanced/rare.
- Anchor style: GitHub auto-anchors — link as `#classnamemethodname` (lowercase, no `::`).

## Coverage manifest — every public header maps to a chapter

This table is the enforcement backbone: **every** header included by `Cosmic/src/Cosmic.h`
must appear here, and every listed symbol must have an entry in its chapter. The checker
script `tests/check_docs_coverage.ps1` (work order D5) diffs `Cosmic.h` against this table.

> **³ᴰ marks a header `Cosmic.h` includes only in the 3D configuration** (inside an
> `#ifndef COSMIC_2D_ONLY` fence). Its symbols do not exist in the 2D engine build, and a chapter
> entry for one should say so. Everything unmarked is present in **both** configurations — including
> the whole `physics/` block: physics is dimension-agnostic and ships on both branches. When D5's
> checker is written it must parse `Cosmic.h` **with the fences**, not as flat text, or it will
> report the ³ᴰ headers as missing from a 2D tree. Background:
> [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).
>
> The other 3D-fenced `Cosmic.h` includes already have manifest rows under their own chapters:
> `renderer/Renderer3D.h`, `renderer/EnvironmentMap.h`, `renderer/ShadowMap.h`,
> `renderer/InstanceSet.h`, `renderer/CoverageCapture.h`, `terrain/Terrain.h`, `water/Water.h`,
> `particles/ParticleSystem.h`, `particles/Presets.h`, `graphics/Model.h` — **D54 added the missing
> ³ᴰ marker to all ten**, which the table listed unmarked, i.e. as present in both configurations.
> Note that `graphics/Mesh.h` and `math/Frustum.h` are correctly unmarked: both are unfenced in
> `Cosmic.h` and compile in a 2D tree, even though nothing there draws a mesh.
>
> **The hand-maintained gap list that used to live here is retired (D61, 2026-07-26).** It named
> `water/Presets.h`, `assets/MeshImport.h`, `scene/WorldSystemRecipes.h`, `physics/ScenePhysics.h`,
> `nav/NavWorld.h`, `nav/NavTypes.h` and `scene/SceneNav.h` as known-missing. **All of them now have
> rows**, along with 35 others: `tests/check_docs_coverage.ps1` found **42** unlisted headers against
> a public surface of **147**, so this table had been covering 71 %. Five of the 42 had never been
> spotted by hand at all — `graphics/MaterialAsset.h`, `layers/PlayerLayer.h`, `water/GerstnerWave.h`,
> `renderer/RenderQueue.h` and `core/Version.h` — and four of those are named in a chapter's own
> scope in doc 12, which is exactly the drift a script catches and a person does not.
>
> **Do not maintain a gap list here again.** Run the checker; it is wired into CI and it prints
> ready-made rows for anything missing. It classifies each header by *how* it is reachable (direct
> include, transitive closure, or an explicit `#include` from `Projects/**` or `tests/**`) and
> derives 3D-only status from the CMake `list(FILTER)` block as well as the `Cosmic.h` fences —
> which is why it correctly flags `voxel/VoxelMesher.h` and its two siblings, all of them
> **unfenced**.
>
> **Rows may point outside this tier.** Roughly twenty headers have no reference chapter yet and are
> routed to the `docs/guide/` chapter that actually documents them — the whole `scripting/` and
> `reflect/` tiers, the `voxel/` and `nav/` blocks, `scene/ui/`, and the flow/story headers. Strict
> mode is a reference-tier contract and is skipped for those targets. **The `scripting/` tier having
> no reference chapter at all is a chapter-sized hole, not a row fix**, and it needs a decision
> before D36 can reach a green strict-mode run.
>
> **³ᴰ⁺ marks the one header that is 3D-only in a *different* way** (found by D53).
> `camera/NavigationCube.h` is included by `Cosmic.h` **unfenced**, so it compiles in a 2D tree —
> but `NavigationCube.cpp` is filtered out of the 2D build (`Cosmic/CMakeLists.txt:198`), so calling
> it fails at **link** time rather than at compile time. Its symbols are 3D-only exactly like a ³ᴰ
> header's; only the diagnostic differs. D5's checker must not treat "inside a fence" as the sole
> test for 3D-only, and fencing the include is the one-line fix.

| Header (under `Cosmic/src/`) | Chapter |
| --- | --- |
| `core/Core.h` | [core.md](core.md) |
| `core/Application.h` | [core.md](core.md) |
| `core/Layer.h` | [core.md](core.md) |
| `core/Timestep.h` | [core.md](core.md) |
| `core/Log.h` | [core.md](core.md) |
| `core/Input.h` | [events-input.md](events-input.md) |
| `events/Event.h` | [events-input.md](events-input.md) |
| `events/ApplicationEvent.h` | [events-input.md](events-input.md) |
| `events/KeyEvent.h` | [events-input.md](events-input.md) |
| `events/MouseEvent.h` | [events-input.md](events-input.md) |
| `codes/KeyCodes.h` | [events-input.md](events-input.md) |
| `codes/MouseButtonCodes.h` | [events-input.md](events-input.md) |
| `codes/GamepadCodes.h` | [events-input.md](events-input.md) |
| `renderer/Renderer.h` | [graphics-resources.md](graphics-resources.md) |
| `renderer/RenderCommand.h` | [graphics-resources.md](graphics-resources.md) |
| `renderer/BindingPoints.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/Buffer.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/VertexArray.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/Shader.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/Texture.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/TextureCube.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/FrameBuffer.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/UniformBuffer.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/StorageBuffer.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/Material.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/MaterialAsset.h` | [graphics-resources.md](graphics-resources.md) |
| `renderer/Renderer2D.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/RenderPass.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/SubTexture2D.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/Font.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/Light2DRenderer.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/Renderer3D.h` ³ᴰ | [rendering-3d.md](rendering-3d.md) |
| `graphics/Mesh.h` | [rendering-3d.md](rendering-3d.md) |
| `graphics/Model.h` ³ᴰ | [rendering-3d.md](rendering-3d.md) |
| `renderer/InstanceSet.h` ³ᴰ | [rendering-3d.md](rendering-3d.md) |
| `renderer/RenderQueue.h` | [rendering-3d.md](rendering-3d.md) |
| `math/Frustum.h` | [math.md](math.md) |
| `renderer/SceneRenderer.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/PostProcessStack.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/EnvironmentMap.h` ³ᴰ | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/ShadowMap.h` ³ᴰ | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/CoverageCapture.h` ³ᴰ | [rendering-pipeline.md](rendering-pipeline.md) |
| `terrain/Terrain.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `water/Water.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `particles/ParticleSystem.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `particles/Presets.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `water/Presets.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `water/GerstnerWave.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `scene/WorldSystemRecipes.h` ³ᴰ | [world-systems.md](world-systems.md) |
| `scene/Scene.h` | [ecs.md](ecs.md) |
| `scene/Entity.h` | [ecs.md](ecs.md) |
| `scene/Components.h` | [ecs.md](ecs.md) |
| `scene/Components3D.h` ³ᴰ | [ecs.md](ecs.md) |
| `scene/System.h` | [ecs.md](ecs.md) |
| `scene/ComponentRegistry.h` | [ecs.md](ecs.md) |
| `scene/ScenePicker.h` ³ᴰ | [cameras.md](cameras.md) |
| `scene/SelectableComponent.h` | [ecs.md](ecs.md) |
| `scene/SceneSerializer.h` | [ecs.md](ecs.md) |
| `scene/SceneManager.h` | [ecs.md](ecs.md) |
| `physics/PhysicsTypes.h` | [physics.md](physics.md) |
| `physics/PhysicsBody.h` | [physics.md](physics.md) |
| `physics/PhysicsBackend.h` | [physics.md](physics.md) |
| `physics/PhysicsWorld.h` | [physics.md](physics.md) |
| `physics/CharacterController.h` | [physics.md](physics.md) |
| `physics/ScenePhysics.h` | [physics.md](physics.md) |
| `camera/Camera.h` | [cameras.md](cameras.md) |
| `camera/OrthographicCamera.h` | [cameras.md](cameras.md) |
| `camera/OrthographicCameraController.h` | [cameras.md](cameras.md) |
| `camera/PerspectiveCamera.h` | [cameras.md](cameras.md) |
| `camera/OrbitCameraController.h` | [cameras.md](cameras.md) |
| `camera/FlyCameraController.h` | [cameras.md](cameras.md) |
| `camera/Camera2DController.h` | [cameras.md](cameras.md) |
| `camera/NavigationCube.h` ³ᴰ⁺ | [cameras.md](cameras.md) |
| `graphics/Gizmo.h` | [cameras.md](cameras.md) |
| `math/Spatial.h` | [math.md](math.md) |
| `math/Integrators.h` | [math.md](math.md) |
| `math/Filters.h` | [math.md](math.md) |
| `math/LookupTable.h` | [math.md](math.md) |
| `math/Noise.h` | [math.md](math.md) |
| `math/Random.h` | [math.md](math.md) |
| `assets/AssetLibrary.h` | [assets-io.md](assets-io.md) |
| `utils/FileSystem.h` | [assets-io.md](assets-io.md) |
| `utils/Config.h` | [assets-io.md](assets-io.md) |
| `utils/DataExport.h` | [assets-io.md](assets-io.md) |
| `assets/MeshImport.h` ³ᴰ | [assets-io.md](assets-io.md) |
| `utils/FileWatcher.h` | [assets-io.md](assets-io.md) |
| `utils/FileDialog.h` | [assets-io.md](assets-io.md) |
| `utils/ImageIO.h` | [assets-io.md](assets-io.md) |
| `utils/ExeResources.h` | [assets-io.md](assets-io.md) |
| `utils/Branding.h` | [assets-io.md](assets-io.md) |
| `audio/AudioEngine.h` | [audio.md](audio.md) |
| `audio/Sound.h` | [audio.md](audio.md) |
| `serial/SerialPort.h` | [serial-telemetry.md](serial-telemetry.md) |
| `serial/SerialLink.h` | [serial-telemetry.md](serial-telemetry.md) |
| `serial/Framing.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/TelemetryChannel.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/EntitySelection.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/DataRecorder.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/DataPlayer.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/TelemetryPanel.h` | [serial-telemetry.md](serial-telemetry.md) |
| `telemetry/EntityPicker.h` | [serial-telemetry.md](serial-telemetry.md) |
| `jobs/JobSystem.h` | [jobs.md](jobs.md) |
| `jobs/ParallelSystem.h` | [jobs.md](jobs.md) |
| `jobs/ParallelFor.h` | [jobs.md](jobs.md) |
| `jobs/SystemQuery.h` | [jobs.md](jobs.md) |
| `jobs/ComponentArray.h` | [jobs.md](jobs.md) |
| `jobs/DoubleBuffer.h` | [jobs.md](jobs.md) |
| `layers/ImGuiLayer.h` | [ui.md](ui.md) |
| `ui/Fonts.h` | [ui.md](ui.md) |
| `ui/Overlay.h` | [ui.md](ui.md) |
| `ui/Theme.h` | [ui.md](ui.md) |
| `ui/ThemeManager.h` | [ui.md](ui.md) |
| `ui/IconsLucide.h` | [ui.md](ui.md) |
| `ui/Widgets.h` | [ui.md](ui.md) |
| `ui/PlotStyle.h` | [ui.md](ui.md) |
| `layers/ImGuiThemes.h` | [ui.md](ui.md) |
| `Cosmic.h` (plugin exports, `HostContext`, `SetImGuiTheme`) | [core.md](core.md) + [ui.md](ui.md) |
| `core/CommandStack.h` | [core.md](core.md) |
| `core/UUID.h` | [core.md](core.md) |
| `core/Version.h` | [core.md](core.md) |
| `layers/PlayerLayer.h` | [core.md](core.md) |
| `graphics/Skeleton.h` ³ᴰ | [../guide/animation.md](../guide/animation.md) |
| `graphics/AnimationClip.h` ³ᴰ | [../guide/animation.md](../guide/animation.md) |
| `nav/NavWorld.h` ³ᴰ | [../guide/navigation-and-ai.md](../guide/navigation-and-ai.md) |
| `nav/NavTypes.h` ³ᴰ | [../guide/navigation-and-ai.md](../guide/navigation-and-ai.md) |
| `scene/SceneNav.h` ³ᴰ | [../guide/navigation-and-ai.md](../guide/navigation-and-ai.md) |
| `voxel/VoxelVolume.h` ³ᴰ | [../guide/voxels.md](../guide/voxels.md) |
| `voxel/BlockPalette.h` ³ᴰ | [../guide/voxels.md](../guide/voxels.md) |
| `voxel/VoxelMesher.h` ³ᴰ⁺ | [../guide/voxels.md](../guide/voxels.md) |
| `voxel/VoxelGenerator.h` ³ᴰ⁺ | [../guide/voxels.md](../guide/voxels.md) |
| `voxel/VoxelRender.h` ³ᴰ⁺ | [../guide/voxels.md](../guide/voxels.md) |
| `scene/EventBus.h` | [../guide/flow-and-story.md](../guide/flow-and-story.md) |
| `scene/FlowMachine.h` | [../guide/flow-and-story.md](../guide/flow-and-story.md) |
| `scene/StoryGraph.h` | [../guide/flow-and-story.md](../guide/flow-and-story.md) |
| `scene/ui/UiComponents.h` | [../guide/game-ui.md](../guide/game-ui.md) |
| `scene/ui/UiSystem.h` | [../guide/game-ui.md](../guide/game-ui.md) |
| `reflect/TypeDescriptor.h` | [../guide/scenes-and-serialization.md](../guide/scenes-and-serialization.md) |
| `reflect/TypeRegistry.h` | [../guide/scenes-and-serialization.md](../guide/scenes-and-serialization.md) |
| `scripting/ScriptableEntity.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scripting/ScriptHost.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scripting/ModuleRegistry.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scripting/ModuleMacros.h` | [../guide/scripting.md](../guide/scripting.md) |

*Not in `Cosmic.h` but client-reachable, documented anyway:* `core/Window.h` (via
`Application::GetWindow()`) → [core.md](core.md); `layers/WorkspaceLayer.h` (via
`Application::GetWorkspaceLayer()`) → [ui.md](ui.md).

## The upkeep contract (living documentation)

**Any PR that adds, removes, or changes a public API symbol updates the matching reference
chapter in the same PR.** "Public" means: reachable from a project DLL through `Cosmic.h` or
through an object `Cosmic.h` hands out (e.g. `Window&`, `WorkspaceLayer*`).

Mechanics:
1. New header in `Cosmic.h` → add a row to the manifest above **and** entries in its chapter.
2. New/changed method on an existing public class → update its entry (signature is verbatim).
3. Removed API → delete the entry, note it in the chapter changelog line at the bottom.
4. `tests/check_docs_coverage.ps1` (D5) runs in CI and fails when `Cosmic.h` includes a header
   with no manifest row, or a manifest row's chapter file lacks the header's class names.

This mirrors the root README §1.5 contract for command-line commands (docs/plans/06 D1),
extended to the C++ API.
