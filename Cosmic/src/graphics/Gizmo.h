#pragma once

// Gizmo.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Gizmo (transform manipulators, S5.5)
 * ============================================================================
 *
 * A thin engine wrapper over vendored ImGuizmo (MIT, ImGui-native — it matches
 * our stack; hand-rolling gizmo math is weeks of work for no gain). Exposes only
 * engine enums so no third-party type leaks into a public header (§0 rule 2).
 *
 * FRAME PROTOCOL (per frame, inside the ImGui frame):
 *   1. Gizmo::BeginFrame();                       // after ImGui::NewFrame
 *   2. Gizmo::SetRect(vpX, vpY, vpW, vpH);        // the viewport panel rect (screen px)
 *   3. if (selected) Gizmo::Manipulate(cam, xform, op, space, snap);
 *   4. camera controller should yield while Gizmo::IsUsing() so a drag on the
 *      gizmo doesn't also orbit the camera.
 *
 * ImGuizmo draws into the foreground draw list within the rect, so the gizmo
 * renders on top of the viewport image regardless of which ImGui window is
 * active when Manipulate is called.
 *
 * The TransformComponent overload writes rotation as a QUATERNION (and sets
 * UseQuatRotation) — the component's Euler/quat representations are independent
 * by design (see Components.h), so this is the unambiguous choice for a 3D gizmo.
 * Undo/redo is deferred to the S14 editor work (documented, not wired here).
 * ============================================================================
 */

#include "core/Core.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class Camera;
	struct TransformComponent;

	class COSMIC_API Gizmo
	{
	public:
		enum class Operation { Translate, Rotate, Scale };
		enum class Space     { Local, World };

		/** @brief Reset ImGuizmo's per-frame state. Call once, after ImGui::NewFrame. */
		static void BeginFrame();

		/** @brief The screen-space rect (window px) the gizmo draws + hit-tests within. */
		static void SetRect(float x, float y, float width, float height);

		/** @brief Master enable (grey out the gizmo without removing it). */
		static void SetEnabled(bool enabled);

		/** @brief True while the gizmo is being dragged (camera should yield). */
		static bool IsUsing();
		/** @brief True while the cursor is over any gizmo handle. */
		static bool IsOver();

		/**
		 * @brief Manipulate a raw model matrix in place. Returns true if it changed
		 * this frame. `snap` > 0 snaps to that increment (world units for
		 * translate/scale, degrees for rotate); 0 disables snapping.
		 */
		static bool Manipulate(const Camera& camera, glm::mat4& model,
		                       Operation op, Space space, float snap = 0.0f);

		/**
		 * @brief Manipulate a TransformComponent in place (decomposes the result back
		 * into Position/Scale/RotationQuat; sets UseQuatRotation). Returns true if the
		 * transform changed this frame.
		 */
		static bool Manipulate(const Camera& camera, TransformComponent& transform,
		                       Operation op, Space space, float snap = 0.0f);
	};
}
