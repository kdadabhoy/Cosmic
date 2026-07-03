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

		// Let vertex shaders set gl_PointSize (S4.7 GPU-point rendering).
		glEnable(GL_PROGRAM_POINT_SIZE);
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
	 * SetDepthTest / SetDepthWrite
	 * * Depth-state toggles for passes that need explicit control (e.g. a sky
	 * gradient drawn without depth, or transparent 3D geometry drawn without depth
	 * writes). Both are ON by default from Init(); changers must restore.
	 */
	void OpenGLRendererAPI::SetDepthTest(bool enabled)
	{
		if (enabled) glEnable(GL_DEPTH_TEST);
		else         glDisable(GL_DEPTH_TEST);
	}

	void OpenGLRendererAPI::SetDepthWrite(bool enabled)
	{
		glDepthMask(enabled ? GL_TRUE : GL_FALSE);
	}

	void OpenGLRendererAPI::SetCullMode(CullMode mode)
	{
		switch (mode)
		{
		case CullMode::None:
			glDisable(GL_CULL_FACE);
			break;
		case CullMode::Back:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			break;
		case CullMode::Front:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_FRONT);
			break;
		}
	}

	void OpenGLRendererAPI::SetBlendMode(BlendMode mode)
	{
		switch (mode)
		{
		case BlendMode::Alpha:                       // engine default (Init)
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case BlendMode::Additive:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		case BlendMode::Off:
			glDisable(GL_BLEND);
			break;
		}
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
	 * * CONTRACT: The caller binds the VertexArray before calling — identical to
	 * DrawIndexed and DrawIndexedInstanced. (Renderer2D::Flush already binds the line
	 * VAO before this call; binding here too would be a redundant second bind.)
	 */
	void OpenGLRendererAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	{
		glDrawArrays(GL_LINES, 0, vertexCount);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount)
	{
		glDrawElementsInstanced(GL_TRIANGLES,
			static_cast<GLsizei>(indexCount),
			GL_UNSIGNED_INT,
			nullptr,
			static_cast<GLsizei>(instanceCount));
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Compute & Attribute-less Draw (S4.7)
	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::DispatchCompute(uint32_t x, uint32_t y, uint32_t z)
	{
		glDispatchCompute(x, y, z);
	}

	void OpenGLRendererAPI::GpuMemoryBarrier(GpuBarrier bits)
	{
		GLbitfield gl = 0;
		if (bits & GpuBarrier::VertexAttribArray) gl |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
		if (bits & GpuBarrier::ShaderStorage)     gl |= GL_SHADER_STORAGE_BARRIER_BIT;
		if (bits & GpuBarrier::ShaderImage)       gl |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
		if (bits == GpuBarrier::All)              gl = GL_ALL_BARRIER_BITS;
		glMemoryBarrier(gl);
	}

	void OpenGLRendererAPI::DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count)
	{
		// Core GL requires a bound VAO even for attribute-less draws.
		if (m_EmptyVAO == 0)
			glGenVertexArrays(1, &m_EmptyVAO);
		glBindVertexArray(m_EmptyVAO);

		GLenum mode = GL_TRIANGLES;
		switch (topology)
		{
		case PrimitiveTopology::Points:    mode = GL_POINTS;    break;
		case PrimitiveTopology::Lines:     mode = GL_LINES;     break;
		case PrimitiveTopology::Triangles: mode = GL_TRIANGLES; break;
		}
		glDrawArrays(mode, static_cast<GLint>(first), static_cast<GLsizei>(count));
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Texture Binding (S6 — post-process passes)
	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLRendererAPI::BindTextureSlot(uint32_t slot, uint32_t rendererID)
	{
		// glActiveTexture + glBindTexture (not DSA glBindTextureUnit) to match the
		// engine's existing glGen*/glBind* style (S4 execution notes).
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, rendererID);
	}

	void OpenGLRendererAPI::BindTextureCubeSlot(uint32_t slot, uint32_t rendererID)
	{
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_CUBE_MAP, rendererID);
	}

	uint32_t OpenGLRendererAPI::GetBoundFramebuffer() const
	{
		GLint id = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &id);
		return static_cast<uint32_t>(id);
	}

	void OpenGLRendererAPI::BindFramebufferHandle(uint32_t id)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, id);
	}

	/////////////////////////////////////////////////////////////////////////////////


}