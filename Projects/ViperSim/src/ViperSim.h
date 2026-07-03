#pragma once

// ViperSim.h
//
// ============================================================================
// ViperSim — UAV simulator & flight-computer workbench (root manager, P0).
// ============================================================================
//
// Homescreen tile menu -> screens (SF_Telem pattern). All five screens are
// live as of P2–P5: Flight (viewport + FPV inset + gates), Tuning (P2),
// Energy (P3), Transition (P4), Replay (P1). All screens share ONE SimHub so
// dynamics/config/recording state persists across switches.
// ============================================================================

#include <Cosmic.h>

#include "SimHub.h"

#include <memory>

namespace Viper
{
	class FlightScreen;
	class ReplayScreen;
	class TuningScreen;
	class EnergyScreen;
	class TransitionScreen;

	class ViperSim : public Cosmic::Layer
	{
	public:
		ViperSim();
		virtual ~ViperSim() override = default;

		virtual void OnAttach()                override;
		virtual void OnDetach()                override;
		virtual void OnUpdate(float ts)        override;
		virtual void OnFixedUpdate(float dt)   override;
		virtual void OnImGuiRender()           override;
		virtual void OnEvent(Cosmic::Event& e) override;

		enum Screen { SCREEN_HOME = 0, SCREEN_FLIGHT, SCREEN_TUNING, SCREEN_ENERGY, SCREEN_TRANSITION, SCREEN_REPLAY, SCREEN_COUNT };

	private:
		void SetScreen(Screen s);
		void DrawHomescreen();
		void DrawTopPanel();
		void ApplyDockLayout();
		int  DockStateKey() const;    // screen -> re-dock trigger (SF_Telem pattern)

		SimHub m_Hub;

		std::unique_ptr<FlightScreen>     m_Flight;
		std::unique_ptr<ReplayScreen>     m_Replay;
		std::unique_ptr<TuningScreen>     m_Tuning;
		std::unique_ptr<EnergyScreen>     m_Energy;
		std::unique_ptr<TransitionScreen> m_Transition;

		Screen m_Screen      = SCREEN_HOME;
		int    m_AppliedDock = -1;

		bool Uses3DViewport() const { return m_Screen == SCREEN_FLIGHT || m_Screen == SCREEN_REPLAY; }
	};
}
