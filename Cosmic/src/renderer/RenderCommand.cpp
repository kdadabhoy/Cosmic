// RenderCommand.cpp
// Last Modified: 5/14/2026

#include "renderer/RenderCommand.h"

// Platform-specific implementations
#include "platform/opengl/OpenGLRendererAPI.h"
// #include "platform/directx/DirectXRendererAPI.h"     // Placeholder for future D3D context

namespace Cosmic
{
    /**
     * @section API Selection Logic
     *
     * Because static member variables are initialized at global scope, we cannot
     * use complex logic (like switch statements) directly in the assignment.
     * CreateRendererAPI() acts as a private bootstrap function to determine
     * which hardware backend to instantiate based on the current engine configuration.
     */

     /**
      * @brief Factory function to instantiate the correct RendererAPI implementation.
      * @return A raw pointer to the heap-allocated Graphics API object.
      * 
      * Essentially: Logic to choose API based on the flag
      * A helper function is used bc you cannot put a switch statement 
      * w/ static member initialization 
      */
	static RendererAPI* CreateRendererAPI()
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::OpenGL:                  return new OpenGLRendererAPI();
		case RendererAPI::API::DirectX:                 return nullptr;                         // return new DirectXRendererAPI();
		case RendererAPI::API::None:                    return nullptr;
		}

		return nullptr;
	}

    /////////////////////////////////////////////////////////////////////////////////
    // Static Member Initialization
    /////////////////////////////////////////////////////////////////////////////////

    /**
     * The heart of the RenderCommand dispatcher.
     * This pointer is initialized exactly once when the engine's static memory is
     * allocated. It enables the static methods in the header to forward calls
     * to the active backend without the overhead of an instance lookup.
     */
	RendererAPI* RenderCommand::s_RendererAPI = CreateRendererAPI();

}