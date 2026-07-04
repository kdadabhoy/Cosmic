#pragma once
// Cosmic.h
// Last Modified: 5/20/2026

/**
 * General Description:
 * Cosmic.h is the single entry-point header for all applications built with the
 * Cosmic Engine. It aggregates the entire engine's public API into one file,
 * streamlining the development process for Sandbox and Game projects.
 * * ARCHITECTURAL RULE:
 * This header must NOT include internal engine helpers, vendor-specific code
 * (like glad.h), or platform-specific implementations (like OpenGLShader.h).
 * It serves strictly as an abstraction layer for the end-user.
 */

 // Core Subsystems (Core.h must be first so COSMIC_API is defined!)
#include "core/Core.h"
#include "core/Application.h"
#include "core/Layer.h"
#include "core/Input.h"
#include "core/Timestep.h"
#include "core/Log.h"
#include "core/UUID.h"          // E2 — 64-bit stable entity identity
#include "core/CommandStack.h"  // E7 — generic undo/redo command stack

// Event System
#include "events/Event.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"

// Rendering & Graphics
#include "renderer/Renderer.h"
#include "renderer/Renderer2D.h"
#include "renderer/Renderer3D.h"
#include "renderer/RenderCommand.h"
#include "renderer/RenderPass.h"
#include "renderer/BindingPoints.h"
#include "renderer/PostProcessStack.h"   // S6.1 — HDR pipeline (float target + tonemap)
#include "renderer/EnvironmentMap.h"     // S6.3 — image-based lighting + skybox
#include "renderer/ShadowMap.h"          // S6.4 — directional sun shadows
#include "renderer/SceneRenderer.h"      // F2  — engine-owned multi-pass frame orchestration
#include "renderer/InstanceSet.h"        // F5  — per-instance transform pool (instanced draw)
#include "renderer/CoverageCapture.h"    // F8  — top-down snow/coverage accumulation mask
#include "terrain/Terrain.h"             // S8  — heightmap terrain (quadtree LOD + queries)
#include "water/Water.h"                 // S9  — Gerstner water surface + buoyancy queries
#include "water/Presets.h"               // E18 — ocean/lake/storm water presets
#include "particles/ParticleSystem.h"    // S10 — GPU particles + ribbon trails
#include "particles/Presets.h"           // F8/F9 — atmospheric particle emitter presets
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"
#include "graphics/Texture.h"
#include "graphics/TextureCube.h"     // S6.3 — cubemap resource (IBL)
#include "graphics/SubTexture2D.h"
#include "graphics/FrameBuffer.h"
#include "graphics/UniformBuffer.h"
#include "graphics/StorageBuffer.h"
#include "graphics/Material.h"
#include "graphics/MaterialAsset.h"   // E17 — reflected .cmat struct
#include "graphics/Mesh.h"
#include "graphics/Model.h"
#include "graphics/Font.h"
#include "graphics/Gizmo.h"           // S5.5 — transform gizmos (ImGuizmo)

// Assets
#include "assets/AssetLibrary.h"
#include "assets/MeshImport.h"        // E16 — model import (OBJ + gated assimp) + .cmeta

// Camera & Control
#include "camera/Camera.h"
#include "camera/OrthographicCamera.h"
#include "camera/OrthographicCameraController.h"
#include "camera/PerspectiveCamera.h"
#include "camera/OrbitCameraController.h"   // + NavStyle / ViewPreset (S5.1 / S5.2)
#include "camera/FlyCameraController.h"      // F1 — WASD + mouse-look exploration camera
#include "camera/NavigationCube.h"          // S5.3 — orientation cube widget

// Math (spatial conventions: NED world frame, Y-up render frame, quaternions)
#include "math/Spatial.h"
#include "math/Frustum.h"        // F5 — view-frustum extraction + culling tests

// Simulation toolkit (E-series: docs/plans/03-simulation-engine-plan.md)
#include "math/Integrators.h"    // E11 — RK4, semi-implicit Euler, FixedSubstepper
#include "math/Filters.h"        // E12 — LPF, derivative, rate limit, biquad, washout
#include "math/LookupTable.h"    // E13 — 1D/2D interp tables (aero polars, thrust maps)
#include "math/Noise.h"          // E14 — seeded value/Perlin/fBm noise
#include "math/Random.h"         // E15 — deterministic PCG32 RNG

