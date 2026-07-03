#pragma once

// OrbitCameraController.h
// Last Modified: 7/1/2026

/**
 * @class OrbitCameraController
 * @brief Mouse-driven orbit rig for a PerspectiveCamera — the "editor camera"
 *        of the 3D viewport.
 *
 * The camera rides a spherical mount around a focus target:
 *   - LMB drag  -> orbit (yaw around world +Y, pitch around the local right axis)
 *   - RMB drag  -> pan (translate the target in the camera's right/up plane)
 *   - Scroll    -> zoom (exponential distance change with smooth blending)
 *
 * Mirrors OrthographicCameraController's architecture: input POLLING happens in
 * OnUpdate (mouse deltas are derived from the position each frame), while
 * discrete events (scroll, window resize) arrive via OnEvent. Event handlers
 * return false so clients can observe the same events (see the consumption
 * contract on OrthographicCameraController).
 *
 * Frame convention: render frame, right-handed, Y-up (see math/Spatial.h).
 * Yaw 0 / pitch 0 places the camera on the +Z side of the target looking -Z.
 * Pitch is clamped shy of the poles so the up vector never degenerates.
 */

#include "core/Core.h"
#include "camera/PerspectiveCamera.h"
#include "events/ApplicationEvent.h"
#include "events/MouseEvent.h"
#include <glm/glm.hpp>
#include <functional>

namespace Cosmic
{
	/**
	 * @brief Mouse-binding scheme for the orbit rig.
	 *
	 * Classic (default, unchanged): LMB orbit / RMB pan / scroll zoom-to-center.
	 * CAD (S5.1, SolidWorks feel): MMB orbit / Ctrl+MMB pan / Shift+MMB dolly /
	 * scroll zoom-toward-cursor, with LMB left free for selection. In CAD mode
	 * orbit pivots about the point under the cursor (via the pivot probe, falling
	 * back to a ray/target-plane hit) — the single behavior that most makes a
	 * viewport feel like SolidWorks.
	 */
	enum class NavStyle { Classic, CAD };

	/** @brief Standard snap views for SnapView (S5.2). Iso is the default 3/4 view. */
	enum class ViewPreset { Front, Back, Left, Right, Top, Bottom, Iso };

	class COSMIC_API OrbitCameraController
	{
	public:
		/**
		 * @brief Probe for the world-space point under the cursor, used by CAD-style
		 * orbit-about-cursor and zoom-to-cursor. The app supplies it (typically a
		 * FrameBuffer depth read + unproject); return false on a miss (empty space).
		 * When no probe is set the controller falls back to intersecting the cursor
		 * ray with the plane through the current target — so CAD nav needs only S1.
		 *   @param windowMouse  cursor position in WINDOW pixels (Input::GetMousePosition)
		 *   @param outWorld     receives the hit point on success
		 */
		using PivotProbe = std::function<bool(const glm::vec2& windowMouse, glm::vec3& outWorld)>;

		/////////////////////////////////////////////////////////////////////////////////
		// Lifecycle & Main Execution Cascade
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Constructor
		 * Pre: Initial width-to-height aspect ratio is provided.
		 * Post: The camera is mounted at (yaw, pitch, distance) around the target.
		 */
		OrbitCameraController(float aspectRatio);
		~OrbitCameraController() = default;

		/**
		 * @brief Per-frame tick. Polls mouse state for orbit/pan drags and blends zoom.
		 * Pre: ts is a frame-scaled delta time in seconds.
		 */
		void OnUpdate(float ts);

		/**
		 * @brief Dispatches scroll + resize events to internal handlers.
		 */
		void OnEvent(Event& e);

		/////////////////////////////////////////////////////////////////////////////////
		// Viewport & Aspect Ratio Configuration
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Re-syncs the projection aspect with a resized viewport/framebuffer.
		 */
		void OnResize(float width, float height);

		/////////////////////////////////////////////////////////////////////////////////
		// Camera Access
		/////////////////////////////////////////////////////////////////////////////////

		PerspectiveCamera&       GetCamera()       { return m_Camera; }
		const PerspectiveCamera& GetCamera() const { return m_Camera; }

		/////////////////////////////////////////////////////////////////////////////////
		// Orbit State (target / distance / angles)
		/////////////////////////////////////////////////////////////////////////////////

		void             SetTarget(const glm::vec3& target)	{ m_Target = target; RecalculateCamera(); }
		const glm::vec3& GetTarget() const						{ return m_Target; }

