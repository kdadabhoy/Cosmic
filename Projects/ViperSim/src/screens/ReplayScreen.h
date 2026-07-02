#pragma once

// ReplayScreen.h
//
// P1 acceptance: the recorded drop is replayable — load the session, scrub it,
// and watch the airframe re-fall in the 3D viewport driven by DataPlayer truth
// (not the live sim). Uses the engine's DataPlayer + TelemetryPanel for free
// (plan §3: "engine replay makes regression testing nearly free").

#include <Cosmic.h>
#include "SimHub.h"

#include <vector>

namespace Viper
{
	class ReplayScreen
	{
	public:
		explicit ReplayScreen(SimHub& hub);

		void OnAttach();
		void OnDetach();
		void OnUpdate(float ts);
		void OnImGuiRender();
		void OnEvent(Cosmic::Event& e);

	private:
		void LoadLatest();
		void RebuildTrail();

		SimHub& m_Hub;
		Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
		glm::vec2 m_ViewportSize{ 0.0f, 0.0f };

		Cosmic::Ref<Cosmic::Mesh> m_Body;
		Cosmic::Ref<Cosmic::Mesh> m_Pad;

		std::vector<glm::vec3> m_Trail;   // full recorded path (render frame)
		bool m_Loaded = false;
	};
}
