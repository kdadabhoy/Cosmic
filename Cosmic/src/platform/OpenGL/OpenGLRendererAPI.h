#pragma once

// OpenGLRendererAPI.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * OpenGLRendererAPI is the concrete implementation of the RendererAPI interface
 * for the OpenGL graphics backend. It provides the low-level implementation for
 * the virtual calls made by the high-level RenderCommand system.
 * 
 * This class is responsible for executing raw OpenGL commands (glDrawElements,
 * glClear, etc.) and managing global OpenGL state settings such as alpha
 * blending and depth testing.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. void Init()
 * Pre:  A valid OpenGL context has been made current.
 * Post: Global OpenGL states (Blending, Depth Testing) are enabled and configured.
 * 
 * 2. void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
 * Pre:  None.
 * Post: OpenGL viewport is set to the specified dimensions.
 * 
 * 3. void SetClearColor(const glm::vec4& color)
 * Pre:  None.
 * Post: Internal OpenGL clear color state is updated.
 * 
 * 4. void Clear()
 * Pre:  None.
 * Post: The color and depth buffers are cleared using the current clear color.
 * 
 * 5. void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
 * Pre:  The desired VertexArray and associated Shader must be bound.
 * Post: Executes an indexed draw call (GL_TRIANGLES) for the specified index count.
 * 
 * 6. void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
 * Pre:  The desired VertexArray must be bound.
 * Post: Executes an array-based draw call (GL_LINES) for debug and wireframe rendering.
 */

#include "core/Core.h"
#include "renderer/RendererAPI.h"

namespace Cosmic
{
	class OpenGLRendererAPI : public RendererAPI
	{
	public:
		////////////////////////////////
		// Hardware Initialization
		///////////////////////////////

		virtual void	Init() override;

		////////////////////////////////
		// State & Viewport Control
		///////////////////////////////

		virtual void	SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
		virtual void	SetClearColor(const glm::vec4& color) override;
		virtual void	Clear() override;
		virtual void	SetDepthTest(bool enabled) override;
		virtual void	SetDepthWrite(bool enabled) override;

		////////////////////////////////
		// Primitive Submission
		///////////////////////////////

		virtual void	DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		virtual void	DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;

		virtual void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) override;
	};
}