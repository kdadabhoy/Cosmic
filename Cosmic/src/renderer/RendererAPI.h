#pragma once

// Abstract Class for back-end... has a flag to pick what graphics API
// Every platform will derive a RendererAPI class from this

#include "core/Core.h"
#include <glm/glm.hpp>
#include "graphics/VertexArray.h"

namespace Cosmic
{
	class RendererAPI
	{
	public:
		enum class API
		{
			None = 0, OpenGL = 1, DirectX = 2
		};

	public:
		virtual ~RendererAPI() = default;

		virtual void Init() = 0;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		/**
		 * @brief Executes a draw call.
		 * @param vertexArray The Vertex Array containing the data.
		 * @param count The specific number of indices to draw. If 0, draws the entire index buffer.
		 */
		 // Inside RendererAPI class
		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;

		inline static API GetAPI() { return s_API; }

	private:
		static API s_API;
	};
}