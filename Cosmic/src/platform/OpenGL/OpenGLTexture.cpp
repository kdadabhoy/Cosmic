// windows.h defines APIENTRY as __stdcall; undef it so glad can redefine it cleanly.
#ifdef _WIN32
    #include <windows.h>
#endif

#ifdef APIENTRY
    #undef APIENTRY
#endif

#include "platform/opengl/OpenGLTexture.h"
#include "platform/opengl/OpenGLContext.h"
#include <stb_image.h>
#include "core/Log.h"


namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLTexture Constructor (Procedural/Empty)
	 * * Generates a GPU texture with specified dimensions but no initial data.
	 * * Useful for creating solid color textures (like the engine's default 1x1 white texture)
	 * or for textures that will be populated later via SetData().
	 */
	OpenGLTexture::OpenGLTexture(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height)
	{
		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_2D, m_RendererID);

		// Default Parameters for procedural textures
		// We use GL_NEAREST for magnification to keep pixel-art style sharp
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// Pre-allocate GPU storage without uploading data (nullptr)
		glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, GL_UNSIGNED_BYTE, nullptr);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLTexture Constructor (File-based)
	 * * Orchestrates the full asset loading pipeline:
	 * 1. Uses stb_image to load and decode compressed image data (PNG, JPG, etc.).
	 * 2. Flips the image vertically to match OpenGL's coordinate system (origin at bottom-left).
	 * 3. Dynamically determines the format (RGB vs RGBA) based on the image's channel count.
	 * 4. Configures Mipmaps for high-quality scaling at various distances.
	 */
	OpenGLTexture::OpenGLTexture(const std::string& path)
		: m_Path(path)
	{
		int width, height, channels;

		// OpenGL expects 0.0 on the y-axis to be the bottom, but images usually store the top row first.
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		if (data)
		{
			m_Width = width;
			m_Height = height;

			// Logic to select the appropriate GPU internal format based on source channels.
			// Covers RGBA, RGB, grayscale+alpha (RG), and grayscale (R). Leaving the format
			// at 0 for an unhandled channel count would feed glTexImage2D an invalid enum,
			// silently failing the upload and rendering the texture black.
			GLenum internalFormat = 0, dataFormat = 0;
			if (channels == 4)
			{
				internalFormat = GL_RGBA8;
				dataFormat = GL_RGBA;
			}
			else if (channels == 3)
			{
				internalFormat = GL_RGB8;
				dataFormat = GL_RGB;
			}
			else if (channels == 2)
			{
				internalFormat = GL_RG8;
				dataFormat = GL_RG;
			}
			else if (channels == 1)
			{
				internalFormat = GL_R8;
				dataFormat = GL_RED;
			}
			else
			{
				CS_CORE_ERROR("OpenGLTexture: Unsupported channel count ({0}) in '{1}'.", channels, path);
				stbi_image_free(data);
				m_Width          = 0;
				m_Height         = 0;
				m_InternalFormat = GL_RGBA8;
				m_DataFormat     = GL_RGBA;
				return;
			}

			m_InternalFormat = internalFormat;
			m_DataFormat = dataFormat;

			glGenTextures(1, &m_RendererID);
			glBindTexture(GL_TEXTURE_2D, m_RendererID);

			// Setup Filtering - Using Linear Mipmap for smoother scaling at distances
			// Dino and other sprites use NEAREST for magnification to maintain the retro aesthetic.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

			// Upload pixel data to the GPU
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_UNSIGNED_BYTE, data);

			// Required for the GL_LINEAR_MIPMAP_LINEAR filter to work correctly
			glGenerateMipmap(GL_TEXTURE_2D);

			stbi_image_free(data);
		}
		else
		{
			CS_CORE_ERROR("Failed to load texture at {0}", path);
			m_Width          = 0;
			m_Height         = 0;
			m_InternalFormat = GL_RGBA8;
			m_DataFormat     = GL_RGBA;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLTexture Destructor
	 * * Ensures GPU memory is released when the texture object is destroyed.
	 */
	OpenGLTexture::~OpenGLTexture()
	{
		// Guard against teardown order: if the OpenGL context has already been
		// destroyed (e.g. an abort/early-exit path that skipped Renderer2D::Shutdown,
		// or static destruction at process exit), glDeleteTextures would fault inside
		// opengl32.dll. The driver already reclaimed the GPU memory when the context
		// died, so skipping the call here leaks nothing.
		if (m_RendererID != 0 && OpenGLContext::HasCurrentContext())
			glDeleteTextures(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetData
	 * * Allows manual updates to the texture pixels.
	 * * CORE LOGIC: Uses glTexSubImage2D to overwrite the existing GPU memory block.
	 * This is much more efficient than glTexImage2D as it doesn't reallocate the buffer.
	 */
	void OpenGLTexture::SetData(void* data, uint32_t size)
	{
		uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;

		// Pre-condition: The incoming data must exactly match the texture's footprint
		if (size != m_Width * m_Height * bpp)
		{
			CS_CORE_ERROR("Texture data must fill entire texture! Expected size: {0}, provided: {1}", m_Width * m_Height * bpp, size);
			return;
		}

		glBindTexture(GL_TEXTURE_2D, m_RendererID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Maps the texture to a specific GPU hardware slot.
	 * * BATCH RENDERING: By specifying a 'slot', the engine can have multiple
	 * textures active simultaneously, allowing the Batch Renderer to draw quads
	 * from different textures in a single draw call.
	 */
	void OpenGLTexture::Bind(uint32_t slot) const
	{
		if (m_RendererID == 0)
		{
			CS_CORE_WARN("OpenGLTexture::Bind: Attempted to bind a texture that failed to load (path: {0}). Slot {1} will render black.", m_Path, slot);
			return;
		}
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Equality Operator
	 * * Compares textures based on their internal RendererID.
	 * * Used by the Batch Renderer to verify if a texture is already bound to a slot.
	 */
	bool OpenGLTexture::operator==(const Texture& other) const
	{
		return m_RendererID == ((const OpenGLTexture&)other).m_RendererID;
	}

	/////////////////////////////////////////////////////////////////////////////////
}