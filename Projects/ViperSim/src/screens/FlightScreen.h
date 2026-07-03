#pragma once

// FlightScreen.h
//
// ============================================================================
// The Flight screen, grown from the P1 drop-test seat into the full workbench
// (plan §3): 3D viewport (orbit camera, pad/home/ROI markers, trajectory
// ribbon) + FPV INSET (S3.1 — second Renderer3D pass into its own FrameBuffer,
// shown via ImGui::Image) + the flight inspector:
//
//   arm/takeoff/mode buttons · gamepad status (E7) · wind + gusts · fault
//   injection (kill link, drop GPS, freeze pitot, motor-out, battery-low) ·
//   SITL <-> HIL backend dropdown with link UI + latency (P6) · gimbal rig
//   output (P7) · gate demos G1/G2/G3 with PASS/FAIL reports · failsafe
//   checklist ("every failsafe path exercised") · record/flush.
// ============================================================================

#include <Cosmic.h>
#include "SimHub.h"

#include <vector>

namespace Viper
{
	class FlightScreen
	{
	public:
		explicit FlightScreen(SimHub& hub);

		void OnAttach();
		void OnDetach();
		void OnUpdate(float ts);       // camera + 3D render passes (FPV first)
		void OnImGuiRender();          // inspector panel
		void OnEvent(Cosmic::Event& e);

	private:
		void BuildMeshes();
		void DrawWorld();              // world draws shared by main + FPV passes
		void RenderFpvPass();
		void RenderMainPass();
		void DrawStatusBlock();
		void DrawFlightControls();
		void DrawEnvironmentAndFaults();
		void DrawBackendSection();
		void DrawGateSection();

		SimHub& m_Hub;

		Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
		glm::vec2 m_ViewportSize{ 0.0f, 0.0f };

		Cosmic::Ref<Cosmic::Mesh> m_Fuselage;
		Cosmic::Ref<Cosmic::Mesh> m_WingMesh;
		Cosmic::Ref<Cosmic::Mesh> m_Pad;

		// S3.1 FPV inset — belly camera into a private FBO.
		Cosmic::Ref<Cosmic::FrameBuffer> m_FpvFbo;
		Cosmic::PerspectiveCamera        m_FpvCam{ 70.0f, 16.0f / 9.0f, 0.05f, 2000.0f };
		bool m_FpvEnabled = true;

		std::vector<glm::vec3> m_Trail;
		bool  m_AutoFollow = true;
		float m_TakeoffAlt = 10.0f;
		float m_DropHeight = 6.0f;
		glm::vec2 m_RoiField{ 100.0f, 0.0f };
	};
}
