#include "graphics/Texture.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLTexture.h"

namespace Cosmic
{
	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (RendererAPI::GetAPI())
		{
			case RendererAPI::API::None:    return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture>(width, height);
		}

		return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (RendererAPI::GetAPI())
		{
			case RendererAPI::API::None:    return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture>(path);
		}

		return nullptr;
	}

	// Keep existing Texture::Create for backward compatibility if needed
	Ref<Texture> Texture::Create(const std::string& path)
	{
		return Texture2D::Create(path);
	}
}