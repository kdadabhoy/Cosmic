#pragma once

// Cosmic.h
// Last Modified 5/14/2026

/**
 * General Description:
 * Cosmic.h is the single entry-point header for all applications built with the 
 * Cosmic Engine. It aggregates the entire engine's public API into one file, 
 * streamlining the development process for Sandbox and Game projects.
 * 
 * ARCHITECTURAL RULE:
 * This header must NOT include internal engine helpers, vendor-specific code 
 * (like glad.h), or platform-specific implementations (like OpenGLShader.h). 
 * It serves strictly as an abstraction layer for the end-user.
 * 
 * Included Subsystems:
 * - Core: Application lifecycle and Layer management.
 * - Events: Full input and window event system.
 * - Renderer: High-level 2D batching and Render Commands.
 * - Graphics: Abstractions for Shaders, Textures, and Framebuffers.
 * - Utilities: FileSystem helpers and Serial Port communication.
 */


// Core Subsystems
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