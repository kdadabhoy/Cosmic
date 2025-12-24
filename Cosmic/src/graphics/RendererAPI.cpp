#include "graphics/RendererAPI.h"

namespace Cosmic
{
	// The Master Flag: Change this to DirectX (2) later to swap back-ends
	RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
}