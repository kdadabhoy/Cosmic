#include "graphics/FrameBuffer.h"
#include "renderer/Renderer.h"
#include "platform/OpenGL/OpenGLFrameBuffer.h"

namespace Cosmic
{
	Ref<FrameBuffer> FrameBuffer::Create(const FramebufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLFrameBuffer>(spec);
		}
		return nullptr;
	}
}