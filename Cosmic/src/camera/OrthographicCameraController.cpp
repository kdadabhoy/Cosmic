#include "camera/OrthographicCameraController.h"
#include "core/Input.h"
#include "codes/KeyCodes.h"
#include <algorithm>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 *
	 * Initializes the controller with a specific aspect ratio and creates the
	 * underlying OrthographicCamera using the initial zoom level. Sets whether
	 * user-controlled rotation is permitted for this camera instance.
	 */
	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation)
		: m_AspectRatio(aspectRatio),
		m_Camera(-m_AspectRatio * m_ZoomLevel, m_AspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel),
		m_Rotation(rotation)
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnUpdate
	 *
	 * Per-frame logic update. Handles keyboard input for camera translation
	 * and rotation.
	 *
	 * CONSISTENCY LOGIC: The movement speed is scaled by the current m_ZoomLevel.
	 * This ensures that as the user zooms in, the camera moves slower in world units,
	 * keeping the perceived movement speed on screen feeling consistent.
	 *
	 * All position updates are passed through std::clamp to respect engine-defined
	 * world boundaries.
	 */
	void OrthographicCameraController::OnUpdate(float ts)
	{

		// --- Smooth Zoom Interpolation ---
		// Higher multiplier = faster/snappier zoom. Lower multiplier = smoother/floatier zoom.
		float smoothnessFactor = 10.0f;

		if (std::abs(m_ZoomLevel - m_TargetZoomLevel) > 0.001f)
		{
			// Prevent overshooting if frame rate hiccups by clamping the step multiplier to 1.0max
			float blendStep = std::clamp(smoothnessFactor * ts, 0.0f, 1.0f);

			m_ZoomLevel += (m_TargetZoomLevel - m_ZoomLevel) * blendStep;
			m_ZoomLevel = std::clamp(m_ZoomLevel, m_MinZoom, m_MaxZoom);
			CalculateView();
		}
		else
		{
			m_ZoomLevel = m_TargetZoomLevel;
		}
		// ---------------------------------


		float actualMoveSpeed = m_CameraTranslationSpeed * m_ZoomLevel;

		if (Input::IsKeyPressed(CS_KEY_A))
			m_CameraPosition.x -= actualMoveSpeed * ts;
		else if (Input::IsKeyPressed(CS_KEY_D))
			m_CameraPosition.x += actualMoveSpeed * ts;

		if (Input::IsKeyPressed(CS_KEY_W))
			m_CameraPosition.y += actualMoveSpeed * ts;
		else if (Input::IsKeyPressed(CS_KEY_S))
			m_CameraPosition.y -= actualMoveSpeed * ts;

		m_CameraPosition.x = std::clamp(m_CameraPosition.x, m_MinX, m_MaxX);
		m_CameraPosition.y = std::clamp(m_CameraPosition.y, m_MinY, m_MaxY);

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(CS_KEY_Q))
				m_CameraRotation += m_CameraRotationSpeed * ts;
			if (Input::IsKeyPressed(CS_KEY_E))
				m_CameraRotation -= m_CameraRotationSpeed * ts;

			m_Camera.SetRotation(m_CameraRotation);
		}

		m_Camera.SetPosition(m_CameraPosition);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnEvent
	 *
	 * Entry point for engine events. Uses an EventDispatcher to route window
	 * resizing and mouse scrolling specifically to the controller's internal
	 * logic handlers.
	 */
	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(GLCORE_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(OrthographicCameraController::OnWindowResized));
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnResize
	 *
	 * Recalculates the aspect ratio based on new pixel dimensions. This is
	 * essential for maintaining the correct scale of the scene when the user
	 * resizes the application window or an ImGui viewport.
	 */
	void OrthographicCameraController::OnResize(float width, float height)
	{
		m_AspectRatio = width / height;
		CalculateView();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * CalculateView
	 *
	 * Synchronizes the underlying Camera's projection matrix with the controller's
	 * current AspectRatio and ZoomLevel.
	 */
	void OrthographicCameraController::CalculateView()
	{
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnMouseScrolled
	 *
	 * Handles zoom logic. The zoom level is clamped to prevent the projection
	 * from becoming inverted or excessively large. Returns false to allow the
	 * event to bubble up if needed (though usually handled here).
	 */
	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		m_TargetZoomLevel -= e.GetYOffset() * m_ZoomSpeed;
		m_TargetZoomLevel = std::clamp(m_TargetZoomLevel, m_MinZoom, m_MaxZoom);

		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnWindowResized
	 *
	 * Callback for the WindowResizeEvent. Updates the viewport dimensions to
	 * prevent stretching.
	 */
	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		OnResize((float)e.GetWidth(), (float)e.GetHeight());
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrthographicCameraController::SetZoomLevel(float level)
	{
		m_TargetZoomLevel = std::clamp(level, m_MinZoom, m_MaxZoom);
		m_ZoomLevel = m_TargetZoomLevel;
		CalculateView();
	}

	/////////////////////////////////////////////////////////////////////////////////

}