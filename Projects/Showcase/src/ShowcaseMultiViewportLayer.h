#pragma once
// ShowcaseMultiViewportLayer.h
// Last Modified: 5/24/2026

/**
 * ShowcaseMultiViewportLayer
 *
 * Demonstrates the Cosmic Engine's RenderPass / multi-viewport system by rendering
 * the same live simulation scene from four independent camera perspectives
 * simultaneously within a single OnUpdate frame.
 *
 * LAYOUT (2x2 grid, each quadrant is one camera):
 *
 *  ┌───────────────────┬───────────────────┐
 *  │                   │                   │
 *  │  TOP-LEFT         │  TOP-RIGHT        │
 *  │  Main Camera      │  Overhead         │
 *  │  (orthographic    │  (zoom-out,       │
 *  │   follow cam)     │   bird's eye)     │
 *  │                   │                   │
 *  ├───────────────────┼───────────────────┤
 *  │                   │                   │
 *  │  BOTTOM-LEFT      │  BOTTOM-RIGHT     │
 *  │  Slow-Mo / Tinted │  Debug Wireframe  │
 *  │  (same scene,     │  (entity bounds,  │
 *  │   blue tint)      │   grid lines)     │
 *  │                   │                   │
 *  └───────────────────┴───────────────────┘
 *
 * Each quadrant uses a completely independent RenderPass RAII scope. Geometry
 * submitted inside one scope is flushed and isolated before the next quadrant
 * begins. The same entity data is queried from the shared scene each frame,
 * but each camera produces a visually distinct view.
 *
 * This layer is a child mode inside ShowcaseProject's m_Modes vector.
 * It is NOT pushed onto the engine LayerStack directly.
 */

#include <Cosmic.h>
#include <vector>

namespace Showcase
{
	// Simple floating object to give all cameras something interesting to watch
	struct MultiViewObject
	{
		Cosmic::Entity EntityHandle;
		glm::vec3      Velocity = { 0.0f, 0.0f, 0.0f };
		glm::vec4      Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float          RotationVel = 0.0f;    // degrees per second
		float          Radius = 3.5f;    // orbit radius
		float          OrbitSpeed = 1.0f;    // radians per second
		float          OrbitPhase = 0.0f;    // initial phase offset
		bool           IsStar = false;   // central anchor vs. orbiter
	};

	class ShowcaseMultiViewportLayer : public Cosmic::Layer
	{
	public:
		ShowcaseMultiViewportLayer();
		virtual ~ShowcaseMultiViewportLayer() override = default;

		virtual void OnAttach()                         override;
		virtual void OnDetach()                         override;
		virtual void OnUpdate(float ts)                 override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender()                    override;
		virtual void OnEvent(Cosmic::Event& e)          override;

	private:
		void BuildScene();
		void CleanupScene();
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

		// Draw helpers called per quadrant (so each pass renders the same content
		// but can apply per-pass stylistic overrides)
		void DrawSceneContent(bool wireframe = false, const glm::vec4& tint = { 1.f, 1.f, 1.f, 1.f });
		void DrawGrid(const glm::vec4& color, float spacing, float extent);

	private:
		// Shared scene that all four cameras observe
		Cosmic::Ref<Cosmic::Scene> m_Scene;

		// Four independent cameras — each targets the same world but from a unique angle/zoom
		Cosmic::OrthographicCameraController m_CamMain;      // Top-left: tracking close-up
		Cosmic::OrthographicCameraController m_CamOverhead;  // Top-right: zoomed-out overview
		Cosmic::OrthographicCameraController m_CamSlowMo;    // Bottom-left: tinted clone of main
		Cosmic::OrthographicCameraController m_CamDebug;     // Bottom-right: wireframe / debug

		// Orbital simulation objects
		std::vector<MultiViewObject> m_Objects;

		// Viewport pixel dimensions (updated on resize)
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		// Simulation state
		float m_Time = 0.0f;
		float m_CamMainAngle = 0.0f;  // slow auto-pan on main camera

		// Inspector controls
		int   m_ObjectCount = 6;
		float m_OrbitScale = 3.5f;
		bool  m_ShowCamLabels = true;
		bool  m_AnimateCam = true;
	};
}