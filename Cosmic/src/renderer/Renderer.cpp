#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// Global storage for scene-wide data (like View-Projection matrices)
	// This is allocated on the heap to persist across the static class lifetime.
	Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Init
	 * Bootstraps the rendering pipeline by initializing the low-level
	 * RenderCommand dispatcher and the optimized Renderer2D system.
	 */
	void Renderer::Init()
	{
		RenderCommand::Init();
		Renderer2D::Init();
	}

	/**
	 * Shutdown
	 * Performs a graceful cleanup of specialized rendering subsystems.
	 */
	void Renderer::Shutdown()
	{
		Renderer2D::Shutdown();
	}

	/**
	 * OnWindowResize
	 * Directly interfaces with the Graphics API via RenderCommand to
	 * adjust the OpenGL/Vulkan viewport mapping to the new window size.
	 */
	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * BeginScene
	 * Captures necessary environment data (Camera matrices) required
	 * for all draw calls submitted within this scene context.
	 */
	void Renderer::BeginScene(OrthographicCamera& camera)
	{
		s_SceneData->ViewProjectionMatrix = camera.GetViewProjectionMatrix();
	}

	/**
	 * EndScene
	 * Finalizes the current scene. Currently acts as a placeholder
	 * for future command sorting, multi-threading, or batch submission.
	 */
	void Renderer::EndScene()
	{
		// Future: Submit CommandQueue for sorting/optimization
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Submit (Base Implementation)
	 * The core bottleneck for non-batched rendering. Handles:
	 * 1. Shader binding and global uniform updates (ViewProjection).
	 * 2. Model-specific uniform updates (Transform).
	 * 3. Texture unit binding (Slot 0).
	 * 4. Dispatching the draw call to the RenderCommand system.
	 */
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

	/**
	 * Submit (Textureless Overload)
	 * Forwards call to the base Submit with no texture bound.
	 */
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		Submit(shader, vertexArray, nullptr, transform);
	}

	/**
	 * Submit (Component Translation/Scale)
	 * Helper that constructs a transform matrix from basic vectors
	 * for users who do not want to manage raw mat4 math.
	 */
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, const glm::vec3& scale)
	{
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), scale);

		Submit(shader, vertexArray, nullptr, transform);
	}

	/**
	 * Submit (Component Translation/Rotation/Scale)
	 * Advanced helper that constructs a TRS matrix, handling the
	 * degrees-to-radians conversion for Z-axis rotation internally.
	 */
	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, float rotationDegrees, const glm::vec3& scale)
	{
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees), { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), scale);

		Submit(shader, vertexArray, nullptr, transform);
	}

	/////////////////////////////////////////////////////////////////////////////////
}