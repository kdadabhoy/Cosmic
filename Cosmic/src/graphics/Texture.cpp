#include "graphics/Texture.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLTexture.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Texture2D::Create (Procedural)
	 * * Instantiates a blank Texture2D resource on the GPU.
	 * * API ABSTRACTION: Queries the active RendererAPI to return the correct
	 * platform implementation (e.g., OpenGLTexture). This is primarily used
	 * for creating the engine's "White Texture" (used for solid colors) or
	 * for textures generated via math or noise algorithms.
	 */
	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::static_pointer_cast<Texture2D>(CreateRef<OpenGLTexture>(width, height));
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Texture2D::Create (From Disk)
	 * * Loads an image file and uploads it to GPU memory.
	 * * ROLE IN PIPELINE: This is the primary method for loading assets like
	 * "Dino.png". It handles the cross-platform file reading and ensures
	 * that the resulting GPU object is managed via reference counting.
	 */
	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::static_pointer_cast<Texture2D>(CreateRef<OpenGLTexture>(path));
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Texture::Create
	 * * A generic convenience wrapper. Since most textures in a 2D engine are
	 * 2D by nature, this simply redirects to the Texture2D implementation.
	 */
	Ref<Texture> Texture::Create(const std::string& path)
	{
		return Texture2D::Create(path);
	}

	/////////////////////////////////////////////////////////////////////////////////
}