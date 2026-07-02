#pragma once

// Engine3DDemo.h
//
// ============================================================================
// Engine3DDemo — Phase 4 acceptance test for the 3D engine foundations.
// ============================================================================
//
// Exercises every Phase 4 deliverable in one runnable app:
//
//   E1  Configurable fixed timestep — a Hz slider drives
//       Application::SetFixedTimestepHz and a counter shows the measured
//       OnFixedUpdate rate (240 Hz must read ~4x the default 60).
//   E3  math/Spatial.h — the aircraft is simulated in the NED world frame
//       with a quaternion attitude (QuatFromEulerZYX / IntegrateBodyRate) and
//       converted to the render frame via NedToRender / NedQuatToRender.
//   S1  PerspectiveCamera + OrbitCameraController + Renderer3D lines:
//       grid, axes, wire box, trajectory polyline; LMB orbit / RMB pan /
//       scroll zoom, plus an auto-orbit mode.
//   S2  Meshes + Lambert: a placeholder aircraft assembled from primitives
//       (box/cylinder/cone/plane/uv-sphere all appear in the scene).
//   2D coexistence: a Renderer2D overlay pass renders on top of the 3D
//       world every frame (doc 05 contract rule: no 2D regressions).
//
// Acceptance line (roadmap Phase 4): "demo layer flies an orbit camera
// around a shaded placeholder aircraft over a grid."
// ============================================================================

#include <Cosmic.h>

#include <vector>

namespace Workspace
{
	class Engine3DDemo : public Cosmic::Layer
	{
	public:
		Engine3DDemo();
		virtual ~Engine3DDemo() override = default;

		virtual void OnAttach()                override;
		virtual void OnDetach()                override;
		virtual void OnUpdate(float ts)        override;
		virtual void OnFixedUpdate(float dt)   override;
		virtual void OnImGuiRender()           override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		void BuildAircraftMeshes();
		void DrawAircraft();                  // submits the mesh parts under the sim transform
		void Draw2DOverlay();                 // proves Renderer2D still works on top

	private:
		// ---- Camera rig (S1) ----
		Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
		glm::vec2 m_ViewportSize{ 0.0f, 0.0f };
		bool      m_AutoOrbit      = true;
		float     m_AutoOrbitSpeed = 10.0f;   // deg/s

		// ---- Placeholder aircraft (S2) — model frame: nose -Z, up +Y, right +X ----
		Cosmic::Ref<Cosmic::Mesh> m_Fuselage;   // cylinder
		Cosmic::Ref<Cosmic::Mesh> m_Nose;       // cone
		Cosmic::Ref<Cosmic::Mesh> m_Canopy;     // uv-sphere
		Cosmic::Ref<Cosmic::Mesh> m_Wing;       // box
		Cosmic::Ref<Cosmic::Mesh> m_Tailplane;  // box
		Cosmic::Ref<Cosmic::Mesh> m_Fin;        // box
		Cosmic::Ref<Cosmic::Mesh> m_Pod;        // wingtip motor pods (cylinder)
		Cosmic::Ref<Cosmic::Mesh> m_Pad;        // ground pad under the orbit center (plane)

		// ---- Simulation state (E3: NED frame, quaternion attitude) ----
		glm::vec3 m_PosNed{ 0.0f, 0.0f, -6.0f };   // N, E, D — 6 m above ground
		glm::quat m_AttNed{ 1.0f, 0.0f, 0.0f, 0.0f }; // body -> NED
		float     m_SpeedMs   = 8.0f;               // forward speed
		float     m_BankDeg   = 20.0f;              // constant-bank circle
		bool      m_SimPaused = false;

		// Trajectory ribbon (render-frame points, ring-buffer style)
		std::vector<glm::vec3> m_Trail;
		float                  m_TrailTimer = 0.0f;
		static constexpr size_t k_TrailMax  = 600;

		// ---- E1 instrumentation ----
		float m_FixedHzUi        = 60.0f;   // slider value pushed to the engine
		int   m_FixedTickCounter = 0;       // ticks since the last window rollover
		float m_WindowStartTime  = -1.0f;   // GetAbsoluteTime() at window start (unscaled)
		float m_MeasuredFixedHz  = 0.0f;    // last completed window's ticks/second

		// ---- Render toggles ----
		bool  m_ShowGrid    = true;
		bool  m_ShowAxes    = true;
		bool  m_ShowWireBox = false;
		bool  m_ShowTrail   = true;
		bool  m_Show2D      = true;
		float m_Ambient     = 0.25f;
		glm::vec3 m_LightDir{ -0.4f, -1.0f, -0.25f };
	};

} // namespace Workspace
