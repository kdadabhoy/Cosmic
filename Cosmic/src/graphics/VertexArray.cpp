#include "graphics/VertexArray.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLVertexArray.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * VertexArray::Create
	 * * Static factory method to instantiate a platform-specific Vertex Array Object (VAO).
	 * * ROLE IN PIPELINE: The VertexArray is the "State Manager" for geometry. It binds
	 * together Vertex Buffers, their specific Memory Layouts, and an Index Buffer into
	 * a single GPU handle.
	 * * API ABSTRACTION: Following the engine's decoupled architecture, this function
	 * queries RendererAPI::GetAPI() to decide whether to return an OpenGLVertexArray
	 * or an alternative backend. This ensures that high-level renderer code remains
	 * "API-Blind," interacting only with the generic VertexArray interface.
	 */
	Ref<VertexArray> VertexArray::Create()
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexArray>();
		case RendererAPI::API::DirectX: return nullptr;
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}