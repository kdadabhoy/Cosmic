#include "renderer/Renderer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;

	/////////////////////////////////////////////////////////////////////////////////

	void Renderer::Init()
	{
		RenderCommand::Init();
	}

	/////////////////////////////////////////////////////////////////////////////////


	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////


	void Renderer::BeginScene(OrthographicCamera& camera)
	{
		// Grab the pre-calculated matrix from the camera we just fixed
		s_SceneData->ViewProjectionMatrix = camera.GetViewProjectionMatrix();
	}


	/////////////////////////////////////////////////////////////////////////////////

	void Renderer::EndScene()
	{

	}

	/////////////////////////////////////////////////////////////////////////////////

	void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
	{
		shader->Bind();

		// UPLOAD 1: The Camera Data (Uniform common to the whole scene)
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);

		// UPLOAD 2: The Object Data (Uniform specific to this model)
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	/////////////////////////////////////////////////////////////////////////////////

}