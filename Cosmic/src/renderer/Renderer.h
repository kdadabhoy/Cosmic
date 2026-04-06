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

		// --- Original Submit (Raw Transform) ---
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Ref<Texture>& texture, const glm::mat4& transform = glm::mat4(1.0f));
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		// --- New Overloads (Components with Rotation) ---

		// Basic Submit (No rotation parameter in call)
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f));

		// Rotated Submit (With rotation parameter)
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, float rotationDegrees, const glm::vec3& scale = glm::vec3(1.0f));

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};

		static SceneData* s_SceneData;
	};
}