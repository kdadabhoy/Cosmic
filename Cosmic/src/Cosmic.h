#pragma once

// Purpose: Have only 1 file that the Sandbox needs to include
	// Don't include Platform-specific (OpenGL), vendor, or helper classes

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
#include "renderer/RenderCommand.h"
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"

// Camera
#include "camera/OrthographicCamera.h"



// TODO: Add an entry point #include "Cosmic/Core/EntryPoint.h"