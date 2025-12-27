#include "renderer/RendererAPI.h"


namespace Cosmic
{
	// Where we determine which API to use... using macros based on operating system:

#ifdef COSMIC_PLATFORM_WINDOWS
	RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
//#else
	//RendererAPI::API RendererAPI::s_API = RendererAPI::API::DirectX;
#endif
}