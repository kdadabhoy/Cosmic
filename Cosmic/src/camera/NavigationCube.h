#pragma once

// NavigationCube.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — NavigationCube (SolidWorks-style orientation cube, S5.3)
 * ============================================================================
 *
 * A small orientation widget: it renders a cube into its OWN framebuffer,
 * oriented to match a camera's current view, and turns a click on a face into
 * the matching ViewPreset so the app can SnapView to it (S5.2). As the main
 * camera orbits, the cube's tripod + shading rotate with it — the "which way
 * am I looking?" readout every CAD viewport has.
 *
 * SELF-CONTAINED: owns its FBO + cube mesh and renders in its own
 * Renderer3D::BeginScene/EndScene pass. Call Render() OUTSIDE the main scene's
 * Begin/End (mirrors the FPV-inset / pick pre-pass pattern) — it leaves its FBO
 * unbound afterward, so the caller restores its own render target + viewport.
 *
 * USAGE (in a layer):
 *   m_NavCube = NavigationCube::Create(140);
 *   ...
 *   m_NavCube->Render(m_Orbit.GetCamera().GetViewMatrix());   // pre-pass
 *   // then, in ImGui, show it and route clicks:
 *   ImGui::Image(m_NavCube->GetTextureID(), {size, size}, {0,1}, {1,0});
 *   if (clicked) { ViewPreset p; if (m_NavCube->PickFace(u, v, p)) m_Orbit.SnapView(p); }
 *
 * The projection is orthographic so the cube reads without perspective skew,
 * and PickFace reuses the exact view-projection of the last Render so screen
 * picks and pixels always agree.
 * ============================================================================
 */

#include "core/Core.h"
#include "camera/OrbitCameraController.h"   // ViewPreset
#include "graphics/FrameBuffer.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API NavigationCube
	{
	public:
		/** @brief Factory (unified Ref creation). pixelSize is the square FBO edge. */
		static Ref<NavigationCube> Create(uint32_t pixelSize = 140);

		explicit NavigationCube(uint32_t pixelSize);
		~NavigationCube() = default;

		/**
		 * @brief Render the cube oriented by `cameraView` (only its ROTATION is used)
		 * into the internal FBO. Restores nothing but its own unbind — the caller
		 * re-binds its render target + viewport afterward.
		 */
		void Render(const glm::mat4& cameraView);

		/** @brief FBO color texture handle for ImGui::Image (flip V: {0,1},{1,0}). */
		uint32_t GetTextureID() const;
		uint32_t GetSize() const { return m_Size; }

		/**
		 * @brief Hit-test a click on the cube. (u, v) are panel coordinates — u to
		 * the right, v DOWN from the top-left, both in [0, 1]. Returns the clicked
		 * face's ViewPreset, or false on a background miss. Uses the view-projection
		 * of the most recent Render().
		 */
		bool PickFace(float u, float v, ViewPreset& outPreset) const;

		/**
		 * @brief Pure ray/face test used by PickFace, exposed static so it can be
		 * unit-tested without a GL context: unproject (u, v) through `viewProjection`,
		 * intersect the unit cube [-0.5, 0.5]³, and return the entry face's ViewPreset.
		 */
		static bool PickFaceFromViewProjection(const glm::mat4& viewProjection,
		                                       float u, float v, ViewPreset& outPreset);

	private:
		uint32_t         m_Size = 140;
		Ref<FrameBuffer> m_Fbo;
		Ref<Mesh>        m_Box;                 // unit cube (±0.5)
		glm::mat4        m_Projection{ 1.0f };  // orthographic; fixed
		glm::mat4        m_LastView{ 1.0f };    // rotation-only view from the last Render
		float            m_ViewDistance = 2.0f;
	};
}
