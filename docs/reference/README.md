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
| [**Graphics Resources**](graphics-resources.md) | `Shader`, `Material`, `MaterialAsset`, `Texture2D`, `FrameBuffer`, vertex/index buffers, `Mesh`, `RenderCommand`, [`BindingPoints`](graphics-resources.md#bindingpoints), `Renderer` init | **✅ WRITTEN — D8 · 2026-07-26** |
| [**2D Rendering**](rendering-2d.md) | `Renderer2D` draw API, `RenderPass` multi-camera, `SubTexture2D`, `Font` text, `Light2DRenderer` | **✅ WRITTEN — D9 · 2026-07-26** |
| [3D Rendering](../parked-3d/reference/rendering-3d.md) (parked 3D) | `Renderer3D`, `Model`, `InstanceSet` — deleted from `main` by AP-05; `Mesh` is documented in [graphics-resources.md](graphics-resources.md#mesh) | PARKED (was SKELETON — D10) |
| [Frame Pipeline](rendering-pipeline.md) | `SceneRenderer` pass orchestration, `PostProcessStack` (the 3D half — `EnvironmentMap`, `ShadowMap`, `CoverageCapture` — is [parked](../parked-3d/reference/rendering-pipeline-3d.md) (parked 3D)) | SKELETON — D11 |
| [World Systems](../parked-3d/reference/world-systems.md) (parked 3D) | `Terrain`, `Water` + `GerstnerWave`, `ParticleEmitter`/`RibbonEmitter` + `Presets` — deleted from `main` by AP-05 | PARKED (was SKELETON — D12) |
| [**Entity Component System**](ecs.md) | `Scene`, `Entity`, all 34 components field-by-field (with their reflected names + Inspector ranges), `System`, `ComponentRegistry`, `SelectableComponent` | **✅ WRITTEN — D13 · 2026-07-26** |
| [**Physics**](physics.md) | `PhysicsWorld`, `PhysicsTypes` value types, `PhysicsBody`/`CharacterHandle`, `CharacterController`, `ScenePhysics`, `PhysicsBackendRegistry`, the `Physics()`/`Character()` script proxies | **✅ WRITTEN — D43 · 2026-07-25** |
| [**Cameras & Navigation**](cameras.md) | Camera classes, orthographic/orbit/fly controllers, `Camera2DController`, `NavStyle`/`ViewPreset`, `Gizmo` (`NavigationCube` and `ScenePicker` were deleted by AP-05) | **✅ WRITTEN — D14 · 2026-07-26** |
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

> **History (2026-09-20, App Platform AP-D1).** Until AP-05 this table carried ³ᴰ / ³ᴰ⁺ markers for headers that
> existed only in the 3D configuration and a hand-maintained gap list (retired D61). The 3D headers were deleted from
> `main` (D-PURGE) and their rows removed; the checker no longer parses fences or the CMake filter block. Rows may
> point outside this tier: the `scripting/`, `reflect/`, `data/`, `scene/ui/` and flow/story headers are routed to the
> `docs/guide/` chapter that documents them (strict mode is a reference-tier contract and is skipped for those). Do
> not maintain a gap list here; run `tests/check_docs_coverage.ps1`, which prints ready-made rows for anything missing.
> The parked 3D chapters and their old rows are under [`../parked-3d/`](../parked-3d/README.md) (parked 3D).

| Header (under `Cosmic/src/`) | Chapter |
| --- | --- |
| `core/Core.h` | [core.md](core.md) |
| `core/Application.h` | [core.md](core.md) |
| `core/Layer.h` | [core.md](core.md) |
| `core/Timestep.h` | [core.md](core.md) |
| `core/IFrameClock.h` | [core.md](core.md) |
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
| `graphics/FrameBuffer.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/GpuObjectStats.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/Material.h` | [graphics-resources.md](graphics-resources.md) |
| `graphics/MaterialAsset.h` | [graphics-resources.md](graphics-resources.md) |
| `renderer/Renderer2D.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/RenderPass.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/SubTexture2D.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/Font.h` | [rendering-2d.md](rendering-2d.md) |
| `renderer/Light2DRenderer.h` | [rendering-2d.md](rendering-2d.md) |
| `graphics/Mesh.h` | [rendering-3d.md](../parked-3d/reference/rendering-3d.md) (parked 3D) |
| `renderer/SceneRenderer.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `renderer/PostProcessStack.h` | [rendering-pipeline.md](rendering-pipeline.md) |
| `scene/Scene.h` | [ecs.md](ecs.md) |
| `scene/Entity.h` | [ecs.md](ecs.md) |
| `scene/Components.h` | [ecs.md](ecs.md) |
| `scene/System.h` | [ecs.md](ecs.md) |
| `scene/ComponentRegistry.h` | [ecs.md](ecs.md) |
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
| `utils/FileWatcher.h` | [assets-io.md](assets-io.md) |
| `utils/FileDialog.h` | [assets-io.md](assets-io.md) |
| `utils/ImageIO.h` | [assets-io.md](assets-io.md) |
| `utils/ExeResources.h` | [assets-io.md](assets-io.md) |
| `utils/Branding.h` | [assets-io.md](assets-io.md) |
| `utils/AtomicOutput.h` | [assets-io.md](assets-io.md) |
| `audio/AudioEngine.h` | [audio.md](audio.md) |
| `audio/Sound.h` | [audio.md](audio.md) |
| `serial/SerialPort.h` | [serial-telemetry.md](serial-telemetry.md) |
| `serial/SerialLink.h` | [serial-telemetry.md](serial-telemetry.md) |
| `serial/Framing.h` | [serial-telemetry.md](serial-telemetry.md) |
| `serial/ISerialTransport.h` | [serial-telemetry.md](serial-telemetry.md) |
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
| `data/DataBus.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scripting/AppService.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scripting/ServiceHost.h` | [../guide/scripting.md](../guide/scripting.md) |
| `scene/FlowKeyBridge.h` | [../guide/flow-and-story.md](../guide/flow-and-story.md) |

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
