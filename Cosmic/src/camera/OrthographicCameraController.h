#pragma once

// OrthographicCameraController.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The OrthographicCameraController acts as a high-level wrapper for the
 * OrthographicCamera, providing an automated interaction layer. It handles user
 * input (Keyboard/Mouse), manages smooth zooming, and ensures the camera's aspect
 * ratio remains synchronized with the application window or viewport. It
 * simplifies camera manipulation by calculating proportional movement speeds
 * and enforcing spatial constraints (clamping).
 *
 * Documentation Notes:
 * - Proportional Movement: Movement speed is multiplied by the zoom level to
 *   ensure consistent feel regardless of magnification.
 * - Aspect Ratio Management: Automatically re-calculates projection bounds
 *   during resize events to prevent image stretching.
 * - Event Dispatching: Built-in handlers for MouseScrolled and WindowResized events.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. OrthographicCameraController(float aspectRatio, bool rotation = false)
 *    Pre:  Initial aspect ratio is provided.
 *    Post: Camera and controller state are initialized; rotation logic is enabled if specified.
 *
 * 2. void OnUpdate(float ts)
 *    Pre:  ts (timestep) is the time elapsed since the last frame.
 *    Post: Polls input (WASD/Arrows) and updates camera position and rotation.
 *
 * 3. void OnEvent(Event& e)
 *    Pre:  A valid Event object is passed.
 *    Post: Dispatches resize and scroll events to internal handler functions.
 *
 * 4. void OnResize(float width, float height)
 *    Pre:  None.
 *    Post: Updates internal aspect ratio and forces a camera projection re-calculation.
 *
 * 5. OrthographicCamera& GetCamera()
 *    Pre:  None.
 *    Post: Returns a reference to the underlying OrthographicCamera object.
 *
 * 6. void SetZoomLevel(float level)
 *    Pre:  None.
 *    Post: Sets the camera zoom and immediately updates the view projection.
 *
 * 7. void SetZoomLimits(float min, float max)
 *    Pre:  min < max.
 *    Post: Defines the hard caps for mouse-wheel zooming.
 *
 * 8. void SetPositionLimits(float minX, float maxX, float minY, float maxY)
 *    Pre:  None.
 *    Post: Enforces boundaries that the camera position cannot exceed during update.
 *
 * 9. void SetPosition(const glm::vec3& position)
 *    Pre:  None.
 *    Post: Manually overrides the current position (useful for following entities).
 */

#include "camera/OrthographicCamera.h"
#include "events/ApplicationEvent.h"
#include "events/MouseEvent.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class OrthographicCameraController
	{
	public:
		////////////////////////////////
		// Life Cycle & Main Execution
		///////////////////////////////

		OrthographicCameraController(float aspectRatio, bool rotation = false);

		void							OnUpdate(float ts);
		void							OnEvent(Event& e);

		////////////////////////////////
		// Viewport & Aspect Management
		///////////////////////////////

		void							OnResize(float width, float height);

		////////////////////////////////
		// Camera & Zoom Accessors
		///////////////////////////////

		OrthographicCamera&				GetCamera()					{ return m_Camera; }
		const OrthographicCamera&		GetCamera() const			{ return m_Camera; }

		float							GetZoomLevel() const		{ return m_ZoomLevel; }
		void							SetZoomLevel(float level)	{ m_ZoomLevel = level; CalculateView(); }

		////////////////////////////////
		// Speed Controls (Mutators)
		///////////////////////////////

		void				SetTranslationSpeed(float speed)		{ m_CameraTranslationSpeed = speed; }
		float				GetTranslationSpeed() const				{ return m_CameraTranslationSpeed; }

		void				SetRotationSpeed(float speed)			{ m_CameraRotationSpeed = speed; }
		float				GetRotationSpeed() const				{ return m_CameraRotationSpeed; }

		void				SetZoomSpeed(float speed)				{ m_ZoomSpeed = speed; }
		float				GetZoomSpeed() const					{ return m_ZoomSpeed; }

		////////////////////////////////
		// Constraint & Limit Management
		///////////////////////////////

		void		SetZoomLimits(float min, float max)				{ m_MinZoom = min; m_MaxZoom = max; }


		void SetPositionLimits(float minX, float maxX, float minY, float maxY)
		{
			m_MinX = minX; m_MaxX = maxX;
			m_MinY = minY; m_MaxY = maxY;
		}


		////////////////////////////////
		// Direct Transformation
		///////////////////////////////

		void		SetPosition(const glm::vec3& position)			{ m_CameraPosition = position; m_Camera.SetPosition(m_CameraPosition); }
		const		glm::vec3& GetPosition() const					{ return m_CameraPosition; }

	private:
		////////////////////////////////
		// Internal Handlers & Math
		///////////////////////////////

		void CalculateView();
		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		////////////////////////////////
		// Projection State
		///////////////////////////////

		float m_AspectRatio;
		float m_ZoomLevel = 1.0f;

		////////////////////////////////
		// Limit Constants
		///////////////////////////////

		float m_MinZoom		= 0.25f;
		float m_MaxZoom		= 10.0f;
		float m_ZoomSpeed	= 0.25f;

		float m_MinX		= -1000.0f, m_MaxX = 1000.0f;
		float m_MinY		= -1000.0f, m_MaxY = 1000.0f;

		////////////////////////////////
		// Camera Components & State
		///////////////////////////////

		OrthographicCamera m_Camera;
		bool m_Rotation;

		glm::vec3 m_CameraPosition	= { 0.0f, 0.0f, 0.0f };
		float m_CameraRotation		= 0.0f;

		////////////////////////////////
		// Movement Constants
		///////////////////////////////

		float m_CameraTranslationSpeed	= 5.0f;
		float m_CameraRotationSpeed		= 180.0f;
	};
}