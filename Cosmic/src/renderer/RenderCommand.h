#pragma once

#include "core/Core.h"
#include "renderer/RendererAPI.h"
#include "graphics/VertexArray.h"

namespace Cosmic
{
	/**
	 * @brief Static utility class that dispatches commands to the specific RendererAPI implementation.
	 * * The s_RendererAPI is initialized in the .cpp file based on the selected graphics API.
	 */
	class RenderCommand
	{
	public:
		/**
		 * @brief Initializes the active Graphics API.
		 */
		inline static void Init()
		{
			s_RendererAPI->Init();
		}

		/**
		 * @brief Sets the screen/window viewport size.
		 */
		inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
		{
			s_RendererAPI->SetViewport(x, y, width, height);
		}

		/**
		 * @brief Sets the clear color and clears the buffers.
		 */
		inline static void Clear(float r, float g, float b)
		{
			s_RendererAPI->SetClearColor({ r, g, b, 1.0f });
			s_RendererAPI->Clear();
		}

		/**
		 * @brief Executes a draw call.
		 * @param vertexArray The Vertex Array containing the data to draw.
		 * @param count The number of indices to draw. If 0, the entire Index Buffer is drawn.
		 */
		inline static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t count = 0)
		{
			s_RendererAPI->DrawIndexed(vertexArray, count);
		}

	private:
		// Pointer to the active API implementation (e.g., OpenGLRendererAPI)
		static RendererAPI* s_RendererAPI;
	};

}