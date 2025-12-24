#include "renderer/RenderCommand.h"

// Include your platform implementations
#include "platform/opengl/OpenGLRendererAPI.h"
// #include "platform/directx/DirectXRendererAPI.h" // Add this when ready

namespace Cosmic
{

    /////////////////////////////////////////////////////////////////////////////////

    /* 
        Logic to choose API based on the flag
        A helper function is used bc you cannot put a switch statement 
        w/ static member initialization 
    */
    static RendererAPI* CreateRendererAPI()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:      return new OpenGLRendererAPI();
        case RendererAPI::API::DirectX:     return nullptr; // return new DirectXRendererAPI();
        case RendererAPI::API::None:        return nullptr;
        }

        return nullptr;
    }

	/////////////////////////////////////////////////////////////////////////////////

    // Initialize the static pointer by calling our selection logic
    // Can't be called before CreateRendererAPI... obviously
    RendererAPI* RenderCommand::s_RendererAPI = CreateRendererAPI();

}