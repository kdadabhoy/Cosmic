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

namespace Cosmic
{
	class COSMIC_API OrbitCameraController
	{
	public:
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

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Infrastructure Handlers & Math
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Rebuilds the camera pose from (target, yaw, pitch, distance).
		 */
		void RecalculateCamera();

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
		// The Camera Itself
		/////////////////////////////////////////////////////////////////////////////////

		PerspectiveCamera m_Camera;
	};
}
