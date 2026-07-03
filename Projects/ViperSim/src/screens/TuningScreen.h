#pragma once

// TuningScreen.h
//
// ============================================================================
// P2 — the controller workbench: live gains + step-response plots + the
// offline replay-through-FC regression runner (doc 04 §1 "determinism bonus").
// ============================================================================
//
// Live gains edit viperfc::FcParams on the SITL FlightComputer directly (HIL:
// gains live on the board — panel says so). Step buttons issue
// CommandAttitudeStep; the response ring buffer feeds ImPlot.
//
// Replay-through-FC: loads a recorded session's "sensors" entity, streams the
// frames through a FRESH FlightComputer, and diffs its actuator outputs
// against the recorded "fc" channels — controller regression with no
// simulator in the loop.
// ============================================================================

#include <Cosmic.h>
#include "SimHub.h"

#include <vector>

namespace Viper
{
	class TuningScreen
	{
	public:
		explicit TuningScreen(SimHub& hub);

		void OnAttach() {}
		void OnDetach() {}
		void OnUpdate(float ts);       // sample the response ring buffer
		void OnImGuiRender();
		void OnEvent(Cosmic::Event&) {}

	private:
		void DrawGains();
		void DrawStepResponse();
		void DrawReplayThroughFc();
		void RunReplayThroughFc();

		SimHub& m_Hub;

		// Response history ring (seconds of samples at frame rate).
		struct Sample
		{
			float t, attErrDeg, rollDeg, pitchDeg, yawDeg;
			float motorR, motorL, servoR, servoL;
		};
		std::vector<Sample> m_Hist;
		float m_Clock = 0.0f;

		float m_StepRollDeg  = 20.0f;
		float m_StepPitchDeg = 0.0f;
		float m_StepHold     = 3.0f;

		// Replay-through-FC results.
		bool  m_ReplayRan = false;
		float m_ReplayMaxMotorDiff = 0.0f;
		float m_ReplayMaxServoDiff = 0.0f;
		int   m_ReplayFrames = 0;
		std::vector<float> m_ReplayT, m_ReplayLiveM0, m_ReplayOffM0;
	};
}
