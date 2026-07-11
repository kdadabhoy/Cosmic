#pragma once

// Camera2DController.h — 2D pan/zoom camera rig (Phase 17 / U3).
//
/**
 * General Description:
 *
 * The editor-style 2D navigation rig: an OrthographicCamera on the XY plane
 * (+Y up) looking down -Z, with MMB drag = pan and scroll = zoom about the
 * cursor. Mirrors OrbitCameraController's architecture — input POLLING in
 * OnUpdate, scroll via OnEvent, a SetViewportRect contract in SCREEN pixels
 * (WorkspaceLayer::GetViewportPos/GetViewportSize — ImGui's coordinate space),
 * and a master SetControlEnabled gate the host flips per-frame from viewport
 * hover. Engine-generic: nothing editor-branded; any 2D app can drive it.
 *
 * CONVENTIONS: `Focus` is the world XY point at the view center; `Zoom` is the
 * visible HALF-HEIGHT in world units (smaller = closer). The camera sits at
 * (Focus, 0) with a ±1000 Z clip range, so sprites spread across Z/ZOrder and
 * modest 3D props in 2.5D scenes all render; larger world Z = nearer the viewer
 * (the standard 2D convention). The pure pan/zoom math is exposed as static
 * functions (PanBy / ZoomAboutPoint / ScreenToWorld) so it is headless-testable.
 */

#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "events/Event.h"
#include "events/MouseEvent.h"

#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API Camera2DController
	{
	public:
		Camera2DController(float aspectRatio);
		~Camera2DController() = default;

		// Per-frame tick: polls the MMB pan drag. Call after SetViewportRect /
		// SetControlEnabled for the frame.
		void OnUpdate(float ts);

		// Dispatches scroll events (zoom about the cursor). Resize is the host's
		// job (call OnResize / SetViewportRect — matches OrbitCameraController).
		void OnEvent(Event& e);

		// Re-sync the projection aspect with a resized viewport/framebuffer.
		void OnResize(float width, float height);

		// The viewport rectangle in SCREEN pixels (zoom-to-cursor + pan px→world
		// need it; also updates the aspect — superset of OnResize).
		void SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);

		// Master enable (disable while a gizmo drags / the cursor left the
		// viewport). Disabling mid-drag ends the drag.
		void SetControlEnabled(bool enabled);
		bool IsControlEnabled() const { return m_ControlEnabled; }

		// True while an MMB pan drag is in progress (lets the host keep control
		// enabled for a drag that started inside the viewport).
		bool IsDragging() const { return m_Dragging; }

		OrthographicCamera&       GetCamera()       { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }

		// --- View state ------------------------------------------------------
		void             SetFocus(const glm::vec2& xy) { m_Focus = xy; Recalculate(); }
		const glm::vec2& GetFocus() const              { return m_Focus; }

		// Zoom = visible half-height in world units (clamped to sane limits).
		void  SetZoom(float halfHeight);
		float GetZoom() const { return m_Zoom; }

		float GetAspect() const { return m_Aspect; }

		// The world-space rect currently visible (grid drawing / culling).
		void VisibleRect(glm::vec2& outMin, glm::vec2& outMax) const;

		// Frame a world-space XY box (pads ~10%); degenerate boxes just recenter.
		void FrameBounds(const glm::vec2& worldMin, const glm::vec2& worldMax);

		// --- Pure math (headless-tested) --------------------------------------
		// Screen pixel → world XY for a given focus/zoom/viewport.
		static glm::vec2 ScreenToWorld(const glm::vec2& screenPx,
		                               const glm::vec2& vpPosPx, const glm::vec2& vpSizePx,
		                               const glm::vec2& focus, float zoomHalfHeight);

		// Pan by a screen-pixel delta: the world point under the cursor follows it.
		static glm::vec2 PanBy(const glm::vec2& focus, const glm::vec2& deltaPx,
		                       float zoomHalfHeight, float viewportHeightPx);

		// New focus so `worldAnchor` stays at the same screen position across a
		// zoom change (zoom-about-cursor).
		static glm::vec2 ZoomAboutPoint(const glm::vec2& focus, const glm::vec2& worldAnchor,
		                                float zoomBefore, float zoomAfter);

	private:
		void Recalculate();
		bool OnMouseScrolled(MouseScrolledEvent& e);

	private:
		OrthographicCamera m_Camera{ -1.0f, 1.0f, -1.0f, 1.0f };

		glm::vec2 m_Focus{ 0.0f, 0.0f };
		float     m_Zoom   = 5.0f;      // half-height, world units
		float     m_Aspect = 16.0f / 9.0f;

		float m_MinZoom = 0.01f;
		float m_MaxZoom = 10000.0f;
		float m_ZoomSpeed = 1.0f;

		glm::vec2 m_ViewportPos{ 0.0f };
		glm::vec2 m_ViewportSize{ 0.0f };

		bool      m_ControlEnabled = true;
		bool      m_Dragging = false;
		glm::vec2 m_LastMouse{ 0.0f };
	};
}
