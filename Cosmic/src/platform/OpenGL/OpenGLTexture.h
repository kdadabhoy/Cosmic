#pragma once

// OpenGLTexture.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * OpenGLTexture is the concrete implementation of the Texture2D interface for the
 * OpenGL graphics backend. It manages the lifecycle of GPU-resident image data,
 * supporting both asset-based loading from disk and procedural creation via
 * memory buffers.
 * 
 * This class handles the automated decoding of image formats (PNG, JPG) using
 * stb_image, configures hardware-level sampling states (filtering, wrapping),
 * and provides the binding logic necessary for the engine's multi-slot
 * batch rendering system.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLTexture(uint32_t width, uint32_t height)
 * Pre:  Width and height are greater than zero.
 * Post: An empty GPU texture resource is allocated with RGBA8 storage.
 * 
 * 2. OpenGLTexture(const std::string& path)
 * Pre:  The path points to a valid image file.
 * Post: Image data is decoded, flipped vertically for OpenGL compatibility,
 * uploaded to the GPU, and mipmaps are generated.
 * 
 * 3. virtual ~OpenGLTexture()
 * Pre:  The OpenGLTexture instance exists.
 * Post: The associated GPU texture resource is deleted via glDeleteTextures.
 * 
 * 4. void SetData(void* data, uint32_t size)
 * Pre:  'data' points to a buffer matching the texture's dimensions and format.
 * Post: The GPU's texture memory is updated with the new pixel data.
 * 
 * 5. void Bind(uint32_t slot = 0)
 * Pre:  A valid GPU texture handle exists.
 * Post: The texture is bound to the specified OpenGL texture unit (GL_TEXTURE0 + slot).
 * 
 * 6. bool operator==(const Texture& other)
 * Pre:  None.
 * Post: Returns true if both objects share the same internal OpenGL RendererID.
 */

#include <glad/glad.h>
#include "graphics/Texture.h"
#include <string>

namespace Cosmic
{
	class OpenGLTexture : public Texture2D
	{
	public:
		////////////////////////////////
		// Life Cycle & Initialization
		///////////////////////////////

		OpenGLTexture(uint32_t width, uint32_t height, bool mipmapped = false);
		OpenGLTexture(const std::string& path);
		OpenGLTexture(const std::string& path, bool hdr);       // HDR equirect → RGBA16F (H4)
		OpenGLTexture(const uint8_t* encoded, uint32_t size);   // decode from memory (S6.2 glTF)
		virtual ~OpenGLTexture();


		////////////////////////////////
		// Metadata Accessors
		///////////////////////////////

		virtual uint32_t	GetWidth() const override			{ return m_Width; }
		virtual uint32_t	GetHeight() const override			{ return m_Height; }
		virtual uint64_t	GetGpuBytes() const override;
		virtual uint32_t	GetRendererID() const override		{ return m_RendererID; }


		////////////////////////////////
		// GPU Data Operations
		///////////////////////////////

		virtual void		SetData(void* data, uint32_t size) override;
		virtual void		Bind(uint32_t slot = 0) const override;
		virtual void		SetSampling(TextureFilter filter, TextureWrap wrap) override;


		////////////////////////////////
		// Utility Operators
		///////////////////////////////

		virtual bool		operator==(const Texture& other) const override;


	private:
		////////////////////////////////
		// Asset & State Tracking
		///////////////////////////////

		std::string			m_Path;
		uint32_t			m_Width, m_Height;
		uint32_t			m_RendererID = 0;

		////////////////////////////////
		// Hardware Format Metadata
		///////////////////////////////

		GLenum				m_InternalFormat;
		GLenum				m_DataFormat;

		// Procedural textures created mipmapped regenerate their mip chain on
		// SetData (trilinear minification for distance sampling).
		bool				m_Mipmapped = false;

		// Whether a mip chain was actually generated (file + embedded textures
		// always mip; procedural only when m_Mipmapped; HDR never). Read by
		// GetGpuBytes to add the ~1/3 mip tail (T2 asset accounting).
		bool				m_HasMips = false;
	};
}