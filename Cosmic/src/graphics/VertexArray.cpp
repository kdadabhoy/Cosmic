#include "graphics/VertexArray.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLVertexArray.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

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