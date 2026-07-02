#pragma once

// FlightScreen.h
//
// P1 drop-test viewport: an orbit camera over a grid + ground pad, the airframe
// (primitive mesh) falling under gravity via SimHub/ComposableDynamics, with
// live truth readout and Drop/Reset controls. The recorded run is replayable in
// the Replay screen (P1 acceptance).
//
// This is also the seat the full Flight screen grows into (FPV inset + PFD at
// P3/P5) — for now it proves the dynamics + recorder are wired end to end.

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
		void OnUpdate(float ts);       // camera + 3D render pass
		void OnImGuiRender();          // inspector panel
		void OnEvent(Cosmic::Event& e);

	private:
		void BuildMeshes();
		void RenderScene();

		SimHub& m_Hub;

		Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
		glm::vec2 m_ViewportSize{ 0.0f, 0.0f };

		Cosmic::Ref<Cosmic::Mesh> m_Body;   // airframe stand-in (box)
		Cosmic::Ref<Cosmic::Mesh> m_Pad;    // ground pad (plane)

		std::vector<glm::vec3> m_Trail;      // render-frame fall path
		float m_DropHeight = 6.0f;
		bool  m_AutoFollow = true;           // keep the camera target on the body
	};
}
