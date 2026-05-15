#include "renderer/RendererAPI.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Static API Initialization
	 * * This is the "Grand Switch" of the Cosmic Engine. By utilizing preprocessor 
	 * macros (like COSMIC_PLATFORM_WINDOWS), we determine which graphics backend 
	 * is assigned to s_API at compile time.
	 * * Implementation Note: All static factory methods (Shader::Create, Buffer::Create, etc.) 
	 * query this variable to decide which platform-specific class to instantiate.
	 */

	 // Where we determine which API to use... using macros based on operating system:
#ifdef COSMIC_PLATFORM_WINDOWS
	RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
#else
	// Defaulting to None or OpenGL for other platforms
	RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL; 
#endif

	/////////////////////////////////////////////////////////////////////////////////
}