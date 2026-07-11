// Camera2DController.cpp — 2D pan/zoom camera rig (Phase 17 / U3). See header.

#include "camera/Camera2DController.h"

#include "core/Input.h"
#include "codes/MouseButtonCodes.h"

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	Camera2DController::Camera2DController(float aspectRatio)
		: m_Aspect(aspectRatio > 0.0f ? aspectRatio : 1.0f)
	{
		Recalculate();
	}

	void Camera2DController::Recalculate()
	{
		// Half-extents from zoom + aspect; ±1000 Z clip so sprites across
		// Z/ZOrder and modest 2.5D props stay visible (larger world Z = nearer).
		const float halfH = m_Zoom;
		const float halfW = m_Zoom * m_Aspect;
		m_Camera.SetProjection(-halfW, halfW, -halfH, halfH, -1000.0f, 1000.0f);
		m_Camera.SetPosition({ m_Focus.x, m_Focus.y, 0.0f });
	}

	void Camera2DController::SetZoom(float halfHeight)
	{
		m_Zoom = std::clamp(halfHeight, m_MinZoom, m_MaxZoom);
		Recalculate();
	}

	void Camera2DController::OnResize(float width, float height)
	{
		if (width <= 0.0f || height <= 0.0f)
			return;
		m_Aspect = width / height;
		Recalculate();
	}

	void Camera2DController::SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx)
	{
		m_ViewportPos  = posPx;
		m_ViewportSize = sizePx;
		OnResize(sizePx.x, sizePx.y);
	}

	void Camera2DController::SetControlEnabled(bool enabled)
	{
		m_ControlEnabled = enabled;
		if (!enabled)
			m_Dragging = false;   // ending control mid-drag ends the drag
	}

	// ------------------------------------------------------------------------
	// Pure math
	// ------------------------------------------------------------------------

	glm::vec2 Camera2DController::ScreenToWorld(const glm::vec2& screenPx,
	                                            const glm::vec2& vpPosPx, const glm::vec2& vpSizePx,
	                                            const glm::vec2& focus, float zoomHalfHeight)
	{
		if (vpSizePx.y <= 0.0f)
			return focus;
		const glm::vec2 center = vpPosPx + vpSizePx * 0.5f;
		const float unitsPerPx = (2.0f * zoomHalfHeight) / vpSizePx.y;
		return { focus.x + (screenPx.x - center.x) * unitsPerPx,
		         focus.y - (screenPx.y - center.y) * unitsPerPx };   // screen +y down
	}

	glm::vec2 Camera2DController::PanBy(const glm::vec2& focus, const glm::vec2& deltaPx,
	                                    float zoomHalfHeight, float viewportHeightPx)
	{
		if (viewportHeightPx <= 0.0f)
			return focus;
		const float unitsPerPx = (2.0f * zoomHalfHeight) / viewportHeightPx;
		// Dragging right moves the world right under the cursor = focus left.
		return { focus.x - deltaPx.x * unitsPerPx,
		         focus.y + deltaPx.y * unitsPerPx };
	}

	glm::vec2 Camera2DController::ZoomAboutPoint(const glm::vec2& focus, const glm::vec2& worldAnchor,
	                                             float zoomBefore, float zoomAfter)
	{
		if (zoomBefore <= 0.0f)
			return focus;
		const float k = zoomAfter / zoomBefore;
		return worldAnchor + (focus - worldAnchor) * k;
	}

	// ------------------------------------------------------------------------
	// Input
	// ------------------------------------------------------------------------

	void Camera2DController::OnUpdate(float ts)
	{
		(void)ts;
		const glm::vec2 mouse = Input::GetMouseScreenPosition();

		const bool mmb = m_ControlEnabled && Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_MIDDLE);
		if (mmb && !m_Dragging)
		{
			m_Dragging  = true;
			m_LastMouse = mouse;
		}
		else if (!mmb)
		{
			m_Dragging = false;
		}

		if (m_Dragging)
		{
			const glm::vec2 delta = mouse - m_LastMouse;
			m_LastMouse = mouse;
			if (delta.x != 0.0f || delta.y != 0.0f)
			{
				m_Focus = PanBy(m_Focus, delta, m_Zoom, m_ViewportSize.y);
				Recalculate();
			}
		}
	}

	void Camera2DController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(
			[this](MouseScrolledEvent& event) { return OnMouseScrolled(event); });
	}

	bool Camera2DController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		if (!m_ControlEnabled)
			return false;

		const float before = m_Zoom;
		const float after  = std::clamp(before * std::pow(1.15f, -e.GetYOffset() * m_ZoomSpeed),
		                                m_MinZoom, m_MaxZoom);
		if (after == before)
			return false;

		// Keep the world point under the cursor fixed on screen.
		if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		{
			const glm::vec2 anchor = ScreenToWorld(Input::GetMouseScreenPosition(),
			                                       m_ViewportPos, m_ViewportSize,
			                                       m_Focus, before);
			m_Focus = ZoomAboutPoint(m_Focus, anchor, before, after);
		}
		m_Zoom = after;
		Recalculate();

		// Contract: never consume — other observers may want scroll too
		// (matches OrbitCameraController / OrthographicCameraController).
		return false;
	}

	// ------------------------------------------------------------------------
	// View queries / framing
	// ------------------------------------------------------------------------

	void Camera2DController::VisibleRect(glm::vec2& outMin, glm::vec2& outMax) const
	{
		const glm::vec2 half{ m_Zoom * m_Aspect, m_Zoom };
		outMin = m_Focus - half;
		outMax = m_Focus + half;
	}

	void Camera2DController::FrameBounds(const glm::vec2& worldMin, const glm::vec2& worldMax)
	{
		const glm::vec2 size = worldMax - worldMin;
		m_Focus = (worldMin + worldMax) * 0.5f;
		if (size.x > 1e-6f || size.y > 1e-6f)
		{
			const float needH = std::max(size.y * 0.5f,
			                             (m_Aspect > 0.0f ? (size.x * 0.5f) / m_Aspect : size.y * 0.5f));
			m_Zoom = std::clamp(needH * 1.1f, m_MinZoom, m_MaxZoom);
		}
		Recalculate();
	}
}
