#include "camera/OrthographicCameraController.h"
#include "core/Input.h"
#include "codes/KeyCodes.h"
#include <algorithm>
#include <cmath>

namespace Cosmic
{
	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation)
		: m_AspectRatio(aspectRatio)
		, m_Camera(-m_AspectRatio * m_ZoomLevel, m_AspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel)
		, m_Rotation(rotation)
	{
	}

	void OrthographicCameraController::OnUpdate(float ts)
	{
		// --- Asymptotic Smooth Zoom Interpolation ---
		// Tuning co-efficient. Higher values offer snappy scaling; lower results in floaty velocity deceleration curves.
		constexpr float smoothnessFactor = 10.0f;

		if (std::abs(m_ZoomLevel - m_TargetZoomLevel) > 0.001f)
		{
			// Prevent matrix overshooting thresholds during frame spikes by clamping delta steps
			float blendStep = std::clamp(smoothnessFactor * ts, 0.0f, 1.0f);

			m_ZoomLevel += (m_TargetZoomLevel - m_ZoomLevel) * blendStep;
			m_ZoomLevel = std::clamp(m_ZoomLevel, m_MinZoom, m_MaxZoom);
			CalculateView();
		}
		else
		{
			m_ZoomLevel = m_TargetZoomLevel;
		}

		// --- Keyboard Input Processing Pipeline ---
		if (m_ManualMovementEnabled)
		{
			// Scaling speed parameters proportionally to magnification ensures translation control remains uniform
			float actualMoveSpeed = m_CameraTranslationSpeed * m_ZoomLevel;

			if (m_Bindings.MoveLeft != 0 && Input::IsKeyPressed(m_Bindings.MoveLeft))
				m_CameraPosition.x -= actualMoveSpeed * ts;
			else if (m_Bindings.MoveRight != 0 && Input::IsKeyPressed(m_Bindings.MoveRight))
				m_CameraPosition.x += actualMoveSpeed * ts;

			if (m_Bindings.MoveUp != 0 && Input::IsKeyPressed(m_Bindings.MoveUp))
				m_CameraPosition.y += actualMoveSpeed * ts;
			else if (m_Bindings.MoveDown != 0 && Input::IsKeyPressed(m_Bindings.MoveDown))
				m_CameraPosition.y -= actualMoveSpeed * ts;

			// Enforce hard layout system limits
			m_CameraPosition.x = std::clamp(m_CameraPosition.x, m_MinX, m_MaxX);
			m_CameraPosition.y = std::clamp(m_CameraPosition.y, m_MinY, m_MaxY);

			// Angular rotation pipeline processing
			if (m_Rotation)
			{
				if (m_Bindings.RotateQ != 0 && Input::IsKeyPressed(m_Bindings.RotateQ))
					m_CameraRotation += m_CameraRotationSpeed * ts;
				if (m_Bindings.RotateE != 0 && Input::IsKeyPressed(m_Bindings.RotateE))
					m_CameraRotation -= m_CameraRotationSpeed * ts;

				m_Camera.SetRotation(m_CameraRotation);
			}
		}

		// Flush coordinate transforms down into hardware camera memory matrices
		m_Camera.SetPosition(m_CameraPosition);
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) { return OnMouseScrolled(event); });
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) { return OnWindowResized(event); });
	}

	void OrthographicCameraController::OnResize(float width, float height)
	{
		m_AspectRatio = width / height;
		CalculateView();
	}

	void OrthographicCameraController::SetPosition(const glm::vec3& position)
	{
		m_CameraPosition = position;
		m_Camera.SetPosition(m_CameraPosition);
	}

	void OrthographicCameraController::SetZoomLevel(float level)
	{
		m_TargetZoomLevel = std::clamp(level, m_MinZoom, m_MaxZoom);
		m_ZoomLevel = m_TargetZoomLevel;
		CalculateView();
	}

	void OrthographicCameraController::CalculateView()
	{
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		m_TargetZoomLevel -= e.GetYOffset() * m_ZoomSpeed;
		m_TargetZoomLevel = std::clamp(m_TargetZoomLevel, m_MinZoom, m_MaxZoom);
		return false; // Bubbles event upward so client systems can monitor scroll states
	}

	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		OnResize((float)e.GetWidth(), (float)e.GetHeight());
		return false; // Bubbles event upward so framebuffers and swapchains update size metrics
	}
}