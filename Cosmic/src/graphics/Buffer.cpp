#include "graphics/Buffer.h"
#include "graphics/RendererAPI.h"

// Include platform-specific implementations
#include "platform/opengl/OpenGLBuffer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// --- VertexBuffer Factory -----------------------------------------------
	std::shared_ptr<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(vertices, size);
		case RendererAPI::API::DirectX: return nullptr; // Add DirectXVertexBuffer here later
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////

	// --- IndexBuffer Factory ------------------------------------------------
	std::shared_ptr<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(indices, count);
		case RendererAPI::API::DirectX: return nullptr; // Add DirectXIndexBuffer here later
		}

		return nullptr;
	}

}