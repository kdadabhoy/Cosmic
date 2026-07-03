// FlyCameraController.cpp
// Last Modified: 7/3/2026

#include "camera/FlyCameraController.h"
#include "core/Input.h"
#include "codes/MouseButtonCodes.h"
#include "codes/KeyCodes.h"

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	namespace
	{
		constexpr float kMaxPitchDeg = 89.0f;   // shy of the pole (LookAt up stays valid)
		const glm::vec3 kWorldUp     = { 0.0f, 1.0f, 0.0f };
	}

	/////////////////////////////////////////////////////////////////////////////////

	FlyCameraController::FlyCameraController(float aspectRatio)
		: m_Camera(45.0f, aspectRatio, 0.1f, 5000.0f)
	{
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void FlyCameraController::OnUpdate(float ts)
	{
		// SCREEN pixels — the same space as the viewport rect (drag deltas are
		// space-invariant, but absolute positions must line up).
		const glm::vec2 mouse = Input::GetMouseScreenPosition();

		if (m_ControlEnabled)
		{
			// --- Mouse-look while RMB is held ---------------------------------------
			const bool look = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
			if (look)
			{
				if (m_Looking)
				{
					// FPS convention: cursor right yaws right, cursor up pitches up.
					const glm::vec2 delta = mouse - m_LastMousePos;
					m_YawDeg   += delta.x * m_LookSpeed;
					m_PitchDeg -= delta.y * m_LookSpeed;
					m_PitchDeg  = std::clamp(m_PitchDeg, -kMaxPitchDeg, kMaxPitchDeg);
				}
				// First frame of the look: latch (m_LastMousePos below) with no delta
				// so the view doesn't jump by however far the cursor moved beforehand.
				m_Looking = true;
			}
			else
			{
				m_Looking = false;
			}

			// --- Movement wish velocity (world space) -------------------------------
			const glm::vec3 forward = DirectionFromYawPitch(m_YawDeg, m_PitchDeg);
			const glm::vec3 right   = glm::normalize(glm::cross(forward, kWorldUp));

			const bool  boost = Input::IsKeyPressed(CS_KEY_LEFT_SHIFT);
			const float speed = m_MoveSpeed * (boost ? m_BoostMultiplier : 1.0f);

			const glm::vec3 wish = ComputeWishVelocity(
				Input::IsKeyPressed(CS_KEY_W),
				Input::IsKeyPressed(CS_KEY_S),
				Input::IsKeyPressed(CS_KEY_A),
				Input::IsKeyPressed(CS_KEY_D),
				Input::IsKeyPressed(CS_KEY_E) || Input::IsKeyPressed(CS_KEY_SPACE),
				Input::IsKeyPressed(CS_KEY_Q) || Input::IsKeyPressed(CS_KEY_LEFT_CONTROL),
				forward, right, speed);

			// --- Integrate + optional above-ground clamp ----------------------------
			const Motion advanced = IntegrateMotion({ m_Position, m_Velocity }, wish, m_Smoothing, ts);
			m_Position = advanced.Position;
			m_Velocity = advanced.Velocity;

			if (m_GroundProbe)
				m_Position = ClampAboveGround(m_Position,
				                              m_GroundProbe(m_Position.x, m_Position.z),
				                              m_GroundClearance);
		}
		else
		{
			m_Looking   = false;
			m_Velocity *= std::exp(-ts * 12.0f);   // coast to rest while control is off
		}

		m_LastMousePos = mouse;
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void FlyCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) { return OnMouseScrolled(event); });
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) { return OnWindowResized(event); });
	}

	void FlyCameraController::OnResize(float width, float height)
	{
		m_Camera.SetViewportSize(width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void FlyCameraController::SetPose(const glm::vec3& position, float yawDeg, float pitchDeg)
	{
		m_Position = position;
		m_YawDeg   = yawDeg;
		m_PitchDeg = std::clamp(pitchDeg, -kMaxPitchDeg, kMaxPitchDeg);
		m_Velocity = glm::vec3(0.0f);   // a fresh pose carries no residual motion
		RecalculateCamera();
	}

	void FlyCameraController::SetMoveSpeed(float metersPerSec)
	{
		m_MoveSpeed = std::clamp(metersPerSec, 0.5f, 500.0f);
	}

	void FlyCameraController::SetControlEnabled(bool enabled)
	{
		m_ControlEnabled = enabled;
		if (!enabled)
			m_Looking = false;   // disabling mid-look ends the look (Orbit contract)
	}

	void FlyCameraController::SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx)
	{
		m_ViewportPos  = posPx;
		m_ViewportSize = sizePx;
		if (sizePx.x > 0.0f && sizePx.y > 0.0f)
			m_Camera.SetViewportSize(sizePx.x, sizePx.y);
	}

	void FlyCameraController::SetGroundProbe(GroundProbe probe, float clearance)
	{
		m_GroundProbe     = std::move(probe);
		m_GroundClearance = clearance;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Pure movement helpers
	/////////////////////////////////////////////////////////////////////////////////

	glm::vec3 FlyCameraController::DirectionFromYawPitch(float yawDeg, float pitchDeg)
	{
		// Yaw 0 / pitch 0 -> (0, 0, -1). Yaw increases -> turn toward +X (right);
		// pitch increases -> look up (+Y). Matches the mouse-look sign conventions.
		const float yaw   = glm::radians(yawDeg);
		const float pitch = glm::radians(pitchDeg);
		const float cp    = std::cos(pitch);
		return glm::vec3(std::sin(yaw) * cp,
		                 std::sin(pitch),
		                 -std::cos(yaw) * cp);
	}

	glm::vec3 FlyCameraController::ComputeWishVelocity(bool forward, bool back, bool left, bool right,
	                                                   bool up, bool down,
	                                                   const glm::vec3& forwardDir, const glm::vec3& rightDir,
	                                                   float speed)
	{
		glm::vec3 wish(0.0f);
		if (forward) wish += forwardDir;
		if (back)    wish -= forwardDir;
		if (right)   wish += rightDir;
		if (left)    wish -= rightDir;
		if (up)      wish += kWorldUp;
		if (down)    wish -= kWorldUp;

		if (glm::dot(wish, wish) > 1e-8f)
			return glm::normalize(wish) * speed;
		return glm::vec3(0.0f);
	}

	FlyCameraController::Motion FlyCameraController::IntegrateMotion(const Motion& state,
	                                                                const glm::vec3& wishVelocity,
	                                                                float smoothingPerSec, float ts)
	{
		Motion out = state;

		// Exponential approach: v += (wish - v) * (1 - exp(-k*ts)). k <= 0 -> raw.
		const float blend = (smoothingPerSec > 0.0f) ? (1.0f - std::exp(-smoothingPerSec * ts)) : 1.0f;
		out.Velocity += (wishVelocity - out.Velocity) * blend;
		out.Position += out.Velocity * ts;
		return out;
	}

	glm::vec3 FlyCameraController::ClampAboveGround(const glm::vec3& pos, float groundY, float clearance)
	{
		glm::vec3 clamped = pos;
		clamped.y = std::max(clamped.y, groundY + clearance);
		return clamped;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void FlyCameraController::RecalculateCamera()
	{
		const glm::vec3 forward = DirectionFromYawPitch(m_YawDeg, m_PitchDeg);
		m_Camera.LookAt(m_Position, m_Position + forward, kWorldUp);
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool FlyCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		if (!m_ControlEnabled)
			return false;

		// Exponential: each notch scales the base move speed (uniform feel at any speed).
		SetMoveSpeed(m_MoveSpeed * std::pow(1.15f, e.GetYOffset()));
		return false;   // never consume — see the event consumption contract
	}

	bool FlyCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		if (e.GetHeight() > 0)
			OnResize(static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()));

		return false;   // never consume — resize must reach framebuffers/viewports
	}

	/////////////////////////////////////////////////////////////////////////////////
}
