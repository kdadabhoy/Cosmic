#include <glad/glad.h>
#include "platform/opengl/OpenGLRendererAPI.h"
#include "core/Core.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Init
	 * * Configures the initial OpenGL state machine.
	 * 1. Enables Alpha Blending to support transparent textures (standard SRC_ALPHA).
	 * 2. Enables Depth Testing to ensure correct 3D/Layered 2D occlusion.
	 */
	void OpenGLRendererAPI::Init()
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetViewport
	 * * Direct wrapper for glViewport. Defines the rectangle onto which the final
	 * rendered image is mapped.
	 */
	void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetClearColor
	 * * Sets the color used by the GPU when glClear is called.
	 */
	void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Clear
	 * * Wipes the screen. We clear both COLOR_BUFFER_BIT (pixels) and
	 * DEPTH_BUFFER_BIT (z-buffer) to prevent artifacts from previous frames.
	 */
	void OpenGLRendererAPI::Clear()
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * DrawIndexed
	 * * The primary method for rendering geometry.
	 * * BATCHING LOGIC: If indexCount is provided (non-zero), we draw only that specific
	 * portion of the buffer. This is critical for the Renderer2D, which may fill
	 * only half of a large pre-allocated vertex buffer before needing to flush.
	 * * Note: We use GL_UNSIGNED_INT, matching our IndexBuffer implementation.
	 */
	void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
	{
		uint32_t count = indexCount != 0 ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * DrawLines
	 * * Specialized draw call for wireframes and debug shapes. Unlike DrawIndexed,
	 * this utilizes glDrawArrays as debug lines in the Cosmic Engine often use
	 * non-indexed streaming buffers.
	 */
	void OpenGLRendererAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	{
		glDrawArrays(GL_LINES, 0, vertexCount);
	}

	/////////////////////////////////////////////////////////////////////////////////
}