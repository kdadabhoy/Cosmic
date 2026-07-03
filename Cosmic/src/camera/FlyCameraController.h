#pragma once

// FlyCameraController.h
// Last Modified: 7/3/2026

/**
 * @class FlyCameraController
 * @brief Free-flight "exploration camera" for a PerspectiveCamera — the WASD +
 *        mouse-look rig that drives the Frontier showcase (Phase 11, F1).
 *
 * Where OrbitCameraController rides a spherical mount around a focus point (the
 * "editor camera"), this rig flies the camera itself:
 *   - RMB held    -> mouse-look (yaw/pitch from the per-frame cursor delta)
 *   - W/S         -> move along the full look vector (forward/back)
 *   - A/D         -> strafe along the camera right axis
 *   - E / Space   -> ascend (world +Y);  Q / LCtrl -> descend (world -Y)
 *   - LShift      -> speed boost while held
 *   - Scroll      -> change the base move speed (exponential, ×1.15 per notch)
 *
 * Mirrors OrbitCameraController's architecture: input POLLING happens in
 * OnUpdate (mouse deltas are derived from the position each frame), while
 * discrete events (scroll, window resize) arrive via OnEvent and return false so
 * clients can still observe them. Screen-pixel mouse space
 * (Input::GetMouseScreenPosition) — the same space as the viewport rect.
 *
 * Frame convention: render frame, right-handed, Y-up (see math/Spatial.h). Yaw 0
 * looks down -Z (Orbit convention); positive pitch looks up. Pitch is clamped
 * shy of ±90° so the LookAt up vector never degenerates.
 *
 * The movement math is factored into pure static helpers (DirectionFromYawPitch,
 * ComputeWishVelocity, IntegrateMotion, ClampAboveGround) so it is unit-testable
 * without a live Application/Input backend (headless tests, no GL).
 */

#include "core/Core.h"
#include "camera/PerspectiveCamera.h"
#include "events/ApplicationEvent.h"
#include "events/MouseEvent.h"
#include <glm/glm.hpp>
#include <functional>

namespace Cosmic
{
	class COSMIC_API FlyCameraController
	{
	public:
		/**
		 * @brief Ground-height probe for the optional above-ground clamp. The app
		 * supplies it (typically Terrain::SampleHeight); returns the world-space
		 * ground Y at (x, z). When set, the camera position is kept at least
		 * `clearance` metres above the returned height each frame. Null = free
		 * flight (no clamp).
		 */
		using GroundProbe = std::function<float(float x, float z)>;

		/** Pure position/velocity pair advanced by IntegrateMotion (headless-testable). */
		struct Motion
		{
			glm::vec3 Position{ 0.0f };
			glm::vec3 Velocity{ 0.0f };
		};

		/////////////////////////////////////////////////////////////////////////////////
		// Lifecycle & Main Execution Cascade
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Constructor
		 * Pre:  Initial width-to-height aspect ratio is provided.
		 * Post: The camera is placed at the default pose looking down -Z.
		 */
		FlyCameraController(float aspectRatio);
		~FlyCameraController() = default;

		/**
		 * @brief Per-frame tick. Polls RMB mouse-look and the movement keys, blends
		 *        the velocity, integrates the position, and applies the ground clamp.
		 * Pre:  ts is a frame-scaled delta time in seconds.
		 */
		void OnUpdate(float ts);

		/** @brief Dispatches scroll (speed) + resize events to internal handlers. */
		void OnEvent(Event& e);

		/** @brief Re-syncs the projection aspect with a resized viewport/framebuffer. */
		void OnResize(float width, float height);

		/////////////////////////////////////////////////////////////////////////////////
		// Camera Access
		/////////////////////////////////////////////////////////////////////////////////

		PerspectiveCamera&       GetCamera()       { return m_Camera; }
		const PerspectiveCamera& GetCamera() const { return m_Camera; }

		/////////////////////////////////////////////////////////////////////////////////
		// Pose (position / yaw / pitch)
		/////////////////////////////////////////////////////////////////////////////////

		// Hard-set the pose. Yaw is free; pitch clamps to ±89°. Zeroes residual velocity.
		void      SetPose(const glm::vec3& position, float yawDeg, float pitchDeg);
		glm::vec3 GetPosition() const { return m_Position; }
		float     GetYaw() const      { return m_YawDeg; }    // degrees; yaw 0 looks -Z
		float     GetPitch() const    { return m_PitchDeg; }  // clamped to ±89°

		/////////////////////////////////////////////////////////////////////////////////
		// Behaviour Tuning
		/////////////////////////////////////////////////////////////////////////////////

