#include "graphics/UniformBuffer.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLUniformBuffer.h"

namespace Cosmic
{
	Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:   return nullptr;
		case RendererAPI::API::OpenGL: return std::make_shared<OpenGLUniformBuffer>(size, binding);
		}
		return nullptr;
	}
}
