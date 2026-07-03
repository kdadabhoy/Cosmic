#pragma once

// EnergyScreen.h
//
// ============================================================================
// P3 — the Energy screen (plan §2.5): "the first genuinely useful output".
// Answers the proposal's power questions BEFORE parts are purchased:
// hover ≈230 W vs cruise ≈106 W, endurance splits for a mission profile.
// Divergence from the spreadsheet is a FINDING, not a bug — both are shown.
// ============================================================================

#include <Cosmic.h>
#include "SimHub.h"

#include <vector>

namespace Viper
{
	class EnergyScreen
	{
	public:
		explicit EnergyScreen(SimHub& hub);

		void OnAttach() {}
		void OnDetach() {}
		void OnUpdate(float ts);      // accumulate per-regime power stats
		void OnImGuiRender();
		void OnEvent(Cosmic::Event&) {}

	private:
		struct RegimeStat
		{
			double powerSum = 0.0;
			int    count = 0;
			float  Avg(float fallback) const
			{
				return count > 30 ? static_cast<float>(powerSum / count) : fallback;
			}
			bool Measured() const { return count > 30; }
		};

		SimHub& m_Hub;

		// Power history for the plot.
		std::vector<float> m_T, m_P, m_Blend;
		float m_Clock = 0.0f;

		RegimeStat m_Hover, m_Cruise, m_Orbit;

		// Mission profile inputs (minutes).
		float m_MinHover = 3.0f, m_MinCruise = 30.0f, m_MinOrbit = 15.0f;
	};
}
