#pragma once

// OpenGLFrameBuffer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * OpenGLFrameBuffer is the concrete implementation of the FrameBuffer interface
 * for the OpenGL graphics backend. It manages "off-screen" rendering targets,
 * allowing the engine to render scenes to textures rather than directly to the
 * primary window backbuffer.
 * 
 * This class is a fundamental component for Editor viewports, post-processing
 * pipelines, and shadow mapping. It handles the generation of Framebuffer
 * Objects (FBOs), manages color and depth/stencil attachments, and provides
 * robust reconstruction logic for handling window or viewport resizing.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLFrameBuffer(const FramebufferSpecification& spec)
 * Pre:  The specification contains non-zero width and height.
 * Post: An FBO is generated and initialized with the requested attachments.
 * 
 * 2. virtual ~OpenGLFrameBuffer()
 * Pre:  The OpenGLFrameBuffer instance exists.
 * Post: All associated GPU resources (FBO and Texture attachments) are deleted.
 * 
 * 3. void Invalidate()
 * Pre:  None.
 * Post: Deletes any existing resources and re-allocates the entire framebuffer
 * based on the current specification.
 * 
 * 4. void Bind()
 * Pre:  The framebuffer is complete and valid.
 * Post: Sets the current OpenGL draw target to this FBO and updates the viewport.
 * 
 * 5. void Unbind()
 * Pre:  None.
 * Post: Resets the OpenGL draw target to the default screen buffer (ID 0).
 * 
 * 6. void Resize(uint32_t width, uint32_t height)
 * Pre:  Width and height are greater than zero.
 * Post: Updates the internal specification and triggers Invalidate() to
 * re-allocate textures at the new resolution.
 */

#include "graphics/FrameBuffer.h"
#include <vector>

namespace Cosmic
{
	class OpenGLFrameBuffer : public FrameBuffer
	{
	public:
		////////////////////////////////
		// Life Cycle & Initialization
		///////////////////////////////

		OpenGLFrameBuffer(const FramebufferSpecification& spec);
		virtual ~OpenGLFrameBuffer();

		void									Invalidate();


		////////////////////////////////
		// Pipeline State
		///////////////////////////////

		virtual void							Bind() override;
		virtual void							Unbind() override;


		////////////////////////////////
		// Dynamic Transformation
		///////////////////////////////

		virtual void							Resize(uint32_t width, uint32_t height) override;


		////////////////////////////////
		// Resource Accessors
		///////////////////////////////

		virtual uint32_t							GetColorAttachmentRendererID(uint32_t index = 0) const override;
		virtual uint32_t							GetDepthAttachmentRendererID() const override		{ return m_DepthAttachment; }
		virtual uint32_t							GetWidth() const override							{ return m_Specification.Width; }
		virtual uint32_t							GetHeight() const override							{ return m_Specification.Height; }
		virtual const FramebufferSpecification&		GetSpecification() const override					{ return m_Specification; }

		virtual int									ReadPixel(uint32_t attachmentIndex, int x, int y) override;
		virtual void								ClearAttachment(uint32_t attachmentIndex, int value) override;


	private:
		////////////////////////////////
		// GPU Resource Handles
		///////////////////////////////

		uint32_t					m_RendererID = 0;

		// Sorted attachment specs (parsed from the FramebufferSpecification), and
		// the live GPU texture handles that mirror them.
		std::vector<FramebufferTextureSpecification>	m_ColorAttachmentSpecs;
		FramebufferTextureSpecification					m_DepthAttachmentSpec; // None ⇒ no depth
		std::vector<uint32_t>							m_ColorAttachments;
		uint32_t										m_DepthAttachment = 0;

		////////////////////////////////
		// Configuration State
		///////////////////////////////

		FramebufferSpecification	m_Specification;
	};
}