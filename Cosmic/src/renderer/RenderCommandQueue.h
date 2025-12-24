#pragma once

// Purpose: a Simple queue that stores rendering commands to be executed at the end of a frame
// This decouples the "Submission" from the "Execution"


#include "graphics/RendererAPI.h"

namespace Cosmic
{

	class RenderCommand
	{
	public:
		inline static void Init() { s_RendererAPI->Init(); }

		inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
		{
			s_RendererAPI->SetViewport(x, y, width, height);
		}

		inline static void Clear(float r, float g, float b)
		{
			s_RendererAPI->Clear(r, g, b);
		}

		inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
		{
			s_RendererAPI->DrawIndexed(vertexArray);
		}

	private:
		// This pointer is initialized based on the RendererAPI::API flag
		static RendererAPI* s_RendererAPI;
	};

}