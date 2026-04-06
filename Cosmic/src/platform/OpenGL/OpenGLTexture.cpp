#include "platform/opengl/OpenGLTexture.h"
#include <stb_image.h>
#include <iostream>

namespace Cosmic
{
	OpenGLTexture::OpenGLTexture(const std::string& path)
		: m_Path(path)
	{
		int width, height, channels;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		if (data)
		{
			m_Width = width;
			m_Height = height;

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

			m_InternalFormat = internalFormat;
			m_DataFormat = dataFormat;

			glGenTextures(1, &m_RendererID);
			glBindTexture(GL_TEXTURE_2D, m_RendererID);

			// Setup Filtering and Wrapping
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

			// Send pixel data to GPU
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_UNSIGNED_BYTE, data);

			// Clean up CPU memory
			stbi_image_free(data);
		}
		else
		{
			std::cout << "Cosmic Error: Failed to load texture at " << path << std::endl;
		}
	}

	OpenGLTexture::~OpenGLTexture()
	{
		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture::Bind(uint32_t slot) const
	{
		// Select the texture unit (0 to 31) and bind our texture to it
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, m_RendererID);
	}
}