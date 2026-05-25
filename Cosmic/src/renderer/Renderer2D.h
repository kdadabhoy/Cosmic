#pragma once
// Renderer2D.h
// Last Modified: 5/24/2026

/**
 * General Description:
 * Renderer2D is a high-performance, 2D-specific hardware batch rendering system
 * designed to minimize draw call overhead. It manages separate internal vertex staging
 * buffers for Quads, Lines, and procedurally generated Circles. The system dynamically
 * executes automatic "flushes" to dispatch vertex arrays to the GPU when target buffer
 * capacities are saturated or when pipeline state transitions (such as switching Active
 * Materials or Textures) occur.
 *
 * RENDER PASS STACK (Multi-Camera / Multi-Viewport):
 * The renderer now supports a scoped render pass stack via PushRenderPass / PopRenderPass.
 * Each push captures the camera's View-Projection matrix and a pixel-space viewport region,
 * flushing any pending geometry from the prior pass before installing new state.
 * This allows multiple independent camera passes per frame without state leakage.
 * Clients should prefer the RAII RenderPass helper class over calling these directly.
 *
 * See RenderPass.h for usage patterns.
 *
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. void Init()
 *    Pre:  None.
 *    Post: GPU buffers, VAOs, shaders, and the fallback white texture are allocated.
 *
 * 2. void Shutdown()
 *    Pre:  Init() was previously called.
 *    Post: CPU staging buffers are freed; smart pointers release GPU resources.
 *
 * 3. void BeginScene(const OrthographicCamera& camera)
 *    Pre:  An active camera is provided.
 *    Post: Pushes a full-window render pass using the camera's VP matrix. Resets
 *          batch counters. Legacy compatibility wrapper over PushRenderPass.
 *
 * 4. void EndScene()
 *    Pre:  BeginScene() was called.
 *    Post: Calls PopRenderPass(), flushing remaining geometry.
 *
 * 5. void PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds)
 *    Pre:  Renderer2D::Init() must have been called.
 *    Post: Any pending geometry is flushed. The given VP matrix and viewport bounds
 *          are pushed onto the internal stack. The hardware viewport is updated.
 *          Batch counters are reset for the new pass.
 *
 * 6. void PopRenderPass()
 *    Pre:  A matching PushRenderPass call exists on the stack.
 *    Post: Remaining geometry is flushed. The top entry is removed. If a prior
 *          pass exists, its VP matrix and viewport are restored.
 *
 * 7. void Flush()
 *    Pre:  A render pass is active.
 *    Post: Submits all staged Quads, Lines, and Circles to the GPU.
 *
 * 8. void SetViewportSize(uint32_t width, uint32_t height)
 *    Pre:  None.
 *    Post: Updates internal viewport dimension tracking (used by shader uniforms).
 *
 * 9–N. DrawQuad / DrawRotatedQuad / DrawCircle / DrawLine / DrawRect
 *    Pre:  A render pass is active (BeginScene or PushRenderPass was called).
 *    Post: Geometry is appended to the appropriate batch buffer. A FlushAndReset is
 *          triggered automatically if buffer capacity is exceeded or state changes.
 */

#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Texture.h"
#include "graphics/Material.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	// Forward Declaration
	class SubTexture2D;

	class COSMIC_API Renderer2D
	{
	public:
		/////////////////////////////////////////////////////////////////////////////////
		// Lifecycle
		/////////////////////////////////////////////////////////////////////////////////

		static void Init();
		static void Shutdown();

		/////////////////////////////////////////////////////////////////////////////////
		// Scene / Pass Control
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Legacy scene wrapper. Equivalent to PushRenderPass with full viewport bounds.
		 * Prefer PushRenderPass / RenderPass RAII for multi-camera work.
		 */
		static void BeginScene(const OrthographicCamera& camera);

		/**
		 * @brief Legacy scene terminator. Equivalent to PopRenderPass.
		 */
		static void EndScene();

		/**
		 * @brief Push a new scoped render pass onto the stack.
		 *
		 * Flushes any pending geometry from the currently active pass, then installs the
		 * new camera VP matrix and updates the hardware viewport to viewportBounds.
		 *
		 * @param viewProj       Camera View-Projection matrix for this pass.
		 * @param viewportBounds Pixel region { x_offset, y_offset, width, height }.
		 */
		static void PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds);

		/**
		 * @brief Pop the current render pass from the stack.
		 *
		 * Flushes remaining geometry, removes the top entry, and restores the prior pass's
		 * VP matrix and viewport if one exists.
		 */
		static void PopRenderPass();

		/////////////////////////////////////////////////////////////////////////////////
		// Flush
		/////////////////////////////////////////////////////////////////////////////////

		static void Flush();

		/////////////////////////////////////////////////////////////////////////////////
		// Viewport
		/////////////////////////////////////////////////////////////////////////////////

		static void SetViewportSize(uint32_t width, uint32_t height);

		/////////////////////////////////////////////////////////////////////////////////
		// SubTexture2D Drawing
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));

		/////////////////////////////////////////////////////////////////////////////////
		// Material-Based Drawing
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Material>& material);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Material>& material);

		/////////////////////////////////////////////////////////////////////////////////
		// Primitive Drawing — Color & Texture
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		/////////////////////////////////////////////////////////////////////////////////
		// Rotated Quads — Color & Texture
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		/////////////////////////////////////////////////////////////////////////////////
		// Specialized Math Primitives (SDF)
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f);

		inline static void DrawCircle(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f)
		{
			DrawCircle({ position.x, position.y, 0.0f }, size, color, thickness, fade);
		}

		/////////////////////////////////////////////////////////////////////////////////
		// Debug Geometry
		/////////////////////////////////////////////////////////////////////////////////

		static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color);
		static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);

		/////////////////////////////////////////////////////////////////////////////////
		// Telemetry
		/////////////////////////////////////////////////////////////////////////////////

		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;
			uint32_t LineCount = 0;

			uint32_t GetTotalVertexCount() const { return QuadCount * 4 + LineCount * 2; }
			uint32_t GetTotalIndexCount()  const { return QuadCount * 6; }
		};

		static void       ResetStats();
		static Statistics GetStats();
		static void       SetStatsStatus(bool enabled);

		/////////////////////////////////////////////////////////////////////////////////
		// Render Pass State (used internally by RenderPass.h)
		/////////////////////////////////////////////////////////////////////////////////

		struct RenderPassState
		{
			glm::mat4 ViewProjectionMatrix{ 1.0f };
			glm::vec4 ViewportBounds{ 0.0f, 0.0f, 1280.0f, 720.0f }; // x, y, width, height
		};

	private:
		static void FlushAndReset();
	};

} // namespace Cosmic