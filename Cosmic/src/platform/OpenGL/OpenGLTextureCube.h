#pragma once

// OpenGLTextureCube.h
// Last Modified: 7/3/2026

#include <glad/glad.h>
#include "graphics/TextureCube.h"

namespace Cosmic
{
	// Concrete cubemap for the OpenGL backend. See TextureCube.h for the contract.
	class OpenGLTextureCube : public TextureCube
	{
	public:
		OpenGLTextureCube(const TextureCubeSpecification& spec);
		virtual ~OpenGLTextureCube();

		virtual void     Bind(uint32_t slot = 0) const override;
		virtual uint32_t GetRendererID() const override { return m_RendererID; }
		virtual uint32_t GetSize() const override       { return m_Size; }
		virtual uint32_t GetMipLevels() const override  { return m_MipLevels; }

		virtual void BeginRenderToFace(uint32_t face, uint32_t mip = 0) override;
		virtual void FinishRender() override;
		virtual void GenerateMips() override;

	private:
		uint32_t m_RendererID = 0;
		uint32_t m_Size       = 0;
		uint32_t m_MipLevels  = 1;
		GLenum   m_InternalFormat = GL_RGBA16F;

		// Lazily-created FBO used only when this cube is a render target.
		uint32_t m_FBO = 0;
		bool     m_CheckedComplete = false;   // one completeness log per cube
	};
}
