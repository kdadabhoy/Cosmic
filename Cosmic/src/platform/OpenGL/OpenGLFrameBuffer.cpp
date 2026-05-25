// Resolve Windows/GLAD macro collision
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#ifdef APIENTRY
    #undef APIENTRY
#endif


#include <glad/glad.h>
#include "platform/OpenGL/OpenGLFrameBuffer.h"
#include "core/Log.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLFrameBuffer Constructor
	 * * Initializes the framebuffer by calling Invalidate(), which performs the
	 * initial GPU resource allocation based on the provided specification.
	 */
	OpenGLFrameBuffer::OpenGLFrameBuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		Invalidate();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLFrameBuffer Destructor
	 * * Cleans up the Framebuffer Object (FBO) and its associated color/depth
	 * texture attachments from GPU memory.
	 */
	OpenGLFrameBuffer::~OpenGLFrameBuffer()
	{
		glDeleteFramebuffers(1, &m_RendererID);
		glDeleteTextures(1, &m_ColorAttachment);
		glDeleteTextures(1, &m_DepthAttachment);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Invalidate
	 * * THE REGENERATION HUB: This function handles the physical allocation of GPU
	 * resources. It is responsible for:
	 * 1. Releasing existing handles if they already exist (Resource Reset).
	 * 2. Generating the FBO and binding it.
	 * 3. Allocating the Color Attachment (RGBA8 texture).
	 * 4. Allocating the Depth/Stencil Attachment (Depth24_Stencil8).
	 * 5. Verifying completeness with the driver.
	 */
	void OpenGLFrameBuffer::Invalidate()
	{
		if (m_RendererID)
		{
			glDeleteFramebuffers(1, &m_RendererID);
			glDeleteTextures(1, &m_ColorAttachment);
			glDeleteTextures(1, &m_DepthAttachment);
		}

		glGenFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		// Color Attachment: Standard RGBA texture used as the "Canvas"
		glGenTextures(1, &m_ColorAttachment);
		glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

		// Depth/Stencil Attachment: Required for Z-testing in off-screen renders
		glGenTextures(1, &m_DepthAttachment);
		glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

		// Validation Check: Ensure the GPU driver is satisfied with the requested attachments
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			CS_CORE_ERROR("Framebuffer is incomplete!");
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Activates this framebuffer for rendering.
	 *
	 * ARCHITECTURAL NOTE: The glViewport call has been intentionally removed.
	 * Framebuffer binding now strictly controls memory targets (which FBO is the
	 * active draw target). Viewport area control (sub-quadrant regions for
	 * picture-in-picture or multi-camera grids) is managed exclusively by the
	 * active RenderPass, which has full awareness of the desired viewport bounds.
	 * This separation allows multiple RenderPass instances to share the same
	 * framebuffer while targeting different pixel regions.
	 */
	void OpenGLFrameBuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Unbind
	 * * Resets the active framebuffer to 0 (the default window/screen buffer).
	 */
	void OpenGLFrameBuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Resize
	 * * Updates the specification with new dimensions and triggers Invalidate()
	 * to recreate the GPU textures at the new resolution.
	 * * Includes a guard against invalid (0,0) dimensions common during
	 * window minimization.
	 */
	void OpenGLFrameBuffer::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			CS_CORE_WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
			return;
		}

		m_Specification.Width = width;
		m_Specification.Height = height;

		Invalidate();
	}

	/////////////////////////////////////////////////////////////////////////////////
}