#include "renderer/RenderCommand.h"

// Include your platform implementations
#include "platform/opengl/OpenGLRendererAPI.h"
// #include "platform/directx/DirectXRendererAPI.h" // Add this when ready

namespace Cosmic
{

    /**
     * @brief Logic to choose the API based on the flag.
     * We use a helper function because you cannot put a switch statement
     * directly in a static member initialization.
     */
    static RendererAPI* CreateRendererAPI()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:  return new OpenGLRendererAPI();
        case RendererAPI::API::DirectX: return nullptr; // return new DirectXRendererAPI();
        case RendererAPI::API::None:    return nullptr;
        }

        return nullptr;
    }

    // Initialize the static pointer by calling our selection logic
    RendererAPI* RenderCommand::s_RendererAPI = CreateRendererAPI();

}