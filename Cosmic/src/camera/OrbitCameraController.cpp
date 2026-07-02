// OrbitCameraController.cpp
// Last Modified: 7/1/2026

#include "camera/OrbitCameraController.h"
#include "core/Input.h"
#include "codes/MouseButtonCodes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	OrbitCameraController::OrbitCameraController(float aspectRatio)
		: m_Camera(45.0f, aspectRatio, 0.1f, 1000.0f)
	{
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::OnUpdate(float ts)
	{
		const glm::vec2 mouse = Input::GetMousePosition();

		if (m_ControlEnabled)
		{
			const bool lmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
			const bool rmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);

			if (lmb || rmb)
			{
				// First frame of a drag: latch the position, produce no delta —
				// otherwise the camera jumps by the full distance the mouse moved
				// since the previous drag ended.
				if (m_Dragging)
				{
					const glm::vec2 delta = mouse - m_LastMousePos;

					if (lmb)
					{
						// Orbit: horizontal drag yaws around world +Y, vertical drag
						// pitches. Dragging right moves the camera right (scene
						// appears to rotate left) — the editor-standard feel.
						m_YawDeg   -= delta.x * m_OrbitSpeed;
						m_PitchDeg += delta.y * m_OrbitSpeed;
						m_PitchDeg  = std::clamp(m_PitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
					}
					else // rmb pan
					{
						// Pan in the camera's right/up plane. Scaling by distance
						// keeps the world pinned under the cursor at any zoom.
						const float scale = m_Distance * 0.0015f * m_PanSpeed;
						m_Target -= m_Camera.GetRight() * (delta.x * scale);
						m_Target += m_Camera.GetUp()    * (delta.y * scale);
					}
				}
				m_Dragging = true;
			}
			else
			{
				m_Dragging = false;
			}
		}
		else
		{
			m_Dragging = false;
		}

		m_LastMousePos = mouse;

		// Smooth asymptotic zoom blend (same feel as the ortho controller's zoom).
		// Frame-rate independent: converges ~63% per 0.1 s.
		if (std::abs(m_TargetDistance - m_Distance) > 1e-4f)
		{
			const float blend = 1.0f - std::exp(-ts * 10.0f);
			m_Distance += (m_TargetDistance - m_Distance) * blend;
		}
		else
		{
			m_Distance = m_TargetDistance;
		}

		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) { return OnMouseScrolled(event); });
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) { return OnWindowResized(event); });
	}

	void OrbitCameraController::OnResize(float width, float height)
	{
		m_Camera.SetViewportSize(width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::SetDistance(float distance)
	{
		m_Distance       = std::clamp(distance, m_MinDistance, m_MaxDistance);
		m_TargetDistance = m_Distance;
		RecalculateCamera();
	}

	void OrbitCameraController::SetTargetDistance(float distance)
	{
		m_TargetDistance = std::clamp(distance, m_MinDistance, m_MaxDistance);
	}

	void OrbitCameraController::SetYawPitch(float yawDeg, float pitchDeg)
	{
		m_YawDeg   = yawDeg;
		m_PitchDeg = std::clamp(pitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::RecalculateCamera()
	{
		// Spherical mount: yaw 0 / pitch 0 puts the camera on the target's +Z side.
		// Positive pitch raises the camera above the target (looking down).
		const float yaw   = glm::radians(m_YawDeg);
		const float pitch = glm::radians(m_PitchDeg);

		const glm::vec3 offset(
			m_Distance * std::cos(pitch) * std::sin(yaw),
			m_Distance * std::sin(pitch),
			m_Distance * std::cos(pitch) * std::cos(yaw));

		m_Camera.LookAt(m_Target + offset, m_Target);
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool OrbitCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		if (!m_ControlEnabled)
			return false;

		// Exponential zoom: each notch scales the distance, so zoom speed is
		// proportional to how far out you are (uniform-feeling at every scale).
		const float factor = std::pow(1.15f, -e.GetYOffset() * m_ZoomSpeed);
		m_TargetDistance = std::clamp(m_TargetDistance * factor, m_MinDistance, m_MaxDistance);

		return false; // never consume — see the event consumption contract
	}

	bool OrbitCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		if (e.GetHeight() > 0)
			OnResize(static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()));

		return false; // never consume — resize must reach framebuffers/viewports
	}

	/////////////////////////////////////////////////////////////////////////////////
}
