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
		case RendererAPI::API::OpenGL:  return std::static_pointer_cast<Texture2D>(CreateRef<OpenGLTexture>(width, height));
		}

		return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::static_pointer_cast<Texture2D>(CreateRef<OpenGLTexture>(path));
		}

		return nullptr;
	}

	Ref<Texture> Texture::Create(const std::string& path)
	{
		return Texture2D::Create(path);
	}
}