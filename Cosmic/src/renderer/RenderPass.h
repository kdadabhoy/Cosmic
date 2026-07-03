#pragma once

// RenderPass.h
// Last Modified 5/24/2026

/**
 * General Description:
 *
 * RenderPass provides an RAII (Resource Acquisition Is Initialization) scoping
 * mechanism for isolated camera render contexts within the Cosmic Engine.
 *
 * MOTIVATION:
 * The Renderer2D system uses a single internal ViewProjectionMatrix uniform for
 * all geometry submitted within a BeginScene/EndScene block. Without an explicit
 * scoping mechanism, two concurrent camera passes would overwrite each other's
 * matrix state, producing vertex distortion or silent state leakage.
 *
 * SOLUTION:
 * RenderPass wraps PushRenderPass / PopRenderPass into constructor / destructor
 * calls. On construction, any pending batched geometry from a prior pass is
 * flushed and the new camera matrix + viewport bounds are pushed onto an internal
 * stack inside Renderer2D. On destruction, the pass is popped and the previous
 * state is restored. This allows arbitrarily many cameras to render sequentially
 * or nested within a single frame without manual state tracking.
 *
 * USAGE PATTERNS:
 *
 * Pattern A — Simple sequential multi-camera (most common):
 *
 *   // First camera: main world view
 *   {
 *       Cosmic::RenderPass mainPass(m_MainCamera, { 0, 0, 1280, 720 });
 *       Renderer2D::DrawQuad(...);
 *   } // ← auto-flushes and restores on scope exit
 *
 *   // Second camera: minimap top-down
 *   {
 *       Cosmic::RenderPass minimapPass(m_OverviewCamera, { 900, 500, 380, 220 });
 *       Renderer2D::DrawQuad(...);
 *   }
 *
 * Pattern B — With explicit framebuffer targeting (advanced):
 *
 *   m_SideFramebuffer->Bind();
 *   {
 *       Cosmic::RenderPass sidePass(m_SideCamera, { 0, 0, w, h });
 *       Renderer2D::DrawQuad(...);
 *   }
 *   m_SideFramebuffer->Unbind();
 *
 * RULES:
 * - Do NOT nest two RenderPass instances targeting the same viewport bounds.
 *   Each should have its own bounds or framebuffer target.
 * - RenderPass is non-copyable and non-movable by design.
 * - The viewport bounds vec4 is { x_offset, y_offset, width, height } in pixels.
 *
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. RenderPass(const OrthographicCamera& camera, const glm::vec4& viewportBounds)
 *    Pre:  Renderer2D::Init() must have been called. The camera must be valid.
 *    Post: Any pending geometry is flushed. The camera's VP matrix and viewport
 *          bounds are pushed onto Renderer2D's internal render pass stack.
 *          The hardware viewport is updated to viewportBounds.
 *
 * 2. ~RenderPass()
 *    Pre:  A matching construction call exists.
 *    Post: Any geometry staged under this pass is flushed. The pass is popped
 *          from the stack. If a prior pass exists, its VP matrix and viewport
 *          are restored.
 */

#include "core/Core.h"
#include "renderer/Renderer2D.h"
#include "camera/Camera.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API RenderPass
	{
	public:
		/**
		 * @brief Constructs a scoped render pass targeting the given camera and viewport region.
		 *
		 * @param camera         Any camera whose VP matrix drives this pass (2D or 3D).
		 * @param viewportBounds Pixel-space viewport region { x, y, width, height }.
		 *                       Defaults to a full-screen pass if width/height are 0.
		 */
		RenderPass(const Camera& camera, const glm::vec4& viewportBounds)
		{
			Renderer2D::PushRenderPass(camera.GetViewProjectionMatrix(), viewportBounds);
		}

		/**
		 * @brief Destructor — flushes remaining geometry and restores prior pass state.
		 */
		~RenderPass()
		{
			Renderer2D::PopRenderPass();
		}

		// Non-copyable, non-movable — the stack entry must be owned by a single scope
		RenderPass(const RenderPass&) = delete;
		RenderPass& operator=(const RenderPass&) = delete;
		RenderPass(RenderPass&&) = delete;
		RenderPass& operator=(RenderPass&&) = delete;
	};
}