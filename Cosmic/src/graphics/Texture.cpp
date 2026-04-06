#include "graphics/Texture.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLTexture.h"

namespace Cosmic
{
	Ref<Texture> Texture::Create(const std::string& path)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture>(path);
		}

		return nullptr;
	}
}