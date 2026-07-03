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
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"
#include "graphics/Texture.h"
#include "graphics/SubTexture2D.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Font.h"

// Camera & Control
#include "camera/OrthographicCamera.h"
#include "camera/OrthographicCameraController.h"
#include "camera/PerspectiveCamera.h"
#include "camera/OrbitCameraController.h"

// Math (spatial conventions: NED world frame, Y-up render frame, quaternions)
#include "math/Spatial.h"

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