		void  SetMoveSpeed(float metersPerSec);                 // clamped to [0.5, 500]
		float GetMoveSpeed() const           { return m_MoveSpeed; }
		void  SetBoostMultiplier(float x)    { m_BoostMultiplier = x; }   // LShift (default 4.0)
		void  SetSmoothing(float perSec)     { m_Smoothing = perSec; }    // 0 = raw velocity
		void  SetLookSpeed(float degPerPixel){ m_LookSpeed = degPerPixel; }

		// Master enable for keyboard/mouse control (apps gate on viewport hover, the
		// Orbit pattern). Disabling ends any in-progress mouse-look.
		void  SetControlEnabled(bool enabled);
		bool  IsControlEnabled() const       { return m_ControlEnabled; }

		// The viewport rectangle in SCREEN pixels (WorkspaceLayer::GetViewportPos /
		// GetViewportSize). Stored for future cursor math; also updates the projection
		// aspect (superset of OnResize).
		void  SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);

		// True while the RMB mouse-look drag is active. Lets apps keep control enabled
		// for a look that started inside the viewport even after the cursor leaves it.
		bool  IsLooking() const              { return m_Looking; }

		// Optional ground clamp. clearance = metres kept above the probed height.
		// Passing a null probe disables the clamp (free flight).
		void  SetGroundProbe(GroundProbe probe, float clearance = 1.5f);

		/////////////////////////////////////////////////////////////////////////////////
		// Pure movement helpers (headless-testable — no Input/GL dependency)
		/////////////////////////////////////////////////////////////////////////////////

		// Unit look direction for a (yaw, pitch) in degrees. Yaw 0 / pitch 0 -> -Z.
		static glm::vec3 DirectionFromYawPitch(float yawDeg, float pitchDeg);

		// World-space wish velocity from the movement key states + camera basis. The
		// combined direction is normalised then scaled by `speed` (0 when idle).
		static glm::vec3 ComputeWishVelocity(bool forward, bool back, bool left, bool right,
		                                     bool up, bool down,
		                                     const glm::vec3& forwardDir, const glm::vec3& rightDir,
		                                     float speed);

		// Advance a Motion one step: exponential velocity smoothing toward the wish
		// velocity (`smoothingPerSec <= 0` = raw, snap to wish), then Euler position
		// integration. No ground clamp (see ClampAboveGround).
		static Motion IntegrateMotion(const Motion& state, const glm::vec3& wishVelocity,
		                              float smoothingPerSec, float ts);

		// Raise pos.y to at least groundY + clearance (leaves x/z untouched).
		static glm::vec3 ClampAboveGround(const glm::vec3& pos, float groundY, float clearance);

	private:
		/** @brief Rebuilds the camera transform from (position, yaw, pitch). */
		void RecalculateCamera();

		/** @note Consumption contract: returns false so scroll still propagates. */
		bool OnMouseScrolled(MouseScrolledEvent& e);
		/** @note Consumption contract: returns false so resize reaches framebuffers. */
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Pose State
		/////////////////////////////////////////////////////////////////////////////////

		glm::vec3 m_Position = { 0.0f, 20.0f, 40.0f };
		float     m_YawDeg   = 0.0f;
		float     m_PitchDeg = -15.0f;
		glm::vec3 m_Velocity = { 0.0f, 0.0f, 0.0f };

		/////////////////////////////////////////////////////////////////////////////////
		// Behaviour Coefficients
		/////////////////////////////////////////////////////////////////////////////////

		float m_MoveSpeed       = 25.0f;   // metres / second (scroll adjusts)
		float m_BoostMultiplier = 4.0f;    // LShift factor
		float m_Smoothing       = 12.0f;   // exp. velocity smoothing per second
		float m_LookSpeed       = 0.15f;   // degrees per pixel of RMB drag

		/////////////////////////////////////////////////////////////////////////////////
		// Input / Control State
		/////////////////////////////////////////////////////////////////////////////////

		bool      m_ControlEnabled = true;
		bool      m_Looking        = false;   // RMB currently held
		glm::vec2 m_LastMousePos   = { 0.0f, 0.0f };
		glm::vec2 m_ViewportPos    = { 0.0f, 0.0f };
		glm::vec2 m_ViewportSize   = { 0.0f, 0.0f };

		/////////////////////////////////////////////////////////////////////////////////
		// Ground Clamp
		/////////////////////////////////////////////////////////////////////////////////

		GroundProbe m_GroundProbe;
		float       m_GroundClearance = 1.5f;

		/////////////////////////////////////////////////////////////////////////////////
		// The Camera Itself
		/////////////////////////////////////////////////////////////////////////////////

		PerspectiveCamera m_Camera;
	};
}
