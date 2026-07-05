// windows.h defines APIENTRY as __stdcall; undef it so glad can redefine it cleanly.
#ifdef _WIN32
    #include <windows.h>
#endif

#ifdef APIENTRY
    #undef APIENTRY
#endif


#include <glad/glad.h>
#include "platform/OpenGL/OpenGLFrameBuffer.h"
#include "platform/OpenGL/OpenGLContext.h"
#include "core/Log.h"

#include <array>
#include <cstring>
#include <vector>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// Format helpers (the ONLY place GL enums for attachment formats live)
	/////////////////////////////////////////////////////////////////////////////////

	namespace
	{
		bool IsDepthFormat(FramebufferTextureFormat format)
		{
			return format == FramebufferTextureFormat::DEPTH24STENCIL8;
		}

		// Allocate a color texture for one attachment spec and attach it at
		// GL_COLOR_ATTACHMENT0 + index. RED_INTEGER MUST use GL_NEAREST or the FBO
		// is incomplete.
		void AttachColorTexture(uint32_t id, FramebufferTextureFormat format,
		                        uint32_t width, uint32_t height, uint32_t index)
		{
			GLenum internalFormat = GL_RGBA8;
			GLenum dataFormat     = GL_RGBA;
			GLenum dataType       = GL_UNSIGNED_BYTE;
			GLint  filter         = GL_LINEAR;

			switch (format)
			{
			case FramebufferTextureFormat::RGBA8:
				internalFormat = GL_RGBA8;  dataFormat = GL_RGBA;         dataType = GL_UNSIGNED_BYTE; filter = GL_LINEAR;  break;
			case FramebufferTextureFormat::RGBA16F:
				internalFormat = GL_RGBA16F; dataFormat = GL_RGBA;        dataType = GL_FLOAT;         filter = GL_LINEAR;  break;
			case FramebufferTextureFormat::RED_INTEGER:
				internalFormat = GL_R32I;   dataFormat = GL_RED_INTEGER;  dataType = GL_INT;           filter = GL_NEAREST; break;
			default:
				CS_CORE_ERROR("OpenGLFrameBuffer: unsupported color attachment format.");
				break;
			}

			glBindTexture(GL_TEXTURE_2D, id);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, dataType, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, id, 0);
		}

		void AttachDepthTexture(uint32_t id, uint32_t width, uint32_t height)
		{
			glBindTexture(GL_TEXTURE_2D, id);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0,
			             GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			// Without explicit filters the texture defaults to a mipmap min filter
			// and is mip-incomplete — sampling it (GetDepthAttachmentRendererID →
			// ImGui::Image / debug views) would read black. NEAREST is the standard
			// depth sampling mode.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, id, 0);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	OpenGLFrameBuffer::OpenGLFrameBuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		// Parse the attachment list into color specs + an optional depth spec.
		// EMPTY ⇒ the historical default {RGBA8, DEPTH24STENCIL8}.
		auto attachments = spec.Attachments.Attachments;
		if (attachments.empty())
			attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::DEPTH24STENCIL8 };

		for (const auto& a : attachments)
		{
			if (IsDepthFormat(a.TextureFormat))
				m_DepthAttachmentSpec = a;
			else
				m_ColorAttachmentSpecs.push_back(a);
		}

		Invalidate();
	}

	/////////////////////////////////////////////////////////////////////////////////

	OpenGLFrameBuffer::~OpenGLFrameBuffer()
	{
		// Skip GL deletes if the context is already gone (abort/teardown order).
		if (!OpenGLContext::HasCurrentContext())
			return;

		glDeleteFramebuffers(1, &m_RendererID);
		if (!m_ColorAttachments.empty())
			glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
		glDeleteTextures(1, &m_DepthAttachment);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLFrameBuffer::Invalidate()
	{
		CS_CORE_ASSERT(m_Specification.Width > 0 && m_Specification.Height > 0,
			"FrameBuffer::Invalidate() called with zero dimensions.");

		if (m_RendererID)
		{
			glDeleteFramebuffers(1, &m_RendererID);
			if (!m_ColorAttachments.empty())
				glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
			glDeleteTextures(1, &m_DepthAttachment);

			m_ColorAttachments.clear();
			m_DepthAttachment = 0;
		}

		glGenFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

		// --- Color attachments ---
		if (!m_ColorAttachmentSpecs.empty())
		{
			m_ColorAttachments.resize(m_ColorAttachmentSpecs.size());
			glGenTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
			for (size_t i = 0; i < m_ColorAttachments.size(); ++i)
				AttachColorTexture(m_ColorAttachments[i], m_ColorAttachmentSpecs[i].TextureFormat,
				                   m_Specification.Width, m_Specification.Height, (uint32_t)i);
		}

		// --- Depth attachment ---
		if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None)
		{
			glGenTextures(1, &m_DepthAttachment);
			AttachDepthTexture(m_DepthAttachment, m_Specification.Width, m_Specification.Height);
		}

		// --- Draw-buffer setup ---
		if (m_ColorAttachments.size() > 1)
		{
			CS_CORE_ASSERT(m_ColorAttachments.size() <= 8, "OpenGLFrameBuffer: max 8 color attachments.");
			std::array<GLenum, 8> buffers{};
			for (size_t i = 0; i < m_ColorAttachments.size(); ++i)
				buffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
			glDrawBuffers((GLsizei)m_ColorAttachments.size(), buffers.data());
		}
		else if (m_ColorAttachments.empty())
		{
			// Depth-only pass (free prep for S6.4 shadow maps).
			glDrawBuffer(GL_NONE);
		}

		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			CS_CORE_ERROR("Framebuffer is incomplete! glCheckFramebufferStatus = {0:#x}", status);
			CS_CORE_ASSERT(false, "Framebuffer incomplete — see error log for the status code.");
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLFrameBuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
	}

	void OpenGLFrameBuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////

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

	uint32_t OpenGLFrameBuffer::GetColorAttachmentRendererID(uint32_t index) const
	{
		if (index >= m_ColorAttachments.size())
		{
			CS_CORE_WARN("OpenGLFrameBuffer::GetColorAttachmentRendererID: index {0} out of range ({1} attachments).",
			             index, m_ColorAttachments.size());
			return 0;
		}
		return m_ColorAttachments[index];
	}

	int OpenGLFrameBuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
	{
		if (attachmentIndex >= m_ColorAttachments.size())
			return -1;

		// The FBO must already be bound by the caller.
		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		int pixel = -1;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixel);
		return pixel;
	}

	void OpenGLFrameBuffer::ClearAttachment(uint32_t attachmentIndex, int value)
	{
		if (attachmentIndex >= m_ColorAttachments.size())
			return;

		// glClearBufferiv reads 4 components for GL_COLOR; replicate `value`. The
		// FBO must be bound; `attachmentIndex` is also the draw-buffer index
		// (glDrawBuffers stored GL_COLOR_ATTACHMENT0+i at slot i).
		const GLint clear[4] = { value, value, value, value };
		glClearBufferiv(GL_COLOR, (GLint)attachmentIndex, clear);
	}

	float OpenGLFrameBuffer::ReadDepth(int x, int y)
	{
		if (m_DepthAttachmentSpec.TextureFormat == FramebufferTextureFormat::None)
			return 1.0f;

		// The FBO must already be bound by the caller. Depth reads come from the
		// depth buffer directly (not a color read-buffer), so no glReadBuffer here.
		float depth = 1.0f;
		glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
		return depth;
	}

	bool OpenGLFrameBuffer::ReadPixels(uint32_t attachmentIndex, std::vector<uint8_t>& outRGBA,
	                                   uint32_t& outWidth, uint32_t& outHeight)
	{
		if (attachmentIndex >= m_ColorAttachments.size())
			return false;

		const uint32_t w = m_Specification.Width;
		const uint32_t h = m_Specification.Height;
		outWidth  = w;
		outHeight = h;
		if (w == 0 || h == 0)
		{
			outRGBA.clear();
			return true;
		}

		// The FBO must already be bound by the caller.
		outRGBA.assign(static_cast<size_t>(w) * h * 4, 0);
		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, outRGBA.data());

		// GL's origin is bottom-left; flip rows so the buffer is top-left origin.
		const size_t rowBytes = static_cast<size_t>(w) * 4;
		std::vector<uint8_t> tmp(rowBytes);
		for (uint32_t y = 0; y < h / 2; ++y)
		{
			uint8_t* top = outRGBA.data() + static_cast<size_t>(y) * rowBytes;
			uint8_t* bot = outRGBA.data() + static_cast<size_t>(h - 1 - y) * rowBytes;
			std::memcpy(tmp.data(), top, rowBytes);
			std::memcpy(top, bot, rowBytes);
			std::memcpy(bot, tmp.data(), rowBytes);
		}
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////
}
