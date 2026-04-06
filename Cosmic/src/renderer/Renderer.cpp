#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;

	void Renderer::Init()
	{
		RenderCommand::Init();
		Renderer2D::Init();
	}

	void Renderer::Shutdown()
	{
		Renderer2D::Shutdown();
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	void Renderer::BeginScene(OrthographicCamera& camera)
	{
		s_SceneData->ViewProjectionMatrix = camera.GetViewProjectionMatrix();
	}

	void Renderer::EndScene()
	{
		// Future: Submit CommandQueue for sorting/optimization
	}

	// Base Submit (Raw Transform)
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Ref<Texture>& texture, const glm::mat4& transform)
	{
		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		if (texture)
		{
			texture->Bind(0);
			shader->SetInt("u_Texture", 0);
		}

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		Submit(shader, vertexArray, nullptr, transform);
	}

	// Component Submit (No Rotation)
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, const glm::vec3& scale)
	{
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), scale);

		Submit(shader, vertexArray, nullptr, transform);
	}

	// Component Submit (With Rotation)
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, float rotationDegrees, const glm::vec3& scale)
	{
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), scale);

		Submit(shader, vertexArray, nullptr, transform);
	}
}