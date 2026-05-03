#pragma once

// Purpose: The single entry-point header for Cosmic Engine applications (Sandbox)
// Do not include Platform-specific (OpenGL), vendor, or internal helper classes here.

// Core
#include "core/Core.h"
#include "core/Application.h"
#include "core/Layer.h"
#include "core/Input.h"
#include "core/Timestep.h"

// Events
#include "events/Event.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"

// Renderer
#include "renderer/Renderer.h"
#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"

// Graphics
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"

// Camera
#include "camera/OrthographicCamera.h"
#include "camera/OrthographicCameraController.h"

// Key Codes
#include "codes/KeyCodes.h"
#include "codes/MouseButtonCodes.h"

// Serial
#include "serial/SerialPort.h"

// TODO: Add an entry point #include "Cosmic/Core/EntryPoint.h"




