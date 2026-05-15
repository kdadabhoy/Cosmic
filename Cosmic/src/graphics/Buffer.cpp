#include "graphics/Buffer.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLBuffer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * VertexBuffer::Create (Dynamic)
	 * * Instantiates an empty VertexBuffer of a specified size.
	 * * API ABSTRACTION: This factory method queries the current RendererAPI and
	 * returns the appropriate platform implementation (e.g., OpenGLVertexBuffer).
	 * This allows the Batch Renderer to request a generic VertexBuffer without
	 * knowing which graphics driver is currently active.
	 */
	std::shared_ptr<VertexBuffer> VertexBuffer::Create(uint32_t size)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(size);
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * VertexBuffer::Create (Static)
	 * * Instantiates a VertexBuffer and immediately populates it with vertex data.
	 * * Use this overload for geometry that does not change frequently (e.g., static
	 * world geometry). Like the dynamic version, this is API-agnostic and
	 * automatically selects the correct implementation based on the engine's
	 * initialization state.
	 */
	std::shared_ptr<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(vertices, size);
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * IndexBuffer::Create
	 * * Instantiates an IndexBuffer for topology management.
	 * * By abstracting the creation of Index Buffers, we ensure that the order
	 * and management of triangles are handled correctly regardless of whether
	 * the engine is running on OpenGL, DirectX, or Vulkan.
	 */
	std::shared_ptr<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(indices, count);
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}