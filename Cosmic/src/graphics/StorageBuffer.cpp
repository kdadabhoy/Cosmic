#include "graphics/StorageBuffer.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLStorageBuffer.h"

namespace Cosmic
{
	Ref<StorageBuffer> StorageBuffer::Create(uint32_t size, uint32_t binding)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:   return nullptr;
		case RendererAPI::API::OpenGL: return std::make_shared<OpenGLStorageBuffer>(size, binding);
		}
		return nullptr;
	}
}
