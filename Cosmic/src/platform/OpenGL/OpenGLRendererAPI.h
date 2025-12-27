#pragma once

// This is the file that fulfills the virtual calls made by RenderCommand
#include "core/Core.h"
#include "renderer/RendererAPI.h"

namespace Cosmic
{

	class OpenGLRendererAPI : public RendererAPI
	{
	public:
		void				Init()																	override;
		void				SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)	override;
		void				SetClearColor(const glm::vec4& color)									override;
		void				Clear()																	override;

		void				DrawIndexed(const Ref<VertexArray>& vertexArray)						override;
	};

}