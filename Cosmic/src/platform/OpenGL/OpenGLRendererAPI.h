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

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

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
		virtual void	SetCullMode(CullMode mode) override;
		virtual void	SetBlendMode(BlendMode mode) override;
		virtual void	SetPolygonMode(PolygonMode mode) override;

		////////////////////////////////
		// Primitive Submission
		///////////////////////////////

		virtual void	DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0,
		                            uint32_t indexOffset = 0) override;
		virtual void	DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;

		virtual void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) override;

		////////////////////////////////
		// Compute & Attribute-less Draw (S4.7)
		///////////////////////////////

		virtual void DispatchCompute(uint32_t x, uint32_t y, uint32_t z) override;
		virtual void GpuMemoryBarrier(GpuBarrier bits) override;
		virtual void DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count) override;

		////////////////////////////////
		// Texture Binding (S6 — post-process passes)
		///////////////////////////////

		virtual void BindTextureSlot(uint32_t slot, uint32_t rendererID) override;
		virtual void BindTextureCubeSlot(uint32_t slot, uint32_t rendererID) override;
		virtual uint32_t GetBoundFramebuffer() const override;
		virtual void     BindFramebufferHandle(uint32_t id) override;

		////////////////////////////////
		// GPU Timing (S12.5 profiler — doc 10 F3)
		///////////////////////////////

		virtual void BeginGpuZone(const char* name) override;
		virtual void EndGpuZone() override;
		virtual void GpuFrameMark() override;
		virtual const std::vector<GpuZoneResult>& GetGpuZoneResults() const override;

	private:
		// Lazily-created empty VAO for attribute-less DrawArrays (core GL requires
		// a bound VAO). Created on first DrawArrays call.
		uint32_t m_EmptyVAO = 0;

		// --- GPU timer-query state (F3) ---------------------------------------
		// GL_TIMESTAMP query pairs per zone, read back a few frames late so the
		// GPU never stalls. glGenQueries objects are pooled and recycled.
		struct GpuZoneRecord
		{
			std::string Name;
			uint32_t    Depth   = 0;
			uint32_t    StartQ  = 0;   // GL_TIMESTAMP query at BeginGpuZone
			uint32_t    EndQ    = 0;   // GL_TIMESTAMP query at EndGpuZone
		};
		struct GpuFrameRecord { std::vector<GpuZoneRecord> Zones; };

		uint32_t AcquireGpuQuery();                 // pool front or glGenQueries
		void     RecycleGpuFrame(GpuFrameRecord& frame);

		std::vector<uint32_t>       m_FreeQueries;  // recycled query object pool
		GpuFrameRecord              m_RecordingFrame;   // zones since the last GpuFrameMark
		std::vector<size_t>         m_ZoneStack;    // indices into m_RecordingFrame.Zones
		std::deque<GpuFrameRecord>  m_PendingFrames;    // in-flight (awaiting GPU results)
		std::vector<GpuZoneResult>  m_ZoneResults;  // most recent resolved frame
	};
}