#pragma once

#include "core/Core.h"
#include "renderer/RenderCommand.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"
#include "graphics/Texture.h"

#include <glm/glm.hpp>

namespace Cosmic
{
	class Renderer
	{
	public:
		static void Init();
		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene(OrthographicCamera& camera);
		static void EndScene();

		// For objects with textures
		static void Submit(const Ref<Shader>& shader,
			const Ref<VertexArray>& vertexArray,
			const Ref<Texture>& texture,
			const glm::mat4& transform = glm::mat4(1.0f));

		// For objects without textures (uses default behavior or solid colors)
		static void Submit(const Ref<Shader>& shader,
			const Ref<VertexArray>& vertexArray,
			const glm::mat4& transform = glm::mat4(1.0f));

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};

		static SceneData* s_SceneData;
	};
}