#include "graphics/FrameBuffer.h"
#include "renderer/Renderer.h"
#include "platform/OpenGL/OpenGLFrameBuffer.h"
#include "core/Log.h"

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
		// Surface the reserved spec fields — silently ignoring them (e.g. Samples = 4
		// yielding single-sampled output) has burned users before. See IMPROVEMENTS §5.4.
		if (spec.Samples > 1)
			CS_CORE_WARN("FramebufferSpecification::Samples is reserved — MSAA is not implemented; rendering single-sampled.");
		if (spec.SwapChainTarget)
			CS_CORE_WARN("FramebufferSpecification::SwapChainTarget is reserved and has no effect.");

		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLFrameBuffer>(spec);
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}