#pragma once
// Cosmic.h
// Last Modified: 5/19/2026

/** * General Description:
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
#include "renderer/RenderCommand.h"
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Material.h"

// Camera & Control
#include "camera/OrthographicCamera.h"
#include "camera/OrthographicCameraController.h"

// Input Mapping
#include "codes/KeyCodes.h"
#include "codes/MouseButtonCodes.h"

// Specialized Utilities
#include "serial/SerialPort.h"
#include "utils/FileSystem.h"

#include <imgui.h>
#include <implot.h>

namespace Cosmic
{
	// The bucket that carries the host contexts to the DLL boundary safely
	struct COSMIC_API HostContext
	{
		ImGuiContext* ImGuiCtx;
		ImPlotContext* ImPlotCtx;
	};

	// NOTE: ProjectPlugin can be kept for legacy compatibility, but your layout now uses Cosmic::Layer!
	class COSMIC_API ProjectPlugin
	{
	public:
		virtual ~ProjectPlugin() = default;
		virtual void OnAttach() = 0;
		virtual void OnDetach() = 0;
		virtual void OnUpdate(Timestep ts) = 0;
		virtual void OnFixedUpdate(Timestep ts) = 0;
		virtual void OnImGuiRender() = 0;
	};
}

// The native Win32/C communication module interface tunnels
extern "C" {
	// UPDATED: This signature now matches Application.cpp's factory expectations perfectly!
	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
}