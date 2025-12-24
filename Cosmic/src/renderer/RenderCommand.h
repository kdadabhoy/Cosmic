#pragma once

#include "graphics/RendererAPI.h"
#include "graphics/VertexArray.h"

// RenderCommand is a static utility class that dispatches commands to the specific RendererAPI implementation
// TODO: Refactor this to put the commands in the .cpp

namespace Cosmic
{
    class RenderCommand
    {
    public:
        // Initialize the graphics API
        inline static void Init()
        {
            s_RendererAPI->Init();
        }

        // Set the screen/window viewport size
        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        // Clear the screen with a specific color
        inline static void Clear(float r, float g, float b)
        {
            s_RendererAPI->SetClearColor({ r, g, b, 1.0f });
            s_RendererAPI->Clear();
        }

        // Execute a draw call
        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
        {
            s_RendererAPI->DrawIndexed(vertexArray);
        }


    private:
        // This is the pointer that holds either OpenGLRendererAPI or DirectXRendererAPI
        static RendererAPI* s_RendererAPI;
    };

}