		// Hard-snaps the distance (no blend). Use scroll/SetTargetDistance for smooth zoom.
		void  SetDistance(float distance);
		float GetDistance() const								{ return m_Distance; }

		// Smoothly blends toward the given distance over the next frames.
		void  SetTargetDistance(float distance);

		// Angles in DEGREES. Yaw wraps freely; pitch clamps to the configured limits.
		void  SetYawPitch(float yawDeg, float pitchDeg);
		float GetYaw() const									{ return m_YawDeg; }
		float GetPitch() const									{ return m_PitchDeg; }

		/////////////////////////////////////////////////////////////////////////////////
		// Behavior Tuning
		/////////////////////////////////////////////////////////////////////////////////

		void  SetDistanceLimits(float minDist, float maxDist)	{ m_MinDistance = minDist; m_MaxDistance = maxDist; }
		void  SetPitchLimits(float minDeg, float maxDeg)		{ m_MinPitchDeg = minDeg; m_MaxPitchDeg = maxDeg; }

		void  SetOrbitSpeed(float degPerPixel)					{ m_OrbitSpeed = degPerPixel; }
		void  SetPanSpeed(float scale)							{ m_PanSpeed = scale; }
		void  SetZoomSpeed(float scale)							{ m_ZoomSpeed = scale; }

		// Master enable for mouse control (e.g. disable while another gizmo drags).
		void  SetControlEnabled(bool enabled)					{ m_ControlEnabled = enabled; }
		bool  IsControlEnabled() const							{ return m_ControlEnabled; }

		/////////////////////////////////////////////////////////////////////////////////
		// CAD Navigation (S5.1)
		/////////////////////////////////////////////////////////////////////////////////

		// Switch binding schemes at runtime. Existing apps stay Classic unless they
		// opt in — no behavior change for anyone who never calls this.
		void       SetNavigationStyle(NavStyle style)			{ m_NavStyle = style; }
		NavStyle   GetNavigationStyle() const					{ return m_NavStyle; }

		// Cursor-pivot probe for orbit-about-cursor / zoom-to-cursor (see PivotProbe).
		void       SetPivotProbe(PivotProbe probe)				{ m_PivotProbe = std::move(probe); }

		// Optional exponential damping on orbit/pan velocities (off by default —
		// existing feel is unchanged). Purely cosmetic; does not alter end poses.
		void       SetInertiaEnabled(bool enabled)				{ m_InertiaEnabled = enabled; }
		bool       IsInertiaEnabled() const						{ return m_InertiaEnabled; }

		// The viewport rectangle in WINDOW pixels (content-area origin + size).
		// Required for zoom-to-cursor and the ray/target-plane pivot fallback; also
		// updates the projection aspect (superset of OnResize). Apps that never use
		// CAD nav can keep calling OnResize instead.
		void       SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);

		/////////////////////////////////////////////////////////////////////////////////
		// Frame & Snap Views (S5.2)
		/////////////////////////////////////////////////////////////////////////////////

		// Snap to a standard orientation about the current target, keeping the current
		// distance. Animated by default (blends over a few frames); pass animate=false
		// for an instant cut.
		void       SnapView(ViewPreset preset, bool animate = true);

		// Frame a world-space AABB so it fills ~70% of the viewport height at any
		// aspect: recenters the target on the box and dollies to fit, keeping the
		// current yaw/pitch. Degenerate boxes are ignored.
		void       FrameBounds(const glm::vec3& worldMin, const glm::vec3& worldMax, bool animate = true);

		// Frame a bounding sphere (center + radius) — the primitive FrameBounds builds on.
		void       FrameSphere(const glm::vec3& center, float radius, bool animate = true);

		// True while a SnapView/Frame pose blend is in progress (drag/scroll cancels it).
		bool       IsAnimating() const							{ return m_Animating; }

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Infrastructure Handlers & Math
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Rebuilds the camera pose from (target, yaw, pitch, distance).
		 */
		void RecalculateCamera();

		/////////////////////////////////////////////////////////////////////////////////
		// CAD Navigation & Framing Helpers (S5.1 / S5.2)
		/////////////////////////////////////////////////////////////////////////////////

		// The spherical mount offset for a pose (mirrors RecalculateCamera exactly).
		glm::vec3 PoseToOffset(float yawDeg, float pitchDeg, float dist) const;

