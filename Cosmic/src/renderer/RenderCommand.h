#pragma once

// RenderCommand.h
// Last Modified: 5/14/2026
/**
	 * General Description:
	 * The RenderCommand class is a high-level static utility that serves as the
	 * engine's primary entry point for issuing hardware commands. It acts as a
	 * "Command Dispatcher," abstracting the complexities of the active Graphics API
	 * (OpenGL, DirectX, etc.) behind a unified interface.
	 *
	 * The Goal:
	 * The primary goal of this class is to achieve Hardware Independence. By routing
	 * all draw calls, buffer clears, and state changes through this interface, the
	 * rest of the engine (including the high-level Renderer and Renderer2D) can remain
	 * agnostic to which API is currently driving the display. This allows for seamless
	 * multi-platform support and simplified maintenance.
	 *
	 * Public Function Prototypes (Pre and Post Conditions):
	 *
	 * 1. static void Init()
	 *    Pre:  A valid RendererAPI implementation has been instantiated.
	 *    Post: The active Graphics API is initialized with engine-default states.
	 *
	 * 2. static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	 *    Pre:  None.
	 *    Post: The rendering region of the window is updated to the specified dimensions.
	 *
	 * 3. static void SetClearColor(const glm::vec4& color)
	 *    Pre:  None.
	 *    Post: Updates the internal state of the API with the color to be used during a Clear() call.
	 *
	 * 4. static void Clear()
	 *    Pre:  None.
	 *    Post: Clears the current frame's backbuffer (Color and Depth) using the stored ClearColor.
	 *
	 * 5. static void Clear(float r, float g, float b)
	 *    Pre:  None.
	 *    Post: Helper overload that sets the clear color and executes a clear in a single command.
	 *
	 * 6. static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t count)
	 *    Pre:  A valid VertexArray containing an Index Buffer must be bound.
	 *    Post: Submits an indexed draw call to the GPU. If count is 0, the VAO's full index count is used.
	 *
	 * 7. static void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	 *    Pre:  A valid VertexArray configured for line primitives must be bound.
	 *    Post: Submits a non-indexed line draw call for the specified number of vertices.
	 **/

#include "core/Core.h"
#include "renderer/RendererAPI.h"
#include "graphics/VertexArray.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API RenderCommand
	{
	public:

		/**
		 * @brief Initializes the active Graphics API implementation.
		 * Configures global capabilities such as blending, depth testing, and face culling.
		 */
		inline static void Init()
		{
			s_RendererAPI->Init();
		}

		/**
		 * @brief Sets the screen/window viewport size.
		 * Maps the normalized device coordinates to the window's pixel coordinates.
		 * @param x The horizontal offset from the bottom-left corner.
		 * @param y The vertical offset from the bottom-left corner.
		 * @param width The width of the viewport in pixels.
		 * @param height The height of the viewport in pixels.
		 */
		inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
		{
			s_RendererAPI->SetViewport(x, y, width, height);
		}

		/**
		 * @brief Sets the clear color for the graphics context.
		 * @param color A vec4 representing the (R, G, B, A) values.
		 */
		inline static void SetClearColor(const glm::vec4& color)
		{
			s_RendererAPI->SetClearColor(color);
		}

		/**
		 * @brief Clears the active framebuffers.
		 * Instructs the GPU to wipe the color and depth buffers to prepare for a new frame.
		 */
		inline static void Clear()
		{
			s_RendererAPI->Clear();
		}

		/**
		 * @brief Convenience helper to set color and clear in a single call.
		 * Primarily used in simple demo applications or quick test sweeps.
		 * @param r Red component (0.0f - 1.0f).
		 * @param g Green component (0.0f - 1.0f).
		 * @param b Blue component (0.0f - 1.0f).
		 */
		inline static void Clear(float r, float g, float b)
		{
			s_RendererAPI->SetClearColor({ r, g, b, 1.0f });
			s_RendererAPI->Clear();
		}

		/**
		 * @brief Enables or disables depth testing.
		 * ON by default from Init(). Generic render-state verb (see doc 05's
		 * forward-compatibility contract): passes that need explicit control — a sky
		 * gradient drawn without depth, transparent 3D geometry — toggle it here and
		 * MUST restore it before their scope ends. Never call GL directly for this.
		 */
		inline static void SetDepthTest(bool enabled)
		{
			s_RendererAPI->SetDepthTest(enabled);
		}

		/**
		 * @brief Enables or disables depth-buffer writes (depth mask).
		 * ON by default from Init(). Changers must restore — Renderer2D relies on
		 * the default state being in place when its batches flush.
		 */
		inline static void SetDepthWrite(bool enabled)
		{
			s_RendererAPI->SetDepthWrite(enabled);
		}

		/**
		 * @brief Sets the face-culling mode (None by default from Init()).
		 * None must stay the engine-wide default — 2D sprites flip winding via
		 * FlipX/FlipY. Passes that enable Back/Front (opaque 3D, shadow maps)
		 * must restore None before their scope ends, same contract as the depth
		 * verbs.
		 */
		inline static void SetCullMode(RendererAPI::CullMode mode)
		{
			s_RendererAPI->SetCullMode(mode);
		}

		/**
		 * @brief Executes an indexed draw call.
		 * This is the standard method for drawing optimized 2D and 3D geometry.
		 * @param vertexArray The Vertex Array Object containing the vertex and index data.
		 * @param count The number of indices to draw. If 0, uses the VAO's internal index buffer count.
		 */
		inline static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t count = 0)
		{
			s_RendererAPI->DrawIndexed(vertexArray, count);
		}

		/**
		 * @brief Executes a non-indexed line draw call.
		 * Used for drawing wireframes, debug outlines, and pathfinding visualizations.
		 * @param vertexArray The Vertex Array Object containing the point data.
		 * @param vertexCount The total number of vertices to process as lines.
		 */
		inline static void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
		{
			s_RendererAPI->DrawLines(vertexArray, vertexCount);
		}




		/**
		 * @brief Executes an instanced indexed draw call.
		 * Draws `indexCount` indices `instanceCount` times, advancing per-instance
		 * attributes once per instance via the divisors configured in the VAO.
		 */
		inline static void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray,
			uint32_t indexCount,
			uint32_t instanceCount)
		{
			s_RendererAPI->DrawIndexedInstanced(vertexArray, indexCount, instanceCount);
		}

		/////////////////////////////////////////////////////////////////////////////////
		// Compute & attribute-less draw (S4.7)
		/////////////////////////////////////////////////////////////////////////////////

		using GpuBarrier        = RendererAPI::GpuBarrier;
		using PrimitiveTopology = RendererAPI::PrimitiveTopology;
		using CullMode          = RendererAPI::CullMode;

		/** @brief Dispatch the bound compute program over an x*y*z work-group grid. */
		inline static void DispatchCompute(uint32_t x, uint32_t y, uint32_t z)
		{
			s_RendererAPI->DispatchCompute(x, y, z);
		}

		/** @brief GPU memory barrier so compute writes are visible to later reads.
		 *  Named GpuMemoryBarrier — <winnt.h> macro-defines MemoryBarrier. */
		inline static void GpuMemoryBarrier(GpuBarrier bits)
		{
			s_RendererAPI->GpuMemoryBarrier(bits);
		}

		/** @brief Attribute-less array draw (points/lines/tris from gl_VertexID). */
		inline static void DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count)
		{
			s_RendererAPI->DrawArrays(topology, first, count);
		}

	private:
		/**
		 * @brief Internal pointer to the active API implementation.
		 * This static instance is created at startup based on the selected platform.
		 */
		static RendererAPI* s_RendererAPI;
	};
}