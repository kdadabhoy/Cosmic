#pragma once

// ScenePicker.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — ScenePicker (entity-ID 3D picking, S5.4)
 * ============================================================================
 *
 * Turns a mouse position over a 3D viewport into the Entity under the cursor,
 * using the entity-ID MRT attachment (S4.6) that Scene::OnRender3D already
 * writes. Owns a private {RGBA8, RED_INTEGER, DEPTH} framebuffer sized to the
 * viewport; RenderIdPass draws the scene's IDs into it (its own Renderer3D
 * pass), and Pick reads one texel back.
 *
 * The read-back is a synchronous glReadPixels (a small pipeline stall) — ideal
 * for click-to-select. For per-frame HOVER picking on large scenes, move the
 * read to an async PBO round-robin (noted for a future pass; click picking does
 * not need it).
 *
 * Feeds the existing selection bus: callers pass the returned Entity to
 * EntitySelection::Set so 2D panels and 3D tools share one selection.
 *
 * SELF-CONTAINED: RenderIdPass renders in its own BeginScene/EndScene and
 * leaves its FBO unbound — the caller restores its own render target + viewport
 * (mirrors the FPV-inset / pick pre-pass pattern).
 * ============================================================================
 */

#include "core/Core.h"
#include "scene/Entity.h"
#include "graphics/FrameBuffer.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Cosmic
{
	class Camera;
	class Scene;

	class COSMIC_API ScenePicker
	{
	public:
		static Ref<ScenePicker> Create();

		ScenePicker();
		~ScenePicker() = default;

		/**
		 * @brief Render `scene`'s entity IDs (and color, harmlessly) into the internal
		 * MRT FBO at (width, height). Resizes the FBO on demand. Clears the ID
		 * attachment to -1 first so empty space reads as "no entity".
		 *
		 * `only` (K12 — the selection-outline mask): when non-null, ONLY those
		 * entities render (MeshRenderer / LODGroup levels / voxel chunks — the same
		 * shapes the full pass draws), so the ID attachment becomes a selection
		 * mask (-1 outside, entity ids inside). Null = the historical full pass.
		 */
		void RenderIdPass(Scene& scene, const Camera& camera, uint32_t width, uint32_t height,
		                  const std::vector<entt::entity>* only = nullptr);

		/**
		 * @brief The Entity under a viewport pixel — x from the LEFT, y from the TOP
		 * (the natural ImGui/mouse convention; the GL bottom-left flip is handled
		 * internally). Returns an invalid Entity (operator bool == false) on a miss,
		 * out-of-range coordinates, or a stale/destroyed id. Call after RenderIdPass.
		 */
		Entity Pick(Scene& scene, int xFromLeft, int yFromTop) const;

		/**
		 * @brief Reconstruct the world-space point under a viewport pixel (x from the
		 * LEFT, y from the TOP) from the depth written in the last RenderIdPass.
		 * Returns false where nothing was drawn (far plane). `camera` must be the one
		 * RenderIdPass used. Supports orbit-about-surface navigation (S5.1) — feed it
		 * to OrbitCameraController::SetPivotProbe.
		 */
		bool WorldPoint(const Camera& camera, int xFromLeft, int yFromTop, glm::vec3& out) const;

		/** @brief Color attachment handle (for an optional debug ImGui::Image view). */
		uint32_t GetColorTextureID() const;
		/** @brief RED_INTEGER id attachment handle (K12 — the outline pass samples it). */
		uint32_t GetIdTextureID() const;
		uint32_t GetWidth() const;
		uint32_t GetHeight() const;

	private:
		Ref<FrameBuffer> m_Fbo;   // {RGBA8, RED_INTEGER, DEPTH24STENCIL8}
	};
}
