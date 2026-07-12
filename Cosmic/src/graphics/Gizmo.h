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
 *
 *   auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
 *   if (ws->BeginViewportOverlay())                        // append to the Viewport window
 *   {
 *       Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);   // ImGui screen px
 *       if (selected) Gizmo::Manipulate(cam, xform, op, space, snap);
 *   }
 *   ws->EndViewportOverlay();                              // always pair with Begin
 *
 * Manipulate MUST be called between ImGui::Begin/End of the window that shows
 * the rendered scene (BeginViewportOverlay does that for the engine viewport):
 * it draws into — and, critically, hit-tests hover against — the CURRENT ImGui
 * window. Calling it outside a window (e.g. with a foreground draw list) draws
 * a gizmo that can never be grabbed, because ImGuizmo treats the mouse as
 * "over some other window" whenever the viewport is hovered.
 *
 * The engine resets ImGuizmo once per frame in ImGuiLayer::Begin — clients have
 * no per-frame bookkeeping.
 *
 * Input etiquette: the camera controller should yield while IsUsing() (an
 * active drag) or IsOver() (cursor on a handle) so grabbing the gizmo doesn't
 * also orbit the camera, and click-to-select should skip clicks on handles.
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
		// Universal (K11) combines translate arrows + rotate rings + universal
		// scale handles in ONE gizmo (ImGuizmo TRANSLATE|ROTATE|SCALEU). ImGuizmo
		// takes a single snap value per call, so under Universal the caller's
		// snap applies to the MOVE handles (rotate/scale drag unsnapped) — pass
		// the move increment there.
		enum class Operation { Translate, Rotate, Scale, Universal };
		enum class Space     { Local, World };

		/** @brief The screen-space rect (ImGui screen px) the gizmo draws + hit-tests within. */
		static void SetRect(float x, float y, float width, float height);

		/** @brief Master enable (grey out the gizmo without removing it). */
		static void SetEnabled(bool enabled);

		/** @brief True while the gizmo is being dragged (camera should yield). */
		static bool IsUsing();
		/** @brief True while the cursor is over any gizmo handle (selection should skip). */
		static bool IsOver();

		/**
		 * @brief Manipulate a raw model matrix in place. Returns true if it changed
		 * this frame. `snap` > 0 snaps to that increment (world units for
		 * translate/scale, degrees for rotate); 0 disables snapping.
		 * Pre: called between Begin/End of the viewport window (see FRAME PROTOCOL).
		 * Orthographic vs perspective is detected from the camera's projection.
		 */
		static bool Manipulate(const Camera& camera, glm::mat4& model,
		                       Operation op, Space space, float snap = 0.0f);

		/**
		 * @brief Manipulate a TransformComponent in place (decomposes the result back
		 * into Position/Scale/RotationQuat; sets UseQuatRotation). Returns true if the
		 * transform changed this frame.
		 * Pre: called between Begin/End of the viewport window (see FRAME PROTOCOL).
		 */
		static bool Manipulate(const Camera& camera, TransformComponent& transform,
		                       Operation op, Space space, float snap = 0.0f);
	};
}
