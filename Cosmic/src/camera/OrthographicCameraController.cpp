#include "camera/OrthographicCameraController.h"
#include "core/Input.h"
#include "codes/KeyCodes.h"
#include <algorithm>

namespace Cosmic
{
	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation)
		: m_AspectRatio(aspectRatio),
		m_Camera(-m_AspectRatio * m_ZoomLevel, m_AspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel),
		m_Rotation(rotation)
	{
	}

	void OrthographicCameraController::OnUpdate(float ts)
	{
		// Movement logic using the translation speed multiplied by zoom level for consistent feel
		float actualMoveSpeed = m_CameraTranslationSpeed * m_ZoomLevel;

		if (Input::IsKeyPressed(KEY_A))
			m_CameraPosition.x -= actualMoveSpeed * ts;
		else if (Input::IsKeyPressed(KEY_D))
			m_CameraPosition.x += actualMoveSpeed * ts;

		if (Input::IsKeyPressed(KEY_W))
			m_CameraPosition.y += actualMoveSpeed * ts;
		else if (Input::IsKeyPressed(KEY_S))
			m_CameraPosition.y -= actualMoveSpeed * ts;

		// Apply position constraints
		m_CameraPosition.x = std::clamp(m_CameraPosition.x, m_MinX, m_MaxX);
		m_CameraPosition.y = std::clamp(m_CameraPosition.y, m_MinY, m_MaxY);

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(KEY_Q))
				m_CameraRotation += m_CameraRotationSpeed * ts;
			if (Input::IsKeyPressed(KEY_E))
				m_CameraRotation -= m_CameraRotationSpeed * ts;

			m_Camera.SetRotation(m_CameraRotation);
		}

		m_Camera.SetPosition(m_CameraPosition);
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(GLCORE_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(OrthographicCameraController::OnWindowResized));
	}

	void OrthographicCameraController::CalculateView()
	{
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		// Use the customizable zoom speed
		m_ZoomLevel -= e.GetYOffset() * m_ZoomSpeed;
		m_ZoomLevel = std::clamp(m_ZoomLevel, m_MinZoom, m_MaxZoom);

		CalculateView();
		return false;
	}

	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		m_AspectRatio = (float)e.GetWidth() / (float)e.GetHeight();
		CalculateView();
		return false;
	}
}