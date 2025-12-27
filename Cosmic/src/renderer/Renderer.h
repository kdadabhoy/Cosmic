#pragma once

#include "core/Core.h"
#include "renderer/RenderCommand.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Shader.h"



namespace Cosmic
{
	class Renderer
	{
	public:
		static void							Init();

		static void							BeginScene(OrthographicCamera& camera);
		static void							EndScene();

		static void							Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		static void							OnWindowResize(uint32_t width, uint32_t height);

		inline static RendererAPI::API		GetAPI() { return RendererAPI::GetAPI(); }

	private:
		struct SceneData
		{
			glm::mat4		ViewProjectionMatrix;
		};

		static SceneData*	s_SceneData;
	};
}