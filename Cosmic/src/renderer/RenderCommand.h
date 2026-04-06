#pragma once

#include "core/Core.h"
#include "renderer/RendererAPI.h"
#include "graphics/VertexArray.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	/**
	 * @brief Static utility class that dispatches commands to the specific RendererAPI implementation.
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
		 * @brief Sets the clear color for the setup.
		 */
		inline static void SetClearColor(const glm::vec4& color)
		{
			s_RendererAPI->SetClearColor(color);
		}

		/**
		 * @brief Clears the buffers using the previously set clear color.
		 */
		inline static void Clear()
		{
			s_RendererAPI->Clear();
		}

		/**
		 * @brief Legacy helper to set color and clear in one call.
		 */
		inline static void Clear(float r, float g, float b)
		{
			s_RendererAPI->SetClearColor({ r, g, b, 1.0f });
			s_RendererAPI->Clear();
		}

		/**
		 * @brief Executes a draw call.
		 * Matches the signature in RendererAPI.h.
		 */
		inline static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t count = 0)
		{
			s_RendererAPI->DrawIndexed(vertexArray, count);
		}

	private:
		static RendererAPI* s_RendererAPI;
	};
}