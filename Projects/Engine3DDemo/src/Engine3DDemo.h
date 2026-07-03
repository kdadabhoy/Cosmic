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
// Phase 8 (S5) adds the CAD/editor layer on top: NavStyle bindings +
// orbit-about-cursor, snap/frame views, the ViewCube and the transform gizmo
// drawn as a VIEWPORT OVERLAY (WorkspaceLayer::BeginViewportOverlay), and
// click-to-select through ScenePicker + the shared EntitySelection bus.
//
// UI layout: one window per concern, bound to engine dock ports in OnAttach —
//   left   : "Camera & Views" / "Editor Tools" / "Rendering & Lighting"
//   right  : "Simulation & Timing" / "Feature Demos"
//   center : the engine Viewport (+ gizmo/ViewCube overlay)
//
// Acceptance line (roadmap Phase 4): "demo layer flies an orbit camera
// around a shaded placeholder aircraft over a grid."
// ============================================================================

#include <Cosmic.h>

#include <string>
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

		// ---- ImGui panels (window names = dock-port bindings in OnAttach) ----
		void DrawViewportOverlay();           // gizmo + ViewCube, on the 3D image
		void DrawCameraPanel();               // "Camera & Views"
		void DrawEditorPanel();               // "Editor Tools"
		void DrawRenderingPanel();            // "Rendering & Lighting"
		void DrawSimulationPanel();           // "Simulation & Timing"
		void DrawFeatureDemosPanel();         // "Feature Demos"

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

		// ---- Material pad (S4.2) — custom-material render path ----
		Cosmic::Ref<Cosmic::Material> m_PadMaterial;   // DemoChecker3D material

		// ---- ECS scene (S4.3) — MeshRendererComponent via Scene::OnRender3D ----
		Cosmic::Ref<Cosmic::Scene> m_Scene;            // small mesh-renderer scene
		void BuildEcsScene();

		// ---- Asset cache check (S4.4a) ----
		std::string m_CacheCheckResult;                // last "cache check" outcome

		// ---- glTF model (S4.4b) ----
		Cosmic::Ref<Cosmic::Model> m_DuckModel;        // engine://models/Duck.glb
		std::string m_GltfCacheResult;                 // "same Ref on reload" outcome

		// ---- Compute + SSBO (S4.7) ----
		Cosmic::Ref<Cosmic::Shader>        m_ComputeShader;   // ComputeParticles.glsl
		Cosmic::Ref<Cosmic::Shader>        m_PointShader;     // ParticlePoints.glsl
		Cosmic::Ref<Cosmic::StorageBuffer> m_ParticleSSBO;    // std430 particle positions
		bool  m_ShowCompute  = false;
		float m_ComputeTime  = 0.0f;
		static constexpr uint32_t k_ParticleCount = 1000000;  // 1M points

		// ---- Picking / MRT (S4.6) ----
		Cosmic::Ref<Cosmic::FrameBuffer> m_PickFbo;    // {RGBA8, RED_INTEGER, DEPTH24STENCIL8}
		Cosmic::PerspectiveCamera m_PickCam{ 45.0f, 1.0f, 0.1f, 100.0f };
		bool m_ShowPicking = false;
		int  m_HoveredId   = -1;                        // last ReadPixel result
		void RenderPickPass();                          // pre-pass into m_PickFbo

		// ---- CAD navigation, ViewCube, picking, gizmos (Phase 8 / S5) ----
		bool m_CadNav       = false;   // S5.1: CAD (SolidWorks) bindings vs Classic
		bool m_Inertia      = false;   // S5.1: optional orbit inertia
		Cosmic::Ref<Cosmic::NavigationCube> m_NavCube;   // S5.3 orientation cube
		Cosmic::Ref<Cosmic::ScenePicker>    m_Picker;    // S5.4 entity-ID picking
		bool m_EditorMode   = false;   // S5.4/S5.5: render + select + gizmo the ECS scene
		bool m_ShowNavCube  = true;    // S5.3: ViewCube overlay in the viewport corner
		bool m_NavCubeHovered = false; // cursor on the ViewCube (previous ImGui frame)
		bool m_GizmoActive  = false;   // Gizmo::IsUsing() from the previous frame
		bool m_GizmoOver    = false;   // Gizmo::IsOver() from the previous frame
		bool m_LmbWasDown   = false;   // click edge-detect for picking
		bool m_KeyFWasDown  = false;   // 'F' edge-detect (frame selection)
		bool m_KeyHomeWasDown = false; // 'Home' edge-detect (iso view)
		Cosmic::Gizmo::Operation m_GizmoOp    = Cosmic::Gizmo::Operation::Translate;
		Cosmic::Gizmo::Space     m_GizmoSpace = Cosmic::Gizmo::Space::World;
		bool  m_GizmoSnap  = false;
		float m_SnapValue  = 0.5f;     // world units (translate/scale) or degrees (rotate)

		void           RenderEditorIdPass();                                 // S5.4 id pre-pass
		void           HandleEditorPicking();                                // S5.4 click-to-select
		void           DrawSelectionOutline();                               // S5.4 outline
		Cosmic::Entity SelectedEntity() const;                               // from EntitySelection
		bool           ComputeEntityWorldAABB(Cosmic::Entity e, glm::vec3& mn, glm::vec3& mx) const;
		bool           ComputeSceneWorldAABB(glm::vec3& mn, glm::vec3& mx) const; // S5.2 frame scene

		// ---- Lighting v1 (S4.5) ----
		Cosmic::Ref<Cosmic::Material> m_LitMaterial;   // MeshLit.glsl material
		bool      m_LitAircraft = false;               // draw the aircraft lit (UBO lights)
		glm::vec3 m_SunColor{ 1.0f, 0.97f, 0.9f };
		float     m_SunIntensity = 1.0f;
		float     m_Shininess    = 48.0f;
		glm::vec3 m_P0Pos{ -3.0f, 2.5f, 2.0f };        // red point light
		glm::vec3 m_P1Pos{  3.0f, 2.5f, 2.0f };        // blue point light
		float     m_PointRadius  = 12.0f;

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
		bool  m_MaterialPad = false;   // S4.2: draw the pad via the custom-material path
		bool  m_EcsScene    = false;   // S4.3: render the ECS mesh scene via OnRender3D
		bool  m_ShowGltf    = false;   // S4.4b: render the imported glTF Duck model
		float m_Ambient     = 0.25f;
		glm::vec3 m_LightDir{ -0.4f, -1.0f, -0.25f };
	};

} // namespace Workspace
