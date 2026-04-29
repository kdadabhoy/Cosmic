#pragma once

// This is the file that fulfills the virtual calls made by RenderCommand
#include "core/Core.h"
#include "renderer/RendererAPI.h"

namespace Cosmic
{
	class OpenGLRendererAPI : public RendererAPI
	{
	public:
		virtual void Init() override;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
		virtual void SetClearColor(const glm::vec4& color) override;
		virtual void Clear() override;

		// Updated to include the index count for batch rendering
		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;

		virtual void OpenGLRendererAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
	};
}