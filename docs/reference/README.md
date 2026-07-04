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
| [Core Runtime](core.md) | `Application`, `Layer`, `Log`, `Timestep`, `Window` client surface, `Ref`/`Scope`, plugin exports | SKELETON — D6 |
| [Events & Input](events-input.md) | `Event` hierarchy, `EventDispatcher`, `Input` polling, key/mouse/gamepad codes | SKELETON — D7 |
| [Graphics Resources](graphics-resources.md) | `Shader`, `Material`, `Texture2D`, `TextureCube`, `FrameBuffer`, vertex/index/uniform/storage buffers, `RenderCommand`, `BindingPoints`, `Renderer` init | SKELETON — D8 |
| [2D Rendering](rendering-2d.md) | `Renderer2D` draw API, `RenderPass` multi-camera, `SubTexture2D`, `Font` text | SKELETON — D9 |
| [3D Rendering](rendering-3d.md) | `Renderer3D` (submit/cull/sort/instancing/transparency/LOD), `Mesh`, `Model`, `InstanceSet`, `Frustum` | SKELETON — D10 |
| [Frame Pipeline](rendering-pipeline.md) | `SceneRenderer` pass orchestration, `PostProcessStack`, `EnvironmentMap` (IBL/sky), `ShadowMap`, `CoverageCapture` | SKELETON — D11 |
| [World Systems](world-systems.md) | `Terrain`, `Water` + `GerstnerWave`, `ParticleEmitter`/`RibbonEmitter` + `Presets` | SKELETON — D12 |
| [Entity Component System](ecs.md) | `Scene`, `Entity`, every component, `System`, `ComponentRegistry`, `ScenePicker` | SKELETON — D13 |
| [Cameras & Navigation](cameras.md) | Camera classes, orthographic/orbit/fly controllers, `NavStyle`/`ViewPreset`, `NavigationCube`, `Gizmo` | SKELETON — D14 |
| [Math & Simulation Toolkit](math.md) | `Spatial`, `Integrators`, `Filters`, `LookupTable`, `Noise`, `Random` | SKELETON — D15 |
| [Assets, Files & Config](assets-io.md) | `AssetLibrary`, `FileSystem` VFS, `Config` (TOML), `DataExport` | SKELETON — D16 |
| [Audio](audio.md) | `AudioEngine`, `Sound` | SKELETON — D16 |
| [Serial & Telemetry](serial-telemetry.md) | `SerialPort`, `SerialLink`, `Framing`, `TelemetryChannel`, `DataRecorder`/`DataPlayer`, `TelemetryPanel`, entity selection | SKELETON — D17 |
| [Jobs & Parallelism](jobs.md) | `JobSystem`, `ParallelSystem`, `ParallelFor`, `SystemQuery`, `ComponentArray`, `DoubleBuffer` | SKELETON — D17 |
| [UI & Theming](ui.md) | `ImGuiLayer`, `WorkspaceLayer` docking surface, `ThemeManager`, `Fonts`, `Overlay`, `Widgets`, `PlotStyle`, Lucide icons | SKELETON — D18 |

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
| `renderer/Renderer2D.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/RenderPass.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/SubTexture2D.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/Font.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/Renderer3D.h` | [rendering-3d.md](rendering-3d.md) |
| `graphics/Mesh.h` | [rendering-3d.md](rendering-3d.md) |
| `graphics/Model.h` | [rendering-3d.md](rendering-3d.md) |
| `renderer/InstanceSet.h` | [rendering-3d.md](rendering-3d.md) |
| `math/Frustum.h` | [rendering-3d.md](rendering-3d.md) |
| `renderer/SceneRenderer.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/PostProcessStack.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/EnvironmentMap.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/ShadowMap.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/CoverageCapture.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `terrain/Terrain.h` | [world-systems.md](world-systems.md) |
| `water/Water.h` | [world-systems.md](world-systems.md) |
| `particles/ParticleSystem.h` | [world-systems.md](world-systems.md) |
| `particles/Presets.h` | [world-systems.md](world-systems.md) |
| `scene/Scene.h` | [ecs.md](ecs.md) |
| `scene/Entity.h` | [ecs.md](ecs.md) |
| `scene/Components.h` | [ecs.md](ecs.md) |
| `scene/System.h` | [ecs.md](ecs.md) |
| `scene/ComponentRegistry.h` | [ecs.md](ecs.md) |
| `scene/ScenePicker.h` | [ecs.md](ecs.md) |
| `scene/SelectableComponent.h` | [ecs.md](ecs.md) |
| `camera/Camera.h` | [cameras.md](cameras.md) |
| `camera/OrthographicCamera.h` | [cameras.md](cameras.md) |
| `camera/OrthographicCameraController.h` | [cameras.md](cameras.md) |
| `camera/PerspectiveCamera.h` | [cameras.md](cameras.md) |
| `camera/OrbitCameraController.h` | [cameras.md](cameras.md) |
| `camera/FlyCameraController.h` | [cameras.md](cameras.md) |
| `camera/NavigationCube.h` | [cameras.md](cameras.md) |
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
| `Cosmic.h` (plugin exports, `HostContext`, `SetImGuiTheme`) | [core.md](core.md) + [ui.md](ui.md) |

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
