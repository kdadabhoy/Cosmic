#pragma once

// OrthographicCameraController.h
// Last Modified 5/24/2026

/**
 * Needs documentation updates
 * 
 * @class OrthographicCameraController
 * @brief High-level interaction wrapper for the OrthographicCamera subsystem.
 *
 * This controller manages hardware input polling (Keyboard/Mouse), provides smooth asymptotic
 * zoom interpolation, tracks translation/rotation offsets, and forces projection matrices
 * to scale proportionally with variable-sized ImGui or application viewports.
 *
 * Architectural Features:
 * - Proportional Speed scaling (Camera moves slower when zoomed closely to retain fine precision).
 * - Decoupled keybinding data layout structures allowing custom input mapping profiles at runtime.
 * - Global control flags to safely halt user-driven camera transformations during script/cutscene tracks.
 */

#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "events/ApplicationEvent.h"
#include "events/MouseEvent.h"
#include "codes/KeyCodes.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API OrthographicCameraController
	{
	public:
		/////////////////////////////////////////////////////////////////////////////////
		// Keybinding Layout Definition
		/////////////////////////////////////////////////////////////////////////////////

		struct CameraKeyBindings
		{
			uint32_t MoveLeft  = CS_KEY_A;
			uint32_t MoveRight = CS_KEY_D;
			uint32_t MoveUp    = CS_KEY_W;
			uint32_t MoveDown  = CS_KEY_S;
			uint32_t RotateQ   = CS_KEY_Q;
			uint32_t RotateE   = CS_KEY_E;
		};

	public:
		/////////////////////////////////////////////////////////////////////////////////
		// Lifecycle & Main Execution Cascade
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Constructor
		 * Pre: Initial width-to-height aspect ratio is provided.
		 * Post: Underlying OrthographicCamera projection boundaries are assigned.
		 */
		OrthographicCameraController(float aspectRatio, bool rotation = false);
		~OrthographicCameraController() = default;

		/**
		 * @brief Per-frame tick update. Polls active inputs and resolves zoom blending.
		 * Pre: ts represents a valid, frame-scaled delta time metric.
		 */
		void OnUpdate(float ts);

		/**
		 * @brief Dispatches interface and window updates down to internal event handlers.
		 */
		void OnEvent(Event& e);

		/////////////////////////////////////////////////////////////////////////////////
		// Viewport & Aspect Ratio Configurations
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Re-calculates perspective metrics to eliminate canvas image stretching.
		 */
		void OnResize(float width, float height);

		/////////////////////////////////////////////////////////////////////////////////
		// Camera & Zoom Management Accessors
		/////////////////////////////////////////////////////////////////////////////////

		OrthographicCamera& GetCamera() { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }

		float GetZoomLevel() const { return m_ZoomLevel; }

		/**
		 * @brief Forces an absolute override of active and target zoom levels instantly.
		 *        Hard-snaps to the new zoom with no interpolation; bypasses the smooth
		 *        asymptotic blend. Use SetTargetZoomLevel for animated transitions.
		 */
		void  SetZoomLevel(float level);

		/**
		 * @brief Initiates a smooth interpolated zoom toward the given level.
		 *        Sets only m_TargetZoomLevel; m_ZoomLevel is left unchanged so that
		 *        OnUpdate's asymptotic blend animates the camera naturally over time.
		 *        Do NOT call CalculateView() inside this method — the view is
		 *        recalculated by OnUpdate on the next tick.
		 */
		void  SetTargetZoomLevel(float level);

		/////////////////////////////////////////////////////////////////////////////////
		// Physics & Dynamic Translation Speed Controls
		/////////////////////////////////////////////////////////////////////////////////

		void  SetTranslationSpeed(float speed) { m_CameraTranslationSpeed = speed; }
		float GetTranslationSpeed() const { return m_CameraTranslationSpeed; }

		void  SetRotationSpeed(float speed) { m_CameraRotationSpeed = speed; }
		float GetRotationSpeed() const { return m_CameraRotationSpeed; }

		void  SetZoomSpeed(float speed) { m_ZoomSpeed = speed; }
		float GetZoomSpeed() const { return m_ZoomSpeed; }

		/////////////////////////////////////////////////////////////////////////////////
		// Spatial Constraint & Boundary Limit Configurations
		/////////////////////////////////////////////////////////////////////////////////

		void SetZoomLimits(float min, float max) { m_MinZoom = min; m_MaxZoom = max; }

		void SetPositionLimits(float minX, float maxX, float minY, float maxY)
		{
			m_MinX = minX; m_MaxX = maxX;
			m_MinY = minY; m_MaxY = maxY;
		}

		/////////////////////////////////////////////////////////////////////////////////
		// Direct Transformation Overrides
		/////////////////////////////////////////////////////////////////////////////////

		void             SetPosition(const glm::vec3& position);
		const glm::vec3& GetPosition() const { return m_CameraPosition; }

		/////////////////////////////////////////////////////////////////////////////////
		// Runtime Input Remapping & Control Toggles
		/////////////////////////////////////////////////////////////////////////////////

		void SetManualMovementEnabled(bool enabled) { m_ManualMovementEnabled = enabled; }
		bool IsManualMovementEnabled() const { return m_ManualMovementEnabled; }

		void                     SetKeyBindings(const CameraKeyBindings& bindings) { m_Bindings = bindings; }
		const CameraKeyBindings& GetKeyBindings() const { return m_Bindings; }
		CameraKeyBindings& GetKeyBindings() { return m_Bindings; }

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Infrastructure Handlers & Math
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Syncs camera projection ranges with the aspect ratio and zoom properties.
		 */
		void CalculateView();

		/**
		 * @note Event Consumption Contract:
		 * Returns false explicitly to allow the event to bubble up.
		 * This ensures client simulation layers can still intercept mouse
		 * scroll metrics even when the camera controller handles zooming.
		 */
		bool OnMouseScrolled(MouseScrolledEvent& e);

		/**
		 * @note Event Consumption Contract:
		 * Returns false explicitly to allow window sizing propagation to
		 * reach other decoupled systems (such as framebuffers and viewports).
		 */
		bool OnWindowResized(WindowResizeEvent& e);

	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Projection State Data
		/////////////////////////////////////////////////////////////////////////////////

		float m_AspectRatio;
		float m_ZoomLevel = 1.0f;
		float m_TargetZoomLevel = 1.0f;

		/////////////////////////////////////////////////////////////////////////////////
		// Mathematical Constraint Constants
		/////////////////////////////////////////////////////////////////////////////////

		float m_MinZoom = 0.25f;
		float m_MaxZoom = 10.0f;
		float m_ZoomSpeed = 0.25f;

		float m_MinX = -1000.0f, m_MaxX = 1000.0f;
		float m_MinY = -1000.0f, m_MaxY = 1000.0f;

		/////////////////////////////////////////////////////////////////////////////////
		// Hardware Input Mapping & Behavioral States
		/////////////////////////////////////////////////////////////////////////////////

		bool              m_ManualMovementEnabled = true;
		CameraKeyBindings m_Bindings;

		/////////////////////////////////////////////////////////////////////////////////
		// Hardware Core Camera Subsystems
		/////////////////////////////////////////////////////////////////////////////////

		OrthographicCamera m_Camera;
		bool               m_Rotation;

		glm::vec3 m_CameraPosition = { 0.0f, 0.0f, 0.0f };
		float     m_CameraRotation = 0.0f;

		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Velocity Co-efficients
		/////////////////////////////////////////////////////////////////////////////////

		float m_CameraTranslationSpeed = 5.0f;
		float m_CameraRotationSpeed = 180.0f;
	};
}