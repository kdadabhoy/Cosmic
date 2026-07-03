#pragma once

// TransitionScreen.h
//
// ============================================================================
// P4 — the transition workbench: state machine visualizer + blend traces
// (doc 04 §2.4.2 "instrument everything"). Shows the live machine
// HOVER → ACCEL → BLEND → CRUISE (and back through DECEL/FLARE), plots
// blend/airspeed/altitude/pitch, and hosts the G2 gate run + report.
// ============================================================================

#include <Cosmic.h>
#include "SimHub.h"

#include <vector>

namespace Viper
{
	class TransitionScreen
	{
	public:
		explicit TransitionScreen(SimHub& hub);

		void OnAttach() {}
		void OnDetach() {}
		void OnUpdate(float ts);
		void OnImGuiRender();
		void OnEvent(Cosmic::Event&) {}

	private:
		void DrawStateMachine();
		void DrawTraces();

		SimHub& m_Hub;

		struct Sample { float t, blend, airspeed, altAgl, pitchDeg; };
		std::vector<Sample> m_Hist;
		float m_Clock = 0.0f;
	};
}
