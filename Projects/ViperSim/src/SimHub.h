#pragma once

// SimHub.h
//
// ============================================================================
// SimHub — the simulation backbone shared across all ViperSim screens (plan §3).
// ============================================================================
//
// Owns: the config (E10 viper.toml), the active IDynamics, the DataRecorder +
// DataPlayer, the flight mode, and (later) the viper-fc bridge + SerialLink for
// HIL. Screens read/drive the sim through this one object so state persists
// across screen switches — the SF_Telem shared-hub pattern.
//
// P0/P1 scope: config load, ComposableDynamics, drop-test run + record, replay
// load. viper-fc / SimHal / HIL are reserved hooks (comments mark the seams).
// ============================================================================

#include <Cosmic.h>

#include "sim/ComposableDynamics.h"
#include "fc_glue/telemetry_schema.h"

#include <memory>
#include <string>

namespace Viper
{
	class SimHub
	{
	public:
		SimHub();

		// Load viper.toml and (re)build the dynamics from it. Safe to call again
		// to hot-reload parameters. Returns false only if dynamics couldn't be
		// created (config missing degrades to built-in defaults, still true).
		bool LoadConfig(const std::string& path = "project://config/viper.toml");

		Cosmic::Ref<Cosmic::Config> GetConfig() const { return m_Config; }
		const BodyParams& GetBodyParams() const { return m_Params; }

		// --- Drop-test control (P1) ------------------------------------------
		// Reset the vehicle to `dropHeight` m above ground, at rest, and begin a
		// fresh recording.
		void StartDrop(float dropHeightM);
		void ResetDrop();                    // re-drop from the last height
		bool IsRunning() const { return m_Running; }

		// Advance the sim by dt (call from OnFixedUpdate). Records truth each
		// tick while running; auto-stops when the body has settled.
		void Step(float dt);

		const RigidState& Truth() const { return m_Dynamics->GetTruth(); }
		IDynamics& Dynamics() { return *m_Dynamics; }
		float AltitudeAgl() const { return m_Dynamics->AltitudeAgl(); }
		float RunTime() const { return m_RunTime; }

		// --- Recording / replay ----------------------------------------------
		Cosmic::DataRecorder& Recorder() { return m_Recorder; }
		Cosmic::DataPlayer&   Player()   { return m_Player; }

		// Flush the current recording to user://recordings/<name> and return the
		// written folder (for the Replay screen to load).
		std::string FlushRecording();
		const std::string& LastRecordingPath() const { return m_LastRecordingPath; }

		FlightMode Mode() const { return m_Mode; }
		void SetMode(FlightMode m) { m_Mode = m; }

	private:
		void RegisterEntities();
		void RecordTruth();

		Cosmic::Ref<Cosmic::Config>          m_Config;
		BodyParams                           m_Params;
		std::unique_ptr<ComposableDynamics>  m_Dynamics;

		Cosmic::DataRecorder m_Recorder;
		Cosmic::DataPlayer   m_Player;
		bool                 m_EntitiesRegistered = false;
		uint32_t             m_TruthId = 0;
		uint32_t             m_FcId    = 0;

		bool  m_Running     = false;
		float m_RunTime     = 0.0f;
		float m_DropHeight  = 6.0f;
		float m_SettleTimer = 0.0f;
		std::string m_LastRecordingPath;

		FlightMode m_Mode = FlightMode::Idle;
	};
}
