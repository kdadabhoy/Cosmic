// windows.h defines APIENTRY as __stdcall; undef it so glad can redefine it cleanly.
#ifdef _WIN32
	#include <windows.h>
#endif
#ifdef APIENTRY
	#undef APIENTRY
#endif

#include <glad/glad.h>
#include "platform/OpenGL/OpenGLTextureCube.h"
#include "platform/OpenGL/OpenGLContext.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	namespace
	{
		void CubeFormat(TextureCubeFormat fmt, GLenum& internalFmt, GLenum& dataFmt, GLenum& dataType)
		{
			switch (fmt)
			{
			case TextureCubeFormat::RGBA16F: internalFmt = GL_RGBA16F; dataFmt = GL_RGBA; dataType = GL_FLOAT; break;
			case TextureCubeFormat::RGB16F:
			default:                         internalFmt = GL_RGB16F;  dataFmt = GL_RGB;  dataType = GL_FLOAT; break;
			}
		}
	}

	OpenGLTextureCube::OpenGLTextureCube(const TextureCubeSpecification& spec)
		: m_Size(spec.Size)
	{
		GLenum dataFmt = GL_RGB, dataType = GL_FLOAT;
		CubeFormat(spec.Format, m_InternalFormat, dataFmt, dataType);

		if (spec.Mipmapped)
		{
			// One mip chain down to 1x1.
			m_MipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(m_Size, 1u)))));
		}

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

		for (uint32_t face = 0; face < 6; ++face)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, m_InternalFormat,
			             m_Size, m_Size, 0, dataFmt, dataType, nullptr);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
		                spec.Mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

		if (spec.Mipmapped)
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);   // allocate the chain now; bakes refill it
	}

	OpenGLTextureCube::~OpenGLTextureCube()
	{
		if (!OpenGLContext::HasCurrentContext())
			return;
		if (m_FBO)
			glDeleteFramebuffers(1, &m_FBO);
		if (m_RendererID)
			glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTextureCube::Bind(uint32_t slot) const
	{
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
	}

	void OpenGLTextureCube::BeginRenderToFace(uint32_t face, uint32_t mip)
	{
		if (m_FBO == 0)
			glGenFramebuffers(1, &m_FBO);

		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_RendererID, static_cast<GLint>(mip));

		const uint32_t mipSize = std::max(1u, m_Size >> mip);
		glViewport(0, 0, mipSize, mipSize);
	}

	void OpenGLTextureCube::FinishRender()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLTextureCube::GenerateMips()
	{
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	}
}
