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

// Entity Component System Submodule Architecture
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

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
    /**
     * @brief The data bucket that carries the host contexts across the DLL boundary safely.
     */
    struct COSMIC_API HostContext
    {
        ImGuiContext* ImGuiCtx;
        ImPlotContext* ImPlotCtx;
    };
}

// The native Win32/C communication module interface tunnels
extern "C" {
    // This signature matches Application.cpp's dynamic runtime factory expectations perfectly.
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
}