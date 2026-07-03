// TextureCube.cpp — factory dispatch. See TextureCube.h.

#include "graphics/TextureCube.h"
#include "renderer/RendererAPI.h"
#include "platform/OpenGL/OpenGLTextureCube.h"

namespace Cosmic
{
	Ref<TextureCube> TextureCube::Create(const TextureCubeSpecification& spec)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:   return nullptr;
		case RendererAPI::API::OpenGL: return std::static_pointer_cast<TextureCube>(CreateRef<OpenGLTextureCube>(spec));
		}
		return nullptr;
	}
}
