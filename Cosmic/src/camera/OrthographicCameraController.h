#pragma once

#include "camera/OrthographicCamera.h"
#include "events/ApplicationEvent.h"
#include "events/MouseEvent.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class OrthographicCameraController
	{
	public:
		OrthographicCameraController(float aspectRatio, bool rotation = false);

		void OnUpdate(float ts);
		void OnEvent(Event& e);

		OrthographicCamera& GetCamera() { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }

		float GetZoomLevel() const { return m_ZoomLevel; }
		void SetZoomLevel(float level) { m_ZoomLevel = level; CalculateView(); }

		// Speed Setters and Getters
		void SetTranslationSpeed(float speed) { m_CameraTranslationSpeed = speed; }
		float GetTranslationSpeed() const { return m_CameraTranslationSpeed; }

		void SetRotationSpeed(float speed) { m_CameraRotationSpeed = speed; }
		float GetRotationSpeed() const { return m_CameraRotationSpeed; }

		void SetZoomSpeed(float speed) { m_ZoomSpeed = speed; }
		float GetZoomSpeed() const { return m_ZoomSpeed; }

		// Setters for hard caps
		void SetZoomLimits(float min, float max) { m_MinZoom = min; m_MaxZoom = max; }
		void SetPositionLimits(float minX, float maxX, float minY, float maxY)
		{
			m_MinX = minX; m_MaxX = maxX;
			m_MinY = minY; m_MaxY = maxY;
		}

	private:
		void CalculateView();
		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		float m_AspectRatio;
		float m_ZoomLevel = 1.0f;

		// Zoom and Movement Settings
		float m_MinZoom = 0.25f;
		float m_MaxZoom = 10.0f;
		float m_ZoomSpeed = 0.25f;

		// Position hard caps
		float m_MinX = -1000.0f, m_MaxX = 1000.0f;
		float m_MinY = -1000.0f, m_MaxY = 1000.0f;

		OrthographicCamera m_Camera;

		bool m_Rotation;
		glm::vec3 m_CameraPosition = { 0.0f, 0.0f, 0.0f };
		float m_CameraRotation = 0.0f;

		// Base speeds
		float m_CameraTranslationSpeed = 5.0f;
		float m_CameraRotationSpeed = 180.0f;
	};
}