		// Re-derive (target, yaw, pitch, distance) so the rig orbits about `pivot`
		// WITHOUT moving the camera — the essence of orbit-about-cursor.
		void ReanchorAround(const glm::vec3& pivot);

		// World point under the cursor: pivot probe if set + it hits, else the cursor
		// ray intersected with the plane through the target facing the camera. Returns
		// false only when even the fallback degenerates.
		bool ComputeCursorPivot(glm::vec3& outWorld) const;

		// Cursor ray in world space from the stored viewport rect + camera inverse-VP.
		// Returns false if the viewport size is unset/zero.
		bool CursorRay(glm::vec3& outOrigin, glm::vec3& outDir) const;

		// Start a smooth blend toward a target pose (SnapView / Frame). Cancelled by
		// any drag or scroll in OnUpdate.
		void BeginPoseAnimation(float yawDeg, float pitchDeg, float dist, const glm::vec3& target);

		/**
		 * @note Event Consumption Contract: returns false so clients can still
		 * observe scroll events (matches OrthographicCameraController).
		 */
		bool OnMouseScrolled(MouseScrolledEvent& e);

		/**
		 * @note Event Consumption Contract: returns false so resize propagation
		 * reaches framebuffers and other systems.
		 */
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Orbit Rig State
		/////////////////////////////////////////////////////////////////////////////////

		glm::vec3 m_Target          = { 0.0f, 0.0f, 0.0f };
		float     m_Distance        = 10.0f;
		float     m_TargetDistance  = 10.0f;   // zoom blends toward this
		float     m_YawDeg          = 45.0f;
		float     m_PitchDeg        = 30.0f;

		/////////////////////////////////////////////////////////////////////////////////
		// Constraint Constants
		/////////////////////////////////////////////////////////////////////////////////

		float m_MinDistance =   0.5f;
		float m_MaxDistance = 500.0f;
		float m_MinPitchDeg = -89.0f;
		float m_MaxPitchDeg =  89.0f;

		/////////////////////////////////////////////////////////////////////////////////
		// Input Response Coefficients
		/////////////////////////////////////////////////////////////////////////////////

		float m_OrbitSpeed = 0.25f;   // degrees per pixel of drag
		float m_PanSpeed   = 1.0f;    // scales with distance so panning feels constant
		float m_ZoomSpeed  = 1.0f;    // scroll multiplier

		/////////////////////////////////////////////////////////////////////////////////
		// Drag Tracking & Behavioral State
		/////////////////////////////////////////////////////////////////////////////////

		bool      m_ControlEnabled = true;
		bool      m_Dragging       = false;   // any mouse button drag in progress
		glm::vec2 m_LastMousePos   = { 0.0f, 0.0f };

		/////////////////////////////////////////////////////////////////////////////////
		// CAD Navigation State (S5.1)
		/////////////////////////////////////////////////////////////////////////////////

		NavStyle   m_NavStyle       = NavStyle::Classic;
		PivotProbe m_PivotProbe;                       // optional; null ⇒ ray/plane fallback
		bool       m_InertiaEnabled = false;
		glm::vec2  m_ViewportPos    = { 0.0f, 0.0f };  // content-area origin, window px
		glm::vec2  m_ViewportSize   = { 0.0f, 0.0f };  // panel size, window px

		// Which gesture the in-progress drag is performing (latched on the frame the
		// drag starts so mid-drag modifier changes don't switch modes).
		enum class DragMode { None, Orbit, Pan, Dolly };
		DragMode   m_DragMode       = DragMode::None;

		// Inertial damping velocities (deg/frame-ish); only used when m_InertiaEnabled.
		glm::vec2  m_OrbitVelocity  = { 0.0f, 0.0f };  // (yaw, pitch)

		/////////////////////////////////////////////////////////////////////////////////
		// Pose Animation State (S5.2 — SnapView / Frame blends)
		/////////////////////////////////////////////////////////////////////////////////

		bool      m_Animating       = false;
		float     m_AnimYawDeg      = 0.0f;
		float     m_AnimPitchDeg    = 0.0f;
		float     m_AnimDistance    = 0.0f;
		glm::vec3 m_AnimTarget      = { 0.0f, 0.0f, 0.0f };

		/////////////////////////////////////////////////////////////////////////////////
		// The Camera Itself
		/////////////////////////////////////////////////////////////////////////////////

		PerspectiveCamera m_Camera;
	};
}