// Entity Component System Submodule Architecture
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/System.h"
#include "scene/ComponentRegistry.h"
#include "scene/ScenePicker.h"        // S5.4 — entity-ID 3D picking

// Reflection (E1) — runtime type/field descriptors that drive the editor's
// inspector, scene serializer, undo stack, and telemetry panel from one
// registration per component.
#include "reflect/TypeDescriptor.h"
#include "reflect/TypeRegistry.h"
#include "scene/SceneSerializer.h"     // E2 — JSON scene/prefab/material (de)serialization
#include "scene/SceneManager.h"        // E5 — async scene load + fade transitions
#include "scene/WorldSystemRecipes.h"  // E18 — terrain/water/particle recipe -> spec mapping

// Scripting (E11) — native C++ scripts compiled into a hot-reloadable project
// DLL. ScriptableEntity is the base class; ModuleMacros is the registration DSL
// (CS_MODULE_BEGIN/CS_SCRIPT/CS_FIELD/...); the ScriptHost drives the lifecycle.
#include "scripting/ScriptableEntity.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleMacros.h"

// Input Mapping
#include "codes/KeyCodes.h"
#include "codes/MouseButtonCodes.h"
#include "codes/GamepadCodes.h"

// Audio (A-series: docs/plans/08-audio-plan.md — miniaudio-backed)
#include "audio/AudioEngine.h"
#include "audio/Sound.h"

// Specialized Utilities
#include "serial/SerialPort.h"
#include "serial/SerialLink.h"
#include "serial/Framing.h"
#include "utils/FileSystem.h"
#include "utils/DataExport.h"
#include "utils/Config.h"        // E10 — TOML config facade
#include "utils/FileWatcher.h"   // E10 — directory change notifications

// Telemetry, Recording, and Entity Selection
#include "telemetry/TelemetryChannel.h"
#include "telemetry/EntitySelection.h"
#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include "telemetry/TelemetryPanel.h"
#include "telemetry/EntityPicker.h"
#include "scene/SelectableComponent.h"

// Layers
#include "layers/ImGuiLayer.h"
#include "layers/PlayerLayer.h"       // E13 — standalone scene player (ship path)

// Job System / Multithreading
#include "jobs/JobSystem.h"
#include "jobs/ParallelSystem.h"
#include "jobs/SystemQuery.h"
#include "jobs/ComponentArray.h"
#include "jobs/ParallelFor.h"
#include "jobs/DoubleBuffer.h"


// ImGui
#include <imgui.h>
#include <implot.h>

// User Interface helpers (font registry + overlay/text drawing).
// Included after ImGui so the header-only overlay helpers see ImGui types.
#include "ui/Fonts.h"
#include "ui/Overlay.h"
#include "ui/Theme.h"
#include "ui/ThemeManager.h"
#include "ui/IconsLucide.h"
#include "ui/Widgets.h"
#include "ui/PlotStyle.h"

namespace Cosmic
{
    /**
     * @brief The data bucket that carries the host contexts across the DLL boundary safely.
     */
    struct COSMIC_API HostContext
    {
        ImGuiContext* ImGuiCtx;
        ImPlotContext* ImPlotCtx;
    };

	// Forward the helper inline configuration down to the engine's compiled Layer implementation
	inline void SetImGuiTheme(Cosmic::ImGuiTheme theme)
	{
		Cosmic::ImGuiLayer::SetTheme(theme);
	}

	// Name-based theme selection — works with built-in, client-, and
	// editor-registered themes. Enumerate available names via
	// Cosmic::ThemeManager::All().
	inline void SetImGuiTheme(const std::string& name)
	{
		Cosmic::ImGuiLayer::SetTheme(name);
	}
}

// The native Win32/C communication module interface tunnels
extern "C" {
    // This signature matches Application.cpp's dynamic runtime factory expectations perfectly.
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
}