#include "platform/opengl/OpenGLRendererAPI.h"
#include <glad/glad.h>
#include "core/Core.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::Init()
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::Clear()
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
	{
		// Ensure the correct Vertex Array Object is active for this draw call
		vertexArray->Bind();

		// If indexCount is 0, use the total count from the index buffer (standard 3D)
		// Otherwise, use the specific count passed in (batch rendering)
		uint32_t count = indexCount != 0 ? indexCount : vertexArray->GetIndexBuffer()->GetCount();

		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
	}

	/////////////////////////////////////////////////////////////////////////////////
	void OpenGLRendererAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	{
		// Ensure the correct Vertex Array Object is active for this draw call
		vertexArray->Bind();

		glDrawArrays(GL_LINES, 0, vertexCount);
	}
}