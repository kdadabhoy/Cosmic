#include "graphics/FrameBuffer.h"
#include "renderer/Renderer.h"
#include "platform/OpenGL/OpenGLFrameBuffer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * FrameBuffer::Create
	 * * Instantiates a platform-specific Framebuffer Object (FBO) based on the
	 * provided specification.
	 * * ROLE IN PIPELINE: This factory method is the entry point for off-screen
	 * rendering. It abstracts the creation of complex GPU resources (including
	 * color and depth attachments) so that the WorkspaceLayer can capture the
	 * engine's output without direct dependency on the underlying Graphics API.
	 * * API ABSTRACTION: By querying Renderer::GetAPI(), the engine determines
	 * whether to instantiate an OpenGLFrameBuffer or another supported backend.
	 * This ensures the high-level Rendering pipeline remains hardware-agnostic.
	 */
	Ref<FrameBuffer> FrameBuffer::Create(const FramebufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLFrameBuffer>(spec);
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}